#pragma once

namespace bmo::reverb
{

//==============================================================================
/** The early-reflection table: the contract between the tap generator and the
    ER engine.

    M2 is built in two halves at once (docs/reverb/HANDOFF-linger-dsp.md). One
    half derives the tables offline from the image-source method and audits
    them against the comb, flam, mono and lateral rules (10 section 3, 11
    section 6); the other half is the engine that plays them. This header is
    the one thing both halves compile against, so that neither has to guess at
    the other's shape. **Fields may be added; none may be renamed or removed
    without both halves agreeing**, because that is the point of having it.

    JUCE-free and panel-includable, like `TapTables.h`: the panel's scatter
    draws these taps, and the engine plays them, from one table.

    Everything here is quoted **at `kReferenceSizeM`** (TapTables.h). The Size
    law scales times by S / S_ref and gains by S_ref / S, and cutoffs follow
    the (1 m / d)^kappa term of 10 section 3; `erBandCutoffHzAt` is that last
    rule and the engine does not re-derive it. */

/** The 21 image-source taps every density keeps, 10 section 3. Their
    activation threshold is 0, so they never switch off -- which is what keeps
    the density renormalisation's denominator away from zero. */
inline constexpr int kErCoreTaps = 21;

/** The master sequence per channel: the 21 core taps plus 27 velvet-noise
    infill times, 10 section 3's M = 48. The engine's buffers and the CPU
    worst case are sized from this. */
inline constexpr int kErMaxTaps = 48;

/** Taps share four order-banded low-passes rather than one each (10 section 3,
    section 6's cost argument). */
inline constexpr int kErBands = 4;

/** VARIATION 0..6. Positions 0-5 are per-channel tap sets with gamma falling
    from about 0.95 to about 0.05 and never below zero. Position 6 is built
    differently: Schroeder's complementary-comb pair, applied by the engine to
    the mono set this table carries in both channels (see `combDelayMs`). */
inline constexpr int kErVariations     = 7;
inline constexpr int kErCombVariation  = 6;

/** One reflection in one channel. */
struct ErTap
{
    float timeMs;   ///< arrival at kReferenceSizeM relative to the direct sound, after jitter
    float gain;     ///< linear, (1 m / d) * beta^n at kReferenceSizeM, before density weighting
    float theta;    ///< density activation threshold in [0, 1]; exactly 0 for the core taps
    float pan;      ///< the image's bearing, -1..+1, for the panel's scatter only; the sound
                    ///< gets its width from the per-channel sets, never from this
    int   band;     ///< 0..kErBands-1, the order-banded filter this tap shares
};

/** One channel's taps, ascending in time. Core taps and infill are interleaved
    by time; `theta` is what tells them apart. */
struct ErChannel
{
    ErTap taps[kErMaxTaps];
    int   numTaps;  ///< kErMaxTaps in a finished table; smaller only while a table is under construction
};

struct ErVariationSet
{
    ErChannel left;
    ErChannel right;
};

/** Everything the engine needs to play one type's early reflections. */
struct ErTable
{
    ErVariationSet variation[kErVariations];

    /** VARIATION 6's comb delay: L = E + g * E(t - delay), R = E - g * E(t - delay),
        whose transfer functions sum to unity so the mono sum is exactly flat
        (10 section 3). Milliseconds at kReferenceSizeM, scaled by Size like
        every other time. */
    float combDelayMs;
    float combGain;

    /** The window the taps must stay inside at kReferenceSizeM, and the clamp
        10 section 3 puts on it at any size: 100 ms for Room, Chamber and
        Ambience, 200 ms for Hall, Cavern and Plate. */
    float windowMs;
    float windowClampMs;

    /** The four band low-passes' cutoffs at kReferenceSizeM. */
    float bandCutoffHz[kErBands];

    /** The wall reflection coefficient the gains were derived with. Carried so
        a test can re-derive a gain from its order and path length, and so the
        panel never has to guess. CALIBRATE, 0.70 Room to 0.88 Cavern. */
    float beta;

    /** The seed the jitter and the infill were drawn from. **A table that fails
        an audit is re-seeded, not patched** (10 section 8), so this is the
        number that changes when that happens, and it is pinned with the rest. */
    unsigned int seed;
};

/** The six tables, indexed by the `type` choice's ordinal (Room 0 ... Ambience
    5). Defined by the generator half; the engine half only reads it. */
const ErTable& erTableFor (int typeIndex) noexcept;

/** A band's cutoff at room size `sizeM`. The (1 m / d)^kappa term of 10
    section 3's cutoff law, with d scaling as S / S_ref. Defined beside the
    tables, because kappa is a table-generation constant. */
float erBandCutoffHzAt (const ErTable& table, int band, float sizeM) noexcept;

} // namespace bmo::reverb
