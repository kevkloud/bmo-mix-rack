#pragma once

#include "modules/tune/dsp/Pitch.h"
#include <array>
#include <cmath>

namespace bmo::tune
{

/** Transposed direct form II biquad, double state.

    Double because these run on the analysis side, where a lowpass at a few
    hundred hertz and a 192 kHz rate puts the poles within 1e-4 of the unit
    circle, and float coefficients there are the difference between a filter
    and an oscillator. */
class Biquad
{
public:
    void setCoefficients (double b0In, double b1In, double b2In, double a1In, double a2In) noexcept
    {
        b0 = b0In; b1 = b1In; b2 = b2In; a1 = a1In; a2 = a2In;
    }

    void reset() noexcept { z1 = z2 = 0.0; }

    double process (double x) noexcept
    {
        const auto y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return y;
    }

    /** RBJ-cookbook second-order lowpass at `hz` with quality `q`. */
    void makeLowpass (double sampleRate, double hz, double q) noexcept
    {
        const auto w = 2.0 * kPi * hz / sampleRate;
        const auto alpha = std::sin (w) / (2.0 * q);
        const auto c = std::cos (w);
        const auto a0 = 1.0 + alpha;
        setCoefficients ((1.0 - c) * 0.5 / a0, (1.0 - c) / a0, (1.0 - c) * 0.5 / a0,
                         -2.0 * c / a0, (1.0 - alpha) / a0);
    }

    void makeHighpass (double sampleRate, double hz, double q) noexcept
    {
        const auto w = 2.0 * kPi * hz / sampleRate;
        const auto alpha = std::sin (w) / (2.0 * q);
        const auto c = std::cos (w);
        const auto a0 = 1.0 + alpha;
        setCoefficients ((1.0 + c) * 0.5 / a0, -(1.0 + c) / a0, (1.0 + c) * 0.5 / a0,
                         -2.0 * c / a0, (1.0 - alpha) / a0);
    }

private:
    double b0 = 1.0, b1 = 0.0, b2 = 0.0, a1 = 0.0, a2 = 0.0;
    double z1 = 0.0, z2 = 0.0;
};

/** An even-order Butterworth lowpass as a cascade of biquads.

    The Q of section k of an order-N Butterworth is 1 / (2 cos((2k+1) pi / 2N)),
    which is what makes the cascade maximally flat rather than N/2 identical
    peaky sections. */
template <int Sections>
class ButterworthLowpass
{
public:
    void prepare (double sampleRate, double hz) noexcept
    {
        constexpr int order = 2 * Sections;

        for (int k = 0; k < Sections; ++k)
        {
            const auto q = 1.0 / (2.0 * std::cos ((2.0 * k + 1.0) * kPi / (2.0 * order)));
            stages[(size_t) k].makeLowpass (sampleRate, hz, q);
        }

        reset();
    }

    void reset() noexcept
    {
        for (auto& s : stages)
            s.reset();
    }

    double process (double x) noexcept
    {
        for (auto& s : stages)
            x = s.process (x);

        return x;
    }

private:
    std::array<Biquad, Sections> stages;
};

/** First-order DC blocker: y = x - x[n-1] + r y[n-1].

    The analysis path needs it and the audio path must not have it. A DC
    offset adds the same constant to every lag's correlation and to every
    lag's energy, so the NSDF of an offset signal tends toward 1 at every lag
    -- the detector would call a DC step perfectly periodic at the shortest
    lag it is allowed. The spec's pathological corpus includes exactly that. */
class DcBlocker
{
public:
    void prepare (double sampleRate, double cornerHz) noexcept
    {
        r = std::exp (-2.0 * kPi * cornerHz / sampleRate);
        reset();
    }

    void reset() noexcept { x1 = y1 = 0.0; }

    double process (double x) noexcept
    {
        const auto y = x - x1 + r * y1;
        x1 = x;
        y1 = y;
        return y;
    }

private:
    double r = 0.999, x1 = 0.0, y1 = 0.0;
};

} // namespace bmo::tune
