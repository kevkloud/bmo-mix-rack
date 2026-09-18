#include "modules/tune/dsp/Detector.h"
#include <algorithm>
#include <cmath>

namespace bmo::tune
{

namespace
{
    int nextPowerOfTwo (int n)
    {
        int size = 1;
        while (size < n)
            size <<= 1;
        return size;
    }

    double dbToGain (double db) { return std::pow (10.0, db / 20.0); }
}

double Detector::parabolicOffset (double left, double centre, double right) noexcept
{
    // The vertex of the parabola through (-1, left), (0, centre), (1, right).
    // Same formula for a minimum of YIN's d' and a maximum of the NSDF.
    const auto denominator = left - 2.0 * centre + right;

    if (! std::isfinite (denominator) || std::abs (denominator) < 1.0e-12)
        return 0.0;

    const auto offset = 0.5 * (left - right) / denominator;

    // A vertex outside the bracket means the three points were not a peak
    // (or a trough) at all; trust the middle sample rather than extrapolate.
    return std::isfinite (offset) ? std::clamp (offset, -1.0, 1.0) : 0.0;
}

void Detector::prepare (double rate, const Settings& s)
{
    sampleRate = rate;

    // About 12 kHz for the coarse pass at every supported rate: 4 at 44.1
    // and 48, 8 at 96, 16 at 192. That keeps harmonics up to ~3.5 kHz, which
    // is plenty to bracket a period, at a sixteenth of the full-rate cost.
    decimation = std::max (1, (int) std::lround (rate / 12000.0));
    coarseRate = rate / decimation;
    antiAlias.prepare (rate, std::min (3000.0, 0.3 * coarseRate));

    // Allocate for the widest range any setting can ask for, so a
    // pitch-range change on the audio thread only moves the active window.
    const auto capMinCoarse = std::max (2, (int) std::floor (coarseRate / kCapacityMaxHz) - 1);
    const auto capMaxCoarse = (int) std::ceil (coarseRate / kCapacityMinHz) + 2;

    // Every lag is recomputed from scratch at least every quarter second.
    kernel.prepare (capMinCoarse, capMaxCoarse, (int) std::lround (coarseRate * 0.25));
    coarseNsdf.assign ((size_t) capMaxCoarse + 2, 0.0);

    // The fine pass reads a one-period window at a lag of up to a period and
    // a bit: 2 x the longest lag plus the refinement bracket, with room.
    const auto capMaxFull = (int) std::ceil (rate / kCapacityMinHz) + decimation + 2;
    fullRing.assign ((size_t) nextPowerOfTwo (2 * capMaxFull + 4 * decimation + 16), 0.0f);
    fullMask = (int) fullRing.size() - 1;
    lowRing.assign (fullRing.size(), 0.0f);
    cumSum.assign (fullRing.size(), 0.0);
    cumSq.assign (fullRing.size(), 0.0);

    baseHop = std::max (1, (int) std::lround (rate * 0.0005));
    zcrCoeff = std::exp (-1.0 / (rate * 0.010));

    configured = false;
    setSettings (s);
    reset();
}

void Detector::setSettings (const Settings& s) noexcept
{
    const auto minHz = std::clamp (std::min (s.minHz, s.maxHz * 0.5), kCapacityMinHz, kCapacityMaxHz * 0.5);
    const auto maxHz = std::clamp (std::max (s.maxHz, minHz * 2.0), minHz * 2.0, kCapacityMaxHz);
    const auto rangeChanged = ! configured || minHz != settings.minHz || maxHz != settings.maxHz;

    settings = s;
    settings.minHz = minHz;
    settings.maxHz = maxHz;

    if (! rangeChanged)
        return;

    configured = true;

    // Below the range floor, never at it: 0.6 of the lowest note keeps the
    // fundamental within a dB or so while taking out DC (which reads as
    // "periodic at every lag") and the rumble and kick bleed that would
    // otherwise sit under a period and fail the whole-cycle mean test.
    highpass.makeHighpass (sampleRate, 0.6 * minHz, 0.70710678);

    // One lag of slack at the top of the range and two at the bottom, so the
    // extreme notes still have a neighbour on each side to interpolate with.
    kernel.setLagRange (std::max (2, (int) std::floor (coarseRate / maxHz) - 1),
                        (int) std::ceil (coarseRate / minHz) + 2);

    minFullLag = std::max (2, (int) std::floor (sampleRate / maxHz) - decimation - 2);
    maxFullLag = (int) std::ceil (sampleRate / minHz) + decimation + 2;

    // A new range is a new question; nothing held from the old one applies.
    current = {};
    heldPeriod = candidatePeriod = 0.0;
    voicedRun = unvoicedRun = 0;
    guardFactor = 1;
    guardLag = 0;
    guardPending = 0;
    guardPeriod = 0.0;
    leapHoldFrom = -1;
}

void Detector::reset()
{
    highpass.reset();
    antiAlias.reset();
    kernel.reset();
    std::fill (fullRing.begin(), fullRing.end(), 0.0f);
    std::fill (lowRing.begin(), lowRing.end(), 0.0f);
    std::fill (cumSum.begin(), cumSum.end(), 0.0);
    std::fill (cumSq.begin(), cumSq.end(), 0.0);
    runningSum = runningSq = 0.0;
    rebaseCountdown = kRebaseInterval;
    std::fill (coarseNsdf.begin(), coarseNsdf.end(), 0.0);
    fullWrite = 0;
    decimationPhase = 0;
    zcrState = 0.0;
    energyState = 0.0;
    previousSign = 1.0f;
    hopCountdown = baseHop;
    lastHop = baseHop;
    samplesSeen = 0;
    leapHoldFrom = -1;
    lastOnsetAt = -1'000'000;
    voicedRun = unvoicedRun = 0;
    heldPeriod = 0.0;
    guardFactor = 1;
    guardLag = 0;
    guardPending = 0;
    guardPeriod = 0.0;
    guardDueAt = 0;
    current = {};
    evaluated = false;
}

void Detector::push (float input) noexcept
{
    const auto x = std::isfinite (input) ? (double) input : 0.0;
    const auto y = highpass.process (x);

    runningSum += y;
    runningSq += y * y;
    fullRing[(size_t) fullWrite] = (float) y;
    cumSum[(size_t) fullWrite] = runningSum;
    cumSq[(size_t) fullWrite] = runningSq;
    fullWrite = (fullWrite + 1) & fullMask;

    // The running sums grow without bound (the squared one linearly), and a
    // window's sum is the difference of two of them, so their magnitude is
    // the error floor. Rebasing every 2^20 samples keeps that floor below
    // 1e-9 of full scale forever, at the cost of one pass over the ring
    // every 20 seconds. Only differences are ever read, so it is invisible.
    if (--rebaseCountdown <= 0)
    {
        rebaseCountdown = kRebaseInterval;
        const auto baseSum = runningSum, baseSq = runningSq;
        for (size_t i = 0; i < cumSum.size(); ++i)
        {
            cumSum[i] -= baseSum;
            cumSq[i] -= baseSq;
        }
        runningSum = runningSq = 0.0;
    }

    // Zero crossings and energy, both as one-pole averages over ~10 ms, so
    // neither needs a window of its own.
    const auto sign = y >= 0.0 ? 1.0f : -1.0f;
    const auto crossed = sign != previousSign ? 1.0 : 0.0;
    previousSign = sign;
    zcrState = zcrCoeff * zcrState + (1.0 - zcrCoeff) * crossed;
    energyState = zcrCoeff * energyState + (1.0 - zcrCoeff) * y * y;

    const auto filtered = antiAlias.process (y);
    lowRing[(size_t) ((fullWrite - 1) & fullMask)] = (float) filtered;

    if (++decimationPhase >= decimation)
    {
        decimationPhase = 0;
        kernel.push ((float) filtered);
    }

    ++samplesSeen;
    current.onset = false;
    evaluated = false;

    if (--hopCountdown <= 0)
    {
        evaluate();
        evaluated = true;

        // A quarter period between evaluations once locked: the fine pass
        // costs a period's worth of multiplies, so this keeps its cost per
        // second flat across the range instead of 7x higher at 80 Hz.
        lastHop = current.voiced && heldPeriod > 0.0
                    ? std::max (baseHop, (int) std::lround (heldPeriod * 0.25))
                    : baseHop;
        hopCountdown = lastHop;
    }
}

void Detector::evaluate() noexcept
{
    double coarseLag = 0.0, period = 0.0, clarity = 0.0;

    const auto found = coarseSearch (coarseLag)
                    && refine (coarseLag * decimation, period, clarity);

    if (found)
        preferWholeCycle (period, clarity);
    else
        clarity = 0.0;

    // Guard 6. A leap to a SHORTER period -- the direction a harmonic error
    // takes -- has to beat the period being held, on a window long enough to
    // judge them both. It never blocks a move to a longer period: that is
    // guard 4's direction, and it is how an octave error is recovered from,
    // so vetoing it would latch the very fault this guard exists to stop.
    //
    // The veto expires: a run of them lasts at most kLeapHoldMs, after which
    // an estimate that keeps insisting is taken. So the guard can delay a
    // real leap by an evaluation or two, and can never latch.
    {
        const auto holdSamples = (std::int64_t) (0.001 * settings.leapVetoHoldMs * sampleRate);
        const auto runAllows = leapHoldFrom < 0 || samplesSeen - leapHoldFrom < holdSamples;

        if (found && current.voiced && heldPeriod > 1.0 && runAllows
            && std::log2 (heldPeriod / period) > settings.leapVetoCents / 1200.0
            && heldSurvives (period))
        {
            // The held period exactly, never a fresh reading near it. A
            // reading taken on material that no longer repeats at this lag
            // wanders, and the next evaluation is then compared against the
            // wandering value: without this the held period ran 421 -> 600 Hz
            // over ten evaluations at a note transition in the reference
            // stimulus. Re-reading it inside a tone-wide clamp was tried too
            // and was worse on the takes -- Failure's splices taken while the
            // detector had lost the period went 2 of 27 to 4 of 32.
            period = heldPeriod;

            if (leapHoldFrom < 0)
                leapHoldFrom = samplesSeen;
        }
        else
        {
            leapHoldFrom = -1;
        }
    }

    const auto rms = std::sqrt (std::max (0.0, energyState));
    const auto gate = dbToGain (settings.gateDb);
    const auto zcrHz = zcrState * sampleRate;

    // A candidate has to hold still before it can open voicing. In the first
    // period of a low note the window has not yet seen a whole cycle, and a
    // short lag across a smooth arc of the waveform can read 0.9 clarity --
    // but that false period moves from one evaluation to the next as the arc
    // does, while a real one stays put. Measured on a 147 Hz sine: the false
    // candidates ran 1143, 1655, 1043, 750 Hz on consecutive hops.
    const auto stable = found && candidatePeriod > 0.0
                     && std::abs (std::log2 (period / candidatePeriod)) < settings.stabilityCents / 1200.0;

    const auto frameVoiced = stable && clarity > settings.clarityHi
                          && rms > gate && zcrHz < settings.zcrMaxHz;
    const auto frameUnvoiced = ! found || clarity < settings.clarityLo || rms < gate * 0.5;

    candidatePeriod = found ? period : 0.0;
    current.candidate = candidatePeriod;
    updateVoicing (frameVoiced, frameUnvoiced);

    current.clarity = clarity;
    current.rms = rms;

    // On unvoiced, the period freezes where it was (spec §3.4): the engines
    // keep free-running on it, and the correction amount is what fades.
    if (found && current.voiced && clarity >= settings.clarityLo)
    {
        heldPeriod = period;
        current.period = period;
        current.hz = sampleRate / period;
    }
}

void Detector::updateVoicing (bool frameVoiced, bool frameUnvoiced) noexcept
{
    // Counters in samples, sized in periods: open after a quarter period of
    // agreement, close after two. Opening fast is the time-to-lock budget --
    // and a frame only counts toward it if its candidate is stable against
    // the previous one, so "one frame" is already two agreeing estimates.
    // Half a period was the first choice and cost 880 Hz a whole extra hop
    // (3.1 periods to lock rather than 2.6) for no robustness the stability
    // test was not already providing. Closing slow is what lets a vibrato's
    // trough or a soft consonant inside a word pass without dropping the note.
    const auto periodForCounts = candidatePeriod > 0.0 ? candidatePeriod
                               : (heldPeriod > 0.0 ? heldPeriod : sampleRate / 200.0);
    const auto attack  = std::max (1, (int) std::lround (0.25 * periodForCounts));
    const auto release = std::max (2 * baseHop, (int) std::lround (2.0 * periodForCounts));

    if (! current.voiced)
    {
        voicedRun = frameVoiced ? voicedRun + lastHop : 0;

        if (voicedRun >= attack)
        {
            current.voiced = true;
            current.onset = true;
            lastOnsetAt = samplesSeen;
            unvoicedRun = 0;

            // A transition is when the running sums are most likely to be
            // carrying a louder passage's residue; refresh them all now
            // rather than wait for the schedule (spec §3.1).
            kernel.recomputeAll();
        }
    }
    else
    {
        unvoicedRun = frameUnvoiced ? unvoicedRun + lastHop : 0;

        if (unvoicedRun >= release)
        {
            current.voiced = false;
            voicedRun = 0;
            kernel.recomputeAll();
        }
    }
}

bool Detector::coarseSearch (double& coarseLag) noexcept
{
    const auto lo = kernel.getMinLag();
    const auto hi = kernel.getMaxLag();

    // A relative floor: a lag whose two-period energy is this far below the
    // signal's own level is reading silence at the start of a note, and 0/0
    // there is not a correlation.
    const auto floor = std::max (1.0e-12, 1.0e-9 * kernel.energyAt (hi));

    for (int L = lo; L <= hi; ++L)
        coarseNsdf[(size_t) L] = kernel.nsdfAt (L, floor);

    // McLeod's key maxima: the highest point of each positive lobe between
    // zero crossings. The lobe the scan starts inside only counts if its
    // maximum is interior -- at short lags a lowpassed signal's NSDF starts
    // near 1 and falls, and that falling edge is not a period.
    struct Candidate { int lag; double value; };
    Candidate candidates[64];
    int count = 0;

    bool inLobe = false;
    int lobeLag = lo;
    double lobeValue = -2.0;
    // Closing a lobe records it as a candidate. It does NOT end the scan.
    //
    // It used to: a lobe whose RAW correlation cleared settings.earlyExit
    // (0.95) broke the loop, and every octave guard downstream then had to
    // work from whatever the scan had reached. That is what the pops were.
    // The coarse pass's window IS the lag -- MPM's form, and what buys the
    // fast lock -- so at a short lag it spans about a millisecond, roughly one
    // cycle of a vowel's first formant, and a formant ringing in there
    // correlates as well as a period does. On Failure at 17.409 s the list
    // held ONE entry, 787.5 Hz, on a singer at 219; the real period, at 0.974,
    // was never scored. 787.5 is no harmonic of 219 -- it is 3.67x -- so
    // guard 4 could not climb back either; it steps by 2 and 3.
    //
    // The exit also bought nothing: coarseNsdf[] is filled for EVERY lag
    // before this loop runs, so it only ever truncated an O(1)-per-lag walk
    // and a cumulative-sum test. Retired 2026-09-14, heard in round eight.
    const auto closeLobe = [&] ()
    {
        inLobe = false;
        if (lobeLag <= lo || lobeLag >= hi || count >= 64)
            return;

        if (! spansWholeCycles (lobeLag * decimation))
            return;

        candidates[count++] = { lobeLag, lobeValue };
    };

    for (int L = lo; L <= hi; ++L)
    {
        const auto v = coarseNsdf[(size_t) L];

        if (v > 0.0)
        {
            if (! inLobe)
            {
                inLobe = true;
                lobeValue = v;
                lobeLag = L;
            }
            else if (v > lobeValue)
            {
                lobeValue = v;
                lobeLag = L;
            }
        }
        else if (inLobe)
        {
            closeLobe();
        }
    }

    if (inLobe)
        closeLobe();

    if (count == 0)
        return false;

    // Temporal continuity (guard 3): weight each candidate by its distance in
    // octaves from the held period, except just after an onset, where a real
    // leap must not be argued out of.
    const auto graceSamples = (std::int64_t) (settings.onsetGraceMs * 0.001 * sampleRate);
    const auto useContinuity = current.voiced && heldPeriod > 0.0
                            && samplesSeen - lastOnsetAt > graceSamples;

    double best = -1.0e9;
    double scores[64];

    for (int i = 0; i < count; ++i)
    {
        auto score = candidates[i].value;

        if (useContinuity)
            score -= settings.continuityWeight
                   * std::abs (std::log2 (candidates[i].lag * decimation / heldPeriod));

        scores[i] = score;
        best = std::max (best, score);
    }

    // Guard 2, McLeod's peak-fraction rule: the smallest lag that is nearly
    // as good as the best is the fundamental; the best itself is often a
    // multiple of it.
    int chosen = 0;
    const auto threshold = settings.peakFraction * best;

    for (int i = 0; i < count; ++i)
    {
        if (scores[i] >= threshold)
        {
            chosen = i;
            break;
        }
    }

    int tau = candidates[chosen].lag;

    // Guard 1, sub-multiples: if a half or a third of the chosen lag matches
    // nearly as well by the patent's own measure, the chosen one was a
    // multiple of the period.
    for (int k = 2; k <= 3; ++k)
    {
        const auto centre = (int) std::lround ((double) tau / k);
        if (centre - 1 <= lo)
            break;

        int subLag = centre;
        for (int L = centre - 1; L <= centre + 1; ++L)
            if (coarseNsdf[(size_t) L] > coarseNsdf[(size_t) subLag])
                subLag = L;

        const auto dTau = 1.0 - coarseNsdf[(size_t) tau];
        const auto dSub = 1.0 - coarseNsdf[(size_t) subLag];

        if (subLag > lo && subLag < hi && dSub < settings.subMultipleRatio * dTau)
            tau = subLag;
    }
    coarseLag = tau + parabolicOffset (coarseNsdf[(size_t) tau - 1],
                                       coarseNsdf[(size_t) tau],
                                       coarseNsdf[(size_t) tau + 1]);
    return true;
}

bool Detector::spansWholeCycles (int lag) const noexcept
{
    // The coarse pass's window is the lag itself -- that is the patent's form
    // and what buys the fast lock -- so at a short lag it sees only a
    // fragment of a slow wave, and a fragment near a crest correlates with
    // the fragment before it well enough to beat the real period under the
    // smallest-lag rule. Measured on a 110 Hz sine: a 1230 Hz lobe at 0.98
    // every half cycle.
    //
    // What tells them apart is cheap. The analysis signal is highpassed, so a
    // real period of it averages to zero, in both halves of the two-period
    // span; a fragment of a crest does not. The means come off running sums
    // in O(1), with no window of their own.
    const auto meanSquaredOverPower = [this] (int newest, int length)
    {
        const auto s = cumSumAt (newest) - cumSumAt (newest + length);
        const auto q = cumSqAt (newest) - cumSqAt (newest + length);

        if (q <= 1.0e-20)
            return 1.0;

        return (s * s) / ((double) length * q);
    };

    const auto limit = settings.cycleMeanLimit * settings.cycleMeanLimit;

    return meanSquaredOverPower (0, lag) < limit
        && meanSquaredOverPower (lag, lag) < limit;
}

double Detector::fullNsdf (int lag, int window) const noexcept
{
    double r = 0.0, m = 0.0;

    for (int j = 0; j < window; ++j)
    {
        const double a = fullAt (j);
        const double b = fullAt (j + lag);
        r += a * b;
        m += a * a + b * b;
    }

    return m > 1.0e-20 ? 2.0 * r / m : 0.0;
}

double Detector::lowNsdf (int lag, int window) const noexcept
{
    double r = 0.0, m = 0.0;

    // Stepped, not every sample. lowRing carries the anti-alias lowpass's
    // output, which is band-limited to 3 kHz, so summing it at the host rate
    // oversamples the correlation by rate/6000 -- 7x at 44.1 kHz and 32x at
    // 192 kHz -- for an answer that does not change. The cost of the guards
    // that read it therefore grew with the SQUARE of the sample rate: guard 6
    // took the 192 kHz / 128 bench p99 from 20 % to 32 %. Stepping by the
    // decimation factor is the same factor the coarse pass already uses, and
    // makes the cost flat across the range.
    const auto step = std::max (1, decimation);

    for (int j = 0; j < window; j += step)
    {
        const double a = lowAt (j);
        const double b = lowAt (j + lag);
        r += a * b;
        m += a * a + b * b;
    }

    return m > 1.0e-20 ? 2.0 * r / m : 0.0;
}

bool Detector::refine (double centre, double& period, double& clarity) noexcept
{
    // The coarse lag is good to half a coarse sample, which is half the
    // decimation factor at the full rate; search a little wider than that.
    const auto reach = decimation + 1;
    const auto window = std::clamp ((int) std::lround (centre), 8, maxFullLag);

    const auto lo = std::max (minFullLag, (int) std::floor (centre) - reach);
    const auto hi = std::min (maxFullLag, (int) std::ceil (centre) + reach);

    if (hi <= lo)
        return false;

    // Coarse to fine inside the bracket. The bracket is 2 x decimation wide,
    // which is 37 lags at 192 kHz, each a period of multiply-adds -- about
    // 260k in one evaluation, landing in one host block. Stepping by half the
    // decimation factor and then refining around the best visits ~19 lags
    // instead; the NSDF peak spans several samples at these rates, so the
    // stride cannot step over it.
    const auto stride = std::max (1, decimation / 2);
    int bestLag = lo;
    double bestValue = -2.0;

    for (int L = lo; L <= hi; L += stride)
    {
        const auto v = fullNsdf (L, window);
        if (v > bestValue)
        {
            bestValue = v;
            bestLag = L;
        }
    }

    if (stride > 1)
    {
        const auto centreLag = bestLag;
        for (int L = std::max (lo, centreLag - stride + 1); L <= std::min (hi, centreLag + stride - 1); ++L)
        {
            if (L == centreLag)
                continue;
            const auto v = fullNsdf (L, window);
            if (v > bestValue)
            {
                bestValue = v;
                bestLag = L;
            }
        }
    }

    const auto left  = fullNsdf (bestLag - 1, window);
    const auto right = fullNsdf (bestLag + 1, window);
    const auto offset = parabolicOffset (left, bestValue, right);

    period = bestLag + offset;
    clarity = std::clamp (bestValue - 0.25 * (left - right) * offset, 0.0, 1.0);
    return period > 1.0;
}

double Detector::lowPeak (double centre, int reach, int window, int& atLag) const noexcept
{
    const auto c = (int) std::lround (centre);
    const auto lo = std::max (minFullLag + 1, c - reach), hi = std::min (maxFullLag - 1, c + reach);
    atLag = std::clamp (c, lo, hi);
    double best = -2.0;

    // Coarse to fine, as refine() does: every other lag first (below 3 kHz
    // the peak spans several samples, so a stride of 2 cannot step over it),
    // then the neighbours of the best.
    for (int L = lo; L <= hi; L += 2)
    {
        const auto v = lowNsdf (L, window);
        if (v > best)
        {
            best = v;
            atLag = L;
        }
    }

    const auto centreLag = atLag;
    for (const auto L : { centreLag - 1, centreLag + 1 })
    {
        if (L < lo || L > hi)
            continue;
        const auto v = lowNsdf (L, window);
        if (v > best)
        {
            best = v;
            atLag = L;
        }
    }

    const auto l = lowNsdf (atLag - 1, window), r = lowNsdf (atLag + 1, window);
    const auto offset = parabolicOffset (l, best, r);
    return best - 0.25 * (l - r) * offset;
}

bool Detector::multipleOf (double base, int& factor, int& lag) const noexcept
{
    for (int k = 2; k <= 3; ++k)
    {
        const auto longer = base * k;
        if (longer > maxFullLag - 2)
            return false;

        // Two of the longer periods: four cycles when the period is right,
        // enough for jitter to average out, where one was a coin toss (the
        // first version doubled a rough voice, 0.5 % jitter, on 8 of 768
        // evaluations).
        const auto window = std::min ((int) std::lround (2.0 * longer), fullMask - (int) std::lround (longer) - 4);

        // Searched, not assumed: a period read an octave up is itself off
        // (82.2 samples for a true 80 on the 300 Hz test voice), so its
        // double can miss the real period by several samples. The reach is
        // refine()'s own, scaled by k at the multiple.
        //
        // The period's own reading comes first, because it is cheap and it
        // usually ends the question: a clean period is under the floor, and
        // the multiple is then never searched for. That is what keeps the
        // guard inside the CPU budget -- searching every time took the
        // bench from 0.9 % to 1.5 % median at 48 kHz / 128.
        int hereLag = 0, longerLag = 0;
        const auto dHere = 1.0 - lowPeak (base, 2, window, hereLag);
        if (dHere <= settings.multipleFloor)
            return false;

        const auto dLonger = 1.0 - lowPeak (longer, k * decimation + 1, window, longerLag);

        // While the pitch moves, a longer lag loses correlation to the
        // movement itself, so on a slide the true period cannot beat its own
        // harmonic by 4x (Failure at 4.75 s: a scoop from 197 to 184 Hz read
        // at its third harmonic, on and off, for 20 ms). When the multiple is
        // the period the detector was just holding, being the more periodic
        // of the two is enough -- but only when the shorter lag is clearly
        // not a period (heldMultipleFloor).
        const auto held = current.voiced && heldPeriod > 0.0
                       && std::abs (std::log2 ((double) longerLag / heldPeriod)) < 60.0 / 1200.0
                       && dHere > settings.heldMultipleFloor;
        const auto ratio = held ? settings.heldMultipleRatio : settings.multipleRatio;

        if (dHere > settings.multipleFloor && dLonger < ratio * dHere)
        {
            // And the same answer over the most recent long period alone.
            // The long window looks back two of them, so right after an
            // instant leap it still holds mostly the old note -- which is
            // periodic at the old period, a multiple of the new one -- and it
            // kept the old note ~9 ms past a step (the corpus's instant octave
            // and fifth transitions lost 2-3 frames each). The recent stretch
            // already hears the new note, where the short period repeats, and
            // vetoes. On a steady voice read an octave up, both agree.
            const auto recent = std::max (8, (int) std::lround (longer));
            int recentHereLag = 0, recentLongerLag = 0;
            const auto dHereRecent = 1.0 - lowPeak (base, 2, recent, recentHereLag);
            const auto dLongerRecent = 1.0 - lowPeak ((double) longerLag, 1, recent, recentLongerLag);

            if (dHereRecent > settings.multipleFloor && dLongerRecent < ratio * dHereRecent)
            {
                factor = k;
                lag = longerLag;
                return true;
            }
        }
    }

    return false;
}


/** Guard 6: a leap has to beat the period being held, on a window long
    enough to judge them both.

    Every guard above reads the coarse NSDF, whose window IS the lag (that is
    MPM's form, and what buys the fast lock). At a short lag that window is a
    millisecond or so, and a vowel's first formant ringing inside it correlates
    as well as a period does -- 0.96 on Failure at 17.409 s, where the voice is
    219 Hz and the lobe sits at 787 Hz, which is no harmonic of it at all. The
    scan's early exit then ends the search before the real period is ever
    scored, and guard 4 cannot climb back because 3.67 is not 2 or 3.

    So judge the two lags the one way that is fair: the same window, long
    enough for the longer of them, at the full rate. A formant lobe collapses
    there; a real period does not. And ask the most recent stretch as well, so
    a genuine leap -- where the new note is what the recent material actually
    repeats at -- is still allowed through, exactly as multipleOf() does. */
bool Detector::heldSurvives (double candidate) const noexcept
{
    if (heldPeriod <= 1.0 || candidate <= 1.0)
        return false;

    const auto longer = std::max (candidate, heldPeriod);
    const auto window = std::min ((int) std::lround (2.0 * longer),
                                  fullMask - (int) std::lround (longer) - 4);

    if (window < 8)
        return false;

    int a = 0, b = 0;
    const auto dNew  = 1.0 - lowPeak (candidate, 2, window, a);
    const auto dHeld = 1.0 - lowPeak (heldPeriod, 2, window, b);

    if (dHeld >= dNew)
        return false;   // the newcomer is at least as periodic; let it through

    // The long window still holds most of the old note right after a real
    // leap, so it always prefers the old period there. The most recent long
    // period alone already hears the new note; if the newcomer wins that, the
    // leap is real.
    const auto recent = std::max (8, (int) std::lround (longer));
    int c = 0, d = 0;
    const auto rNew  = 1.0 - lowPeak (candidate, 2, recent, c);
    const auto rHeld = 1.0 - lowPeak (heldPeriod, 2, recent, d);

    return rHeld < rNew;
}
void Detector::preferWholeCycle (double& period, double& clarity) noexcept
{
    // Guard 4, multiples: every guard above looks for a SHORTER period than
    // the one found, and the scan stops at the first lobe that clears 0.95.
    // So on a voice whose fundamental sits well under its second harmonic, a
    // half period -- read over a window only that long -- clears the bar,
    // and nothing ever asks whether twice it is the real period. Measured on
    // the 2026-09-11 shoot-out's Failure take: a D4 read at D5 on 8.5 % of
    // voiced frames and a twelfth up on 2 %, the estimate swinging by a
    // semitone either way every evaluation, because a half-period window
    // sees a different half of each real cycle each time. The engine splices
    // by the period it is given, so a harmonic's period is also a splice cut
    // at a fraction of the real cycle. VoiceTests rebuilds it.
    //
    // The test: over one window long enough for the longer period, is the
    // signal much less aperiodic (1 - NSDF) at k x the period than at the
    // period? A real period is no better at 2T than at T -- worse, as the
    // voice drifts and jitters over the longer lag -- so it is never doubled.
    // A half period of a real cycle is worse, because the odd harmonics turn
    // over at T/2. Read as a ratio, not a difference: on a near-pure second
    // harmonic the NSDF gap is tiny (0.009 on a 350 Hz voice with its
    // fundamental 26 dB down) while the aperiodicity is twenty times larger
    // at T/2 than at T -- the measure guard 1 uses the other way. The floor
    // keeps two near-perfect tones from being compared at rounding level.
    //
    // Both lags are read the same way -- the best integer lag near each, then
    // the parabola's peak -- on the anti-alias lowpass's output, not the full
    // band. At the full band a period that falls between samples is misread
    // by a different amount at each lag: an 880 Hz sawtooth is 54.5 samples,
    // half a sample off, while twice it is 109.09 and almost exact, so the
    // doubled lag won on rounding (the first version read 440 Hz, and doubled
    // 3 % of ordinary voices). Below 3 kHz the NSDF peak is broad enough for
    // the parabola to read true, and the odd harmonics are all still there.
    //
    // The decision is taken at most every 2 ms, and whenever the period has
    // moved, and held in between: it costs a few long correlations, and one
    // re-taken every quarter period would flip on a single unlucky hop.
    const auto moved = guardPeriod <= 0.0 || std::abs (std::log2 (period / guardPeriod)) > 30.0 / 1200.0;

    if (moved || samplesSeen >= guardDueAt)
    {
        guardPeriod = period;
        guardDueAt = samplesSeen + (std::int64_t) (0.002 * sampleRate);

        // Up to two steps, so a lock two octaves up (a fourth harmonic)
        // comes all the way down: each step takes 2 or 3 x what the last
        // one found.
        int found = 1, foundLag = 0;
        auto base = period;
        for (int step = 0; step < 2; ++step)
        {
            int k = 1, lag = 0;
            if (! multipleOf (base, k, lag))
                break;

            found *= k;
            foundLag = lag;
            base = (double) lag;
        }

        // A multiple that would move the detector to a period it is not
        // already holding is taken only when two runs agree -- a fresh 2 ms
        // of signal each. A lock on a harmonic is there run after run; a
        // chance alignment of a jittered voice is not (one 2 %-jitter
        // corpus voice lost two evaluations to it). One that lands on the
        // held period is taken at once: the raw search can alternate between
        // the true period and a harmonic from one evaluation to the next,
        // and waiting each time let the harmonic through every other time.
        const auto onHeld = found > 1 && heldPeriod > 0.0
                         && std::abs (std::log2 ((double) foundLag / heldPeriod)) < 60.0 / 1200.0;

        if (found > 1 && ! onHeld && found != guardPending)
        {
            guardPending = found;
            found = 1;
        }
        else
        {
            guardPending = 0;
        }

        guardFactor = found;
        guardLag = found > 1 ? foundLag : 0;
    }

    if (guardFactor > 1 && guardLag > 0)
    {
        double refined = 0.0, refinedClarity = 0.0;
        if (refine ((double) guardLag * period / guardPeriod, refined, refinedClarity))
        {
            period = refined;
            clarity = refinedClarity;
        }
    }
}

} // namespace bmo::tune
