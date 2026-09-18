/*
    bmo-tune-blind: a blind listening set -- every tuner's render of the same
    take, made indistinguishable in everything but the tuning, under letters
    that do not say which is which.

        bmo-tune-blind manifest.txt out-folder [--seed N]

    The manifest is one line per file, tab-separated; '#' starts a comment:

        group <TAB> Failure 0 ms <TAB> path/to/dry.wav
        Antares <TAB> path/to/antares.wav
        Waves <TAB> path/to/waves.wav
        BMO <TAB> path/to/bmo.wav
        group <TAB> ...

    For each group, each entry is:
      - taken as its left channel (every tuner here is mono, or dual mono);
      - moved in time to line up with the dry, by cross-correlating onset
        envelopes (consonants, which a pitch shift leaves where they are),
        so no one hears which came out late -- latency is measured elsewhere
        (bmo-tune-ref), and a flam against memory would give a file away;
      - matched to the dry's RMS level, so louder does not read as better;
      - cut to the dry's length and written as 24-bit stereo at its rate,
        named by a shuffled letter.

    The dry goes in each group's folder as dry.wav, openly: it is the
    reference, not a contestant. KEY.txt says which letter is which and what
    was done to each file -- do not open it until the answers are written.
    ANSWERS.md is a sheet to write them on.
*/

#include "tools/tune/common/Stimulus.h"
#include "tools/tune/common/Wav.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <random>
#include <sstream>
#include <string>
#include <vector>

using namespace bmo::tune;
namespace fsys = std::filesystem;

namespace
{
    struct Entry { std::string label, path; };
    struct Group { std::string name, dry; std::vector<Entry> entries; };

    std::string trim (std::string s)
    {
        while (! s.empty() && (s.back() == '\r' || s.back() == ' ' || s.back() == '\t')) s.pop_back();
        while (! s.empty() && (s.front() == ' ' || s.front() == '\t')) s.erase (s.begin());
        return s;
    }

    bool readLeft (const std::string& path, std::vector<float>& x, double& rate)
    {
        wav::Channels ch;
        if (! wav::read (path, ch, rate))
            return false;
        x = ch.front();
        return true;
    }

    double rms (const std::vector<float>& x)
    {
        double s = 0.0;
        for (auto v : x) s += (double) v * v;
        return std::sqrt (s / (double) std::max<size_t> (x.size(), 1));
    }

    /** The envelope of the signal's change, |x[n] - x[n-1]| over ~1 ms:
        consonants and onsets, which a pitch shift does not move. A plain
        level envelope is too smooth on a vocal -- the first version, a 10 ms
        RMS, left files up to 6.5 ms off the dry. */
    std::vector<float> onsets (const std::vector<float>& x, double fs)
    {
        std::vector<float> e (x.size(), 0.0f);
        const auto k = 1.0 - std::exp (-1.0 / (0.001 * fs));
        double acc = 0.0;
        for (size_t i = 1; i < x.size(); ++i)
        {
            acc += k * (std::abs ((double) x[i] - x[i - 1]) - acc);
            e[i] = (float) acc;
        }
        return e;
    }

    /** How late `y` is against `x`, in samples, by cross-correlating their
        onset envelopes over the loudest 4 s of x, +/- 60 ms. */
    int delayOf (const std::vector<float>& x, const std::vector<float>& y, double fs, double& corr)
    {
        auto ex = onsets (x, fs);
        auto ey = onsets (y, fs);
        const auto win = (size_t) (4.0 * fs), hop = (size_t) (0.25 * fs), pad = (size_t) (0.06 * fs);
        size_t best = pad;
        double bestE = -1.0;
        for (size_t s = pad; s + win + pad < std::min (x.size(), y.size()); s += hop)
        {
            double e = 0.0;
            for (size_t i = s; i < s + win; i += 8) e += ex[i];
            if (e > bestE) { bestE = e; best = s; }
        }
        const auto n = std::min (win, std::min (x.size(), y.size()) - best - pad);
        double mx = 0.0, my = 0.0;
        for (size_t i = best; i < best + n; ++i) { mx += ex[i]; my += ey[i]; }
        mx /= (double) n; my /= (double) n;
        for (auto& v : ex) v = (float) (v - mx);
        for (auto& v : ey) v = (float) (v - my);
        const auto lag = (int) pad;
        return (int) std::lround (stimulus::detail::delayBetween (ex, ey, best, n, -lag, lag, corr));
    }
}

