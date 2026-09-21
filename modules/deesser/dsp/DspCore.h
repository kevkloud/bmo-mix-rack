#pragma once

#include "modules/deesser/dsp/Band.h"
#include "modules/deesser/dsp/Detector.h"
#include "modules/deesser/params.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>

namespace bmo::deesser
{

//==============================================================================
/** The de-esser: a level-independent detector driving a dynamic-EQ cut.

    A band filter and a reference rectifier watch the **dry** input; the
    detector turns the two into a *prominence* -- how far the band stands above
    the signal, in dB, rather than how loud it is -- and a static curve turns
    that into a depth. That depth redesigns one TPT state-variable filter every
    eight samples, and its coefficients are walked there a sample at a time.

    No lookahead, no oversampling and no crossover, and therefore **no latency
    at any setting**. `Detector.h` holds the deciding and `Band.h` the cutting;
    this file is the wiring, the clamps and the order.

    The spec is `docs/deesser/10-dsp-spec.md`; what the tests ask of it is
    `docs/deesser/11-integration-and-test-plan.md` 5.

    **Nothing here has been heard.** Every constant below marked CALIBRATE is
    a first pass: the arithmetic is tested, the sound is not.

    **The contract the rest of the module is built against:**

    - `Params` below carries every parameter `specs()` has, in real units, and
      the adapter (DeesserDsp.h) is the only thing that unpacks the flat array.
      **Add DSP state here, not parameters**: the schema is frozen at first
      ship and ADAPT's `kappa`, the attack and release times, the knee, the
      slope and the two gates are all internal constants below, deliberately.
    - `latencyForParams` is **0 at every setting**, and permanently so. There
      is no lookahead and no oversampling: 10 section 1 refuses both, and the
      DSP test asserts zero across `prepare`/`reset` by impulse correlation.
      That is the costly decision to undo, so it is stated in two places.
    - `currentGainReductionDb()` is **signed, positive = gain taken away**
      (core/dsp/ModuleDsp.h), and it reports the **peak band reduction** -- the
      magnitude of the currently applied, glided offset -- and *not* a
      wideband-equivalent figure. 10 section 8 has the argument: a 6 dB bell
      cut at Q 2.5 removes under 1 dB of broadband energy, so a wideband meter
      would read about 0.5 dB exactly when the user is doing the 6 dB of work
      the module is for.
    - `setSolo` is the **listen path**, momentary and never a parameter. -1 is
      off; any index at or above 0 is on, because this module has one band. See
      `setSolo` below for what it must output and what it must not.
    - Detection is taken from the **dry** input, never from the moving output
      filter, whose poles track its own gain -- tapping that would close a
      feedback loop (the reason is recorded at `modules/deq/dsp/DspCore.h`
      100-111, which is the engine this one is shaped after).
    - Band and reference levels are **power-summed across channels**, so both
      channels take identical gain and a hard-panned sibilant cannot shift the
      image. That is why there is no stereo-link parameter to get wrong.

    **Where the pieces came from**, settled 2026-09-21 and recorded in
    docs/deesser/00-repo-conventions.md 1: the *detector* is this module's own
    (`Detector.h`, adapted from BMO DEQ's `Dynamics.h`), because the repo bars
    a shared compressor-detector library. The *filter design* is shared
    (`core/dsp/Design.h`), because it is arithmetic with one right answer and
    two copies of a least-squares fit would need correcting twice.
*/
class DspCore
{
public:
    /** Everything the real DSP is given, in real units. Five fields for five
        parameters -- nothing here is derived, smoothed or pre-converted,
        because the one place a host value becomes a DSP unit is the adapter in
        DeesserDsp.h and it should stay readable in one screen. */
    struct Params
    {
        float freqHz   = 6500.0f;   ///< 2000..10000; band centre, or the shelf corner
        float q        = 2.5f;      ///< 0.7..6.0; the engine clamps 0.1..40 behind it
        float threshDb = 0.0f;      ///< -24..+24, **prominence dB, not dBFS**
        float rangeDb  = 8.0f;      ///< 1..18; the cut is clamped to this depth
        Shape shape    = Shape::bell;
    };

    //== The internal constants =================================================
    //
    // Fixed at first ship, invisible to the host, and free to be retuned right
    // up until it -- docs/deesser/10-dsp-spec.md 9 is the table these come
    // from, and every one of them is marked CALIBRATE there. They are declared
    // here rather than left in the DSP pass's head because the *decision* that
    // they are constants and not parameters is a schema decision, and it is
    // this file's job to hold it.
    //
    // The placeholder ignores all of them.

