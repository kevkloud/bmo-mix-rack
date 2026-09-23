#pragma once

#include "modules/fetcomp/dsp/Calibration.h"

#include <algorithm>
#include <cmath>

namespace bmo::fetcomp
{

//==============================================================================
// The FET divider law, and the per-sample implicit solve.
//
// The cell is a voltage-variable shunt: conductance rises with the gate
// control `c >= 0`, so its gain is the divider law `g = 1/(1 + k*c)` and its
// ceiling is `20*log10(1 + k)` -- 40 dB at k = 99. The sidechain is **linear**,
// as the hardware's is: it rectifies the cell output and drives the gate
// through the ratio network, so the demand is `d = G_R * max(0, |y| - T_R)`.
// That is a true feedback loop (modules/vcomp is feedforward by design, and
// records why).
//
// The consequence, which is the whole character of this compressor and is not
// a defect: with the control settled, the local compression ratio is
// `(1 + 2u + beta_R)/(1 + u)` where `u` and the reduction are the same
// quantity. Each ratio setting therefore delivers its nameplate at a
// different depth and all of them decay toward 2:1 as they are driven -- 4:1
// delivers about 3.0 at 10 dB of reduction, 20:1 about 12.3. 10 section 12
// defends that at length against the alternative (loop gains high enough to
// hold the nominal ratio at 20 dB would make every setting a hard-knee
// limiter at the top of its knee, and make the four indistinguishable near
// threshold). Do not "fix" the sag.
//
// The alternative topology is not deleted either: 10 section 5a keeps a
// dB-domain loop over core/dsp/GainComputer.h with exact nominal slopes, and
// says plainly what it costs -- it buys those slopes by modelling a sidechain
// the unit does not have, since the hardware's ratio network is a resistive
// divider. It is the fallback if the sag proves unusable by ear.
//==============================================================================

/** The `v`-dependent part of the cell's conductance:

        F(v) = 1 + lambda*(1-q)*v + mu*v^2

    `v` is the **signed** cell output of the previous sample, and the sign is
    load-bearing: `r_ds` depends on `v_ds`, so the linear term is what makes
    the two half-cycles unequal and produces the 2nd harmonic the unit is
    known for, while the square term is symmetric and is the odd content that
    appears at deep reduction. Take the magnitude here instead and the even
    order vanishes entirely.

    It multiplies `k*c`, so it contributes **nothing** at no reduction and
    grows with depth -- clean when it is not compressing, and dirtier the
    harder it works, which is the behaviour and not a side effect.

    Evaluated from the previous sample so the quadratic below is unchanged:
    ADAA cannot help here (its antiderivative assumes a curve fixed between
    samples and this one moves every sample with the control), so the cell's
    own aliasing is the oversampler's business. */
inline double cellDistortionFactor (double signedPreviousOutput,
                                    double normalisedDepth,
                                    const VoicingConstants& c) noexcept
{
    const auto growth = 1.0 + c.depthGrowth * normalisedDepth;
    const auto even   = c.lambda * (1.0 - c.qBias) * growth;
    const auto odd    = c.mu * growth;
    const auto v      = signedPreviousOutput;

    // Positive by construction for every coefficient set in Calibration.h
    // (even^2 < 4*odd), but clamped anyway: a negative factor would be a
    // negative conductance, and the divider would hand back a gain above one
    // or a division by zero rather than a compressor.
    return std::max (0.2, 1.0 + even * v + odd * v * v);
}

/** How far into its range the cell is, 0 at no reduction and approaching 1 at
    the ceiling. The distortion coefficients grow along it, which is what makes
    the gap between the voicings widen with depth rather than sit flat. */
inline double normalisedDepthFor (double control) noexcept
{
    const auto kc = kCellConductance * control;
    return kc / (1.0 + kc);
}

/** The divider itself. Positive = gain taken away, the repo's convention
    (core/dsp/ModuleDsp.h), and it keeps telling the truth past the 24 dB the
    meter pins at -- the clamp is a drawing limit, not a measurement one. */
inline float reductionDbFor (double control) noexcept
{
    return (float) (20.0 * std::log10 (1.0 + kCellConductance * control));
}

//==============================================================================
/** What one sample of the loop needs to know, gathered so the solve reads as
    the algebra in 10 section 4 rather than as eleven arguments. */
struct CellState
{
    double control   = 0.0;   ///< the loop's part of the gate control, previous sample
    double bias      = 0.0;   ///< the standing part (all-buttons), not smoothed
    double rectifier = 0.0;   ///< the detector's rectified cell output, previous sample
    double factor    = 1.0;   ///< F(v) from the previous sample
};

struct SidechainState
{
    double gain      = 1.0;   ///< G_R
    double threshold = 0.0;   ///< T_R, linear
    double attack    = 1.0;   ///< alpha, the attack one-pole's step
    double rectPole  = 1.0;   ///< the rectifier's step
};

//==============================================================================
/** Solve the loop for this sample's cell output magnitude.

    Attack smoothing sits **inside** the divider, or the loop is a unit-delay
    loop and oscillates. With one-pole step `alpha`, previous control `c-`,
    and `v = m/(1 + k*c)`, substituting the control update gives a quadratic
    in `v`:

        B*v^2 + A*v - m = 0
            B = k*alpha*G*alpha_r
            A = 1 + k*(c0 + (1-alpha)*c-) + k*alpha*G*((1-alpha_r)*r- - T)

    The rectifier pole is inside the substitution rather than bolted on
    afterwards, which is why `alpha_r` appears in both terms; at `alpha_r = 1`
    this reduces exactly to the spec's form. Keeping it inside is what lets the
    control's band limit run at the oversampled rate, so its corner can sit
    above 20 kHz without folding (10 section 12).

    Exactly one root is positive -- the product of the roots is `-m/B < 0` --
    and it is the stable one: it reduces to `v = m/A` as `alpha -> 0` and to
    the static curve at `alpha = 1`. Which closed form to use is the standard
    conditioning rule, and it is not optional here: at 20:1 and 30 dB of
    reduction `A` is about -32 while `4Bm` is about 8.1e3, so the `A < 0`
    branch adds two positive numbers and nothing cancels, while the other
    corner -- `A > 0` with `4Bm << A^2` -- is exactly what `2m/(A + sqrt)`
    exists for.

    **Double throughout**, and that is 10 section 12's instruction rather than
    caution: `A^2` reaches about 1e4 under all-buttons bias, where float's
    24-bit mantissa drops terms that matter near threshold.

    No iteration and no unit delay. `alpha = 1` is legal, which is what makes
    the 20 microsecond attack work at base rate: there the solve collapses to
    the static curve and delivers the correct steady-state gain on the **first
    sample**, with no overshoot. */
inline double solveCellOutput (double m, const CellState& s, const SidechainState& sc) noexcept
{
    const auto k = kCellConductance * s.factor;
    const auto held = 1.0 + k * (s.bias + (1.0 - sc.attack) * s.control);

    // Below threshold the sidechain is simply not asking for anything, and the
    // quadratic -- which assumes it is -- has to be discarded. Two evaluations
    // at worst, never an iteration.
    const auto belowThreshold = [&]
    {
        return held > 0.0 ? m / held : m;
    };

    const auto b = k * sc.attack * sc.gain * sc.rectPole;
    const auto a = held + k * sc.attack * sc.gain
                            * ((1.0 - sc.rectPole) * s.rectifier - sc.threshold);

    double v;

    if (! (b > 0.0))
    {
        v = a > 0.0 ? m / a : m;
    }
    else
    {
        const auto disc = std::sqrt (std::max (0.0, a * a + 4.0 * b * m));

        v = a >= 0.0 ? (a + disc > 0.0 ? 2.0 * m / (a + disc) : 0.0)
                     : (-a + disc) / (2.0 * b);
    }

    if (! std::isfinite (v) || v < 0.0)
        v = belowThreshold();

    // The quadratic assumed the rectifier would land above threshold. If it
    // does not, the demand is zero and the control simply relaxes.
    const auto predicted = (1.0 - sc.rectPole) * s.rectifier + sc.rectPole * v;

    if (predicted <= sc.threshold)
        return belowThreshold();

    return v;
}

} // namespace bmo::fetcomp
