#include "modules/reverb/dsp/ErAudit.h"

#include "modules/reverb/dsp/DspCore.h"
#include "modules/reverb/dsp/ImageSource.h"
#include "modules/reverb/dsp/TapTables.h"
#include "modules/reverb/params.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

namespace bmo::reverb::ergen
{
namespace
{
    constexpr double kPi   = 3.14159265358979323846;
    constexpr double kNone = std::numeric_limits<double>::infinity();

    /** One tap as it is heard at a given size: the Size law applied. */
    struct Heard
    {
        double t, a, pan, theta, fc;
        int band;
    };

    std::vector<Heard> heard (const ErTable& table, const ErChannel& ch, double sizeM)
    {
        const double scale = sizeM / (double) kReferenceSizeM;
        std::vector<Heard> out;

        for (int i = 0; i < ch.numTaps; ++i)
        {
            const auto& tap = ch.taps[i];
            out.push_back ({ tap.timeMs * scale, tap.gain / scale, tap.pan, tap.theta,
                             (double) erBandCutoffHzAt (table, tap.band, (float) sizeM), tap.band });
        }

        return out;
    }

    /** The density bridge's weight. A core tap (theta 0) is on at every
        density -- 10 section 3's "the core taps never switch off" -- which the
        ramp formula taken literally would contradict at D = 0. */
    double weight (double theta, double density) noexcept
    {
        if (theta <= 0.0)
            return 1.0;

        return std::clamp ((density - theta) / (double) DspCore::kRampWidth, 0.0, 1.0);
    }

    double db (double linear) noexcept { return 20.0 * std::log10 (linear); }
    double dbPower (double power) noexcept { return 10.0 * std::log10 (power); }

    /** The zero-lag correlation integral of two taps, each an impulse through
        its band's one-pole low-pass h(t) = (1 / tau) e^(-t / tau). For tap j
        arriving Delta after tap i, the integral of h_i h_j is
        e^(-Delta / tau_i) / (tau_i + tau_j): continuous-time, so the figure
        belongs to the table rather than to any sample rate or interpolator. */
    double overlap (const Heard& i, double ai, const Heard& j, double aj) noexcept
    {
        const double ti = 1.0 / (2.0 * kPi * i.fc);
        const double tj = 1.0 / (2.0 * kPi * j.fc);
        const double delta = (j.t - i.t) / 1000.0;
        const double decay = delta >= 0.0 ? delta / ti : -delta / tj;

        if (decay > 40.0)
            return 0.0;

        return ai * aj * std::exp (-decay) / (ti + tj);
    }

    double correlation (const std::vector<Heard>& l, const std::vector<double>& al,
                        const std::vector<Heard>& r, const std::vector<double>& ar)
    {
        double lr = 0.0, ll = 0.0, rr = 0.0;

        for (size_t i = 0; i < l.size(); ++i)
            for (size_t j = 0; j < r.size(); ++j)
                lr += overlap (l[i], al[i], r[j], ar[j]);

        for (size_t i = 0; i < l.size(); ++i)
            for (size_t j = 0; j < l.size(); ++j)
                ll += overlap (l[i], al[i], l[j], al[j]);

        for (size_t i = 0; i < r.size(); ++i)
            for (size_t j = 0; j < r.size(); ++j)
                rr += overlap (r[i], ar[i], r[j], ar[j]);

        return lr / std::sqrt (ll * rr);
    }

    std::vector<double> weights (const std::vector<Heard>& taps, double density)
    {
        std::vector<double> w;

        for (const auto& t : taps)
            w.push_back (t.a * weight (t.theta, density));

        return w;
    }

