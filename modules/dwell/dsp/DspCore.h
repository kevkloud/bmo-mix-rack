#pragma once

#include "modules/dwell/dsp/DelayEngine.h"
#include "modules/dwell/dsp/GainLaws.h"
#include "modules/dwell/params.h"
#include "modules/tune/dsp/Denormals.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace bmo::dwell
{

//==============================================================================
/** A half-cosine gate: closed at 0, open at 1, raised cosine across the
    travel, with its own opening and closing times.

    All three of the lane's gates are one of these (10 §11.4) -- SEND at
    5 / 15 ms, CHOP and the HOLD-off mute at 1 / 1 ms -- because they are the
    same object used at three points in the signal path rather than three
    shapes. The **phase** is linear and the cosine is taken from it, so "fully
    open within 5 ms" is exact by construction rather than asymptotic: a
    smoother would only ever approach its rail, and `11` §4e8 measures the
    edge against a stopwatch.

    **The gate decision quantises to the block boundary and there is no
    lookahead** (10 §11.4). That falls out of where it is set rather than being
    enforced here: `setOpen` is called from `setParams`, which a host calls
    once per block. */
class CosineGate
{
public:
    void prepare (double sampleRate, double openMs, double closeMs) noexcept
    {
        openStep  = 1.0 / std::max (1.0, std::round (sampleRate * openMs * 0.001));
        closeStep = 1.0 / std::max (1.0, std::round (sampleRate * closeMs * 0.001));
    }

    void setOpen (bool shouldOpen) noexcept { wantOpen = shouldOpen; }

    /** Takes the state without a ramp -- what `prepare` wants, and what the
        lane wants when it is empty and there is nothing for an edge to click
        on. */
    void snap (bool shouldOpen) noexcept
    {
        wantOpen = shouldOpen;
        phase = shouldOpen ? 1.0 : 0.0;
    }

    bool isFullyClosed() const noexcept { return ! wantOpen && phase <= 0.0; }

    double tick() noexcept
    {
        if (wantOpen)
        {
            if (phase < 1.0)
                phase = std::min (1.0, phase + openStep);
        }
        else if (phase > 0.0)
        {
            phase = std::max (0.0, phase - closeStep);
        }

        // The rails are exact, so a closed gate emits exact zeros and an open
        // one is bit-transparent rather than multiplying by 0.99999997.
        if (phase <= 0.0) return 0.0;
        if (phase >= 1.0) return 1.0;

        return 0.5 - 0.5 * std::cos (kPiD * phase);
    }

private:
    double phase = 0.0, openStep = 1.0, closeStep = 1.0;
    bool wantOpen = false;
};

//==============================================================================
/** 10 §11.4's gate times. SEND is asymmetric on purpose -- it opens fast
    enough to catch the front of a word and closes slowly enough not to chop
    its tail off -- and both figures are CALIBRATE, being the ones the retired
    THROW control used. */
inline constexpr double kSendOpenMs = 5.0;
inline constexpr double kSendCloseMs = 15.0;

/** CHOP's edge, and the mute that precedes a HOLD-off clear, 10 §11.4.

    **The fade stays at 1 ms and the acceptance is band-limited instead**
    (DECIDED, Frosty 2026-09-22): a raised cosine is C1, so its splatter falls
    as 1/f^3 and spreads over roughly 1 kHz, and on a sustained bright tone the
    first sidelobe sits 30-40 dB below the gated signal rather than 60. A
    broadband -60 dBFS assertion and a 1 ms gate cannot both stand, and the
    gate wins: CHOP is a rhythmic gate on a delay tail, tightness is the
    feature, and the ~3 ms a broadband figure needs would cost sixteenths above
    ~160 BPM. `11` §4e3 therefore judges it on content band-limited to 5 kHz. */
inline constexpr double kChopFadeMs = 1.0;

//==============================================================================
/** 10 §6's ducker. The ballistics, threshold and width are the spec's, all
    CALIBRATE; what is settled **here** is the key filter's corner, because §6
    fixes it at build time with no parameter reserved for it.

    **120 Hz** (CALIBRATE). The detector is keyed structurally rather than by
    routing -- the module is an insert, so the dry input *is* the track being
    sent to the delay, and there is no sidechain bus to key it from (§6). That
    makes the corner's job narrow and answerable: stop the track's own low end
    from deciding how hard the repeats ducked. Without a key filter a kick or a
    bass note holds the follower at the top of its range through every gap,
    because peak level at those frequencies runs 10-15 dB above the mid-band
    content the echoes actually collide with, and DUCK stops being a control
    over the collision and becomes a tremolo at the tempo. 120 Hz sits above
    the fundamental of a kick and below the body of most voices and guitars, so
    what drives the follower is the material the delay is competing with. Lower
    -- 40-60 Hz -- leaves the kick in; higher -- 250 Hz and up -- starts to
    miss the body of the very sources ducking is for.

    It is one pole, not two: the point is a tilt away from the bottom octave,
    not a cut, and a first-order slope leaves a bass-heavy mix still able to
    duck rather than making the control deaf to it. */
inline constexpr double kDuckKeyHighPassHz = 120.0;
inline constexpr double kDuckAttackMs = 5.0;
inline constexpr double kDuckReleaseMs = 180.0;
inline constexpr double kDuckThresholdDb = -30.0;
inline constexpr double kDuckWidthDb = 20.0;

//==============================================================================
/** BMO Dwell's audio core.

    **This is stages 2c and 2d: the lane's gates, the ducker and stereo.** 2a
    left the ring, the fractional read, §3's feedback law with its computed
    `P_c`, §9's MIX law and both engines; 2b added the characters. 2c adds
    SEND, HOLD and CHOP, §11.2's bipolar tail and the lane's LEVEL; 2d adds
    §6's ducker and §8's three stereo modes. **2e adds §11a's in-loop FX
    stage**, which lands almost entirely in `DelayEngine` -- the stage is
    in-loop, so it is the engine's -- and leaves out here only `fx_link`, which
    chooses which trio the lane engine is handed.

    **Where each of those went is the design, not an implementation detail.**
    The stereo mode is one shared control governing both engines, so it is a
    parameter and lives in `DelayEngine` with the character. The three gates
    and the ducker are what tells the two instances apart, so they live **here**
    and nowhere else: SEND gates what this class feeds the lane, CHOP and the
    HOLD mute gate what this class reads back from it, and the ducker
    multiplies the main's tap on its way to the wet bus. None of them is
    reachable from inside an engine, which is what keeps 10 §11.1's
    requirement true as the module grows.

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

    **The main loop has no input gate, and that is the point.** §3's injection
    is `v[n] = x[n] + Σ g . C(y[n])` with the old `s` term **removed, not
    repurposed** (§11), so there is no mechanism by which a throw can disturb
    the main delay. "Unaffected" is then structural rather than careful, and
    `11` §4e1 proves it by asserting the main engine's own tap is
    bit-identical between a render where SEND is held throughout and one where
    it is never touched. Nothing below may reintroduce a gate on that
    injection under another name.

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
        bool  sync          = false;    ///< `DwellDsp` maps the divisions before they get here
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

        // 20 ms on the wet and dry gains and on DUCK (10 §9). The dry moves
        // only above the hinge or on its way across it; settled below it, it
        // is not a gain at all.
        wetGain.prepare (sampleRate, 20.0);
        dryGain.prepare (sampleRate, 20.0);
        laneLevel.prepare (sampleRate, 20.0);
        duckAmount.prepare (sampleRate, 20.0);

        // 10 §11.4's three gates. SEND gates the lane's input, CHOP its
        // output, and the mute is the 1 ms that precedes a HOLD-off clear --
        // the same fade CHOP uses, so dropping a full lane does not click.
        sendGate.prepare (sampleRate, kSendOpenMs, kSendCloseMs);
        chopGate.prepare (sampleRate, kChopFadeMs, kChopFadeMs);
        laneMute.prepare (sampleRate, kChopFadeMs, kChopFadeMs);

        duckFollower.prepare (sampleRate, kDuckAttackMs, kDuckReleaseMs);

        for (auto& f : duckKey)
            f.setCutoff (kDuckKeyHighPassHz, sampleRate);

        gainsPrimed = false;
        parametersSeen = false;
        holdOn = false;
        laneLive = false;

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

        for (auto& f : duckKey)
            f.reset();

        duckFollower.reset();

        sendGate.snap (false);
        chopGate.snap (! params.chop);
        laneMute.snap (true);
        laneLive = holdOn;

        // The rings are empty, so there is nothing for MIX, LANE LEVEL or
        // DUCK's glide to protect: the next parameter set takes them at once,
        // as the first after `prepare` does. Left primed, MIX 100 then 0 after
        // a reset glided in from silence (out[0] 0.00052 on a 0.5 input,
        // measured on AURORA 2026-10-01).
        gainsPrimed = false;
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
        follow.

        `landTime` is the narrower form of the same rule for TIME alone: both
        engines' reads go straight to their new times, with no glide and no
        crossfade, and nothing else snaps. `DwellDsp` asks for it once, for the
        first valid host tempo after a `prepare` or `reset`, when the ring is
        empty and a move would only be the knob's time sliding into the
        note's. */
    void setParams (const Params& p, bool landTime = false) noexcept
    {
        params = p;
        applyParams (! parametersSeen);
        parametersSeen = true;

        if (landTime)
        {
            mainEngine.landTime();
            laneEngine.landTime();
        }
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

    /** **The two wet taps, before they are summed** -- the main's before the
        ducker and the lane's before CHOP and LEVEL. Each holds the most
        recently processed chunk, so a caller reading them renders in blocks of
        at most `getMaxBlockSize()` and takes each block's tap as it goes.

        `11` §4e1 left this as an explicit **build decision**: the headline
        assertion -- that the main loop is bit-identical between a render with
        a send held throughout and one where it is never touched -- cannot be
        made from parameters alone, because `lane_level` bottoms at -24 dB
        rather than at -inf and no parameter silences a running lane. The
        alternative on offer was to ship the weaker `hold`-off form plus a code
        review that the `s` term is gone. **The tap is taken instead**: it
        costs two const accessors onto buffers that already exist, it adds
        nothing to `process()`, and it turns the claim the whole topology rests
        on into something a test can fail. §4e3's "the lane's contents are
        bit-identical to a chop-never render" needs the lane's half for the
        same reason, and §4g's "ducking never shortens the tail" is the main's
        half again in a different currency.

        They are **taps, not outputs**: nothing downstream of them -- MIX, the
        ducker, LEVEL -- is in what they carry. */
    const float* mainWetTap (int channel) const noexcept
    {
        return wetMain[(size_t) std::clamp (channel, 0, (int) DelayEngine::kMaxChannels - 1)].data();
    }

    const float* laneWetTap (int channel) const noexcept
    {
        return wetLane[(size_t) std::clamp (channel, 0, (int) DelayEngine::kMaxChannels - 1)].data();
    }

    /** Whether the lane is circulating at all: HOLD, plus the 1 ms mute that
        outlives it while the drop fades. With this false the lane emits exact
        zeros and costs one branch a block (10 §11.8). */
    bool laneIsLive() const noexcept { return laneLive; }

    /** LANE LEVEL's smoothed gain as it stands, for the test that holds it to
        landing exactly on its target. */
    float smoothedLaneLevel() const noexcept { return laneLevel.value(); }

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
        const auto voiceOf = [this] (float engineTimeMs, bool fxOn, int fxType, float fxAmountPct)
        {
            DelayEngine::Params p;
            p.timeMs      = engineTimeMs;
            p.character   = params.characterChoice;
            p.stereoMode  = params.stereoChoice;
            p.lowCutHz    = params.lowCutHz;
            p.highCutHz   = params.highCutHz;
            p.modRateHz   = params.modRateHz;
            p.modDepthPct = params.modDepthPct;
            p.drivePct    = params.drivePct;
            p.fx          = fxOn;
            p.fxType      = fxType;
            p.fxAmountPct = fxAmountPct;
            return p;
        };

        mainEngine.setParams (voiceOf (params.timeMs, params.fx, params.fxTypeChoice,
                                       params.fxAmountPct),
                              snapNow);
        mainEngine.setFeedbackGain (feedbackGainFor (params.feedbackPct,
                                                     mainEngine.referenceLoopPeak()),
                                    snapNow);

        // **`fx_link` lives here and cannot live in the engine** (10 §11.3,
        // §11a). It is a decision about *which three numbers the lane engine
        // is handed*, which is a statement about the two instances -- so it
        // belongs where the difference between them already lives, beside the
        // two gain laws. An engine that took the flag would be an engine that
        // knew it was the lane, which is exactly the seam 10 §11.1 forbids.
        //
        // **While it is on, the lane's three rows are ignored, not
        // overwritten** (§11.3): nothing is written back to them, they keep
        // whatever they held, and they come back untouched when the tie
        // releases. That is why there is no seeding here and why `11` §4e7 can
        // assert that automating `fx_link` writes no parameters at all -- there
        // is nothing in this module that writes one.
        //
        // **The tie is of values and never of state.** Even with both trios
        // identical the two stages run from separate buffers, because each
        // engine holds its own; that is what keeps "FX off is bit-identical to
        // the loop without the stage" true per path at every setting of this
        // flag (§11a).
        const auto laneFxOn     = params.fxLink ? params.fx : params.laneFx;
        const auto laneFxType   = params.fxLink ? params.fxTypeChoice : params.laneFxTypeChoice;
        const auto laneFxAmount = params.fxLink ? params.fxAmountPct : params.laneFxAmountPct;

        // **`lane_note` never reaches here, any more than `note` does** (10
        // §11.7): with SYNC on, `DwellDsp::apply` maps both divisions to
        // milliseconds at the held host tempo before handing the parameters
        // over, so the lane engine runs from `laneTimeMs` either way.
        laneEngine.setParams (voiceOf (params.laneTimeMs, laneFxOn, laneFxType, laneFxAmount),
                              snapNow);

        // 10 §11.2's bipolar tail. **The detent snaps rather than smooths**:
        // `lane_gain` at exactly 0 is a promise that the loop peak is 1.000,
        // and a smoothed approach leaves 0.9999 circulating -- a hold that
        // quietly decays. That is the same rule §9 already imposes on the dry
        // gain below 50 % MIX, for the same reason.
        const auto atDetent = params.laneGain == 0.0f;

        laneEngine.setFeedbackGain (laneGainFor (params.laneGain,
                                                 laneEngine.referenceLoopPeak()),
                                    snapNow || atDetent);

        applyHold (params.hold, snapNow);

        // SEND does nothing without HOLD (10 §11.4): with the lane not
        // circulating there is nothing for an input to land in, so the gate is
        // held closed rather than opening onto a dead ring.
        sendGate.setOpen (holdOn && params.sendHeld);

        // CHOP gates the lane's **output only** and never its contents, so it
        // is free to take its state instantly whenever there is no output for
        // an edge to click on.
        if (laneLive)
            chopGate.setOpen (! params.chop);
        else
            chopGate.snap (! params.chop);

        duckAmount.setTarget (std::clamp (params.duckDb, 0.0f, 24.0f));

        if (! gainsPrimed || snapNow)
            duckAmount.snap (std::clamp (params.duckDb, 0.0f, 24.0f));

        const auto m = std::clamp ((double) params.mixPct * 0.01, 0.0, 1.0);
        const auto belowHinge = m <= 0.5;

        const auto wetTarget = belowHinge ? std::sin (kPiD * m) : 1.0;
        const auto dryTarget = belowHinge ? 1.0 : std::cos (kPiD * (m - 0.5));
        const auto laneTarget = std::pow (10.0, (double) params.laneLevelDb / 20.0);

        if (! gainsPrimed || snapNow)
        {
            wetGain.snap ((float) wetTarget);
            dryGain.snap ((float) dryTarget);
            laneLevel.snap ((float) laneTarget);
            gainsPrimed = true;
            return;
        }

        // **Both gains are smoothed on both sides of the hinge, and across
        // it.** Until 2026-10-01 the dry was snapped to 1 on the way down and
        // the smoothed wet was dropped on the way up, so a host jump across
        // 50 % inside one block stepped the output by most of the signal
        // (0.433 on a 0.5 sine, measured on AURORA). Now each gain just moves
        // to its target over 20 ms, wherever the target is, and below the
        // hinge the dry *lands* on exactly 1.0 (`Smoother::tickLanding`) --
        // which is when `processChunk` goes back to not multiplying it at all.
        wetGain.setTarget ((float) wetTarget);
        dryGain.setTarget ((float) dryTarget);
        laneLevel.setTarget ((float) laneTarget);
    }

    //==========================================================================
    /** **HOLD gates the lane's life, and switching it off CLEARS the lane**
        (10 §11.4).

        It must clear rather than mute, and the distinction is the whole test
        in `11` §4e2: a muted-but-circulating buffer stacks on the next SEND,
        so the user hears the old word reappear under the new one at whatever
        level HOLD had hidden it. A mute would pass a level check and fail a
        musician.

        The clear is preceded by a **1 ms mute** -- the same fade CHOP uses --
        so dropping a full lane does not click: the zeroing happens under
        silence. That is why HOLD off does not take effect here but arms
        `laneMute`, and why `laneLive` outlives `holdOn` by a millisecond.

        Turning HOLD on is **instant and needs no ramp**, because the lane is
        empty. The one case that needs care is HOLD coming back on while a drop
        is still in flight: the pending clear is finished *first*, or the
        content the user asked to be rid of survives into the next send, which
        is the failure the clear exists to prevent. */
    void applyHold (bool wanted, bool snapNow) noexcept
    {
        if (snapNow)
        {
            holdOn = wanted;
            clearLane();
            laneLive = wanted;
            return;
        }

        if (wanted == holdOn)
            return;

        holdOn = wanted;

        if (holdOn)
        {
            if (laneLive)
                clearLane();

            laneMute.snap (true);
            laneLive = true;
        }
        else
        {
            // 1 ms of mute, then `processChunk` does the zeroing underneath it.
            laneMute.setOpen (false);
        }
    }

    /** Everything 10 §11.4 lists: the ring, both filter states, the DC
        blocker, the shaper, the modulation phases and the crossfade state --
        `DelayEngine::reset()` is exactly that list, which is why the clear is
        one call and not a second copy of the engine's own state. Allocates
        nothing; it is a fill. */
    void clearLane() noexcept
    {
        laneEngine.reset();

        for (auto& b : wetLane)  std::fill (b.begin(), b.end(), 0.0f);
        for (auto& b : laneFeed) std::fill (b.begin(), b.end(), 0.0f);

        sendGate.snap (false);
        laneMute.snap (true);
        laneLive = false;
    }

    /** **The key is sanitised before it reaches any state.** A one-pole and a
        log follower both keep a NaN or an infinity they are handed, for good:
        the gain reduction became NaN, the output guard below turned every
        sample into 0 -- the dry with it -- and only a `reset` recovered. The
        engines already refuse a non-finite sample at their ring writes; this is
        the same refusal at the ducker's door. Measured on AURORA 2026-10-01:
        one NaN at DUCK 6 dB took the output to exact zeros for good. */
    static double finiteOrZero (float x) noexcept
    {
        return std::isfinite (x) ? (double) x : 0.0;
    }

    /** 10 §6's detector, one sample: the key high-pass and the follower,
        **run whether or not DUCK is up**.

        They ran only while ducking until 2026-10-01, and whether it was
        ducking is decided once a chunk -- so a DUCK taken to 0 and back froze
        the follower at a point set by where the host's blocks fell, and the
        audio after it changed with the block size (0.0125 between blocks of 64
        and 1024, measured on AURORA). Run always, the detector's state is a
        function of the input alone. It is two one-poles and a log a sample;
        the gain itself is still only computed while ducking. */
    double duckLevelFor (const float* const* dry, int i, int nch) noexcept
    {
        const auto keyL = duckKey[0].highPass (finiteOrZero (dry[0][i]));
        const auto keyR = nch >= 2 ? duckKey[1].highPass (finiteOrZero (dry[1][i])) : 0.0;

        const auto peak = nch >= 2 ? 0.5 * (std::abs (keyL) + std::abs (keyR))
                                   : std::abs (keyL);

        return duckFollower.tick (LevelDetectorDb::levelDb (peak));
    }

    /** 10 §6, and the two things about it that are structural rather than
        careful.

        **The follower reads the dry input only**, through a fixed key
        high-pass, so what decides the depth is the track being sent to the
        delay rather than the delay's own returns -- a follower on the mix
        would duck itself and never recover.

        **`GR` is applied to the wet output after the loop tap**, so it is not
        in the feedback path and **ducking can never shorten the tail**. That
        is not a tuning choice that could be got wrong by 0.5 dB; it is a
        position in the graph, and `11` §4g proves it by asserting the main
        engine's own tap is bit-identical at DUCK 0 and at 24 dB.

        At DUCK 0 the gain is branched past rather than multiplying by 1.0 --
        `11` §4g wants the null bit-exact -- and the lane never sees this
        function at all (10 §11.3: the lane's whole job is to be heard). */
    float duckGainFor (double levelDb) noexcept
    {
        const auto amount = (double) duckAmount.tick();
        const auto over = std::clamp ((levelDb - kDuckThresholdDb) / kDuckWidthDb, 0.0, 1.0);

        return (float) std::pow (10.0, -amount * over / 20.0);
    }

    void processChunk (float* const* channels, int numChannels_, int offset, int count) noexcept
    {
        const auto nch = std::min (numChannels_, (int) DelayEngine::kMaxChannels);

        const float* dry[DelayEngine::kMaxChannels] {};
        const float* feed[DelayEngine::kMaxChannels] {};
        float* main[DelayEngine::kMaxChannels] {};
        float* lane[DelayEngine::kMaxChannels] {};

        for (int ch = 0; ch < nch; ++ch)
        {
            dry[ch]  = channels[ch] + offset;
            feed[ch] = laneFeed[(size_t) ch].data();
            main[ch] = wetMain[(size_t) ch].data();
            lane[ch] = wetLane[(size_t) ch].data();
        }

        // **The main engine runs first, off the untouched dry, and nothing
        // downstream of this line can reach it** (10 §11, §11.1). The `s`
        // input gate is gone from the injection rather than repurposed, so
        // there is no mechanism by which a send could disturb the main delay
        // -- which makes `11` §4e1's bit-identity structural rather than
        // careful. The one thing that would break it is feeding this engine
        // anything but `dry`.
        mainEngine.process (dry, main, nch, count);

        // 10 §11.1 item 1: **the lane taps the dry input, not the main's
        // wet**, gated by SEND. A send should catch the source word, not the
        // main delay's already-coloured repeats -- and tapping the wet would
        // make the lane's content depend on the main's FEEDBACK, which is the
        // bit-identity claim above running backwards.
        //
        // With the lane not live the gate is irrelevant: it is not fed, not
        // run and not read, which is what "HOLD off emits exact zeros" costs
        // -- one branch (10 §11.8).
        if (laneLive)
        {
            for (int i = 0; i < count; ++i)
            {
                const auto s = (float) sendGate.tick();

                for (int ch = 0; ch < nch; ++ch)
                    laneFeed[(size_t) ch][(size_t) i] = s * dry[ch][i];
            }

            // **A send onto an occupied lane sums** (10 §11.4): nothing here
            // clears or ducks the ring first, so words layer into a chord and
            // the engine's own in-loop safety clip is what bounds them.
            laneEngine.process (feed, lane, nch, count);
        }

        // 10 §6: the ducker's gain is skipped outright at DUCK 0 rather than
        // multiplying by 1.0, so the null is bit-exact; its detector runs
        // regardless (see `duckLevelFor`). The smoother is allowed to finish
        // its travel to exactly zero before the branch takes effect, so
        // releasing the control is a fade rather than a step.
        const auto ducking = params.duckDb > 0.0f || duckAmount.value() > 0.0f;

        auto clearWhenDone = false;

        for (int i = 0; i < count; ++i)
        {
            const auto w  = wetGain.tickLanding();
            const auto d  = dryGain.tickLanding();

            // §9: the dry is multiplied by **nothing**, not by 1.0f, whenever
            // its gain is exactly 1 -- every MIX at or below 50 % once the
            // gain has landed. Above the hinge the wet's gain lands on exactly
            // 1.0 too, so the wet is then carried unscaled to the bit.
            const auto dryIsBitExact = d == 1.0f;
            const auto ll = laneLevel.tickLanding();

            const auto level = duckLevelFor (dry, i, nch);
            auto gr = 1.0f;

            if (ducking)
                gr = duckGainFor (level);

            // CHOP gates the lane's output only; the mute is the 1 ms that
            // precedes a HOLD-off clear. Both are on this side of the engine,
            // so neither can reach what is circulating.
            auto laneOut = 0.0f;

            if (laneLive)
            {
                laneOut = (float) (chopGate.tick() * laneMute.tick()) * ll;

                if (! holdOn && laneMute.isFullyClosed())
                    clearWhenDone = true;
            }

            for (int ch = 0; ch < nch; ++ch)
            {
                // §11.1 item 3: `wet = GR . wet_main + chop . LEVEL . y_lane`.
                // **The ducker is on the main's side of the plus sign only**
                // -- §6's GR never reaches the lane, because the lane's whole
                // job is to be heard, and ducking it would duck the emphasis
                // against the source that caused it.
                const auto wet = gr * main[ch][i] + (laneLive ? laneOut * lane[ch][i] : 0.0f);
                auto* slot = channels[ch] + offset + i;

                // §9: below 50 % the dry is multiplied by **nothing**, not by
                // 1.0f. That is what makes the null bit-exact rather than
                // merely -120 dB, and it is why this is a branch and not a
                // gain of 1.
                const auto out = dryIsBitExact ? (*slot + w * wet)
                                               : (d * *slot + w * wet);

                *slot = std::isfinite (out) ? out : 0.0f;
            }
        }

        // The mute has run out, so the zeroing happens under silence (10
        // §11.4). It is done at the end of the chunk rather than in the middle
        // of it because the lane's output is already exactly zero from the
        // sample the fade landed on: what is cleared is state, and state is
        // only read on the next block.
        if (clearWhenDone)
            clearLane();
    }

    Params params;

    DelayEngine mainEngine, laneEngine;

    std::array<std::vector<float>, DelayEngine::kMaxChannels> wetMain, wetLane, laneFeed;

    Smoother wetGain, dryGain, laneLevel, duckAmount;
    bool gainsPrimed = false, parametersSeen = false;

    /** The lane's three gates (10 §11.4). **All three live out here rather
        than in `DelayEngine`**, which is 10 §11.1's requirement written as a
        member list: an engine that held a send gate would be an engine that
        knew it was the lane. */
    CosineGate sendGate, chopGate, laneMute;

    bool holdOn = false, laneLive = false;

    /** 10 §6's ducker, main-engine only. The key filter is one pole per
        channel at a corner fixed at build time -- no parameter is reserved for
        it, so there is nothing here for a preset to carry. */
    std::array<TptOnePole, DelayEngine::kMaxChannels> duckKey;
    LevelDetectorDb duckFollower;

    double sampleRate  = 48000.0;
    int maxBlockSize   = 512;
    int numChannels    = 2;
};

} // namespace bmo::dwell
