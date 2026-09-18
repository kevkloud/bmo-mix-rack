#include "modules/deq/dsp/DspCore.h"

#include <algorithm>

namespace bmo::deq
{

namespace
{
    constexpr SvfCoeffs kNoStep { 0.0, 0.0, 0.0, 0.0, 0.0 };

    /** The detector's own filter. Bilinear TPT is fine here -- it only has to
        aim the detector at the band's region, and it never reaches the audio.
        A bell listens through a unity-peak bandpass, so a threshold means the
        same thing at every Q; a shelf listens to the side of its corner that
        it moves. */
    SvfCoeffs sidechainFor (Shape shape, double hz, double q, double rate) noexcept
    {
        constexpr double kButterworthK = 1.4142135623730951;

        SvfCoeffs c;
        c.g = std::tan (kPi * hz / rate);

        switch (shape)
        {
            case Shape::lowShelf:
                c.k = kButterworthK; c.m0 = 0.0; c.m1 = 0.0; c.m2 = 1.0;
                break;

            case Shape::highShelf:
                c.k = kButterworthK; c.m0 = 1.0; c.m1 = -c.k; c.m2 = -1.0;
                break;

            case Shape::bell:
            case Shape::lowCut:
            case Shape::highCut:
                c.k = 1.0 / q; c.m0 = 0.0; c.m1 = c.k; c.m2 = 0.0;
                break;
        }

        return c;
    }

    SvfCoeffs perSampleStep (const SvfCoeffs& from, const SvfCoeffs& to) noexcept
    {
        constexpr double n = 1.0 / (double) kControlInterval;
        return { (to.g - from.g) * n, (to.k - from.k) * n,
                 (to.m0 - from.m0) * n, (to.m1 - from.m1) * n, (to.m2 - from.m2) * n };
    }

