#pragma once

#include "modules/tune/dsp/Pitch.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace bmo::tune
{

/** Polyphase Kaiser-windowed sinc for fractional reads (spec §5.5).

    `Taps` coefficients per phase, `kPhases` phases, linear interpolation
    between adjacent phases -- 256 phases alone leave a time quantisation of
    1/256 sample, which at 0.4 fs is a -40 dB error; interpolating between
    rows pushes that below the kernel's own error.

    Built once at prepare time (it allocates); read on the audio thread.

    `cutoff` is a fraction of Nyquist. At 1.0 with a whole-sample position the
    kernel is a pure delay -- every other tap lands on a zero of the sinc --
    which is what lets the engines pass audio bit-exactly while they are not
    correcting. Below 1.0 it is also the anti-aliasing filter spec §5.4 asks
    for when the read runs faster than the write: content above fs / (2 rho)
    is folded back by a read at rate rho, so it has to be gone first.
*/
template <int Taps>
class SincTable
{
public:
    static_assert (Taps % 2 == 0, "an even tap count centres the kernel between samples");

    static constexpr int kTaps = Taps;
    static constexpr int kHalf = Taps / 2;
    static constexpr int kPhases = 256;

    /** Whole samples the newest input must be ahead of a read position. */
    static constexpr int kLookahead = kHalf;

    void build (double cutoff, double beta)
    {
        table.assign ((size_t) (kPhases + 1) * kTaps, 0.0f);
        const auto i0Beta = besselI0 (beta);

        for (int p = 0; p <= kPhases; ++p)
        {
            const auto frac = (double) p / kPhases;
            double row[Taps];
            double sum = 0.0;

            for (int k = 0; k < Taps; ++k)
            {
                // Tap k reads sample (i - kHalf + 1 + k); its distance from
                // the read position i + frac is t.
                const auto t = frac + (kHalf - 1 - k);
                const auto x = cutoff * t;

                // sin(pi n) in double is ~1e-16 n, not 0, so without this the
                // "zero" taps of a whole-sample read are 1e-17 and the read is
                // not an exact copy -- which the passthrough test caught.
                const auto nearestInteger = std::round (x);
                const auto onZero = nearestInteger != 0.0 && std::abs (x - nearestInteger) < 1.0e-12;
                const auto sinc = std::abs (x) < 1.0e-12 ? 1.0
                                : onZero ? 0.0 : std::sin (kPi * x) / (kPi * x);
                const auto r = t / kHalf;
                const auto w = std::abs (r) < 1.0 ? besselI0 (beta * std::sqrt (1.0 - r * r)) / i0Beta : 0.0;

                row[k] = cutoff * sinc * w;
                sum += row[k];
            }

            // Unity at DC on every phase, so a fractional read never ripples
            // the level as the phase moves.
            for (int k = 0; k < Taps; ++k)
                table[(size_t) (p * Taps + k)] = (float) (row[k] / sum);
        }
    }

    /** Reads `ring` (power-of-two size, `mask`) at fractional index
        `position`. The caller guarantees position + kHalf has been written. */
    float read (const float* ring, int mask, double position) const noexcept
    {
        const auto whole = std::floor (position);
        const auto frac = position - whole;
        const auto i = (int) (long long) whole;

        const auto phase = frac * kPhases;
        const auto p = std::min ((int) phase, kPhases - 1);
        const auto mix = (float) (phase - p);

        const float* a = table.data() + (size_t) p * Taps;
        const float* b = a + Taps;
        const auto base = i - kHalf + 1;

        float accA = 0.0f, accB = 0.0f;

        for (int k = 0; k < Taps; ++k)
        {
            const auto x = ring[(size_t) ((base + k) & mask)];
            accA += a[k] * x;
            accB += b[k] * x;
        }

        return accA + mix * (accB - accA);
    }

    static double besselI0 (double x)
    {
        // Power series; converges fast for the betas a window uses.
        double sum = 1.0, term = 1.0;
        const auto half = 0.5 * x;

        for (int k = 1; k < 64; ++k)
        {
            term *= (half / k) * (half / k);
            sum += term;
            if (term < 1.0e-17 * sum)
                break;
        }

        return sum;
    }

private:
    std::vector<float> table;
};

/** A bank of 32-tap kernels for reads at rate rho, shared by both engines
    (spec §5.4). A read at rate rho folds content at f to fs - rho f, which
    cannot land below 20 kHz until rho passes 2 (1 - 20 kHz / fs): +267 cents
    at 48 kHz, +154 at 44.1, and never within +/-1200 cents at 88.2 and up.
    Below that ratio the full-band kernel -- the one that is a pure delay at a
    whole-sample position -- is kept; above it, one kernel per 100 cents, cut
    to 0.90 / rho (measured: -68 dB of alias on a full-band sawtooth at
    +100 cents, -0.29 dB at 18 kHz; see tests/dsp/InterpolatorTests.cpp). */
class SincBank
{
public:
    using Table = SincTable<32>;

    void build (double sampleRate)
    {
        fullBandRatio = std::max (1.0, 2.0 * (1.0 - 20000.0 / sampleRate));
        tables[0].build (1.0, 8.0);

        for (int b = 1; b < (int) tables.size(); ++b)
        {
            const auto upper = std::exp2 (b * 100.0 / 1200.0);
            tables[(size_t) b].build (upper <= fullBandRatio ? 1.0 : 0.90 / upper, 8.0);
        }
    }

    const Table& forRatio (double rho) const noexcept
    {
        if (rho <= 1.0)
            return tables[0];

        const auto cents = 1200.0 * std::log2 (rho);
        const auto b = std::clamp ((int) std::ceil (cents / 100.0), 1, (int) tables.size() - 1);
        return tables[(size_t) b];
    }

private:
    std::array<Table, 13> tables;
    double fullBandRatio = 1.0;
};

} // namespace bmo::tune
