#pragma once

/*
    Offline measurement: the ruler the tests and bmo-tune-score hold the
    plugin's output against.

    Deliberately NOT the plugin's own detector. Measuring the output with the
    same code that decided the correction would agree with itself whatever
    it did. These are non-causal, long-window, brute-force estimators --
    slow, and accurate to hundredths of a cent on a steady tone -- and they
    share nothing with modules/tune/dsp but the arithmetic.
*/

#include <algorithm>
#include <cmath>
#include <vector>

namespace bmo::tune::analysis
{

inline constexpr double kPi = 3.14159265358979323846;

/** Frequency of the dominant periodicity in x[start, start + length), by a
    full NSDF over the segment and parabolic interpolation, then refined by
    fitting a sinusoid at that frequency and its neighbours (golden-section
    on the fit residual). Returns 0 if nothing periodic in range. */
inline double measureHz (const std::vector<float>& x, size_t start, size_t length,
                         double fs, double minHz = 50.0, double maxHz = 2000.0)
{
    if (start + length > x.size() || length < 64)
        return 0.0;

    const auto minLag = std::max (2, (int) std::floor (fs / maxHz));
    const auto maxLag = std::min ((int) (length / 2), (int) std::ceil (fs / minHz));
    const auto window = (int) length - maxLag;

    if (window <= 16 || maxLag <= minLag + 2)
        return 0.0;

    std::vector<double> nsdf ((size_t) maxLag + 2, 0.0);

    for (int L = minLag - 1; L <= maxLag + 1 && L < (int) length - window; ++L)
    {
        double r = 0.0, m = 0.0;
        for (int j = 0; j < window; ++j)
        {
            const double a = x[start + (size_t) j];
            const double b = x[start + (size_t) (j + L)];
            r += a * b;
            m += a * a + b * b;
        }
        nsdf[(size_t) L] = m > 0.0 ? 2.0 * r / m : 0.0;
    }

    // McLeod's rule again, but with the whole segment to look at: the first
    // key maximum within 0.9 of the best.
    double best = 0.0;
    for (int L = minLag; L <= maxLag; ++L)
        best = std::max (best, nsdf[(size_t) L]);

    if (best < 0.5)
        return 0.0;

    int lag = 0;
    for (int L = minLag + 1; L < maxLag; ++L)
    {
        if (nsdf[(size_t) L] >= 0.9 * best && nsdf[(size_t) L] >= nsdf[(size_t) L - 1]
            && nsdf[(size_t) L] >= nsdf[(size_t) L + 1])
        {
            lag = L;
            break;
        }
    }

    if (lag == 0)
        return 0.0;

    const auto l = nsdf[(size_t) lag - 1], c = nsdf[(size_t) lag], r = nsdf[(size_t) lag + 1];
    const auto d = l - 2.0 * c + r;
    const auto period = lag + (std::abs (d) > 1.0e-12 ? 0.5 * (l - r) / d : 0.0);

    // Refine: the period that makes the segment best match itself shifted by
    // one period, searched by golden section on the squared difference with
    // a band-limited (sinc-interpolated) shift. This is what gets the ruler
    // from parabolic accuracy to hundredths of a cent.
    const auto shifted = [&] (double p)
    {
        const auto whole = (int) std::floor (p);
        const auto frac = p - whole;
        double err = 0.0;
        constexpr int half = 16;

        // The kernel depends on the fraction only, so it is worked out once
        // per trial period rather than once per sample: the same result to
        // rounding (x (sinc w) rather than (x sinc) w; ~1e-10 of a cent),
        // several times sooner -- the hard-tune suite went from 50 s to 8.
        double kernel[2 * half];
        double wsum = 0.0;
        for (int k = -half + 1; k <= half; ++k)
        {
            const auto t = frac - k;
            const auto sinc = std::abs (t) < 1.0e-12 ? 1.0 : std::sin (kPi * t) / (kPi * t);
            const auto w = 0.5 + 0.5 * std::cos (kPi * t / half);
            kernel[k + half - 1] = sinc * w;
            wsum += sinc * w;
        }

        for (int j = half; j + whole + half < (int) length && j < window; ++j)
        {
            double v = 0.0;
            for (int k = -half + 1; k <= half; ++k)
                v += x[start + (size_t) (j + whole + k)] * kernel[k + half - 1];
            const auto e = x[start + (size_t) j] - v / wsum;
            err += e * e;
        }

        return err;
    };

    double a = period - 0.6, b = period + 0.6;
    const auto g = 0.5 * (std::sqrt (5.0) - 1.0);
    double c1 = b - g * (b - a), c2 = a + g * (b - a);
    double f1 = shifted (c1), f2 = shifted (c2);

    for (int it = 0; it < 40; ++it)
    {
        if (f1 < f2) { b = c2; c2 = c1; f2 = f1; c1 = b - g * (b - a); f1 = shifted (c1); }
        else         { a = c1; c1 = c2; f1 = f2; c2 = a + g * (b - a); f2 = shifted (c2); }
    }

    return fs / (0.5 * (a + b));
}

/** Delay of y behind x in whole samples, by cross-correlation over
    [0, maxLag]. */
inline int delayOf (const std::vector<float>& x, const std::vector<float>& y, int maxLag)
{
    int best = 0;
    double bestValue = -1.0e300;
    const auto n = std::min (x.size(), y.size());

    for (int L = 0; L <= maxLag; ++L)
    {
        double s = 0.0;
        for (size_t i = (size_t) L; i < n; ++i)
            s += (double) y[i] * x[i - (size_t) L];
        if (s > bestValue)
        {
            bestValue = s;
            best = L;
        }
    }

    return best;
}

/** THD+N of a segment against the best-fit sinusoid at `hz` (amplitude and
    phase fitted by least squares), in dB relative to that sinusoid. */
inline double thdPlusNoiseDb (const std::vector<float>& x, size_t start, size_t length, double hz, double fs)
{
    double ss = 0.0, sc = 0.0, s2 = 0.0, c2 = 0.0, sxc = 0.0;
    const auto w = 2.0 * kPi * hz / fs;

    for (size_t i = 0; i < length; ++i)
    {
        const auto sv = std::sin (w * (double) i), cv = std::cos (w * (double) i);
        const double v = x[start + i];
        ss += v * sv; sc += v * cv; s2 += sv * sv; c2 += cv * cv; sxc += sv * cv;
    }

    // 2x2 normal equations; sxc is ~0 over many cycles but solved anyway.
    const auto det = s2 * c2 - sxc * sxc;
    const auto a = (ss * c2 - sc * sxc) / det;
    const auto b = (sc * s2 - ss * sxc) / det;

    double signal = 0.0, residual = 0.0;
    for (size_t i = 0; i < length; ++i)
    {
        const auto fit = a * std::sin (w * (double) i) + b * std::cos (w * (double) i);
        const auto e = x[start + i] - fit;
        signal += fit * fit;
        residual += e * e;
    }

    return 10.0 * std::log10 (std::max (residual, 1.0e-30) / std::max (signal, 1.0e-30));
}

inline double cents (double hzA, double hzB) { return 1200.0 * std::log2 (hzA / hzB); }

} // namespace bmo::tune::analysis