    /** Move a glide to its next tick value and set up the straight line from
        where it ended to there. Starting `now` from the previous tick value,
        not from wherever the additions left it, keeps rounding from drifting. */
    template <typename G>
    void beginInterval (G& g, double target, double alpha, bool snap) noexcept
    {
        const auto from = g.tick;
        g.target = target;

        if (snap)
        {
            g.snap (target);
            return;
        }

        g.advanceTick (alpha);
        g.now  = from;
        g.step = (g.tick - from) / (double) kControlInterval;
    }
}

//==============================================================================
void DspCore::prepare (double sampleRate, int, int) noexcept
{
    rate = (std::isfinite (sampleRate) && sampleRate > 0.0) ? sampleRate : 48000.0;
    tickAlpha = std::exp (-(double) kControlInterval / (kSmoothingMs * 1.0e-3 * rate));
    grid = DesignGrid::make (rate);

    // The analyser's window: about a third of a second, which holds four
    // 8192-point frames at 44.1 kHz and leaves the display free to choose its
    // own frame size and overlap. Allocated here, never on the audio thread,
    // and written only while a panel is looking.
    shared->pre.prepare ((int) (0.35 * rate));
    shared->post.prepare ((int) (0.35 * rate));

    reset();
}

void DspCore::reset() noexcept
{
    for (auto& b : bands)
        resetBand (b);

    tickPhase = 0;
    primed = false;
}

void DspCore::resetBand (Band& b) noexcept
{
    b.m.reset(); b.s.reset();
    b.sideM.reset(); b.sideS.reset();
    b.detector.reset();

    b.cur = b.next = SvfCoeffs {};
    b.step = kNoStep;
    b.offsetDb = b.appliedGainDb = 0.0;
    b.live = false;

    b.designedHz = b.designedQ = -1.0;
    b.designedStatic = 1.0e9;
    b.designedOffset = 0.0;
    b.sideHz = b.sideQ = -1.0;
    b.detAttack = b.detRelease = -1.0;

    b.enable.snap (0.0);
}

//==============================================================================
void DspCore::controlTick() noexcept
{
    const auto snapAll = ! primed;
    primed = true;

    for (int i = 0; i < kMaxBands; ++i)
    {
        const auto& s = current.bands[(size_t) i];
        auto& b = bands[(size_t) i];

        // A band that has faded out completely stops costing anything, and
        // restarts from silence rather than from stale state.
        if (! s.enabled && (snapAll || (b.enable.tick == 0.0 && b.enable.now == 0.0)))
        {
            if (b.live)
                resetBand (b);
            continue;
        }

        const auto gain  = hasGain (s.shape);
        const auto hzT   = std::log2 (clampFrequency (s.frequencyHz, rate));
        const auto qT    = std::log (clampQ (s.q));
        const auto gT    = gain ? clampGainDb (s.gainDb) : 0.0;
        const auto betaT = s.placement == Placement::stereo ? 0.0 : std::clamp (s.msAmount, 0.0, 1.0);

        // Waking up: controls start where they are asked to be; only the
        // enable fade glides, from silence.
        const auto waking = ! b.live;
        const auto snapControls = snapAll || waking;

        beginInterval (b.logHz,  hzT,   tickAlpha, snapControls);
        beginInterval (b.logQ,   qT,    tickAlpha, snapControls);
        beginInterval (b.gainDb, gT,    tickAlpha, snapControls);
        beginInterval (b.beta,   betaT, tickAlpha, snapControls);
        beginInterval (b.enable, s.enabled ? 1.0 : 0.0, tickAlpha, snapAll);
        b.live = true;

        const auto hz = std::exp2 (b.logHz.tick);
        const auto q  = std::exp (b.logQ.tick);

        //== Dynamics: the gain offset the detector asks for right now ==========
        const auto dynamic = gain && s.dynamics.enabled;

        if (dynamic)
        {
            const auto& d = s.dynamics;

            if (d.attackMs != b.detAttack || d.releaseMs != b.detRelease || d.rms != b.detRms)
            {
                b.detector.configure (d.attackMs, d.releaseMs, d.rms, rate);
                b.detAttack = d.attackMs; b.detRelease = d.releaseMs; b.detRms = d.rms;
            }

            if (hz != b.sideHz || q != b.sideQ || s.shape != b.sideShape)
            {
                b.sideCoeffs = sidechainFor (s.shape, hz, q, rate);
                b.sideTaps   = SvfTaps::of (b.sideCoeffs.g, b.sideCoeffs.k);
                b.sideHz = hz; b.sideQ = q; b.sideShape = s.shape;
            }

            b.computer.thresholdDb = d.thresholdDb;
            b.computer.ratio       = d.ratio;
            b.computer.kneeDb      = d.kneeDb;
            b.computer.rangeDb     = d.rangeDb;
            b.computer.direction   = d.direction;

            const auto env = b.detector.envelope();
            b.offsetDb = b.computer.offsetDb (20.0 * std::log10 (env + 1.0e-12));
        }
        else
        {
            b.offsetDb = 0.0;
        }

        //== Coefficients: glide from the last target to a new one ==============
        b.cur = b.next;

        const auto staticChanged = s.shape != b.designedShape || hz != b.designedHz || q != b.designedQ
                                || b.gainDb.tick != b.designedStatic;
        const auto offsetMoved = std::abs (b.offsetDb - b.designedOffset) > kOffsetHysteresisDb
                              || (b.offsetDb == 0.0 && b.designedOffset != 0.0);

        if (staticChanged || offsetMoved)
        {
            const auto gainNow = gain ? clampGainDb (b.gainDb.tick + b.offsetDb) : 0.0;
            b.next = SvfCoeffs::fromBiquad (designMatched (s.shape, hz, q, gainNow, grid));
            b.designedShape = s.shape; b.designedHz = hz; b.designedQ = q;
            b.designedStatic = b.gainDb.tick; b.designedOffset = b.offsetDb;
        }

        b.appliedGainDb = gain ? clampGainDb (b.designedStatic + b.designedOffset) : 0.0;

        if (snapControls)
            b.cur = b.next;

        b.step = perSampleStep (b.cur, b.next);

        b.m.flushTiny(); b.s.flushTiny();
        b.sideM.flushTiny(); b.sideS.flushTiny();
        b.detector.flushTiny();
    }
}

//==============================================================================
template <typename Sample>
void DspCore::processImpl (Sample* const* channels, int numChannels, int numSamples) noexcept
{
    if (numChannels < 1 || numSamples <= 0 || channels == nullptr || channels[0] == nullptr)
        return;

    // Stereo in, stereo out first (spec A4). Channels past the second pass
    // untouched rather than being guessed at.
    const auto stereo = numChannels >= 2 && channels[1] != nullptr;
    auto* left  = channels[0];
    auto* right = stereo ? channels[1] : nullptr;
    const auto serial = current.topology == Topology::serial;

    // The dry signal, before anything. Returns immediately unless a panel has
    // asked for it, and touches no state the audio depends on.
    shared->pre.write (channels, numChannels, numSamples);

    // Read once, so a solo arriving mid-block cannot make a 4096-sample block
    // differ from 4096 one-sample blocks. See kControlInterval.
    const auto soloed = shared->solo.load (std::memory_order_relaxed);

    for (int n = 0; n < numSamples; ++n)
    {
        if (tickPhase == 0)
            controlTick();

        if (++tickPhase >= kControlInterval)
            tickPhase = 0;

        const double xl = (double) left[n];
        const double xr = stereo ? (double) right[n] : xl;
        const auto dryM = 0.5 * (xl + xr), dryS = 0.5 * (xl - xr);

        double yl = xl, yr = xr, accL = 0.0, accR = 0.0, soloL = 0.0, soloR = 0.0;

        for (int i = 0; i < kMaxBands; ++i)
        {
            auto& b = bands[(size_t) i];

            if (! b.live)
                continue;

            const auto& s = current.bands[(size_t) i];

            b.cur.g  += b.step.g;  b.cur.k  += b.step.k;
            b.cur.m0 += b.step.m0; b.cur.m1 += b.step.m1; b.cur.m2 += b.step.m2;
            b.beta.now   += b.beta.step;
            b.enable.now += b.enable.step;

            const auto beta = b.beta.now;

            if (hasGain (s.shape) && s.dynamics.enabled)
            {
                const auto sm = std::abs (b.sideM.process (b.sideTaps, b.sideCoeffs, dryM));
                const auto ss = stereo ? std::abs (b.sideS.process (b.sideTaps, b.sideCoeffs, dryS)) : 0.0;

                // |L| and |R| of the band-limited signal peak together at
                // |M| + |S|, so that is the linked stereo level.
                const auto stereoLevel = sm + ss;
                const auto placed = s.placement == Placement::mid  ? sm
                                  : s.placement == Placement::side ? ss
                                                                   : stereoLevel;

                b.detector.process ((1.0 - beta) * stereoLevel + beta * placed);
            }

            const auto inL = serial ? yl : xl;
            const auto inR = serial ? yr : xr;
            const auto mid  = 0.5 * (inL + inR);
            const auto side = 0.5 * (inL - inR);

            const auto taps = SvfTaps::of (b.cur.g, b.cur.k);
            const auto dM = b.m.process (taps, b.cur, mid) - mid;
            const auto dS = stereo ? b.s.process (taps, b.cur, side) - side : 0.0;

            // H(L) - L and H(R) - R, from the M/S pair.
            auto wl = dM + dS, wr = dM - dS;

            if (s.placement == Placement::mid)
            {
                wl = (1.0 - beta) * wl + beta * dM;
                wr = (1.0 - beta) * wr + beta * dM;
            }
            else if (s.placement == Placement::side)
            {
                wl = (1.0 - beta) * wl + beta * dS;
                wr = (1.0 - beta) * wr - beta * dS;
            }

            const auto e = b.enable.now;

            if (serial) { yl += e * wl;   yr += e * wr; }
            else        { accL += e * wl; accR += e * wr; }

            // This band on its own: what it adds or takes away, moving with
            // its detector. A band that is off never reaches here, so soloing
            // one is silence.
            if (i == soloed) { soloL = e * wl; soloR = e * wr; }
        }

        if (! serial)
        {
            yl = xl + accL;
            yr = xr + accR;
        }

        if (soloed >= 0)
        {
            yl = soloL;
            yr = soloR;
        }

        left[n] = (Sample) yl;

        if (stereo)
            right[n] = (Sample) yr;
    }

    // What the module is putting out, solo included: the analyser draws what
    // is being heard, not what would have been heard.
    shared->post.write (channels, numChannels, numSamples);
}

void DspCore::process (float* const* channels, int numChannels, int numSamples) noexcept
{
    processImpl (channels, numChannels, numSamples);
}

void DspCore::process (double* const* channels, int numChannels, int numSamples) noexcept
{
    processImpl (channels, numChannels, numSamples);
}

//==============================================================================
double DspCore::currentGainReductionDb() const noexcept
{
    // Signed, and the sign is the point: **positive is gain taken away,
    // negative is gain added**.
    //
    // This was `std::max (0, -offsetDb)`, which reported a flat zero for every
    // upward band. Measured on the panel during the UI pass (2026-09-15), a
    // band boosting 12 dB and a band with its dynamics switched off drew the
    // same empty meter -- the one thing a meter must never do. Frosty's call
    // the same day: reduction reads down from the top of the bar, gain added
    // reads up from the bottom.
    //
    // Still the deepest *single* band rather than a sum, as before, because a
    // panel has no path to one band's figure. "Deepest" is now by magnitude,
    // so the band moving the signal furthest wins whichever way it is moving
    // it, and ties go to the first one reached -- the same arbitrary but
    // stable choice `std::max` was already making.
    double deepest = 0.0;

    for (const auto& b : bands)
        if (b.live)
        {
            const auto moved = -b.offsetDb * b.enable.tick;

            if (std::abs (moved) > std::abs (deepest))
                deepest = moved;
        }

    return deepest;
}

Biquad DspCore::bandDesign (int band) const noexcept
{
    const auto& s = current.bands[(size_t) band];
    return designMatched (s.shape, s.frequencyHz, s.q, s.gainDb, grid);
}

std::complex<double> DspCore::staticResponseAt (double hz) const noexcept
{
    const auto w = 2.0 * kPi * hz / rate;
    const auto serial = current.topology == Topology::serial;
    std::complex<double> acc = serial ? 1.0 : 0.0;

    for (int i = 0; i < kMaxBands; ++i)
    {
        const auto& s = current.bands[(size_t) i];

        if (! s.enabled)
            continue;

        // A centred source has no side, so a side band does nothing to it and
        // a mid band acts in full.
        const auto beta = s.placement == Placement::stereo ? 0.0 : std::clamp (s.msAmount, 0.0, 1.0);
        const auto weight = s.placement == Placement::side ? 1.0 - beta : 1.0;
        const auto contribution = weight * (bandDesign (i).responseAt (w) - 1.0);

        if (serial) acc *= 1.0 + contribution;
        else        acc += contribution;
    }

    return serial ? acc : 1.0 + acc;
}

bool DspCore::allStateNormal() const noexcept
{
    for (const auto& b : bands)
        if (! (b.m.isNormal() && b.s.isNormal() && b.sideM.isNormal() && b.sideS.isNormal() && b.detector.isNormal()))
            return false;

    return true;
}

bool DspCore::allCoefficientsStable() const noexcept
{
    for (const auto& b : bands)
        if (b.live && ! (b.cur.isStable() && b.next.isStable()))
            return false;

    return true;
}

} // namespace bmo::deq
