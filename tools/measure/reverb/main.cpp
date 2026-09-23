/*
    Offline measurement harness for BMO Linger.

    Links the DSP core directly -- no plugin host, no GUI, no JUCE -- the same
    shape as tools/measure/deq and tools/measure/vcomp.

    **It is registered before it is useful, and that is deliberate.** The core
    it drives is still the placeholder in modules/reverb/dsp/DspCore.h, so
    there is no impulse response, no decay and no echo density to print yet.
    What it can answer honestly today are the things that are true of the
    placeholder and will still be true of the finished module:

        measure_reverb latency      the reported delay at every setting, and
                                    the delay actually measured by running an
                                    impulse through the core -- zero
                                    everywhere, permanently
        measure_reverb tail         the figure `tailSecondsForParams` will
                                    report once ModuleDsp has the accessor,
                                    across the schema's corners and against
                                    the 30 s ceiling
        measure_reverb taps         the panel's placeholder tap table at a given
                                    SIZE (TapTables.h), which the display is
                                    still drawn from
        measure_reverb taps --audit every ER audit's margin for every shipped
                                    table (ErTable.cpp); --emit writes those
                                    tables from the generator, --reseed,
                                    --best and --try search seeds and
                                    geometry, --dump shows one type
        measure_reverb constants    the internal constants v1 ships, so the
                                    value being argued about is the value in
                                    the build

    docs/reverb/11-integration-and-test-plan.md section 6 lists the modes this
    grows when the engine lands -- `ir t60 er density mono sweep bench` -- with
    WAVs going to the gitignored packages/reverb-listening/. **No audio is ever
    written into the tree**; twice a tool in this repository has done that.

    Two of those modes decide things nothing else can. `bench` is the only way
    the 10 section 6 budget gets a number: 60 s of noise at 48 kHz/128,
    Release, median of five, against 1.5 % of one core -- and 10 section 8's
    worst case (DENSITY at 48 taps, three diffuser stages, 192 kHz) is the
    first thing to measure, not the last. `density` prints the normalised echo
    density curve, which is how the Abel-Huang crossings at 0.3 and 0.7 get
    placed at recognisable knob positions.

    **Every recorded result names the machine it ran on (AURORA / ICE QUEEN) in
    testing-notes/.**
*/

#include "modules/reverb/dsp/ErAudit.h"
#include "modules/reverb/dsp/ErTable.h"
#include "modules/reverb/dsp/ImageSource.h"
#include "modules/reverb/dsp/ReverbDsp.h"
#include "modules/reverb/dsp/TapTables.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

using namespace bmo::reverb;

