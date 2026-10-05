#pragma once

#include "Filters.h"
#include "core/dsp/Oversampler.h"
#include "core/dsp/SwitchFade.h"
#include "Shaper.h"
#include <array>
#include <vector>

namespace bmo::sat
{

//==============================================================================
/** One-pole parameter smoother.

    Snaps to the target once it is within epsilon, so a settled parameter
    compares exactly equal and the coefficient recomputation can be skipped.
*/
class Smoother
{
public:
    void prepare (double controlRateHz, double timeMs) noexcept
    {
        const auto tau = std::max (timeMs, 0.01) * 0.001;
        coeff = (float) (1.0 - std::exp (-1.0 / (std::max (controlRateHz, 1.0) * tau)));
    }

    void snap (float v) noexcept        { current = target = v; }
    void setTarget (float t) noexcept   { target = t; }
    float value() const noexcept        { return current; }

    float tick() noexcept
    {
        current += coeff * (target - current);

        if (std::abs (target - current) < 1.0e-6f)
            current = target;

        return current;
    }

private:
    float coeff = 1.0f, current = 0.0f, target = 0.0f;
};

//==============================================================================
/** Everything the plugin does to audio, with no dependency on JUCE's plugin
    layer or on a host. Takes plain values and raw buffers, so the measurement
    harness and the unit tests can drive the real signal path directly.

    The chain, per channel, inside the oversampled region:

        input trim -> the fitted curve
                   -> plus two harmonic generators, each filling one band
                   -> DC blocker -> makeup -> output trim

    The generators are the unusual part, and they are the reason this hits the
    brief rather than merely saturating.

    A waveshaper applied the ordinary way -- signal in, shaped signal out --
    distributes its new harmonics wherever they fall. On a voice that is mostly
    the 600 Hz to 2.5 kHz octaves, since that is where the second and third
    harmonics of the fundamental land. The reference this plugin is fitted to
    does the opposite: it leaves everything below 2.5 kHz within about a dB of
    where it started and puts 6 to 8 dB of new energy above it. No single curve
    applied to the whole signal does that at any drive setting. Push it hard
    enough to fill the top and the midrange fills with it.

    So the fitted curve runs across the whole signal and carries the character
    -- the asymmetry, the even-order content, the compression the reference
    measured -- and two further instances of the same curve at the same drive
    place the new energy. Each is fed only the band below a corner and read
    only above it, so what it contributes is harmonic content that was not
    there before rather than a scaled copy of the programme. They are still
    harmonics of the programme, made by the same asymmetric curve and carrying
    its even-order signature; they are simply weighted towards the part of the
    spectrum the target puts them in.

    That structure also settles the dynamics. The dry path through the stage is
    never attenuated, so the programme's own peaks arrive intact and the added
    residual is largest exactly where the waveform moves fastest -- on
    transients. The crest factor comes out slightly up, which is what was asked
    for. There is no compressor, limiter, or peak reduction anywhere in here,
    and adding one would be a change of design rather than a feature.
*/
class DspCore
{
public:
    struct Params
    {
        float inputGainDb   = 0.0f;
        float driveAmount   = 40.0f;   // per cent, the panel's DRIVE
        float toneAmount    = 100.0f;  // per cent, the panel's TONE
        float mixPercent    = 100.0f;
        float outputLevelDb = 0.0f;

        bool  saturationIn = true;
        bool  phaseInvert  = false;
        bool  autoGain     = false;

        int   oversampling = 1;        // 1, 2, 4 or 8
    };

    void prepare (double sampleRate, int maxBlockSize, int numChannels, int oversampleFactor = 2);
    void reset() noexcept;

    /** Round-trip delay of the oversampling filters, in samples at the host's
        rate. Reported to the host so plugin delay compensation can undo it.
        After a change of oversampling it is the new factor's from the first
        process() on, while the audio dips through the change. */
    int getLatencySamples() const noexcept { return latencySamples; }

    /** How long each side of a switch's fade takes. Ten milliseconds, as the
        EQ's: long enough that a 1 kHz tone crosses any of these switches
        well under the 1.5x step bound, short enough to read as immediate. */
    static constexpr double kSwitchFadeMs = 10.0;

