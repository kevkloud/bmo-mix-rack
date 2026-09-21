#pragma once

#include <algorithm>
#include <array>
#include <cmath>

namespace bmo::fetcomp
{

//==============================================================================
/** Which curve family the ratio buttons select, and the one that is not a
    ratio at all.

    `allButtons` is every button pushed in together: a different curve with a
    standing reduction of its own, a very high effective slope near threshold
    and a plateau beyond it. It is in this enum rather than in a separate flag
    because it is one of the five states the one control has. */
enum class Ratio { four = 0, eight, twelve, twenty, allButtons };

/** Two constant sets over one topology, gain-matched, identical latency. */
enum class Voicing { blue = 0, black };

//==============================================================================
// **Every constant docs/1176-comp/10-dsp-spec.md marks CALIBRATE is in this
// file, and every one of them is a first-pass value awaiting measurement and
// an ear.** Nothing here is fitted. The spec's own tables are where the
// numbers come from; where the spec gives only a direction ("above G_20",
// "1-2 dB", "1.5-3x") a value inside the stated range is picked here and said
// to be a pick, not a measurement.
//
// The reason they are gathered rather than scattered: none of them can be
// defended from first principles, so the honest thing is for a reader to be
// able to see the whole set of guesses at once and for an ear to be able to
// move them without hunting. The ones that matter most to the sound, from the
// derivation in 10 section 12, are the cell's `lambda`/`q`/`mu` (which set the
// whole character and the distance between the two voicings) and the
// threshold family below (which sets where the compressor starts, and which
// every drive figure in the spec rides on).
//
// What is NOT calibrate, and must not be moved without reopening the spec:
// `kCellConductance` (it fixes the 40 dB ceiling), the position-to-time laws
// in modules/fetcomp/params.h, and the latency table.
//==============================================================================

/** `k` in `g = 1/(1 + k*c)`. G_max = 20*log10(1 + k) = 40 dB at k = 99, and
    the 30 dB design target sits 10 dB below that. DERIVED, not calibrate:
    raising it also raises every `beta_R` for the same `G_R*T_R` and reopens
    the ratio fit (10 section 12). */
inline constexpr double kCellConductance = 99.0;

/** The control is clamped here so `k*c <= k`: the FET running out of
    conductance is a property of the divider rather than a limiter bolted on
    (10 section 5, "Ceiling"). */
inline constexpr double kControlCeiling = 1.0;

//==============================================================================
// The ratio family.
//
// `beta_R = k*G_R*T_R` is what the ratio switch actually selects, and 10
// section 5 fits it so the **average** delivered slope over a 10 dB window
// above threshold equals the nameplate. The delivered ratio is therefore
// depth-dependent by construction -- 4:1 delivers about 3.0 at 10 dB of
// reduction and about 2.1 at 30 -- and that sag is the divider law, not an
// error. 10 section 12 defends it; do not "fix" it here.
//
// CALIBRATE: the four figures are derived from a fitting window nobody has
// measured a real unit over (10 section 13.1 is the risk).
//==============================================================================

inline constexpr std::array<double, 4> kRatioBeta { 4.11, 11.23, 18.60, 33.51 };

/** Input-referred threshold per ratio, dBFS.

    Only the 20:1 figure is documented -- the manual's -24 dB +/- 2 dB -- and
    even that is a dBu number being read as a dBFS one, which is the
    alignment 10 section 12 calls out as unmeasured and as the thing every
    drive figure rides on. The other three are the spec's stated *direction*
    (T_4 > T_8 > T_12 > T_20, offsets about +8/+5/+2 dB) turned into numbers.
    **CALIBRATE, all four.** */
inline constexpr std::array<double, 4> kRatioThresholdDb { -16.0, -19.0, -22.0, -24.0 };

inline double ratioThresholdLinear (int index) noexcept
{
    return std::pow (10.0, kRatioThresholdDb[(size_t) std::clamp (index, 0, 3)] * 0.05);
}

/** Fixing `beta_R` and `T_R` fixes the sidechain gain: `G_R = beta_R/(k*T_R)`.
    There is no third degree of freedom, which is worth knowing before
    reaching for one. */
inline double ratioSidechainGain (int index) noexcept
{
    const auto i = (size_t) std::clamp (index, 0, 3);
    return kRatioBeta[i] / (kCellConductance * ratioThresholdLinear ((int) i));
}

//==============================================================================
// All-buttons-in: a bias state, not a fifth ratio.
//
// 10 section 5 gives it four traits and says every number is CALIBRATE, since
// A2 pins none of them. What is picked here, and why:
//
//  - `G_ab` is the four ratio resistors in parallel, so the four sidechain
//    gains **add**. That is the one figure with a mechanism behind it rather
//    than a guess, and it lands at about 1.7x G_20 -- "above G_20", as the
//    spec requires. The corner ratio that implies is about 57:1, which is the
//    "more abrupt catch" the paper describes.
//  - the standing bias parks the cell in its nonlinear region at silence. The
//    spec says 1-2 dB; 1.5 is the middle of it.
//  - the plateau is a **fitted collapse** of sidechain gain above a
//    breakpoint. It does not fall out of the law -- (1 + 2u + beta)/(1 + u) is
//    monotone and never below 2 -- so it is put in by hand, as the spec
//    instructs. Expressed against the demand the uncollapsed network would
//    make, `d -> d0 / (1 + (d0/d_p)^gamma)`, which at gamma = 1 is an exact
//    plateau in the control and above it lets the reduction fall away again.
//    That is the "flat-topped, non-monotonic" shape, in one constant each for
//    where it happens and how hard.
//  - the lag is the documented delay before reduction engages. It is in the
//    control path, not the rectifier, so it also damps the collapse -- which
//    is what keeps a non-monotone sidechain from ringing sample to sample.
//==============================================================================

/** The four ratio networks in parallel: conductances add. CALIBRATE in the
    sense that the mechanism is documented and the arithmetic is an
    assumption about what "in parallel" does to this sidechain. */
inline double allButtonsSidechainGain() noexcept
{
    return ratioSidechainGain (0) + ratioSidechainGain (1)
         + ratioSidechainGain (2) + ratioSidechainGain (3);
}

/** All-buttons keeps the 20:1 threshold bias. **CALIBRATE** -- the spec says
    the bias shifts throughout the sidechain and gives no figure, so the least
    invented choice is to move only the gain and leave the anchor alone. */
inline constexpr int kAllButtonsThresholdIndex = 3;

/** Standing gain reduction at silence, dB. The spec's range is 1-2 dB;
    this is the middle. **CALIBRATE.** */
inline constexpr double kAllButtonsStandingGrDb = 1.5;

inline double allButtonsStandingControl() noexcept
{
    return (std::pow (10.0, kAllButtonsStandingGrDb * 0.05) - 1.0) / kCellConductance;
}

/** Where the flat top sits, as the reduction held there -- a readable depth
    rather than an opaque level. **CALIBRATE.** */
inline constexpr double kAllButtonsPlateauGrDb = 18.0;

/** How hard the sidechain gain collapses. 1.0 is an exact plateau; above 1
    the reduction turns over and falls away as the input keeps rising, which
    is the non-monotonic shape the paper describes. **CALIBRATE.** */
inline constexpr double kAllButtonsPlateauExponent = 1.25;

/** The collapse is keyed on a **level**, so it needs an envelope of the
    demand rather than the demand itself, and this is that envelope's decay.
    Instant to rise, 25 ms to fall: long enough to hold across a 50 Hz cycle,
    short enough to follow a phrase. **CALIBRATE.**

    It is not optional and it is not cosmetic. Keyed on the *instantaneous*
    rectified sample the collapse is defeated outright: the gain is read one
    sample behind the demand it is applied to, so on the rising flank of a
    rectified sine it is read where the signal is small -- barely collapsed --
    and applied where the signal is large. The release stage's `max` latches
    exactly that sample. Measured on AURORA at 0 dBFS into all-buttons: a flat
    top specified at 18 dB delivered 19.8 dB and still climbing at +20 dBFS,
    with the curve wobbling by a decibel either way as the sampling phase
    moved, which is what gave it away. */
inline constexpr double kAllButtonsPlateauEnvelopeMs = 25.0;

/** The demand at which the collapse is centred.

    Not the plateau depth itself: `d -> d0/(1 + (d0/d_p)^gamma)` peaks at
    `d_p * X*(gamma-1)/gamma` for `X* = (1/(gamma-1))^(1/gamma)`, so the
    breakpoint has to be divided back by that factor for
    `kAllButtonsPlateauGrDb` to name the depth it actually flattens at.
    Derived rather than written down, so moving either constant keeps the
    other honest. */
inline double allButtonsPlateauDemand() noexcept
{
    const auto target = (std::pow (10.0, kAllButtonsPlateauGrDb * 0.05) - 1.0) / kCellConductance
                          - allButtonsStandingControl();

    const auto g = kAllButtonsPlateauExponent;
    const auto peak = std::pow (1.0 / (g - 1.0), 1.0 / g) * (g - 1.0) / g;

    return std::max (target, 1.0e-9) / std::max (peak, 1.0e-6);
}

/** The one-pole lag in the control path. The spec's range is 1-5 ms.
    **CALIBRATE.** */
inline constexpr double kAllButtonsLagMs = 2.5;

/** Release is scaled 1.5-3x in this state; this is the low end of it, so the
    mode does not also become a different release knob. **CALIBRATE.** */
inline constexpr double kAllButtonsReleaseScale = 2.0;

/** Q-bias cancellation is reduced here, so the even-order content the network
    normally cancels comes back. A multiplier on the voicing's own `q`.
    **CALIBRATE.** */
inline constexpr double kAllButtonsQScale = 0.35;

//==============================================================================
// Timing.
//==============================================================================

/** A2's attack figure is a **100 % recovery** one, and a one-pole never
    arrives, so `tau_att = t_att(p)/N`. N = 5 is 99.3 %; 4.6 (99 %) is the
    common alternative. **CALIBRATE**, and it moves every attack time
    together. */
inline constexpr double kAttackRecoveryFactor = 5.0;

/** The full-wave rectifier's own pole, at the effective rate. Fast enough to
    be nearly a wire at base rate and to matter only where the chain is
    oversampled -- which is the point: where oversampling is on, the control's
    band limit runs at the oversampled rate so its corner can sit above 20 kHz
    without folding (10 section 12). **CALIBRATE.** */
inline constexpr double kRectifierMicroseconds = 3.0;

/** The program-dependent release, acting on the control rather than on dB.
    The slow branch is program-dependent through its **charge** time, not its
    release: one transient barely moves it, a sustained passage charges it and
    it then owns the recovery. With equal charge times `max` would always pick
    the slower branch -- the trap recorded in modules/vcomp/dsp/Detector.h.
    **CALIBRATE**, both. */
inline constexpr double kReleaseChargeScale = 1.5;
inline constexpr double kReleaseSlowScale   = 10.0;

/** The depth the release detents are honest at. Smoothing happens in the
    control domain, where the hardware's gate RC is, so an exponential decay
    of `c` has a 63 % point *in dB* that depends on how deep it started --
    which is A2's "hangs, then lets go" for free, and the reason the table has
    to name a depth at all. **CALIBRATE**, and 10 section 13.5 says it moves
    before ship if the detents read wrong at working depths. Tests read it;
    the DSP does not. */
inline constexpr double kReleaseReferenceGrDb = 10.0;

/** What the knob's printed time has to be divided by to become a time
    constant, and the reason the depth above has to be named at all.

    A2's release figure is a 63 % recovery one, and the smoothing is in the
    **control** domain, so `GR(t) = 20*log10(1 + k*c0*exp(-t/tau))` rather than
    an exponential in dB. That curve is 63 % of the way back down, *in dB*, at
    about 1.40 time constants from 10 dB -- and at a different multiple from
    any other depth, which is A2's "hangs, then lets go" arriving for free
    rather than being drawn in.

    So the detent table is honest at one stated depth and nowhere else, and
    this is the arithmetic that makes it honest there. Derived from
    `kReleaseReferenceGrDb` rather than written out, so moving the reference
    moves this with it -- 10 section 13.5 says the reference is a decision
    that can still move before ship, since the table is permanent. */
inline double releaseTimeConstantScale() noexcept
{
    const auto from = std::pow (10.0, kReleaseReferenceGrDb * 0.05) - 1.0;
    const auto to   = std::pow (10.0, 0.37 * kReleaseReferenceGrDb * 0.05) - 1.0;

    return std::log (from / to);
}

/** Below this the control is simply zero. Stops a one-pole decaying toward 0
    from spending the rest of the session in denormals; the same guard
    modules/vcomp/dsp/Detector.h keeps, in the control's units. */
inline constexpr double kControlFloor = 1.0e-12;

//==============================================================================
// Smoothing and crossfades (10 section 10).
//==============================================================================

inline constexpr double kGainSmoothingMs = 20.0;

/** Ratio is a switch, so its constants are walked across rather than stepped.
    The control state itself is **preserved** -- what crossfades is the
    sidechain the loop is looking through. */
inline constexpr double kRatioCrossfadeMs = 5.0;

/** Voicing crossfades the two static shapers' *outputs*, 5-10 ms. Not their
    coefficients: ADAA state has to be rebuilt on any coefficient change, and
    rebuilding it every sample through a ramp would smear the residual it
    exists to anti-alias (10 section 8). */
inline constexpr double kVoicingCrossfadeMs = 8.0;

//==============================================================================
// The FET cell's own distortion, and the two voicings.
//
// `g(v) = 1 / (1 + k*c*(1 + lambda*(1-q)*v + mu*v^2))`, with `v` the **signed**
// cell output of the previous sample -- signed, because that is what makes
// the `lambda` term even-order (2nd-dominant) and the `mu` term odd. Both
// multiply `k*c`, so distortion vanishes as GR -> 0 (clean when not
// compressing) and grows steeply with depth: between 10 and 30 dB of
// reduction `k*c` grows about 11x.
//
// **Every amount here is CALIBRATE and fitted by nothing.** A2 supports the
// mechanisms -- the Q-bias network equalising the FET's two half-cycles, the
// low-noise change around Rev C that held the gain-reduction FET in its
// linear range -- and gives no revision-specific distortion figure at all.
// The one thing the amounts must respect is that the two voicings are
// described as differing in **distortion at depth**, so the gap widens with
// reduction rather than sitting flat; `depthGrowth` is what makes Blue's
// coefficients grow faster than Black's as well as starting larger.
//
// The starting point for the sizes: 10 section 11 wants Black under 0.5 % THD
// at 10 dB GR and under 0.05 % at none, with the 2nd about 10 dB above the
// 3rd, and wants Blue above Black at every depth. These were chosen to land
// there on paper and then moved once each after `measure_fetcomp thd` was
// run on AURORA; see testing-notes/fetcomp-dsp-2026-09-20.md. That is a
// measurement of *this code*, not of a unit, so they are still first-pass.
//==============================================================================

/** One voicing, as constants. Nothing else differs: same `k`, same `beta_R`
    family, same detents, same solve, same latency. */
struct VoicingConstants
{
    //== the cell =============================================================
    double lambda      = 0.075;  ///< even-order, 2nd-dominant
    double qBias       = 0.55;   ///< Q-bias cancellation depth, 0..1
    double mu          = 0.026;  ///< odd content at deep reduction
    double depthGrowth = 0.15;   ///< how much faster the cell dirties with depth

