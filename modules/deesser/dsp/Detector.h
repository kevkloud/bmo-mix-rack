#pragma once

#include <algorithm>
#include <cmath>

namespace bmo::deesser
{

/** Time constants use the tau convention: a time is how long a one-pole takes
    to cover 63.2 % of a step. BMO Opto, BMO DEQ and LTV Comp all read their
    knobs that way (`modules/opto/dsp/Detector.h`, `modules/deq/dsp/Dynamics.h`)
    and four dynamics modules in one suite reading the same number differently
    would be worse than any one convention. The 10-90 % rise is 2.2 tau; quote
    that if a number is ever wanted for a tooltip, never mix the two here. */
inline double onePoleCoeff (double timeMs, double sampleRate) noexcept
{
    const auto tau = std::max (timeMs, 1.0e-3) * 1.0e-3;
    return std::exp (-1.0 / (tau * std::max (sampleRate, 1.0)));
}

/** dB of a linear level, floored rather than allowed to reach -inf.

    -200 dB is far below every gate in this file, so a gated branch reads as
    "silent" rather than as NaN the first time it is multiplied by kappa. */
inline double levelDb (double linear) noexcept
{
    return 20.0 * std::log10 (std::max (linear, 1.0e-10));
}

//==============================================================================
/** A smooth, decoupled peak detector (Giannoulis, Massberg & Reiss 2012):

        y1[n] = max (x[n], ar y1[n-1] + (1 - ar) x[n])
        y[n]  = aa y[n-1] + (1 - aa) y1[n]

    Causal, no lookahead: the output at n has seen x[n] and nothing later,
    which is what lets this module report zero latency at every setting.

    Adapted from `modules/deq/dsp/Dynamics.h` rather than shared with it. The
    repo's rule is that each dynamics module owns its own envelope and release
    stages -- there is no shared compressor-detector library
    (docs/fet-comp/00-repo-conventions.md 2) -- and the two have already
    diverged here: this one is fed a power-summed level from two channels
    rather than one channel's rectified sample, because the image must not
    wander (10 section 3).

    In RMS mode the recursion runs on x^2 and the root is taken after, which is
    what the slow reference branch wants: a level estimate, not an event
    detector. */
class Envelope
{
public:
    void configure (double attackMs, double releaseMs, bool rmsMode, double sampleRate) noexcept
    {
        aa  = onePoleCoeff (attackMs, sampleRate);
        ar  = onePoleCoeff (releaseMs, sampleRate);
        rms = rmsMode;
    }

    void reset() noexcept { y1 = y = 0.0; }

    /** Feed one rectified level (>= 0); returns the envelope, linear. */
    double process (double level) noexcept
    {
        const auto x = rms ? level * level : level;
        y1 = std::max (x, ar * y1 + (1.0 - ar) * x);
        y  = aa * y + (1.0 - aa) * y1;
        return value();
    }

    double value() const noexcept { return rms ? std::sqrt (y) : y; }

    /** Denormals cost more than this test does. Called once a block, not once
        a sample: three envelopes times two flushes is six branches a block. */
    void flushTiny() noexcept
    {
        if (y1 < 1.0e-30) y1 = 0.0;
        if (y  < 1.0e-30) y  = 0.0;
    }

private:
    double aa = 0.0, ar = 0.0, y1 = 0.0, y = 0.0;
    bool rms = false;
};

//==============================================================================
/** The level-independent detector: how far the band stands above the signal,
    in dB, rather than how loud it is.

        P[n] = 20log10(B) - [ k*20log10(W) + (1-k)*20log10(S) ] - P_ref

    `B` is the band's fast peak envelope, `W` the reference's fast peak
    envelope on the same timing, and `S` the band's own slow RMS -- the
    programme's brightness memory, tau 500 ms.

    **Why this and not a level threshold.** Every term is the log of a level,
    so an input gain adds the same constant to B, W and S and cancels exactly
    at every kappa. The threshold therefore does not need re-riding when the
    take gets louder, which is the entire reason this detection style exists
    (docs/deesser/01-reference-behavior.md 2).

    **What kappa buys.** At 1 the band is compared with the whole signal right
    now, which is classic relative detection. At 0 it is compared with its own
    recent average, which is the guard against constantly-bright material and
    cymbal bleed: steady brightness raises S and stops the thing triggering,
    while a short burst still rides above it. v1 ships one fixed middle value
    and `DspCore` owns the number.

    **Both gates hard-zero the offset rather than clamping it.** A reference
    approaching silence sends prominence to infinity -- room tone would read as
    sibilance louder than any ess -- so below the reference gate the detector
    reports "nothing", not "a small amount". The band gate catches breaths the
    same way. They return `kSilent` rather than 0 dB, because 0 dB of
    prominence is a real reading that means "typical vocal balance". */
class Prominence
{
public:
    /** A prominence that means "no signal worth acting on", distinct from a
        prominence of zero. Far below any threshold the schema can reach
        (`thresh` floors at -24). */
    static constexpr double kSilent = -1000.0;