    /** The reference blend. 1 compares the band with the whole signal right
        now; 0 compares it with the band's own last half second, which is the
        guard against constantly-bright material and cymbal bleed. **v1 ships
        one fixed middle value** -- the owner likes the idea of exposing it and
        wants it proven first, so ADAPT is potential work (10 section 11) and
        appends after `shape` if the listening pass earns it. 0.6 is a first
        pass, not a measurement. */
    static constexpr float kKappa = 0.6f;

    static constexpr float kAttackMs      = 0.8f;   ///< ~90 % applied 2 ms into an onset
    static constexpr float kReleaseFastMs = 30.0f;  ///< the default branch, an ess tail
    static constexpr float kReleaseSlowMs = 120.0f; ///< crossfaded in after kSlowEngageMs
    static constexpr float kSlowEngageMs  = 150.0f; ///< continuously over threshold
    static constexpr float kSlowRefMs     = 500.0f; ///< the programme's brightness memory

    static constexpr float kKneeDb        = 6.0f;   ///< a hard corner snaps on a 60 ms event
    static constexpr float kSlope         = 4.0f;   ///< the fixed 4:1 internal ratio
    static constexpr float kHoldMs        = 5.0f;   ///< an /s/-/t/ cluster is one event
    static constexpr float kHysteresisDb  = 1.5f;   ///< threshold drop while engaged

    static constexpr float kRefGateDb  = -55.0f;    ///< reference below this: offset exactly 0
    static constexpr float kBandGateDb = -60.0f;    ///< band below this: offset exactly 0
    static constexpr float kRefHighPassHz = 150.0f; ///< ahead of the reference rectifier

    /** The engine clamps, which exist because hosts send anything. `freqHz` is
        additionally clamped to 0.45 * Fs, so a 10 kHz bell at 44.1 kHz cannot
        cramp. */
    static constexpr float kMinQ = 0.1f, kMaxQ = 40.0f;
    static constexpr float kMaxDepthDb = 30.0f;
    static constexpr float kMaxFreqFraction = 0.45f;

    /** Control-rate interval, in samples. It is a *rate*, so the coefficient
        glide is specified in milliseconds and converted -- never in ticks, or
        modulation behaviour changes with the sample rate (10 section 7). */
    static constexpr int kControlInterval = 8;

    //==========================================================================

    void prepare (double newSampleRate, int /*maxBlockSize*/, int numChannels) noexcept
    {
        rate = newSampleRate;
        numActiveChannels = std::clamp (numChannels, 1, kMaxChannels);

        // 48 pow() and sin() calls, built once and reused by every design
        // afterwards: it was most of a shelf design's cost (core/dsp/Design.h).
        grid = dsp::DesignGrid::make (rate);

        Prominence::Config pc;
        pc.attackMs      = kAttackMs;
        pc.releaseFastMs = kReleaseFastMs;
        pc.slowRefMs     = kSlowRefMs;
        pc.kappa         = kKappa;
        pc.refGateDb     = kRefGateDb;
        pc.bandGateDb    = kBandGateDb;
        detector.prepare (pc, rate);

        Reduction::Config rc;
        rc.attackMs      = kAttackMs;
        rc.releaseFastMs = kReleaseFastMs;
        rc.releaseSlowMs = kReleaseSlowMs;
        rc.slowEngageMs  = kSlowEngageMs;
        rc.holdMs        = kHoldMs;
        rc.hysteresisDb  = kHysteresisDb;
        reduction.prepare (rc, rate);

        // The reference is rectified above 150 Hz, so a kick or a bass note
        // cannot raise the fullband level the band is judged against and stop
        // the module hearing an ess over it. A plain second-order high-pass,
        // which is what `detectorDesign` builds in shelf mode.
        refCoeffs = dsp::SvfCoeffs::fromBiquad (
            detectorDesign (Shape::highShelf, kRefHighPassHz, 0.707, rate));
        refTaps = dsp::SvfTaps::of (refCoeffs.g, refCoeffs.k);

        reset();
    }

