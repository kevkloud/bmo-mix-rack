#include "modules/reverb/dsp/ImageSource.h"

#include "modules/reverb/dsp/DspCore.h"
#include "modules/reverb/dsp/TapTables.h"
#include "modules/reverb/params.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <string_view>
#include <vector>

namespace bmo::reverb::ergen
{
namespace
{
    constexpr double kPi = 3.14159265358979323846;
    constexpr double kC  = (double) DspCore::kSpeedOfSound;

    //==========================================================================
    /** splitmix64: small, portable and bit-identical everywhere, which the
        standard library's distributions are not -- their algorithms are
        implementation-defined, and a table pinned on one platform has to
        regenerate identically on the other two. */
    struct Rng
    {
        std::uint64_t state;

        std::uint64_t next() noexcept
        {
            auto z = (state += 0x9e3779b97f4a7c15ull);
            z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ull;
            z = (z ^ (z >> 27)) * 0x94d049bb133111ebull;
            return z ^ (z >> 31);
        }

        double unit() noexcept { return (double) (next() >> 11) * (1.0 / 9007199254740992.0); }  // [0, 1)
        double sym()  noexcept { return 2.0 * unit() - 1.0; }                                     // [-1, 1)
    };

    struct Vec { double x, y, z; };

    double distance (const Vec& a, const Vec& b, bool planar) noexcept
    {
        const auto dx = a.x - b.x, dy = a.y - b.y, dz = planar ? 0.0 : a.z - b.z;
        return std::sqrt (dx * dx + dy * dy + dz * dz);
    }

    double quantise (double v, double grid) noexcept { return std::round (v / grid) * grid; }

    /** The m-th image of a coordinate between walls at 0 and `length`: even
        m translates, odd m mirrors. |m| is the number of reflections off that
        pair of walls. */
    double imageCoord (int m, double length, double s) noexcept
    {
        return (m % 2 == 0) ? m * length + s : (m + 1) * length - s;
    }

    //==========================================================================
    // Receiver indices. C is the listener; L and R stand either side of it
    // along x. A tap VARIATION keeps *shared* is the C arrival in both
    // channels; a tap it *splits* is the L arrival in the left channel and
    // the R arrival in the right.
    enum { C = 0, L = 1, R = 2, kReceivers = 3 };

    struct Candidate
    {
        double timeMs;      // at the type's default size
        double gain;
        double pan;
        double pathM;
        int    band;
    };

    struct Slot
    {
        bool      core;
        double    order;          // an image's reflection count; an infill pulse's typical one
        int       band;           // a plate image's band, chosen with its speed; -1 in a room
        double    theta;
        double    splitKey;       // larger splits first
        Candidate c[kReceivers];
    };

    struct Scene
    {
        bool   planar;
        double width, length, height;
        Vec    source;
        Vec    receiver[kReceivers];
        double direct[kReceivers];
        double meanFreePath;
    };

    Scene sceneFor (const Recipe& r, double sizeM)
    {
        Scene s {};
        s.planar = r.planar;
        s.length = (r.planar && r.plateLongSideM > 0.0) ? r.plateLongSideM : sizeM;
        s.width  = r.planar ? s.length / 2.0 : sizeM * 1.4 / 1.9;
        s.height = r.planar ? 0.0 : sizeM / 1.9;

        const Vec listener { r.listenerFx * s.width, r.listenerFy * s.length, r.planar ? 0.0 : r.listenerZ };
        const auto az = r.sourceAzimuthDeg * kPi / 180.0;
        s.source = { listener.x + r.sourceDistM * std::sin (az),
                     listener.y + r.sourceDistM * std::cos (az),
                     r.planar ? 0.0 : r.sourceZ };

        s.receiver[C] = listener;
        s.receiver[L] = { listener.x - 0.5 * r.receiverSpacingM, listener.y, listener.z };
        s.receiver[R] = { listener.x + 0.5 * r.receiverSpacingM, listener.y, listener.z };

        for (int k = 0; k < kReceivers; ++k)
            s.direct[k] = distance (s.source, s.receiver[k], s.planar);

        // The classical mean free path, 4V/S in a room and pi A / P on a
        // plate. It is what turns an infill pulse's time into the order a
        // reflection arriving then would typically have.
        if (s.planar)
            s.meanFreePath = kPi * s.width * s.length / (2.0 * (s.width + s.length));
        else
            s.meanFreePath = 4.0 * s.width * s.length * s.height
                           / (2.0 * (s.width * s.length + s.width * s.height + s.length * s.height));

        return s;
    }

    double cutoffLaw (double order, double pathM) noexcept
    {
        return kCutoffTopHz * std::pow (kLambda, order) * std::pow (1.0 / pathM, kKappa);
    }

    int orderBand (double order) noexcept
    {
        return std::clamp ((int) std::lround (order) - 1, 0, 2);
    }

    /** Whether a tap goes to the proximity band. Everything before 8 ms,
        not only 1-8 ms: below 1 ms a full-band tap is *allowed*, because it
        fuses, but it is a comb against the dry signal at its own delay, and
        the first reflection is never split, so a full-band one would also
        be a floor under gamma that VARIATION 5 could not get beneath. */
    bool inProximityZone (double timeMs) noexcept
    {
        return timeMs < kProximityHiMs;
    }

    /** Plate's measured rise-and-fall envelope for one band (ImageSource.h). */
    double plateEnvelope (int band, double timeMs) noexcept
    {
        const auto x = std::max (timeMs, 0.0) / kPlatePeakMs[band];
        return kPlatePeakGain * std::pow (10.0, kPlateBandLevelDb[band] / 20.0)
             * std::pow (x, kPlateRise) * std::exp (kPlateRise * (1.0 - x));
    }