    struct Config
    {
        double attackMs      = 0.8;
        double releaseFastMs = 30.0;
        double slowRefMs     = 500.0;
        double kappa         = 0.6;
        double refGateDb     = -55.0;
        double bandGateDb    = -60.0;

        /** Where 0 dB of prominence sits, so THRESH reads 0 at a typical vocal
            balance rather than at an arbitrary number. CALIBRATE. */
        double referenceDb   = 0.0;

        /** How far below the kappa=1 reference the slow term may fall. Without
            it the brightness memory chases a fade-out downward and the
            detector grows steadily more eager as a phrase ends. */
        double slowFloorDb   = 20.0;
    };

    void prepare (const Config& c, double sampleRate) noexcept
    {
        config = c;

        // B and W share timing deliberately: the prominence is a *ratio* of
        // two envelopes, and two different time constants would make it read
        // the difference between the smoothers during every transient.
        band.configure (c.attackMs, c.releaseFastMs, false, sampleRate);
        reference.configure (c.attackMs, c.releaseFastMs, false, sampleRate);

        // The slow term's attack and release are both the memory: it is an
        // average, and an average with a fast attack is not one.
        slow.configure (c.slowRefMs, c.slowRefMs, true, sampleRate);

        reset();
    }

    void reset() noexcept
    {
        band.reset();
        reference.reset();
        slow.reset();
    }

    /** One sample of band level and reference level, both already power-summed
        across channels. Returns prominence in dB, or `kSilent`. */
    double process (double bandLevel, double referenceLevel) noexcept
    {
        const auto b = band.process (bandLevel);
        const auto w = reference.process (referenceLevel);
        const auto s = slow.process (bandLevel);

        const auto bDb = levelDb (b);
        const auto wDb = levelDb (w);

        if (wDb < config.refGateDb || bDb < config.bandGateDb)
            return kSilent;

        // The floor is measured from the fullband reference rather than from
        // an absolute level, so it travels with the material.
        const auto sDb = std::max (levelDb (s), wDb - config.slowFloorDb);

        const auto against = config.kappa * wDb + (1.0 - config.kappa) * sDb;
        return bDb - against - config.referenceDb;
    }

    void flushTiny() noexcept
    {
        band.flushTiny();
        reference.flushTiny();
        slow.flushTiny();
    }

