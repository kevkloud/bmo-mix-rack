#pragma once

#include "modules/reverb/dsp/ErTable.h"

#include <cstdint>

namespace bmo::reverb::ergen
{

//==============================================================================
/** The early-reflection table generator: the image-source method of 10
    section 3, run offline, whose output is emitted into `ErTableData.inc` and
    pinned.

    **Why it lives in `dsp/` and yet is not in the plugin.** It is JUCE-free
    and it sits beside `ErTable.h` because it *is* the documentation of where
    those numbers came from: a reader who opens `ErTableData.inc` and asks why
    a tap is at 13.2 ms should find the answer one file away, not under
    `tools/`. But nothing at run time calls it. `modules/CMakeLists.txt`
    compiles it into `bmo_reverb_ergen`, which only `reverb_dsp_tests` and
    `measure_reverb` link, so the plugin carries the emitted numbers and not
    the machinery that derived them -- and the test that regenerates every
    table and compares it with the committed data is what stops the two from
    drifting.

    **Everything is derived at the type's default SIZE and then quoted at
    `kReferenceSizeM`** by the Size law (times x S_ref / S, gains x S / S_ref,
    cutoffs by the kappa term). A room is voiced at the size it opens at: the
    heights of a source and a listener are human, not proportional, and the
    floor bounce that gives 10 section 3's proximity allocation only lands
    inside 5 ms because they are. The rules the audits check are in
    milliseconds and decibels, so they are checked at that size too; see
    `ErAudit.h`. The default sizes are read from `constantsFor`, so a
    CALIBRATE change to one of them regenerates that type's table and the pin
    test goes red until the new table is emitted and re-audited. */

//==============================================================================
/** Everything that makes one type's room, and nothing that is drawn at random.
    Every number here is a design constant, CALIBRATE unless said otherwise;
    the seed is the one number an audit failure is allowed to change. */
struct Recipe
{
    const char* name;

    /** A plate is two-dimensional: its bending waves cannot leave the plate,
        so it has no floor and no ceiling and its images form a plane lattice.
        Every room is a 1 : 1.4 : 1.9 shoebox (10 section 3). */
    bool planar;

    /** The highest image order: 3, as 10 section 3 has it, for every type but
        two -- and **both exceptions depart from the spec**:

        - Plate, to 5, and from 0: the direct wave is its first tap. A plate
          is not a room and the spec's orders were written for rooms (owner,
          2026-09-23); a plane lattice holds 4n images of order n, and orders
          up to 5 are what fill its window at the highs' speed.
        - Cavern. At its 55 m default SIZE only 26 images of orders 1-3
          arrive inside the 200 ms window and 18 survive fusing: a room that
          large has fewer than 21 distinct reflections in 200 ms. Order 4
          gives it 32 and leaves 21. */
    int maxOrder;

    /** Wall reflection coefficient. 0.70 Room to 0.88 Cavern in type order,
        per 10 section 3. CALIBRATE. */
    double beta;

    /** The ER window's clamp, which 10 section 3 fixes per type at any size. */
    double windowClampMs;

    /** Where the listener stands, as fractions of the room's width (x) and
        length (y), and how high. Off-centre on both axes, as 10 section 3
        asks: a centred listener gets pairs of equal-length images, which is
        exactly the coincidence the spacing rules forbid. */
    double listenerFx, listenerFy, listenerZ;

    /** The source: horizontal distance ahead of the listener along +y, and
        how far off that axis it sits, and how high. A small azimuth keeps
        the phantom centre while giving the median-plane images a different
        path to each receiver. */
    double sourceDistM, sourceAzimuthDeg, sourceZ;

    /** The two receivers' spacing along x, for the taps that VARIATION
        splits between channels: near-coincident, a little over ORTF's
        0.17 m, so a split tap's two arrivals differ by the pair's real
        inter-channel delay for that image's bearing -- at most spacing / 2c
        either side, 0.29 ms at 0.20 m. CALIBRATE. */
    double receiverSpacingM;