    /** Called once per block, before process(). Cheap: stores targets only. */
    void setParams (const Params&) noexcept;

    void process (float* const* channels, int numChannels, int numSamples) noexcept;

    /** How many samples, at the oversampled rate and summed over channels and
        paths, the oversampled region has processed since construction. A
        count of the work rather than a clock: the switch tests bound what a
        single callback may do with it, deterministically. Not used by the
        audio. */
    unsigned long long oversampledSamplesProcessed() const noexcept { return wetSamplesProcessed; }

    //== The character, in one place ==========================================

    /** DRIVE, 0-100 on the panel, mapped to the curve's drive.

        Geometric, because what the ear follows is the ratio between where the
        signal sits and where the knee is, not the difference. The bottom of
        the range is deliberately not zero: at DRIVE 0 the curve is still the
        curve, just far enough below the knee to be almost linear, so turning
        the control up changes the amount of a character rather than fading one
        in. */
    static float driveFor (float amountPercent) noexcept;

    /** Where Auto Gain settles, for a given drive, on the harness's reference
        voice. Kept for the measurement tool and the tests; the plugin itself
        no longer uses it. See the note on the detector in the .cpp. */
    static float makeupGainDb (float amountPercent) noexcept;

    static constexpr int kSubBlock = 32;

    //== The character ========================================================
    /** The constants that decide what this sounds like, gathered in one place
        and settable, because they were fitted rather than chosen and will be
        fitted again whenever a better reference turns up.

        The defaults below are the shipping values. Nothing in the plugin ever
        changes them; the measurement harness does, so that `measure fit` can
        search this space against a real before/after pair rather than against
        a synthetic signal that may or may not resemble one. That distinction
        cost a release: the first fit was made against a test signal with 22 dB
        less energy above 6 kHz than the actual reference vocal, so the same
        harmonic generation that measured +8 dB on the test signal measured
        +0.4 dB on the real thing.
    */
    struct Character
    {
        /** Where the target's flat bands stop and its lifted ones begin. */
        double residualSplitHz = 2400.0;

        /** The two harmonic generators, each a second instance of the same
            curve at the same drive, fed only from below a corner and read only
            above it -- so what each contributes is harmonic content that was
            not there before rather than a scaled copy of the programme.

            The `body` generator refills 600 Hz to 2.5 kHz, which a compressive
            curve suppresses. The `sheen` generator fills 2.5 kHz upwards,
            where the reference puts most of its new energy.

            bodyGain's sign is load-bearing and is fitted, not reasoned out.
            The body generator's harmonics land where the programme already has
            harmonics of its own, so they either reinforce or cancel depending
            on which way round they are added -- and which way is right changed
            when the voicing arrived. It was -3 when the plugin was all
            waveshaper; it is +3 now. Whenever a generator is fed a band the
            source already occupies, this sign has to be re-fitted rather than
            assumed. */
        double bodySourceHz  = 600.0;
        float  bodyGain      = -3.0f;
        double sheenSourceHz = 6000.0;
        float  sheenGain     = 3.0f;

        /** A shelf on the sheen generator's output only, tilting the top
            octaves up so 6-18 kHz lifts further than 2.5-6 kHz. It shapes
            distortion the plugin generated, never the programme, which is why
            it is not a tone control and is not on the panel.

            Left at 1.30 in 0.5.0 despite the 0.5.0 measurement pass naming it
            as a suspect for the high end. It is not one: it acts only on the
            sheen generator's output, and the AUTO-off render at TONE 0 puts
            the whole waveshaper within 0.5 dB of the dry file above 5 kHz.
            Whatever this shelf is doing, it is doing it to a signal small
            enough not to move a band table. Changing it would have been a
            change that measured as nothing. See bellQ below for what the
            high end actually was. */
        double sheenHz   = 6000.0;
        float  sheenTilt = 1.30f;

