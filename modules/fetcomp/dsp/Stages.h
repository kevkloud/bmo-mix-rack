#pragma once

#include "modules/fetcomp/dsp/Calibration.h"

#include <algorithm>
#include <cmath>

namespace bmo::fetcomp
{

/** Anything smaller than this is not a signal, it is a denormal waiting to
    cost a hundred cycles. Every filter state in this file is flushed through
    it; the control path has its own floor in Calibration.h. */
inline constexpr float kDenormalFloor = 1.0e-20f;

inline float flush (float v) noexcept
{
    return std::abs (v) < kDenormalFloor ? 0.0f : v;
}

//==============================================================================
/** One asymmetric soft stage -- an input amplifier, an output amplifier or a
    transformer core -- as a residual the caller adds back:

        f(u)     = u / sqrt(1 + u^2)
        shape(x) = (f(a*x + b) - f(b)) / a
        stage(x) = x + amount * (shape(x) - f'(b) * x)

    **The algebraic curve rather than `tanh`, and the reason is the
    antiderivative.** ADAA needs `F` with `F' = f`, and this one's is
    `sqrt(1 + u^2)` -- a single square root. `tanh`'s is `log(cosh(u))`, which
    modules/sat/dsp/Shaper.h has to write as `|u| + log1p(expm1(-2|u|)/2)` to
    stay accurate near zero, and that is two transcendentals per stage per
    sample. There are three of these stages per channel and they run inside the
    oversampled region, so on AURORA the tanh form cost 263 ns/sample at
    defaults against 66 for LTV Comp, and the stages were nearly all of it.
    The Saturator can afford `tanh` because its curve *is* the product; here
    the shapers are the colour around a cell that does the work, and the two
    curves are indistinguishable at the drives this module runs them at.

    Two further things about the last line are deliberate.

    **The residual is taken against the curve's own small-signal gain**, not
    against x. `shape` has slope `f'(b)` at the origin, so subtracting
    `f'(b)*x` leaves something whose value *and slope* are zero at the
    origin: the stage is exactly unity gain for a small signal, at every
    setting of every constant. That is what makes the two voicings
    gain-matched at no reduction by construction rather than by a fitted
    makeup -- 10 section 8 asks for +/-0.1 dB and this gives 0.0 -- and it is
    also what keeps the response spec honest, since a level difference between
    voicings would otherwise have to be trimmed back out somewhere.

    **The bias is what makes it 2nd-dominant.** A symmetric curve is an odd
    function and produces only odd harmonics; offsetting the operating point
    is what a single-ended class-A stage does by construction, and it is the
    only reason there is any even-order content here at all.

    ADAA, first-order, on the **residual only**. Applied to the whole shaper
    the difference quotient costs real high-frequency response: where the
    curve is locally linear it reduces exactly to (x[n] + x[n-1])/2, a
    two-point average of magnitude cos(pi*f/fs) -- a tone control nobody asked
    for. Splitting the linear path out leaves it untouched.

    The FET cell does **not** use this class and cannot: ADAA's antiderivative
    assumes a curve fixed between samples, and the cell's moves every sample
    with the control (10 section 7). */
class SoftStage
{
public:
    /** Rebuilds the ADAA state against the new curve, which is not a nicety:
        ADAA carries F(x[n-1]) across samples and divides the difference of two
        antiderivatives by dx, so changing coefficients without this makes the
        numerator the difference between two *different* functions divided by a
        possibly tiny dx. modules/sat/dsp/Shaper.h measured what that sounds
        like -- single samples thirty times full scale. */
    void setShape (double newDrive, double newBias, double newAmount) noexcept
    {
        const auto d = std::max (newDrive, 1.0e-6);

        if (d == drive && newBias == bias && newAmount == amount)
            return;

        drive  = d;
        bias   = newBias;
        amount = newAmount;

        const auto root = std::sqrt (1.0 + bias * bias);
        offset = bias / root;                          // f(b)
        slope  = 1.0 / (root * root * root);           // f'(b)

        previousR = residualAntiderivative (previousX);
    }

