#pragma once

#include "modules/fetcomp/dsp/Calibration.h"
#include "modules/fetcomp/dsp/FetCell.h"
#include "modules/fetcomp/params.h"

#include <algorithm>
#include <cmath>

namespace bmo::fetcomp
{

/** The knob positions, in the units the coefficients actually want.

    Derived here rather than stored, so nothing can hold a stale time after a
    position moves, and so the laws in modules/fetcomp/params.h stay the only
    definition of what a position means. */
inline double attackSecondsFor (float position) noexcept
{
    return (double) attackMicrosecondsFor (position) * 1.0e-6;
}

inline double releaseSecondsFor (float position) noexcept
{
    return (double) releaseMillisecondsFor (position) * 1.0e-3;
}

/** `exp(-1/(tau*fs))` -- the fraction of a one-pole's state that **survives**
    a sample, not the fraction of the step it takes. The published form of the
    decoupled detector is written in survivors and so is everything below;
    mixing the two conventions up is a bug that sounds like a working
    compressor with the wrong numbers on its knobs. Same convention, same
    name, as `poleFor` in modules/vcomp/dsp/Detector.h. */
inline double poleFor (double tauSeconds, double rate) noexcept
{
    if (! (tauSeconds > 0.0))
        return 0.0;

    return std::exp (-1.0 / (std::max (rate, 1.0) * tauSeconds));
}

/** The attack one-pole's **step**, which is what the implicit solve wants.

    A2's attack figure is a 100 % recovery one and a one-pole never arrives,
    so the time constant is the published figure divided by
    `kAttackRecoveryFactor`. Sub-sample time constants are not a limit: 4
    microseconds is 0.18 samples at 44.1 kHz, but the step still separates all
    seven detents (0.12 ... 0.995 at 48 kHz) and the solve is exact at 1. */
inline double attackStepFor (float position, double rate) noexcept
{
    const auto tau = attackSecondsFor (position) / kAttackRecoveryFactor;
    return std::clamp (1.0 - poleFor (tau, rate), 0.0, 1.0);
}

//==============================================================================
/** One ratio button's worth of sidechain, plus the three extras all-buttons
    needs. Held as plain numbers so a ratio change can be walked across in 5 ms
    rather than stepped -- the control state itself is **preserved** through
    the change, so the compression does not jump; what moves is the network the
    loop is looking through. */
struct Sidechain
{
    double gain            = 1.0;   ///< G_R
    double threshold       = 0.1;   ///< T_R, linear, input-referred
    double standingControl = 0.0;   ///< c0: the gate parked off zero at silence
    double plateau         = 0.0;   ///< 0 for a ratio, 1 for all-buttons
    double lagSeconds      = 0.0;   ///< the documented transient lag
    double releaseScale    = 1.0;
    double qScale          = 1.0;   ///< how much Q-bias cancellation survives
};

inline Sidechain sidechainFor (Ratio r) noexcept
{
    if (r != Ratio::allButtons)
    {
        const auto i = (int) r;
        return { ratioSidechainGain (i), ratioThresholdLinear (i), 0.0, 0.0, 0.0, 1.0, 1.0 };
    }

    // All-buttons-in is a bias state, not a fifth ratio: the four ratio
    // resistors end up in parallel, so the sidechain gains add and the corner
    // ratio goes far above 20:1; the gate sits off zero even at silence; the
    // recovery is scaled; and the Q-bias network stops cancelling as much of
    // the even order. The plateau is the one trait the law does not give --
    // see `collapsedGain` below.
    return { allButtonsSidechainGain(),
             ratioThresholdLinear (kAllButtonsThresholdIndex),
             allButtonsStandingControl(),
             1.0,
             kAllButtonsLagMs * 0.001,
             kAllButtonsReleaseScale,
             kAllButtonsQScale };
}

inline Sidechain blend (const Sidechain& a, const Sidechain& b, double t) noexcept
{
    const auto lerp = [t] (double x, double y) { return x + t * (y - x); };

    return { lerp (a.gain, b.gain),
             lerp (a.threshold, b.threshold),
             lerp (a.standingControl, b.standingControl),
             lerp (a.plateau, b.plateau),
             lerp (a.lagSeconds, b.lagSeconds),
             lerp (a.releaseScale, b.releaseScale),
             lerp (a.qScale, b.qScale) };
}

/** All-buttons' plateau, and the only thing in this module that is put in by
    hand rather than derived.

    `(1 + 2u + beta)/(1 + u)` is monotone and never below 2, so the flat top
    the published investigation describes -- "a plateau rather than the gentler
    slope of, e.g., 4:1", near-flat-topped, with reduction that can fall away
    again -- cannot come out of the divider law. 10 section 5 therefore
    specifies a fitted collapse of sidechain gain above a breakpoint, reading
    the previous sample's rectified output so the quadratic is unchanged.

    Written against the demand the uncollapsed network would make rather than
    against a level in volts, because that is the quantity with a readable
    unit:

        d -> d0 / (1 + strength * (d0/d_p)^gamma)

    At `gamma = 1` that is an exact plateau in the control, so the gain
    reduction stops climbing; above 1 it turns over and reduction falls as the
    input keeps rising. `d_p` is derived in Calibration.h from the depth the
    flat top is meant to sit at. `strength` is the crossfade: zero for the
    four ratios, so they are untouched by all of this.

    **`envelopeDemand` is an envelope and must not be an instantaneous
    sample** -- `kAllButtonsPlateauEnvelopeMs` records what happens when it
    is. "L" in the spec's `G_ab(L)` is a level. */
inline double collapsedGain (const Sidechain& s, double envelopeDemand) noexcept
{
    if (s.plateau <= 0.0 || ! (envelopeDemand > 0.0))
        return s.gain;

    const auto excess = std::pow (envelopeDemand / allButtonsPlateauDemand(),
                                  kAllButtonsPlateauExponent);

    return s.gain / (1.0 + s.plateau * excess);
}

//==============================================================================
/** Instant up, one-pole down: the level the plateau collapse is keyed on.

    A peak follower rather than a one-pole average because the demand it
    watches is a full-wave rectified thing that spends most of each cycle near
    zero, and what the collapse has to know is how hard the programme is
    hitting, not what the waveform is doing between peaks. */
class PeakFollower
{
public:
    void setTime (double tauSeconds, double rate) noexcept
    {
        pole = poleFor (tauSeconds, rate);
    }