    /** gamma of one of VARIATION 0-5 at one density. VARIATION 6 has no gamma
        to audit: it is mono null (owner, 2026-09-23), L = +E and R = -E, so
        its correlation is -1 by construction, and what is checked of it is
        `monoNullMismatches` below. */
    double gammaAt (const ErTable& table, int v, double sizeM, double density)
    {
        const auto l = heard (table, table.variation[v].left, sizeM);
        const auto r = heard (table, table.variation[v].right, sizeM);
        return correlation (l, weights (l, density), r, weights (r, density));
    }

    /** VARIATION 6's check: the ER mono sum is exactly zero. The engine plays
        the set as L = +E and R = -E, and the table carries that one set in
        both channels (ErTable.h), so the mono sum +E_left - E_right is exactly
        zero when -- and only when -- the two channels are the same taps to
        the bit: same times, gains, thresholds and bands. Returns how many
        taps differ. */
    int monoNullMismatches (const ErTable& table)
    {
        const auto& l = table.variation[kErCombVariation].left;
        const auto& r = table.variation[kErCombVariation].right;

        if (l.numTaps != r.numTaps)
            return kErMaxTaps;

        int differ = 0;

        for (int i = 0; i < l.numTaps; ++i)
        {
            const auto& a = l.taps[i];
            const auto& b = r.taps[i];

            if (a.timeMs != b.timeMs || a.gain != b.gain || a.theta != b.theta || a.band != b.band)
                ++differ;
        }

        return differ;
    }

