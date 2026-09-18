#pragma once

#include <algorithm>

namespace bmo::dsp
{

/** A static compression curve: where it starts, how hard it pulls, and how
    wide the corner is.

    `slope` is dB of reduction per dB over threshold, **not** a ratio. That
    distinction is not pedantry -- a feedback cell cannot express what it
    needs as a ratio at all, which is what modules/opto/dsp/Detector.h found
    out the hard way -- so the shared type carries the slope and each module
    converts from whatever it actually has. */
struct Curve
{
    float thresholdDb = 0.0f;
    float slope       = 0.0f;
    float kneeDb      = 0.0f;
};

/** The reduction slope a **feedforward** cell needs to deliver `ratio`.
    Output is input minus the reduction, so a slope of `1 - 1/R` leaves a
    residual slope of `1/R` -- the ratio, straightforwardly. */
constexpr float feedforwardSlope (float ratio) noexcept
{
    return 1.0f - 1.0f / ratio;
}

/** The reduction slope a **feedback** cell needs to deliver `ratio`, which is
    a different number, because the detector reads the already-reduced output
    rather than the input. In steady state, with slope `a`:

        y = x - a(y - T)  ->  y = (x + aT)/(1 + a)  ->  dy/dx = 1/(1 + a)

    so the delivered ratio is `1 + a`, and `a = ratio - 1`. Expressed as a
    slope there is no ceiling; expressed as `1 - 1/R` a feedback cell could
    never be asked for more than 2:1 whatever number you handed it. */
constexpr float feedbackSlope (float ratio) noexcept
{
    return ratio - 1.0f;
}

/** The soft-knee gain computer from Reiss & McPherson's compressor tutorial,
    restated to return the reduction directly: for a ratio R and knee width W
    either side of threshold T, the input-output gain difference is a smooth
    parabola inside the knee and a straight line beyond it.

    Generic knee arithmetic, and nothing about any particular unit: it is what
    BMO Opto's two modes and BMO Vcomp's single swept curve all agree on, and
    the reason it lives in core/ rather than in one of them. */
inline float kneeReductionDb (float levelDb, const Curve& curve) noexcept
{
    const auto diff = levelDb - curve.thresholdDb;
    const auto half = curve.kneeDb * 0.5f;

    if (diff <= -half)
        return 0.0f;

    if (diff < half)
    {
        const auto t = diff + half;
        return curve.slope * (t * t) / (2.0f * curve.kneeDb);
    }

    return curve.slope * diff;
}

} // namespace bmo::dsp
