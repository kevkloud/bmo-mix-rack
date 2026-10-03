#pragma once

#include "Crossover.h"
#include "Detector.h"
#include "Gate.h"
#include "Limiter.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

namespace bmo::vcomp
{

/** How close a smoother has to get before it lands on its target, in the
    parameter's own units: % of AMOUNT, dB of OUTPUT. The landing is the one
    step a move takes that is not on its curve, and this is how big it can
    be: 0.001 dB of OUTPUT is a gain step of 1.2e-4, and 0.001 % of AMOUNT
    moves the static gain by at most 0.00035 dB. Against a 100 Hz tone's own
    largest step at 192 kHz, 3.3e-3 of its amplitude, either is under a
    twentieth. */
inline constexpr float kSmootherLandWithin = 1.0e-3f;

/** One-pole parameter smoother, the same shape as the ones in modules/sat/dsp
    and modules/opto/dsp: a 15 ms one-pole that lands on its target and then
    holds it, so a settled parameter compares exactly equal.

    AMOUNT and OUTPUT get one each. Both are steady knobs most of the time, but
    either can be automated, and AMOUNT moves the threshold, the knee, the
    ratio and the makeup gain at once -- stepping all four block to block with
    no ramp is an audible zipper on the one control anybody will ride.

    **It used to stall instead of landing.** The state was a float, and near
    AMOUNT 100 one float step is 7.6e-6, so once the pole's increment fell
    under half of that it rounded away and the smoother stopped where it was:
    0.0007 to 0.011 short of the target (dB of OUTPUT, % of AMOUNT) for good,
    under the 1e-5 it snapped within. A held setting after a move then never
    sounded the way that setting sounds from a fresh instance. The state is a
    double now, which keeps moving all the way in, and it lands when it is
    within kSmootherLandWithin: a full-range move takes about 175 ms at any
    rate. Once landed it does no arithmetic at all. */
class Smoother
{
public:
    void prepare (double sampleRate, double timeMs) noexcept
    {
        const auto tau = std::max (timeMs, 0.01) * 0.001;
        coeff = 1.0 - std::exp (-1.0 / (std::max (sampleRate, 1.0) * tau));
    }

    void snap (float v) noexcept
    {
        current = target = v;
        state = v;
        moving = false;
    }

    /** Safe to call every block with the control's current value: asking for
        the target already being approached, or held, changes nothing. */
    void setTarget (float t) noexcept
    {
        if (! (t < target) && ! (target < t))
            return;

        target = t;
        moving = true;
    }

