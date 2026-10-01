#pragma once

// For `bmo::kMaxTailSeconds`, which is the suite's tail ceiling and is read
// here rather than copied: the rack clamps its summed total at the same
// figure, and two 30.0s written down in two folders is how they come to differ.
#include "core/dsp/ModuleDsp.h"
#include "modules/reverb/dsp/EqNodes.h"
#include "modules/reverb/dsp/ErGenerator.h"
#include "modules/reverb/dsp/TapTables.h"
#include "modules/reverb/params.h"

#include <algorithm>
#include <cmath>
#include <vector>

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

/** How the early cluster is generated. See `kErModeNames` in params.h for why
    there are two. */
enum class ErMode { taps = 0, energy };

//==============================================================================
/** **M2: the early reflections play; the tail is silent.**

    `ErGenerator.h` is the early-reflection generator -- image-source tables,
    the Size law and its crossfade, four order-banded poles, the density
    bridge and its feed-forward diffuser, seven VARIATION positions, the ER
    hi-cut -- and this class feeds it the mid of the input, takes the ER bus
    back at table level, and applies the two faders, the MIX law
    and OUTPUT with 20 ms smoothing. The late network (M3) and the six type
    blocks (M4) are still to come: REVERB's fader is smoothed and applied to a
    bus that is all zeros, so it already behaves, and the Reverb EQ is not
    yet in the signal path. Latency is zero, which is the *shipped* figure.

    The spec is `docs/reverb/10-dsp-spec.md`; what the tests ask of it is
    `docs/reverb/11-integration-and-test-plan.md` section 6, whose ER block is
    `tests/dsp/ReverbDspTests.cpp`. The DSP pass owns this folder and nothing
    outside it.

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
    - **`setSolo` and `currentGainReductionDb` are still unused**, and that
      part has not moved: a reverb has no gain reduction to report and there is
      no part of it to hear on its own. **The `AnalyserTap` is no longer in
      that list.** The owner asked for a spectrum behind the EQ page's response
      curve on 2026-09-21, so `eqAnalyser()` below is a real tap at the point
      the Reverb EQ acts on -- see it for why it shows the dry input until
      there is an engine, and why that is honest rather than broken. EARLY and
      TAIL stay parameter-driven.
    - `tailSecondsFor` below is the figure the host is told. It reaches a host
      through `ModuleDsp::tailSecondsForParams` (milestone M5, 11 section 2a),
      which `ReverbDsp` answers by unpacking the values and calling it, and
      both processors report it from a cache refreshed where their latency is.
      The arithmetic lives here and only here, so the accessor had nothing to
      invent when it landed.
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
        in real units.

        **Six of these fields no longer have a knob behind them**, since the
        2026-09-21 control-set trim: `linkEr` is `kPreLinkFixed`, and
        `decayShape`, `attack`, `dampLoFreqHz`, `dampHiFreqHz` and `erShape`
        come off the selected type's row in `kTypeConstants`. The struct did
        not change shape, because the engine still needs all six -- what
        changed is where the adapter reads them from. The defaults below are
        Room's row for exactly that reason, so a default-constructed `Params`
        is a Room and not a mixture. */
    struct Params
    {
        Type  type          = Type::room;
        float sizeM         = roomDefaults::kSizeM;         ///< 0.5..80
        float preDelayMs    = 0.0f;                         ///< 0..250, tail only, never negative
        bool  linkEr        = kPreLinkFixed;                ///< fixed: ER travel with dry
        float decaySeconds  = 1.8f;                          ///< 0.1..20, T_mid
        float decayShape    = roomDefaults::kDecayShape;      ///< per type; 3.5 is linear, i.e. off
        float attack        = roomDefaults::kAttack * 0.01f;  ///< per type, 0..1 over the 0-120 ms onset
        float feed          = roomDefaults::kFeed * 0.01f;   ///< 0..1; d in (1-d)*direct + d*ER

        float dampLoFreqHz  = roomDefaults::kDampLoFreqHz;   ///< per type, the low knee
        float dampLo        = 1.20f;                         ///< 0.10..2.00, T60 multiplier below it
        float dampHiFreqHz  = roomDefaults::kDampHiFreqHz;   ///< per type, the high knee
        float dampHi        = 0.40f;                         ///< 0.10..2.00, T60 multiplier above it

        //== The Reverb EQ: three nodes, fixed shapes, one mode ================
        //
        // Node 1 low shelf, node 2 bell, node 3 high shelf, and `eqFilter`
        // turns one or both of the outer two into cuts. `EqNodes.h` is the
        // arithmetic and the argument; `eqSettings()` below is the only thing
        // that reads these ten fields, so the engine and the panel design one
        // set of filters.
        EqFilter eqFilter   = EqFilter::off;                 ///< which outer nodes are cuts
        float eqLoFreqHz    = 200.0f;                        ///< 16..1600
        float eqLoDb        = 0.0f;                          ///< -24..+12; -24 is "Cut"; withheld where node 1 is a cut
        float eqLoQ         = 0.71f;                         ///< 0.1..2, kShelfMaxQ
        float eqMidFreqHz   = 1000.0f;                       ///< 20..20000
        float eqMidDb       = 0.0f;                          ///< -24..+12
        float eqMidQ        = 0.71f;                         ///< 0.1..40, a bell's range
        float eqHiFreqHz    = 6000.0f;                       ///< 1000..20000
        float eqHiDb        = 0.0f;                          ///< -24..+12; withheld where node 3 is a cut
        float eqHiQ         = 0.71f;                         ///< 0.1..2

        ErMode erMode       = ErMode::taps;
        float erDensity     = roomDefaults::kErDensity * 0.01f;   ///< 0..1, the bridge
        float erShape       = roomDefaults::kErShape;             ///< per type, the rise exponent p
        float erSpreadMs    = roomDefaults::kErSpreadMs;          ///< 5..200, the envelope sigma
        float erHiCutHz     = 7000.0f;                            ///< 1000..20000, one post-ER shelf
        int   erVariation   = 4;                                  ///< 0..6; 6 is the comb pair

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
    // The ER generator reads its own copies of the ones it needs
    // (`ErGenerator::kRampWidth`, `kCrossfadeMs`); the late network will read
    // the rest.

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

    /** The ten EQ fields as `EqNodes.h` wants them. The **only** reader of
        them, so the curve the panel draws and the filters the engine will run
        are designed from one struct rather than from two transcriptions of
        one. */
    static EqSettings eqSettingsFor (const Params& p) noexcept
    {
        EqSettings s;
        s.filter    = p.eqFilter;
        s.loFreqHz  = p.eqLoFreqHz;   s.loDb  = p.eqLoDb;   s.loQ  = p.eqLoQ;
        s.midFreqHz = p.eqMidFreqHz;  s.midDb = p.eqMidDb;  s.midQ = p.eqMidQ;
        s.hiFreqHz  = p.eqHiFreqHz;   s.hiDb  = p.eqHiDb;   s.hiQ  = p.eqHiQ;
        return s;
    }

    EqSettings eqSettings() const noexcept { return eqSettingsFor (params); }

    void prepare (double newSampleRate, int maxBlockSize, int numChannels)
    {
        // Everything the engine owns is allocated **here and only here**,
        // sized for the largest type at this rate; `process()` allocates
        // nothing ever.
        sampleRate = newSampleRate > 0.0 ? newSampleRate : 48000.0;
        blockSize  = std::max (1, maxBlockSize);
        channels   = numChannels;

        er.prepare (sampleRate, blockSize);
        pushErConfig();

        feed.assign ((size_t) blockSize, 0.0f);
        erL.assign ((size_t) blockSize, 0.0f);
        erR.assign ((size_t) blockSize, 0.0f);

        smoothCoef = 1.0f - std::exp (-1.0f / (kSmoothingMs * 0.001f * (float) sampleRate));
        snapGains();

        // The design grid the real EQ will build its three nodes on, built
        // once per rate change because 48 pow() and sin() calls is most of a
        // shelf design's cost (`dsp::DesignGrid`). Nothing runs it yet; it is
        // here so that `prepare` is where it lands when the engine arrives,
        // rather than being discovered on the audio thread.
        grid = dsp::DesignGrid::make (newSampleRate > 0.0 ? newSampleRate : kEqDesignRate);

        // 4096 samples is the analyser's frame (`Analyser::kFftSize`), and the
        // tap rounds up to a power of two anyway. Twice the frame so a reader
        // trailing the write head still gets a whole one.
        eqTap.prepare (1 << 13);
    }

    void reset()
    {
        er.reset();
        snapGains();
    }

    void setParams (const Params& p)
    {
        params = p;
        pushErConfig();
    }

    /** The early-reflection generator itself, for the tests and the
        measurement tool. */
    ErGenerator& earlyReflections() noexcept { return er; }
    const ErGenerator& earlyReflections() const noexcept { return er; }

    //== The level laws =========================================================

    /** A fader in dB to a linear gain, with the bottom of the range being
        **off** and not -40 dB (10 section 2). */
    static float faderGain (float db) noexcept
    {
        return db <= -39.95f ? 0.0f : std::pow (10.0f, db * 0.05f);
    }

    /** **The MIX law, Frosty's on 2026-09-24.** dry = min(1, 2(1 - mix)),
        wet = min(1, 2 mix): at 50 % the input is unchanged and the verb is
        heard at its faders, below it the wet comes down alone, above it the
        dry goes away alone, and 100 % is verb only, for use as a send. The
        default is 50 %. `tests/dsp/ReverbDspTests.cpp` pins all five points
        of it; 11 section 6's null at 50 % with the faders off follows from
        dry being unity there. */
    static float dryGainFor (float mix) noexcept { return std::min (1.0f, 2.0f * (1.0f - std::clamp (mix, 0.0f, 1.0f))); }
    static float wetGainFor (float mix) noexcept { return std::min (1.0f, 2.0f * std::clamp (mix, 0.0f, 1.0f)); }

    const Params& getParams() const noexcept { return params; }

    /** The window the EQ page's spectrum is drawn from.

        **It is the signal the Reverb EQ acts on**, which 10 section 2 puts
        pre both generators: the EQ shapes what the room is given rather than
        what it returns, so this is the point whose spectrum a user is reading
        the EQ curve against. Wiring it here rather than on the output is not a
        placeholder decision -- it is where the tap belongs once there is a
        reverb, and putting it on the output would have to be undone.

        **Until the Reverb EQ is in the path (M3) this shows the dry input,
        and that is honest.** The EQ sits pre both generators, so the input
        and the point the EQ acts on are still the same samples; the ER now
        playing on the output is downstream of it. A reader who finds the
        spectrum "not reacting to the EQ knobs" has found M3's absence, not a
        broken analyser. **Do not move the tap to fix it.**
        See modules/reverb/AGENTS.md, "The analyser is real and the signal
        under it is not yet".

        Reading it costs the audio thread nothing until a panel enables it, and
        a closed editor is the normal state of a plugin in a finished session
        -- `core/dsp/AnalyserTap.h` is emphatic that this path can change
        neither the sound nor the latency, and why. */
    AnalyserTap& eqAnalyser() noexcept { return eqTap; }

    /** M2: the early reflections play; the tail is silent.

        The analyser window is written from the input, which is still the
        point the Reverb EQ will act on -- the EQ lands in M3 ahead of both
        generators, and until then the input and that point are the same
        samples. The ER generator is fed the mid of the input (a mono bus
        feeds it directly), returns the ER bus at table level, and the two
        faders, the MIX law and OUTPUT are applied here with 20 ms
        one-pole smoothing on every gain. `verbLevel` is smoothed and applied
        to a bus that is all zeros, so its fader already behaves. */
    void process (float* const* channelData, int numChannels, int numSamples)
    {
        eqTap.write (channelData, numChannels, numSamples);

        if (numChannels < 1 || numSamples < 1 || feed.empty())
            return;

        float* const left  = channelData[0];
        float* const right = numChannels > 1 ? channelData[1] : channelData[0];

        for (int start = 0; start < numSamples; start += blockSize)
        {
            const auto n = std::min (blockSize, numSamples - start);

            if (numChannels > 1)
                for (int i = 0; i < n; ++i)
                    feed[(size_t) i] = 0.5f * (left[start + i] + right[start + i]);
            else
                for (int i = 0; i < n; ++i)
                    feed[(size_t) i] = left[start + i];

            er.process (feed.data(), erL.data(), erR.data(), n);

            for (int i = 0; i < n; ++i)
            {
                gEr  += (tEr  - gEr)  * smoothCoef;
                gDry += (tDry - gDry) * smoothCoef;
                gWet += (tWet - gWet) * smoothCoef;
                gOut += (tOut - gOut) * smoothCoef;

                const auto wetL = erL[(size_t) i] * gEr;
                const auto wetR = erR[(size_t) i] * gEr;

                if (numChannels > 1)
                {
                    left[start + i]  = (left[start + i]  * gDry + wetL * gWet) * gOut;
                    right[start + i] = (right[start + i] * gDry + wetR * gWet) * gOut;
                }
                else
                {
                    // A mono bus hears the mono sum, which the gamma >= 0 rule
                    // keeps within 3 dB of either channel.
                    left[start + i] = (left[start + i] * gDry + 0.5f * (wetL + wetR) * gWet) * gOut;
                }
            }
        }
    }

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
        pulling. The ceiling below is the module's, and the rack clamps its sum
        at the same `kMaxTailSeconds` (`RackProcessor::totalTail`), because
        eight maxed reverbs in one chain would otherwise report four minutes. */
    static float tailSecondsFor (const Params& p) noexcept
    {
        // t_ER,max is the span of the ER mode in use. Taps plays the type's
        // table, whose last tap scales with SIZE; Energy lays its pulses over
        // its own window, up to 500 ms whatever the SIZE. Adding the Taps
        // span in both modes, as this did until the 2026-09-30 review, told a
        // host 231 ms at DECAY 0.1 s while an Energy cluster at ER SPREAD 200
        // played for half a second.
        const auto erMs = p.erMode == ErMode::energy ? ErGenerator::energyWindowMs (p.erSpreadMs)
                                                     : erSpanMsAt ((int) p.type, p.sizeM);

        const auto longest = std::max (1.0f, std::max (p.dampLo, p.dampHi));
        const auto seconds = p.preDelayMs * 0.001f
                           + p.decaySeconds * longest
                           + erMs * 0.001f
                           + 0.05f;

        return std::min (seconds, kMaxTailSeconds);
    }

private:
    Params params;

    /** What the generator is told, from the parameter set: the fields that
        decide the table, plus DENSITY and ER HI-CUT as smoothed targets. */
    void pushErConfig()
    {
        ErConfig c;
        c.type      = (int) params.type;
        c.sizeM     = params.sizeM;
        c.mode      = (int) params.erMode;
        c.variation = params.erVariation;
        c.spreadMs  = params.erSpreadMs;
        c.shape     = params.erShape;
        er.setConfig (c);
        er.setDensity (params.erDensity);
        er.setHiCut (params.erHiCutHz);

        tEr  = faderGain (params.erLevelDb);
        tDry = dryGainFor (params.mix);
        tWet = wetGainFor (params.mix);
        tOut = std::pow (10.0f, params.outputDb * 0.05f);
    }

    /** Gains jump to their targets: after prepare() and reset() there is no
        history to smooth from. */
    void snapGains() noexcept
    {
        gEr = tEr; gDry = tDry; gWet = tWet; gOut = tOut;
    }

    ErGenerator er;
    std::vector<float> feed, erL, erR;

    float smoothCoef = 0.0f;
    float tEr = 0.0f, tDry = 1.0f, tWet = 1.0f, tOut = 1.0f;
    float gEr = 0.0f, gDry = 1.0f, gWet = 1.0f, gOut = 1.0f;

    /** The grid the three EQ nodes are designed on, at the running rate.
        Unused by the placeholder; rebuilt in `prepare` so the engine has it.
        `kEqDesignRate` until a host says otherwise, which is BMO DEQ's
        `kDesignRate` and its argument. */
    dsp::DesignGrid grid = dsp::DesignGrid::make (kEqDesignRate);

    /** Held by value and not behind a `unique_ptr<Shared>` as BMO DEQ's is.
        DEQ needs the indirection because `deq::DspCore` is built and handed
        back **by value** across its tests and tools and an atomic is neither
        copyable nor movable; nothing copies this one -- every caller holds a
        `ReverbDsp` and reaches the core through it -- so the indirection would
        buy nothing. If a copy is ever wanted, this is the member that will
        refuse to compile, which is the right way to be told. */
    AnalyserTap eqTap;

    double sampleRate = 0.0;
    int blockSize = 0;
    int channels = 0;
};

} // namespace bmo::reverb