    void reset() noexcept { state = 0.0; }

    double process (double v) noexcept
    {
        state = std::max (v, pole * state + (1.0 - pole) * v);

        if (state < kControlFloor)
            state = 0.0;

        return state;
    }

private:
    double pole = 0.0, state = 0.0;
};

//==============================================================================
/** The release half of the detector, acting on the **control** rather than on
    dB, in the smooth decoupled form (Giannoulis, Massberg and Reiss, JAES
    60(6), 2012) that modules/vcomp/dsp/Detector.h already uses.

    The `max` against the demand makes the attack instant *here*, so the
    attack smoothing inside the divider is the only thing shaping it -- which
    is what stops the attack time drifting with how far over threshold the
    signal is.

    **What makes the slow branch programme-dependent is its charge time, not
    its release.** Two release branches with different times combined with
    `max` is just the slower of the two at every sample, for any input: a
    slower one-pole fed the same signal is never below a faster one. There is
    no programme dependence in that at all, and it is a trap recorded in
    vcomp. Charging the slow branch over 1.5x the release means one transient
    barely moves it and a sustained passage charges it most of the way, after
    which it is the branch that owns the recovery.

    Because the smoothing is in the control domain, where the hardware's gate
    RC is, an exponential decay of `c` gives
    `GR(t) = 20*log10(1 + k*c0*exp(-t/tau))` -- a recovery that starts slow and
    accelerates, and whose 63 % point *in dB* depends on how deep it started.
    That is A2's "hangs, then lets go" for free, and it is why the detent table
    has to be calibrated at a stated depth (`kReleaseReferenceGrDb`). */
class ReleaseStage
{
public:
    void setTime (double releaseSeconds, double scale, double rate) noexcept
    {
        // The printed time is a 63 %-in-dB figure at the reference depth; the
        // time constant that delivers it is shorter. See
        // `releaseTimeConstantScale` for the whole of why.
        const auto tau = std::max (releaseSeconds * scale / releaseTimeConstantScale(), 1.0e-6);

        fastPole   = poleFor (tau, rate);
        chargePole = poleFor (tau * kReleaseChargeScale, rate);
        slowPole   = poleFor (tau * kReleaseSlowScale, rate);
    }