    /** Clears the filters, all three envelopes and the glide, and arranges for
        the next parameter set to be **snapped** rather than glided to: a core
        that has just been prepared has nowhere to glide from, and gliding from
        a zeroed coefficient set would sweep the band up from DC on the first
        eight samples of every session. */
    void reset() noexcept
    {
        band.reset();
        detector.reset();
        reduction.reset();

        for (auto& s : refState)
            s.reset();

        primed = false;
        sinceTick = 0;
        depthAtTick = depthTwoTicksAgo = 0.0;
        reportedDb.store (0.0f, std::memory_order_relaxed);
    }

    void setParams (const Params& p) noexcept { params = p; }

    /** The de-esser.

        Per sample: detect from the **dry** input, decide, then filter. The
        order matters and is the one thing here that cannot be rearranged --
        detection reads the input before the cut, never the output, whose poles
        track its own gain. Tapping that would close a feedback loop.

        The detector runs every sample; the *filter* is re-derived every
        `kControlInterval` samples and its coefficients are walked there one
        sample at a time. That split is what makes an 0.8 ms attack affordable:
        the expensive part is designing a biquad, and the cheap part is moving
        toward one. See docs/deesser/10-dsp-spec.md 5. */
    void process (float* const* channels, int numChannels, int numSamples) noexcept
    {
        if (channels == nullptr || numSamples <= 0)
            return;

        const auto chans = std::clamp (numChannels, 1, kMaxChannels);
        const auto listening = isListening();   // once a block, so block size cannot change it

        // Hosts send anything, so every one of these is clamped rather than
        // trusted. `freqHz` additionally clamps to a fraction of Fs, so a
        // 10 kHz band at 44.1 kHz cannot reach for a pole it has no room for.
        const auto hz = std::clamp ((double) params.freqHz, 2000.0,
                                    std::min (10000.0, (double) kMaxFreqFraction * rate));
        const auto q  = std::clamp ((double) params.q, (double) kMinQ, (double) kMaxQ);
        const auto rangeDb = std::clamp ((double) params.rangeDb, 0.0, (double) kMaxDepthDb);
        const auto shape = params.shape;

        band.designSide (shape, hz, q, rate);

        GainComputer computer;
        computer.kneeDb = kKneeDb;
        computer.slope  = kSlope;
        computer.rangeDb = rangeDb;

        for (int n = 0; n < numSamples; ++n)
        {
            //== Detect, from the dry input =================================
            //
            // Power-summed across channels rather than mono-summed:
            // decorrelation-safe, and it forces identical gain on both
            // channels so the image cannot wander on a hard-panned sibilant.
            // That is why there is no stereo-link parameter to get wrong.
            double bandPower = 0.0, refPower = 0.0;

            for (int c = 0; c < chans; ++c)
            {
                const auto x = (double) channels[c][n];
                const auto b = band.sideSample (c, x);
                const auto r = refState[(size_t) c].process (refTaps, refCoeffs, x);

                bandPower += b * b;
                refPower  += r * r;
            }

            const auto inv = 1.0 / (double) chans;
            const auto prominence = detector.process (std::sqrt (bandPower * inv),
                                                      std::sqrt (refPower * inv));

            // Hysteresis is applied to the threshold the detector is judged
            // against, so that having decided this is an ess the module is
            // slower to decide it has ended.
            computer.thresholdDb = (double) params.threshDb - reduction.thresholdOffsetDb();

            const auto applied = reduction.process (computer.reductionDb (prominence));

            //== Re-derive the cut, or walk toward the one already asked for ==
            if (sinceTick == 0)
            {
                band.design (shape, hz, q, applied, grid, kControlInterval, ! primed);
                primed = true;

                depthTwoTicksAgo = depthAtTick;
                depthAtTick = applied;
            }
            else
            {
                band.advance();
            }

            if (++sinceTick >= kControlInterval)
                sinceTick = 0;

            // **At zero depth the filter is bypassed, and that is exactness
            // rather than thrift.** A bell designed for 0 dB has a numerator
            // equal to its denominator, so it is unity -- but the coefficient
            // solve leaves m1 and m2 as rounding dust near 1e-17 rather than
            // at zero, and a sample run through that comes back changed in its
            // last bit. That is the difference between a module you can leave
            // inserted across a bus and one you cannot prove harmless. Handing
            // the sample back untouched is what the arithmetic says anyway.
            //
            // Two consecutive ticks at zero are required, not one: on the tick
            // where the depth first reaches zero the coefficients are still
            // gliding up to unity from a real cut, and bypassing then would
            // put a step exactly where the glide was.
            const auto unity = applied == 0.0 && depthAtTick == 0.0 && depthTwoTicksAgo == 0.0;

            //== Cut ==========================================================
            for (int c = 0; c < chans; ++c)
            {
                const auto x = (double) channels[c][n];

                // The filter is run either way, so its state stays current and
                // the first cut after a quiet passage starts from the signal
                // rather than from silence.
                const auto filtered = band.filterSample (c, x);
                const auto y = unity ? x : filtered;

                // Listen hands back the band's *contribution*, H(x) - x: the
                // sibilance being taken out. The filtered output would be the
                // whole signal with a dip in it, which is not the thing worth
                // auditioning. BMO DEQ's band solo does exactly this.
                channels[c][n] = (float) (listening ? y - x : y);
            }
        }

        band.flushTiny();
        detector.flushTiny();

        for (auto& s : refState)
            s.flushTiny();

        reportedDb.store ((float) reduction.appliedDb(), std::memory_order_relaxed);
    }

