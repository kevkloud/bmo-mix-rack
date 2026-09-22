#pragma once

// For `bmo::kMaxTailSeconds`, which is the suite's tail ceiling and is read
// here rather than copied: the rack clamps its summed total at the same
// figure, and two 30.0s written down in two folders is how they come to differ.
#include "core/dsp/ModuleDsp.h"
#include "modules/reverb/dsp/TapTables.h"
#include "modules/reverb/params.h"

#include <algorithm>
#include <cmath>

namespace bmo::reverb
{

//==============================================================================
/** Which constant block the engine runs. Six in v1; the order is frozen with
    the choice list in params.h, and four more append later.

    A type changes **only constants** over one shared topology: the ER tap
    table, the ER window and default density, the eight FDN delay times, the
    input-diffusion depth, damping and modulation defaults, input bandwidth,
    the default ER feed, and three reserved era fields. No audio-path branch
    beyond a table lookup, and buffers sized in `prepare()` for the largest
    type, so switching never allocates (10 section 1).

    **The engine sees none of the writing.** Ten of those constants are also
    parameters (`kTypeConstants`), and selecting a type stamps them through the
    parameter set before this enum ever changes -- so by the time `setParams`
    runs, `Params` already carries the new values and there is nothing here to
    special-case. The rest, the ones with no host lane, are this file's to hold.

    `cavern` was `largeHall` until 2026-09-21. The ordinal did not move; see
    `kTypeNames`. */
enum class Type { room = 0, chamber, hall, cavern, plate, ambience };

/** How the early cluster is generated. See `kErModeNames` in params.h for what
    Blend is: defined, reachable, and **not yet heard**. */
enum class ErMode { taps = 0, energy, blend };

//==============================================================================
/** **Placeholder core. There is no reverb in this file yet.**

    Samples come out as they went in. Latency is zero -- which, unlike the
    silence, is the *shipped* figure rather than a stand-in -- and no tail is
    produced, so nothing downstream has to pretend one is there.

    The spec is `docs/reverb/10-dsp-spec.md`; what the tests will ask of it is
    `docs/reverb/11-integration-and-test-plan.md` section 6, which is the
    longest test plan in the repository and worth reading before the first line
    of engine is written. The DSP pass owns this folder and nothing outside it.

    **What must survive the real implementation** -- the contract the rest of
    the module is already built against:

    - `Params` below carries all thirty parameters, **in real units and in
      engine units**, and `ReverbDsp.h` is the only thing that unpacks the flat
      array. **Add DSP state here, not parameters**: the schema freezes at
      first ship with only two spare host lanes, and the era block, beta, the
      tap cutoff law, the density ramp width and the eight FDN times are all
      internal constants by decision (10 sections 1, 3, 4).
    - `latencyForParams` is **0 at every setting, permanently**. There is no
      lookahead, no oversampling and no negative pre-delay: 10 section 2
      refuses the last of those by name, because a negative pre-delay delays
      the module's whole output against the host timeline, which is latency,
      and an automatable one would thrash PDC on every move. Dimension reports
      zero for exactly this reason (`modules/dim/dsp/DimDsp.h:45-58`). That is
      the costly decision to undo, so it is stated in three places.
    - **Nothing in this module uses `setSolo`, `currentGainReductionDb` or an
      `AnalyserTap`.** The panel's display is parameter-driven only; neither
      doc asks for metering, and a reverb has no gain reduction to report. If
      one of those ever arrives it is a design change, not a fill-in.
    - `tailSecondsFor` below is the figure the host should be told, and
      **nothing tells it yet**: both processors still hardcode
      `getTailLengthSeconds()` to 0.0 and `ModuleDsp` has no tail accessor.
      Adding `tailSecondsForParams` to `ModuleDsp` touches every module's
      vtable, so it is milestone M5 and its own reviewed commit (11 section
      2a). The arithmetic lives here now so that when the accessor lands it has
      nothing to invent.
*/
class DspCore
{
public:
    /** Everything the real engine is given, one field per parameter, in the
        units the engine wants rather than the units a host shows.

        Three conversions happen in the adapter and nowhere else: the four
        percentage controls arrive as 0..1, WIDTH as 0..2, and the two choice
        indices as the enums above. Everything else is already in engine units
        on the knob -- metres, milliseconds, seconds, hertz, decibels, a bare
        multiplier, a bare exponent -- which is the point of a schema written
        in real units. */
    struct Params
    {
        Type  type          = Type::room;
        float sizeM         = roomDefaults::kSizeM;         ///< 0.5..80
        float preDelayMs    = 0.0f;                         ///< 0..250, tail only, never negative
        bool  linkEr        = false;                        ///< ER travel with the tail, not with dry
        float decaySeconds  = 1.8f;                          ///< 0.1..20, T_mid
        float decayShape    = 3.5f;                          ///< 0.04..3.5; 3.5 is linear, i.e. off
        float attack        = 0.30f;                         ///< 0..1 over the 0-120 ms bloom
        float feed          = roomDefaults::kFeed * 0.01f;   ///< 0..1; d in (1-d)*direct + d*ER

