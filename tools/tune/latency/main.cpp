/*
    bmo-tune-latency: the latency table (spec T-5) -- the one that goes in the
    manual, so it is measured, never computed from what the code says it does.

        bmo-tune-latency [--rate 48000] [--range Auto|...|all]
                         [--md reports/latency.md] [--csv reports/latency.csv]

    For every semitone of the range, on the synthetic voice:

      rest       delay of an in-tune note through the core, by cross-
                 correlation against the dry signal. The algorithmic floor:
                 what the plugin costs when it is not correcting.
      lock       time from a note's onset (out of silence) to the first
                 detector estimate within 20 cents.
      correcting the engine's read delay while holding a note 35 cents sharp
                 at retune 0 -- mean and worst over the steady state. It
                 wanders up to a period above rest.
      reported   what the host is told.

    Live is the only contract since 2026-09-11 (Studio is on branch
    archive/hybrid-studio).

    The check at the end is the latency rule (Frosty, 2026-09-11; AGENTS.md):
    no cell's true latency -- the WORST of its rest delay and its correcting
    delay -- may exceed what Waves Tune Real-Time does AT THE SAME NOTE
    (references::ceilingMsAt). It still reports separately whether the rest is
    the documented one, so the manual's number is never quietly wrong.

    Two things it got wrong before 2026-09-11, both recorded so nobody undoes
    them:

      - It tested the REST delay against the ceiling, which compares a floor
        with a worst: References.h records the ceiling as "worst delay, in
        tune or correcting". It passed whatever the engine did.
      - The ceiling was a scalar. Waves' delay is about 1.73 x the period with
        almost no fixed floor, so one number taken at one note is wrong
        everywhere else -- and wrong in both directions. Held to the 10.62 ms
        figure (Waves at A2), this reported 54 cells over at the bottom of the
        range, where BMO is in fact comfortably under Waves, and reported
        nothing at the top, where BMO really is later: 4.64 ms against Waves'
        0.71 at A5.

    testing-notes/tune-latency-review-2026-09-11.md.

    This tool is not run by ctest or CI; it is the per-semitone sweep you run
    by hand. The whole-plugin form of the rule, on the reference stimulus, is
    tests/dsp/tune/HardTuneTests.cpp -- and note that the stimulus reads a
    correlation peak over a held note, so this sweep is the stricter of the two.
*/

#include "modules/tune/dsp/TuneCore.h"
#include "tools/tune/common/Analysis.h"
#include "tools/tune/common/Params.h"
#include "tools/tune/common/References.h"
#include "tools/tune/common/Signals.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

using namespace bmo::tune;
namespace sig = bmo::tune::signals;
namespace an = bmo::tune::analysis;

namespace
{
    const char* kNoteNames[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };

    struct LagStats { double mean = 0.0, worst = 0.0, least = 1.0e9; double lockMs = -1.0; };

    struct Collector
    {
        long long from = 0, onset = 0;
        double targetHz = 0.0;
        LagStats stats;
        double sum = 0.0;
        long long count = 0;

        static void tap (void* context, const AnalysisFrame& f)
        {
            auto* c = static_cast<Collector*> (context);

            if (c->stats.lockMs < 0.0 && f.evaluated && f.voiced && f.sample >= c->onset && f.f0 > 0.0
                && std::abs (an::cents (f.f0, c->targetHz)) < 20.0)
                c->stats.lockMs = 1000.0 * (double) (f.sample - c->onset);   // scaled by fs later

            if (f.sample >= c->from)
            {
                c->sum += f.lag;
                ++c->count;
                c->stats.worst = std::max (c->stats.worst, f.lag);
                c->stats.least = std::min (c->stats.least, f.lag);
            }
        }
    };

    std::vector<float> run (const std::vector<float>& x, const TuneParams& p, double fs, Collector* collector)
    {
        TuneCore core;
        core.setParams (p);
        core.prepare (fs, 512);
        if (collector)
            core.setAnalysisTap (&Collector::tap, collector);

        auto y = x;
        for (size_t at = 0; at < y.size(); at += 256)
            core.process (y.data() + at, (int) std::min<size_t> (256, y.size() - at));
        return y;
    }