        /** The voicing.

            Measured, and it is the finding that mattered most: fitting the
            best linear filter from the reference's dry file to its processed
            one explains 97 to 98 per cent of the difference. The reference is
            not mostly harmonic generation. It is a broad bell around 7 kHz of
            roughly +10 dB, about 2 dB of broadband trim, and a high-pass at
            the bottom -- with a few per cent of nonlinearity on top.

            No amount of waveshaping reaches those band figures, because the
            band figures are not made by waveshaping. They are made by an
            equaliser. This stage is that equaliser, stated plainly rather than
            hidden inside a curve, and the panel's TONE control scales it from
            nothing to the fitted shape. */
        /** Re-fitted in 0.5.0 against an AUTO-off render pair, which is the
            first measurement of this stage that was not confounded.

            The 0.4.0 pass concluded sibilance was a transient/asymmetry
            problem; the 0.5.0 pass corrected that to band energy above 9 kHz
            but could not say whether the cause was this bell, the sheen
            shelf, or the waveshaper, because every render available had AUTO
            engaged. Rendering the dry file twice -- once at TONE 100, once at
            TONE 0, both with AUTO off -- separates them completely:

              at TONE 0 the plugin is within 0.5 dB of the dry file
              everywhere above 5 kHz. At TONE 100 it is +8.8 to +9.6 dB.

            So the waveshaper contributes essentially nothing up here and this
            stage is the whole high end, which is DspCore's own "97-98% is a
            linear filter" note arrived at from the other direction.

            Against the reference, ours was not simply hot -- it was the wrong
            shape. The reference peaks sharply at 7-9 kHz and falls away;
            ours peaked lower and spilled upward:

              band     reference   ours    ours - reference
              5-7k       +9.68     +8.79       -0.89
              7-9k      +11.13     +9.59       -1.54
              9-12k      +6.53     +7.66       +1.13
              12-16k     +2.85     +4.67       +1.82

            That is a bell too wide, not a bell too loud, and not the shelf:
            a shelf error is monotonic above its corner, and this one changes
            sign -- under the reference at 5-9 kHz, over it at 9-16 kHz. No
            single shelf setting does that.

            The centre was right all along; fitting f0/Q/gain against those
            four figures leaves f0 at 7 kHz and moves the width, so the lift
            narrows onto where the reference puts it. Roughly a dB of the
            12-16 kHz excess survives, which one bell cannot reach without
            giving up 7-9 kHz.

            **Deliberately fitted on four bands, not five.** A 16-20 kHz row
            exists and is not used, because it cannot be trusted here: the
            reference vocals are 44.1 kHz and the renders are 48 kHz Ableton
            bounces, and running the *same* audio through both paths costs
            1.29 dB in 16-20 kHz while costing at most 0.29 dB in every band
            below it. Our 16-20 kHz figure was -1.27 dB, which is the
            artifact and nothing else. An earlier version of this note argued
            from that band that the error could not be the shelf; the
            argument above replaces it, and the fit is unchanged either way
            because the band was already weighted near zero. The frequency
            axes themselves are fine -- the bounces align with the originals
            to a scale factor of 1.00000, so nothing is pitch-shifted. */
        double bellHz     = 7000.0;
        double bellQ      = 1.40;
        float  bellGainDb = 13.5f;
        double highPassHz = 40.0;
    };

    void setCharacter (const Character& c) noexcept { character = c; }
    const Character& getCharacter() const noexcept { return character; }

private:
    void applyOversampling (int factor);
    void beginWarming (int factor) noexcept;
    void switchOversampling() noexcept;
    void updateAutoGain (double blockInput, double blockProcessed, int samples) noexcept;

    /** Auto Gain's figure from the detector's reading, or unity if it has
        never had one. */
    float currentAutoGain() const noexcept;

    struct Channel
    {
        AsymmetricShaper shaper, bodyShaper, sheenShaper;
        Bell             bell;
        HighPass         highPass;
        OnePole          bodyInput[2], bodySplit;
        OnePole          sheenInput[2], sheenSplit;
        Shelf            sheenTilt;
        DcBlocker        dc;
        Oversampler      oversampler;

        void prepare (double rate, const Character&) noexcept;
        void reset() noexcept;

        /** Everything but the oversampler: the stage itself, which stops
            while Sat In is out and starts from rest when it comes back. */
        void resetStage() noexcept;

