#pragma once

#include <cmath>
#include <cstdint>

namespace bmo::tune
{

/** Which pitch classes a correction may land on (spec §4.1).

    A scale is a 12-bit mask, bit 0 = C, in every octave. Key and scale type
    build one; the user's per-note allow map is a mask too, and the quantizer
    only ever sees the intersection. */
using NoteMask = std::uint16_t;

inline constexpr NoteMask kAllNotes = 0x0FFF;

/** In the order of the Scale parameter's choices. New scales go on the end,
    so a saved session's index keeps meaning the scale it meant. */
enum class ScaleType
{
    chromatic, major, minor,
    count
};

/** The scale's intervals from its tonic, as a mask with the tonic at bit 0. */
inline NoteMask intervalsOf (ScaleType type) noexcept
{
    const auto bits = [] (std::initializer_list<int> steps)
    {
        NoteMask m = 0;
        for (auto s : steps)
            m = (NoteMask) (m | (1u << s));
        return m;
    };

    switch (type)
    {
        case ScaleType::chromatic: return kAllNotes;
        case ScaleType::major:     return bits ({ 0, 2, 4, 5, 7, 9, 11 });
        case ScaleType::minor:     return bits ({ 0, 2, 3, 5, 7, 8, 10 });   // natural minor
        case ScaleType::count:     break;
    }

    return kAllNotes;
}

/** The scale rotated so its tonic sits on `key` (0 = C ... 11 = B). */
inline NoteMask scaleMask (ScaleType type, int key) noexcept
{
    const auto intervals = (unsigned) intervalsOf (type);
    const auto k = ((key % 12) + 12) % 12;
    return (NoteMask) (((intervals << k) | (intervals >> (12 - k))) & kAllNotes);
}

inline bool allows (NoteMask mask, int note) noexcept
{
    return (mask >> (((note % 12) + 12) % 12)) & 1u;
}

/** Nearest allowed note to a continuous pitch.

    Ties and near-ties go to the note already held: `hysteresisCents` is how
    much closer another note has to be before the target leaves the held one.
    Zero is the spec's strict rule (an exact 50-cent tie resolves toward the
    held note, never toward a fixed direction, which flip-flops on a slow
    glide); the default is a few cents more than that, because an input
    sitting within a couple of cents of the midpoint jitters across it on
    every frame and each crossing is an audible note change.

    Returns false, and leaves `note` alone, when the mask allows nothing --
    an empty scale means "no correction", not "correct to somewhere". */
inline bool nearestAllowed (double semitones, NoteMask mask, int heldNote, bool haveHeld,
                            double hysteresisCents, int& note) noexcept
{
    if ((mask & kAllNotes) == 0 || ! std::isfinite (semitones))
        return false;

    const auto centre = (int) std::floor (semitones);
    int best = 0;
    double bestDistance = 1.0e9;

    // Six either side reaches any allowed pitch class from anywhere.
    for (int n = centre - 6; n <= centre + 7; ++n)
    {
        if (! allows (mask, n))
            continue;

        const auto d = std::abs (semitones - n);

        // Strictly closer wins; an exact tie keeps the earlier (lower) note,
        // and the held-note rule below overrides that whenever there is one.
        if (d < bestDistance)
        {
            bestDistance = d;
            best = n;
        }
    }

    if (haveHeld && allows (mask, heldNote))
    {
        const auto heldDistance = std::abs (semitones - heldNote);

        if (heldDistance <= bestDistance + hysteresisCents * 0.01)
            best = heldNote;
    }

    note = best;
    return true;
}

} // namespace bmo::tune
