#pragma once

// The offline half of the early-reflection generator: a shoebox, the
// image-source method to third order, a deterministic jitter, the level
// ceilings 10 section 3 imposes, and an audit of the spacing and flamming
// rules 11 section 6 asserts. JUCE-free, and slow enough to be honest about:
// it is what `measure_reverb tables` runs to *print* the rows that are baked
// into TapTables.h, and what the tests run to prove those rows are the
// geometry's and not somebody's typing. The audio thread never calls it.
//
// **Why the tables are baked and not generated at prepare().** The panel draws
// `kReferenceTaps` and needs them at compile time; the tests need a golden copy
// that is not the same code as the thing under test; and 10 section 8's rule
// that "a failing table is re-seeded, not patched" only means anything if the
// seed is written down next to the numbers. So this file derives, the header
// holds, and `reverb_dsp_tests` asserts the two agree to within a rounding.

#include "modules/reverb/dsp/ErGenerator.h"
#include "modules/reverb/dsp/TapTables.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

namespace bmo::reverb::imagesource
{

//==============================================================================
/** The shoebox and the two positions in it, all as fractions of the room so
    that the whole picture scales with SIZE and the Size law stays exactly
    linear (10 section 3: "scaling times while keeping the pattern preserves
    the room's identity").

    Proportions 1 : 1.4 : 1.9 are the spec's. SIZE is the long dimension. */
struct Geometry
{
    float sizeM       = kReferenceSizeM;   ///< the long dimension, metres
    float heightRatio = 1.0f / 1.9f;       ///< Lz / Ly
    float widthRatio  = 1.4f / 1.9f;       ///< Lx / Ly

    // Source and listener as fractions of (Lx, Ly, Lz). Off centre on
    // purpose: a centred pair makes image pairs coincide, which is the
    // near-coincident flam 04 section 2 warns against.
    float srcX = 0.55f, srcY = 0.44f, srcZ = 0.22f;   // nearly ahead of the listener, so the back wall reads as centre
    float lisX = 0.60f, lisY = 0.86f, lisZ = 0.19f;   // near the back wall: its reflection lands inside 10 ms and near centre

