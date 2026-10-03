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

    /** dsp::crossfade at the engine's double precision, on the same terms:
        at either end it returns that input itself, bit for bit, and never
        reads the other, so a switch at rest leaves the audio untouched. */
    double blend (double from, double to, float position) noexcept
    {
        if (! (position > 0.0f))
            return from;

        if (! (position < 1.0f))
            return to;

        const auto p = (double) position;
        return from * (1.0 - p) + to * p;
    }

    /** A band's contribution to L and R, H(L) - L and H(R) - R, from its M/S
        pair, per placement. */
    void placeContribution (Placement placement, double beta, double dM, double dS, double& wl, double& wr) noexcept
    {
        wl = dM + dS;
        wr = dM - dS;

        if (placement == Placement::mid)
        {
            wl = (1.0 - beta) * wl + beta * dM;
            wr = (1.0 - beta) * wr + beta * dM;
        }
        else if (placement == Placement::side)
        {
            wl = (1.0 - beta) * wl + beta * dS;
            wr = (1.0 - beta) * wr - beta * dS;
        }
    }

    /** Butterworth: a shelf leaving or arriving in a change of shape takes
        its Q toward this, so its own resonance does not dip or bump the
        level on the way (DspCore::designAt). */
    constexpr double kPlainQ = 0.7071067811865476;

    /** The Q a design of `shape` uses: a cut's stops at `cutMaxQ`
        (Settings::cutMaxQ). */
    double designQ (Shape shape, double q, double cutMaxQ) noexcept
    {
        const auto cut = shape == Shape::lowCut || shape == Shape::highCut;
        return cut && q > cutMaxQ + 0.005 ? cutMaxQ : q;
    }

    /** The level a detector placed here listens to. */
    double placedLevel (Placement placement, double sm, double ss) noexcept
    {
        return placement == Placement::mid  ? sm
             : placement == Placement::side ? ss
                                            : sm + ss;
    }
}

//==============================================================================
void DspCore::prepare (double sampleRate, int, int) noexcept
{
    rate = (std::isfinite (sampleRate) && sampleRate > 0.0) ? sampleRate : 48000.0;
    prepared = true;
    tickAlpha = std::exp (-(double) kControlInterval / (kSmoothingMs * 1.0e-3 * rate));
    grid = DesignGrid::make (rate);

    // The analyser's window: about a third of a second, which holds four
    // 8192-point frames at 44.1 kHz and leaves the display free to choose its
    // own frame size and overlap. Allocated here, never on the audio thread,
    // and written only while a panel is looking.
    shared->pre.prepare ((int) (0.35 * rate));
    shared->post.prepare ((int) (0.35 * rate));

    for (auto& b : bands)
    {
        for (auto* r : { &b.dynMix, &b.dirMix, &b.placeMix })
            r->prepare (rate, kSwitchFadeMs);

        b.shapeMix.prepare (rate, kShapeChangeMs);
    }

    soloMix.prepare (rate, kSwitchFadeMs);

    reset();
}

void DspCore::reset() noexcept
{
    for (auto& b : bands)
        resetBand (b, true);

    tickPhase = 0;
    primed = false;
    soloMix.snap (1.0f);
    soloPrimed = false;
}

void DspCore::resetBand (Band& b, bool listenerToo) noexcept
{
    b.m.reset(); b.s.reset();

    b.cur = b.next = SvfCoeffs {};
    b.step = kNoStep;
    b.appliedGainDb = 0.0;
    b.live = false;

    b.designedHz = b.designedQ = -1.0;
    b.designedStatic = 1.0e9;
    b.designedOffset = 0.0;

    b.enable.snap (0.0);

    b.shapeMix.snap (1.0f);
    b.oldM.reset(); b.oldS.reset();

    if (listenerToo)
    {
        b.sideM.reset(); b.sideS.reset();
        b.detector.reset();
        b.offsetDb = 0.0;
        b.sideHz = b.sideQ = -1.0;
        b.detAttack = b.detRelease = -1.0;
        b.placeMix.snap (1.0f);
        b.hearing = false;
    }
}