        float dampLoFreqHz  = 200.0f;                        ///< 16..1600, the low knee
        float dampLo        = 1.20f;                         ///< 0.10..2.00, T60 multiplier below it
        float dampHiFreqHz  = 1600.0f;                       ///< 1000..2100, the high knee
        float dampHi        = 0.40f;                         ///< 0.10..2.00, T60 multiplier above it

        float eqLoFreqHz    = 200.0f;                        ///< 16..1600
        float eqLoDb        = 0.0f;                          ///< -24..+12; -24 is "Cut"
        float eqHiFreqHz    = 1600.0f;                       ///< 1000..2100
        float eqHiDb        = 0.0f;                          ///< -24..+12

        ErMode erMode       = ErMode::taps;
        float erDensity     = roomDefaults::kErDensity * 0.01f;   ///< 0..1, the bridge
        float erShape       = roomDefaults::kErShape;             ///< 0..3, the rise exponent p
        float erSpreadMs    = roomDefaults::kErSpreadMs;          ///< 5..200, the envelope sigma
        float erHiCutHz     = 7000.0f;                            ///< 1000..20000, one post-ER shelf
        int   erVariation   = 2;                                  ///< 0..6; 6 is the comb pair

        float modDepthMs    = roomDefaults::kModDepthMs;     ///< 0.1..0.8
        float modRateHz     = roomDefaults::kModRateHz;      ///< 0.1..1.2
        float width         = 1.0f;                          ///< 0..2, M/S gain on the tail only
        float inHiCutHz     = roomDefaults::kInHiCutHz;      ///< 2000..20000, ahead of both generators

        float erLevelDb     = roomDefaults::kErLevelDb;      ///< -40..0; -40 is silence, not -40 dB
        float verbLevelDb   = roomDefaults::kVerbLevelDb;    ///< -40..0; likewise
        float mix           = 1.0f;                          ///< 0..1
        float outputDb      = 0.0f;                          ///< -24..0
    };

    //== The internal constants =================================================
    //
    // Fixed at first ship, invisible to the host, and free to be retuned right
    // up until it. 10 section 7 is the table they come from and nearly every
    // row there is marked CALIBRATE. They are declared here rather than left
    // in the DSP pass's head because the *decision* that they are constants
    // and not parameters is a schema decision -- there are only two spare host
    // lanes -- and it is this file's job to hold it.
    //
    // The placeholder ignores every one of them.

    /** Eight FDN lines, Householder matrix. **10 section 4 is the live risk
        here**: the mode-density rule scales with decay, Sum(m_i) >= 0.15 * T60
        * fs, so eight lines cover Hall to about 2.9 s and Plate to barely 1 s.
        The fix is 16 lines for the long-decay types, a larger mean delay, or
        accepting sparsity and letting modulation carry the colour -- and the
        first of those takes the CPU budget with it. Write the modal-density
        test before Plate is tuned and expect it red. */
    static constexpr int kNumLines = 8;

    /** The speed of sound, m/s -- the constant that turns a path length into a
        tap time, and the only figure in this file that is not CALIBRATE. */
    static constexpr float kSpeedOfSound = 343.0f;

    /** The density bridge's ramp width: w_k(D) = clamp((D - theta_k) / kRampWidth,
        0, 1). Wide enough that no tap appears at a non-zero level, so the
        sweep cannot click; narrow enough that the sweep still has a top end.
        CALIBRATE. */
    static constexpr float kRampWidth = 0.08f;