    /** The cutoff of the fourth band -- the proximity band, which carries
        every tap that lands inside 8 ms. Below 1.5 kHz so that a tap between
        1 and 8 ms is allowed there at all (11 section 6). CALIBRATE. */
    double proximityCutoffHz;

    /** VARIATION 6's Schroeder pair: the delay at the type's default size,
        and the gain. CALIBRATE. */
    double combDelayMs, combGain;

    /** The pinned seed. **The only field an audit failure may change.** */
    std::uint32_t seed;

    //== Plate only -- zero in every room's row ===============================

    /** The plate's real long side. A plate's taps are timed by bending waves
        on a plate this size, not by sound in a room of the default SIZE, and
        the table at Plate's default SIZE *is* that plate; SIZE then scales it
        like any other table. CALIBRATE. */
    double plateLongSideM;

    /** A plate has no 1 m / d of its own -- its output level is its return
        gain -- so its taps take the room law at an equivalent path of this
        plus c * t: loud enough to matter, under the -15.3 dB ceiling. CALIBRATE. */
    double plateEquivalentDistM;

    /** The window a plate fills at its default SIZE, inside its 200 ms clamp:
        a plate's arrivals are over in tens of milliseconds. CALIBRATE. */
    double plateWindowMs;

    //== Early scatter -- zero in every shipping row ===========================