//==============================================================================
void DspCore::controlTick() noexcept
{
    const auto snapAll = ! primed;
    primed = true;

    const auto listening = std::clamp (current.bandCount, 0, kMaxBands);

    for (int i = 0; i < kMaxBands; ++i)
    {
        const auto& s = current.bands[(size_t) i];
        auto& b = bands[(size_t) i];
        const auto listens = i < listening;

        // A band that has faded out completely stops costing anything, and
        // restarts from silence rather than from stale state. Its listener
        // carries on if the band is one of the product's and has been live
        // since reset() (Band, above); one never switched on does no work.
        const auto asleep = ! s.enabled && (snapAll || (b.enable.tick == 0.0 && b.enable.now == 0.0));

        if (asleep)
        {
            if (b.live || (b.hearing && ! listens))
                resetBand (b, ! listens);

            if (! listens || ! b.hearing)
                continue;
        }

        const auto hzT   = std::log2 (clampFrequency (s.frequencyHz, rate));
        const auto qT    = std::log (clampQ (s.q));

        //== The listener: where the band is, and what its detector asks for ===
        //
        // Started from where it is asked to be the first time it runs and
        // glided from then on, band on or off -- except that a band coming
        // into use from fully out starts AT its settings, as it always did: a
        // preset turning an off bell at Q 40 into a cut that is on must not
        // glide down from 40 while it fades in (round 2 of the review). The
        // fade in is what makes the entry smooth.
        const auto waking = ! asleep && ! b.live;
        const auto snapListener = snapAll || ! b.hearing || waking;

        // A new placement crosses over from the one in use; one asked for
        // during a crossover waits for it to finish.
        if (snapListener)
        {
            b.placement = b.fromPlacement = s.placement;
            b.placeMix.snap (1.0f);
        }
        else if (s.placement != b.placement && ! b.placeMix.isMoving())
        {
            b.fromPlacement = b.placement;
            b.placement = s.placement;
            b.placeMix.snap (0.0f);
            b.placeMix.setTarget (1.0f);
        }

        const auto betaT = b.placement == Placement::stereo ? 0.0 : std::clamp (s.msAmount, 0.0, 1.0);

        beginInterval (b.logHz, hzT,   tickAlpha, snapListener);
        beginInterval (b.logQ,  qT,    tickAlpha, snapListener);
        beginInterval (b.beta,  betaT, tickAlpha, snapListener);
        b.hearing = true;

        const auto hz = std::exp2 (b.logHz.tick);
        auto q = std::exp (b.logQ.tick);

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
        }

        // The gain offset the detector asks for right now, scaled by how far
        // the band's dynamics are in use. Out of use it is exactly 0, which is
        // what keeps DYN off bit-identical to a static EQ whatever the
        // detector hears. DYN and the direction each blend two offsets over
        // kSwitchFadeMs instead of jumping between them: the jump was a
        // 12 dB coefficient change in 8 samples, 5x the steady step at 100 Hz.
        const auto dynamic = hasGain (s.shape) && s.dynamics.enabled;
        const auto below = s.dynamics.direction == Direction::below;

        if (snapListener)
        {
            b.dynMix.snap (dynamic ? 1.0f : 0.0f);
            b.dirMix.snap (below ? 1.0f : 0.0f);
        }
        else
        {
            b.dynMix.setTarget (dynamic ? 1.0f : 0.0f);
            b.dirMix.setTarget (below ? 1.0f : 0.0f);
        }

        const auto inUse = b.dynMix.advance (kControlInterval);
        const auto towardBelow = b.dirMix.advance (kControlInterval);

        if (inUse > 0.0f)
        {
            const auto& d = s.dynamics;

            b.computer.thresholdDb = d.thresholdDb;
            b.computer.ratio       = d.ratio;
            b.computer.kneeDb      = d.kneeDb;
            b.computer.rangeDb     = d.rangeDb;

            const auto env = b.detector.envelope();
            const auto envDb = 20.0 * std::log10 (env + 1.0e-12);

            auto offsetFor = [&b, envDb] (Direction direction)
            {
                b.computer.direction = direction;
                return b.computer.offsetDb (envDb);
            };

            const auto offset = ! (towardBelow > 0.0f) ? offsetFor (Direction::above)
                              : ! (towardBelow < 1.0f) ? offsetFor (Direction::below)
                                                       : blend (offsetFor (Direction::above), offsetFor (Direction::below), towardBelow);

            b.offsetDb = blend (0.0, offset, inUse);
        }
        else
        {
            b.offsetDb = 0.0;
        }

        b.sideM.flushTiny(); b.sideS.flushTiny();
        b.detector.flushTiny();

        if (asleep)
            continue;

        //== The band ===========================================================
        //
        // Waking up: its controls start where they are asked to be; only the
        // enable fade glides, from silence.
        const auto snapControls = snapAll || waking;

        // A new shape: the one in use leaves to nothing while the new one
        // arrives from nothing, in series (Band). One asked for during a
        // change waits for it to finish.
        auto crossing = false;

        if (snapControls)
        {
            b.shape = s.shape;
            b.shapeMix.snap (1.0f);
        }
        else if (s.shape != b.shape && ! b.shapeMix.isMoving())
        {
            b.oldShape = b.shape;
            b.oldGainDb = hasGain (b.shape) ? clampGainDb (b.designedStatic + b.designedOffset) : 0.0;
            b.oldHz = b.designedHz;
            b.oldQ = b.designedQ;
            b.oldNext = b.next;            // where the glide in progress has arrived
            b.oldM = b.m;                  // both carry on from the running state
            b.oldS = b.s;
            b.shape = s.shape;
            b.shapeMix.snap (0.0f);
            b.shapeMix.setTarget (1.0f);
            crossing = true;

            // And at its own Q: a Q still gliding from the last shape's -- a
            // cut's is capped at 0.71 -- would arrive as a different filter.
            b.logQ.snap (qT);
            q = std::exp (b.logQ.tick);
        }

        // How far the arriving shape will have come by the next tick, so the
        // designs gliding toward it land on nothing and on the full setting
        // exactly as the change ends.
        const auto changing = b.shapeMix.isMoving();
        const auto arrived = changing ? std::min (1.0f, b.shapeMix.value() + (float) kControlInterval / (float) b.shapeMix.lengthInSamples())
                                      : 1.0f;

        if (changing)
        {
            b.oldCoeffs = b.oldNext;
            b.oldNext = designAt (b.oldShape, b.oldHz, b.oldQ, b.oldGainDb, 1.0f - arrived);
            b.oldStep = perSampleStep (b.oldCoeffs, b.oldNext);
        }

        const auto gain = hasGain (b.shape);
        const auto gT   = gain ? clampGainDb (s.gainDb) : 0.0;

        // A shape arriving starts at its own gain: its amount is the fade, and
        // a gain still gliding from the last shape's would put a second, later
        // fade on top of it.
        beginInterval (b.gainDb, gT, tickAlpha, snapControls || crossing);
        beginInterval (b.enable, s.enabled ? 1.0 : 0.0, tickAlpha, snapAll);
        b.live = true;

        //== Coefficients: glide from the last target to a new one ==============
        b.cur = b.next;

        const auto staticChanged = b.shape != b.designedShape || hz != b.designedHz || q != b.designedQ
                                || b.gainDb.tick != b.designedStatic || arrived != b.designedAmount;
        const auto offsetMoved = std::abs (b.offsetDb - b.designedOffset) > kOffsetHysteresisDb
                              || (b.offsetDb == 0.0 && b.designedOffset != 0.0);
        const auto gainNow = gain ? clampGainDb (b.gainDb.tick + b.offsetDb) : 0.0;

        if (staticChanged || offsetMoved)
        {
            b.next = designAt (b.shape, hz, q, gainNow, arrived);
            b.designedShape = b.shape; b.designedHz = hz; b.designedQ = q;
            b.designedStatic = b.gainDb.tick; b.designedOffset = b.offsetDb;
            b.designedAmount = arrived;
        }

        b.appliedGainDb = gain ? clampGainDb (b.designedStatic + b.designedOffset) : 0.0;

        // A band waking starts at its design; a shape arriving starts at
        // nothing and glides toward the design.
        if (snapControls)
            b.cur = b.next;
        else if (crossing)
            b.cur = designAt (b.shape, hz, q, gainNow, 0.0f);

        b.step = perSampleStep (b.cur, b.next);

        b.m.flushTiny(); b.s.flushTiny();
        b.oldM.flushTiny(); b.oldS.flushTiny();
    }

    // The per-sample loop stops at the last band doing anything, so the
    // engine's spare bands are not visited every sample to be skipped.
    bandsInUse = 0;

    for (int i = kMaxBands; i > 0; --i)
        if (bands[(size_t) i - 1].hearing)
        {
            bandsInUse = i;
            break;
        }
}

