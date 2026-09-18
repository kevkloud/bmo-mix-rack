/*
    bmo-tune-cli: BMO Tune RT, headless (spec T-1).

    Renders a WAV through the real DSP core with no host, and optionally dumps
    what the core decided at every detector evaluation. Every other tool, and
    anything that wants to hear the plugin before there is a plugin, goes
    through this.

        bmo-tune-cli in.wav out.wav [options]

          --params file.json     flat object of parameter id -> value
          --set id=value         one parameter; repeatable; after --params
          --block N | random     host block size (default 128)
          --dump-analysis a.csv  one row per detector evaluation and splice
          --pcm24                write 24-bit PCM rather than 32-bit float
          --seed N               seed for --block random (default 1)
          --list                 print every parameter and exit

    Multichannel input is summed to mono -- the plugin is monophonic, as the
    references it is measured against are -- and written out mono.
*/

#include "modules/tune/dsp/TuneCore.h"
#include "tools/tune/common/Params.h"
#include "tools/tune/common/Signals.h"
#include "tools/tune/common/Wav.h"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

using namespace bmo::tune;
using bmo::ParamKind;

namespace
{
    int usage (const char* why = nullptr)
    {
        if (why)
            std::fprintf (stderr, "bmo-tune-cli: %s\n\n", why);

        std::fprintf (stderr,
            "usage: bmo-tune-cli in.wav out.wav [--params p.json] [--set id=value ...]\n"
            "                    [--block N|random] [--dump-analysis a.csv]\n"
            "                    [--pcm24] [--seed N]\n"
            "       bmo-tune-cli --list\n");
        return 2;
    }

    void listParameters()
    {
        for (const auto& s : specs())
        {
            std::printf ("%-15s %-16s default %-10s", s.id, s.name, s.text (s.def).c_str());
            if (s.kind == ParamKind::Choice)
            {
                std::printf (" [");
                for (int c = 0; c < s.numChoices(); ++c)
                    std::printf ("%s%s", c ? " | " : "", s.choices[(size_t) c]);
                std::printf ("]");
            }
            else if (s.kind == ParamKind::Float)
                std::printf (" [%g .. %g]", (double) s.min, (double) s.max);
            std::printf ("\n");
        }
    }

    struct Dump
    {
        std::FILE* file = nullptr;

        static void tap (void* context, const AnalysisFrame& f)
        {
            auto* self = static_cast<Dump*> (context);
            if (! f.evaluated && ! f.splice)
                return;

            std::fprintf (self->file, "%lld,%d,%.6f,%.5f,%d,%.5f,%.5f,%d,%.5f,%.9f,%.5f,%d\n",
                          f.sample, (int) f.evaluated, f.f0, f.clarity, (int) f.voiced, f.pitchIn, f.target,
                          f.note, f.appliedCents, f.ratio, f.lag, (int) f.splice);
        }
    };
}

int main (int argc, char** argv)
{
    if (argc >= 2 && std::string (argv[1]) == "--list")
    {
        listParameters();
        return 0;
    }

    if (argc < 3)
        return usage();

    const std::string inPath = argv[1], outPath = argv[2];
    auto values = tools::defaultValues();
    int block = 128;
    bool randomBlocks = false, pcm24 = false;
    unsigned long long seed = 1;
    std::string dumpPath, error;

    for (int i = 3; i < argc; ++i)
    {
        const std::string a = argv[i];
        const auto next = [&] () -> std::string { return i + 1 < argc ? argv[++i] : std::string(); };

        if (a == "--params")
        {
            if (! tools::loadJson (next(), values, error)) return usage (error.c_str());
        }
        else if (a == "--set")
        {
            if (! tools::applyAssignment (values, next(), error)) return usage (error.c_str());
        }
        else if (a == "--block")
        {
            const auto b = next();
            if (b == "random") randomBlocks = true;
            else block = std::max (1, std::atoi (b.c_str()));
        }
        else if (a == "--dump-analysis") dumpPath = next();
        else if (a == "--pcm24")         pcm24 = true;
        else if (a == "--seed")          seed = std::strtoull (next().c_str(), nullptr, 10);
        else return usage (("unknown option " + a).c_str());
    }

    wav::Channels in;
    double rate = 0.0;
    if (! wav::read (inPath, in, rate))
        return usage (("cannot read " + inPath).c_str());

    std::vector<float> mono (in.front().size(), 0.0f);
    for (const auto& ch : in)
        for (size_t i = 0; i < mono.size(); ++i)
            mono[i] += ch[i] / (float) in.size();

    TuneCore core;
    const auto params = tools::toParams (values);
    core.setParams (params);
    core.prepare (rate, 8192);

    Dump dump;
    if (! dumpPath.empty())
    {
        dump.file = std::fopen (dumpPath.c_str(), "w");
        if (! dump.file)
            return usage (("cannot write " + dumpPath).c_str());
        std::fprintf (dump.file, "n,evaluated,f0,clarity,voiced,pitch_in,target,note,applied_cents,ratio,lag,splice\n");
        core.setAnalysisTap (&Dump::tap, &dump);
    }

    signals::Random rng (seed);
    size_t at = 0;

    while (at < mono.size())
    {
        auto n = randomBlocks ? 1 + (int) (rng.next() % 1024) : block;
        n = (int) std::min<size_t> ((size_t) n, mono.size() - at);
        core.process (mono.data() + at, n);
        at += (size_t) n;
    }

    if (dump.file)
        std::fclose (dump.file);

    if (! wav::writeMono (outPath, mono, rate, pcm24))
        return usage (("cannot write " + outPath).c_str());

    std::fprintf (stderr, "bmo-tune-cli: %zu samples at %.0f Hz, reported latency %d samples, %lld splices\n",
                  mono.size(), rate, TuneCore::kReportedLatency, core.classic().spliceCount());
    return 0;
}
