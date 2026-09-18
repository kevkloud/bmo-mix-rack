/*
    bmo-tune-bench: CPU per block (spec T-11), as the fraction of the block's
    real-time budget one instance uses on one core.

        bmo-tune-bench [--seconds 5] [--csv out.csv] [--quick]

    Reported as median, p99 and max per block -- p99 and max are what cause
    xruns; the mean is not reported because it hides exactly them.

    Matrix: {44.1, 48, 96, 192 kHz} x {32, 64, 128, 256, 512} x two cases:

      best   an in-tune 880 Hz note: short period, no splices
      worst  an 82 Hz voice gliding and swinging +/-150 cents, so the
             correction is large and the note changes constantly -- the
             longest refinement window, splices every few periods

    Then the denormal check (spec T-4): 1e-30 noise against noise at a
    normal level, same length. If flush-to-zero were not in force the first
    would run many times slower; the ratio is printed and gated at 1.5.

    Timing is std::chrono::steady_clock around each process() call. Numbers
    are this machine's, under whatever else it is doing: run it on a quiet
    machine, and compare runs on the same one.
*/

#include "modules/tune/dsp/TuneCore.h"
#include "tools/tune/common/Params.h"
#include "tools/tune/common/Signals.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

using namespace bmo::tune;
namespace sig = bmo::tune::signals;

namespace
{
    struct Result { double median = 0.0, p99 = 0.0, max = 0.0, meanUs = 0.0; };

    Result bench (const std::vector<float>& x, const TuneParams& p, double fs, int block)
    {
        TuneCore core;
        core.setParams (p);
        core.prepare (fs, block);

        auto buffer = x;
        std::vector<double> loads;
        loads.reserve (buffer.size() / (size_t) block + 1);
        const auto budget = (double) block / fs;
        const auto warmup = (size_t) (0.5 * fs);
        double total = 0.0;

        for (size_t at = 0; at + (size_t) block <= buffer.size(); at += (size_t) block)
        {
            const auto t0 = std::chrono::steady_clock::now();
            core.process (buffer.data() + at, block);
            const auto t1 = std::chrono::steady_clock::now();

            if (at < warmup)
                continue;

            const auto seconds = std::chrono::duration<double> (t1 - t0).count();
            loads.push_back (seconds / budget);
            total += seconds;
        }

        // BMO_BENCH_TRACE=1 prints where the five worst blocks fell, in
        // samples. A spike at the same position run after run is the
        // algorithm; one that moves is the machine.
        if (std::getenv ("BMO_BENCH_TRACE") != nullptr)
        {
            std::vector<std::pair<double, size_t>> ranked;
            for (size_t k = 0; k < loads.size(); ++k)
                ranked.emplace_back (loads[k], warmup + k * (size_t) block);
            std::partial_sort (ranked.begin(), ranked.begin() + std::min<size_t> (5, ranked.size()), ranked.end(),
                               [] (auto& l, auto& r) { return l.first > r.first; });
            std::fprintf (stderr, "  worst blocks @ %.0f/%d:", fs, block);
            for (size_t k = 0; k < std::min<size_t> (5, ranked.size()); ++k)
                std::fprintf (stderr, " %zu (%.0f%%)", ranked[k].second, 100.0 * ranked[k].first);
            std::fprintf (stderr, "\n");
        }

        std::sort (loads.begin(), loads.end());
        Result r;
        if (loads.empty())
            return r;

        r.median = loads[loads.size() / 2];
        r.p99 = loads[std::min (loads.size() - 1, (size_t) (0.99 * (double) loads.size()))];
        r.max = loads.back();
        r.meanUs = 1.0e6 * total / (double) loads.size();
        return r;
    }

    std::vector<float> worstCase (double seconds, double fs)
    {
        auto c = sig::glide (82.41, 164.8, seconds, fs);
        for (size_t i = 0; i < c.size(); ++i)
            c[i] *= std::exp2 (150.0 / 1200.0 * std::sin (2.0 * sig::kPi * 3.0 * (double) i / fs));
        return sig::voice (c, fs).samples;
    }
}

int main (int argc, char** argv)
{
    double seconds = 5.0;
    std::string csvPath;
    bool quick = false;

    for (int i = 1; i < argc; ++i)
    {
        const std::string a = argv[i];
        if (a == "--seconds" && i + 1 < argc)     seconds = std::atof (argv[++i]);
        else if (a == "--csv" && i + 1 < argc)    csvPath = argv[++i];
        else if (a == "--quick")                  quick = true;
        else
        {
            std::fprintf (stderr, "usage: bmo-tune-bench [--seconds s] [--csv out.csv] [--quick]\n");
            return 2;
        }
    }

    const auto params = tools::toParams (tools::defaultValues());
    const std::vector<double> rates = quick ? std::vector<double> { 48000.0 } : std::vector<double> { 44100.0, 48000.0, 96000.0, 192000.0 };
    const std::vector<int> blocks = quick ? std::vector<int> { 64, 128 } : std::vector<int> { 32, 64, 128, 256, 512 };

    std::string csv = "rate,block,case,median_pct,p99_pct,max_pct,mean_us_per_block\n";
    std::printf ("One instance, one core, %% of the block's real-time budget\n\n");
    std::printf ("| rate | block | case | median | p99 | max |\n|---:|---:|---|---:|---:|---:|\n");

    for (auto fs : rates)
    {
        const auto best = sig::voice (sig::steady (880.0, seconds, fs), fs).samples;
        const auto worst = worstCase (seconds, fs);

        for (auto block : blocks)
        {
            for (int which = 0; which < 2; ++which)
            {
                const auto r = bench (which == 0 ? best : worst, params, fs, block);
                const char* name = which == 0 ? "best" : "worst";
                std::printf ("| %.1f kHz | %d | %s | %.3f %% | %.3f %% | %.3f %% |\n",
                             fs / 1000.0, block, name, 100.0 * r.median, 100.0 * r.p99, 100.0 * r.max);

                char line[256];
                std::snprintf (line, sizeof line, "%.0f,%d,%s,%.4f,%.4f,%.4f,%.3f\n", fs, block, name,
                               100.0 * r.median, 100.0 * r.p99, 100.0 * r.max, r.meanUs);
                csv += line;
            }
        }
    }

    // Denormals (spec T-4).
    const double fs = 48000.0;
    const auto normal = sig::whiteNoise ((size_t) (seconds * fs), 0.1, 5);
    const auto tiny = sig::whiteNoise ((size_t) (seconds * fs), 1.0e-30, 5);
    const auto a = bench (normal, params, fs, 128);
    const auto b = bench (tiny, params, fs, 128);
    const auto ratio = b.meanUs / std::max (a.meanUs, 1.0e-9);

    std::printf ("\ndenormal check, 48 kHz / 128: 1e-30 noise %.2f us per block vs %.2f us at -20 dBFS (ratio %.2f)\n",
                 b.meanUs, a.meanUs, ratio);

    if (! csvPath.empty())
        if (auto* f = std::fopen (csvPath.c_str(), "w")) { std::fputs (csv.c_str(), f); std::fclose (f); }

    if (ratio > 1.5)
    {
        std::fprintf (stderr, "FAIL: denormal-level input costs %.2fx normal -- flush-to-zero is not in force\n", ratio);
        return 1;
    }

    return 0;
}