    //== the static blocks ====================================================
    double hfPoleHz    = 45000.0; ///< input transformer leakage inductance
    double lfPoleHz    = 10.0;    ///< the output transformer's LF corner

    /** LF core saturation: the band below `lfSatCornerHz` is shaped and the
        residual re-added, so saturation stays LF-only and nothing above the
        corner is touched. Blue's core gives up earlier -- a lower breakpoint
        is a larger drive into the same curve. */
    double lfSatCornerHz = 120.0;
    double lfSatDrive    = 0.06;
    double lfSatBias     = 0.16;
    double lfSatAmount   = 0.35;

    /** The input amplifier. Blue is the earlier revision, before the
        low-noise input circuitry, so its stage nonlinearity starts sooner. */
    double inputDrive  = 0.025;
    double inputBias   = 0.18;
    double inputAmount = 0.40;

    /** The output amplifier: class-A in **both** voicings, 2nd-dominant,
        lower in amount on Black. The push-pull revisions are out of scope --
        neither voicing covers them (10 section 1). */
    double outputDrive  = 0.030;
    double outputBias   = 0.20;
    double outputAmount = 0.45;
};

/** Black: the later low-noise class-A revisions. The default, per
    modules/fetcomp/params.h. */
inline constexpr VoicingConstants kBlack
{
    0.075, 0.55, 0.026, 0.15,
    45000.0, 10.0,
    120.0, 0.06, 0.16, 0.35,
    0.025, 0.18, 0.40,
    0.030, 0.20, 0.45
};

/** Blue: the earliest revisions. Less bias refinement, more colour, and the
    gap to Black widens with depth rather than sitting flat. */
inline constexpr VoicingConstants kBlue
{
    0.150, 0.25, 0.052, 0.50,
    38000.0, 10.0,
    120.0, 0.13, 0.22, 0.55,
    0.055, 0.24, 0.55,
    0.048, 0.26, 0.55
};

inline const VoicingConstants& constantsFor (Voicing v) noexcept
{
    return v == Voicing::blue ? kBlue : kBlack;
}

/** Linear interpolation of a whole constant set, for the crossfade. The two
    sets are numbers over one topology, so walking between them is meaningful;
    what it must never do is walk a *shaper's* coefficients while its ADAA
    state is running, which is why the crossfade in DspCore blends outputs
    instead and uses this only for the cell, whose factor carries no state. */
inline VoicingConstants blendCell (const VoicingConstants& a,
                                   const VoicingConstants& b, double t) noexcept
{
    VoicingConstants out = b;
    out.lambda      = a.lambda      + t * (b.lambda      - a.lambda);
    out.qBias       = a.qBias       + t * (b.qBias       - a.qBias);
    out.mu          = a.mu          + t * (b.mu          - a.mu);
    out.depthGrowth = a.depthGrowth + t * (b.depthGrowth - a.depthGrowth);
    return out;
}

} // namespace bmo::fetcomp
