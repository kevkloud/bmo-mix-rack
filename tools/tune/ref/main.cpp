/*
    bmo-tune-ref: the reference stimulus, and the score of any tuner's render
    of it -- BMO Tune RT's, Antares', Waves' -- by one piece of code
    (tools/common/Stimulus.h), so the numbers compare.

        bmo-tune-ref stimulus out.wav [--rate 48000]
            Writes the stimulus, 32-bit float mono. Put it through a tuner at
            retune 0, chromatic, no vibrato/humanize/flex, with the host's
            delay compensation OFF, and export the result from sample 0.

        bmo-tune-ref score render.wav [--rate 48000] [--offset N] [--channel left|right|mix]
            Scores that render: true latency and correction lag.

        bmo-tune-ref bmo [--rate 48000] [--set id=value ...] [--out render.wav]
            Renders the stimulus through BMO Tune RT's core (defaults:
            chromatic, 0.0 ms, vibrato 0) and scores it.

    Exit status is 0 unless something could not be read or written; the
    checks against the references live in tests/dsp/HardTuneTests.cpp.
*/

#include "modules/tune/dsp/TuneCore.h"
#include "tools/tune/common/Params.h"
#include "tools/tune/common/Stimulus.h"
#include "tools/tune/common/Wav.h"

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

using namespace bmo::tune;
namespace st = bmo::tune::stimulus;

namespace
{
    int usage()
    {
        std::fprintf (stderr,
            "usage: bmo-tune-ref stimulus out.wav [--rate hz]\n"
            "       bmo-tune-ref score render.wav [--rate hz] [--offset samples] [--channel left|right|mix]\n"
            "       bmo-tune-ref bmo [--rate hz] [--set id=value ...] [--out render.wav]\n");
        return 2;
    }

    void print (const st::Score& s, const std::string& title, double fs)
    {
        std::printf ("\n## %s\n\n", title.c_str());
        std::printf ("| segment | delay ms | corr | correction lag ms | RMS c | p95 c | not lag c | frames |\n");
        std::printf ("|---|---:|---:|---:|---:|---:|---:|---:|\n");

        for (const auto& r : s.rows)
        {
            if (r.kind == st::Kind::vibrato)
                std::printf ("| %s | | | %.2f | %.2f | %.2f | %.2f | %d |\n", r.name.c_str(),
                             r.lagMs, r.rmsCents, r.p95Cents, r.unexplainedCents, r.frames);
            else
                std::printf ("| %s | %.3f | %.3f | | | | | |\n", r.name.c_str(), r.delayMs, r.correlation);
        }

        std::printf ("\ntrue latency %.2f ms (in tune %.2f ms, while correcting %.2f ms) | "
                     "correction lag %.2f ms mean, %.2f ms worst | RMS %.2f c mean | %.0f Hz\n",
                     s.trueLatencyMs, s.inTuneLatencyMs, s.correctingLatencyMs, s.meanLagMs, s.worstLagMs,
                     s.meanRmsCents, fs);
    }
}

int main (int argc, char** argv)
{
    if (argc < 2)
        return usage();

    const std::string mode = argv[1];
    double fs = 48000.0;
    long long offset = 0;
    std::string channel = "left", outPath, inPath;
    std::vector<std::string> sets;

    int i = 2;
    if ((mode == "stimulus" || mode == "score") && argc >= 3)
        inPath = argv[i++];

    for (; i < argc; ++i)
    {
        const std::string a = argv[i];
        const auto next = [&] { return i + 1 < argc ? std::string (argv[++i]) : std::string(); };

        if (a == "--rate")         fs = std::atof (next().c_str());
        else if (a == "--offset")  offset = std::atoll (next().c_str());
        else if (a == "--channel") channel = next();
        else if (a == "--set")     sets.push_back (next());
        else if (a == "--out")     outPath = next();
        else                       return usage();
    }

    const auto stim = st::make (fs);

    if (mode == "stimulus")
    {
        if (inPath.empty() || ! wav::writeMono (inPath, stim.samples, fs))
            return usage();

        std::printf ("wrote %s: %.2f s at %.0f Hz, %zu segments\n", inPath.c_str(),
                     (double) stim.samples.size() / fs, fs, stim.segments.size());
        for (const auto& seg : stim.segments)
            std::printf ("  %6.3f s  %s\n", (double) seg.start / fs, seg.name);
        return 0;
    }

    if (mode == "score")
    {
        wav::Channels ch;
        double rate = 0.0;
        if (inPath.empty() || ! wav::read (inPath, ch, rate))
        {
            std::fprintf (stderr, "bmo-tune-ref: cannot read %s\n", inPath.c_str());
            return 1;
        }

        if (std::abs (rate - fs) > 0.5)
        {
            std::fprintf (stderr, "bmo-tune-ref: %s is %.0f Hz; score it with --rate %.0f against a stimulus made at that rate\n",
                          inPath.c_str(), rate, rate);
            return 1;
        }

        std::vector<float> y = ch.front();
        if (channel == "right" && ch.size() > 1)
            y = ch[1];
        else if (channel == "mix" && ch.size() > 1)
            for (size_t k = 0; k < y.size(); ++k)
                y[k] = 0.5f * (ch[0][k] + ch[1][k]);

        print (st::score (stim, y, offset), inPath, fs);
        return 0;
    }

    if (mode == "bmo")
    {
        auto values = tools::defaultValues();
        for (const auto& s : sets)
        {
            std::string error;
            if (! tools::applyAssignment (values, s, error))
            {
                std::fprintf (stderr, "bmo-tune-ref: %s\n", error.c_str());
                return 2;
            }
        }

        TuneCore core;
        core.setParams (tools::toParams (values));
        core.prepare (fs, 128);
        auto y = stim.samples;
        for (size_t at = 0; at < y.size(); at += 128)
            core.process (y.data() + at, (int) std::min<size_t> (128, y.size() - at));

        if (! outPath.empty() && ! wav::writeMono (outPath, y, fs))
            return 1;

        print (st::score (stim, y), "BMO Tune RT (core)", fs);
        return 0;
    }

    return usage();
}