    void reset() noexcept { fast = slow = 0.0; }

    /** `demand` is what the sidechain is asking for this sample, `attacked` is
        what the implicit solve made of it, and `previous` is the control that
        is currently applied.

        **The branch on `demand` against `previous` is the whole point**, and
        it is 10 section 4's instruction: solve with the attack coefficient,
        and if the demand is below the current control discard that answer and
        take the release update instead. Feeding the release branches the
        *attacked* value rather than the demand looks equivalent and is not --
        the attack one-pole's own decay then multiplies into the release rate,
        and the release knob comes out `1/alpha` times too slow. Measured on
        AURORA before this branch existed: position 4 released in 418 ms
        against a printed 234, and the error tracked the attack knob. */
    double tick (double demand, double attacked, double previous) noexcept
    {
        // The slow branch runs every sample and branches on **its own** value,
        // which is vcomp's form and is not interchangeable with branching on
        // the applied control. Under a sustained tone with a slow release the
        // control sits at the ripple's peak, so a branch against the control
        // is true only at the very top of each cycle and the slow branch
        // charges about seventy times slower than its constant says. Measured
        // on AURORA: written that way, four seconds of sustained 10 dB GR
        // recovered in exactly the same 1100 ms as a 20 ms burst at position 1
        // -- no programme dependence at all, at the setting where it matters
        // most -- while the fast detents still showed it. That asymmetry is
        // what gave it away.
        const auto pole = demand > slow ? chargePole : slowPole;
        slow = pole * slow + (1.0 - pole) * demand;

        if (slow < kControlFloor)
            slow = 0.0;

        if (demand >= previous)
        {
            // Rising: the solve owns the shape. The slow branch is only ever
            // charging here, and its **charge** time is where the programme
            // dependence lives -- one transient barely moves it, a sustained
            // passage charges it and it then owns the recovery.
            fast = attacked;
            return std::max (attacked, slow);
        }

        fast = std::max (demand, fastPole * fast + (1.0 - fastPole) * demand);

        if (fast < kControlFloor)
            fast = 0.0;

        return std::max (fast, slow);
    }

private:
    double fastPole = 0.0, chargePole = 0.0, slowPole = 0.0;
    double fast = 0.0, slow = 0.0;
};

//==============================================================================
/** The all-buttons lag: one pole in the **control** path, not in the
    rectifier.

    The distinction matters twice over. It is the documented delay before
    reduction engages -- the thing that lets peaks approach full scale despite
    heavy average reduction -- and, because the plateau above makes the
    sidechain gain non-monotone in level, it is also what stops a non-monotone
    loop from arguing with itself sample to sample. A rectifier pole would do
    neither: it would only blur what the detector hears.

    A coefficient of zero is a wire, which is what the four ratios get. */
class ControlLag
{
public:
    void setTime (double tauSeconds, double rate) noexcept
    {
        pole = tauSeconds > 0.0 ? poleFor (tauSeconds, rate) : 0.0;
    }

    void reset() noexcept { state = 0.0; }

    double process (double v) noexcept
    {
        if (! (pole > 0.0))
        {
            state = v;
            return v;
        }

        state = pole * state + (1.0 - pole) * v;

        if (state < kControlFloor)
            state = 0.0;

        return state;
    }

private:
    double pole = 0.0, state = 0.0;
};

} // namespace bmo::fetcomp
