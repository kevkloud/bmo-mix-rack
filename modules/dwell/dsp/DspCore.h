#pragma once

#include "modules/dwell/dsp/DelayEngine.h"
#include "modules/dwell/params.h"
#include "modules/tune/dsp/Denormals.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace bmo::dwell
{

//==============================================================================
/** docs/delay/10 §3's FEEDBACK law, `g = (1.05 . fb^1.6) / P_c`.

    A free function because the **law is not the engine's** (10 §11.1): the
    main delay maps FEEDBACK through this one, the lane maps `lane_gain`
    through §11.2's bipolar law, and an engine that knew both would be an
    engine that knew which instance it was. `P_c` comes from the engine the
    gain is for, which computes it by sweep -- it is never a constant here.

    The exponent puts resolution in the 2-8-repeat region; the 1.05 puts the
    loop's peak magnitude at 1.000 at fb = 97.0 % and 1.05 at full travel, on
    every character, which is what lets the panel carry one self-oscillation
    tick rather than one per character. */
inline float feedbackGainFor (float feedbackPercent, double loopPeak) noexcept
{
    const auto fb = std::clamp ((double) feedbackPercent * 0.01, 0.0, 1.0);
    return (float) (1.05 * std::pow (fb, 1.6) / std::max (loopPeak, 1.0e-6));
}

//==============================================================================
/** BMO Dwell's audio core.

    **This is stage 2b: the three characters.** The ring, the fractional read,
    §3's feedback law with its computed `P_c`, §9's MIX law and both engines
    came from 2a; 2b adds the characters themselves -- the wired cuts, tape,
    bucket-brigade with its compander, the DRIVE shaper and §5's modulation --
    all of them inside `DelayEngine` and all of them parameters rather than
    branches on which engine is running. The ducker, the stereo matrix, the FX
    stage and the lane's gates are 2c, per docs/delay/10-dsp-spec.md.

    **One delay engine, instantiated twice** -- the main delay and the lane --
    rather than one bespoke dual engine with the lane written into it (10
    §11.1; DECIDED, Frosty 2026-09-23). `DelayEngine` knows nothing about
    which instance it is: it is handed a time and a character, it publishes
    the `P_c` its own filters and interpolator come to, and the **laws** that
    turn a knob into a loop gain stay out here, where the difference between
    the main delay and the lane lives. Written the other way the lane would be
    a set of branches threaded through the main loop, and pulling it apart
    later -- if Frosty ever wants the throw lane as its own product, or the
    main delay without one -- would be a rewrite rather than a deletion.

    **The lane is instantiated, allocated, summed and fed silence.** SEND,
    HOLD, CHOP and §11.2's bipolar tail law are 2c; until they land the lane's
    input is a block of zeros and its loop gain is zero, so it contributes
    exact zeros through `lane_level`. What it proves now is that the summing
    path and the second ring exist and cost what §10 says they cost.

    The schema is permanent from this release, so the wiring from spec index
    to named value stays pinned by tests: `DwellDspTests` checks the mapping
    value by value.

    **Latency is 0 at every setting, now and after stage 2.** There is no
    oversampling (docs/delay/10 §0 drops it), no lookahead and therefore no dry
    compensation ring. The wet delay time is *not* latency and is never
    reported as such -- the host is not being asked to shift anything, the same
    way BMO Dimension's detune voices are not reported. If the half-band
    fallback in 10 §4 is ever added, its group delay is a whole number of
    samples and comes off D, so this line does not change either.
*/
class DspCore
{
public:
    /** Every parameter in the real units the panel and the host show, in the
        schema's own order. `DwellDsp::setParams` fills it.

        **Two engines, one voicing** from 2026-09-22: the lane reads
        `characterChoice`, `stereoChoice`, both cuts, both modulation values
        and `drivePct` from the fields below rather than from lane copies of
        them, because there are no lane copies any more. `duckDb` is the one
        main-delay value it does *not* read -- the ducker pushes the main wet
        out of the way of the dry, and the lane's job is to be heard.

        Each field's initialiser is its spec default, so a core that has never
        been handed a parameter array is still the module at its defaults. */
    struct Params
    {
        float timeMs        = 375.0f;
        bool  sync          = false;    ///< read only once 12's tempo plumbing lands
        int   noteChoice    = kDefaultNote;
        float feedbackPct   = 35.0f;
        int   characterChoice = 0;
        int   stereoChoice  = 0;
        float lowCutHz      = 20.0f;
        float highCutHz     = 20000.0f;
        float modRateHz     = 0.6f;
        float modDepthPct   = 0.0f;
        float drivePct      = 0.0f;
        float duckDb        = 0.0f;
        float mixPct        = 35.0f;

        /** The lane's gates and its tail. `laneGain` is bipolar: below 0 the
            lane decays, at 0 it holds at exact unity, above it builds. */
        bool  sendHeld      = false;
        float laneGain      = -40.0f;
        bool  hold          = false;
        bool  chop          = false;

        bool  fx            = false;
        int   fxTypeChoice  = 0;
        float fxAmountPct   = 35.0f;

        /** What the lane declares for itself: how loud it is against the main
            delay's wet, how long its own repeat is, and its own FX stage.
            Everything else it needs is above -- one set of voicing values, two
            engines reading them. */
        float laneLevelDb       = 0.0f;
        float laneTimeMs        = 250.0f;
        bool  laneFx            = false;
        int   laneFxTypeChoice  = 0;
        float laneFxAmountPct   = 35.0f;

        /** Whether the lane's FX trio follows the main delay's. Carried here
            whatever it says -- a value that stops arriving is a value the lane
            could not go back to -- and it is the later stage that acts on it.
            Defaults on: a fresh instance is one delay with one set of
            controls. */
        bool  fxLink            = true;
    };

    /** **Everything either engine will ever need is allocated here**, from
        `kMaxTimeMs` and never from a parameter (10 §10): two rings of the
        fixed maximum -- 2.0 MB a channel each at 192 kHz -- the sweep grids,
        and the three scratch blocks the wet sum is built in. `process` then
        allocates nothing, which `DwellDspTests` asserts by counting. */
    void prepare (double newSampleRate, int newMaxBlockSize, int newNumChannels)
    {
        sampleRate   = newSampleRate > 0.0 ? newSampleRate : 48000.0;
        maxBlockSize = std::max (newMaxBlockSize, 1);
        numChannels  = std::max (newNumChannels, 1);

        const auto engineChannels = std::min (numChannels, (int) DelayEngine::kMaxChannels);

        mainEngine.prepare (sampleRate, maxBlockSize, engineChannels, (double) kMaxTimeMs);
        laneEngine.prepare (sampleRate, maxBlockSize, engineChannels, (double) kMaxTimeMs);

        for (int ch = 0; ch < (int) DelayEngine::kMaxChannels; ++ch)
        {
            wetMain[(size_t) ch].assign ((size_t) maxBlockSize, 0.0f);
            wetLane[(size_t) ch].assign ((size_t) maxBlockSize, 0.0f);
            laneFeed[(size_t) ch].assign ((size_t) maxBlockSize, 0.0f);
        }

        // 20 ms on the wet and dry gains (10 §9). The dry is only ever
        // smoothed above the hinge; below it, it is not a gain at all.
        wetGain.prepare (sampleRate, 20.0);
        dryGain.prepare (sampleRate, 20.0);
        laneLevel.prepare (sampleRate, 20.0);
        gainsPrimed = false;
        parametersSeen = false;

        applyParams (true);
        reset();
    }

    void reset() noexcept
    {
        mainEngine.reset();
        laneEngine.reset();

        for (auto& b : wetMain)  std::fill (b.begin(), b.end(), 0.0f);
        for (auto& b : wetLane)  std::fill (b.begin(), b.end(), 0.0f);
        for (auto& b : laneFeed) std::fill (b.begin(), b.end(), 0.0f);
    }

    /** **The first parameter set after `prepare` snaps; every one after it
        moves under §2's law.**

        This is not a convenience. §2 rate-limits tape's and bucket-brigade's
        glide to 0.25 samples/sample, which is seconds for a large jump -- so a
        session recalled at TIME 1500 ms on tape would spend ten seconds
        sliding up from the default 375 ms it was never set to. `prepare` is
        handed the defaults, so the host's opening push is the one that says
        what the instance actually is, and an instance arriving at the setting
        it was saved at is the same rule `setFeedbackGain` and DRIVE already
        follow. */
    void setParams (const Params& p) noexcept
    {
        params = p;
        applyParams (! parametersSeen);
        parametersSeen = true;
    }

    const Params& getParams() const noexcept { return params; }

    /** One block, in place: the dry arrives in `channels` and the mixed output
        leaves in it.

        Both engines read the block before anything writes over it, so the dry
        is still intact when §9's law runs. Chunked at `maxBlockSize` so a host
        handing over more than it promised is still served from the scratch
        that `prepare` allocated rather than from a fresh buffer. */
    void process (float* const* channels, int numChannels_, int numSamples) noexcept
    {
        if (channels == nullptr || numChannels_ <= 0 || numSamples <= 0)
            return;

        const bmo::tune::ScopedNoDenormals noDenormals;

        for (int offset = 0; offset < numSamples; )
        {
            const auto count = std::min (numSamples - offset, maxBlockSize);
            processChunk (channels, numChannels_, offset, count);
            offset += count;
        }
    }

    double getSampleRate() const noexcept { return sampleRate; }
    int getMaxBlockSize() const noexcept  { return maxBlockSize; }
    int getNumChannels() const noexcept   { return numChannels; }

    /** The two engines, for the tests that have to see one of them alone --
        `P_c` on clean, and later `11` §4e's claim that the main's own tap is
        bit-identical with and without a send. */
    const DelayEngine& getMainEngine() const noexcept { return mainEngine; }
    const DelayEngine& getLaneEngine() const noexcept { return laneEngine; }

    /** The longest delay a ring is sized for, in samples at the prepared
        rate. Each engine allocates the next power of two at or above this. */
    int maxDelaySamples() const noexcept
    {
        return (int) std::ceil ((double) kMaxTimeMs * 0.001 * sampleRate);
    }

private:
    //==========================================================================
    /** Both engines take the same TIME law and the same character -- one
        voicing, two engines (params.h, 2026-09-22) -- and differ in the law
        that turns a knob into their loop gain.

        The order matters: TIME and CHARACTER go in first, because that is what
        re-sweeps `P_c`, and only then is the gain built from the figure the
        sweep landed on. */
    void applyParams (bool snapNow) noexcept
    {
        // **One voicing, two engines** (10 §12, DECIDED 2026-09-23). Every row
        // below is filled from the same field for both engines, and there are
        // no lane copies of them to keep in step -- which is most of the point
        // of an engine that cannot ask which one it is. What differs between
        // the two calls is TIME, and in 2c the lane's own FX trio. Nothing
        // else, and nothing about the sound.
        const auto voiceOf = [this] (float engineTimeMs)
        {
            DelayEngine::Params p;
            p.timeMs      = engineTimeMs;
            p.character   = params.characterChoice;
            p.lowCutHz    = params.lowCutHz;
            p.highCutHz   = params.highCutHz;
            p.modRateHz   = params.modRateHz;
            p.modDepthPct = params.modDepthPct;
            p.drivePct    = params.drivePct;
            return p;
        };

        mainEngine.setParams (voiceOf (params.timeMs), snapNow);
        mainEngine.setFeedbackGain (feedbackGainFor (params.feedbackPct,
                                                     mainEngine.referenceLoopPeak()),
                                    snapNow);

        laneEngine.setParams (voiceOf (params.laneTimeMs), snapNow);

        // 10 §11.2's bipolar law -- throw, freeze and build off one knob --
        // arrives with the gates in 2c, together with the `g_max` figure it
        // needs, which is still CALIBRATE (10 §12). Until then the lane runs
        // with no tail at all rather than with half a law in place.
        laneEngine.setFeedbackGain (0.0f, snapNow);

        const auto m = std::clamp ((double) params.mixPct * 0.01, 0.0, 1.0);
        dryIsBitExact = m <= 0.5;

        const auto wetTarget = dryIsBitExact ? std::sin (kPiD * m) : 1.0;
        const auto dryTarget = dryIsBitExact ? 1.0 : std::cos (kPiD * (m - 0.5));
        const auto laneTarget = std::pow (10.0, (double) params.laneLevelDb / 20.0);

        if (! gainsPrimed || snapNow)
        {
            wetGain.snap ((float) wetTarget);
            dryGain.snap ((float) dryTarget);
            laneLevel.snap ((float) laneTarget);
            gainsPrimed = true;
            return;
        }

        wetGain.setTarget ((float) wetTarget);
        laneLevel.setTarget ((float) laneTarget);

        // Below the hinge the dry is not multiplied at all, so there is
        // nothing to smooth and nothing that can zipper; the smoother is
        // snapped so that crossing back up starts from the right place. At the
        // hinge itself cos(0) is 1, so the two branches meet without a step.
        if (dryIsBitExact)
            dryGain.snap (1.0f);
        else
            dryGain.setTarget ((float) dryTarget);
    }

    void processChunk (float* const* channels, int numChannels_, int offset, int count) noexcept
    {
        const auto nch = std::min (numChannels_, (int) DelayEngine::kMaxChannels);

        const float* dry[DelayEngine::kMaxChannels] {};
        const float* silence[DelayEngine::kMaxChannels] {};
        float* main[DelayEngine::kMaxChannels] {};
        float* lane[DelayEngine::kMaxChannels] {};

        for (int ch = 0; ch < nch; ++ch)
        {
            dry[ch]     = channels[ch] + offset;
            silence[ch] = laneFeed[(size_t) ch].data();
            main[ch]    = wetMain[(size_t) ch].data();
            lane[ch]    = wetLane[(size_t) ch].data();
        }

        mainEngine.process (dry, main, nch, count);

        // 10 §11.1: the lane taps the **dry input**, gated by SEND -- which is
        // 2c. Until it lands the gate is closed, so what the lane is fed is a
        // block of zeros it never allocated.
        laneEngine.process (silence, lane, nch, count);

        for (int i = 0; i < count; ++i)
        {
            const auto w  = wetGain.tick();
            const auto d  = dryGain.tick();
            const auto ll = laneLevel.tick();

            for (int ch = 0; ch < nch; ++ch)
            {
                // §11.1 item 3: the lane sums into the wet bus after the
                // main's loop tap. §6's GR goes between the two and is 2b.
                const auto wet = main[ch][i] + ll * lane[ch][i];
                auto* slot = channels[ch] + offset + i;

                // §9: below 50 % the dry is multiplied by **nothing**, not by
                // 1.0f. That is what makes the null bit-exact rather than
                // merely -120 dB, and it is why this is a branch and not a
                // gain of 1.
                const auto out = dryIsBitExact ? (*slot + w * wet)
                                               : (d * *slot + wet);

                *slot = std::isfinite (out) ? out : 0.0f;
            }
        }
    }

    Params params;

    DelayEngine mainEngine, laneEngine;

    std::array<std::vector<float>, DelayEngine::kMaxChannels> wetMain, wetLane, laneFeed;

    Smoother wetGain, dryGain, laneLevel;
    bool dryIsBitExact = true, gainsPrimed = false, parametersSeen = false;

    double sampleRate  = 48000.0;
    int maxBlockSize   = 512;
    int numChannels    = 2;
};

} // namespace bmo::dwell
