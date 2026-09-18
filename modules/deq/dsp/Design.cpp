#include "modules/deq/dsp/Design.h"

#include <algorithm>

namespace bmo::deq
{

namespace
{
    /** sin^2(w/2), the variable in which a biquad's squared magnitude is a
        quadratic. Everything the zero fit does happens in this space. */
    double phiOf (double hz, double sampleRate) noexcept
    {
        const auto s = std::sin (kPi * hz / sampleRate);
        return s * s;
    }

    /** |P(e^jw)|^2 = c0 + c1 phi + c2 phi^2 for P = p0 + p1 z^-1 + p2 z^-2. */
    struct PowerPoly
    {
        double c0 = 0.0, c1 = 0.0, c2 = 0.0;

        static PowerPoly of (double p0, double p1, double p2) noexcept
        {
            const auto sum = p0 + p1 + p2;
            return { sum * sum, -4.0 * (p1 * (p0 + p2) + 4.0 * p0 * p2), 16.0 * p0 * p2 };
        }

        double at (double phi) const noexcept    { return c0 + c1 * phi + c2 * phi * phi; }
        double slope (double phi) const noexcept { return c1 + 2.0 * c2 * phi; }
    };

    /** |H(jw)|^2 of the prototype in real arithmetic, from w and w^2. */
    double prototypePower (const Prototype& p, double w, double w2) noexcept
    {
        const auto nr = p.n0 - p.n2 * w2, ni = p.n1 * w;
        const auto dr = p.d0 - p.d2 * w2, di = p.d1 * w;
        return (nr * nr + ni * ni) / (dr * dr + di * di);
    }

    /** The minimum-phase b0, b1, b2 whose power is B0 + B1 phi + B2 phi^2.

        B0 = (b0 + b1 + b2)^2 is the power at DC and B0 + B1 + B2 =
        (b0 - b1 + b2)^2 the power at Nyquist, so the two square roots give
        b1 and b0 + b2 directly, and B2 = 16 b0 b2 splits the sum. Of the
        sign and root choices, exactly one is minimum phase with a positive
        DC gain; it is the one a reciprocal can be taken of. Returns false if
        the power goes negative somewhere, i.e. the fit asked for something no
        real filter has. */
    bool factorMinimumPhase (double bb0, double bb1, double bb2, Biquad& out) noexcept
    {
        const auto nyq = bb0 + bb1 + bb2;

        if (! (bb0 >= -1.0e-15) || ! (nyq >= -1.0e-15))
            return false;

        const auto s0 = std::sqrt (std::max (0.0, bb0));
        const auto s1 = std::sqrt (std::max (0.0, nyq));

        for (const auto sign : { 1.0, -1.0 })
        {
            const auto w  = 0.5 * (s0 + sign * s1);
            const auto b1 = 0.5 * (s0 - sign * s1);
            auto disc = w * w - 0.25 * bb2;

            if (disc < -1.0e-12 * std::max (w * w, 1.0e-300))
                continue;

            disc = std::sqrt (std::max (0.0, disc));

            for (const auto root : { 1.0, -1.0 })
            {
                Biquad q = out;
                q.b0 = 0.5 * (w + root * disc);
                q.b1 = b1;
                q.b2 = w - q.b0;

                if (q.b0 > 0.0 && q.isMinimumPhase())
                {
                    out = q;
                    return true;
                }
            }
        }

        return false;
    }

    /** The classic matched Z-transform: zeros mapped the same way as the
        poles, zeros at infinity sent to Nyquist, gain matched at f0. Less
        accurate than the fits, but it cannot fail, so it is the fallback. */
    Biquad mapZerosToo (const Prototype& p, double f0, double sampleRate, Biquad poles) noexcept
    {
        std::complex<double> z1, z2;

        if (std::abs (p.n2) > 0.0)
        {
            const auto disc = std::sqrt (std::complex<double> (p.n1 * p.n1 - 4.0 * p.n2 * p.n0, 0.0));
            z1 = std::exp ((-p.n1 + disc) / (2.0 * p.n2) / sampleRate);
            z2 = std::exp ((-p.n1 - disc) / (2.0 * p.n2) / sampleRate);
        }
        else if (std::abs (p.n1) > 0.0)
        {
            z1 = std::exp (std::complex<double> (-p.n0 / p.n1 / sampleRate, 0.0));
            z2 = -1.0;
        }
        else
        {
            z1 = z2 = -1.0;
        }

        Biquad q = poles;
        q.b0 = 1.0;
        q.b1 = -(z1 + z2).real();
        q.b2 = (z1 * z2).real();

        const auto want = std::sqrt (p.magnitudeSquaredAt (f0));
        const auto got  = std::abs (q.responseAt (2.0 * kPi * f0 / sampleRate));

        if (got > 0.0 && std::isfinite (want / got))
        {
            const auto s = want / got;
            q.b0 *= s; q.b1 *= s; q.b2 *= s;
        }

        return q;
    }