    struct Row
    {
        int noteNumber = 0;
        double hz = 0.0, restMs = 0.0, lockMs = 0.0, meanMs = 0.0, worstMs = 0.0, leastMs = 0.0, reportedMs = 0.0;
    };
}

int main (int argc, char** argv)
{
    double fs = 48000.0;
    std::string rangeName = "Auto", mdPath, csvPath;

    for (int i = 1; i < argc; ++i)
    {
        const std::string a = argv[i];
        if (a == "--rate" && i + 1 < argc)        fs = std::atof (argv[++i]);
        else if (a == "--range" && i + 1 < argc)  rangeName = argv[++i];
        else if (a == "--md" && i + 1 < argc)     mdPath = argv[++i];
        else if (a == "--csv" && i + 1 < argc)    csvPath = argv[++i];
        else
        {
            std::fprintf (stderr, "usage: bmo-tune-latency [--rate hz] [--range name|all] [--md out.md] [--csv out.csv]\n");
            return 2;
        }
    }

    const auto& rangeSpec = specs()[(size_t) Index::range];
    std::vector<int> ranges;
    for (int r = 0; r < rangeSpec.numChoices(); ++r)
        if (tools::lower (rangeName) == "all" || tools::lower (rangeSpec.choices[(size_t) r]) == tools::lower (rangeName))
            ranges.push_back (r);

    if (ranges.empty())
    {
        std::fprintf (stderr, "bmo-tune-latency: unknown range %s\n", rangeName.c_str());
        return 2;
    }

    std::string md, csv = "range,note,hz,rest_ms,lock_ms,correcting_mean_ms,correcting_worst_ms,correcting_least_ms,reported_ms\n";
    int failures = 0, offFloor = 0;
    double worstOverall = 0.0;
    char line[512];

    const auto reported = TuneCore::kReportedLatency;
    const auto expectedRestMs = 1000.0 * contract::liveRestSamples (fs) / fs;

    for (auto r : ranges)
    {
        auto values = tools::defaultValues();
        values[(size_t) Index::range] = (float) r;
        const auto params = tools::toParams (values);
        const auto limits = limitsOf (params.range);

        std::snprintf (line, sizeof line, "\n### %s range (%.0f-%.0f Hz), Live, %.1f kHz\n\n"
                       "| note | Hz | rest ms | lock ms | correcting mean ms | correcting worst ms | reported ms |\n"
                       "|---|---:|---:|---:|---:|---:|---:|\n",
                       rangeSpec.choices[(size_t) r], limits.minHz, limits.maxHz, fs / 1000.0);
        md += line;

        const auto lowest = (int) std::ceil (69.0 + 12.0 * std::log2 (limits.minHz / 440.0));
        const auto highest = (int) std::floor (69.0 + 12.0 * std::log2 (limits.maxHz / 440.0));

        double worstRest = 0.0;

        for (int noteNumber = lowest; noteNumber <= highest; ++noteNumber)
        {
            const auto hz = 440.0 * std::exp2 ((noteNumber - 69) / 12.0);
            Row row;
            row.noteNumber = noteNumber;
            row.hz = hz;
            row.reportedMs = 1000.0 * reported / fs;

            // Rest: correction disabled, analysis path live (spec T-5) --
            // every note switched off gives the quantizer nothing to aim at,
            // so the correction is exactly zero while the detector runs.
            // Two things the first runs got wrong, both recorded so nobody
            // undoes them:
            //
            //  - A steady tone correlates with itself at every whole period,
            //    so on a plain one the rig picked an arbitrary peak (the
            //    Studio contract this tool measured until 2026-09-11
            //    "measured" 0.6 to 5.2 ms against 8.2). 30 % random shimmer
            //    gives each cycle its own level and only the true delay
            //    lines up.
            //  - With correction left on, that shimmer biases the detector a
            //    few cents, the engine really corrects it, and the read
            //    wanders: the floor is not what was measured.
            {
                sig::VoiceSettings fingerprint;
                fingerprint.shimmer = 0.3;
                fingerprint.seed = 1000u + (unsigned) noteNumber;
                const auto x = sig::voice (sig::steady (hz, 0.5, fs), fs, fingerprint).samples;

                auto idle = params;
                idle.allowed = 0;
                const auto y = run (x, idle, fs, nullptr);
                row.restMs = 1000.0 * an::delayOf (x, y, (int) (0.03 * fs)) / fs;
                worstRest = std::max (worstRest, row.restMs);
            }

            // Lock and correcting lag: 100 ms of silence, then 35 cents sharp.
            {
                const auto c = sig::concat ({ sig::silence (0.1, fs), sig::steady (hz * std::exp2 (35.0 / 1200.0), 0.6, fs) });
                Collector col;
                col.onset = (long long) (0.1 * fs);
                col.from = (long long) (0.3 * fs);
                col.targetHz = hz * std::exp2 (35.0 / 1200.0);
                run (sig::voice (c, fs).samples, params, fs, &col);

                row.lockMs = col.stats.lockMs >= 0.0 ? col.stats.lockMs / fs : -1.0;
                row.meanMs = col.count ? 1000.0 * (col.sum / (double) col.count) / fs : 0.0;
                row.worstMs = 1000.0 * col.stats.worst / fs;
                row.leastMs = col.count ? 1000.0 * col.stats.least / fs : 0.0;
            }

            std::snprintf (line, sizeof line, "| %s%d | %.1f | %.2f | %.2f | %.2f | %.2f | %.2f |\n",
                           kNoteNames[noteNumber % 12], noteNumber / 12 - 1, hz, row.restMs, row.lockMs, row.meanMs, row.worstMs, row.reportedMs);
            md += line;

            std::snprintf (line, sizeof line, "%s,%s%d,%.3f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f\n",
                           rangeSpec.choices[(size_t) r], kNoteNames[noteNumber % 12], noteNumber / 12 - 1,
                           hz, row.restMs, row.lockMs, row.meanMs, row.worstMs, row.leastMs, row.reportedMs);
            csv += line;

            // THE LATENCY RULE, per cell. The figure it is held to is the
            // WORST delay the read reaches while correcting, not the rest:
            // References.h records the ceiling as "worst delay, in tune or
            // correcting", so testing the rest against it compares a floor
            // with a worst and passes whatever the engine does. It did: until
            // 2026-09-11 this tested row.restMs, printed "every cell's rest
            // delay is under the ceiling", and exited 0 while 54 cells were
            // over. See testing-notes/tune-latency-review-2026-09-11.md.
            const auto worstHere = std::max (row.restMs, row.worstMs);
            const auto ceilingHere = references::ceilingMsAt (hz);

            if (worstHere > ceilingHere)
            {
                std::fprintf (stderr, "FAIL: %s at %.1f Hz: true latency %.3f ms (rest %.3f, correcting worst "
                              "%.3f) is over Waves Tune Real-Time's %.3f ms at the same note\n",
                              rangeSpec.choices[(size_t) r], hz, worstHere, row.restMs, row.worstMs, ceilingHere);
                ++failures;
                worstOverall = std::max (worstOverall, worstHere - ceilingHere);
            }

            if (std::abs (row.restMs - expectedRestMs) > 1000.0 / fs + 1e-9)
                ++offFloor;
        }

        std::fprintf (stderr, "%s: rest delay %.3f ms worst, Live rest %.3f ms, reported %d samples\n",
                      rangeSpec.choices[(size_t) r], worstRest, expectedRestMs, reported);
    }

    if (offFloor > 0)
        std::fprintf (stderr, "note: %d cell(s) off the documented Live rest (%.3f ms) -- allowed under the ceiling, "
                              "but update the manual's figure and modules/tune/AGENTS.md\n", offFloor, expectedRestMs);

    std::printf ("%s", md.c_str());

    if (! mdPath.empty())
        if (auto* f = std::fopen (mdPath.c_str(), "w")) { std::fputs (md.c_str(), f); std::fclose (f); }
    if (! csvPath.empty())
        if (auto* f = std::fopen (csvPath.c_str(), "w")) { std::fputs (csv.c_str(), f); std::fclose (f); }

    if (failures)
        std::fprintf (stderr, "bmo-tune-latency: %d cell(s) later than Waves Tune Real-Time at the same note "
                              "(the latency rule); worst by %.3f ms\n", failures, worstOverall);
    else
        std::fprintf (stderr, "bmo-tune-latency: every cell is under Waves Tune Real-Time at its own note "
                              "(the latency rule)\n");

    return failures ? 1 : 0;
}