    std::vector<double> windowsOf (const std::vector<Heard>& taps, double density, int& count)
    {
        double last = 0.0;

        for (const auto& t : taps)
            last = std::max (last, t.t);

        count = (int) (last / 5.0) + 1;
        std::vector<double> e ((size_t) count, 0.0);

        for (const auto& t : taps)
        {
            const auto w = weight (t.theta, density) * t.a;
            e[(size_t) std::min (count - 1, (int) (t.t / 5.0))] += w * w;
        }

        return e;
    }
}

//==============================================================================
const char* ruleName (int rule) noexcept
{
    switch (rule)
    {
        case ruleSeparation: return "separation >= 0.9 ms";
        case ruleGaps:       return "core gaps >= 2 % apart";
        case ruleFullBand:   return "no full-band tap 1-8 ms";
        case ruleKuttruff:   return "Kuttruff 2-20 ms";
        case ruleTapCeiling: return "tap <= -15.3 dB";
        case ruleFlamLate:   return "flam (i) after 25 ms";
        case ruleFlamOnset:  return "flam (ii) no second onset";
        case ruleLoc:        return "flam (iii) LOC, 3 dB";
        case ruleProximity:  return "flam (iv) inside 5 ms";
        case ruleCentre:     return "first tap near centre";
        case ruleGamma:      return "gamma 0-5, Var 6 mono null";
        case ruleLateral:    return "lateral fraction";
        case ruleMoorer:     return "Moorer (Room)";
        case rulePlateOnset:     return "plate: onset <= 2 ms";
        case rulePlateFront:     return "plate: <= 4 % inside 5 ms";
        case rulePlateSwellTime: return "plate: swell peak 10-25 ms";
        case rulePlateSwellRise: return "plate: swell >= 8 dB";
        case rulePlateBands:     return "plate: band onsets";
        case rulePlateLr500:     return "plate: 500 Hz R later 3-7";
        default:             return "?";
    }
}

AuditContext contextFor (int typeIndex)
{
    const auto& c = constantsFor (typeIndex);
    return { c.sizeM, c.erLevelDb, c.erDensity / 100.0f,
             (float) (1.0 / directDistanceM (typeIndex)), typeIndex == room, recipeFor (typeIndex).planar };
}

AuditContext candidateContext (const char* name)
{
    const auto type = candidateType (name);
    auto ctx = contextFor (type < 0 ? 0 : type);

    if (const auto* recipe = candidateRecipe (name))
        ctx.directGain = (float) (1.0 / directDistanceM (*recipe, type));

    return ctx;
}

int windowEnergies (const ErTable& table, const AuditContext& ctx, int variation, bool right,
                    double* out, int maxWindows)
{
    const auto& set = table.variation[std::clamp (variation, 0, kErVariations - 1)];
    const auto taps = heard (table, right ? set.right : set.left, ctx.sizeM);

    int count = 0;
    const auto e = windowsOf (taps, ctx.density, count);

    for (int i = 0; i < std::min (count, maxWindows); ++i)
        out[i] = e[(size_t) i];

    return std::min (count, maxWindows);
}

Report auditAtSize (const ErTable& table, const AuditContext& ctx, float sizeMf)
{
    const double sizeM   = sizeMf;
    const double density = ctx.density;
    const double fader   = std::pow (10.0, ctx.erLevelDb / 20.0);

    Report rep {};

    for (auto& m : rep.margin)
        m = kNone;

    auto& fig = rep.figures;
    fig.energyBefore30 = kNone;
    fig.locMarginDb = kNone;
    fig.proximityShare = 0.0;
    fig.maxTapDb = -kNone;
    fig.largestRiseDb = -kNone;
    fig.worstGapPct = kNone;
    fig.fullSetGapCollisions = 0;

    const auto lower = [&rep] (int rule, double m) { rep.margin[rule] = std::min (rep.margin[rule], m); };

    double proximityMin = kNone, proximityMax = 0.0;

    for (int v = 0; v < kErVariations; ++v)
    {
        for (const auto* ch : { &table.variation[v].left, &table.variation[v].right })
        {
            const auto taps = heard (table, *ch, sizeM);

            if (ch->numTaps != kErMaxTaps)
                lower (ruleSeparation, -1.0e9);

            //== Separation, all 48 ============================================
            for (size_t i = 1; i < taps.size(); ++i)
                lower (ruleSeparation, (taps[i].t - taps[i - 1].t) - kMinSeparationMs);

            //== Core gaps, every pair; and the full set's adjacent pairs ======
            {
                std::vector<double> core;

                for (const auto& t : taps)
                    if (t.theta <= 0.0)
                        core.push_back (t.t);

                std::vector<double> gaps;

                for (size_t i = 1; i < core.size(); ++i)
                    gaps.push_back (core[i] - core[i - 1]);

                for (size_t i = 0; i < gaps.size(); ++i)
                    for (size_t j = i + 1; j < gaps.size(); ++j)
                    {
                        const auto rel = std::abs (gaps[i] - gaps[j]) / std::max (gaps[i], gaps[j]);
                        lower (ruleGaps, rel - 0.02);
                        fig.worstGapPct = std::min (fig.worstGapPct, 100.0 * rel);
                    }

                int collisions = 0;

                for (size_t i = 2; i < taps.size(); ++i)
                {
                    const auto a = taps[i - 1].t - taps[i - 2].t;
                    const auto b = taps[i].t - taps[i - 1].t;

                    if (std::abs (a - b) / std::max (a, b) < 0.02)
                        ++collisions;
                }

                fig.fullSetGapCollisions = std::max (fig.fullSetGapCollisions, collisions);
            }

            //== Levels ========================================================
            for (const auto& t : taps)
            {
                const auto level = db (t.a);
                fig.maxTapDb = std::max (fig.maxTapDb, level);
                lower (ruleTapCeiling, -15.3 - level);

                if (t.t >= 2.0 && t.t <= 20.0)
                    lower (ruleKuttruff, (-0.6 * t.t - 8.0) - level);

                if (t.t >= kProximityLoMs && t.t < kProximityHiMs)
                    lower (ruleFullBand, 1500.0 - t.fc);
            }

            //== Flamming, at the default density ==============================
            {
                double e25 = 0.0, total = 0.0, before30 = 0.0, inside5 = 0.0, heardTotal = 0.0;

                for (const auto& t : taps)
                {
                    const auto w = weight (t.theta, density) * t.a;
                    total += w * w;
                    heardTotal += w * w * kPi * t.fc;

                    if (t.t <= 25.0) e25 += w * w;
                    if (t.t < 30.0)  before30 += w * w;
                    if (t.t <= 5.0)  inside5 += w * w * kPi * t.fc;
                }

                for (const auto& t : taps)
                {
                    const auto w = weight (t.theta, density) * t.a;

                    if (t.t > 25.0 && w > 0.0)
                        lower (ruleFlamLate, -12.0 - dbPower (w * w / e25));
                }

                fig.energyBefore30 = std::min (fig.energyBefore30, before30 / total);

                // (iv) is judged on the energy as heard -- after each tap's
                // band -- because the allocation lives in the proximity band,
                // which is dark on purpose: a broadband sum would count the
                // highs the band removes.
                const auto share = inside5 / heardTotal;
                proximityMin = std::min (proximityMin, share);
                proximityMax = std::max (proximityMax, share);

                // (ii): after the peak window, no 5 ms window holds more than
                // kOnsetToleranceDb over the louder of the two non-empty
                // windows before it. Taken literally -- each window at most
                // the one before, 0 dB -- the rule fails every sparse set
                // there is, because one tap more or less moves a window by a
                // few dB. Three changes make it a test of what it is for, a
                // second onset: empty windows are passed over, since silence
                // then a tap is not a rise of the envelope; the comparison
                // looks back 10 ms rather than 5, so a window holding one
                // barely-switched-on pulse does not make the next look like
                // an onset; and 3 dB -- a doubling -- is the least rise that
                // is a new cluster rather than that fluctuation. The literal
                // figure is reported beside it.
                int count = 0;
                const auto e = windowsOf (taps, density, count);
                const auto peak = (int) (std::max_element (e.begin(), e.end()) - e.begin());
                double before[2] { e[(size_t) peak], 0.0 };

                for (int w = peak + 1; w < count; ++w)
                {
                    if (e[(size_t) w] <= 0.0)
                        continue;

                    fig.largestRiseDb = std::max (fig.largestRiseDb, dbPower (e[(size_t) w] / before[0]));
                    lower (ruleFlamOnset, kOnsetToleranceDb - dbPower (e[(size_t) w] / std::max (before[0], before[1])));
                    before[1] = before[0];
                    before[0] = e[(size_t) w];
                }


                // (iii): every tap on, no renormalisation, the default fader.
                double e100 = 0.0;

                for (const auto& t : taps)
                    if (t.t <= 100.0)
                        e100 += t.a * t.a * fader * fader;

                const auto below = -dbPower (e100);
                fig.locMarginDb = std::min (fig.locMarginDb, below);
                lower (ruleLoc, below - 3.0);
            }

            //== The first reflection near centre ==============================
            if (v < kErCombVariation || ch == &table.variation[v].left)
                lower (ruleCentre, 0.25 - std::abs (taps.front().pan));
        }
    }

    // (iv): present in every channel of every position, and small -- a
    // quarter of the heard ER energy at most. 10 section 3 gives it no number;
    // this one is the pass's, and is only there so that "small" means
    // something a test can fail.
    fig.proximityShare = proximityMax;
    lower (ruleProximity, proximityMin > 0.0 ? 0.25 - proximityMax : -1.0);

    //== gamma ==================================================================
    for (int v = 0; v < kErCombVariation; ++v)
    {
        fig.gamma[v]     = gammaAt (table, v, sizeM, density);
        fig.gammaCore[v] = gammaAt (table, v, sizeM, 0.0);
        fig.gammaFull[v] = gammaAt (table, v, sizeM, 1.0);
    }

    // At the default density and at 100 %: >= 0 at 0-5, about 0.95 at
    // 0, about 0.05 at 5, and falling by at least 0.05 a step -- a step you
    // could not hear would not be a position. At DENSITY 0 the same, except
    // the end: there the core alone carries the ER, the first reflection is
    // a larger share of it, and it never splits, because it is what holds the
    // phantom centre -- so gamma at 5 cannot go below that share, and is held
    // to 0.30 instead of 0.15.
    for (const auto* g : { fig.gamma, fig.gammaCore, fig.gammaFull })
    {
        for (int v = 0; v < kErCombVariation; ++v)
            lower (ruleGamma, g[v]);

        lower (ruleGamma, g[0] - 0.85);
        lower (ruleGamma, (g == fig.gammaCore ? 0.30 : 0.15) - g[kErCombVariation - 1]);

        for (int v = 1; v < kErCombVariation; ++v)
            lower (ruleGamma, (g[v - 1] - g[v]) - 0.05);
    }

    // VARIATION 6 is mono null: its check is that the mono sum is exactly
    // zero, not a gamma. 11 section 6.
    fig.monoNullMismatches = monoNullMismatches (table);
    if (fig.monoNullMismatches != 0)
        lower (ruleGamma, -(double) fig.monoNullMismatches);

    //== Lateral fraction at VARIATION 2 =========================================
    //
    // Two figures, and the rule is on the first.
    //
    // **The room's own lateral fraction** -- Barron's LF as ISO 3382-1 defines
    // it, a figure-of-eight facing the side over an omni, taken of the sound
    // field the table models: each core tap weighted by the square of its
    // image's lateral direction cosine (`pan`), 5-80 ms over 0-80 ms, with the
    // direct sound at the geometry's own 1 m / d in the denominator. Core taps
    // only, because an infill pulse has no image and its bearing is drawn.
    // This is what 10 section 3's 0.10-0.35 describes: a property of the room.
    //
    // **What the stereo output carries of it**, reported beside it: the same
    // ratio taken on the channels, side S = (L - R) / 2 over mid M = (L + R) / 2
    // with the direct in the mid, in the 125-1000 Hz band the spatial measures
    // weight -- which is what VARIATION changes. It comes out far lower,
    // because a split tap's two arrivals are 0.24-0.6 ms apart and so
    // decorrelate only above about a kilohertz; offsets large enough to
    // decorrelate the low-mids cannot be placed 0.9 ms from every other tap at
    // all seven positions in a 100 ms window. That is the open question this
    // figure is here to keep in view (see the testing note), not a rule.
    {
        const auto roomLf = [&]
        {
            double sum = 0.0;
            const double direct = (double) ctx.directGain * (double) ctx.sizeM / sizeM;

            for (const auto* ch : { &table.variation[2].left, &table.variation[2].right })
            {
                double lateral = 0.0, omni = direct * direct;

                for (const auto& t : heard (table, *ch, sizeM))
                {
                    if (t.theta > 0.0)
                        continue;

                    if (t.t <= 80.0)
                        omni += t.a * t.a;

                    if (t.t >= 5.0 && t.t <= 80.0)
                        lateral += t.a * t.a * t.pan * t.pan;
                }

                sum += lateral / omni;
            }

            return 0.5 * sum;
        };

        constexpr double f1 = 125.0, f2 = 1000.0;
        constexpr int bins = 1024;
        const double df = (f2 - f1) / bins;
        const double direct = (double) ctx.directGain * (double) ctx.sizeM / sizeM;
        const auto l = heard (table, table.variation[2].left, sizeM);
        const auto r = heard (table, table.variation[2].right, sizeM);

        const auto stereoLf = [&] (double density)
        {
            double side = 0.0, mid = 0.0;

            // One channel's taps arriving in [lo, hi] ms, at frequency f:
            // the sum of a e^(-j 2 pi f t) / (1 + j f / fc).
            const auto spectrum = [&] (const std::vector<Heard>& taps, double f, double lo, double hi,
                                       double& re, double& im)
            {
                re = im = 0.0;

                for (const auto& t : taps)
                {
                    if (t.t < lo || t.t > hi)
                        continue;

                    const double a = weight (t.theta, density) * t.a;
                    const double ph = -2.0 * kPi * f * t.t / 1000.0;
                    const double x = f / t.fc, den = 1.0 + x * x;
                    const double hr = 1.0 / den, hj = -x / den;
                    re += a * (std::cos (ph) * hr - std::sin (ph) * hj);
                    im += a * (std::cos (ph) * hj + std::sin (ph) * hr);
                }
            };

            for (int b = 0; b < bins; ++b)
            {
                const double f = f1 + (b + 0.5) * df;
                double lr, li, rr, ri;

                spectrum (l, f, 5.0, 80.0, lr, li);
                spectrum (r, f, 5.0, 80.0, rr, ri);
                side += 0.25 * ((lr - rr) * (lr - rr) + (li - ri) * (li - ri));

                spectrum (l, f, 0.0, 80.0, lr, li);
                spectrum (r, f, 0.0, 80.0, rr, ri);
                const double mr = direct + 0.5 * (lr + rr), mi = 0.5 * (li + ri);
                mid += mr * mr + mi * mi;
            }

            return side / mid;
        };

        fig.lateralFraction           = roomLf();
        fig.lateralFractionStereo     = stereoLf (density);
        fig.lateralFractionStereoFull = stereoLf (1.0);
        // Barron's range is a property of rooms -- 189 seats in halls -- and a
        // plate is not one: its images lie in its own plane, so half their
        // energy is lateral by geometry alone and the figure says nothing a
        // listener would. Plate's is computed and printed, and not held to it.
        if (! ctx.isPlate)
            lower (ruleLateral, std::min (fig.lateralFraction - 0.10, 0.25 - fig.lateralFraction));
    }



    //== Spans, and Moorer ======================================================
    {
        const auto taps = heard (table, table.variation[2].left, sizeM);
        fig.firstTapMs = taps.front().t;
        fig.lastTapMs  = taps.back().t;
        fig.coreFirstMs = kNone;
        fig.coreLastMs  = 0.0;
        int core = 0;

        for (const auto& t : taps)
            if (t.theta <= 0.0)
            {
                fig.coreFirstMs = std::min (fig.coreFirstMs, t.t);
                fig.coreLastMs  = std::max (fig.coreLastMs, t.t);
                ++core;
            }

        if (ctx.isRoom)
        {
            // Moorer's 19 taps span 4.3-79.7 ms. A sanity reference, not a
            // target: the count within 25 %, the first tap inside 1-8 ms and
            // the last within 25 % of 79.7 ms.
            lower (ruleMoorer, 0.25 - std::abs (core - 19.0) / 19.0);
            lower (ruleMoorer, std::min (fig.firstTapMs - 1.0, 8.0 - fig.firstTapMs) / 7.0);
            lower (ruleMoorer, 0.25 - std::abs (fig.lastTapMs - 79.7) / 79.7);
        }
    }

    //== Plate: the room rules lifted, the plate rules in their place ==========
    //
    // See ErAudit.h: owner decision, 2026-09-23, and Frosty's plate research.
    // The room rules above ran for every type; for a plate their margins are
    // withdrawn here, so there is one code path for the rooms and not two.
    if (ctx.isPlate)
    {
        for (const int r : { ruleSeparation, ruleGaps, ruleFullBand, ruleKuttruff, ruleFlamLate,
                             ruleFlamOnset, ruleLoc, ruleProximity, ruleCentre, ruleGamma,
                             ruleLateral, ruleMoorer })
            rep.margin[r] = kNone;

        // Mono safety is not a room rule: gamma >= 0 at 0-5, all three densities.
        for (const auto* g : { fig.gamma, fig.gammaCore, fig.gammaFull })
            for (int v = 0; v < kErCombVariation; ++v)
                lower (ruleGamma, g[v]);

        if (fig.monoNullMismatches != 0)
            lower (ruleGamma, -(double) fig.monoNullMismatches);

        // The plate rules. Source: measured, 16 EMT 140 IRs, research doc
        // section 8, 2026-09-23 -- see ErAudit.h. Every channel of VARIATION
        // 0-5, at the default density, on heard energy (each tap through its
        // band's one-pole), which is what an IR measurement sees.
        const auto heardOf = [&] (const Heard& t)
        {
            const auto w = weight (t.theta, density) * t.a;
            return w * w * kPi * t.fc;
        };

        // A band's onset: when its energy reaches 10 % of its first 100 ms.
        const auto onset = [&] (const std::vector<Heard>& taps, int b)
        {
            double total = 0.0, run = 0.0;

            for (const auto& t : taps)
                if (t.band == b && t.t <= 100.0)
                    total += heardOf (t);

            for (const auto& t : taps)
                if (t.band == b && t.t <= 100.0)
                {
                    run += heardOf (t);

                    if (run >= 0.1 * total)
                        return t.t;
                }

            return kNone;
        };

        for (int v = 0; v < kErCombVariation; ++v)
        {
            double on[2][kErBands] {};
            int side = 0;

            for (const auto* ch : { &table.variation[v].left, &table.variation[v].right })
            {
                const auto taps = heard (table, *ch, sizeM);

                lower (rulePlateOnset, 2.0 - taps.front().t);

                double first5 = 0.0, first100 = 0.0;
                std::vector<double> e;

                for (const auto& t : taps)
                {
                    const auto h = heardOf (t);

                    if (t.t <= 5.0)   first5 += h;
                    if (t.t <= 100.0) first100 += h;

                    const auto slot = (size_t) (t.t / 2.0);

                    if (e.size() <= slot)
                        e.resize (slot + 1, 0.0);

                    e[slot] += h;
                }

                const auto share = first5 / first100;
                lower (rulePlateFront, 0.04 - share);

                // The swell, on 2 ms windows: when it peaks, and by how much
                // over the 0-5 ms level (per window, so the two are comparable).
                const auto peak = (size_t) (std::max_element (e.begin(), e.end()) - e.begin());
                const auto peakMs = 2.0 * (double) peak + 1.0;
                const auto rise = dbPower (e[peak] / std::max (first5 / 2.5, 1.0e-30));
                lower (rulePlateSwellTime, std::min (peakMs - 10.0, 25.0 - peakMs));
                lower (rulePlateSwellRise, rise - 8.0);

                for (int b = 0; b < kErBands; ++b)
                    on[side][b] = onset (taps, b);

                for (int b = 1; b < kErBands; ++b)
                    lower (rulePlateBands, on[side][b] - on[side][b - 1]);

                lower (rulePlateBands, 4.0 - on[side][0]);
                lower (rulePlateBands, std::min (on[side][2] - 8.0, 16.0 - on[side][2]));

                if (v == 2)
                {
                    for (int b = 0; b < kErBands; ++b)
                        fig.plateOnsetMs[side][b] = on[side][b];

                    if (side == 0)
                    {
                        fig.plateFrontShare = share;
                        fig.plateRiseDb = rise;
                        fig.platePeakMs = peakMs;
                    }
                }

                ++side;
            }

            // 500 Hz is band 2: the right output's onset 3-7 ms after the left's.
            const auto lr = on[1][2] - on[0][2];
            lower (rulePlateLr500, std::min (lr - 3.0, 7.0 - lr));
        }
    }

    rep.allPass = true;

    for (int r = 0; r < numRules; ++r)
    {
        rep.pass[r] = rep.margin[r] >= 0.0;
        rep.allPass = rep.allPass && rep.pass[r];
    }

    return rep;
}

Report audit (const ErTable& table, const AuditContext& ctx)
{
    return auditAtSize (table, ctx, ctx.sizeM);
}

} // namespace bmo::reverb::ergen