    Biquad designBell (const Prototype& p, double f0, double sampleRate, Biquad poles, const PowerPoly& d) noexcept
    {
        // Unity at DC, the prototype's gain at f0, and a flat top there:
        //   B0 = D(0)
        //   B0 + B1 phi0 + B2 phi0^2 = T0 D(phi0)
        //   B1 + 2 B2 phi0           = T0 D'(phi0)      (T'(phi0) = 0 at the peak)
        //
        // B2 is a difference of two nearly equal terms divided by phi0^2, so
        // for f0 below ~3e-4 of the sample rate a few parts in 1e7 of the
        // centre gain are lost to rounding (2e-6 dB at 50 Hz / 176.4 kHz).
        const auto phi0 = phiOf (f0, sampleRate);
        const auto t0   = p.magnitudeSquaredAt (f0);

        const auto bb0 = d.at (0.0);
        const auto r1  = t0 * d.at (phi0) - bb0;
        const auto r2  = t0 * d.slope (phi0);
        const auto bb2 = (r2 * phi0 - r1) / (phi0 * phi0);
        const auto bb1 = r2 - 2.0 * bb2 * phi0;

        Biquad q = poles;
        return factorMinimumPhase (bb0, bb1, bb2, q) ? q : mapZerosToo (p, f0, sampleRate, poles);
    }

    /** Least squares on the relative error of |H|^2 over the grid. Relative
        error in |H|^2 is twice the error in dB to first order, so this is
        close to a dB fit while staying linear in the unknowns.

        `doubleZeroAtDc` pins B0 = B1 = 0 and fits B2 alone (the low cut);
        otherwise B0 is pinned to the prototype's DC power and B1, B2 are
        fitted. */
    Biquad designByLeastSquares (const Prototype& p, double f0, const DesignGrid& grid, Biquad poles,
                                 const PowerPoly& d, bool doubleZeroAtDc) noexcept
    {
        const auto bb0 = doubleZeroAtDc ? 0.0 : (p.n0 * p.n0) / (p.d0 * p.d0) * d.at (0.0);
        double s11 = 0.0, s12 = 0.0, s22 = 0.0, r1 = 0.0, r2 = 0.0;

        for (int i = 0; i < DesignGrid::kPoints; ++i)
        {
            const auto phi = grid.phi[(size_t) i];
            const auto td  = prototypePower (p, grid.omega[(size_t) i], grid.omega2[(size_t) i]) * d.at (phi);

            if (! (td > 0.0))
                continue;

            const auto inv = 1.0 / td;
            const auto u = phi * inv, v = phi * phi * inv, rhs = 1.0 - bb0 * inv;
            s11 += u * u; s12 += u * v; s22 += v * v;
            r1  += u * rhs; r2 += v * rhs;
        }

        Biquad q = poles;
        bool ok = false;

        if (doubleZeroAtDc)
        {
            ok = s22 > 0.0 && factorMinimumPhase (0.0, 0.0, r2 / s22, q);
        }
        else
        {
            const auto det = s11 * s22 - s12 * s12;
            ok = std::abs (det) > 1.0e-300
              && factorMinimumPhase (bb0, (r1 * s22 - r2 * s12) / det, (s11 * r2 - s12 * r1) / det, q);
        }

        return ok ? q : mapZerosToo (p, f0, grid.sampleRate, poles);
    }

    Biquad designDirect (Shape shape, double f0, double q, double gainDb, const DesignGrid& grid) noexcept
    {
        const auto p = Prototype::make (shape, f0, q, gainDb);

        Biquad poles;
        matchedPoles (p, grid.sampleRate, poles.a1, poles.a2);
        const auto d = PowerPoly::of (1.0, poles.a1, poles.a2);

        switch (shape)
        {
            case Shape::bell:      return designBell           (p, f0, grid.sampleRate, poles, d);
            case Shape::lowShelf:
            case Shape::highShelf:
            case Shape::highCut:   return designByLeastSquares (p, f0, grid, poles, d, false);
            case Shape::lowCut:    return designByLeastSquares (p, f0, grid, poles, d, true);
        }

        return poles;
    }

