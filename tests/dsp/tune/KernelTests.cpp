/*
    The recursive E/H kernel (spec §3.1, tests T-4).

    The claims: the recursion computes exactly the sums its definition says,
    it does not drift over ten million samples, the refresh schedule never
    bunches, and one bad sample cannot poison the state. The last two are the
    ones the spec calls out as "bugs we already know are coming".
*/

#include "modules/tune/dsp/DifferenceKernel.h"
#include "modules/tune/dsp/Detector.h"
#include "tests/dsp/tune/TestUtil.h"
#include "tools/tune/common/Signals.h"

#include <limits>

using namespace bmo::tune;
using namespace bmo::tune::test;

namespace
{
    /** Worst relative gap between the running sums and a from-scratch
        recompute, across every lag. Relative to the two-period energy, since
        H can legitimately be zero. */
    double worstDrift (const DifferenceKernel& k)
    {
        double worst = 0.0;

        for (int L = k.getMinLag(); L <= k.getMaxLag(); ++L)
        {
            double e = 0.0, h = 0.0;
            k.exactAt (L, e, h);
            const auto scale = std::max (e, 1.0e-30);
            worst = std::max (worst, std::abs (k.energyAt (L) - e) / scale);
            worst = std::max (worst, std::abs (k.crossAt (L) - h) / scale);
        }

        return worst;
    }

    bool energiesNonNegative (const DifferenceKernel& k)
    {
        for (int L = k.getMinLag(); L <= k.getMaxLag(); ++L)
            if (! (k.energyAt (L) >= 0.0))
                return false;
        return true;
    }
}

int main()
{
    //== The recursion is the definition =======================================
    {
        DifferenceKernel k;
        k.prepare (4, 64, 1 << 30);   // no refresh: test the recursion alone
        const auto noise = signals::whiteNoise (5000, 0.5, 7);

        for (auto v : noise)
            k.push (v);

        check (worstDrift (k) < 1.0e-12, "running E and H equal their definitions after 5000 samples");

        // And the three detectors really are one: E - 2H is the squared
        // difference over window L.
        double direct = 0.0;
        const int L = 20;
        for (int j = 0; j < L; ++j)
        {
            const double a = noise[noise.size() - 1 - (size_t) j];
            const double b = noise[noise.size() - 1 - (size_t) (j + L)];
            direct += (a - b) * (a - b);
        }
        check (near (k.differenceAt (L), direct, 1.0e-9 * direct),
               "E - 2H is YIN's squared difference (the spec §3.1 derivation)");
    }

    //== Ten million samples of pink noise =====================================
    {
        DifferenceKernel k;
        k.prepare (3, 160, 3000);
        const auto pink = signals::pinkNoise (10'000'000, 1.0, 11);

        bool neverNegative = true;
        int worstPerPush = 0;
        double worstMidway = 0.0;

        for (size_t i = 0; i < pink.size(); ++i)
        {
            k.push (pink[i]);
            worstPerPush = std::max (worstPerPush, k.lagsRecomputedByLastPush());

            if ((i & 0xFFFFF) == 0xFFFFF)
            {
                neverNegative = neverNegative && energiesNonNegative (k);
                worstMidway = std::max (worstMidway, worstDrift (k));
            }
        }

        const auto finalDrift = worstDrift (k);
        report ("E/H relative drift after 1e7 pink samples", finalDrift);
        report ("worst drift sampled along the way", worstMidway);
        check (finalDrift < 1.0e-9, "running sums within 1e-9 of a recompute after 1e7 samples (T-4)");
        check (worstMidway < 1.0e-9, "and at every checkpoint on the way there");
        check (neverNegative, "E stays >= 0 throughout");
        check (worstPerPush <= 1, "the staggered refresh never recomputes more than one lag per push");
    }

    //== Loud, then quiet: the case that actually breaks recursions ===========
    {
        DifferenceKernel k;
        k.prepare (3, 160, 3000);
        const auto loud = signals::whiteNoise (200'000, 1.0, 3);
        const auto quiet = signals::whiteNoise (200'000, 1.0e-5, 4);

        for (auto v : loud)  k.push (v);
        for (auto v : quiet) k.push (v);

        report ("relative drift after -100 dB step", worstDrift (k));
        check (worstDrift (k) < 1.0e-6,
               "a -100 dB step leaves no loud residue once the schedule has refreshed every lag");
    }

    //== One bad sample ========================================================
    {
        DifferenceKernel k;
        k.prepare (3, 100, 3000);
        const auto noise = signals::whiteNoise (4000, 0.5, 5);

        for (size_t i = 0; i < noise.size(); ++i)
        {
            float v = noise[i];
            if (i == 1000) v = std::numeric_limits<float>::quiet_NaN();
            if (i == 1001) v = std::numeric_limits<float>::infinity();
            k.push (v);
        }

        bool finite = true;
        for (int L = 3; L <= 100; ++L)
            finite = finite && std::isfinite (k.energyAt (L)) && std::isfinite (k.crossAt (L));

        check (finite, "NaN and Inf input never reach the running sums");
        check (worstDrift (k) < 1.0e-9, "and the state afterwards is the state of the clean signal");
    }

    //== Parabolic interpolation ===============================================
    {
        // Exact vertex recovery on a true parabola.
        const auto f = [] (double x) { return 3.0 - 2.0 * (x - 0.3) * (x - 0.3); };
        check (near (Detector::parabolicOffset (f (-1.0), f (0.0), f (1.0)), 0.3, 1.0e-12),
               "parabolic interpolation recovers a parabola's vertex exactly");

        const auto g = [] (double x) { return 1.0 + 5.0 * (x + 0.45) * (x + 0.45); };
        check (near (Detector::parabolicOffset (g (-1.0), g (0.0), g (1.0)), -0.45, 1.0e-12),
               "and a minimum's, with the same formula");

        check (Detector::parabolicOffset (1.0, 1.0, 1.0) == 0.0, "a flat run gives 0, not 0/0");
        check (Detector::parabolicOffset (0.0, 1.0, 2.0) == 0.0, "a straight line gives 0, not 0/0");
        check (std::abs (Detector::parabolicOffset (0.0, 1.0e-300, 0.0)) <= 1.0, "a near-zero denominator stays bounded");
        const auto nan = std::numeric_limits<double>::quiet_NaN();
        check (Detector::parabolicOffset (nan, 1.0, 0.0) == 0.0, "a NaN neighbour gives 0");
    }

    return finish ("kernel");
}