    void reset() noexcept
    {
        previousX = 0.0;
        previousR = residualAntiderivative (0.0);
    }

    /** Re-seat the running state on the sample the signal is actually at,
        without changing the curve. The incoming half of a voicing crossfade
        needs this: one sample of first-order error, inaudible under the fade,
        against an unbounded spike if the state is left where it was. */
    void primeAt (float x) noexcept
    {
        previousX = (double) x;
        previousR = residualAntiderivative (previousX);
    }

    float process (float x) noexcept
    {
        const double xd = x;
        const auto r  = residualAntiderivative (xd);
        const auto dx = xd - previousX;

        // The difference quotient is ill-conditioned when the signal barely
        // moves; fall back to the residual at the midpoint.
        const auto averaged = std::abs (dx) > 1.0e-9 ? (r - previousR) / dx
                                                     : residual (0.5 * (xd + previousX));

        previousX = xd;
        previousR = r;
        return (float) (xd + amount * averaged);
    }

    /** The curve with no anti-aliasing, for the tests and the measurement
        harness to look at directly. */
    double shape (double x) const noexcept
    {
        const auto u = drive * x + bias;
        return (u / std::sqrt (1.0 + u * u) - offset) / drive;
    }

    double residual (double x) const noexcept { return shape (x) - slope * x; }

    /** Where the running state currently sits, so a differently-shaped copy
        can be seated at the same place. */
    double position() const noexcept { return previousX; }

private:
    /** `F(a*x+b)/a^2 - f(b)*x/a - f'(b)*x^2/2`, where `F(u) = sqrt(1 + u^2)`.
        One square root, and the whole reason this class is not `tanh`. */
    double residualAntiderivative (double x) const noexcept
    {
        const auto u = drive * x + bias;
        return (std::sqrt (1.0 + u * u) / drive - offset * x) / drive - 0.5 * slope * x * x;
    }

    double drive = 1.0, bias = 0.0, amount = 0.0;
    double offset = 0.0, slope = 1.0;
    double previousX = 0.0, previousR = 0.0;
};

//==============================================================================
/** One-pole low-pass, the transformer's HF corner. Above Nyquist at base rate
    for the 45 kHz pole, which is fine and is why the coefficient is derived
    from the rate every time rather than written down. */
class OnePoleLowpass
{
public:
    void setCutoff (double hz, double rate) noexcept
    {
        constexpr double kTwoPi = 6.28318530717958647692;
        const auto x = std::exp (-kTwoPi * std::max (hz, 1.0) / std::max (rate, 1.0));
        coeff = (float) std::clamp (x, 0.0, 0.999999);
    }

    void reset() noexcept { state = 0.0f; }

    float getState() const noexcept  { return state; }
    void  setState (float s) noexcept { state = s; }

    float process (float x) noexcept
    {
        state = flush (x + coeff * (state - x));
        return state;
    }

private:
    float coeff = 0.0f, state = 0.0f;
};

//==============================================================================
/** One-pole high-pass: the output transformer's LF corner, and the only thing
    standing between the asymmetric stages' DC and the host.

    There is exactly one of these in the chain and exactly one low-pass, which
    is what keeps the response inside A2's +/-1 dB: 10 Hz and 45 kHz give
    -1.0 dB at 20 Hz and -0.8 dB at 20 kHz, and a second pole at either end
    would double that and fail. If a stage ever needs its own DC blocker, it
    needs a corner far enough below 10 Hz not to move this figure. */
class OnePoleHighpass
{
public:
    void setCutoff (double hz, double rate) noexcept
    {
        constexpr double kTwoPi = 6.28318530717958647692;
        const auto x = std::exp (-kTwoPi * std::max (hz, 0.01) / std::max (rate, 1.0));
        coeff = (float) std::clamp (x, 0.0, 0.999999);
    }

