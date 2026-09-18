/*
    bmo-tune-score: a scorecard from an analysis dump and its ground truth
    (spec T-3).

        bmo-tune-score analysis.csv truth.f0.csv [--rate 48000] [--json out.json]
                       [--summary summary.csv --item name]

    analysis.csv is bmo-tune-cli --dump-analysis; truth.f0.csv is from
    bmo-tune-gen. Detection metrics are scored per evaluation:

        gpe_50c         voiced both ways, off by more than 50 cents
        gpe_20pct       the same at the older 20 % threshold
        fpe_cents_rms   rms error of the frames that were not gross errors
        rpa / rca       raw pitch / raw chroma accuracy over truth-voiced frames;
                        rca - rpa is the octave-error rate, the §3.5 scoreboard
        vde             voicing decision error, split into false alarm and miss
        lock_ms_*       per truth onset, time to the first estimate within
                        20 cents

    Truth is aligned to what a causal detector can see: the contour averaged
    in log frequency over the two periods ending at the frame (see
    tests/dsp/DetectorTests.cpp for why). Frames whose span straddles an
    unvoiced stretch are left out of the pitch metrics but kept for voicing.
*/

#include "tools/tune/common/Json.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace json = bmo::tune::json;

namespace
{
    struct Row
    {
        long long n = 0;
        bool evaluated = false, voiced = false, splice = false;
        double f0 = 0.0, clarity = 0.0;
        int note = -1;
    };

    std::vector<std::string> split (const std::string& line)
    {
        std::vector<std::string> out;
        std::stringstream ss (line);
        std::string cell;
        while (std::getline (ss, cell, ','))
            out.push_back (cell);
        return out;
    }

    bool readAnalysis (const std::string& path, std::vector<Row>& rows)
    {
        std::ifstream f (path);
        if (! f) return false;

        std::string line;
        std::getline (f, line);   // header
        const auto header = split (line);
        const auto col = [&header] (const char* name)
        {
            for (size_t i = 0; i < header.size(); ++i)
                if (header[i] == name) return (int) i;
            return -1;
        };

        const int cN = col ("n"), cEval = col ("evaluated"), cF0 = col ("f0"), cClar = col ("clarity"),
                  cVoiced = col ("voiced"), cNote = col ("note"), cSplice = col ("splice");

        if (cN < 0 || cF0 < 0 || cVoiced < 0)
            return false;

        while (std::getline (f, line))
        {
            const auto c = split (line);
            if ((int) c.size() < (int) header.size()) continue;

            Row r;
            r.n = std::atoll (c[(size_t) cN].c_str());
            r.evaluated = cEval < 0 || c[(size_t) cEval] == "1";
            r.f0 = std::atof (c[(size_t) cF0].c_str());
            r.clarity = cClar >= 0 ? std::atof (c[(size_t) cClar].c_str()) : 0.0;
            r.voiced = c[(size_t) cVoiced] == "1";
            r.note = cNote >= 0 ? std::atoi (c[(size_t) cNote].c_str()) : -1;
            r.splice = cSplice >= 0 && c[(size_t) cSplice] == "1";
            rows.push_back (r);
        }

        return true;
    }

    /** Truth as a per-sample step function. */
    bool readTruth (const std::string& path, std::vector<double>& perSample)
    {
        std::ifstream f (path);
        if (! f) return false;

        std::string line;
        std::getline (f, line);
        std::vector<std::pair<long long, double>> points;

        while (std::getline (f, line))
        {
            const auto c = split (line);
            if (c.size() >= 2)
                points.emplace_back (std::atoll (c[0].c_str()), std::atof (c[1].c_str()));
        }

        if (points.empty()) return false;

        const auto step = points.size() > 1 ? points[1].first - points[0].first : 1;
        perSample.assign ((size_t) (points.back().first + step), 0.0);
        for (size_t k = 0; k < points.size(); ++k)
        {
            const auto end = k + 1 < points.size() ? points[k + 1].first : (long long) perSample.size();
            for (auto i = points[k].first; i < end; ++i)
                perSample[(size_t) i] = points[k].second;
        }

        return true;
    }

    double cents (double a, double b) { return 1200.0 * std::log2 (a / b); }
}