    /** Parameter-change crossfade, raised cosine, windows summing to one --
        `DetuneVoice`'s scheme exactly (`modules/dim/dsp/DspCore.h:61-139`).
        TYPE dips the wet bus over this and swaps tables at the minimum; SIZE
        and PRE-DELAY crossfade two tap sets over it, **ER and late sharing the
        scheme** so the two cannot drift apart. Re-triggered when accumulated
        |dS| passes 1 %. A glide instead would Doppler every reflection at
        once, which on a vocal is a chorus and not a room. */
    static constexpr float kCrossfadeMs = 30.0f;

    /** One-pole smoothing for everything that is only a coefficient change:
        decay, damping, EQ, Attack, the levels and mix. */
    static constexpr float kSmoothingMs = 20.0f;

    /** The wet bus fades over this in `reset()`. There is no per-slot enable
        flag in the rack -- modules are present or absent -- so a removed
        reverb truncates its tail, and this is what stops that being a click
        into the rest of the chain (10 section 5). */
    static constexpr float kBypassFadeMs = 150.0f;

    /** The ceiling on a reported tail, **the suite's own and not a second
        opinion about it**.

        `bmo::kMaxTailSeconds` (core/dsp/ModuleDsp.h) is where the number is
        decided, because the rack clamps its summed total at the same figure
        and `core` cannot include this file to find out what it is. This is the
        float the module's own arithmetic uses, named here so the display and
        the tests can go on reading it where they always did. */
    static constexpr float kMaxTailSeconds = (float) bmo::kMaxTailSeconds;

    //==========================================================================

    void prepare (double newSampleRate, int maxBlockSize, int numChannels)
    {
        // Recorded so the real engine has them, and so a test can see that
        // prepare() was reached. The placeholder allocates nothing because it
        // needs nothing; the real one allocates **here and only here**, sized
        // for the largest type at this rate (~300 kB at 48 k, ~1.2 MB at
        // 192 k), and `process()` allocates nothing ever.
        sampleRate = newSampleRate;
        blockSize  = maxBlockSize;
        channels   = numChannels;
    }

    void reset() {}

    void setParams (const Params& p) { params = p; }

    const Params& getParams() const noexcept { return params; }

    /** **Pass-through.** Marked, not forgotten: see the class comment. */
    void process (float* const* /*channelData*/, int /*numChannels*/, int /*numSamples*/) {}

    /** Zero, at every setting, always. Not computed from the values, because
        nothing in the schema can move it. */
    static constexpr int latencySamples() noexcept { return 0; }

    /** The tail the host should be told about, in seconds:

            preDelay + T_mid * max(1, r_lo, r_hi) + t_ER,max + 0.05 s

        clamped to `kMaxTailSeconds` (10 section 5). From parameter values
        rather than from DSP state, which is what lets it be answered before
        the audio thread has picked a change up -- the same reason
        `latencyForParams` takes values rather than reading state.

        **This is the only copy of the formula.** It is reached from a host
        through `ReverbDsp::tailSecondsForParams`, which is nothing but the
        unpacking in front of it (11 section 2a, milestone M5); neither
        processor computes anything of its own. `tests/plugin/TailTests.cpp`
        asserts the figures a host is handed against seconds written down by
        hand, so the two cannot quietly become different arithmetic.

        The rack **sums** this across occupied slots rather than taking the
        maximum: slots are in series, so 4 s feeding 2 s rings longer than
        either. Under-reporting truncates tails; over-reporting only costs idle
        pulling. The ceiling below is therefore per module, not per rack. */
    static float tailSecondsFor (const Params& p) noexcept
    {
        const auto longest = std::max (1.0f, std::max (p.dampLo, p.dampHi));
        const auto seconds = p.preDelayMs * 0.001f
                           + p.decaySeconds * longest
                           + erSpanMsAt (p.sizeM) * 0.001f
                           + 0.05f;

        return std::min (seconds, kMaxTailSeconds);
    }

private:
    Params params;

    double sampleRate = 0.0;
    int blockSize = 0;
    int channels = 0;
};

} // namespace bmo::reverb
