/*
    The fractional-delay kernel (spec §5.5), measured rather than asserted
    from a table.

    The spec's table gives "< -100 dB error at 0.4 fs" for a 16-tap, 32-phase
    windowed sinc. Kaiser's own design formula says a 16-tap kernel's
    transition band is about a third of the band wide, so that figure cannot
    hold; this file measures what each candidate actually does and holds the
    engine's choice to its measured number, not the spec's.

    Error is the worst over 64 fractional positions of the read against the
    exact band-limited sine at that position, relative to the sine's peak.
*/

#include "modules/tune/dsp/SincTable.h"
#include "tests/dsp/tune/TestUtil.h"

#include <cstdio>
#include <string>
#include <vector>

using namespace bmo::tune;
using namespace bmo::tune::test;

namespace
{
    constexpr int kRing = 8192;

    template <int Taps>
    double worstErrorDb (const SincTable<Taps>& table, double freqOverFs)
    {
        std::vector<float> ring (kRing);
        const auto w = 2.0 * kPi * freqOverFs;

        for (int n = 0; n < kRing; ++n)
            ring[(size_t) n] = (float) std::sin (w * n + 0.3);

        double worst = 0.0;

        for (int f = 0; f < 64; ++f)
        {
            // Offsets that are not multiples of 1/256, so the between-phase
            // interpolation is exercised as well as the table.
            const auto position = 4000.0 + (f + 0.37) / 64.0;
            const auto got = table.read (ring.data(), kRing - 1, position);
            const auto want = std::sin (w * position + 0.3);
            worst = std::max (worst, std::abs (got - want));
        }

        return 20.0 * std::log10 (std::max (worst, 1.0e-12));
    }

    template <int Taps>
    void survey (double beta)
    {
        SincTable<Taps> t;
        t.build (1.0, beta);

        char buf[96];
        for (auto f : { 0.10, 0.25, 0.35, 0.40, 0.45 })
        {
            std::snprintf (buf, sizeof buf, "%d taps, beta %.0f: worst error at %.2f fs", Taps, beta, f);
            report (buf, worstErrorDb (t, f), "dB");
        }
    }

    /** Energy a read at rate rho puts where a band-limited input has none:
        a full-band sawtooth stepped through the table at rho, spectrum
        measured by a Goertzel at every bin that is not a harmonic of the
        shifted fundamental. */
    /** The kernel's gain at a frequency, averaged over read phases: the
        passband cost of a lowered cutoff. */
    template <int Taps>
    double gainDb (const SincTable<Taps>& t, double freqOverFs)
    {
        std::vector<float> ring (kRing);
        for (int n = 0; n < kRing; ++n)
            ring[(size_t) n] = (float) std::sin (2.0 * kPi * freqOverFs * n);

        // Amplitude of the read-back sine by least squares against sin/cos
        // at the same frequency, over a run of consecutive reads.
        double ss = 0.0, sc = 0.0, s2 = 0.0, c2 = 0.0;
        for (int i = 0; i < 2048; ++i)
        {
            const auto pos = 3000.0 + i + 0.43;
            const auto y = (double) t.read (ring.data(), kRing - 1, pos);
            const auto sv = std::sin (2.0 * kPi * freqOverFs * pos);
            const auto cv = std::cos (2.0 * kPi * freqOverFs * pos);
            ss += y * sv; sc += y * cv; s2 += sv * sv; c2 += cv * cv;
        }

        const auto a = ss / s2, b = sc / c2;
        return 20.0 * std::log10 (std::sqrt (a * a + b * b));
    }