        void setDrive (float drive) noexcept;
        void setTone (float amountPercent, double rate) noexcept;
        float toneAmount = 1.0f;
        const Character* character = nullptr;
        float process (float x) noexcept;

        /** One host-rate sample through the oversampled region: up, the
            stage at each oversampled sample (or a wire with Sat In off),
            down. The live path and, during an oversampling change, the
            standby one both go through here. While Sat In fades, the stage
            and the wire are blended at `amount`, 1 being the stage. */
        float runWet (float driven, int factor, bool saturate,
                      bool fading = false, float amount = 1.0f) noexcept;

        /** One harmonic generator: shape the band below the corner, keep what
            appears above it. */
        static float generate (AsymmetricShaper&, OnePole (&input)[2], OnePole& split, float x) noexcept;
    };

    double sampleRate = 44100.0;
    double effectiveRate = 44100.0;
    int    latencySamples = 0;
    unsigned long long wetSamplesProcessed = 0;

    // The oversampled region, both channels, twice over. One path is live;
    // the other runs only while the oversampling changes, at the new factor
    // on the same live input, so that when the dip turns the new path is
    // already mid-stream. pathFactor is each path's factor.
    std::array<std::array<Channel, 2>, 2> paths;
    std::array<int, 2> pathFactor { 1, 1 };
    int live = 0;

    std::array<Channel, 2>& livePath() noexcept     { return paths[(size_t) live]; }
    std::array<Channel, 2>& standbyPath() noexcept  { return paths[(size_t) (1 - live)]; }
    void preparePath (std::array<Channel, 2>&, int factor) noexcept;

    // While true the standby path runs at pendingFactor alongside the live
    // one, and warmedSamples counts the host-rate samples it has heard.
    bool warming = false;
    int  warmedSamples = 0;

    // The dry path of the Mix control has to be delayed to match, or a partial
    // blend combs and a full bypass fails to null. The ring is one length for
    // every factor, long enough for the longest latency, and read at the live
    // path's latency, so a change of factor moves the read point rather than
    // resizing anything.
    static constexpr int kDryRing = Oversampler::kMaxLatency + 2;
    std::vector<float> dryDelay;
    int dryWrite = 0, dryStride = 0, dryLatency = 0;

    // A change of oversampling waits at the bottom of this dip while the
    // standby path warms up alongside; see process().
    bmo::dsp::Dip oversamplingDip;
    int pendingFactor = 1;
    bool running = false;   // false until the first process() after prepare() or reset()

    // Sat In and Phase cross over rather than stepping; see setParams().
    bmo::dsp::Ramp satMix, polaritySwitch;

    Smoother inputGainSm, driveSm, mixSm, outputLevelSm, makeupSm, toneSm;

    // The control period in progress: the smoothed values read at its start,
    // how far into it the stream is, and what Auto Gain's detector has heard
    // of it so far. It runs across process() calls; see process().
    struct Held
    {
        float inGain = 1.0f, outGain = 1.0f, makeup = 1.0f, wet = 1.0f, drive = 1.0f, tone = 100.0f;
    } held;

    int    periodPos = 0, periodSamples = 0;
    double periodInput = 0.0, periodProcessed = 0.0;

    /** Auto Gain's detector: the energy going into the saturation and the
        energy coming out of it, each averaged over about a second and a half.

        Slow on purpose, and the slowness is the whole design. A fast detector
        that followed the programme would be a compressor, which is the one
        thing this plugin must not become; at this time constant it cannot
        respond to anything inside a phrase, so it moves the level and leaves
        the dynamics alone. A test asserts that switching it on changes the
        crest factor by less than a quarter of a decibel.

        The first version of this was a fixed table fitted to one voice at one
        level. It was inaudible on other material and at some settings pulled
        the wrong way -- which is what "AUTO does nothing" in the test report
        turned out to mean. */
    double inputEnergy = 0.0, processedEnergy = 0.0;
    float  autoGainCoeff = 0.0f;

    Params params;
    Character character;
    bool primed = false;
    int maxBlock = 0, maxChannels = 0;
    int currentFactor = 0;
};

} // namespace bmo::sat
