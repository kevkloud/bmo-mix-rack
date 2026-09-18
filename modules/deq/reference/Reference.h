#pragma once

// Test and measurement only. Nothing under modules/deq/dsp includes this, and
// nothing in it may be used to make sound: the cookbook designs here are the
// bilinear transform, which is exactly what the module exists to avoid. They
// are kept so every accuracy claim is made against a named alternative.

#include "modules/deq/dsp/Biquad.h"
#include "modules/deq/dsp/Prototype.h"
#include <algorithm>
#include <cmath>
#include <complex>
#include <vector>

namespace bmo::deq::reference
{

/** Robert Bristow-Johnson's Audio EQ Cookbook, bilinear with prewarping. */
inline Biquad cookbook (Shape shape, double f0, double q, double gainDb, double fs) noexcept
{
    const auto a  = std::pow (10.0, gainDb / 40.0);
    const auto w0 = 2.0 * kPi * f0 / fs, c = std::cos (w0), s = std::sin (w0);
    const auto al = s / (2.0 * q), sa = std::sqrt (a);
    double b0 = 1, b1 = 0, b2 = 0, a0 = 1, a1 = 0, a2 = 0;

    switch (shape)
    {
        case Shape::bell:
            b0 = 1 + al * a; b1 = -2 * c; b2 = 1 - al * a; a0 = 1 + al / a; a1 = -2 * c; a2 = 1 - al / a;
            break;
        case Shape::lowShelf:
            b0 = a * ((a + 1) - (a - 1) * c + 2 * sa * al); b1 = 2 * a * ((a - 1) - (a + 1) * c);
            b2 = a * ((a + 1) - (a - 1) * c - 2 * sa * al); a0 = (a + 1) + (a - 1) * c + 2 * sa * al;
            a1 = -2 * ((a - 1) + (a + 1) * c); a2 = (a + 1) + (a - 1) * c - 2 * sa * al;
            break;
        case Shape::highShelf:
            b0 = a * ((a + 1) + (a - 1) * c + 2 * sa * al); b1 = -2 * a * ((a - 1) + (a + 1) * c);
            b2 = a * ((a + 1) + (a - 1) * c - 2 * sa * al); a0 = (a + 1) - (a - 1) * c + 2 * sa * al;
            a1 = 2 * ((a - 1) - (a + 1) * c); a2 = (a + 1) - (a - 1) * c - 2 * sa * al;
            break;
        case Shape::highCut:
            b0 = (1 - c) / 2; b1 = 1 - c; b2 = (1 - c) / 2; a0 = 1 + al; a1 = -2 * c; a2 = 1 - al;
            break;
        case Shape::lowCut:
            b0 = (1 + c) / 2; b1 = -(1 + c); b2 = (1 + c) / 2; a0 = 1 + al; a1 = -2 * c; a2 = 1 - al;
            break;
    }

    return { b0 / a0, b1 / a0, b2 / a0, a1 / a0, a2 / a0 };
}

//==============================================================================
/** The spec's three accuracy regions, as fractions of the sample rate. */
struct RegionErrors
{
    double worst[3] = { 0.0, 0.0, 0.0 };   // 20 Hz-0.25 Fs, 0.25-0.40 Fs, 0.40-0.45 Fs
    double worstHz[3] = { 0.0, 0.0, 0.0 };
};

inline constexpr double kRegionTargetDb[3] = { 0.05, 0.15, 0.50 };

/** Max |dB| between a digital filter and the analogue prototype, per region,
    on a 4096-point log grid from 10 Hz to 0.499 Fs. `floorDb` skips points
    where both responses are below it: without a floor, a cut filter's
    stopband error is unbounded and says nothing (the analogue response keeps
    falling past Nyquist, the digital one cannot). */
inline RegionErrors regionErrors (const Biquad& q, const Prototype& p, double fs, double floorDb = -1.0e9)
{
    RegionErrors r;

    for (int i = 0; i < 4096; ++i)
    {
        const auto hz = 10.0 * std::pow (0.499 * fs / 10.0, (double) i / 4095.0);

        if (hz < 20.0 || hz > 0.45 * fs)
            continue;

        const auto ad = p.magnitudeDbAt (hz);
        const auto dd = q.magnitudeDbAt (hz, fs);

        if (std::max (ad, dd) < floorDb)
            continue;

        const auto region = hz <= 0.25 * fs ? 0 : (hz <= 0.40 * fs ? 1 : 2);
        const auto e = std::abs (ad - dd);

        if (e > r.worst[region])
        {
            r.worst[region] = e;
            r.worstHz[region] = hz;
        }
    }

    return r;
}

//==============================================================================
inline void fft (std::vector<std::complex<double>>& a)
{
    const auto n = a.size();

    for (size_t i = 1, j = 0; i < n; ++i)
    {
        auto bit = n >> 1;
        for (; j & bit; bit >>= 1)
            j ^= bit;
        j ^= bit;
        if (i < j)
            std::swap (a[i], a[j]);
    }

    for (size_t len = 2; len <= n; len <<= 1)
    {
        const auto wl = std::polar (1.0, -2.0 * kPi / (double) len);

        for (size_t i = 0; i < n; i += len)
        {
            std::complex<double> w = 1.0;

            for (size_t k = 0; k < len / 2; ++k)
            {
                const auto u = a[i + k], v = a[i + k + len / 2] * w;
                a[i + k] = u + v;
                a[i + k + len / 2] = u - v;
                w *= wl;
            }
        }
    }
}

/** How long an impulse response must be before truncating it costs less
    than 1e-13 of amplitude: the slowest pole's radius to that power. The
    spec's fixed 64k FFT is 3.7 time constants for a 50 Hz, Q 16, +24 dB bell
    and disagrees with the analytic response by 0.35 dB -- a truncation
    artefact, not a filter bug. */
inline size_t irLengthFor (const Biquad& q, size_t minimum = (size_t) 1 << 16, size_t maximum = (size_t) 1 << 22)
{
    const auto disc = std::sqrt (std::complex<double> (q.a1 * q.a1 - 4.0 * q.a2, 0.0));
    const auto r = std::max (std::abs ((-q.a1 + disc) * 0.5), std::abs ((-q.a1 - disc) * 0.5));
    auto n = minimum;

    if (r > 0.0 && r < 1.0)
    {
        const auto needed = std::log (1.0e-13) / std::log (r);
        while ((double) n < needed && n < maximum)
            n <<= 1;
    }

    return n;
}

/** Impulse response of the biquad run as a direct-form-I recursion. */
inline std::vector<std::complex<double>> impulseResponse (const Biquad& q, size_t n)
{
    std::vector<std::complex<double>> h (n);
    double x1 = 0, x2 = 0, y1 = 0, y2 = 0;

    for (size_t i = 0; i < n; ++i)
    {
        const auto x = i == 0 ? 1.0 : 0.0;
        const auto y = q.b0 * x + q.b1 * x1 + q.b2 * x2 - q.a1 * y1 - q.a2 * y2;
        x2 = x1; x1 = x; y2 = y1; y1 = y;
        h[i] = y;
    }

    return h;
}

/** Worst |dB| between the analytic transfer function and the FFT of an
    impulse response, over 10 Hz to 0.499 Fs. */
inline double analyticVsMeasuredDb (const Biquad& q, std::vector<std::complex<double>> ir, double fs)
{
    const auto n = ir.size();
    fft (ir);
    double worst = 0.0;

    for (size_t k = 1; k < n / 2; ++k)
    {
        const auto hz = (double) k * fs / (double) n;

        if (hz < 10.0 || hz > 0.499 * fs)
            continue;

        const auto measured = 10.0 * std::log10 (std::max (std::norm (ir[k]), 1.0e-300));
        const auto analytic = 20.0 * std::log10 (std::max (std::abs (q.responseAt (2.0 * kPi * (double) k / (double) n)), 1.0e-300));
        worst = std::max (worst, std::abs (measured - analytic));
    }

    return worst;
}

} // namespace bmo::deq::reference