    float tick() noexcept
    {
        if (! moving)
            return current;

        state += coeff * ((double) target - state);

        if (std::abs ((double) target - state) < (double) kSmootherLandWithin)
        {
            current = target;
            state   = target;
            moving  = false;
        }
        else
        {
            current = (float) state;
        }

        return current;
    }

private:
    double coeff = 1.0, state = 0.0;
    float  current = 0.0f, target = 0.0f;
    bool   moving = false;
};

/** How far below the makeup reference the thru bands' content is taken to sit,
    in dB -- and with it, how much makeup they are given.

    A band that is not compressed but is handed the whole makeup can only get
    louder, by the whole makeup figure. That was the shipped behaviour, and
    lifting this out of the code prints it exactly: the thru band's lift came
    out 12.44, 18.92 and 25.51 dB at AMOUNT 50, 70 and 90, which is the makeup
    column of `measure_vcomp curve` to the second decimal. Not approximately
    the makeup -- the makeup. So LOW THRU worked at the bottom of AMOUNT and
    defeated itself at the top, and "Keep The Chest" was held down at AMOUNT 35
    because at 70 it came out +8.1 dB hot.

    What the thru path is given instead is the *net* gain the curve would have
    given that content had it gone through the compressor: the makeup, less the
    reduction the curve applies at body level. Body level is the reference less
    this figure, because chest and air sit under the peaks the detector reads,
    not on them. It is a cap that the curve works out for itself at every
    AMOUNT rather than a number somebody picked, it costs the thru band no
    dynamics at all, and the thru band keeps the property it was split off for.

    Measured on AURORA, 2026-09-14, against the two other candidates in this
    module's notes and against a flat 6 dB cap. Tilt is `measure_vcomp
    balance`, pumping is the 80 Hz thru band under a 2 kHz burst from
    `measure_vcomp bands`, and both want to be near zero:

        candidate              tilt 30/50/70/90 dB    pumping   Chest at 70
        shipped, no cap        4.6 / 8.8 / 12.9 / 17.7   -1.5      +8.14
        half compression       2.2 / 4.3 /  6.8 /  9.4   -4.3
        gain tracked, 600 ms   2.2 / 3.8 /  5.4 /  6.9   -1.5, late
        flat 6 dB cap          4.3 / 2.9 /  1.9 /  1.3   -0.0      +1.96
        this                   2.4 / 1.7 /  1.1 /  0.6   -0.0      +1.36

    testing-notes/vcomp-thru-cap-2026-09-14.md has the full tables and why the
    other candidates lose. */
inline constexpr float kThruBodyOffsetDb = 6.0f;

/** The makeup the thru path is given: the whole makeup less what the curve
    would have taken off body-level content, which is the net gain that content
    would have come out with had it been compressed with everything else. */
inline float thruMakeupDbFor (const Curve& curve) noexcept
{
    return autoMakeupDb (curve) - kneeReductionDb (kReferenceDb - kThruBodyOffsetDb, curve);
}

//==============================================================================
/** BMO Vcomp.

        in -> gate -> [band split] -> compressor on the mid band
           -> + the thru bands -> auto makeup -> OUTPUT -> limiter

    **The gate is first**, because what it closes has to be closed before the
    makeup amplifies it, and it is keyed off the raw input so its threshold is
    an absolute level rather than one that moves with AMOUNT. See Gate.h.

    **The detector reads the full gated signal through the sidechain high-pass,
    not the mid band.** SIDECHAIN and LOW THRU are deliberately two controls
    doing two jobs -- what the compressor listens to, and what it acts on -- and
    keying the detector off the mid band would quietly merge them, so that
    moving LOW THRU would also change how hard the compressor works. It does
    not: LOW THRU changes only which parts of the signal the gain is applied
    to.

    **Standard mode ignores six parameters.** With COMPLEX off, ATTACK,
    RELEASE, ARC, SIDECHAIN, LOW THRU and HIGH THRU are not read at all -- the
    figures in params.h are used instead. The lock is here rather than in the
    panel, the same way BMO Opto's Color-in-Tele lock is in its DspCore, so it
    holds for the rack, for a preset, for automation and for the tests, not
    just for someone looking at the panel. Because those figures are also the
    six parameters' defaults, switching COMPLEX on with untouched knobs is
    silent.

    **Stereo is always linked**, and there is no switch for it. Two channels of
    one voice compressed independently is a wandering image, not a stereo
    option, and this module is for voices. One detector reads the louder of the
    two channels and both get the same gain; the gate is linked the same way,
    so one channel never opens without the other. Compare BMO Opto, which does
    have a LINK switch because it is a general-purpose box that ends up across
    a mix. */
class DspCore
{
public:
    struct Params
    {
        float amountPercent = 0.0f;
        float gateDb        = kGateOffDb;
        float outputDb      = 0.0f;
        bool  complex       = false;
        float attackMs      = kStandardAttackMs;
        float releaseMs     = kStandardReleaseMs;
        bool  arc           = kStandardArc;
        float sidechainHz   = kStandardSidechainHz;
        float lowThruHz     = kStandardLowThruHz;
        float highThruHz    = kStandardHighThruHz;
    };