    template <int Taps>
    double aliasingDb (double cutoff, double rho, double f0OverFs, double beta = 10.0)
    {
        SincTable<Taps> t;
        t.build (cutoff, beta);

        const int n = 1 << 16;
        std::vector<float> ring (1 << 17);

        for (size_t i = 0; i < ring.size(); ++i)
        {
            double v = 0.0;
            for (int k = 1; k * f0OverFs < 0.499; ++k)
                v += std::sin (2.0 * kPi * k * f0OverFs * (double) i) / k;
            ring[i] = (float) (0.5 * v);
        }

        std::vector<double> out ((size_t) n);
        double pos = 1000.0;
        for (int i = 0; i < n; ++i, pos += rho)
            out[(size_t) i] = t.read (ring.data(), (int) ring.size() - 1, pos);

        // Harmonics of rho f0 are wanted. Harmonic k lands at rho k f0, and if
        // that is past Nyquist it folds to 1 - rho k f0, which is off the
        // harmonic grid -- so the aliases are at known frequencies and are
        // measured there. (A scan on a fixed grid stepped straight over them
        // on the first attempt and reported -100 dB for everything.)
        const auto fOut = rho * f0OverFs;
        double worst = 0.0, fundamental = 0.0;

        const auto goertzel = [&out, n] (double f)
        {
            double re = 0.0, im = 0.0;
            for (int i = 0; i < n; ++i)
            {
                const auto hann = 0.5 - 0.5 * std::cos (2.0 * kPi * i / (n - 1));
                re += hann * out[(size_t) i] * std::cos (2.0 * kPi * f * i);
                im -= hann * out[(size_t) i] * std::sin (2.0 * kPi * f * i);
            }
            return std::sqrt (re * re + im * im);
        };

        fundamental = goertzel (fOut);

        for (int k = 1; k * f0OverFs < 0.499; ++k)
        {
            const auto shifted = rho * k * f0OverFs;
            if (shifted <= 0.5)
                continue;

            const auto alias = 1.0 - shifted;
            if (alias > 0.0)
                worst = std::max (worst, goertzel (alias));
        }

        return 20.0 * std::log10 (worst / fundamental);
    }
}

int main()
{
    //== Survey ================================================================
    for (auto beta : { 6.0, 8.0, 10.0 })
    {
        survey<16> (beta);
        survey<32> (beta);
    }

    //== Identity at whole-sample positions ====================================
    {
        SincTable<32> t;
        t.build (1.0, 8.0);
        std::vector<float> ring (4096);
        for (size_t i = 0; i < ring.size(); ++i)
            ring[i] = (float) std::sin (0.37 * (double) i) * 0.7f;

        bool exact = true;
        for (int i = 100; i < 200; ++i)
            exact = exact && t.read (ring.data(), 4095, (double) i) == ring[(size_t) i];

        check (exact, "a whole-sample read at cutoff 1.0 is the sample itself, bit for bit");
    }

    //== DC and level ==========================================================
    {
        SincTable<32> t;
        t.build (0.9, 8.0);
        std::vector<float> ring (4096, 0.25f);
        double worst = 0.0;
        for (int f = 0; f < 100; ++f)
            worst = std::max (worst, std::abs (t.read (ring.data(), 4095, 1000.0 + f / 100.0) - 0.25));
        check (worst < 1.0e-6, "DC passes at unity on every phase");
    }

    //== Aliasing at +100 cents (spec §5.4, T-3) ==============================
    {
        const auto rho = std::exp2 (100.0 / 1200.0);
        report ("sawtooth +100 c, cutoff 1.0: worst alias", aliasingDb<32> (1.0, rho, 220.0 / 48000.0), "dB");

        char buf[128];
        for (auto beta : { 8.0, 10.0 })
            for (auto k : { 0.97, 0.90, 0.85, 0.80 })
            {
                SincTable<32> t;
                t.build (k / rho, beta);
                std::snprintf (buf, sizeof buf, "beta %.0f cutoff %.2f/rho: alias", beta, k);
                report (buf, aliasingDb<32> (k / rho, rho, 220.0 / 48000.0, beta), "dB");
                std::snprintf (buf, sizeof buf, "beta %.0f cutoff %.2f/rho: gain at 16 kHz / 18 kHz", beta, k);
                std::printf ("  %-58s %6.2f / %6.2f dB\n", buf, gainDb (t, 16000.0 / 48000.0), gainDb (t, 18000.0 / 48000.0));
            }
    }

    return finish ("interpolator");
}
