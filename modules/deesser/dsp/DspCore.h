#pragma once

#include "modules/deesser/params.h"

#include <algorithm>
#include <atomic>
#include <cmath>

namespace bmo::deesser
{

//==============================================================================
/** Which filter the module cuts with, and detects through.

    *Bell* is the surgical one: a constant-Q band, narrow enough to miss the
    vowel region. *High shelf* is the split-band mode without a crossover --
    one minimum-phase filter, so reconstruction error is identically zero at
    every depth, which is the whole reason this topology was chosen over a
    crossover (docs/deesser/10-dsp-spec.md 1).

    The labels and this index order are permanent; they are BMO DEQ's, so that
    two modules doing the same thing to a band call it the same thing. */
enum class Shape { bell = 0, highShelf };

//==============================================================================
/** **Placeholder core. There is no de-esser in this file yet.**

    What is here is the frame the real one is written into, and it does exactly
    nothing to the audio: samples come out as they went in, gain reduction
    reports a flat zero, and latency is zero -- which, unlike the other two, is
    the *shipped* figure and not a placeholder value.

    The spec is `docs/deesser/10-dsp-spec.md`; what the tests will ask of it is
    `docs/deesser/11-integration-and-test-plan.md` 5. The DSP pass owns this
    folder and nothing outside it.

    **What must survive the real implementation** -- the contract the rest of
    the module is already built against:

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

    Per `docs/deesser/00-repo-conventions.md` 1, DEQ's `Svf.h` and `Dynamics.h`
    are **copied into this folder** when the DSP lands, never included: there is
    no precedent in this repo for one module including another's `dsp/` files.
    11 section 1 names the two files they land as, `Band.h` and `Detector.h`.
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
        reset();
    }

    /** **Placeholder: there is no state to clear.** The real core resets the
        filters, all three envelopes and the glide here, and snaps the first
        parameter set after a prepare or a reset rather than gliding to it. */
    void reset() noexcept {}

    void setParams (const Params& p) noexcept { params = p; }

    /** **Placeholder: the audio is not touched.**

        Deliberately a wire rather than a token filter. A placeholder that did
        something would have to be undone, and a listening pass run against it
        would be a listening pass against nothing in particular; a wire is
        obviously not a de-esser to anyone who opens it.

        Listen is honoured the same way -- see `setSolo`. */
    void process (float* const* channels, int numChannels, int numSamples) noexcept
    {
        (void) channels;
        (void) numChannels;
        (void) numSamples;
    }

    /** Zero at every setting, and **this one is shipped, not placeheld**.

        No lookahead and no oversampling, so there is nothing to report and
        nothing that could ever start reporting. Latency is permanent once
        shipped, which makes this the expensive thing to change your mind
        about -- 10 section 1 and 11 section 5's interface row both pin it. */
    static constexpr int latencySamples() noexcept { return 0; }

    /** **Placeholder: always zero.** The real core reports the **peak band
        reduction** -- the magnitude of the applied, glided offset, signed
        positive for reduction, 0..18 dB inside the shared 24 dB meter. Not a
        wideband-equivalent figure; see the class comment. */
    float currentGainReductionDb() const noexcept { return 0.0f; }

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

    /** An atomic store on the panel's side and a relaxed load on the audio
        thread's: no allocation and no lock, which is the whole of the
        `setSolo` contract. */
    std::atomic<bool> listening { false };
};

} // namespace bmo::deesser