    void prepare (double newSampleRate, int /*maxBlockSize*/, int numChannels) noexcept
    {
        rate = newSampleRate;
        numActiveChannels = std::clamp (numChannels, 1, (int) channels.size());

        for (auto& ch : channels)
        {
            ch.sidechain.prepare (rate);
            ch.bands.prepare (rate);
        }

        gate.prepare (rate);
        limiter.prepare (rate);
        release.prepare (rate);
        complexDip.prepare (rate, kComplexDipMs);

        amountSmoother.prepare (rate, 15.0);
        outputSmoother.prepare (rate, 15.0);
        amountSmoother.snap (params.amountPercent);
        cachedAmount = cachedOutputDb = kNotYet;
        outputSmoother.snap (params.outputDb);

        activeComplex = params.complex;
        applyTimings();
        reset();
    }

    void reset() noexcept
    {
        // A switch of COMPLEX in flight is abandoned for where the controls
        // are: reset() is where the state starts over, so the module comes
        // back already in the mode it was asked for, as a fresh instance does.
        complexDip.reset();
        activeComplex = params.complex;
        heard = false;
        applyTimings();

        for (auto& ch : channels)
        {
            ch.sidechain.reset();
            ch.bands.reset();
        }

        gate.reset();
        limiter.reset();
        release.reset();

        envelopeDb = 0.0f;
        reportedReductionDb = 0.0f;
    }

    void setParams (const Params& p) noexcept
    {
        params = p;
        amountSmoother.setTarget (p.amountPercent);
        outputSmoother.setTarget (p.outputDb);

        auto switchNow = false;

        // **COMPLEX is a switch, and it switches through a dip** (Frosty,
        // 2026-10-03). It changes the detector's six settings and brings both
        // sides of the split in or out at once, and taken the way a LOW THRU
        // knob is -- each side in by the edge of its range and a glide -- the
        // band in transit spent up to a second at the wrong gain: the suite's
        // voice more than 1 dB off where it settles for 0.61 s, +8.4 dB at
        // worst, and a 60 Hz tone +7.2 dB for 0.95 s with the limiter taking
        // 2.2 dB more than it settles to. So the output fades to nothing over
        // kComplexDipMs while the sides the switch brings in run unheard on
        // the input at their settings (BandSplit::prime), the switch is made
        // at the bottom, and the output fades back up over kComplexDipMs with
        // the split already settled -- the pattern BMO EQ's oversampling
        // change uses. Nothing heard since prepare() or reset() means nothing
        // to fade, and the change is made at once.
        //
        // Only when the switch moves the split, though. With LOW THRU and
        // HIGH THRU at their rails COMPLEX changes nothing but the detector's
        // settings, which are continuous and change as a knob does, so the
        // switch is made at once, as it always was.
        if (p.complex != activeComplex && ! movesSplit (p.complex))
        {
            if (complexDip.isPending())
                complexDip.cancel();

            switchNow = ! heard;

            if (heard)
            {
                activeComplex = p.complex;
                applyTimings();
                return;
            }
        }
        else if (p.complex != activeComplex)
        {
            if (! heard)
                switchNow = true;
            else if (! complexDip.isPending())
                complexDip.request();
        }
        else if (complexDip.isPending())
        {
            // Changed back before the dip reached the bottom: nothing to
            // change, and the output comes back up from where it got to.
            complexDip.cancel();
        }

        if (switchNow)
            activeComplex = p.complex;

        applyTimings();

        // **Nothing heard since prepare() or reset(), nothing to move.** A
        // setting that arrives before any audio -- a session or a preset
        // restored after the host has prepared the plugin and before its first
        // block, or after ModuleEngine::reset() -- lands at once, as it would
        // in a fresh instance: the smoothers snap, ARC's crossover snaps, and
        // the split is put where it is asked to be rather than walked there by
        // its knobs' edges. Until 2026-10-03 only COMPLEX did, and the rest
        // moved from the old setting as if it had been heard: AMOUNT for
        // 175 ms, which ARC's slow branch then remembered for most of a
        // second, and LOW THRU in by its edge over up to a second.
        if (! heard)
        {
            amountSmoother.snap (p.amountPercent);
            outputSmoother.snap (p.outputDb);
            release.reset();

            for (auto& ch : channels)
                ch.bands.reset();
        }
    }