    Biquad designBoost (Shape shape, double f0, double q, double gainDb, const DesignGrid& grid) noexcept
    {
        if (shape == Shape::highShelf && gainDb > 0.0)
        {
            // HS(A) = A^2 / LS(A): see Design.h for why the high shelf is not
            // designed from its own poles.
            const auto mirrored = designDirect (Shape::lowShelf, f0, q, gainDb, grid).reciprocal();

            if (mirrored.isStable() && mirrored.isFinite())
            {
                const auto g = std::pow (10.0, gainDb / 20.0);
                return { mirrored.b0 * g, mirrored.b1 * g, mirrored.b2 * g, mirrored.a1, mirrored.a2 };
            }
        }

        return designDirect (shape, f0, q, gainDb, grid);
    }
}

//==============================================================================
DesignGrid DesignGrid::make (double sampleRate) noexcept
{
    DesignGrid g;
    g.sampleRate = sampleRate;
    const auto lo = 20.0, hi = 0.4999 * sampleRate;

    for (int i = 0; i < kPoints; ++i)
    {
        const auto hz = lo * std::pow (hi / lo, (double) i / (double) (kPoints - 1));
        const auto w  = 2.0 * kPi * hz;
        g.omega[(size_t) i]  = w;
        g.omega2[(size_t) i] = w * w;
        g.phi[(size_t) i]    = phiOf (hz, sampleRate);
    }

    return g;
}

void matchedPoles (const Prototype& p, double sampleRate, double& a1, double& a2) noexcept
{
    const auto c1 = p.d1 / p.d2, c0 = p.d0 / p.d2;
    const auto disc = c1 * c1 - 4.0 * c0;

    if (disc < 0.0)
    {
        const auto r = std::exp (-0.5 * c1 / sampleRate);
        a1 = -2.0 * r * std::cos (0.5 * std::sqrt (-disc) / sampleRate);
        a2 = r * r;
    }
    else
    {
        const auto root = std::sqrt (disc);
        const auto z1 = std::exp (0.5 * (-c1 + root) / sampleRate);
        const auto z2 = std::exp (0.5 * (-c1 - root) / sampleRate);
        a1 = -(z1 + z2);
        a2 = z1 * z2;
    }
}

double clampFrequency (double hz, double sampleRate) noexcept
{
    const auto top = DesignLimits::kMaxNyquist * sampleRate;
    if (! std::isfinite (hz)) hz = 1000.0;
    return std::clamp (hz, std::min (DesignLimits::kMinHz, top), top);
}

double clampQ (double q) noexcept
{
    if (! std::isfinite (q)) q = 0.707;
    return std::clamp (q, DesignLimits::kMinQ, DesignLimits::kMaxQ);
}

double clampGainDb (double gainDb) noexcept
{
    if (! std::isfinite (gainDb)) gainDb = 0.0;
    return std::clamp (gainDb, -DesignLimits::kMaxGainDb, DesignLimits::kMaxGainDb);
}

Biquad designMatched (Shape shape, double frequencyHz, double q, double gainDb, const DesignGrid& grid) noexcept
{
    const auto sampleRate = grid.sampleRate;
    const auto f0 = clampFrequency (frequencyHz, sampleRate);
    q = clampQ (q);
    gainDb = hasGain (shape) ? clampGainDb (gainDb) : 0.0;

    if (hasGain (shape) && std::abs (gainDb) <= DesignLimits::kUnityGainDb)
    {
        // Numerator equal to denominator: unity, but with the band's own
        // poles, so a band gliding through 0 dB keeps a continuous structure
        // rather than jumping to a different filter and back.
        Biquad poles;
        matchedPoles (Prototype::make (shape, f0, q, 0.0), sampleRate, poles.a1, poles.a2);
        return { 1.0, poles.a1, poles.a2, poles.a1, poles.a2 };
    }

    if (hasGain (shape) && gainDb < 0.0)
    {
        const auto cut = designBoost (shape, f0, q, -gainDb, grid).reciprocal();

        if (cut.isStable() && cut.isFinite())
            return cut;

        // A boost whose zeros landed on the unit circle cannot be inverted.
        // Not expected inside the limits, but design the cut directly rather
        // than hand the audio path something unstable.
        return designDirect (shape, f0, q, gainDb, grid);
    }

    return designBoost (shape, f0, q, gainDb, grid);
}

Biquad designMatched (Shape shape, double frequencyHz, double q, double gainDb, double sampleRate) noexcept
{
    sampleRate = std::isfinite (sampleRate) ? std::max (sampleRate, 1000.0) : 48000.0;
    return designMatched (shape, frequencyHz, q, gainDb, DesignGrid::make (sampleRate));
}

} // namespace bmo::deq
