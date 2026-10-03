#pragma once

#include <cstdint>
#include <cstring>

namespace bmo::finite
{

/** True if any of `n` samples is a NaN or an infinity.

    The test is on the bits rather than through `std::isfinite`, for speed and
    for certainty. A float is non-finite exactly when its eight exponent bits
    are all ones; adding one to the lowest exponent bit carries out of the
    exponent into the sign position for that pattern and for no other, so
    OR-ing `(bits & exponent) + lowest` over the block and looking at the top
    bit once is the whole test. That is three integer operations per sample
    with no branch, which a compiler turns into a vector loop, and it cannot be
    folded away by a fast-maths setting the way `x - x != 0` can. */
inline bool anyNonFinite (const float* x, int n) noexcept
{
    constexpr std::uint32_t exponent = 0x7f800000u;
    constexpr std::uint32_t lowest   = 0x00800000u;

    std::uint32_t acc = 0;

    for (int i = 0; i < n; ++i)
    {
        std::uint32_t bits;
        std::memcpy (&bits, x + i, sizeof bits);
        acc |= (bits & exponent) + lowest;
    }

    return (acc & 0x80000000u) != 0;
}

inline bool anyNonFinite (const float* const* channels, int numChannels, int numSamples) noexcept
{
    for (int ch = 0; ch < numChannels; ++ch)
        if (anyNonFinite (channels[ch], numSamples))
            return true;

    return false;
}

/** Writes zero over every NaN and infinity in the block, and touches nothing
    else. Returns whether it found one.

    Zero, rather than the previous sample or the nearest full-scale value: a
    non-finite sample carries no level to keep, an infinity clamped to full
    scale would hand a compressor's detector a 0 dBFS spike it never heard,
    and zero needs no state carried between blocks. One zeroed sample in a
    signal is what a module then makes of a one-sample gap.

    The scan is read-only and the rewrite runs only on a channel that has a bad
    sample in it, so a finite block costs one pass of reads and its samples are
    left bit for bit as they were. */
inline bool scrub (float* const* channels, int numChannels, int numSamples) noexcept
{
    bool found = false;

    for (int ch = 0; ch < numChannels; ++ch)
    {
        auto* x = channels[ch];

        if (! anyNonFinite (x, numSamples))
            continue;

        found = true;

        for (int i = 0; i < numSamples; ++i)
            if (anyNonFinite (x + i, 1))
                x[i] = 0.0f;
    }

    return found;
}

} // namespace bmo::finite