    void process (float* const* channelData, int numChannels, int numSamples) noexcept
    {
        const auto active = std::min (numChannels, numActiveChannels);

        auto blockMaxReduction = 0.0f;

        heard = true;

        for (int i = 0; i < numSamples; ++i)
        {
            // The bottom of a COMPLEX switch: the split goes where the
            // controls say, keeping what it primed, and then the detector's
            // settings follow -- in that order, so the split is already in
            // when it is told where its sides are and has nothing to glide.
            if (complexDip.ready())
            {
                activeComplex = params.complex;

                for (auto& ch : channels)
                    ch.bands.switchTo (lowThruFor (activeComplex), highThruFor (activeComplex));

                applyTimings();
                complexDip.changed();
            }

            const auto dipping = ! complexDip.isIdle();
            const auto dipGain = dipping ? complexDip.next() : 1.0f;
            const auto priming = dipping && channels[0].bands.isPriming();

            // Asked per sample rather than read off the parameters once a
            // block: a side of the split fading out is still in circuit, and
            // the fade ends wherever in a block it ends. Every channel's split
            // is moved by the same calls, so the first speaks for all.
            const auto split = channels[0].bands.inCircuit();
            // The curve and the two gains read off the smoothers are worked
            // out again only when the smoothed value moves. Each is a pure
            // function of that value, so with a setting held the cached figure
            // is the figure, bit for bit, and the pow() is the cost saved.
            const auto amountNow = amountSmoother.tick();

            if (! (amountNow == cachedAmount))
            {
                cachedAmount    = amountNow;
                cachedCurve     = curveFor (amountNow);
                cachedMakeupLin = std::pow (10.0f, autoMakeupDb (cachedCurve) / 20.0f);
                thruMakeupValid = false;
            }

            const auto& curve = cachedCurve;

            // **The automatic makeup applies to the whole sum, including the
            // bands the compressor did not touch.** Frosty's spec, 2026-09-14:
            // LOW THRU picks a frequency, everything above it is compressed
            // and everything below is not, and *both* get the makeup.
            //
            // This was the other way round for one build, on the reasoning
            // that makeup gives back what the curve took and the thru bands
            // had nothing taken. That is true and it is not what a listener
            // judges. With makeup on the compressed band alone the thru band
            // sits at input level while the compressed band is lifted, so
            // engaging LOW THRU made the voice *thinner* the harder AMOUNT was
            // pushed -- measured at -1.3 dB of tilt at AMOUNT 30 and -4.3 dB
            // at 90. A control called LOW THRU that removes low end is the
            // opposite of the thing, which is how the ear found it.
            //
            // The cost is the other direction and it is bigger: an
            // uncompressed band taking full makeup can only get louder, by the
            // whole makeup figure, so the feature worked at the bottom of the
            // knob and defeated itself at the top. The thru path's makeup is
            // therefore the net gain the curve would have given that content
            // had it been compressed -- see kThruBodyOffsetDb for the figures,
            // and for the two candidates measured and rejected against it.
            //
            // OUTPUT multiplies the sum either way: it is the user's trim on
            // the whole module.
            const auto autoMakeupLin = cachedMakeupLin;

            // Only when the split is in circuit is there a thru band to give
            // it to, so the second figure is worked out only then.
            if (split && ! thruMakeupValid)
            {
                cachedThruMakeupLin = std::pow (10.0f, thruMakeupDbFor (curve) / 20.0f);
                thruMakeupValid = true;
            }

            const auto thruMakeupLin = split ? cachedThruMakeupLin : autoMakeupLin;

            const auto outputNow = outputSmoother.tick();

            if (! (outputNow == cachedOutputDb))
            {
                cachedOutputDb  = outputNow;
                cachedOutputLin = std::pow (10.0f, outputNow / 20.0f);
            }

            const auto outputLin = cachedOutputLin;

            // The gate is keyed off the raw input, before anything else.
            // The IN meter the gate handle sits on is the engine's own input
            // meter, which is taken at the same point -- so the handle is
            // dragged against the level it is actually judging.
            auto inputPeak = 0.0f;

            for (int ch = 0; ch < active; ++ch)
                inputPeak = std::max (inputPeak, std::abs (channelData[ch][i]));

            // One gate for both channels, off the louder of the two: a gate
            // that opened on one channel only would swing the image.
            // A gate at its rail and all the way open is exactly 1, and then
            // the input's level -- a log10 a sample -- is not needed.
            const auto gateGain = gate.isIdle() ? 1.0f : gate.process (levelDbOf (inputPeak));

            // One detector for both channels, off the louder after each
            // channel's own sidechain high-pass.
            auto detected = 0.0f;

            for (int ch = 0; ch < active; ++ch)
                detected = std::max (detected,
                                     std::abs (channels[(size_t) ch].sidechain.process (channelData[ch][i] * gateGain)));

            const auto demandDb = kneeReductionDb (levelDbOf (detected), curve);

            // Smooth decoupled peak detector: the release stage takes the
            // demand instantly, then one attack pole shapes the whole thing.
            // See ReleaseStage in Detector.h for why the attack lives out here
            // and not inside the branches.
            const auto released = release.tick (demandDb);
            envelopeDb = attackPole * envelopeDb + (1.0f - attackPole) * released;

            if (envelopeDb < kEnvelopeFloorDb)
                envelopeDb = 0.0f;

            blockMaxReduction = std::max (blockMaxReduction, envelopeDb);

            // No reduction is a gain of exactly 1, which is what the pow()
            // would return for it.
            const auto compressorGain = envelopeDb == 0.0f ? 1.0f : std::pow (10.0f, -envelopeDb / 20.0f);

            // Both channels are worked out before either is written, because
            // the limiter needs the peak of the pair to decide one gain for
            // both. Writing as we went and limiting afterwards would either
            // limit each channel on its own -- which swings the image exactly
            // when the signal is loudest -- or need a second pass over the
            // samples just written.
            float pending[2] { 0.0f, 0.0f };
            auto pendingPeak = 0.0f;

            for (int ch = 0; ch < active; ++ch)
            {
                auto& c = channels[(size_t) ch];
                const auto gated = channelData[ch][i] * gateGain;

                if (priming)
                    c.bands.prime (gated);

                float out;

                if (split)
                {
                    float mid = 0.0f, thru = 0.0f;
                    c.bands.process (gated, mid, thru);
                    out = (mid * compressorGain * autoMakeupLin + thru * thruMakeupLin) * outputLin;
                }
                else
                {
                    out = gated * compressorGain * autoMakeupLin * outputLin;
                }

                // The dip is taken off what is written, and the limiter still
                // judges the peak without it, so the limiter carries on as if
                // there were no dip and the dip only ever makes the output
                // quieter. Ahead of the limiter it let a ceiling that was
                // clamping relax, and a dip came out louder than either mode.
                pending[(size_t) ch] = dipping ? out * dipGain : out;
                pendingPeak = std::max (pendingPeak, std::abs (out));
            }

            // Last, and after OUTPUT: the ceiling is the module's, so OUTPUT
            // drives into it rather than sitting past it. A trim that could
            // push the output over the ceiling would make the ceiling a
            // suggestion.
            const auto limiterGain = limiter.isIdleFor (pendingPeak) ? 1.0f : limiter.process (pendingPeak);

            for (int ch = 0; ch < active; ++ch)
                channelData[ch][i] = pending[(size_t) ch] * limiterGain;
        }

        reportedReductionDb = blockMaxReduction;
    }