int main (int argc, char** argv)
{
    if (argc < 3)
    {
        std::fprintf (stderr, "usage: bmo-tune-blind manifest.txt out-folder [--seed N]\n");
        return 2;
    }

    const std::string manifest = argv[1];
    const fsys::path out = argv[2];
    std::uint64_t seed = std::random_device {}();
    for (int i = 3; i + 1 < argc; ++i)
        if (std::string (argv[i]) == "--seed")
            seed = std::strtoull (argv[++i], nullptr, 10);

    std::vector<Group> groups;
    {
        std::ifstream f (manifest);
        if (! f) { std::fprintf (stderr, "cannot open %s\n", manifest.c_str()); return 1; }
        std::string line;
        while (std::getline (f, line))
        {
            line = trim (line);
            if (line.empty() || line[0] == '#')
                continue;
            const auto t1 = line.find ('\t');
            if (t1 == std::string::npos) { std::fprintf (stderr, "no tab in: %s\n", line.c_str()); return 1; }
            const auto head = trim (line.substr (0, t1));
            auto rest = trim (line.substr (t1 + 1));

            if (head == "group")
            {
                const auto t2 = rest.find ('\t');
                if (t2 == std::string::npos) { std::fprintf (stderr, "group needs a name and a dry: %s\n", line.c_str()); return 1; }
                groups.push_back ({ trim (rest.substr (0, t2)), trim (rest.substr (t2 + 1)), {} });
            }
            else if (! groups.empty())
                groups.back().entries.push_back ({ head, rest });
        }
    }

    std::mt19937_64 rng (seed);
    std::ostringstream key, answers;
    key << "BLIND SET KEY -- do not open until ANSWERS.md is filled in.\n\n";
    answers << "# Blind listening answers\n\nFor each group: rank the letters best to worst, and a word on each"
               " (clicks, warble, late snap, robotic where it should not be, natural, ...).\n\n";

    for (const auto& g : groups)
    {
        std::vector<float> dry;
        double fs = 0.0;
        if (! readLeft (g.dry, dry, fs)) { std::fprintf (stderr, "cannot read %s\n", g.dry.c_str()); return 1; }

        const auto dir = out / g.name;
        fsys::create_directories (dir);
        wav::write ((dir / "dry.wav").string(), { dry, dry }, fs, true);

        std::vector<size_t> order (g.entries.size());
        for (size_t k = 0; k < order.size(); ++k) order[k] = k;
        std::shuffle (order.begin(), order.end(), rng);

        key << g.name << "\n";
        answers << "## " << g.name << "\n\nRanking: \n\n";

        const auto target = rms (dry);
        for (size_t slot = 0; slot < order.size(); ++slot)
        {
            const auto& e = g.entries[order[slot]];
            std::vector<float> y;
            double rate = 0.0;
            if (! readLeft (e.path, y, rate) || std::abs (rate - fs) > 0.5)
            {
                std::fprintf (stderr, "cannot read %s at %.0f Hz\n", e.path.c_str(), fs);
                return 1;
            }

            double corr = 0.0;
            const auto d = delayOf (dry, y, fs, corr);
            std::vector<float> aligned (dry.size(), 0.0f);
            for (size_t i = 0; i < aligned.size(); ++i)
            {
                const auto j = (long long) i + d;
                if (j >= 0 && (size_t) j < y.size())
                    aligned[i] = y[(size_t) j];
            }

            const auto gain = target / std::max (rms (aligned), 1.0e-12);
            for (auto& v : aligned) v = (float) (v * gain);

            const std::string letter (1, (char) ('A' + slot));
            wav::write ((dir / (letter + ".wav")).string(), { aligned, aligned }, fs, true);

            char buf[512];
            std::snprintf (buf, sizeof buf, "  %s = %-8s moved %+.2f ms (onset corr %.3f), gain %+.2f dB   <- %s\n",
                           letter.c_str(), e.label.c_str(), -1000.0 * d / fs, corr, 20.0 * std::log10 (gain), e.path.c_str());
            key << buf;
            answers << "- " << letter << ": \n";
        }

        key << "\n";
        answers << "\n";
        std::printf ("%s: dry + %zu blind files\n", g.name.c_str(), g.entries.size());
    }

    std::ofstream (out / "KEY.txt") << key.str();
    std::ofstream (out / "ANSWERS.md") << answers.str();
    std::printf ("wrote %s (KEY.txt holds the answers: do not open it first)\n", out.string().c_str());
    return 0;
}