    /** The band's fast envelope, linear. The panel's ribbon draws this: it is
        what the detector is looking at, so a highlight derived from it cannot
        disagree with what the module did. */
    double bandEnvelope() const noexcept { return band.value(); }

private:
    Config config;
    Envelope band, reference, slow;
};

//==============================================================================
/** The static curve, docs/deesser/10-dsp-spec.md 4:

        over  = P - T
        knee  = 0                       over <= -W/2
              = (over + W/2)^2 / (2W)   |over| < W/2
              = over                    over >= W/2
        offset = -min (Range, (1 - 1/R) * knee)

    Flat to T-3, a parabola through T at about -0.56 dB, then 0.75 dB of cut
    per dB of prominence, flattening hard at -Range. The 2-6 dB working point
    sits in the upper knee and the start of the linear region, which is the
    smoothest part of the curve and is not a coincidence.

    The knee is 6 dB and fixed: a hard corner snaps audibly on an event that
    only lasts 60 ms. The slope is 4:1 and fixed: this is an internal ratio,
    not a control, because RANGE already says how far the cut may go and a
    second "how hard" control would be two knobs for one idea. */
struct GainComputer
{
    double thresholdDb = 0.0;
    double kneeDb      = 6.0;
    double slope       = 4.0;
    double rangeDb     = 8.0;

    /** Reduction in dB, positive, 0..rangeDb. `Prominence::kSilent` returns 0
        by falling through the knee's first branch, but it is tested for
        explicitly so that a gate reads as a gate rather than as arithmetic
        that happens to come out right. */
    double reductionDb (double prominenceDb) const noexcept
    {
        if (prominenceDb <= Prominence::kSilent)
            return 0.0;

        const auto over = prominenceDb - thresholdDb;
        const auto w    = std::max (kneeDb, 0.0);

        double knee;
        if (w <= 0.0)              knee = std::max (over, 0.0);
        else if (over <= -0.5 * w) knee = 0.0;
        else if (over <  0.5 * w)  knee = (over + 0.5 * w) * (over + 0.5 * w) / (2.0 * w);
        else                       knee = over;

        const auto ratio = slope > 1.0 ? 1.0 - 1.0 / slope : 0.0;
        return std::min (std::max (rangeDb, 0.0), ratio * knee);
    }
};

//==============================================================================
/** The applied reduction: the gain computer's target with the module's
    ballistics on it, and the one place the three time-domain behaviours in
    10 section 6 live.

    **Attack is one pole at 0.8 ms**, so about 90 % of the reduction is applied
    2 ms into an onset. That is what lets the module refuse lookahead: the ess
    is 60-200 ms long and the first 2 ms of it are not what anyone hears as
    sibilance.

    **Release has two branches.** Fast, 30 ms, is the default and covers a
    normal ess tail -- the reduction is gone within about 90 ms, comparable to
    the event itself, which is why it cannot pump HF ambience. Slow, 120 ms,
    crossfades in only after the detector has been over threshold for longer
    than any phoneme (150 ms), which means a sustained bright passage rather
    than an ess; holding that one down steadily is better than chattering at
    it. The crossfade is on the *coefficient*, not on two parallel followers,
    so there is one state and nothing to re-synchronise.

    **Hold and hysteresis keep a cluster together.** An /s/-/t/ has a gap in
    the middle of it. Without the 5 ms hold the reduction starts releasing
    into the stop and has to attack again on the burst, which is audible as a
    flutter on one syllable. The 1.5 dB of threshold drop while engaged is the
    same idea seen from the detector's side: having decided this is an ess, be
    slower to decide it has ended. */
class Reduction
{
public:
    struct Config
    {
        double attackMs       = 0.8;
        double releaseFastMs  = 30.0;
        double releaseSlowMs  = 120.0;
        double slowEngageMs   = 150.0;
        double holdMs         = 5.0;
        double hysteresisDb   = 1.5;
    };

    void prepare (const Config& c, double sampleRate) noexcept
    {
        config = c;
        rate   = std::max (sampleRate, 1.0);

        attackCoeff      = onePoleCoeff (c.attackMs, rate);
        releaseFastCoeff = onePoleCoeff (c.releaseFastMs, rate);
        releaseSlowCoeff = onePoleCoeff (c.releaseSlowMs, rate);

        holdSamples   = (int) std::lround (c.holdMs * 1.0e-3 * rate);
        engageSamples = (int) std::lround (c.slowEngageMs * 1.0e-3 * rate);

        reset();
    }

    void reset() noexcept
    {
        applied = 0.0;
        overFor = 0;
        holdFor = 0;
        latchedBlend = 0.0;
    }