namespace
{

constexpr double kSampleRate = 48000.0;

/** Every parameter at its schema default, in `Index` order. Built from
    `specs()` so it cannot drift from the schema. */
std::vector<float> defaults()
{
    std::vector<float> v;

    for (const auto& s : specs())
        v.push_back (s.def);

    return v;
}

/** Where an impulse comes back out, in samples: the index of the largest
    magnitude in the core's output. The reported latency has to equal this, or
    the module is lying to the host about its delay. */
int measuredDelay (const std::vector<float>& values)
{
    ReverbDsp dsp;
    dsp.prepare (kSampleRate, 1024, 2);
    dsp.setParams (values.data(), (int) values.size());

    constexpr int n = 1024;
    std::vector<float> left ((size_t) n, 0.0f), right ((size_t) n, 0.0f);
    left[0] = 1.0f;
    right[0] = 1.0f;

    float* channels[] { left.data(), right.data() };
    dsp.process (channels, 2, n);

    int best = 0;

    for (int i = 1; i < n; ++i)
        if (std::fabs (left[(size_t) i]) > std::fabs (left[(size_t) best]))
            best = i;

    return best;
}

void printLatency()
{
    std::printf ("latency, %g Hz\n", kSampleRate);
    std::printf ("  %-28s %8s %8s\n", "setting", "reported", "measured");

    const auto row = [] (const char* what, const std::vector<float>& v)
    {
        ReverbDsp dsp;
        dsp.prepare (kSampleRate, 1024, 2);

        std::printf ("  %-28s %8d %8d\n", what,
                     dsp.latencyForParams (v.data(), (int) v.size()),
                     measuredDelay (v));
    };

    row ("defaults", defaults());

    {
        auto v = defaults();
        v[Index::predelay] = 250.0f;
        row ("pre-delay at maximum", v);
    }

    {
        auto v = defaults();
        v[Index::decay] = 20.0f;
        v[Index::damphi] = 2.0f;
        row ("40 s of effective decay", v);
    }

    for (int t = 0; t < numTypes; ++t)
    {
        auto v = defaults();
        v[Index::type] = (float) t;
        row (kTypeNames[t], v);
    }

    std::printf ("\n  Zero everywhere, and permanently: there is no lookahead,\n"
                 "  no oversampling and no negative pre-delay (10 sections 1, 2).\n");
}

void printTail()
{
    std::printf ("tail report, seconds -- the figure 11 section 2a will wire up at M5\n");
    std::printf ("  %-34s %10s\n", "setting", "seconds");

    const auto row = [] (const char* what, const DspCore::Params& p)
    {
        std::printf ("  %-34s %10.3f\n", what, (double) DspCore::tailSecondsFor (p));
    };

    row ("defaults", DspCore::Params {});

    {
        DspCore::Params p;
        p.decaySeconds = 0.1f;
        p.sizeM = 0.5f;
        row ("shortest decay, smallest room", p);
    }

    {
        DspCore::Params p;
        p.decaySeconds = 20.0f;
        p.dampLo = 2.0f;
        p.dampHi = 2.0f;
        p.preDelayMs = 250.0f;
        p.sizeM = 80.0f;
        row ("everything at maximum (clamped)", p);
    }

    {
        DspCore::Params p;
        p.decaySeconds = 6.0f;
        p.dampLo = 0.1f;
        p.dampHi = 0.1f;
        row ("6 s, both multipliers at 0.10", p);
    }

    std::printf ("\n  preDelay + T_mid * max(1, r_lo, r_hi) + t_ER,max + 0.05 s,\n"
                 "  clamped to %.0f s. The rack SUMS this over occupied slots\n"
                 "  rather than taking the maximum: slots are in series.\n",
                 (double) DspCore::kMaxTailSeconds);
}

void printTaps (float sizeM)
{
    std::printf ("ER taps at SIZE %.1f m (reference %.1f m)\n",
                 (double) sizeM, (double) kReferenceSizeM);
    std::printf ("  %3s %10s %10s %10s %8s\n", "k", "t (ms)", "gain", "dB", "pan");

    for (int i = 0; i < kNumReferenceTaps; ++i)
    {
        const auto& t = kReferenceTaps[i];
        const auto gain = tapGainAt (t, sizeM);

        std::printf ("  %3d %10.3f %10.4f %10.2f %8.2f\n", i,
                     (double) tapTimeMsAt (t, sizeM), (double) gain,
                     (double) (20.0f * std::log10 (gain)), (double) t.pan);
    }

    std::printf ("\n  span %.2f ms\n", (double) erSpanMsAt (sizeM));
    std::printf ("\n  **PLACEHOLDER GEOMETRY.** This is TapTables.h, which the panel's\n"
                 "  display still reads; none of 11 section 6's rules is claimed of it.\n"
                 "  The shipped early-reflection tables are ErTable.cpp's, from the\n"
                 "  image-source generator: `taps --audit` prints them against every\n"
                 "  rule. Moving the panel onto them is integration's.\n");
}

void printConstants()
{
    std::printf ("internal constants -- fixed at first ship, not parameters\n\n");

    std::printf ("  FDN lines                 %d\n", DspCore::kNumLines);
    std::printf ("  speed of sound            %.1f m/s\n", (double) DspCore::kSpeedOfSound);
    std::printf ("  density ramp width        %.3f      CALIBRATE\n", (double) DspCore::kRampWidth);
    std::printf ("  crossfade                 %.1f ms\n", (double) DspCore::kCrossfadeMs);
    std::printf ("  coefficient smoothing     %.1f ms\n", (double) DspCore::kSmoothingMs);
    std::printf ("  wet fade in reset()       %.1f ms\n", (double) DspCore::kBypassFadeMs);
    std::printf ("  reported tail ceiling     %.1f s\n", (double) DspCore::kMaxTailSeconds);
    std::printf ("  base ER tap count         %d\n", kNumReferenceTaps);
    std::printf ("  reference room size       %.1f m\n", (double) kReferenceSizeM);

    std::printf ("\n  **Eight lines is the live risk.** The mode-density rule scales\n"
                 "  with decay -- Sum(m_i) >= 0.15 * T60 * fs -- so eight cover Hall\n"
                 "  to about 2.9 s and Plate to barely 1 s (10 section 4). Sixteen\n"
                 "  lines is one of three ways out and takes the CPU budget with it.\n");

    std::printf ("\n  Not listed here because they do not exist yet: beta per type,\n"
                 "  the per-tap cutoff law, the eight delay times per type, the\n"
                 "  three reserved era fields. 10 section 1 names the categories a\n"
                 "  type's constant block holds and gives no numbers for any of them.\n");
}

void printSchema()
{
    std::printf ("schema -- %d parameters, %d host lanes spare in a rack slot\n\n",
                 (int) specs().size(), 32 - (int) specs().size());

    std::printf ("  %3s %-14s %-16s %10s %10s %10s %6s\n",
                 "i", "id", "name", "min", "max", "default", "log");

    for (size_t i = 0; i < specs().size(); ++i)
    {
        const auto& s = specs()[i];

        std::printf ("  %3d %-14s %-16s %10.3f %10.3f %10.3f %6s\n",
                     (int) i, s.id, s.name, (double) s.min, (double) s.max, (double) s.def,
                     s.logarithmic ? "yes" : "");
    }
}

//==============================================================================
// The early-reflection tables: `taps --audit`, `--emit`, `--reseed`, `--dump`.

const char* typeName (int t) { return ergen::recipeFor (t).name; }

/** The shortest decimal that reads back as exactly `v`, as a float literal.
    Every number the generator produces sits on a coarse grid, so this is
    usually the grid's own digits -- readable, and exact. */
std::string literal (float v)
{
    char buf[48];

    for (int p = 6; p <= 9; ++p)
    {
        std::snprintf (buf, sizeof (buf), "%.*g", p, (double) v);

        if (std::strtof (buf, nullptr) == v)
            break;
    }

    std::string s { buf };

    if (s.find_first_of (".en") == std::string::npos)
        s += ".0";

    return s + "f";
}

int emitTables (const char* path)
{
    std::FILE* f = std::fopen (path, "wb");

    if (f == nullptr)
    {
        std::printf ("cannot write %s\n", path);
        return 1;
    }

    std::fprintf (f,
        "// GENERATED by `measure_reverb taps --emit`. Do not edit by hand.\n"
        "//\n"
        "// BMO Linger's early-reflection tables: the image-source generator in\n"
        "// ImageSource.cpp, run once per type at its pinned seed and audited by\n"
        "// ErAudit.cpp. `reverb_dsp_tests` regenerates every table and asserts it\n"
        "// equals this file exactly, so a change to the generator, a recipe or a\n"
        "// type's default SIZE turns it red until this is re-emitted and the audits\n"
        "// re-run. **A table that fails an audit is re-seeded, not patched**\n"
        "// (docs/reverb/10-dsp-spec.md section 8): change the recipe's seed, never a\n"
        "// number here.\n"
        "//\n"
        "// Quoted at kReferenceSizeM. Each tap: { timeMs, gain, theta, pan, band }.\n"
        "// Inside ErTable.cpp's namespace; included once, from there.\n\n"
        "static const ErTable kErTables[%d]\n{\n", (int) numTypes);

    for (int t = 0; t < numTypes; ++t)
    {
        ErTable table;

        if (! ergen::generate (t, ergen::recipeFor (t).seed, table))
        {
            std::fclose (f);
            std::printf ("%s: its pinned seed %u cannot be placed; nothing emitted that can be trusted\n",
                         typeName (t), ergen::recipeFor (t).seed);
            return 1;
        }

        std::fprintf (f, "    // %s -- seed %u, beta %s\n    {\n        {\n",
                      typeName (t), table.seed, literal (table.beta).c_str());

        for (int v = 0; v < kErVariations; ++v)
        {
            std::fprintf (f, "            // VARIATION %d\n            {\n", v);

            for (const auto* ch : { &table.variation[v].left, &table.variation[v].right })
            {
                std::fprintf (f, "                { { // %s\n", ch == &table.variation[v].left ? "left" : "right");

                for (int i = 0; i < ch->numTaps; ++i)
                {
                    const auto& tap = ch->taps[i];
                    std::fprintf (f, "                    { %s, %s, %s, %s, %d },\n",
                                  literal (tap.timeMs).c_str(), literal (tap.gain).c_str(),
                                  literal (tap.theta).c_str(), literal (tap.pan).c_str(), tap.band);
                }

                std::fprintf (f, "                  }, %d },\n", ch->numTaps);
            }

            std::fprintf (f, "            },\n");
        }

        std::fprintf (f, "        },\n");
        std::fprintf (f, "        %s, %s,   // combDelayMs, combGain\n",
                      literal (table.combDelayMs).c_str(), literal (table.combGain).c_str());
        std::fprintf (f, "        %s, %s,   // windowMs, windowClampMs\n",
                      literal (table.windowMs).c_str(), literal (table.windowClampMs).c_str());
        std::fprintf (f, "        { %s, %s, %s, %s },   // bandCutoffHz\n",
                      literal (table.bandCutoffHz[0]).c_str(), literal (table.bandCutoffHz[1]).c_str(),
                      literal (table.bandCutoffHz[2]).c_str(), literal (table.bandCutoffHz[3]).c_str());
        std::fprintf (f, "        %s,   // beta\n        %uu   // seed\n    },\n",
                      literal (table.beta).c_str(), table.seed);
    }

    std::fprintf (f, "};\n");
    std::fclose (f);
    std::printf ("wrote %s\n", path);
    return 0;
}

void printAudit (int t, const ErTable& table, float directGain = 0.0f)
{
    auto ctx = ergen::contextFor (t);

    if (directGain > 0.0f)
        ctx.directGain = directGain;   // a --try geometry, not the shipped one

    const auto rep = ergen::audit (table, ctx);
    const auto ref = ergen::auditAtSize (table, ctx, kReferenceSizeM);

    std::printf ("\n%s -- seed %u, beta %.2f, default SIZE %.1f m, DENSITY %.0f %%, ER %.1f dB\n",
                 typeName (t), table.seed, (double) table.beta, (double) ctx.sizeM,
                 (double) ctx.density * 100.0, (double) ctx.erLevelDb);
    std::printf ("  %-28s %12s %-6s %14s\n", "rule (at default SIZE)", "margin", "unit", "at 12 m, info");

    static const char* units[ergen::numRules] { "ms", "frac", "Hz", "dB", "dB", "dB", "dB", "dB", "frac", "pan", "gamma", "LF", "frac", "ms", "dB", "ms" };

    for (int r = 0; r < ergen::numRules; ++r)
    {
        const auto m = rep.margin[r];
        const auto mr = ref.margin[r];

        if (std::isinf (m)) std::printf ("  %-28s %12s %-6s", ergen::ruleName (r), "n/a", "");
        else                std::printf ("  %-28s %12.4f %-6s", ergen::ruleName (r), m, units[r]);

        if (std::isinf (mr)) std::printf (" %14s", "n/a");
        else                 std::printf (" %14.4f", mr);

        std::printf ("  %s\n", rep.pass[r] ? "pass" : "FAIL");
    }

    const auto& f = rep.figures;
    std::printf ("  gamma 0-6, default density:");
    for (double g : f.gamma) std::printf (" %.3f", g);
    std::printf ("\n  gamma 0-6, core only:      ");
    for (double g : f.gammaCore) std::printf (" %.3f", g);
    std::printf ("\n  gamma 0-6, every tap:      ");
    for (double g : f.gammaFull) std::printf (" %.3f", g);
    std::printf ("\n  lateral fraction (VARIATION 2): room %.3f; stereo S/M 125-1000 Hz %.3f at default density, %.3f at 100 %%\n",
                 f.lateralFraction, f.lateralFractionStereo, f.lateralFractionStereoFull);
    std::printf ("  ER energy before 30 ms: %.1f %% (the dropped >= 50 %% rule of 11 section 6, measured)\n",
                 100.0 * f.energyBefore30);
    std::printf ("  LOC: ER in 100 ms is %.1f dB below direct at the default fader\n", f.locMarginDb);
    std::printf ("  inside 5 ms: up to %.1f %% of the ER energy\n", 100.0 * f.proximityShare);
    std::printf ("  span (VARIATION 2, left): %.2f - %.2f ms; core %.2f - %.2f ms; loudest tap %.2f dB\n",
                 f.firstTapMs, f.lastTapMs, f.coreFirstMs, f.coreLastMs, f.maxTapDb);
    std::printf ("  closest core gap pair %.2f %% apart; full-set adjacent gap pairs within 2 %%: %d (reported, not a rule)\n",
                 f.worstGapPct, f.fullSetGapCollisions);

    if (ctx.isPlate)
    {
        // Against the research; reported, not asserted (see ErAudit.h).
        std::printf ("  plate: heard energy inside 5 ms %.1f %%; span %.2f - %.2f ms (research ~30)\n",
                     100.0 * f.plateFrontShare, f.firstTapMs, f.lastTapMs);

        for (int b = 0; b < kErBands; ++b)
            std::printf ("  plate band %d (%5.1f m/s): first %6.2f ms (x%.2f of band 0; 1/sqrt f gives x%.2f), "
                         "mean %6.2f ms, VAR 5 mean |L-R| %.2f ms\n", b, ergen::kPlateBandSpeed[b],
                         f.plateBandFirstMs[b],
                         f.plateBandFirstMs[0] > 0.0 ? f.plateBandFirstMs[b] / f.plateBandFirstMs[0] : 0.0,
                         ergen::kPlateBandSpeed[0] / ergen::kPlateBandSpeed[b],
                         f.plateBandMeanMs[b], f.plateLrMs[b]);
    }

    std::printf ("  => %s\n", rep.allPass ? "ALL PASS" : "FAILS");
}

int tapsCommand (int argc, char** argv)
{
    const std::string flag { argv[0] };

    if (flag == "--audit")
    {
        bool all = true;

        for (int t = 0; t < numTypes; ++t)
        {
            printAudit (t, erTableFor (t));
            all = all && ergen::audit (erTableFor (t), ergen::contextFor (t)).allPass;
        }

        std::printf ("\nshipped tables: %s\n", all ? "every rule passes at every type"
                                                   : "AT LEAST ONE RULE FAILS");
        return all ? 0 : 1;
    }

    if (flag == "--reseed")
    {
        // The first seed from `start` whose table generates and passes every
        // audit. Prints it and changes nothing on disk: the seed is pinned by
        // editing the recipe, and --emit then writes the table.
        const int t = argc > 1 ? std::atoi (argv[1]) : 0;
        const auto start = argc > 2 ? (std::uint32_t) std::strtoul (argv[2], nullptr, 10) : 1u;
        const auto count = argc > 3 ? (std::uint32_t) std::strtoul (argv[3], nullptr, 10) : 20000u;
        int failures[ergen::numRules] {};
        int unplaced = 0;

        for (std::uint32_t s = start; s < start + count; ++s)
        {
            ErTable table;

            if (! ergen::generate (t, s, table))
            {
                ++unplaced;
                continue;
            }

            const auto rep = ergen::audit (table, ergen::contextFor (t));

            if (rep.allPass)
            {
                std::printf ("%s: seed %u passes (%u tried, %d could not be placed)\n",
                             typeName (t), s, s - start + 1, unplaced);
                printAudit (t, table);
                return 0;
            }

            for (int r = 0; r < ergen::numRules; ++r)
                failures[r] += rep.pass[r] ? 0 : 1;
        }

        std::printf ("%s: no seed in %u..%u passes; %d could not be placed\n",
                     typeName (t), start, start + count - 1, unplaced);

        for (int r = 0; r < ergen::numRules; ++r)
            std::printf ("  %-28s failed %d\n", ergen::ruleName (r), failures[r]);

        return 1;
    }

    if (flag == "--best")
    {
        // For a type no seed passes: the seed whose worst failing margin is
        // least bad, among those that pass every other rule. Prints it and
        // changes nothing on disk.
        const int t = argc > 1 ? std::atoi (argv[1]) : 0;
        const auto count = argc > 2 ? (std::uint32_t) std::strtoul (argv[2], nullptr, 10) : 2000u;
        std::uint32_t best = 0;
        double bestScore = -1.0e9;
        int bestFails = 99;

        for (std::uint32_t s = 1; s <= count; ++s)
        {
            ErTable table;

            if (! ergen::generate (t, s, table))
                continue;

            const auto rep = ergen::audit (table, ergen::contextFor (t));
            int fails = 0;
            double worst = 1.0e9;

            for (int r = 0; r < ergen::numRules; ++r)
                if (! rep.pass[r])
                {
                    ++fails;
                    worst = std::min (worst, rep.margin[r]);
                }

            if (fails < bestFails || (fails == bestFails && worst > bestScore))
            {
                best = s;
                bestFails = fails;
                bestScore = worst;
            }
        }

        std::printf ("%s: best seed %u of %u fails %d rule(s), worst margin %.4f\n",
                     typeName (t), best, count, bestFails, bestScore);

        ErTable table;

        if (best > 0 && ergen::generate (t, best, table))
            printAudit (t, table);

        return 0;
    }

    if (flag == "--dump")
    {
        const int t = argc > 1 ? std::atoi (argv[1]) : 0;
        const int v = argc > 2 ? std::atoi (argv[2]) : 2;
        const auto seed = argc > 3 ? (std::uint32_t) std::strtoul (argv[3], nullptr, 10) : ergen::recipeFor (t).seed;
        ErTable table;
        ergen::Diagnostics d {};

        if (! ergen::generate (t, seed, table, &d))
        {
            std::printf ("%s: seed %u could not be placed (stage %d: 0 images, 1 core, 2 infill)\n",
                         typeName (t), seed, d.failedAt);
            return 1;
        }

        const auto scale = d.sizeM / kReferenceSizeM;
        std::printf ("%s at %.1f m: direct %.2f m, mean free path %.2f m, %d images inside the window, "
                     "%d draws redrawn\n", typeName (t), d.sizeM, d.directDistM, d.meanFreePathM,
                     d.imagesConsidered, d.attemptsRejected);
        std::printf ("  shared slots per position:");
        for (int n : d.sharedSlots) std::printf (" %d", n);
        std::printf ("\n  VARIATION %d at %.1f m\n", v, d.sizeM);
        std::printf ("  %3s  %9s %8s %6s %6s %2s   %9s %8s %6s %6s %2s\n",
                     "k", "L ms", "dB", "theta", "pan", "b", "R ms", "dB", "theta", "pan", "b");

        const auto& set = table.variation[v];

        for (int i = 0; i < kErMaxTaps; ++i)
        {
            const auto& l = set.left.taps[i];
            const auto& r = set.right.taps[i];
            std::printf ("  %3d  %9.3f %8.2f %6.3f %6.2f %2d   %9.3f %8.2f %6.3f %6.2f %2d\n", i,
                         l.timeMs * scale, 20.0 * std::log10 (l.gain / scale), (double) l.theta, (double) l.pan, l.band,
                         r.timeMs * scale, 20.0 * std::log10 (r.gain / scale), (double) r.theta, (double) r.pan, r.band);
        }

        std::printf ("  band cutoffs at %.1f m:", d.sizeM);
        for (int b = 0; b < kErBands; ++b)
            std::printf (" %.0f", (double) erBandCutoffHzAt (table, b, (float) d.sizeM));

        double w[64];
        const auto n = ergen::windowEnergies (table, ergen::contextFor (t), v, false, w, 64);
        std::printf ("\n  5 ms window energies, dB, left, default density:");
        for (int i = 0; i < n; ++i) std::printf (" %.1f", w[i] > 0.0 ? 10.0 * std::log10 (w[i]) : -99.0);
        std::printf ("\n");
        printAudit (t, table);
        return 0;
    }

    if (flag == "--try")
    {
        // A re-voiced geometry, tried across seeds before its row is edited:
        // --try <type> <listenerFx> <listenerFy> <sourceDistM> <azimuthDeg> [seeds] [maxOrder]
        if (argc < 6)
        {
            std::printf ("--try <type> <listenerFx> <listenerFy> <sourceDistM> <azimuthDeg> [seeds]\n");
            return 2;
        }

        const int t = std::atoi (argv[1]);
        auto recipe = ergen::recipeFor (t);
        recipe.listenerFx       = std::atof (argv[2]);
        recipe.listenerFy       = std::atof (argv[3]);
        recipe.sourceDistM      = std::atof (argv[4]);
        recipe.sourceAzimuthDeg = std::atof (argv[5]);
        const auto count = argc > 6 ? (std::uint32_t) std::strtoul (argv[6], nullptr, 10) : 500u;
        recipe.maxOrder  = argc > 7 ? std::atoi (argv[7]) : recipe.maxOrder;

        int failures[ergen::numRules] {};
        int unplaced = 0, passed = 0, why[3] {};
        std::uint32_t first = 0;

        for (std::uint32_t s = 1; s <= count; ++s)
        {
            ErTable table;
            ergen::Diagnostics d {};

            if (! ergen::generateFrom (recipe, t, s, table, &d))
            {
                ++unplaced;
                ++why[std::clamp (d.failedAt, 0, 2)];
                if (d.failedAt == 0 && why[0] == 1)
                    std::printf ("  (first shortfall: %d images inside the window, %d kept)\n",
                                 d.imagesConsidered, d.attemptsRejected);
                continue;
            }

            auto ctx = ergen::contextFor (t);
            ctx.directGain = (float) d.directGain;
            const auto rep = ergen::audit (table, ctx);

            if (rep.allPass && passed++ == 0)
                first = s;

            for (int r = 0; r < ergen::numRules; ++r)
                failures[r] += rep.pass[r] ? 0 : 1;
        }

        std::printf ("%s tried at fx %.3f fy %.3f d %.2f az %.1f: %d of %u pass (first seed %u), %d unplaced (images %d, core %d, infill %d)\n",
                     typeName (t), recipe.listenerFx, recipe.listenerFy, recipe.sourceDistM,
                     recipe.sourceAzimuthDeg, passed, count, first, unplaced, why[0], why[1], why[2]);

        for (int r = 0; r < ergen::numRules; ++r)
            if (failures[r] > 0)
                std::printf ("  %-28s failed %d\n", ergen::ruleName (r), failures[r]);

        if (argc > 8)
        {
            ErTable table;
            ergen::Diagnostics d {};
            const auto s = (std::uint32_t) std::strtoul (argv[8], nullptr, 10);

            if (ergen::generateFrom (recipe, t, s, table, &d))
                printAudit (t, table, (float) d.directGain);
        }

        return passed > 0 ? 0 : 1;
    }

    if (flag == "--emit")
        return emitTables (argc > 1 ? argv[1] : "modules/reverb/dsp/ErTableData.inc");

    std::printf ("unknown taps flag: %s\n", flag.c_str());
    return 2;
}

void usage()
{
    std::printf ("usage: measure_reverb <latency|tail|taps [size]|constants|schema>\n"
                 "       measure_reverb taps --audit                     every rule's margin, per type\n"
                 "       measure_reverb taps --emit [path]               write ErTableData.inc\n"
                 "       measure_reverb taps --reseed <type> [start] [n] find the first passing seed\n"
                 "       measure_reverb taps --dump <type> [var] [seed]  one type's taps and audit\n"
                 "       measure_reverb taps --best <type> [n]           least-bad seed when none passes\n"
                 "       measure_reverb taps --try <type> <fx> <fy> <d> <az> [n] [order] [seed]\n"
                 "                                                       a re-voiced geometry across seeds\n");
}

} // namespace

int main (int argc, char** argv)
{
    if (argc < 2)
    {
        usage();
        return 2;
    }

    const std::string mode { argv[1] };

    if (mode == "latency")   { printLatency();   return 0; }
    if (mode == "tail")      { printTail();      return 0; }
    if (mode == "constants") { printConstants(); return 0; }
    if (mode == "schema")    { printSchema();    return 0; }

    if (mode == "taps" && argc > 2 && std::string (argv[2]).rfind ("--", 0) == 0)
        return tapsCommand (argc - 2, argv + 2);

    if (mode == "taps")
    {
        const auto sizeM = argc > 2 ? (float) std::atof (argv[2]) : kReferenceSizeM;
        printTaps (sizeM);
        return 0;
    }

    std::printf ("unknown mode: %s\n", mode.c_str());
    usage();
    return 2;
}