//==============================================================================
template <typename Sample>
void DspCore::processImpl (Sample* const* channels, int numChannels, int numSamples) noexcept
{
    if (numChannels < 1 || numSamples <= 0 || channels == nullptr || channels[0] == nullptr)
        return;

    // Before prepare() there is no sample rate to design a filter at, and the
    // grid designed at a rate of 0 gave NaN. Until then the engine is a wire,
    // as an unprepared Dip is (core/dsp/SwitchFade.h) and as this EQ is at
    // its defaults.
    if (! prepared)
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
    // differ from 4096 one-sample blocks. See kControlInterval. A new one
    // crosses over from what was being heard; one asked for during a
    // crossover waits for it to finish (kSwitchFadeMs).
    const auto requested = shared->solo.load (std::memory_order_relaxed);

    if (! soloPrimed)
    {
        soloFrom = soloTo = requested;
        soloMix.snap (1.0f);
        soloPrimed = true;
    }
    else if (requested != soloTo && ! soloMix.isMoving())
    {
        soloFrom = soloTo;
        soloTo = requested;
        soloMix.snap (0.0f);
        soloMix.setTarget (1.0f);
    }

    for (int n = 0; n < numSamples; ++n)
    {
        if (tickPhase == 0)
            controlTick();

        if (++tickPhase >= kControlInterval)
            tickPhase = 0;

        const double xl = (double) left[n];
        const double xr = stereo ? (double) right[n] : xl;
        const auto dryM = 0.5 * (xl + xr), dryS = 0.5 * (xl - xr);

        const auto soloCrossing = soloMix.isMoving();
        double yl = xl, yr = xr, accL = 0.0, accR = 0.0, soloL = 0.0, soloR = 0.0, fromL = 0.0, fromR = 0.0;

        for (int i = 0; i < bandsInUse; ++i)
        {
            auto& b = bands[(size_t) i];

            if (! b.hearing)
                continue;

            b.beta.now += b.beta.step;
            const auto beta = b.beta.now;
            const auto placing = b.placeMix.isMoving() ? b.placeMix.next() : 1.0f;

            // The listener, band on or off and dynamics in use or not.
            {
                const auto sm = std::abs (b.sideM.process (b.sideTaps, b.sideCoeffs, dryM));
                const auto ss = stereo ? std::abs (b.sideS.process (b.sideTaps, b.sideCoeffs, dryS)) : 0.0;

                // |L| and |R| of the band-limited signal peak together at
                // |M| + |S|, so that is the linked stereo level.
                const auto stereoLevel = sm + ss;
                auto placed = placedLevel (b.placement, sm, ss);

                if (placing < 1.0f)
                    placed = blend (placedLevel (b.fromPlacement, sm, ss), placed, placing);

                b.detector.process ((1.0 - beta) * stereoLevel + beta * placed);
                ++b.listened;
            }

            if (! b.live)
                continue;

            b.cur.g  += b.step.g;  b.cur.k  += b.step.k;
            b.cur.m0 += b.step.m0; b.cur.m1 += b.step.m1; b.cur.m2 += b.step.m2;
            b.enable.now += b.enable.step;

            const auto inL = serial ? yl : xl;
            const auto inR = serial ? yr : xr;
            const auto mid  = 0.5 * (inL + inR);
            const auto side = 0.5 * (inL - inR);

            const auto taps = SvfTaps::of (b.cur.g, b.cur.k);
            double dM, dS;

            if (! b.shapeMix.isMoving())
            {
                dM = b.m.process (taps, b.cur, mid) - mid;
                dS = stereo ? b.s.process (taps, b.cur, side) - side : 0.0;
            }
            else
            {
                // A change of shape: the shape arriving, coming from nothing,
                // then the shape leaving, going to nothing, in series -- a
                // product of two filters, which cannot cancel (designAt). The
                // arriving one hears the band's input and carries on from the
                // running state; the leaving one keeps its own, which is exactly
                // right at first, since what it hears starts as that same input.
                b.shapeMix.next();

                b.oldCoeffs.g  += b.oldStep.g;  b.oldCoeffs.k  += b.oldStep.k;
                b.oldCoeffs.m0 += b.oldStep.m0; b.oldCoeffs.m1 += b.oldStep.m1; b.oldCoeffs.m2 += b.oldStep.m2;
                const auto oldTaps = SvfTaps::of (b.oldCoeffs.g, b.oldCoeffs.k);

                const auto inM = b.m.process (taps, b.cur, mid);
                const auto inS = stereo ? b.s.process (taps, b.cur, side) : 0.0;

                dM = b.oldM.process (oldTaps, b.oldCoeffs, inM) - mid;
                dS = stereo ? b.oldS.process (oldTaps, b.oldCoeffs, inS) - side : 0.0;
            }

            // H(L) - L and H(R) - R, from the M/S pair, crossing from the
            // placement that was if one is in progress.
            double wl, wr;
            placeContribution (b.placement, beta, dM, dS, wl, wr);

            if (placing < 1.0f)
            {
                double wasL, wasR;
                placeContribution (b.fromPlacement, beta, dM, dS, wasL, wasR);
                wl = blend (wasL, wl, placing);
                wr = blend (wasR, wr, placing);
            }

            const auto e = b.enable.now;

            if (serial) { yl += e * wl;   yr += e * wr; }
            else        { accL += e * wl; accR += e * wr; }

            // This band on its own: what it adds or takes away, moving with
            // its detector. A band that is off never reaches here, so soloing
            // one is silence.
            if (i == soloTo) { soloL = e * wl; soloR = e * wr; }
            if (soloCrossing && i == soloFrom) { fromL = e * wl; fromR = e * wr; }
        }

        if (! serial)
        {
            yl = xl + accL;
            yr = xr + accR;
        }

        if (soloCrossing)
        {
            // -1 on either side is the whole EQ.
            const auto soloing = soloMix.next();
            const auto wasL = soloFrom >= 0 ? fromL : yl, wasR = soloFrom >= 0 ? fromR : yr;
            const auto nowL = soloTo >= 0 ? soloL : yl,   nowR = soloTo >= 0 ? soloR : yr;
            yl = blend (wasL, nowL, soloing);
            yr = blend (wasR, nowR, soloing);
        }
        else if (soloTo >= 0)
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
    //
    // **The move is the one the band's design makes, clamp included**, not
    // the offset the detector asked for. A design is clamped at +-30 dB
    // (DesignLimits), so a -24 dB bell asked for 24 dB more cut cuts 6 dB
    // more, and until the 2026-10-03 review this read 24. For a bell that is
    // the move at its frequency; for a shelf it is the move of its plateau,
    // which is what its GAIN knob means.
    double deepest = 0.0;

    for (const auto& b : bands)
        if (b.live)
        {
            const auto applied = hasGain (b.designedShape) ? b.appliedGainDb - clampGainDb (b.designedStatic) : 0.0;
            const auto moved = -applied * b.enable.tick;

            if (std::abs (moved) > std::abs (deepest))
                deepest = moved;
        }

    return deepest;
}

SvfCoeffs DspCore::designAt (Shape shape, double hz, double q, double gainDb, float amount) const noexcept
{
    const auto designedQ = designQ (shape, q, current.cutMaxQ);

    if (! (amount < 1.0f))
        return SvfCoeffs::fromBiquad (designMatched (shape, hz, designedQ, gainDb, grid));

    const auto a = (double) std::max (amount, 0.0f);
    SvfCoeffs c;

    if (hasGain (shape))
    {
        // A bell or shelf scales its gain in dB, through the filters its GAIN
        // knob would give. A shelf also takes its Q down to Butterworth on the
        // way, because a resonant shelf's response at its middle gains sits
        // up to 4.5 dB under (or, cutting, over) both its setting and flat --
        // a +12 dB shelf at Q 2 is -4.5 dB at 0.66 f0 where +24 dB is +0.7.
        auto shapedQ = designedQ;

        if (shape == Shape::lowShelf || shape == Shape::highShelf)
        {
            const auto plain = std::min (designedQ, kPlainQ);
            shapedQ = std::exp (a * std::log (designedQ) + (1.0 - a) * std::log (plain));
        }

        c = SvfCoeffs::fromBiquad (designMatched (shape, hz, shapedQ, gainDb * a, grid));
    }
    else
    {
        // A cut has no gain to scale, and blending one with its input can dip:
        // a second-order cut is in quadrature with its input at its corner.
        // Instead its poles stay and its zeros walk from where the cut has
        // them to the poles themselves, where the filter is its input. On
        // the structure's outputs, x = hp + k bp + lp, so the cut is
        // (A hp + B bp + C lp) and nothing is (hp + k bp + lp); the hp and lp
        // weights move with alpha^2 and the bp weight with alpha, which keeps
        // the numerator's coefficients positive -- its zeros stay in the
        // left half-plane, off the frequency axis, so there is never a notch
        // -- and moves the stopband 40 log10 alpha dB. alpha = cos (pi a / 2)
        // is what keeps the level between the two ends: it is sqrt (1 - a) in
        // the middle, which measured best for that, without the infinite
        // slope sqrt has as the cut arrives, which stepped there; 1 - a
        // stepped less but left 4.9 dB holes (DeqDspTests,
        // testShapeChangeLeavesNoHole).
        c = SvfCoeffs::fromBiquad (designMatched (shape, hz, designedQ, 0.0, grid));

        const auto alpha = std::cos (0.5 * kPi * a), alpha2 = alpha * alpha;
        const auto onHp = c.m0, onBp = c.m1 + c.k * c.m0, onLp = c.m2 + c.m0;
        const auto hp = onHp + (1.0 - onHp) * alpha2;
        const auto bp = onBp + (c.k - onBp) * alpha;
        const auto lp = onLp + (1.0 - onLp) * alpha2;

        c.m0 = hp;
        c.m1 = bp - c.k * hp;
        c.m2 = lp - hp;
    }

    // Nothing is exactly the input, whatever the state, so a shape arriving
    // from rest has no start-up transient and one leaving can be dropped.
    if (! (a > 0.0))
    {
        c.m0 = 1.0;
        c.m1 = c.m2 = 0.0;
    }

    return c;
}

Biquad DspCore::bandDesign (int band) const noexcept
{
    const auto& s = current.bands[(size_t) band];
    return designMatched (s.shape, s.frequencyHz, designQ (s.shape, s.q, current.cutMaxQ), s.gainDb, grid);
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
        if (! (b.m.isNormal() && b.s.isNormal() && b.oldM.isNormal() && b.oldS.isNormal()
               && b.sideM.isNormal() && b.sideS.isNormal() && b.detector.isNormal()))
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