    /** The filtered energy of one tap under a one-pole low-pass at `hz`:
        a^2 / (2 tau) with tau = 1 / (2 pi f), i.e. a^2 * pi * f. It is what a
        tap contributes to the correlation the audit measures, so it is what
        the shared-energy split is aimed with. */
    double filteredEnergy (double gain, double hz) noexcept { return gain * gain * kPi * hz; }
}

//==============================================================================
// The six recipes. **Every number is CALIBRATE** except the window clamps,
// which are 10 section 3's, and the betas' end points, which are its range.
//
// The geometry was chosen with the audits in view and says so: the listener
// stands off-centre on both axes, the source a few degrees off the listener's
// axis, both at human heights, so that the floor bounce is the first
// reflection -- near centre, inside 5 ms, and darkened into the proximity band
// -- and so that enough early energy arrives before 25 ms that nothing after
// it flams, and the room's lateral fraction lands in 0.10-0.25. Those are
// design choices, tried across seeds with `measure_reverb taps --try` and made
// once. The seeds are the only thing changed afterwards to make a table pass:
// each is the first seed from 1 that passes every audit (`--reseed`).
//
// **Cavern's is not.** No seed and no geometry tried passes flam rule (i) at
// its 55 m default SIZE: a room that large has its first-order walls 40 ms
// and more behind the floor bounce, so the reflections after 25 ms stand
// above the little that arrived before it. Its seed, 1444, is the one of the
// first 3000 that passes every other rule with (i) least short (`--best`),
// and the test carries that failure by name. The owner will decide it by ear
// at the listening checkpoint -- 55 m against a smaller SIZE (2026-09-23) --
// so it is not a seed to search for. See the testing note.
//
// **Plate is not a room**, and its recipe is a proposal rather than a
// derivation: see the comment on its row.
static const Recipe kRecipes[numTypes]
{
    //  name        planar order beta  clamp   lstFx  lstFy  lstZ  srcD  srcAz srcZ  spacing  proxHz  combMs combG  seed
    { "Room",       false, 3,    0.70, 100.0,  0.30,  0.40,  1.5,  3.4,  6.0,  1.7,  0.20,   1200.0, 10.0,  0.90,  1u },
    { "Chamber",    false, 3,    0.76, 100.0,  0.25,  0.35,  1.5,  4.5,  8.0,  1.7,  0.20,   1200.0, 10.0,  0.90,  1u },
    { "Hall",       false, 3,    0.82, 200.0,  0.24,  0.68,  1.5, 10.0,  5.0,  1.7,  0.20,   1200.0, 20.0,  0.90,  2u },
    { "Cavern",     false, 4,    0.88, 200.0,  0.15,  0.65,  1.5, 20.0,  5.0,  1.7,  0.20,   1200.0, 20.0,  0.90,  1444u },

    // PLATE -- CALIBRATE THROUGHOUT, AND NOT A ROOM.
    //
    // Owner decision, 2026-09-23: "plate verbs are a physical metal plate
    // model, not a room model. room rules shouldn't apply." So nothing in this
    // row, or in the generator's plate path, bends to the room rules, and
    // ErAudit does not hold a plate to them. It is held instead to rules
    // taken from measurement -- measured, 16 EMT 140 IRs, research doc
    // section 8, 2026-09-23 -- which corrected an earlier estimate that a
    // plate's energy peaks inside 5 ms. It does not: it swells.
    //
    // The model: a 2 m x 1 m steel plate (the EMT 140's), the driver 0.3 m
    // from the pickups. Its 2D image lattice, orders 0-5, gives the tap
    // *times*: every path arrives once per band at that band's bending-wave
    // group speed (kPlateBandSpeed), highs first. The tap *levels* follow the
    // measured swell, a per-band rise-and-fall envelope (ImageSource.h), so
    // the bright front is weak and the energy peaks in the teens of
    // milliseconds. The right pickup hears each split wave later and no
    // louder: 4-6 ms later in the 500 Hz band, which is split at every
    // VARIATION because the measured offset is the plate's and not a setting;
    // about 1 ms in the 2 kHz band; nothing in the 8 kHz band; and the 125 Hz
    // band is given no imposed offset, because the measurement is not
    // reliable there. Taps sit 0.25 ms apart, not 0.9 (a room rule; with
    // 0.9, no seed of 1000 can place 48 taps in the window, see the note).
    // Window 45 ms at the default SIZE -- the table at 22 m *is* the EMT.
    { "Plate",      true,  5,    0.90, 200.0,  0.40,  0.25,   0.0,  0.3, 10.0,  0.0,  0.60,   1200.0, 20.0,  0.90,  184u,  2.0, 16.0, 45.0 },

    { "Ambience",   false, 3,    0.72, 100.0,  0.40,  0.38,  1.5,  3.0,  8.0,  1.7,  0.20,   1200.0, 10.0,  0.90,  31u },
};

const Recipe& recipeFor (int typeIndex) noexcept
{
    return kRecipes[std::clamp (typeIndex, 0, (int) numTypes - 1)];
}

//==============================================================================
// Candidates -- for the owner's ear, never for the plugin (ImageSource.h).
//
// Cavern B is the shipping Cavern's room, placement and beta, with one
// change: 14 of its 27 velvet pulses are laid over the first 30 ms, a
// scattered early field ahead of the dip, so that the late focused cluster
// sits at about -17 dB against the energy before 25 ms instead of -10.5.
// Every other number is the shipping row's. The seed is the first from 1
// whose table passes every audit with flam (i) at least 4 dB clear.
static const Recipe kCavernB
    //  name        planar order beta  clamp   lstFx  lstFy  lstZ  srcD  srcAz srcZ  spacing  proxHz  combMs combG  seed   plate (unused)   early scatter
    { "Cavern B",   false, 4,    0.88, 200.0,  0.15,  0.65,  1.5, 20.0,  5.0,  1.7,  0.20,   1200.0, 20.0,  0.90,  11u,   0.0, 0.0, 0.0,   22.0, 13, 60.0 };

const Recipe* candidateRecipe (const char* name) noexcept
{
    return (name != nullptr && std::string_view (name) == "cavern-b") ? &kCavernB : nullptr;
}

int candidateType (const char* name) noexcept
{
    return candidateRecipe (name) != nullptr ? (int) cavern : -1;
}

bool generateCandidate (const char* name, std::uint32_t seed, ErTable& out, Diagnostics* diag)
{
    const auto* recipe = candidateRecipe (name);
    return recipe != nullptr && generateFrom (*recipe, candidateType (name), seed, out, diag);
}

double directDistanceM (const Recipe& recipe, int typeIndex) noexcept
{
    return recipe.planar ? recipe.plateEquivalentDistM
                         : sceneFor (recipe, (double) constantsFor (typeIndex).sizeM).direct[C];
}

double directDistanceM (int typeIndex) noexcept
{
    // A plate's direct wave is one of its taps, not the dry signal; the dry is
    // heard as if from the equivalent distance its taps are levelled at.
    const auto& r = recipeFor (typeIndex);
    return r.planar ? r.plateEquivalentDistM
                    : sceneFor (r, (double) constantsFor (typeIndex).sizeM).direct[C];
}

//==============================================================================
bool generate (int typeIndex, std::uint32_t seed, ErTable& out, Diagnostics* diag)
{
    return generateFrom (recipeFor (typeIndex), typeIndex, seed, out, diag);
}

bool generateFrom (const Recipe& recipe, int typeIndex, std::uint32_t seed, ErTable& out, Diagnostics* diag)
{
    const double sizeM = (double) constantsFor (typeIndex).sizeM;
    const double toRef = (double) kReferenceSizeM / sizeM;   // times at ref = times here * toRef

    const auto scene = sceneFor (recipe, sizeM);

    // The window at this size is the clamp itself: every type is voiced to
    // fill its window at the SIZE it opens at. Quoted at the reference size
    // that is clamp * S_ref / S_default, so for Ambience -- smaller than the
    // reference -- the table spans 150 ms at 12 m against a 100 ms clamp.
    // That is not a contradiction: the clamp holds at every size, and it is
    // the engine that applies it (by dropping taps past it), at 12 m as at
    // 80 m. `windowMs` says where the taps are, `windowClampMs` where they stop.
    const bool plate = recipe.planar;
    const double windowMs = plate ? recipe.plateWindowMs : recipe.windowClampMs;

    Rng rng { 0x5eedull * 0x100000001b3ull ^ ((std::uint64_t) typeIndex << 32) ^ (std::uint64_t) seed };

    //== Every image, to the recipe's order ====================================
    //
    // In a room an image's tap is timed by its path at c, relative to the
    // direct sound. On a plate there is no acoustic direct sound -- the dry
    // signal is the only direct there is -- so the driver-to-pickup wave is
    // itself the first tap (order 0), every arrival is absolute, and every
    // path arrives once per band, at that band's bending-wave group speed
    // (kPlateBandSpeed): dispersion, feed-forward. The room rules -- the 1-8 ms
    // full-band ban among them -- do not bind a plate (owner, 2026-09-23; see
    // ErAudit.h), so nothing here darkens or moves an arrival to obey them.
    struct Image { int order; int band; Vec pos; double timeMs[kReceivers]; double pathM[kReceivers]; double pan[kReceivers]; };
    std::vector<Image> images;

    const int maxOrder = recipe.maxOrder;
    const int zMax = scene.planar ? 0 : maxOrder;

    for (int mx = -maxOrder; mx <= maxOrder; ++mx)
        for (int my = -maxOrder; my <= maxOrder; ++my)
            for (int mz = -zMax; mz <= zMax; ++mz)
            {
                const int order = std::abs (mx) + std::abs (my) + std::abs (mz);

                if (order < (plate ? 0 : 1) || order > maxOrder)
                    continue;

                Image im {};
                im.order = order;
                im.band  = -1;
                im.pos = { imageCoord (mx, scene.width,  scene.source.x),
                           imageCoord (my, scene.length, scene.source.y),
                           scene.planar ? 0.0 : imageCoord (mz, scene.height, scene.source.z) };

                for (int k = 0; k < kReceivers; ++k)
                {
                    const auto d = distance (im.pos, scene.receiver[k], scene.planar);
                    im.pathM[k]  = d;
                    im.timeMs[k] = 1000.0 * (d - scene.direct[k]) / kC;
                    im.pan[k]    = d > 0.0 ? (im.pos.x - scene.receiver[k].x) / d : 0.0;
                }

                if (plate)
                {
                    // Every path carries every band, each at its own group
                    // speed: one arrival per band, highs first.
                    for (int b = 0; b < kErBands; ++b)
                    {
                        auto arrival = im;
                        arrival.band = b;

                        for (int k = 0; k < kReceivers; ++k)
                            arrival.timeMs[k] = 1000.0 * im.pathM[k] / kPlateBandSpeed[b];

                        images.push_back (arrival);
                    }

                    continue;
                }

                images.push_back (im);
            }

    //== The 21 core taps: the strongest image in each of 21 slices ===========
    //
    // Neither the earliest nor simply the strongest. Image density grows as
    // t^2, so the 21 earliest crowd into the first third of the window; the
    // 21 strongest spread further but still bunch where the second order
    // arrives, and leave no room between them for the infill that has to sit
    // 0.9 ms from every one. So the window is cut into 21 equal slices and
    // each keeps its strongest image by (1 m / d) beta^n -- a sampling of the
    // image field that keeps its envelope and spreads it the way Moorer's 19
    // spread across 4.3-79.7 ms. A slice with no image gives its place to the
    // strongest image left anywhere. Two images whose arrivals would sit
    // within 0.9 ms plus the split room of each other fuse into one, and the
    // stronger keeps it.
    std::vector<const Image*> candidates;

    // A plate's taps are a dense dispersive cloud, not discrete reflections,
    // so the rooms' 0.9 ms comb spacing does not bind it; they are kept
    // kPlateMinSeparationMs apart only so no two taps coincide.
    const double sepMs = plate ? kPlateMinSeparationMs : kMinSeparationMs;
    const double reach = (windowMs - sepMs - (plate ? 2.4 * kPlateSplitHalfMs[2] : 0.0)) / (1.0 + kJitter);

    for (const auto& im : images)
        if (im.timeMs[C] > 0.0 && im.timeMs[C] <= reach)
            candidates.push_back (&im);

    const auto strength = [&recipe, plate] (const Image* im)
    {
        return std::pow (recipe.beta, im->order)
             / (plate ? 1.0 : im->pathM[C]) * (plate ? plateEnvelope (im->band, im->timeMs[C]) / std::pow (recipe.beta, im->order) : 1.0);
    };

    std::stable_sort (candidates.begin(), candidates.end(),
                      [&strength] (const Image* a, const Image* b) { return strength (a) > strength (b); });

    std::vector<const Image*> chosen;

    const double fuseMs = sepMs;

    // On a plate a split arrival can sit milliseconds off its shared time, so
    // two arrivals count as one unless their shared times clear the spacing
    // plus both their split offsets -- otherwise no jitter could place them.
    const auto half = [plate] (const Image* im)
    {
        return plate ? 0.3 * kPlateSplitHalfMs[im->band] : 0.0;
    };

    const auto admit = [&chosen, fuseMs, &half] (const Image* im)
    {
        for (const auto* c : chosen)
            if (c == im || std::abs (c->timeMs[C] - im->timeMs[C]) < fuseMs + half (c) + half (im))
                return false;

        chosen.push_back (im);
        return true;
    };

    // On a plate the driver-to-pickup wave is the strongest path in every
    // band, and its four arrivals are what the dispersion order is: they go
    // in first, before any slice is filled.
    if (plate)
        for (const auto* im : candidates)
            if (im->order == 0)
                admit (im);

    for (int slice = 0; slice < kErCoreTaps && (int) chosen.size() < kErCoreTaps; ++slice)
    {
        const double lo = reach * slice / kErCoreTaps, hi = reach * (slice + 1) / kErCoreTaps;

        for (const auto* im : candidates)   // strongest first
            if (im->timeMs[C] > lo && im->timeMs[C] <= hi && admit (im))
                break;
    }

    for (const auto* im : candidates)
    {
        if ((int) chosen.size() >= kErCoreTaps)
            break;

        admit (im);
    }

    if ((int) chosen.size() < kErCoreTaps)
        { if (diag != nullptr) { diag->failedAt = 0; diag->imagesConsidered = (int) candidates.size(); diag->attemptsRejected = (int) chosen.size(); } return false; }   // the recipe's room is too small for its window; not a seed problem

    std::stable_sort (chosen.begin(), chosen.end(),
                      [] (const Image* a, const Image* b) { return a->timeMs[C] < b->timeMs[C]; });


    // A split tap's two arrivals: either side of the shared time by the pair's
    // own offset for that image, held between the floor and the ceiling of
    // kSplitMinMs / kSplitMaxMs. `side` is +1 when the image reaches the left
    // receiver first.
    struct Split { double halfMs; double side; };
    std::vector<Split> splits;
    std::vector<Slot> slots;

    for (const auto* im : chosen)
    {
        Slot s {};
        s.core  = true;
        s.order = im->order;
        s.band  = im->band;

        for (int k = 0; k < kReceivers; ++k)
            s.c[k] = { im->timeMs[C], 0.0, im->pan[plate ? C : k], 0.0, 0 };   // a plate's bearing is from between its pickups

        // On a plate the two pickups' own difference stands, unclamped above:
        // at the lows' speed it is milliseconds, at the highs' a fraction of
        // one, which is the per-output difference a real plate has.
        const auto geometric = 0.5 * (im->timeMs[R] - im->timeMs[L]);
        splits.push_back ({ plate ? kPlateSplitHalfMs[im->band] * (0.8 + 0.4 * rng.unit())
                                  : std::clamp (std::abs (geometric), kSplitMinMs, kSplitMaxMs),
                            plate ? 1.0 : geometric > 0.0 ? 1.0 : (geometric < 0.0 ? -1.0 : (rng.unit() < 0.5 ? 1.0 : -1.0)) });
        slots.push_back (s);
    }

    //== The 27 infill slots: one velvet pulse per equal cell ===================
    //
    // The cells start where the proximity zone ends, so the infill never adds
    // to the deliberate small allocation inside 5 ms and never needs the dark
    // band. Its gains come from the same (1 m / d) beta^n envelope, with n the
    // order a reflection arriving then would typically have -- path over mean
    // free path, held to the core's range -- so the contour is the same at
    // every density. Where in its cell each pulse lands is drawn below.
    //
    // On a plate the infill starts at once -- a plate has no proximity zone
    // to keep clear -- and its bands darken with time, the bloom; a pulse's
    // split is the pickups' difference at its band's speed, drawn.
    // An early-scatter recipe starts its infill inside the proximity zone;
    // anything there lands in the dark band, as every tap inside 8 ms does.
    const double infillStartMs = plate ? kPlateInfillStartMs
                               : recipe.earlyInfillCells > 0 ? kEarlyScatterStartMs : kProximityHiMs;
    const double cellMs = (windowMs - infillStartMs) / kInfillTaps;
    const auto plateInfillBand = [&] (double timeMs)
    {
        const auto late = std::clamp ((timeMs - infillStartMs) / (windowMs - infillStartMs), 0.0, 1.0);
        return std::min (kErBands - 1, (int) (kErBands * late));
    };

    for (int cell = 0; cell < kInfillTaps; ++cell)
    {
        Slot s {};
        s.core = false;

        const auto pan = rng.sym();   // a velvet pulse has no image; its bearing is drawn, for the panel
        const auto centre = infillStartMs + cellMs * (cell + 0.5);
        s.band = plate ? plateInfillBand (centre) : -1;   // a slot keeps one band in both channels

        for (int k = 0; k < kReceivers; ++k)
            s.c[k] = { centre, 0.0, pan, 0.0, 0 };

        const auto half = plate ? kPlateSplitHalfMs[plateInfillBand (centre)] * (0.8 + 0.4 * rng.unit())
                                : kSplitMinMs + (kSplitMaxMs - kSplitMinMs) * rng.unit();
        const auto infillSide = rng.unit() < 0.5 ? 1.0 : -1.0;
        splits.push_back ({ half, plate ? 1.0 : infillSide });
        slots.push_back (s);
    }

    const auto coreCount = kErCoreTaps;
    const auto slotCount = (int) slots.size();

    // Every channel holds exactly kErMaxTaps. A recipe that produced more or
    // fewer slots is a generator bug, and it fails loudly here rather than
    // writing past the end of an ErChannel.
    if (slotCount != kErMaxTaps)
    {
        if (diag != nullptr)
            diag->failedAt = 4;

        return false;
    }

    // Every gain follows from a time: the path it implies, and for a velvet
    // pulse the order a reflection over that path typically has.
    //
    // A plate has no path of its own for the law, so it takes the equivalent
    // path plateEquivalentDistM + c t; its velvet pulses take the order and
    // the band their time would have on the plate -- darker as they come
    // later, the dispersive bloom -- rather than a mean free path.
    const auto settle = [&] (Slot& s, int k)
    {
        auto& c = s.c[k];

        if (plate)
        {
            c.pathM = recipe.plateEquivalentDistM + kC * c.timeMs / 1000.0;
            c.band  = s.band;
            c.gain  = plateEnvelope (c.band, s.c[C].timeMs);   // one wave: the right pickup hears it later, not louder
            return;
        }

        c.pathM = scene.direct[k] + kC * c.timeMs / 1000.0;

        const auto order = s.core ? s.order
                                  : std::clamp (c.pathM / scene.meanFreePath, 1.0, (double) recipe.maxOrder);
        c.gain = std::pow (recipe.beta, order) / c.pathM;
        c.band = inProximityZone (c.timeMs) ? 3 : orderBand (order);
    };

    const auto placeAt = [&] (int i, double t)
    {
        auto& s = slots[(size_t) i];
        const auto& sp = splits[(size_t) i];
        s.c[C].timeMs = t;
        // On a plate the left pickup hears the shared time and the right one
        // later -- the measured EMT has its right output behind -- so a split
        // never moves an arrival earlier than the first sound.
        s.c[L].timeMs = plate ? t : t - sp.side * sp.halfMs;
        s.c[R].timeMs = plate ? t + 2.0 * sp.halfMs : t + sp.side * sp.halfMs;

        for (int k = 0; k < kReceivers; ++k)
            settle (s, k);
    };

    for (int i = 0; i < slotCount; ++i)
        placeAt (i, slots[(size_t) i].c[C].timeMs);

    //== Density thresholds: the infill switches on in a stratified order ======
    //
    // Not a random order. Cell k's key is frac(offset + k * (golden ratio)),
    // with the offset drawn from the seed, and thresholds go up in key order;
    // a golden-ratio sequence is the most even way to take any number of
    // cells from a row, so at every DENSITY the pulses that are on are spread
    // across the window rather than bunched -- which is what keeps the 5 ms
    // half leaves holes. Spread over (0, 0.92) rather than (0, 1]: the engine
    // ramps literally, w = clamp ((D - theta) / 0.08, 0, 1), so a threshold at
    // 1 would never sound and one above 1 - 0.08 would never be fully on. The
    // top one sits a grid step under 0.92, because 0.92 as a float is a hair
    // above it and (1 - 0.92f) / 0.08f comes out a hair under 1.
    // last pulse is fully on at DENSITY 100 %; at exactly 1 it never would be.
    {
        std::array<double, kInfillTaps> key {};
        std::array<int, kInfillTaps> byKey {};
        const double offset = rng.unit();

        for (int i = 0; i < kInfillTaps; ++i)
        {
            const auto k = offset + 0.6180339887498949 * i;
            key[(size_t) i] = k - std::floor (k);
            byKey[(size_t) i] = i;
        }

        std::sort (byKey.begin(), byKey.end(), [&key] (int a, int b) { return key[(size_t) a] < key[(size_t) b]; });

        const double top = 1.0 - (double) DspCore::kRampWidth - kUnitGrid;

        for (int r = 0; r < kInfillTaps; ++r)
            slots[(size_t) (coreCount + byKey[(size_t) r])].theta = top * (double) (r + 1) / kInfillTaps;
    }

    //== VARIATION: which slots each position splits ============================
    //
    // A *shared* slot is one tap, identical in both channels. A *split* one is
    // two taps, the left receiver's arrival on the left and the right's on the
    // right -- two taps, never one tap offset, so what is split cannot comb in
    // mono. gamma is then very nearly the share of the ER's filtered energy
    // that is shared, so VARIATION is a choice of nested shared sets.
    //
    // The *lateral-spread scalar* is 1 - gamma_target, the share each position
    // carries on split taps. The *permutation* is the seeded order in which
    // slots leave the shared set: loud and lateral first -- a slot's key is
    // its energy, weighted up by how lateral its image is and scaled by a
    // seeded factor, so the order is the seed's as well as the geometry's.
    // Loud first because a slot louder than one step can only be taken early:
    // left to the end, it would make the last step two. In a room the first
    // reflection is never split, which is what holds the phantom centre at
    // every position. A plate's first tap is its direct wave, which reaches
    // the two pickups at two times, so on a plate it splits like any other.
    //
    // The core and the infill are aimed separately, each at the same share,
    // so that gamma is on target at DENSITY 0 (core alone), at 100 % (both)
    // and between. Aimed with the unjittered energies; the audit measures
    // what the jittered table got.
    std::vector<double> energy ((size_t) slotCount);
    double total[2] {}, split[2] {};

    for (int i = 0; i < slotCount; ++i)
    {
        const auto& c = slots[(size_t) i].c[C];
        const auto hz = plate ? kPlateBandCutoffHz[c.band]
                              : c.band == 3 ? recipe.proximityCutoffHz : cutoffLaw ((double) (c.band + 1), c.pathM);
        energy[(size_t) i] = filteredEnergy (c.gain, hz);
        total[slots[(size_t) i].core ? 0 : 1] += energy[(size_t) i];
    }

    std::vector<int> splitOrder;

    for (int i = plate ? 0 : 1; i < slotCount; ++i)
    {
        auto& s = slots[(size_t) i];
        s.splitKey = energy[(size_t) i] * (0.5 + s.c[C].pan * s.c[C].pan) * (0.75 + 0.5 * rng.unit());
        splitOrder.push_back (i);
    }

    std::stable_sort (splitOrder.begin(), splitOrder.end(),
                      [&slots] (int a, int b) { return slots[(size_t) a].splitKey > slots[(size_t) b].splitKey; });

    // use[i][v][ch]: which receiver's arrival slot i plays in channel ch
    // (0 left, 1 right) at position v.
    std::vector<std::array<std::array<int, 2>, kErVariations>> use ((size_t) slotCount);
    std::vector<bool> isSplit ((size_t) slotCount, false);

    // A plate's lows reach its two pickups milliseconds apart whatever the
    // VARIATION -- the per-output difference is the plate's, not a setting
    // (the research: 5-10 ms in the lows). So on a plate every dark-band slot
    // is split at every position; they carry little of the heard energy, so
    // the gamma ladder is still aimed by the brighter slots.
    if (plate)
        for (int i = 0; i < slotCount; ++i)
            if (slots[(size_t) i].c[C].band == 2)
            {
                isSplit[(size_t) i] = true;
                split[slots[(size_t) i].core ? 0 : 1] += energy[(size_t) i];
            }

    // Position 0 aims at kGammaTargets[0]. Every later position aims to take
    // an equal share of what is left to fall, down to the last target -- or,
    // for the core, to the first reflection's own share, which never splits.
    // A slot louder than the step is taken at the first position it is
    // louder than, because it can only overshoot, and overshooting early
    // leaves the remaining steps room to be even; left to the end, it would
    // take the last two steps as one.
    constexpr double kAimSlack = 0.03;

    for (int v = 0; v < kErVariations; ++v)
    {
        if (v < kErCombVariation)
        {
            for (int g = 0; g < 2; ++g)
            {
                const auto share = [&] { return (total[g] - split[g]) / total[g]; };
                const auto floor = std::max (kGammaTargets[kErCombVariation - 1],
                                             (g == 0 && ! plate) ? energy[0] / total[0] : 0.0);
                const auto goal = v == 0 ? kGammaTargets[0]
                                         : share() - (share() - floor) / (double) (kErCombVariation - v);
                const auto step = share() - goal;
                const auto take = [&] (int i) { isSplit[(size_t) i] = true; split[g] += energy[(size_t) i]; };
                const auto mine = [&] (int i) { return ! isSplit[(size_t) i] && (slots[(size_t) i].core ? 0 : 1) == g; };

                // The loudest slot louder than the step, if there is one.
                int loudest = -1;

                for (const auto i : splitOrder)
                    if (mine (i) && energy[(size_t) i] / total[g] > step + kAimSlack
                        && (loudest < 0 || energy[(size_t) i] > energy[(size_t) loudest]))
                        loudest = i;

                if (loudest >= 0 && v > 0)
                    take (loudest);

                // Then the permutation, taking what fits.
                for (const auto i : splitOrder)
                {
                    if (share() <= goal + kAimSlack)
                        break;

                    if (mine (i) && (total[g] - split[g] - energy[(size_t) i]) / total[g] >= goal - kAimSlack)
                        take (i);
                }

                // And whatever lands nearest, while still short.
                while (share() > goal + kAimSlack)
                {
                    int best = -1;
                    double bestMiss = 0.0;

                    for (const auto i : splitOrder)
                    {
                        if (! mine (i))
                            continue;

                        const auto miss = std::abs ((total[g] - split[g] - energy[(size_t) i]) / total[g] - goal);

                        if (best < 0 || miss < bestMiss)
                        {
                            best = i;
                            bestMiss = miss;
                        }
                    }

                    if (best < 0)
                        break;

                    take (best);
                }
            }
        }

        for (int i = 0; i < slotCount; ++i)
            use[(size_t) i][(size_t) v] = (v < kErCombVariation && isSplit[(size_t) i]) ? std::array<int, 2> { L, R }
                                                                                        : std::array<int, 2> { C, C };
    }


    //== Placement: the jitter, and the one rule enforced during the draw ======
    //
    // Each slot's shared time is drawn -- a core tap's +-3 % jitter, an infill
    // pulse's place in its cell -- and redrawn from the same stream while any
    // of its arrivals would sit within 0.9 ms of a tap it shares a channel
    // with at any position. An infill pulse is placed *into* the gaps, so this
    // one rule cannot wait for the audit; every other rule does.
    int rejected = 0;

    const auto clear = [&] (int i)
    {
        for (int v = 0; v < kErVariations; ++v)
            for (int ch = 0; ch < 2; ++ch)
            {
                const auto t = slots[(size_t) i].c[use[(size_t) i][(size_t) v][(size_t) ch]].timeMs;

                if (t <= 0.0 || t >= windowMs - 0.5 * sepMs)
                    return false;

                for (int j = 0; j < i; ++j)
                {
                    const auto other = use[(size_t) j][(size_t) v][(size_t) ch];

                    if (std::abs (slots[(size_t) j].c[other].timeMs - t) < sepMs)
                        return false;
                }
            }

        // The core's gap rule is a placement rule too: no two of a channel's
        // core inter-tap gaps within 2 % of each other, at any position.
        if (plate || ! slots[(size_t) i].core)
            return true;

        double times[kErCoreTaps], gaps[kErCoreTaps];

        for (int v = 0; v < kErVariations; ++v)
            for (int ch = 0; ch < 2; ++ch)
            {
                for (int j = 0; j <= i; ++j)
                    times[j] = slots[(size_t) j].c[use[(size_t) j][(size_t) v][(size_t) ch]].timeMs;

                std::sort (times, times + i + 1);

                for (int j = 0; j < i; ++j)
                    gaps[j] = times[j + 1] - times[j];

                for (int a = 0; a < i; ++a)
                    for (int b = a + 1; b < i; ++b)
                        if (std::abs (gaps[a] - gaps[b]) < kGapDistinct * std::max (gaps[a], gaps[b]))
                            return false;
            }

        return true;
    };

    const auto draw = [&] (int i, auto&& where)
    {
        for (int attempt = 0; attempt < 256; ++attempt)
        {
            placeAt (i, where());

            if (clear (i))
                return true;

            ++rejected;
        }

        return false;
    };

    for (int i = 0; i < coreCount; ++i)
    {
        const auto geometric = slots[(size_t) i].c[C].timeMs;

        if (! draw (i, [&] { return geometric * (1.0 + kJitter * rng.sym()); }))
            { if (diag != nullptr) diag->failedAt = 1; return false; }
    }

    // The infill's grid is laid over the time the core leaves free. Velvet
    // noise is one pulse per equal window; here the windows are equal in
    // *free* time -- the span from the end of the proximity zone to the end
    // of the window, less 0.9 ms either side of every core arrival -- so the
    // pulses fall between the core taps rather than onto them, and the
    // combined set's density stays even where the image field bunches. With
    // equal windows of plain time the second-order cluster leaves some cells
    // no room at all.
    std::vector<std::pair<double, double>> free;
    {
        std::vector<std::pair<double, double>> blocked;

        for (int i = 0; i < coreCount; ++i)
            for (int k = 0; k < kReceivers; ++k)
                blocked.push_back ({ slots[(size_t) i].c[k].timeMs - sepMs,
                                     slots[(size_t) i].c[k].timeMs + sepMs });

        std::sort (blocked.begin(), blocked.end());

        double at = infillStartMs;
        // A plate's right arrival can trail its shared time by up to 2.4 half-
        // offsets, so its infill stops that far short of the window.
        const double end = windowMs - 0.5 * sepMs - (plate ? 2.4 * kPlateSplitHalfMs[2] : 0.0);

        for (const auto& b : blocked)
        {
            if (b.first > at)
                free.push_back ({ at, std::min (b.first, end) });

            at = std::max (at, b.second);

            if (at >= end)
                break;
        }

        if (at < end)
            free.push_back ({ at, end });
    }

    double freeMs = 0.0;

    for (const auto& f : free)
        freeMs += std::max (0.0, f.second - f.first);

    // The core can leave no time free at all -- a window too short for its
    // taps and their split arrivals. That is a failed draw, reported, never a
    // read past an empty list.
    if (free.empty() || freeMs <= 0.0)
    {
        if (diag != nullptr)
            diag->failedAt = 3;

        return false;
    }

    const auto freeToTime = [&free] (double x)
    {
        for (const auto& f : free)
        {
            const auto length = f.second - f.first;

            if (x <= length)
                return f.first + x;

            x -= length;
        }

        return free.back().second;
    };

    // An early-scatter recipe lays its first earlyInfillCells pulses over the
    // free time before earlyInfillEndMs and the rest over what follows; every
    // other recipe lays all 27 evenly over the whole of it.
    const int earlyCells = std::clamp (recipe.earlyInfillCells, 0, kInfillTaps - 1);
    double earlyFreeMs = 0.0;

    for (const auto& f : free)
        earlyFreeMs += std::max (0.0, std::min (f.second, recipe.earlyInfillEndMs) - f.first);

    // The rest start no earlier than lateInfillStartMs, so the dip between
    // the early scatter and the late cluster stays clear.
    double lateFreeStart = earlyFreeMs;

    if (earlyCells > 0)
    {
        lateFreeStart = 0.0;

        for (const auto& f : free)
            lateFreeStart += std::max (0.0, std::min (f.second, std::max (recipe.earlyInfillEndMs, recipe.lateInfillStartMs)) - f.first);
    }

    if (earlyCells > 0 && earlyFreeMs <= 0.0)
    {
        if (diag != nullptr)
            diag->failedAt = 3;

        return false;
    }

    const auto infillPosition = [&] (int cell, double u)
    {
        if (earlyCells == 0)
            return freeMs * (cell + u) / kInfillTaps;

        if (cell < earlyCells)
            return earlyFreeMs * (cell + u) / earlyCells;

        return lateFreeStart + (freeMs - lateFreeStart) * (cell - earlyCells + u) / (kInfillTaps - earlyCells);
    };

    for (int i = coreCount; i < slotCount; ++i)
    {
        const int cell = i - coreCount;

        if (! draw (i, [&] { return freeToTime (infillPosition (cell, rng.unit())); }))
            { if (diag != nullptr) diag->failedAt = 2; return false; }
    }


    //== Band cutoffs at this size ============================================
    //
    // Bands 0-2 are orders 1-3 (Plate's fourth order shares band 2), each at
    // the cutoff law evaluated at its members' geometric-mean path. Band 3 is
    // the proximity band: every tap between 1 and 8 ms, below 1.5 kHz, because
    // 11 section 6 allows a tap there on no other terms -- and Haas's finding
    // that darkening an echo raises its critical delay at almost no loudness
    // cost (05 section 1.2) is why the allocation is worth keeping on them.
    double cutoffHz[kErBands] {};
    {
        double logSum[3] {}, count[3] {};

        for (const auto& s : slots)
            for (int k = 0; k < kReceivers; ++k)
            {
                const auto& c = s.c[k];

                if (c.band < 3)
                {
                    logSum[c.band] += std::log (c.pathM);
                    count[c.band]  += 1.0;
                }
            }

        for (int b = 0; b < 3; ++b)
        {
            const auto d = count[b] > 0.0 ? std::exp (logSum[b] / count[b])
                                          : scene.direct[C] + kC * windowMs / 2000.0;
            cutoffHz[b] = cutoffLaw ((double) (b + 1), d);
        }

        cutoffHz[3] = recipe.proximityCutoffHz;

        if (plate)
            for (int b = 0; b < kErBands; ++b)
                cutoffHz[b] = kPlateBandCutoffHz[b];
    }

    //== Assemble, quote at the reference size, and round onto the grid ========
    ErTable table {};

    const auto emit = [&] (const Candidate& c, double theta) noexcept
    {
        ErTap tap {};
        tap.timeMs = (float) quantise (c.timeMs * toRef, kTimeGridMs);
        tap.gain   = (float) quantise (c.gain / toRef, kGainGrid);
        tap.theta  = (float) quantise (theta, kUnitGrid);
        tap.pan    = (float) quantise (c.pan, kUnitGrid);
        tap.band   = c.band;
        return tap;
    };

    for (int v = 0; v < kErVariations; ++v)
    {
        auto& set = table.variation[v];
        set.left.numTaps = set.right.numTaps = 0;

        for (int i = 0; i < slotCount; ++i)
        {
            const auto& s = slots[(size_t) i];
            const auto& u = use[(size_t) i][(size_t) v];

            set.left.taps[set.left.numTaps++]   = emit (s.c[u[0]], s.theta);
            set.right.taps[set.right.numTaps++] = emit (s.c[u[1]], s.theta);
        }

        for (auto* ch : { &set.left, &set.right })
            std::sort (ch->taps, ch->taps + ch->numTaps,
                       [] (const ErTap& a, const ErTap& b) { return a.timeMs < b.timeMs; });
    }

    table.combDelayMs   = (float) quantise (recipe.combDelayMs * toRef, kTimeGridMs);
    table.combGain      = (float) quantise (recipe.combGain, kUnitGrid);
    table.windowMs      = (float) quantise (windowMs * toRef, kTimeGridMs);
    table.windowClampMs = (float) quantise (recipe.windowClampMs, kTimeGridMs);

    // Quoted so that erBandCutoffHzAt gives back this size's cutoff at this size.
    for (int b = 0; b < kErBands; ++b)
        table.bandCutoffHz[b] = (float) quantise (cutoffHz[b] * std::pow (1.0 / toRef, kKappa), kCutoffGrid);

    table.beta = (float) quantise (recipe.beta, kUnitGrid);
    table.seed = seed;

    out = table;

    if (diag != nullptr)
    {
        diag->sizeM            = sizeM;
        diag->directDistM      = scene.direct[C];
        diag->directGain       = 1.0 / scene.direct[C];
        diag->meanFreePathM    = scene.meanFreePath;
        diag->imagesConsidered = (int) candidates.size();
        diag->attemptsRejected = rejected;

        for (int v = 0; v < kErVariations; ++v)
        {
            int n = 0;

            for (const auto& u : use)
                n += u[(size_t) v][0] == C ? 1 : 0;

            diag->sharedSlots[v] = n;
        }
    }

    return true;
}

} // namespace bmo::reverb::ergen
