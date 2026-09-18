#include "modules/tune/dsp/CorrectionLaw.h"
#include "modules/tune/dsp/Pitch.h"
#include <algorithm>
#include <cmath>

namespace bmo::tune
{

namespace
{
    double onePole (double sampleRate, double seconds)
    {
        return seconds > 0.0 ? 1.0 - std::exp (-1.0 / (sampleRate * seconds)) : 1.0;
    }
}

double CorrectionLaw::flexGain (double u, double flex) noexcept
{
    // The spec's knee (§4.3a) with its own curve corrected: it writes
    // ((u - u0) / (u1 - u0))^2 and calls it "smoothstep, C1 at both ends",
    // but a square has slope 2 / (u1 - u0) at u1, so the applied correction
    // e g(|e|) kinks where the knee meets full correction. The real
    // smoothstep, 3t^2 - 2t^3, is flat at both ends and is what is used.
    const auto f = std::clamp (flex, 0.0, 1.0);
    const auto u0 = 20.0 * f;   // leave alone below this many cents
    const auto u1 = 50.0 * f;   // correct fully above this many

    if (u1 <= 0.0 || u >= u1)
        return 1.0;
    if (u <= u0)
        return 0.0;

    const auto t = (u - u0) / (u1 - u0);
    return t * t * (3.0 - 2.0 * t);
}

void CorrectionLaw::prepare (double sampleRate)
{
    fs = sampleRate;
    slowCoeff = onePole (fs, 1.0 / (2.0 * kPi * 3.0));        // 3 Hz corner (spec §4.3b)
    decideCoeff = slowCoeff;
    confidenceCoeff = onePole (fs, 0.005);
    fadeOutSamples = std::max (1, (int) std::lround (0.010 * fs));
    setSettings (s);
    reset();
}

void CorrectionLaw::reset()
{
    pitchIn = pitchSlow = 0.0;
    pitchSlope = predicted = slopeFrom = 0.0;
    history[0] = history[1] = history[2] = {};
    haveHistory = 0;
    slopeAnchorPitch = 0.0;
    slopeAnchorAt = 0;
    haveAnchor = false;
    slopeAt = 0;
    haveSlopeFrom = false;
    havePitch = voiced = false;
    havePendingJump = jumpTaken = false;
    clarity = period = 0.0;
    note = -1;
    haveNote = false;
    pendingNote = -1;
    pendingNoteSince = samples = pendingBilledAt = 0;
    pendingCostCentMs = 0.0;
    target = targetGoal = 0.0;
    errorSlow = applied = confidence = gate = 0.0;
    st = {};
}

void CorrectionLaw::setSettings (const CorrectionSettings& settings) noexcept
{
    s = settings;
    s.refA = std::clamp (s.refA, 380.0, 480.0);
    s.vibratoAmount = std::clamp (s.vibratoAmount, 0.0, 1.5);

    // Exactly zero, not "very fast": at tau = 0 the pole is bypassed.
    retuneAlpha = s.retuneMs > 0.0 ? std::exp (-1.0 / (fs * s.retuneMs * 0.001)) : 0.0;
    dwellSamples = (long long) std::lround (std::max (0.0, s.noteDwellMs) * 0.001 * fs);
    transitionAlpha = s.noteTransitionMs > 0.0 ? std::exp (-1.0 / (fs * s.noteTransitionMs * 0.001)) : 0.0;
}

int CorrectionLaw::holdOrSwitch (double pitch, int candidate) noexcept
{
    // At vibrato 0 the decision reads the raw pitch, and a singer sitting
    // near the boundary between two scale notes crossed it with every
    // wobble: 71 of the note-name flips left on the Failure take were
    // between neighbours, and Frosty heard them as "hunting" and chose "hold
    // the note steadier" (2026-09-11). So a switch by a small margin has to
    // hold still for noteDwellMs first; a switch by a clear one -- the pitch
    // well across the midpoint, where every real step and leap arrives
    // within a few milliseconds -- is taken at once, as before.
    const auto mask = (NoteMask) (scaleMask (s.scale, s.key) & s.allowed);

    if (! haveNote || candidate == note || ! allows (mask, note))
    {
        pendingNote = -1;
        return candidate;
    }

    const auto margin = 100.0 * (std::abs (pitch - note) - std::abs (pitch - candidate));

    if (margin >= s.noteClearCents)
    {
        pendingNote = -1;
        return candidate;
    }

    if (candidate != pendingNote)
    {
        pendingNote = candidate;
        pendingNoteSince = pendingBilledAt = samples;
        pendingCostCentMs = 0.0;
        return note;
    }

    // The bill for holding: the pull this hold cannot afford, times how long
    // it has carried it. The engine's read drifts at the pull, and far enough
    // drift splices a period -- which is what round three heard as pops at
    // retune 20 ms (see noteHoldFreeCents and noteHoldBudgetCentMs).
    const auto pull = 100.0 * std::abs (pitch - note);
    pendingCostCentMs += std::max (0.0, pull - s.noteHoldFreeCents)
                         * 1000.0 * (double) (samples - pendingBilledAt) / fs;
    pendingBilledAt = samples;

    if (samples - pendingNoteSince >= dwellSamples || pendingCostCentMs >= s.noteHoldBudgetCentMs)
    {
        pendingNote = -1;
        return candidate;
    }

    return note;
}

bool CorrectionLaw::decideNote (double pitch, int& decided) noexcept
{
    // An empty mask -- every note switched off -- gives no target, and so no
    // correction at all: the voice passes through at the engine's rest delay.
    const auto mask = (NoteMask) (scaleMask (s.scale, s.key) & s.allowed);
    return nearestAllowed (pitch, mask, note, haveNote, s.hysteresisCents, decided);
}

double CorrectionLaw::confirmPitch (double latest) noexcept
{
    // Guard 5 of spec §3.5, moved from the note to the pitch, and narrowed to
    // jumps.
    //
    // The spec puts a median on the note decision only, to keep it off the
    // period. But a median on the note, with the error computed from the
    // fresh pitch, pairs a held note with a pitch that has already moved, and
    // the engine is then told to correct by the whole interval. Measured: a
    // one-frame octave error at a note's end drove +1200 cents, and every real
    // leap drove its full interval for a hop or two.
    //
    // A median on the pitch fixed that but delayed every estimate by a hop,
    // which doubled the residual on a fast vibrato (1.0 to 2.0 cents peak to
    // peak) for no gain: vibrato, jitter and glides move a few cents per hop,
    // never 75. So only a jump has to be confirmed -- by the next estimate
    // agreeing with it. A lone outlier is dropped from the note and the error
    // together; a real leap lands one evaluation late (half a millisecond, or
    // a quarter period) with nothing in between; everything else is untouched.
    constexpr double jump = 0.75;   // semitones

    if (! havePendingJump && std::abs (latest - pitchIn) <= jump)
        return latest;

    if (havePendingJump && std::abs (latest - pendingJump) <= jump)
    {
        havePendingJump = false;

        // A leap, confirmed. The pitch has just stepped by an interval, and
        // a slope taken across that step is the interval divided by a hop --
        // enough to predict a semitone ahead, which lands the note decision
        // on the wrong side of a boundary and corrects by most of it. The
        // step is not a slope; the estimates after it start a fresh one.
        jumpTaken = true;
        return latest;
    }

    if (havePendingJump && std::abs (latest - pitchIn) <= jump)
    {
        havePendingJump = false;   // the jump was a lone outlier
        return latest;
    }

    pendingJump = latest;
    havePendingJump = true;
    return pitchIn;
}

void CorrectionLaw::setNote (int newNote) noexcept
{
    if (haveNote && newNote == note)
        return;

    if (haveNote)
    {
        // Carry the vibrato split's slow state across the step, so the fast
        // part of the error stays what it was rather than becoming the whole
        // interval for the next few hundred milliseconds.
        errorSlow += 100.0 * (newNote - note);
        ++st.noteChanges;

        // Where the target is headed. It steps straight there when
        // noteTransitionMs is 0, which is what CLASSIC has always done (spec
        // §4.5; HYBRID's glide is on branch archive/hybrid-studio).
        targetGoal = newNote;
        if (transitionAlpha <= 0.0)
            target = newNote;
    }
    else
    {
        // The first note of a phrase: the split's slow state starts at this
        // note's own error rather than the last phrase's. From the predicted
        // pitch, like every other reader of it.
        target = targetGoal = newNote;
        errorSlow = 100.0 * (newNote - predicted);
    }

    note = newNote;
    haveNote = true;
}

double CorrectionLaw::tick (const PitchEstimate& e, bool evaluated) noexcept
{
    voiced = e.voiced;
    ++samples;

    if (evaluated)
    {
        clarity = e.clarity;

        if (e.voiced && e.period > 0.0)
        {
            const auto fresh = pitch::semitonesFromHz (fs / e.period, s.refA);

            if (e.onset || ! havePitch)
            {
                // A new note starts from itself: no slow state carried from
                // the last phrase, no correction carried either, and nothing
                // to confirm a jump against.
                pitchIn = pitchSlow = fresh;
                havePendingJump = false;
                haveNote = false;
                applied = 0.0;

                // And nothing to predict from. An onset's first estimate has
                // no predecessor that means anything -- the slope across a
                // phrase boundary is the interval between two notes divided
                // by a hop, which is enormous and meaningless.
                pitchSlope = 0.0;
                haveSlopeFrom = false;
            }
            else
            {
                jumpTaken = false;
                pitchIn = confirmPitch (fresh);

                if (jumpTaken)
                {
                    pitchSlope = 0.0;
                    haveHistory = 0;
                    haveAnchor = false;
                    haveSlopeFrom = false;
                }
            }

            havePitch = true;

            // The period the rest of the plugin uses comes from the pitch that
            // was ACCEPTED, not from the estimate that arrived.
            //
            // confirmPitch holds the old pitch when a jump has not been
            // confirmed, but `period` used to be written from e.period a line
            // earlier, unconditionally -- so on a one-hop octave error the law
            // correctly ignored the pitch while the engine was handed a period
            // half as long. ClassicEngine's window is [floor, rest + T], so a T
            // that halves collapses the window under the read pointer and
            // forces a splice at that instant, whatever the rest is.
            //
            // That is what the audible pops are. Measured on Failure
            // (2026-09-13): in the 40 ms before each of the five splices Frosty
            // hears, the detector's f0 spans a ratio of 1.76 to 3.94; before
            // the quiet ones, 1.00 to 1.02. And they fire at the same
            // millisecond at every rest from 2 to 8 ms, which is why four rests
            // sounded "almost indistinguishable" and all of them unacceptable.
            //
            // AGENTS.md already has the rule this restores, one step short of
            // far enough: "the correction and the note always come from the
            // same pitch". So does the period.
            period = fs / pitch::hzFromSemitones (pitchIn, s.refA);

            // The slope of the estimate, in semitones per sample. Only from
            // estimates that were taken at face value: while confirmPitch is
            // holding a jump it returns the OLD pitch, so the difference
            // across those evaluations is zero and would drag the slope down
            // just as the voice moves fastest. The jump's own step is not a
            // slope either -- it is a leap, and the estimate after it starts
            // a fresh baseline.
            if (haveSlopeFrom && ! havePendingJump)
            {
                const auto dt = (double) (samples - slopeAt);

                if (dt > 0.0)
                {
                    // Median of the last three, before any smoothing. A pitch
                    // that moves in ONE evaluation and then stops is a step,
                    // or detector noise, and predicting on it overshoots by
                    // whatever it stepped: a 40 cent step at retune 3 ms cut
                    // the measured 10-90 settling from 6.6 ms to 3.2. A real
                    // scoop or vibrato holds its slope across several
                    // evaluations, so the median keeps it and drops the step.
                    // Same discipline as confirmPitch above, and the
                    // detector's own stability gate.
                    history[2] = history[1];
                    history[1] = history[0];
                    history[0] = { (pitchIn - slopeFrom) / dt, pitchIn, samples };
                    if (haveHistory < 3)
                        ++haveHistory;

                    // The median of the three, taken WITH the estimate it was
                    // measured from. A median picks one of three differences,
                    // each spanning a different pair of evaluations, so the
                    // slope it returns describes the voice at that pair and
                    // not at the newest estimate. Predicting from the newest
                    // one with it silently throws away however far back it
                    // came from -- 1.5 hops on a smooth run, which at A2 is
                    // 3.4 ms and cost 1.2 ms of the worst-case lag. Carrying
                    // the anchor makes the extrapolation exact for a straight
                    // line at any staleness, with no constant to tune: a
                    // fixed 1.5-hop correction instead overshot every vibrato
                    // into negative lag and put the residue back up to 1.46 c.
                    auto pick = 0;
                    if (haveHistory == 3)
                    {
                        const auto a = history[0].slope, b = history[1].slope, c = history[2].slope;
                        pick = (a < b) ? ((b < c) ? 1 : ((a < c) ? 2 : 0))
                                       : ((a < c) ? 0 : ((b < c) ? 2 : 1));
                    }

                    const auto chosen = history[pick];

                    // A one-pole on top, off by default: see predictSlopeMs.
                    const auto tau = fs * std::max (0.0, s.predictSlopeMs) * 0.001;
                    const auto alpha = tau > 0.0 ? 1.0 - std::exp (-dt / tau) : 1.0;
                    pitchSlope += alpha * (chosen.slope - pitchSlope);
                    slopeAnchorPitch = chosen.pitch;
                    slopeAnchorAt = chosen.at;
                    haveAnchor = true;
                }
            }

            if (! havePendingJump)
            {
                slopeFrom = pitchIn;
                slopeAt = samples;
                haveSlopeFrom = true;
            }
        }
    }

    // Carry the estimate to where the engine reads: the estimate refers to
    // kAnalysisLagPeriods x T behind the newest sample, the engine reads
    // readDelaySamples behind it, and the gap between them times the pitch
    // slope is the residue off the note on a moving voice.
    //
    // The gap goes BOTH ways. Forward when the engine reads newer material
    // than the estimate describes, backward when it rests past it -- which is
    // not an exotic case but the ordinary one above about 280 Hz at a 4 ms
    // rest, and the ordinary one everywhere at a 6 ms one. Extrapolating
    // backward is the same arithmetic into material already seen, so it is if
    // anything the safer half.
    //
    // It used to refuse the backward half, and that refusal was what made a
    // deeper rest look bad: swept on 2026-09-13 the residue bottomed at 6 ms
    // (1.07 c) and then climbed -- 1.72 at 8 ms, 5.10 at 12 -- purely because
    // more and more of the range fell on the side the law would not correct.
    // That read as "deeper rests do not work" when it was "half the law was
    // switched off".
    predicted = pitchIn;

    if (havePitch && voiced && haveAnchor && period > 1.0)
    {
        // Where the engine actually reads. Taken from the contract, not copied
        // from the engine: when the rest began tracking the note (2026-09-12)
        // a copy taken once at applyParams went stale, the law predicted for a
        // 4 ms read that was no longer there, and the correction lag went from
        // 0.71 ms to 3.19 -- which read as the rest change failing when it was
        // the bookkeeping.
        const auto readDelay = s.readDelaySamples >= 0.0 ? s.readDelaySamples
                                                        : contract::liveRest (fs, period);
        const auto ahead = Detector::kAnalysisLagPeriods * period - readDelay;

        {
            // From the anchor the slope was measured at, to what the engine
            // is about to read: the anchor's own age plus the gap between the
            // estimate and the read. `ahead` may be negative; `span` may not,
            // since the anchor cannot be read from before it existed.
            const auto span = (double) (samples - slopeAnchorAt) + ahead;
            // (age is >= 0 and ahead >= -one period, so span stays sane)
            const auto candidate = slopeAnchorPitch + pitchSlope * span;

            // Taken only if it lands within predictMaxCents of the estimate.
            // Otherwise the estimate is left alone -- NOT dragged to the edge
            // of the allowance, which is the difference between a guard and an
            // error.
            //
            // The case that makes it matter is a pitch move too small for
            // confirmPitch to call a jump, up to 75 cents, which leaves the
            // anchor on the far side of it. Clamping there pulls the
            // prediction a full predictMaxCents off an estimate that was
            // perfectly good: on the reference stimulus it turned 0.72 ms of
            // mean lag into 1.94 and 1.24 cents of residue into 2.41, the
            // clamp firing as an error rather than as a guard. Declining
            // costs only the prediction -- and predictMaxCents = 0 then means
            // exactly what it says, the estimate untouched.
            if (100.0 * std::abs (candidate - pitchIn) <= s.predictMaxCents)
                predicted = candidate;
        }
    }

    if (havePitch)
    {
        // The note decision follows the slow pitch when vibrato is being kept,
        // so a vibrato straddling a boundary does not flip the target twice a
        // cycle. A leap bigger than any vibrato snaps it, so a real interval
        // is not heard late.
        //
        // Everything from here down reads `predicted`, never `pitchIn`: the
        // note and the correction must come from the same pitch (AGENTS.md,
        // "the octave bug" -- a held note paired with a pitch that had moved
        // is what drove a correction by the whole interval).
        pitchSlow += decideCoeff * (predicted - pitchSlow);
        if (std::abs (predicted - pitchSlow) > 1.5)
            pitchSlow = predicted;

        if (evaluated && voiced)
        {
            int decided = 0;
            const auto decideOn = s.vibratoAmount > 0.0 ? pitchSlow : predicted;

            if (decideNote (decideOn, decided))
            {
                // Kept vibrato decides on the slow pitch, which already never
                // flips; the raw pitch at 0 % gets the dwell.
                if (s.vibratoAmount <= 0.0)
                    decided = holdOrSwitch (decideOn, decided);
                setNote (decided);
            }
            else
            {
                haveNote = false;
                pendingNote = -1;
            }
        }
    }

    const auto active = havePitch && haveNote && voiced;
    double desired = 0.0;

    // The target slews toward the note it was told, instead of stepping. At
    // noteTransitionMs = 0 -- the default and what CLASSIC has always done --
    // it is already there and this does nothing.
    if (transitionAlpha > 0.0 && haveNote)
        target = transitionAlpha * target + (1.0 - transitionAlpha) * targetGoal;

    if (havePitch && haveNote)
    {
        const auto error = 100.0 * (target - predicted);
        errorSlow += slowCoeff * (error - errorSlow);

        const auto split = error - s.vibratoAmount * (error - errorSlow);
        desired = split * flexGain (std::abs (split), s.flex);
        st.errorCents = error;
    }

    // Retune. Frozen while unvoiced: the gate below fades the amount, and the
    // ratio holds where it was so a breath is not re-pitched on its way out.
    if (active)
        applied = retuneAlpha * applied + (1.0 - retuneAlpha) * desired;

    const auto conf = std::clamp ((clarity - s.clarityLo) / std::max (1.0e-6, s.clarityHi - s.clarityLo), 0.0, 1.0);
    confidence += confidenceCoeff * (conf - confidence);

    // In over one period after lock (the provisional-output policy of spec
    // §2.1: at an onset the plugin is a wire, and the correction arrives
    // within a cycle), out over 10 ms.
    const auto rampIn = std::max (1.0, period);
    gate = active ? std::min (1.0, gate + 1.0 / rampIn)
                  : std::max (0.0, gate - 1.0 / fadeOutSamples);

    const auto out = std::clamp (applied * confidence * gate,
                                 -s.maxCorrectionCents, s.maxCorrectionCents);

    st.pitchIn = pitchIn;
    st.pitchUsed = predicted;
    st.target = target;
    st.note = haveNote ? note : -1;
    st.appliedCents = std::isfinite (out) ? out : 0.0;
    return st.appliedCents;
}

} // namespace bmo::tune