    void reset() noexcept { state = 0.0f; }

    float getState() const noexcept  { return state; }
    void  setState (float s) noexcept { state = s; }

    float process (float x) noexcept
    {
        state = flush (x + coeff * (state - x));
        return x - state;
    }

private:
    float coeff = 0.0f, state = 0.0f;
};

//==============================================================================
/** The static colour around the cell: one voicing's worth of it.

    Ordered as the hardware is -- input transformer, input amplifier, [the
    cell happens outside this class], output amplifier, output transformer --
    and split into the two halves the cell sits between.

    The LF core saturation is a band split rather than a shaper across the
    whole signal: the band below the corner is shaped and the residual re-added,
    so saturation stays LF-only and nothing above it is touched. A transformer
    core saturates because of flux, and flux is the integral of voltage, so it
    is a low-frequency phenomenon; shaping the whole signal to get it would
    bring the top end along and measure as a tone change.

    Every shaper here is unity at small signal (see SoftStage), so this class
    contributes no level of its own and the two voicings need no gain trim. */
class StaticStages
{
public:
    void prepare (double effectiveRate) noexcept
    {
        rate = std::max (effectiveRate, 1.0);
        applyConstants();
        reset();
    }

    void setVoicing (const VoicingConstants& c) noexcept
    {
        constants = c;
        applyConstants();
    }

    void reset() noexcept
    {
        hf.reset();
        lf.reset();
        lfSplit.reset();
        lfSat.reset();
        inputAmp.reset();
        outputAmp.reset();
    }

    /** Take over another set's running memory without taking its curves.

        A voicing change fades between two of these, and the incoming one has
        to start where the signal already is or it contributes a step: a stale
        10 Hz high-pass state is a thump, and a stale ADAA state is the
        difference of two *different* antiderivatives over a possibly tiny dx.
        Only the states move -- the coefficients belong to the new voicing, so
        `hf = other.hf` would be exactly the wrong thing to write. */
    void adoptStateFrom (const StaticStages& other) noexcept
    {
        hf     .setState (other.hf     .getState());
        lfSplit.setState (other.lfSplit.getState());
        lf     .setState (other.lf     .getState());

        lfSat    .primeAt ((float) other.lfSat    .position());
        inputAmp .primeAt ((float) other.inputAmp .position());
        outputAmp.primeAt ((float) other.outputAmp.position());
    }

    /** Input transformer and input amplifier: everything before the cell. */
    float processInput (float x) noexcept
    {
        const auto banded = hf.process (x);
        const auto low    = lfSplit.process (banded);

        // The core shapes only what is below the corner; the residual is added
        // back so the rest of the spectrum arrives unaltered.
        const auto cored = banded + (lfSat.process (low) - low);

        return inputAmp.process (cored);
    }

    /** Output amplifier and output transformer: everything after it. */
    float processOutput (float x) noexcept
    {
        return lf.process (outputAmp.process (x));
    }

private:
    void applyConstants() noexcept
    {
        hf.setCutoff (constants.hfPoleHz, rate);
        lf.setCutoff (constants.lfPoleHz, rate);
        lfSplit.setCutoff (constants.lfSatCornerHz, rate);

        lfSat    .setShape (constants.lfSatDrive,  constants.lfSatBias,  constants.lfSatAmount);
        inputAmp .setShape (constants.inputDrive,  constants.inputBias,  constants.inputAmount);
        outputAmp.setShape (constants.outputDrive, constants.outputBias, constants.outputAmount);
    }

    VoicingConstants constants = kBlack;
    double rate = 48000.0;

    OnePoleLowpass  hf, lfSplit;
    OnePoleHighpass lf;
    SoftStage       lfSat, inputAmp, outputAmp;
};

} // namespace bmo::fetcomp