    /** Zero at every setting, and **this one is shipped, not placeheld**.

        No lookahead and no oversampling, so there is nothing to report and
        nothing that could ever start reporting. Latency is permanent once
        shipped, which makes this the expensive thing to change your mind
        about -- 10 section 1 and 11 section 5's interface row both pin it. */
    static constexpr int latencySamples() noexcept { return 0; }

    /** The **peak band reduction**: the magnitude of the applied, glided
        offset, signed positive for reduction, 0..RANGE dB. Not a
        wideband-equivalent figure; see the class comment.

        The **applied** figure and never the target, so the meter cannot show
        a reduction the audio did not get. Written once a block by the audio
        thread and read by the panel, which is why it is an atomic. */
    float currentGainReductionDb() const noexcept
    {
        return reportedDb.load (std::memory_order_relaxed);
    }

    /** Hear the sibilance being caught, or -1 for the whole signal.

        **Momentary, never a parameter, never saved.** The panel sets it while
        the button is held and clears it on release, and clears it again when
        the editor closes -- a solo left on in a saved session is a support
        ticket (core/dsp/ModuleDsp.h). This module has one band, so any index
        at or above 0 means on; -1 means off.

        **What it must output is the band's contribution, `H(x) - x`** -- the
        sibilance being removed -- which is BMO DEQ's rule exactly
        (`modules/deq/dsp/DspCore.h` 133-149) and the quantity worth listening
        to. The filtered *output* would be the whole signal with a dip in it,
        which is not. Two things the DSP test pins: soloed output nulls against
        `H(x) - x` to -100 dB, and -1 restores **bit-identical** output.

        **Placeholder: audio passes through unchanged in either state**, so
        what is proven here today is the hook and the momentary lifecycle, not
        the path. Read once per block by the real core, so it cannot break
        block-size invariance. */
    void setSolo (int index) noexcept { listening.store (index >= 0, std::memory_order_relaxed); }
    bool isListening() const noexcept { return listening.load (std::memory_order_relaxed); }

    const Params& getParams() const noexcept { return params; }
    double sampleRate() const noexcept { return rate; }
    int activeChannels() const noexcept { return numActiveChannels; }

private:
    static constexpr int kMaxChannels = 2;

    Params params;
    double rate = 48000.0;
    int numActiveChannels = kMaxChannels;

    dsp::DesignGrid grid {};
    Band band;
    Prominence detector;
    Reduction reduction;

    /** The reference rectifier's high-pass, one state per channel. */
    dsp::SvfCoeffs refCoeffs {};
    dsp::SvfTaps   refTaps {};
    std::array<dsp::SvfState, kMaxChannels> refState {};

    /** False until the first control tick after a prepare or a reset, which
        snaps rather than glides. */
    bool primed = false;
    int  sinceTick = 0;

    /** The depth at the last two control ticks. Two, because the bypass at
        zero depth may only engage once the coefficient glide has finished
        arriving at unity. */
    double depthAtTick = 0.0, depthTwoTicksAgo = 0.0;

    /** Written once a block by the audio thread, read by the panel. */
    std::atomic<float> reportedDb { 0.0f };

    /** An atomic store on the panel's side and a relaxed load on the audio
        thread's: no allocation and no lock, which is the whole of the
        `setSolo` contract. */
    std::atomic<bool> listening { false };
};

} // namespace bmo::deesser
