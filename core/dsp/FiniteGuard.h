#pragma once

#include <cstdint>
#include <cstring>

namespace bmo::finite
{

/** The smallest magnitude the guard refuses as audio: 2^32, +192.7 dBFS.

    **A finite sample can do what an infinity does.** Measured on ICE QUEEN,
    2026-10-03, through every module's engine: one sample at 1e10 (+200 dBFS)
    held BMO Opto and BMO FET 40 dB down for 60 s and 15 s, at 1e20 or more for
    over a minute, and CEQ, Saturator, FET, Dwell and Linger put out finite
    samples up to +744 dBFS for the host to sum. The bigger the sample, the
    longer the hold: no module has to overflow to be silenced by one.

    **Why so high, when +60 dBFS would let every module back within 4 s:**
    legal settings make signals far louder than any mix carries, and a guard
    must not change what a control does. Every parameter at 0.63 of its range
    (bus_tests' swept setting) puts +85.6 dBFS out of BMO DEQ alone and
    +119.4 dBFS between two slots of the full rack, which BMO LTV Comp brings
    back to -0.1 dBFS; a ceiling at +60 moved that rack's output 4.7 dB and
    failed its golden. +192.7 dBFS is 73 dB over the loudest signal the
    suite's own settings make, and under every overload measured above. A
    sample just under it is the module's to answer -- Opto may hold for most
    of a minute -- which is a choice about those modules, not about the
    guard. A power of two so that the test stays an exponent compare (see
    `anyAtOrOver`). */
inline constexpr float kCeiling = 4294967296.0f;

namespace detail
{
    /** True if any of `n` samples has a biased exponent of `lowestBad` or
        more: 255 is NaN and infinity, 159 is |x| >= 2^32.

        The test is on the bits rather than through `std::isfinite` or a
        compare, for speed and for certainty. Adding `256 - lowestBad` to the
        eight exponent bits carries into the sign position exactly when the
        exponent is `lowestBad` or more (the sum never reaches a ninth bit of
        overflow, since 255 + 255 < 512), so OR-ing that sum over the block and
        looking at the top bit once is the whole test. Three integer
        operations per sample with no branch, which a compiler turns into a
        vector loop, and nothing a fast-maths setting can fold away the way it
        can `x - x != 0`. */
    template <std::uint32_t lowestBad>
    inline bool anyAtOrOver (const float* x, int n) noexcept
    {
        static_assert (lowestBad >= 1 && lowestBad <= 255);

        constexpr std::uint32_t exponent = 0x7f800000u;
        constexpr std::uint32_t carry    = (256u - lowestBad) << 23;

        std::uint32_t acc = 0;

        for (int i = 0; i < n; ++i)
        {
            std::uint32_t bits;
            std::memcpy (&bits, x + i, sizeof bits);
            acc |= (bits & exponent) + carry;
        }

        return (acc & 0x80000000u) != 0;
    }

    /** 2^32's biased exponent is 127 + 32. */
    inline constexpr std::uint32_t kCeilingExponent = 159;
    static_assert (kCeiling == 4294967296.0f, "kCeilingExponent is 2^32's; change both together");
}

/** True if any of `n` samples is a NaN or an infinity. */
inline bool anyNonFinite (const float* x, int n) noexcept
{
    return detail::anyAtOrOver<255> (x, n);
}

inline bool anyNonFinite (const float* const* channels, int numChannels, int numSamples) noexcept
{
    for (int ch = 0; ch < numChannels; ++ch)
        if (anyNonFinite (channels[ch], numSamples))
            return true;

    return false;
}

/** True if any of `n` samples is not audio: a NaN, an infinity, or a finite
    sample at or over `kCeiling` in either sign. */
inline bool anyNotAudio (const float* x, int n) noexcept
{
    return detail::anyAtOrOver<detail::kCeilingExponent> (x, n);
}

/** Writes zero over every sample that is not audio -- NaN, infinity, and a
    magnitude at or over `kCeiling` -- and touches nothing else. Returns
    whether it found one.

    Zero, rather than the previous sample or the nearest full-scale value: a
    bad sample carries no level to keep, one clamped to full scale or to the
    ceiling would hand a compressor's detector a spike it never heard, and
    zero needs no state carried between blocks. One zeroed sample in a signal
    is what a module then makes of a one-sample gap.

    The scan is read-only and the rewrite runs only on a channel that has a bad
    sample in it, so a clean block costs one pass of reads and its samples are
    left bit for bit as they were. */
inline bool scrub (float* const* channels, int numChannels, int numSamples) noexcept
{
    bool found = false;

    for (int ch = 0; ch < numChannels; ++ch)
    {
        auto* x = channels[ch];

        if (! anyNotAudio (x, numSamples))
            continue;

        found = true;

        for (int i = 0; i < numSamples; ++i)
            if (anyNotAudio (x + i, 1))
                x[i] = 0.0f;
    }

    return found;
}

} // namespace bmo::finite