    /** The worst (largest) gain reduction seen in the block just processed, in
        dB, always >= 0. Read from the audio thread immediately after
        process() -- see core/product/ModuleEngine.h.

        This is the *compressor's* reduction and does not include the gate.
        The gate is a separate stage doing a different job, and folding its
        attenuation in would make the GR meter read 50 dB every time the singer
        stops, which says nothing about how hard the compressor is working. */
    float currentGainReductionDb() const noexcept { return reportedReductionDb; }


private:
    struct Channel
    {
        SidechainHighpass sidechain;
        BandSplit bands;
    };

    /** The one place standard mode's figures are substituted for the
        parameters. Everything downstream reads the result and cannot tell
        which it got, which is the point. */
    float lowThruFor (bool complex) const noexcept  { return complex ? params.lowThruHz  : kStandardLowThruHz; }
    /** True when running COMPLEX as `complex` would put a side of the split
        in or out, or move one, against the COMPLEX being run. */
    bool movesSplit (bool complex) const noexcept
    {
        return lowThruFor (complex) != lowThruFor (activeComplex) || highThruFor (complex) != highThruFor (activeComplex);
    }

    float highThruFor (bool complex) const noexcept { return complex ? params.highThruHz : kStandardHighThruHz; }

    void applyTimings() noexcept
    {
        // The COMPLEX the module is running, which during a switch's fade
        // down is still the one being left.
        const auto complex     = activeComplex;
        const auto attackMs    = complex ? params.attackMs    : kStandardAttackMs;
        const auto releaseMs   = complex ? params.releaseMs   : kStandardReleaseMs;
        const auto arcOn       = complex ? params.arc         : kStandardArc;
        const auto sidechainHz = complex ? params.sidechainHz : kStandardSidechainHz;
        const auto lowHz       = lowThruFor (complex);
        const auto highHz      = highThruFor (complex);

        attackPole = poleFor (attackMs, rate);
        release.setTimes (releaseMs, arcOn, rate);
        gate.setThreshold (params.gateDb);

        // The split decides for itself which of its sides are in, fades
        // them in and out, and clears a side once it is all the way out, so
        // that it comes back from silence. See BandSplit.
        for (auto& ch : channels)
        {
            ch.sidechain.setCutoff (sidechainHz);

            if (complexDip.isPending())
                ch.bands.primeFor (lowThruFor (params.complex), highThruFor (params.complex));
            else
                ch.bands.cancelPrime();

            ch.bands.setCutoffs (lowHz, highHz);
        }
    }