    float beta        = 0.70f;   ///< per-bounce reflection coefficient, CALIBRATE
    float spanMs      = 65.0f;   ///< the window's end at the reference size, CALIBRATE
    std::uint32_t seed = 1;      ///< jitter seed; recorded with the table it produced
};

/** One image before it becomes a `Tap`: everything the audit needs to see. */
struct Image
{
    float timeMs;     ///< arrival relative to the direct sound
    float distanceM;  ///< path length
    int   order;      ///< number of wall bounces, 1..3
    float pan;        ///< sin of the bearing off the listener's facing, -1..+1
    float gain;       ///< after the ceilings below
    float rawGain;    ///< (d0 / d) * beta^order, before any ceiling
};

//== The rules, as numbers ======================================================

inline constexpr int   kOrderMax        = 3;
inline constexpr float kSpeedOfSound    = 343.0f;
inline constexpr float kJitter          = 0.03f;   ///< +-3 %, deterministic per type
inline constexpr float kMinSeparationMs = 0.9f;    ///< no two taps closer than this
inline constexpr float kGapDistinctness = 0.02f;   ///< no two inter-tap gaps within 2 %
inline constexpr float kTapCeilingDb    = -15.3f;  ///< 20 log((1+a)/(1-a)) <= 3 dB
inline constexpr float kDichoticBonusDb = 10.0f;   ///< Zurek: a lateral tap colours ~10 dB less
inline constexpr float kDichoticPan     = 0.30f;   ///< |pan| at which the bonus applies
inline constexpr float kTaperMs         = 10.0f;   ///< the cluster ramps out over its last 10 ms
inline constexpr float kTaperFloor      = 0.25f;   ///< ... to -12 dB, not to silence
inline constexpr float kCentreFirst     = 0.25f;   ///< the first two taps stay inside |pan| < this

/** What each tap's energy is **after the engine's band filter**, at 48 kHz
    and the reference size: gain^2 times what its band's one-pole keeps. The
    flamming rules are about what is heard, and a tap inside 8 ms keeps a
    tenth of its energy through the proximity pole, so the ceilings that
    enforce them and the audit that checks them both work on this figure.
    The band law is the engine's own (`ErGenerator::cutoffHzFor`). */
template <typename Rows>
std::vector<float> effectiveEnergies (const Rows& t, float sizeM = kReferenceSizeM, double rate = 48000.0)
{
    float sumT[ErGenerator::kNumBands] {};
    int   n[ErGenerator::kNumBands] {};

    for (const auto& im : t)
    {
        const auto b = ErGenerator::bandFor (im.timeMs, im.order);
        sumT[b] += im.timeMs;
        ++n[b];
    }

    float cutoff[ErGenerator::kNumBands];
    for (int b = 0; b < ErGenerator::kNumBands; ++b)
        cutoff[b] = ErGenerator::cutoffHzFor (b, sizeM, n[b] > 0 ? sumT[b] / (float) n[b] : 20.0f);

    std::vector<float> e;
    for (const auto& im : t)
        e.push_back (im.gain * im.gain * ErGenerator::onePoleEnergyGain (cutoff[ErGenerator::bandFor (im.timeMs, im.order)], rate));

    return e;
}

/** Kuttruff's ceiling for a tap in the 2-20 ms colouring window, dB relative
    to the direct sound: -0.6 t - 8, with the dichotic bonus once the tap is
    off centre (10 section 3). Outside the window there is no ceiling beyond
    the global one. */
inline float kuttruffCeilingDb (float timeMs, float pan) noexcept
{
    if (timeMs < 2.0f || timeMs > 20.0f)
        return 0.0f;

    return -0.6f * timeMs - 8.0f + (std::abs (pan) >= kDichoticPan ? kDichoticBonusDb : 0.0f);
}


//==============================================================================
/** Every image of orders 1..kOrderMax, sorted by arrival, **before** the
    count is cut, the jitter is applied or any ceiling is imposed. */
inline std::vector<Image> images (const Geometry& g)
{
    const float ly = g.sizeM, lx = g.sizeM * g.widthRatio, lz = g.sizeM * g.heightRatio;
    const float sx = g.srcX * lx, sy = g.srcY * ly, sz = g.srcZ * lz;
    const float rx = g.lisX * lx, ry = g.lisY * ly, rz = g.lisZ * lz;

    const float d0 = std::sqrt ((sx - rx) * (sx - rx) + (sy - ry) * (sy - ry) + (sz - rz) * (sz - rz));

    // The listener faces the source; lateral is the horizontal cross product.
    const float fxLen = std::sqrt ((sx - rx) * (sx - rx) + (sy - ry) * (sy - ry));
    const float fx = (sx - rx) / fxLen, fy = (sy - ry) / fxLen;

    std::vector<Image> out;

    // Along one axis an image sits at 2 l L + s (order |2l|) or at 2 l L - s
    // (order |2l - 1|); the total order is the sum over the three axes.
    for (int lX = -2; lX <= 2; ++lX)
    for (int pX = 0; pX <= 1; ++pX)
    for (int lY = -2; lY <= 2; ++lY)
    for (int pY = 0; pY <= 1; ++pY)
    for (int lZ = -2; lZ <= 2; ++lZ)
    for (int pZ = 0; pZ <= 1; ++pZ)
    {
        const int order = std::abs (2 * lX - pX) + std::abs (2 * lY - pY) + std::abs (2 * lZ - pZ);

        if (order < 1 || order > kOrderMax)
            continue;

        const float ix = 2.0f * (float) lX * lx + (pX ? -sx : sx);
        const float iy = 2.0f * (float) lY * ly + (pY ? -sy : sy);
        const float iz = 2.0f * (float) lZ * lz + (pZ ? -sz : sz);

        const float dx = ix - rx, dy = iy - ry, dz = iz - rz;
        const float d  = std::sqrt (dx * dx + dy * dy + dz * dz);

        const float hLen = std::sqrt (dx * dx + dy * dy);
        const float pan  = hLen > 1.0e-6f ? (fx * dy - fy * dx) / hLen : 0.0f;

        Image im;
        im.timeMs    = (d - d0) / kSpeedOfSound * 1000.0f;
        im.distanceM = d;
        im.order     = order;
        im.pan       = std::clamp (pan, -1.0f, 1.0f);
        im.rawGain   = (d0 / d) * std::pow (g.beta, (float) order);
        im.gain      = im.rawGain;
        out.push_back (im);
    }

    std::sort (out.begin(), out.end(), [] (const Image& a, const Image& b) { return a.timeMs < b.timeMs; });
    return out;
}

/** The table: `count` of the images, jittered by the seed, picked, ceilinged,
    tapered, sorted. This is what `TapTables.h` holds a printed copy of. */
inline std::vector<Image> table (const Geometry& g, int count = kNumReferenceTaps)
{
    auto candidates = images (g);

    // The jitter goes on before the pick, not after it, so every rule below is
    // applied to the times that will actually play.
    for (size_t c = 0; c < candidates.size(); ++c)
        candidates[c].timeMs *= 1.0f + kJitter * signedUnit (g.seed, (std::uint32_t) c);

    std::sort (candidates.begin(), candidates.end(), [] (const Image& a, const Image& b) { return a.timeMs < b.timeMs; });

    // **Which 21 of the several hundred images.** The earliest 21 of a shoebox
    // arrive in symmetric pairs inside a few milliseconds of each other, so
    // the pick is made against a log-spaced grid over the type's window, and
    // an image is only taken if the two gaps it makes are 0.9 ms or more and
    // differ from every gap already in the table by the distinctness rule.
    // Grid slots with no image near them are left empty and filled afterwards
    // by the strongest remaining image that still obeys both rules. The times,
    // gains and bearings are all the room's; what the grid decides is which of
    // its reflections are represented, which is what a hand-picked geometric
    // table such as Moorer's also did.
    std::vector<Image> all;
    std::vector<bool>  used (candidates.size(), false);

    if (candidates.empty())
        return all;

    const auto admissible = [&all] (float t)
    {
        // Gaps the table would have with `t` inserted, and the one it loses.
        std::vector<float> gaps;
        float lost = -1.0f;

        std::vector<float> times;
        for (const auto& im : all)
            times.push_back (im.timeMs);
        times.push_back (t);
        std::sort (times.begin(), times.end());

        for (size_t k = 1; k < times.size(); ++k)
            gaps.push_back (times[k] - times[k - 1]);

        for (size_t k = 1; k < all.size(); ++k)
        {
            const auto a = all[k - 1].timeMs, b = all[k].timeMs;
            if (a < t && t < b)
                lost = b - a;
        }
        (void) lost;

        for (size_t i = 0; i < gaps.size(); ++i)
        {
            if (gaps[i] < kMinSeparationMs)
                return false;

            for (size_t j = i + 1; j < gaps.size(); ++j)
                if (std::abs (gaps[i] - gaps[j]) / std::max (gaps[i], gaps[j]) < kGapDistinctness * 1.5f)
                    return false;
        }

        return true;
    };

    const auto take = [&] (size_t c)
    {
        used[c] = true;
        all.push_back (candidates[c]);
        std::sort (all.begin(), all.end(), [] (const Image& a, const Image& b) { return a.timeMs < b.timeMs; });
    };

    const float t0 = std::max (candidates.front().timeMs, 1.0f);
    const float t1 = std::max (g.spanMs, t0 * 2.0f);
    const float nearEnough = std::log (1.3f);

    for (int j = 0; j < count; ++j)
    {
        const float target = t0 * std::pow (t1 / t0, (float) j / (float) (count - 1));

        int   best = -1;
        float bestCost = nearEnough;

        for (size_t c = 0; c < candidates.size(); ++c)
        {
            if (used[c] || candidates[c].timeMs > g.spanMs)
                continue;

            const auto cost = std::abs (std::log (candidates[c].timeMs / target));

            if (cost < bestCost && admissible (candidates[c].timeMs))
            {
                bestCost = cost;
                best = (int) c;
            }
        }

        if (best >= 0)
            take ((size_t) best);
    }

    // Fill: the strongest admissible image inside the window, until the
    // count is met or nothing admissible is left.
    while ((int) all.size() < count)
    {
        int best = -1;

        for (size_t c = 0; c < candidates.size(); ++c)
        {
            if (used[c] || candidates[c].timeMs > g.spanMs)
                continue;

            if ((best < 0 || candidates[c].rawGain > candidates[(size_t) best].rawGain)
                 && admissible (candidates[c].timeMs))
                best = (int) c;
        }

        if (best < 0)
            break;

        take ((size_t) best);
    }


    const float span = all.empty() ? 0.0f : all.back().timeMs;

    for (auto& im : all)
    {
        auto db = 20.0f * std::log10 (im.rawGain);

        db = std::min (db, kTapCeilingDb);
        db = std::min (db, kuttruffCeilingDb (im.timeMs, im.pan));

        auto gain = std::pow (10.0f, db * 0.05f);

        // The ramp-out: the last 10 ms fall by a raised cosine to a quarter,
        // so the cluster does not end on its loudest late tap.
        if (im.timeMs > span - kTaperMs)
        {
            const auto x = (im.timeMs - (span - kTaperMs)) / kTaperMs;   // 0..1
            gain *= kTaperFloor + (1.0f - kTaperFloor) * 0.5f * (1.0f + std::cos (x * 3.14159265f));
        }

        im.gain = gain;
    }

    // Flamming (i), applied the way Kuttruff's ceiling is -- as a ceiling and
    // not an exclusion: nothing after 25 ms may exceed -12 dB relative to the
    // cluster's cumulative energy at 25 ms, **as heard through the band
    // filters**. A reflective type (Cavern, beta 0.88) hands over late taps
    // the physics puts above that line, and the rule 10 section 3 wrote for a
    // lead vocal wins over the physics.
    {
        const auto e = effectiveEnergies (all);

        float e25 = 0.0f;
        for (size_t k = 0; k < all.size(); ++k)
            if (all[k].timeMs <= 25.0f)
                e25 += e[k];

        const auto ceiling = e25 * std::pow (10.0f, -1.2f) * 0.99f;   // 1 % under: the rows print at four decimals

        for (size_t k = 0; k < all.size(); ++k)
            if (all[k].timeMs > 25.0f && e[k] > ceiling)
                all[k].gain *= std::sqrt (ceiling / e[k]);
    }

    // Flamming (ii), likewise as a ceiling: after the peak 5 ms window the
    // energy per window never rises, so there is no second onset. A window
    // that would rise is scaled down to the one before it.
    {
        const auto e = effectiveEnergies (all);
        const auto numWindows = (size_t) std::ceil (span / 5.0f) + 1;
        std::vector<float> energy (numWindows, 0.0f);

        for (size_t k = 0; k < all.size(); ++k)
            energy[(size_t) (all[k].timeMs / 5.0f)] += e[k];

        size_t peak = 0;
        for (size_t w = 1; w < numWindows; ++w)
            if (energy[w] > energy[peak])
                peak = w;

        // "The window before" is the last one that held a tap: a sparse
        // cluster has empty windows, and an empty window is not a ceiling of
        // zero on everything after it.
        auto previous = energy[peak];

        for (size_t w = peak + 1; w < numWindows; ++w)
        {
            if (energy[w] <= 0.0f)
                continue;

            if (energy[w] > previous)
            {
                const auto g = std::sqrt (previous / energy[w]) * 0.995f;   // likewise

                for (auto& im : all)
                    if ((size_t) (im.timeMs / 5.0f) == w)
                        im.gain *= g;

                energy[w] = previous * 0.99f;
            }

            previous = energy[w];
        }
    }

    return all;
}

//==============================================================================
/** The rules 11 section 6 asserts on a table, as one pass that names the
    first rule broken. Empty string means the table passed. */
inline const char* audit (const std::vector<Image>& t)
{
    if (t.size() < 3)
        return "too few taps";

    if ((int) t.size() < kNumReferenceTaps)
        return "fewer than 21 taps could be placed";

    for (size_t k = 1; k < t.size(); ++k)
        if (t[k].timeMs - t[k - 1].timeMs < kMinSeparationMs)
            return "two taps closer than 0.9 ms";

    std::vector<float> gaps;
    for (size_t k = 1; k < t.size(); ++k)
        gaps.push_back (t[k].timeMs - t[k - 1].timeMs);

    for (size_t i = 0; i < gaps.size(); ++i)
        for (size_t j = i + 1; j < gaps.size(); ++j)
            if (std::abs (gaps[i] - gaps[j]) / std::max (gaps[i], gaps[j]) < kGapDistinctness)
                return "two inter-tap gaps within 2 % of each other";

    if (std::abs (t[0].pan) >= kCentreFirst || std::abs (t[1].pan) >= kCentreFirst)
        return "the first two taps are not near centre";

    for (const auto& im : t)
        if (20.0f * std::log10 (im.gain) > kTapCeilingDb + 1.0e-3f)
            return "a tap above -15.3 dB";

    // Griesinger (iv): a deliberate allocation inside 5 ms.
    if (t[0].timeMs >= 5.0f)
        return "no tap inside 5 ms";

    const auto e = effectiveEnergies (t);

    // Flamming (i): nothing after 25 ms above -12 dB relative to the cumulative
    // ER energy at 25 ms, as heard through the band filters.
    {
        float e25 = 0.0f;
        for (size_t k = 0; k < t.size(); ++k)
            if (t[k].timeMs <= 25.0f)
                e25 += e[k];

        for (size_t k = 0; k < t.size(); ++k)
            if (t[k].timeMs > 25.0f && e[k] > e25 * std::pow (10.0f, -1.2f))
                return "a tap after 25 ms above -12 dB re the energy at 25 ms";
    }

    // Flamming (ii): energy in successive 5 ms windows never rises again after
    // the peak window -- there is no second onset.
    {
        std::vector<float> windows ((size_t) std::ceil (t.back().timeMs / 5.0f) + 1, 0.0f);

        for (size_t k = 0; k < t.size(); ++k)
            windows[(size_t) (t[k].timeMs / 5.0f)] += e[k];

        size_t peak = 0;
        for (size_t w = 1; w < windows.size(); ++w)
            if (windows[w] > windows[peak])
                peak = w;

        auto previous = windows[peak];

        for (size_t w = peak + 1; w < windows.size(); ++w)
        {
            if (windows[w] <= 0.0f)
                continue;

            if (windows[w] > previous * 1.0001f)
                return "a second onset after the peak 5 ms window";

            previous = windows[w];
        }
    }

    // Flamming (iii), 11 section 6's form: at least half the energy before 30 ms.
    {
        float before = 0.0f, total = 0.0f;
        for (size_t k = 0; k < t.size(); ++k)
        {
            total += e[k];
            if (t[k].timeMs < 30.0f)
                before += e[k];
        }

        if (before < 0.5f * total)
            return "less than half the ER energy before 30 ms";
    }

    return "";
}

/** Try seeds upward from `g.seed` until the audit passes, and say which one
    did. A table that never passes returns the last seed tried and the tool
    prints the reason; it does not paper over it. */
inline Geometry reseeded (Geometry g, int maxTries, const char** reason = nullptr)
{
    const char* why = "";

    for (int i = 0; i < maxTries; ++i)
    {
        why = audit (table (g));
        if (why[0] == 0)
            break;

        ++g.seed;
    }

    if (reason != nullptr)
        *reason = why;

    return g;
}

//==============================================================================
/** The six geometries, one per type, in the frozen type order. Everything
    but the seed is a design choice and everything is CALIBRATE except Room's
    beta, which is 10 section 3's own lower figure. The seeds are what
    `reseeded` found; a table is re-seeded, not patched. */
inline Geometry geometryFor (int typeIndex)
{
    Geometry g;

    switch (typeIndex)
    {
        default:
        case 0: g.beta = 0.70f; g.spanMs =  65.0f; g.seed = 2; break;                                                          // Room
        case 1: g.beta = 0.76f; g.spanMs =  75.0f; g.seed = 1; g.srcX = 0.56f; g.srcY = 0.40f;                          break;  // Chamber  CALIBRATE
        case 2: g.beta = 0.82f; g.spanMs =  95.0f; g.seed = 2; g.srcX = 0.54f; g.srcY = 0.30f; g.lisY = 0.88f;          break;  // Hall     CALIBRATE
        case 3: g.beta = 0.88f; g.spanMs = 110.0f; g.seed = 1; g.srcX = 0.53f; g.srcY = 0.26f; g.lisX = 0.52f; g.heightRatio = 0.62f; break;  // Cavern   CALIBRATE
        case 4: g.beta = 0.80f; g.spanMs =  60.0f; g.seed = 8; g.heightRatio = 0.30f; g.srcZ = 0.40f; g.lisZ = 0.42f; break;  // Plate    CALIBRATE
        case 5: g.beta = 0.66f; g.spanMs =  60.0f; g.seed = 5; g.srcX = 0.56f; g.srcY = 0.50f; g.lisY = 0.84f;          break;  // Ambience CALIBRATE
    }

    return g;
}

} // namespace bmo::reverb::imagesource