    /** The first `earlyInfillCells` of the 27 velvet pulses are laid over the
        free time before `earlyInfillEndMs` rather than over the whole window:
        a scattered early field, the irregular stone of a real cavern, ahead
        of the dip and the late cluster. Used by the Cavern B candidate only.
        CALIBRATE. */
    double earlyInfillEndMs;
    int    earlyInfillCells;
    double lateInfillStartMs;   ///< and the rest begin no earlier than this: the dip, kept clear
};

/** The six recipes, in `type` order. */
const Recipe& recipeFor (int typeIndex) noexcept;

/** The source-to-listener path of a type's geometry at its default SIZE. The
    lateral fraction's denominator includes the direct sound, and the table
    does not carry it. */
double directDistanceM (int typeIndex) noexcept;

//==============================================================================
/** Constants of the generation law. These are 10 section 3's own numbers; the
    ones marked CALIBRATE there are CALIBRATE here. */
inline constexpr double kCutoffTopHz     = 16000.0;  ///< f = 16 kHz * lambda^n * (1 m / d)^kappa
inline constexpr double kLambda          = 0.8;      ///< CALIBRATE
inline constexpr double kKappa           = 0.2;      ///< CALIBRATE; erBandCutoffHzAt reads the same figure
inline constexpr double kJitter          = 0.03;     ///< +-3 % of each core tap's time
inline constexpr double kMinSeparationMs = 0.9;
inline constexpr double kGapDistinct     = 0.02;     ///< no two core inter-tap gaps within 2 %
inline constexpr double kProximityLoMs   = 1.0;      ///< the 1-8 ms zone where a full-band tap is forbidden
inline constexpr double kProximityHiMs   = 8.0;
inline constexpr int    kMaxOrder        = 3;        ///< the rooms'; see Recipe::maxOrder

/** A split tap's two arrivals sit this far either side of its shared time.
    The spaced pair gives an image its own offset, spacing * lateral / 2c; the
    floor is for the images on the median plane -- floor, ceiling, front and
    back walls -- whose offset from the pair is near zero, and which a real
    near-coincident pair decorrelates through its capsules' angles, which this
    model does not have. Twice the floor, 0.24 ms, is over ten time constants
    of the brightest band, so a split pair does not correlate broadband.

    **The ceiling is the binding constraint, and it costs the low-mids.** A
    pair 0.24-0.6 ms apart decorrelates above about a kilohertz and not much
    below it, where 10 section 3 wants lateral energy built. Offsets of
    0.4-1.0 ms were tried and cannot be placed: every slot's shared arrival
    and both its split ones must each sit 0.9 ms clear of every tap they share
    a channel with at any of the seven positions, and with 48 taps a channel
    in a 100 ms window there is no room once a slot's arrivals spread wider.
    See the testing note. CALIBRATE. */
inline constexpr double kSplitMinMs = 0.12;
inline constexpr double kSplitMaxMs = 0.30;
inline constexpr int    kInfillTaps = kErMaxTaps - kErCoreTaps;

/** Plate's four bands' group velocities, m/s: bending waves on 0.5 mm steel,
    v = 43.8 m/s * sqrt (f / 100 Hz), at 10 kHz, 2 kHz, 500 Hz and 250 Hz --
    the brightest band's leading edge, then each darker band's middle. The
    research Frosty supplied on 2026-09-23 computes the 100 Hz-10 kHz points;
    250 Hz follows by the same square root. A plate's tap is timed at its
    band's speed, which is the feed-forward form of dispersion: highs first,
    lows later, with no allpass anywhere. */
inline constexpr double kPlateBandSpeed[kErBands] { 438.1, 195.9, 98.0, 69.3 };

/** A plate's taps are a dense dispersive cloud, not discrete reflections, so
    the rooms' spacing rules do not bind them (owner, 2026-09-23). They are
    kept this far apart only so no two coincide, the infill starts this soon,
    and a split tap's two arrivals differ by at least this much. CALIBRATE. */
inline constexpr double kPlateMinSeparationMs = 0.25;
inline constexpr double kPlateInfillStartMs   = 0.5;
inline constexpr double kPlateSplitMinMs      = 0.05;

/** Where an early-scatter recipe's infill begins (Recipe::earlyInfillCells). */
inline constexpr double kEarlyScatterStartMs  = 1.5;

/** **Plate's envelope, fitted to measurement.** Source: measured, 16 EMT 140
    IRs, research doc section 8, 2026-09-23. A real plate's energy *rises*
    10-13 dB to a peak at 10-25 ms and then decays; only 0.1-4 % of the
    first 100 ms of energy lies inside 5 ms; band onsets (10 % of each band's
    early energy) come at about 2.5 ms (8 kHz), 10 ms (2 kHz), 13 ms (500 Hz)
    and 18 ms (125 Hz); the right output's 500 Hz onset is 3.6-6.4 ms later
    than the left's, 1 kHz 0.8-1.4 ms, 4-8 kHz none. So each band's taps take
    a rise-and-fall envelope, a (t / T)^k e^(k (1 - t / T)) that peaks at
    T = kPlatePeakMs[band], at kPlateBandLevelDb[band] under kPlatePeakGain;
    each band's one-pole sits at kPlateBandCutoffHz[band], so the four bands
    are 8 kHz-ish, 2 kHz-ish, 500 Hz-ish and 125 Hz-ish content; and a split
    tap puts its right arrival kPlateSplitHalfMs[band] * 2 after its left.
    Every figure here is CALIBRATE: the measurement sets the target, not the
    number. (An earlier estimate said a plate's energy peaks inside 5 ms; the
    measurement corrected it.) */
inline constexpr double kPlateBandCutoffHz[kErBands] { 12000.0, 4000.0, 1500.0, 600.0 };
inline constexpr double kPlatePeakMs[kErBands]       { 4.0, 10.0, 14.0, 24.0 };
inline constexpr double kPlateBandLevelDb[kErBands]  { -20.0, -4.0, 0.0, -2.0 };
inline constexpr double kPlateRise                   = 1.5;
inline constexpr double kPlatePeakGain               = 0.1;
inline constexpr double kPlateSplitHalfMs[kErBands]  { 0.05, 0.5, 2.5, 0.5 };

/** gamma at VARIATION 0..5: from about 0.95 to about 0.05 (10 section 3).
    Position 0 aims at the first; each later one at an equal share of what is
    left to fall. The audit measures what the table got. */
inline constexpr double kGammaTargets[6] { 0.95, 0.77, 0.59, 0.41, 0.23, 0.05 };

//==============================================================================
/** What the generator knows that the table does not carry, for the audits and
    for `measure_reverb`. */
struct Diagnostics
{
    double sizeM;              ///< the type's default size, where it was derived
    double directGain;         ///< (1 m / d_direct) at that size, for the lateral fraction
    double directDistM;
    double meanFreePathM;
    int    imagesConsidered;   ///< images of the recipe's orders inside the window
    int    sharedSlots[kErVariations];
    int    attemptsRejected;   ///< jitter or infill draws redrawn for separation
    int    failedAt;           ///< when generate returns false: 0 too few images, 1 a core tap, 2 an infill pulse, 3 no free time for the infill, 4 not 48 slots (a bug)
};

/** Build one type's table from its recipe and `seed`. Returns false when the
    room has too few distinct images for 21 core taps, or when the seeded draw
    cannot place every tap by the two spacing rules -- at least 0.9 ms from
    its neighbours, and for the core no two inter-tap gaps within 2 % -- in
    every channel of every VARIATION. Those two are enforced *during* the draw,
    because they are rules about where a tap may be put and an infill pulse is
    placed into the gaps the core leaves; the audit still checks them after.
    Everything else is only audited (`ErAudit.h`), and a table that fails is
    re-seeded. */
bool generate (int typeIndex, std::uint32_t seed, ErTable& out, Diagnostics* diag = nullptr);

/** The same from a recipe that is not (yet) the shipped one -- for trying a
    re-voiced geometry in `measure_reverb taps --try` before editing the row.
    `typeIndex` still supplies the default SIZE the room is built at. */
bool generateFrom (const Recipe& recipe, int typeIndex, std::uint32_t seed, ErTable& out, Diagnostics* diag = nullptr);

//==============================================================================
/** **Candidates: tables for the owner's ear, never selectable by the plugin.**
    A candidate is a recipe standing in for a shipping type -- "cavern-b" for
    Cavern -- generated, pinned and audited exactly like a shipping table,
    but reachable only here, from `measure_reverb` and the tests: it lives in
    bmo_reverb_ergen, which no plugin links, and erTableFor never returns it.

    Cavern B (2026-09-24): Cavern's character -- early arrivals, a dip, a
    late focused cluster -- with its cluster brought down to about -17 dB
    re the energy before 25 ms, so flam rule (i) passes. Measured stone
    spaces sit at -14 to -18 dB there (York Minster, St Andrew's, Hamilton
    Mausoleum; research doc, 2026-09-23); the shipping Cavern sits at -10.5.
    For the owner's listening checkpoint, against the shipping Cavern. */
const Recipe* candidateRecipe (const char* name) noexcept;
int candidateType (const char* name) noexcept;   ///< the type it stands in for, or -1
bool generateCandidate (const char* name, std::uint32_t seed, ErTable& out, Diagnostics* diag = nullptr);

/** The emitted, pinned candidate (ErCandidateData.inc), or nullptr. */
const ErTable* erCandidateTable (const char* name) noexcept;

/** The direct path of any recipe at its type's default SIZE. */
double directDistanceM (const Recipe& recipe, int typeIndex) noexcept;

/** The emitted precision. The generator rounds every number it produces onto
    these grids before it returns, so the table it builds and the one parsed
    back from `ErTableData.inc` are equal bit for bit and the pin test can
    assert equality rather than a tolerance -- and a last-ulp difference
    between two platforms' `pow` cannot show through a grid a hundred million
    times coarser. */
inline constexpr double kTimeGridMs  = 1.0e-4;
inline constexpr double kGainGrid    = 1.0e-8;
inline constexpr double kUnitGrid    = 1.0e-6;   ///< theta and pan
inline constexpr double kCutoffGrid  = 1.0e-2;

} // namespace bmo::reverb::ergen