    /** How much the threshold drops while the module is engaged. The caller
        subtracts it before asking the gain computer, so the hysteresis is
        visible in the prominence the detector is judged on rather than hidden
        inside the follower. */
    double thresholdOffsetDb() const noexcept
    {
        return applied > kEngagedDb ? config.hysteresisDb : 0.0;
    }

    /** One sample. `target` is the gain computer's reduction, positive dB. */
    double process (double target) noexcept
    {
        const auto engaged = target > kEngagedDb;

        // Time continuously over threshold decides which release is in force.
        // It resets the moment the detector lets go, so a run of separate
        // esses never adds up into the slow branch.
        //
        // **The branch is latched while engaged, not read during the release.**
        // The release only happens once the detector has let go, and letting go
        // is what zeroes the counter -- so reading it at release time asked how
        // long the module had been engaged and was always answered "not at
        // all". Every release took the fast branch and the slow one was
        // unreachable by any signal. The test that caught it holds a target for
        // 400 ms and expects a slower release than a 120 ms one gets.
        if (engaged)
        {
            overFor = std::min (overFor + 1, 2 * engageSamples);
            latchedBlend = blendFor (overFor);
        }
        else
        {
            overFor = 0;
        }

        if (engaged)
            holdFor = holdSamples;
        else if (holdFor > 0)
            --holdFor;

        // Holding means not releasing. It does not mean freezing: a *deeper*
        // target during the hold still attacks, which is what makes a cluster
        // one event rather than one event and a shelf.
        const auto effective = (! engaged && holdFor > 0) ? std::max (target, applied)
                                                          : target;

        const auto coeff = effective > applied ? attackCoeff : releaseCoeff();

        applied = coeff * applied + (1.0 - coeff) * effective;

        if (applied < 1.0e-6)
            applied = 0.0;

        return applied;
    }

    /** The reduction actually being applied, which is what the meter and the
        ribbon both read -- the glided figure, never the target. A meter
        showing the target would read reduction the audio never got. */
    double appliedDb() const noexcept { return applied; }

private:
    /** Below this the module counts as idle. Not zero: the follower approaches
        its target asymptotically, and a threshold of exactly zero would leave
        the hysteresis latched on by a millionth of a dB. */
    static constexpr double kEngagedDb = 0.01;

    /** How far onto the slow branch a run of `samples` over threshold takes
        the release.

        **Fast until the engage time, then a crossfade -- not a crossfade that
        arrives at it.** 10 section 6 says the slow branch comes in "only after
        >150 ms continuously over threshold", and the difference is the whole
        point of the number: 150 ms is longer than any phoneme, so everything
        shorter is an ess and must release on the fast branch entire. Blending
        from the first sample instead put a 120 ms ess -- a long one, but still
        an ess -- four fifths of the way onto the slow release, which is
        exactly the HF pumping the fast branch exists to avoid.

        The crossfade then takes a second engage window, so the slow branch is
        fully in at 300 ms. That second number is not in the spec; it is the
        shortest ramp that cannot be mistaken for a step, and it is CALIBRATE
        with the rest. */
    double blendFor (int samples) const noexcept
    {
        if (engageSamples <= 0 || samples <= engageSamples)
            return 0.0;

        return std::min (1.0, (double) (samples - engageSamples) / (double) engageSamples);
    }

    double releaseCoeff() const noexcept
    {
        // Linear in the coefficient. Both ends are stable one-poles and
        // everything between them is one too, so the crossfade cannot ring
        // however abruptly the detector moves.
        return releaseFastCoeff + latchedBlend * (releaseSlowCoeff - releaseFastCoeff);
    }

    Config config;
    double rate = 48000.0;

    double attackCoeff = 0.0, releaseFastCoeff = 0.0, releaseSlowCoeff = 0.0;
    double applied = 0.0;

    int holdSamples = 0, engageSamples = 0;
    int overFor = 0, holdFor = 0;
    double latchedBlend = 0.0;
};

} // namespace bmo::deesser
