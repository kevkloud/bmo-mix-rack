#pragma once

// For `bmo::kMaxTailSeconds`, which is the suite's tail ceiling and is read
// here rather than copied: the rack clamps its summed total at the same
// figure, and two 30.0s written down in two folders is how they come to differ.
#include "core/dsp/ModuleDsp.h"
#include "modules/reverb/dsp/EqNodes.h"
#include "modules/reverb/dsp/ErEngine.h"
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

/** How the early cluster is generated. See `kErModeNames` in params.h for what
    Blend is: defined, reachable, and **not yet heard**. */
enum class ErMode { taps = 0, energy, blend };

// ErEngine names the three modes as ints because it sits below this header;
// these are what keep the two lists one list.
static_assert ((int) ErMode::taps   == ErEngine::kModeTaps,   "ER Mode ordinals");
static_assert ((int) ErMode::energy == ErEngine::kModeEnergy, "ER Mode ordinals");
static_assert ((int) ErMode::blend  == ErEngine::kModeBlend,  "ER Mode ordinals");

//==============================================================================
/** **The early reflections are real; the tail is silent until M3.**

    Milestone M2 (11 section 7) is here. `ErEngine` plays the ER table for the
    selected type -- image-source taps under the Size law and its crossfade,
    four order-banded low-passes, the density bridge and its feed-forward
    diffuser, VARIATION, ER HI-CUT and the three ER modes -- and this class
    puts it on the wet bus behind the ER fader, mixes it against a dry signal
    that is never delayed, and applies OUTPUT. **There is no late network
    yet**: REVERB, DECAY, the damping multipliers, SOURCE, WIDTH, PRE-DELAY,
    the modulation pair, IN HI-CUT and the Reverb EQ reach `Params` and go no
    further, so the wet bus is the ER and nothing else. Latency is zero, which
    is the *shipped* figure and not a stand-in.

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
        int   erVariation   = 2;                                  ///< 0..6; 6 is "mono null", the ER on the side only

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
    // `kRampWidth`, `kCrossfadeMs` and `kSmoothingMs` are live since M2: the
    // ER engine is built on them. `kNumLines` and `kBypassFadeMs` wait for the
    // late network (M3).

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

    /** Starts at the default `Params`, so a core that is prepared and run
        before anyone calls `setParams` plays a Room and not silence. */
    DspCore() { setParams (params); }

    void prepare (double newSampleRate, int maxBlockSize, int numChannels)
    {
        // Everything the engine will ever need is allocated **here and only
        // here**, sized for the largest type at this rate, and `process()`
        // allocates nothing ever. The ER half is the ER delay line and the
        // diffuser's twenty-four short lines; the late network's lines land
        // here in M3.
        sampleRate = newSampleRate;
        blockSize  = maxBlockSize;
        channels   = numChannels;

        er.prepare (newSampleRate, kRampWidth, kCrossfadeMs, kSmoothingMs);

        // The ER is run a chunk at a time through these, so a host that sends
        // more than it promised in `maxBlockSize` is chunked rather than
        // allocated for.
        const auto chunk = (size_t) std::clamp (maxBlockSize, 64, 4096);
        scratchIn.assign (chunk, 0.0f);
        scratchL.assign (chunk, 0.0f);
        scratchR.assign (chunk, 0.0f);

        const auto rate = newSampleRate > 0.0 ? newSampleRate : 48000.0;
        const auto tau = (double) kSmoothingMs * 0.001 / std::log (100.0);
        levelCoeff = (float) (1.0 - std::exp (-1.0 / (tau * rate)));

        reset();

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

    /** Clears every line and filter, and makes the next block start with the
        smoothers **at** their targets rather than gliding to them -- so after
        `reset()` zeros in are exactly zeros out, and an impulse sent right
        after a `prepare` measures the setting and not the approach to it. */
    void reset()
    {
        er.reset();
        levelsPrimed = false;
    }

    void setParams (const Params& p)
    {
        params = p;
        er.setSettings (erSettingsFor (p));

        // -40 is **off**, not -40 dB (10 section 2, 11 section 6): the fader's
        // bottom detent is silence, and a level law that stopped at 1 % would
        // leave the ER audible under a mix that was told it was gone.
        erGainTarget  = p.erLevelDb <= kLevelOffDb ? 0.0f : std::pow (10.0f, p.erLevelDb / 20.0f);
        mixTarget     = std::clamp (p.mix, 0.0f, 1.0f);
        outGainTarget = std::pow (10.0f, p.outputDb / 20.0f);
    }

    /** The ER engine's view of `Params`. */
    static ErEngine::Settings erSettingsFor (const Params& p) noexcept
    {
        ErEngine::Settings s;
        s.type      = (int) p.type;
        s.mode      = (int) p.erMode;
        s.sizeM     = p.sizeM;
        s.density   = std::clamp (p.erDensity, 0.0f, 1.0f);
        s.shape     = p.erShape;
        s.spreadMs  = p.erSpreadMs;
        s.hiCutHz   = p.erHiCutHz;
        s.variation = p.erVariation;
        return s;
    }

    /** Where the two level faders are off. */
    static constexpr float kLevelOffDb = -40.0f;

    ErEngine& erEngine() noexcept { return er; }

    const Params& getParams() const noexcept { return params; }

    /** The window the EQ page's spectrum is drawn from.

        **It is the signal the Reverb EQ acts on**, which 10 section 2 puts
        pre both generators: the EQ shapes what the room is given rather than
        what it returns, so this is the point whose spectrum a user is reading
        the EQ curve against. Wiring it here rather than on the output is not a
        placeholder decision -- it is where the tap belongs once there is a
        reverb, and putting it on the output would have to be undone.

        **Until M3 builds the input stage this shows the dry input, and that
        is honest.** The early reflections exist since M2, but the Reverb EQ
        and the input conditioning ahead of it do not, so the point the EQ
        acts on is still the module's input -- which is what is written here,
        before the ER engine sees a sample. A reader who finds the spectrum
        "not reacting to the EQ knobs" has found the missing EQ, not a broken
        analyser. **Do not move the tap to fix it.**
        See modules/reverb/AGENTS.md, "The analyser is real and the signal
        under it is not yet".

        Reading it costs the audio thread nothing until a panel enables it, and
        a closed editor is the normal state of a plugin in a finished session
        -- `core/dsp/AnalyserTap.h` is emphatic that this path can change
        neither the sound nor the latency, and why. */
    AnalyserTap& eqAnalyser() noexcept { return eqTap; }

    /** Dry, plus the early reflections behind the ER fader, then OUTPUT.

        The analyser window is written first, off the input, exactly where it
        always was (see `eqAnalyser`). Then the ER engine is given the mono
        sum, and each output sample is

            (dry * (1 - mix) + erGain * ER * mix) * outGain

        with `erGain`, `mix` and `outGain` each smoothed per sample. **The dry
        sample is the input sample** -- never delayed, never summed against a
        delayed copy of itself (10 section 2) -- so MIX 0 is the input to the
        bit and the phasing trap has nothing to catch. The tail joins the wet
        term at M3; until then it is silent at every REVERB setting.

        One channel is a mono bus: the ER's two channels are mono-summed onto
        it, which is what a listener summing the stereo module would hear. */
    void process (float* const* channelData, int numChannels, int numSamples)
    {
        eqTap.write (channelData, numChannels, numSamples);

        if (channelData == nullptr || numChannels <= 0 || numSamples <= 0 || scratchIn.empty())
            return;

        if (! levelsPrimed)
        {
            erGain = erGainTarget;
            mix = mixTarget;
            outGain = outGainTarget;
            levelsPrimed = true;
        }

        float* left  = channelData[0];
        float* right = numChannels > 1 ? channelData[1] : nullptr;

        const auto chunk = (int) scratchIn.size();

        for (int done = 0; done < numSamples; done += chunk)
        {
            const auto n = std::min (chunk, numSamples - done);
            float* l = left + done;
            float* r = right != nullptr ? right + done : nullptr;

            for (int i = 0; i < n; ++i)
                scratchIn[(size_t) i] = r != nullptr ? 0.5f * (l[i] + r[i]) : l[i];

            er.process (scratchIn.data(), scratchL.data(), scratchR.data(), n);

            for (int i = 0; i < n; ++i)
            {
                erGain  = smooth (erGain, erGainTarget);
                mix     = smooth (mix, mixTarget);
                outGain = smooth (outGain, outGainTarget);

                // **The MIX law is provisional.** A plain linear dry*(1-m) +
                // wet*m, pending the owner's choice between this and an
                // equal-power or a dry-held law (11 section 7, "owner confirm,
                // still open: the MIX law and its default"). Nothing but
                // "MIX 0 is exactly dry, MIX 1 has no dry" is asserted of it,
                // so changing it moves no test.
                const auto dryGain = 1.0f - mix;
                const auto wetGain = erGain * mix;

                if (r != nullptr)
                {
                    l[i] = (l[i] * dryGain + scratchL[(size_t) i] * wetGain) * outGain;
                    r[i] = (r[i] * dryGain + scratchR[(size_t) i] * wetGain) * outGain;
                }
                else
                {
                    const auto er1 = 0.5f * (scratchL[(size_t) i] + scratchR[(size_t) i]);
                    l[i] = (l[i] * dryGain + er1 * wetGain) * outGain;
                }
            }
        }

        // A third channel and beyond is left as it came: the layouts the
        // module accepts are mono and stereo, and neither reaches here.
    }

    /** Zero, at every setting, always. Not computed from the values, because
        nothing in the schema can move it. */
    static constexpr int latencySamples() noexcept { return 0; }

    /** The tail the host should be told about, in seconds:

            preDelay + T_mid * max(1, r_lo, r_hi) + t_ER,max + 0.05 s

        clamped to `kMaxTailSeconds` (10 section 5). t_ER,max is the selected
        type's own span at this SIZE, `erSpanMsAt (table, size)` -- the same
        Size law and window clamp the ER engine plays by -- since 2026-09-24;
        it was the placeholder table's last tap scaled without a clamp, which
        was one number for every type. From parameter values
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
        const auto longest = std::max (1.0f, std::max (p.dampLo, p.dampHi));
        const auto seconds = p.preDelayMs * 0.001f
                           + p.decaySeconds * longest
                           + erSpanMsAt (erTableFor ((int) p.type), p.sizeM) * 0.001f
                           + 0.05f;

        return std::min (seconds, kMaxTailSeconds);
    }

private:
    Params params;

    /** The grid the three EQ nodes are designed on, at the running rate.
        Unused until the EQ is built (M3); rebuilt in `prepare` so it has it.
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

    ErEngine er;

    /** The ER engine's input and output, one chunk at a time. Sized in
        `prepare`. */
    std::vector<float> scratchIn, scratchL, scratchR;

    // The three level smoothers: one pole, `kSmoothingMs` to 99 %, per
    // sample. Snapped to exactly the target once within a millionth, so an
    // ER fader at -40 settles on exactly zero and not on a denormal.
    float levelCoeff = 1.0f;
    float erGain = 0.0f, erGainTarget = 0.0f;
    float mix = 1.0f, mixTarget = 1.0f;
    float outGain = 1.0f, outGainTarget = 1.0f;
    bool  levelsPrimed = false;

    float smooth (float value, float targetValue) const noexcept
    {
        if (value == targetValue)
            return value;

        value += levelCoeff * (targetValue - value);
        return std::abs (targetValue - value) <= 1.0e-6f ? targetValue : value;
    }
};

} // namespace bmo::reverb