    double rate = 44100.0;
    int numActiveChannels = 2;

    std::array<Channel, 2> channels;
    Gate gate;
    Limiter limiter;
    ReleaseStage release;
    Smoother amountSmoother, outputSmoother;

    // What the smoothers' values were last turned into. NaN to begin with,
    // so the first sample after construction or prepare() works them out.
    static constexpr float kNotYet = std::numeric_limits<float>::quiet_NaN();
    float cachedAmount = kNotYet, cachedOutputDb = kNotYet;
    Curve cachedCurve {};
    float cachedMakeupLin = 1.0f, cachedThruMakeupLin = 1.0f, cachedOutputLin = 1.0f;
    bool  thruMakeupValid = false;

    float attackPole = 0.0f;
    float envelopeDb = 0.0f;

    /** Each half of a COMPLEX switch's dip, in ms: down, then up -- 28 ms and
        a sample in all. A straight line in gain, so on a 20 Hz tone the fade
        moves the largest sample step to 1.23x the signal's own at worst; at
        12 ms each way it was 1.48x. */
    static constexpr double kComplexDipMs = 14.0;

    bmo::dsp::Dip complexDip;
    bool activeComplex = false;   ///< the COMPLEX being run; params.complex is the one asked for
    bool heard = false;           ///< anything processed since prepare() or reset()

    Params params;
    float reportedReductionDb = 0.0f;
};

} // namespace bmo::vcomp
