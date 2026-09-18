#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace bmo::tune
{

/** The recursive two-period energy / lag-correlation state, one pair per
    candidate lag, updated at O(1) per lag per sample (spec §3.1).

    For lag L at sample i:

        E_i(L) = E_{i-1}(L) + x_i^2       - x_{i-2L}^2         energy over 2L
        H_i(L) = H_{i-1}(L) + x_i x_{i-L} - x_{i-L} x_{i-2L}   lag-L correlation over L

    and three detectors fall out of the same pair:

        E - 2H      = sum (x_j - x_{j-L})^2       YIN's difference, window L
        (E - 2H)/E  <= eps                       the 1998 patent's match test
        2H / E      in [-1, 1]                    McLeod's NSDF, whose peak is clarity

    because E over the 2L span is exactly McLeod's m'(L) re-associated. So
    this class is the one kernel the spec asks for, and "which detector" is
    only ever a question of which normaliser the caller reads.

    Numerics (spec §3.1's warning, and test T-4): sums that add and subtract
    for millions of samples drift, and after a loud passage the residue can
    exceed the energy of the quiet one that follows. Three defences:

      - Everything is double, and the history is stored as double copies of
        the float input. A float squared is exact in double (24 + 24 < 53
        bits), so the value subtracted is bit-identical to the value added
        and only the running additions round.
      - E is clamped to >= 0 and |H| to <= E/2, which Cauchy-Schwarz
        guarantees for the true sums.
      - Every lag is recomputed from scratch on a staggered schedule, at most
        one lag per push, so the refresh cost never lands on one block.
*/
class DifferenceKernel
{
public:
    /** Allocates. `refreshPeriod` is the number of pushes within which every
        lag is recomputed from scratch at least once. */
    void prepare (int minLagIn, int maxLagIn, int refreshPeriodIn)
    {
        capacityMin = std::max (1, minLagIn);
        capacityMax = std::max (capacityMin, maxLagIn);

        const auto numLags = capacityMax - capacityMin + 1;
        energy.assign ((size_t) numLags, 0.0);
        cross.assign ((size_t) numLags, 0.0);

        // Power of two so the ring index is a mask. 2L back is the oldest
        // sample the recursion touches; one spare keeps "i - 2L" in the ring
        // while sample i is being written.
        int size = 1;
        while (size < 2 * capacityMax + 2)
            size <<= 1;

        history.assign ((size_t) size, 0.0);
        mask = size - 1;
        refreshPeriod = refreshPeriodIn;
        writeIndex = 0;

        setLagRange (capacityMin, capacityMax);
        reset();
    }

    /** Narrows the active lags to a sub-range of what prepare() allocated,
        without allocating -- a pitch-range change arrives on the audio
        thread. Resets the sums; the history is kept, so a recomputeAll()
        afterwards picks up where the signal is. */
    void setLagRange (int minLagIn, int maxLagIn) noexcept
    {
        minLag = std::clamp (minLagIn, capacityMin, capacityMax);
        maxLag = std::clamp (maxLagIn, minLag, capacityMax);

        // Spread the lags evenly across the period: one lag every
        // `refreshStride` pushes. With more lags than pushes in a period the
        // stride floors at one, which is still at most one lag per push -- the
        // period stretches instead of the per-push cost growing.
        refreshStride = std::max (1, refreshPeriod / (maxLag - minLag + 1));
        refreshCountdown = refreshStride;
        refreshLag = minLag;
        recomputeAll();
    }

    void reset() noexcept
    {
        std::fill (energy.begin(), energy.end(), 0.0);
        std::fill (cross.begin(), cross.end(), 0.0);
        std::fill (history.begin(), history.end(), 0.0);
        writeIndex = 0;
        pushCount = 0;
        refreshCountdown = refreshStride;
        refreshLag = minLag;
        lastPushRecomputed = 0;
    }

    /** One new sample. Real-time safe: no allocation, bounded work. */
    void push (float sample) noexcept
    {
        // A non-finite sample would poison every sum it ever touches, and the
        // recursion would carry it forward forever -- guard the state, not
        // just the output (spec §8). Zero is what the sums would have seen
        // from silence.
        const double x = std::isfinite (sample) ? (double) sample : 0.0;

        history[(size_t) writeIndex] = x;
        const auto xx = x * x;

        for (int L = minLag, k = minLag - capacityMin; L <= maxLag; ++L, ++k)
        {
            const auto xL  = at (L);
            const auto x2L = at (2 * L);

            auto e = energy[(size_t) k] + xx - x2L * x2L;
            auto h = cross[(size_t) k] + x * xL - xL * x2L;

            e = e > 0.0 ? e : 0.0;
            const auto hMax = 0.5 * e;
            h = h > hMax ? hMax : (h < -hMax ? -hMax : h);

            energy[(size_t) k] = e;
            cross[(size_t) k] = h;
        }

        ++pushCount;
        lastPushRecomputed = 0;

        if (--refreshCountdown <= 0)
        {
            refreshCountdown = refreshStride;
            recompute (refreshLag, writeIndex);
            lastPushRecomputed = 1;
            refreshLag = refreshLag >= maxLag ? minLag : refreshLag + 1;
        }

        writeIndex = (writeIndex + 1) & mask;
    }

    /** From scratch, every lag. Not for the audio thread's steady state --
        it is O(lags x L) -- but cheap enough for a voicing transition, which
        is when the spec asks for it. */
    void recomputeAll() noexcept
    {
        const auto newest = (writeIndex - 1) & mask;

        for (int L = minLag; L <= maxLag; ++L)
            recompute (L, newest);
    }

    //== Reading ===============================================================

    int getMinLag() const noexcept { return minLag; }
    int getMaxLag() const noexcept { return maxLag; }

    double energyAt (int L) const noexcept { return energy[(size_t) (L - capacityMin)]; }
    double crossAt  (int L) const noexcept { return cross [(size_t) (L - capacityMin)]; }

    /** YIN / patent difference, E - 2H: the squared difference over window L. */
    double differenceAt (int L) const noexcept
    {
        return std::max (0.0, energyAt (L) - 2.0 * crossAt (L));
    }

    /** McLeod's normalised square difference function, in [-1, 1]. Zero on
        silence rather than 0/0, because silence is not periodic. */
    double nsdfAt (int L, double energyFloor = 1.0e-12) const noexcept
    {
        const auto e = energyAt (L);
        return e > energyFloor ? 2.0 * crossAt (L) / e : 0.0;
    }

    /** How many lags the last push recomputed from scratch; the schedule
        test reads this to prove the refresh is never bunched. */
    int lagsRecomputedByLastPush() const noexcept { return lastPushRecomputed; }

    /** The same sums, computed from scratch, for the drift test. */
    void exactAt (int L, double& e, double& h) const noexcept
    {
        // Sample i is the one most recently written.
        e = 0.0; h = 0.0;
        for (int j = 0; j < 2 * L; ++j)
        {
            const auto v = back (j);
            e += v * v;
        }
        for (int j = 0; j < L; ++j)
            h += back (j) * back (j + L);
    }

    std::int64_t samplesPushed() const noexcept { return pushCount; }

private:
    /** x_{i-d} while sample i is being written (writeIndex not yet advanced). */
    double at (int d) const noexcept { return history[(size_t) ((writeIndex - d) & mask)]; }

    /** x_{i-d} after the push completed. */
    double back (int d) const noexcept { return history[(size_t) ((writeIndex - 1 - d) & mask)]; }

    /** The sums for lag L, from scratch, with the newest sample at ring index
        `newest` -- which is writeIndex inside push() and writeIndex - 1
        outside it. Passing it explicitly is what keeps the two callers from
        disagreeing about which sample is "now". */
    void recompute (int L, int newest) noexcept
    {
        const auto x = [this, newest] (int d) { return history[(size_t) ((newest - d) & mask)]; };
        double e = 0.0, h = 0.0;

        for (int j = 0; j < 2 * L; ++j)
        {
            const auto v = x (j);
            e += v * v;
        }

        for (int j = 0; j < L; ++j)
            h += x (j) * x (j + L);

        energy[(size_t) (L - capacityMin)] = e;
        cross[(size_t) (L - capacityMin)] = h;
    }

    int capacityMin = 1, capacityMax = 1, refreshPeriod = 1;
    int minLag = 1, maxLag = 1;
    std::vector<double> energy, cross, history;
    int mask = 0, writeIndex = 0;
    std::int64_t pushCount = 0;
    int refreshStride = 1, refreshCountdown = 1, refreshLag = 1, lastPushRecomputed = 0;
};

} // namespace bmo::tune
