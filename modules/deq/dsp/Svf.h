#pragma once

#include "modules/deq/dsp/Biquad.h"
#include <cmath>

namespace bmo::deq
{

/** A TPT state-variable filter (Zavalishin, Simper) that can realise *any*
    stable biquad, not only the bilinear designs it is usually written for.

    The usual SVF sets g = tan(pi f0 / Fs), and that is the bilinear transform:
    measured against the RBJ cookbook it agrees to 1e-11 dB, cramping and all.
    So "use an SVF" and "use matched-Z coefficients" read as a choice, and they
    are not one. The structure's denominator is fixed by (g, k) and its three
    outputs span every numerator, so for a digital pole pair (a1, a2):

        g^2 = (1 + a1 + a2) / (1 - a1 + a2)
        g k = 2 (1 - a2)    / (1 - a1 + a2)

    and the mix (m0, m1, m2) is solved from the numerator in closed form. The
    impulse response then matches the direct-form biquad to 5e-14.

    Why bother, rather than run the biquad directly: the SVF's state means the
    same thing before and after a coefficient change, so interpolating (g, k,
    m) sample by sample glides smoothly -- and g > 0, k > 0 is stable, which
    stays true at every point on a linear path between two stable filters. A
    direct-form biquad has neither property.
*/
struct SvfCoeffs
{
    double g = 1.0, k = 2.0, m0 = 1.0, m1 = 0.0, m2 = 0.0;

    static SvfCoeffs fromBiquad (const Biquad& q) noexcept
    {
        const auto plus  = 1.0 + q.a1 + q.a2;   // |D(z = 1)|, > 0 for stable poles
        const auto minus = 1.0 - q.a1 + q.a2;   // |D(z = -1)|, > 0 for stable poles

        SvfCoeffs c;
        c.g = std::sqrt (plus / minus);
        c.k = 2.0 * (1.0 - q.a2) / (minus * c.g);

        // The structure's own denominator, unnormalised, and the numerator
        // scaled to match it.
        const auto g2 = c.g * c.g;
        const auto d0 = 1.0 + c.g * c.k + g2;
        const auto d1 = 2.0 * g2 - 2.0;
        const auto c0 = d0 * q.b0, c1 = d0 * q.b1, c2 = d0 * q.b2;

        // LP and BP both vanish at Nyquist, so m0 is H(-1); then m2 from the
        // z^-1 row and m1 from the difference of the outer rows.
        c.m0 = 0.25 * (c0 - c1 + c2);
        c.m2 = (c1 - c.m0 * d1) / (2.0 * g2);
        c.m1 = (c0 - c2 - c.m0 * 2.0 * c.g * c.k) / (2.0 * c.g);
        return c;
    }

    bool isStable() const noexcept { return g > 0.0 && k > 0.0 && std::isfinite (m0 + m1 + m2); }
};

/** The per-sample structure coefficients derived from (g, k). */
struct SvfTaps
{
    double a1 = 0.0, a2 = 0.0, a3 = 0.0;

    static SvfTaps of (double g, double k) noexcept
    {
        SvfTaps t;
        t.a1 = 1.0 / (1.0 + g * (g + k));
        t.a2 = g * t.a1;
        t.a3 = g * t.a2;
        return t;
    }
};

struct SvfState
{
    double ic1 = 0.0, ic2 = 0.0;

    void reset() noexcept { ic1 = ic2 = 0.0; }

    /** Advance one sample; returns (v1, v2): band and low outputs. */
    void tick (const SvfTaps& t, double x, double& v1, double& v2) noexcept
    {
        const auto v3 = x - ic2;
        v1 = t.a1 * ic1 + t.a2 * v3;
        v2 = ic2 + t.a2 * ic1 + t.a3 * v3;
        ic1 = 2.0 * v1 - ic1;
        ic2 = 2.0 * v2 - ic2;
    }

    double process (const SvfTaps& t, const SvfCoeffs& c, double x) noexcept
    {
        double v1, v2;
        tick (t, x, v1, v2);
        return c.m0 * x + c.m1 * v1 + c.m2 * v2;
    }

    /** Flush anything a double cannot hold at full precision. Called on a
        fixed sample cadence, never at block boundaries, so it cannot make the
        output depend on the host's block size. */
    void flushTiny() noexcept
    {
        if (std::abs (ic1) < 1.0e-30) ic1 = 0.0;
        if (std::abs (ic2) < 1.0e-30) ic2 = 0.0;
    }

    bool isNormal() const noexcept
    {
        auto ok = [] (double v) { return v == 0.0 || std::isnormal (v); };
        return ok (ic1) && ok (ic2);
    }
};

} // namespace bmo::deq
