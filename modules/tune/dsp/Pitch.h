#pragma once

#include <cmath>

namespace bmo::tune
{

inline constexpr double kPi = 3.14159265358979323846;

/** The conversions every other file in the core uses, in one place (spec §1).

    Pitch is carried as continuous MIDI-style semitones everywhere inside the
    core, because that is the unit the scale quantizer, the vibrato split and
    the glide all work in; hertz exist only at the detector's output and
    samples only at the resynthesis engines' input. Keeping the conversions
    here, and nowhere else, is what lets a test pin the round trip once. */
namespace pitch
{
    /** Hz to continuous semitones, 69 = the reference A. */
    inline double semitonesFromHz (double hz, double refA = 440.0) noexcept
    {
        return 69.0 + 12.0 * std::log2 (hz / refA);
    }

    inline double hzFromSemitones (double semis, double refA = 440.0) noexcept
    {
        return refA * std::exp2 ((semis - 69.0) / 12.0);
    }

    /** Signed distance from b to a, in cents. */
    inline double centsBetween (double hzA, double hzB) noexcept
    {
        return 1200.0 * std::log2 (hzA / hzB);
    }

    /** A correction in cents as a resample ratio: > 1 raises pitch. */
    inline double ratioFromCents (double cents) noexcept
    {
        return std::exp2 (cents / 1200.0);
    }

    inline double centsFromRatio (double ratio) noexcept
    {
        return 1200.0 * std::log2 (ratio);
    }

    /** A period in samples, as hertz. */
    inline double hzFromPeriod (double periodSamples, double sampleRate) noexcept
    {
        return sampleRate / periodSamples;
    }

    inline double periodFromHz (double hz, double sampleRate) noexcept
    {
        return sampleRate / hz;
    }
}

} // namespace bmo::tune
