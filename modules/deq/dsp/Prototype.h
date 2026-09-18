#pragma once

#include "modules/deq/dsp/Biquad.h"

namespace bmo::deq
{

/** The shapes a band can take. This order is internal; params.h maps its own
    choice list onto it, so reordering here never touches a saved session. */
enum class Shape { bell, lowShelf, highShelf, lowCut, highCut };

inline constexpr bool hasGain (Shape s) noexcept
{
    return s == Shape::bell || s == Shape::lowShelf || s == Shape::highShelf;
}

/** The analogue filter a band is the digital image of: the RBJ cookbook's
    s-domain forms, with A = 10^(gain/40).

        bell        (s^2 + s w A/Q + w^2)      / (s^2 + s w/(A Q) + w^2)
        low shelf   A (s^2 + s w sqrtA/Q + A w^2) / (A s^2 + s w sqrtA/Q + w^2)
        high shelf  A (A s^2 + s w sqrtA/Q + w^2) / (s^2 + s w sqrtA/Q + A w^2)
        low cut     s^2 / (s^2 + s w/Q + w^2)
        high cut    w^2 / (s^2 + s w/Q + w^2)

    This is ground truth twice over. The matched design (Design.h) fits its
    zeros to this magnitude response, and the tests measure every shipping
    filter's error against it -- "sounds right" is not a measurement.

    Worth knowing before asserting anything about poles: the bell's pole Q is
    A*Q, not Q, and a shelf's pole frequency is w/sqrtA (low) or w*sqrtA
    (high). Only the cuts have their poles where their controls say. An
    invariant written against the knob values rather than against these
    polynomials will fail on every bell and shelf.
*/
struct Prototype
{
    /** H(s) = (n2 s^2 + n1 s + n0) / (d2 s^2 + d1 s + d0), s in rad/s. */
    double n2 = 0.0, n1 = 0.0, n0 = 1.0;
    double d2 = 0.0, d1 = 0.0, d0 = 1.0;

    static Prototype make (Shape shape, double frequencyHz, double q, double gainDb) noexcept
    {
        const auto a  = std::pow (10.0, gainDb / 40.0);
        const auto sa = std::sqrt (a);
        const auto w  = 2.0 * kPi * frequencyHz;

        switch (shape)
        {
            case Shape::bell:      return { 1.0, w * a / q, w * w,             1.0, w / (a * q), w * w };
            case Shape::lowShelf:  return { a, a * sa * w / q, a * a * w * w,  a,   sa * w / q, w * w };
            case Shape::highShelf: return { a * a, a * sa * w / q, a * w * w,  1.0, sa * w / q, a * w * w };
            case Shape::lowCut:    return { 1.0, 0.0, 0.0,                     1.0, w / q, w * w };
            case Shape::highCut:   return { 0.0, 0.0, w * w,                   1.0, w / q, w * w };
        }

        return {};
    }

    std::complex<double> at (double hz) const noexcept
    {
        const std::complex<double> s { 0.0, 2.0 * kPi * hz };
        return (n2 * s * s + n1 * s + n0) / (d2 * s * s + d1 * s + d0);
    }

    double magnitudeSquaredAt (double hz) const noexcept
    {
        return std::norm (at (hz));
    }

    double magnitudeDbAt (double hz) const noexcept
    {
        const auto m2 = magnitudeSquaredAt (hz);
        return 10.0 * std::log10 (m2 > 1.0e-300 ? m2 : 1.0e-300);
    }
};

} // namespace bmo::deq