int main (int argc, char** argv)
{
    if (argc < 3)
    {
        std::fprintf (stderr, "usage: bmo-tune-score analysis.csv truth.f0.csv [--rate hz] [--json out.json]\n"
                              "                      [--summary summary.csv --item name]\n");
        return 2;
    }

    double fs = 48000.0;
    std::string jsonPath, summaryPath, item = "item";

    for (int i = 3; i < argc; ++i)
    {
        const std::string a = argv[i];
        if (a == "--rate" && i + 1 < argc)         fs = std::atof (argv[++i]);
        else if (a == "--json" && i + 1 < argc)    jsonPath = argv[++i];
        else if (a == "--summary" && i + 1 < argc) summaryPath = argv[++i];
        else if (a == "--item" && i + 1 < argc)    item = argv[++i];
    }

    std::vector<Row> rows;
    std::vector<double> truth;

    if (! readAnalysis (argv[1], rows)) { std::fprintf (stderr, "cannot read %s\n", argv[1]); return 1; }
    if (! readTruth (argv[2], truth))   { std::fprintf (stderr, "cannot read %s\n", argv[2]); return 1; }

    // Aligned truth, in log2 Hz prefix sums.
    std::vector<double> prefix (truth.size() + 1, 0.0), unvoicedPrefix (truth.size() + 1, 0.0);
    for (size_t i = 0; i < truth.size(); ++i)
    {
        prefix[i + 1] = prefix[i] + (truth[i] > 0.0 ? std::log2 (truth[i]) : 0.0);
        unvoicedPrefix[i + 1] = unvoicedPrefix[i] + (truth[i] > 0.0 ? 0.0 : 1.0);
    }

    long long total = 0, truthVoiced = 0, bothVoiced = 0, gross50 = 0, gross20 = 0, rpaHits = 0, rcaHits = 0;
    long long falseAlarm = 0, miss = 0, scored = 0, splices = 0, noteChanges = 0;
    double sumSq = 0.0;
    int lastNote = -1;

    for (const auto& r : rows)
    {
        if (r.splice) ++splices;
        if (! r.evaluated || r.n < 0 || (size_t) r.n >= truth.size()) continue;

        if (r.voiced && r.note >= 0 && lastNote >= 0 && r.note != lastNote) ++noteChanges;
        if (r.voiced && r.note >= 0) lastNote = r.note;

        ++total;
        const auto t = truth[(size_t) r.n];
        const auto tv = t > 0.0;

        if (! tv && r.voiced) ++falseAlarm;
        if (tv && ! r.voiced) ++miss;
        if (! tv) continue;
        ++truthVoiced;

        const auto span = (size_t) std::lround (2.0 * fs / t);
        const auto i = (size_t) r.n;
        if (span > i || unvoicedPrefix[i + 1] - unvoicedPrefix[i - span] > 0.0)
            continue;   // straddles an onset: voicing only

        ++scored;
        const auto aligned = std::exp2 ((prefix[i + 1] - prefix[i - span]) / (double) (span + 1));

        if (! r.voiced || r.f0 <= 0.0)
            continue;

        ++bothVoiced;
        const auto c = cents (r.f0, aligned);
        const auto folded = std::remainder (c, 1200.0);

        if (std::abs (c) > 50.0) ++gross50; else { sumSq += c * c; ++rpaHits; }
        if (std::abs (r.f0 - aligned) > 0.2 * aligned) ++gross20;
        if (std::abs (folded) <= 50.0) ++rcaHits;
    }

    // Time to lock, per truth onset.
    std::vector<double> locks;
    for (size_t i = 1; i < truth.size(); ++i)
    {
        if (! (truth[i] > 0.0 && truth[i - 1] <= 0.0) && ! (i == 1 && truth[0] > 0.0))
            continue;

        const auto onset = truth[i] > 0.0 && truth[i - 1] <= 0.0 ? i : 0;
        double lock = -1.0;
        for (const auto& r : rows)
        {
            if (! r.evaluated || r.n < (long long) onset) continue;
            if ((size_t) r.n < truth.size() && truth[(size_t) r.n] <= 0.0) break;
            if (r.voiced && r.f0 > 0.0 && std::abs (cents (r.f0, truth[(size_t) r.n])) < 20.0)
            {
                lock = 1000.0 * (double) (r.n - (long long) onset) / fs;
                break;
            }
        }
        if (lock >= 0.0)
            locks.push_back (lock);
    }

    const auto rate = [] (long long a, long long b) { return b > 0 ? (double) a / (double) b : 0.0; };

    json::Writer w;
    w.text ("item", item);
    w.number ("frames", (double) total);
    w.number ("frames_truth_voiced", (double) truthVoiced);
    w.number ("frames_scored", (double) scored);
    w.number ("gpe_50c", rate (gross50, bothVoiced));
    w.number ("gpe_20pct", rate (gross20, bothVoiced));
    w.number ("fpe_cents_rms", rpaHits ? std::sqrt (sumSq / (double) rpaHits) : 0.0);
    w.number ("rpa", rate (rpaHits, scored));
    w.number ("rca", rate (rcaHits, scored));
    w.number ("octave_error_rate", rate (rcaHits - rpaHits, scored));
    w.number ("vde", rate (falseAlarm + miss, total));
    w.number ("vde_false_alarm", rate (falseAlarm, total));
    w.number ("vde_miss", rate (miss, total));
    w.number ("onsets", (double) locks.size());

    double lockMean = 0.0, lockMax = 0.0;
    for (auto l : locks) { lockMean += l; lockMax = std::max (lockMax, l); }
    if (! locks.empty()) lockMean /= (double) locks.size();
    w.number ("lock_ms_mean", lockMean);
    w.number ("lock_ms_max", lockMax);
    w.number ("note_changes", (double) noteChanges);
    w.number ("splices", (double) splices);

    const auto text = w.str();
    std::printf ("%s", text.c_str());

    if (! jsonPath.empty())
        if (auto* f = std::fopen (jsonPath.c_str(), "w")) { std::fputs (text.c_str(), f); std::fclose (f); }

    if (! summaryPath.empty())
    {
        const auto exists = std::ifstream (summaryPath).good();
        if (auto* f = std::fopen (summaryPath.c_str(), "a"))
        {
            if (! exists)
                std::fprintf (f, "item,gpe_50c,fpe_cents_rms,rpa,rca,octave_error_rate,vde,vde_false_alarm,vde_miss,lock_ms_mean,lock_ms_max,note_changes,splices\n");
            std::fprintf (f, "%s,%.6f,%.4f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.3f,%.3f,%lld,%lld\n", item.c_str(),
                          rate (gross50, bothVoiced), rpaHits ? std::sqrt (sumSq / (double) rpaHits) : 0.0,
                          rate (rpaHits, scored), rate (rcaHits, scored), rate (rcaHits - rpaHits, scored),
                          rate (falseAlarm + miss, total), rate (falseAlarm, total), rate (miss, total),
                          lockMean, lockMax, noteChanges, splices);
            std::fclose (f);
        }
    }

    return 0;
}
