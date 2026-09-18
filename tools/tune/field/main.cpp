/*
    bmo-tune-field: the hiccup numbers for a real take -- what the 2026-09-11
    shoot-out was diagnosed with, in one command, so any change can be held
    to the real vocals it was made for and not only to the synthetic corpus.

        bmo-tune-field dry.wav [--set id=value ...] [--ruler-min 60] [--ruler-max 400]
                       [--out render.wav] [--csv detector.csv]

    Renders the dry through BMO Tune RT's core, as bmo-tune-cli does, with
    the analysis tap on, and reports:

      detector    every 10 ms frame where the offline ruler (Analysis.h, 40 ms,
                  kept to --ruler-min..--ruler-max, a low male voice by
                  default) finds a pitch, BMO's own estimates in the middle
                  10 ms of that frame, median, sorted: on the note (within
                  60 c), an octave up, a twelfth up, an octave down, other,
                  or BMO unvoiced. An octave keeps the note name and so the
                  correction; a harmonic above means a splice cut mid-cycle,
                  and a twelfth aims at the wrong note name -- those two are
                  the ones heard.
      flips       note changes that change the note NAME, and of those, the
                  ones that come straight back within 80 ms, split into
                  neighbours (1-2 semitones: the note decision) and jumps
                  (the detector).
      dropouts    gaps under 80 ms between voiced stretches: the correction
                  letting go mid-phrase.
      splices     the engine's whole-period jumps, and how badly each one LANDS:
                  the two reads either side of a jump are meant to be one
                  cycle apart on the same waveform, so what they differ by
                  across the crossfade is the step a listener hears. Split two
                  ways, because a big step is only a pop if it is both on
                  pitched material and loud enough to hear:

                    ON PITCH            periodic, and within kQuietBelowVoiceDb
                                        of the take's median voiced level. The
                                        only column that predicts a pop.
                    on pitch, in a gap  periodic but far under the voice. A
                                        step in near-silence is arithmetic,
                                        not a sound.
                    on noise            the ruler finds no pitch: a consonant,
                                        a breath, a stretch where the detector
                                        has lost the voice. Two unrelated
                                        noisy reads differ a lot and sound the
                                        same.

                  --splices lists every one with its level and its column.

                  The level split arrived on 2026-09-14 and changed what the
                  take looks like. Landing error is normalised by the reads'
                  own RMS, so it says how big the step is *relative to the
                  waveform it sits in* and nothing about whether that waveform
                  is audible. On Failure one splice at -54 dB lands at 1.26 and
                  was the worst figure on the whole take; with it in its own
                  column, Failure's audible splices are 0 of 18 over 0.5, worst
                  0.49 -- and Fuji, which had looked comparable, is 5 of 11
                  over 0.5 with a worst of 1.52 and nothing in a gap at all.
                  Several rounds were spent driving down a Failure number that
                  was mostly one inaudible splice, while the take with the real
                  problem sat next to it (testing-notes/tune-field-levels-2026-09-14.md).

                  Count and landing error are different questions: on Failure
                  the median jump lands at 0.10 and is inaudible, which is why
                  driving the count from 120 to 38 did not drive the pops down
                  (testing-notes/tune-blind-2026-09-12.md).

    The ruler must be kept to the voice's range: a voice whose fundamental
    sits under its second harmonic fools an unbounded ruler as it fooled the
    detector (Failure at 1.00 s reads 606 Hz full-range, 303 Hz bounded).
    Field audio never enters the repository; see HANDOFF.md.
*/

#include "modules/tune/dsp/TuneCore.h"
#include "tools/tune/common/Analysis.h"
#include "tools/tune/common/Params.h"
#include "tools/tune/common/Wav.h"

#include <algorithm>
#include <utility>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

using namespace bmo::tune;

namespace
{
    /** How far under the take's own median voiced level a splice has to sit
        before its landing error is reported apart from the rest. 25 dB is well
        past where a step in the waveform stops competing with the voice around
        it, and is measured against the take rather than full scale so a quietly
        recorded one does not read as all gaps. */
    constexpr double kQuietBelowVoiceDb = 25.0;

    struct Row { long long n; double f0, pitchIn; bool voiced; int note; bool splice, evaluated; double mismatch; };

    struct Collector
    {
        std::vector<Row> rows;

        static void tap (void* context, const AnalysisFrame& f)
        {
            if (f.evaluated || f.splice || f.spliceMismatch > 0.0)
                static_cast<Collector*> (context)->rows.push_back (
                    { f.sample, f.f0, f.pitchIn, f.voiced, f.note, f.splice, f.evaluated, f.spliceMismatch });
        }
    };

    int usage()
    {
        std::fprintf (stderr, "usage: bmo-tune-field dry.wav [--set id=value ...] [--ruler-min hz] [--ruler-max hz]\n"
                              "                      [--out render.wav] [--csv detector.csv]\n");
        return 2;
    }
}

int main (int argc, char** argv)
{
    if (argc < 2)
        return usage();

    const std::string inPath = argv[1];
    std::string outPath, csvPath;
    double rulerMin = 60.0, rulerMax = 400.0;
    bool listSplices = false;
    auto values = tools::defaultValues();

    for (int i = 2; i < argc; ++i)
    {
        const std::string a = argv[i];
        const auto next = [&] { return i + 1 < argc ? std::string (argv[++i]) : std::string(); };
        std::string error;

        if (a == "--set")            { if (! tools::applyAssignment (values, next(), error)) { std::fprintf (stderr, "%s\n", error.c_str()); return 2; } }
        else if (a == "--ruler-min") rulerMin = std::atof (next().c_str());
        else if (a == "--ruler-max") rulerMax = std::atof (next().c_str());
        else if (a == "--out")       outPath = next();
        else if (a == "--csv")       csvPath = next();
        else if (a == "--splices")   listSplices = true;
        else                         return usage();
    }

    wav::Channels in;
    double fs = 0.0;
    if (! wav::read (inPath, in, fs))
    {
        std::fprintf (stderr, "cannot read %s\n", inPath.c_str());
        return 1;
    }

    // Mono, as the CLI takes it: the channels averaged.
    std::vector<float> dry (in.front().size(), 0.0f);
    for (const auto& ch : in)
        for (size_t i = 0; i < dry.size(); ++i)
            dry[i] += ch[i] / (float) in.size();

    // Render, with the tap.
    Collector col;
    TuneCore core;
    core.setParams (tools::toParams (values));
    core.prepare (fs, 128);
    core.setAnalysisTap (&Collector::tap, &col);
    auto y = dry;
    for (size_t at = 0; at < y.size(); at += 128)
        core.process (y.data() + at, (int) std::min<size_t> (128, y.size() - at));

    if (! outPath.empty())
        wav::writeMono (outPath, y, fs);

    std::vector<Row> ev;
    std::vector<int> evSplices;   // splices since the previous evaluation
    std::vector<double> evMismatch;   // and the worst one's landing error
    std::vector<std::pair<long long, double>> mismatches;   // every splice: where, and how badly
    std::vector<long long> spliceAt;                        // and just where, for the period test
    int splices = 0, spliceRun = 0;
    double mismatchRun = 0.0;
    for (const auto& r : col.rows)
    {
        if (r.splice) { ++splices; ++spliceRun; }
        if (r.mismatch > 0.0) { mismatches.emplace_back (r.n, r.mismatch); mismatchRun = std::max (mismatchRun, r.mismatch); }
        if (r.splice) spliceAt.push_back (r.n);
        if (r.evaluated)
        {
            ev.push_back (r);
            evSplices.push_back (spliceRun);
            evMismatch.push_back (mismatchRun);
            spliceRun = 0;
            mismatchRun = 0.0;
        }
    }

    if (! csvPath.empty())
        if (auto* f = std::fopen (csvPath.c_str(), "w"))
        {
            std::fprintf (f, "seconds,f0,pitch_in,voiced,note,splices,mismatch\n");
            for (size_t i = 0; i < ev.size(); ++i)
            {
                const auto& r = ev[i];
                std::fprintf (f, "%.5f,%.3f,%.4f,%d,%d,%d,%.4f\n", (double) r.n / fs, r.f0, r.pitchIn,
                              r.voiced ? 1 : 0, r.note, evSplices[i], evMismatch[i]);
            }
            std::fclose (f);
        }

    //== Detector against the ruler ==========================================
    const auto hop = (size_t) std::lround (0.010 * fs), len = (size_t) std::lround (0.040 * fs);
    int frames = 0, right = 0, up8 = 0, up12 = 0, down8 = 0, other = 0, unvoiced = 0;
    size_t k = 0;

    // Where the RULER finds the dry periodic, independent of anything the
    // plugin thinks. A splice's landing error only means something on
    // periodic material: it is normalised by the reads' own RMS, so two
    // unrelated noisy reads score high and sound the same. Frosty's
    // timestamps caught this -- five high-error splices at 16.1-16.8 s went
    // unreported, and that is where the detector reads 580 to 1837 Hz on a
    // 60-400 Hz singer (testing-notes/tune-blind-2026-09-12.md).
    std::vector<std::pair<long long, bool>> periodic;
    std::vector<double> frameLevels;

    for (size_t a = 0; a + len < dry.size(); a += hop)
    {
        double e = 0.0;
        for (size_t i = a; i < a + len; ++i) e += (double) dry[i] * dry[i];
        const auto frameDb = 10.0 * std::log10 (e / (double) len + 1.0e-20);

        if (frameDb < -45.0)
            continue;

        // Kept so the splice statistics below have something to call loud. A
        // landing error is a step in the waveform relative to the reads either
        // side of it, so it says nothing at all about whether the step is
        // above the noise the listener is hearing it in.
        frameLevels.push_back (frameDb);

        const auto truth = analysis::measureHz (dry, a, len, fs, rulerMin, rulerMax);
        periodic.emplace_back ((long long) (a + len / 2), truth > 0.0);
        if (truth <= 0.0)
            continue;

        const double lo = (double) a + 0.015 * fs, hi = (double) a + 0.025 * fs;
        while (k < ev.size() && (double) ev[k].n < lo) ++k;
        std::vector<double> est;
        for (size_t j = k; j < ev.size() && (double) ev[j].n < hi; ++j)
            if (ev[j].voiced && ev[j].f0 > 0.0)
                est.push_back (ev[j].f0);

        ++frames;
        if (est.empty()) { ++unvoiced; continue; }

        std::sort (est.begin(), est.end());
        const auto c = 1200.0 * std::log2 (est[est.size() / 2] / truth);
        if (std::abs (c) < 60.0)               ++right;
        else if (std::abs (c - 1200.0) < 80.0) ++up8;
        else if (std::abs (c - 1902.0) < 80.0) ++up12;
        else if (std::abs (c + 1200.0) < 80.0) ++down8;
        else                                   ++other;
    }

    //== Note-name flips =======================================================
    struct Change { long long n; int from, to; };
    std::vector<Change> changes;
    int prev = -1;
    bool prevVoiced = false;
    for (const auto& r : ev)
    {
        if (! r.voiced || r.note < 0) { prevVoiced = false; continue; }
        if (prevVoiced && prev >= 0 && r.note != prev && ((r.note - prev) % 12) != 0)
            changes.push_back ({ r.n, prev, r.note });
        prev = r.note;
        prevVoiced = true;
    }

    int flips = 0, neighbour = 0, jump = 0;
    for (size_t c = 1; c < changes.size(); ++c)
        if (changes[c].to == changes[c - 1].from && (double) (changes[c].n - changes[c - 1].n) / fs < 0.080)
        {
            ++flips;
            (std::abs (changes[c].to - changes[c].from) <= 2 ? neighbour : jump)++;
        }

    //== Dropouts ==============================================================
    int dropouts = 0;
    long long lastVoiced = -1;
    bool inRun = false;
    for (const auto& r : ev)
    {
        if (r.voiced)
        {
            if (! inRun && lastVoiced >= 0 && (double) (r.n - lastVoiced) / fs < 0.080)
                ++dropouts;
            inRun = true;
            lastVoiced = r.n;
        }
        else
        {
            inRun = false;
        }
    }

    const auto pct = [frames] (int v) { return frames ? 100.0 * v / frames : 0.0; };
    const auto seconds = (double) dry.size() / fs;
    std::printf ("%s  (%.1f s, %.0f Hz; ruler %.0f-%.0f Hz)\n", inPath.c_str(), seconds, fs, rulerMin, rulerMax);
    std::printf ("  detector, %d voiced frames: on the note %.1f %% | octave up %.1f %% | twelfth up %.1f %% | "
                 "octave down %.1f %% | other %.1f %% | BMO unvoiced %.1f %%\n",
                 frames, pct (right), pct (up8), pct (up12), pct (down8), pct (other), pct (unvoiced));
    std::printf ("  note-name changes %zu; flips back within 80 ms %d (neighbours %d, jumps %d)\n",
                 changes.size(), flips, neighbour, jump);
    std::printf ("  dropouts under 80 ms %d | splices %d (%.1f/s)\n", dropouts, splices, splices / seconds);

    // How badly the splices land, which is not the same question as how many
    // there are: on this take Frosty heard 7 of 38 (2026-09-12). A count
    // cannot separate a jump that lands in phase, where the crossfade hides
    // it, from one that lands anywhere, which steps the waveform.
    if (! mismatches.empty())
    {
        // Level over 50 ms around a splice, against the take's own peak --
        // the same window the per-splice listing shows, computed once here so
        // the listing and the statistics can never disagree about how loud a
        // moment was.
        const auto levelAt = [&dry, fs] (long long at)
        {
            const auto half = (size_t) std::lround (0.025 * fs);
            const auto from = (size_t) std::max<long long> (0, at - (long long) half);
            const auto to = std::min (dry.size(), (size_t) at + half);
            double e = 0.0;
            for (size_t i = from; i < to; ++i) e += (double) dry[i] * dry[i];
            return 10.0 * std::log10 (e / (double) std::max<size_t> (1, to - from) + 1.0e-20);
        };

        // Split by whether the RULER calls the material periodic there. On
        // aperiodic material -- a consonant, a breath, the stretch where the
        // detector loses the voice entirely -- a high landing error is not a
        // pop: two unrelated noisy reads differ a lot and sound the same.
        //
        // The second split is LEVEL, and it was missing until 2026-09-14.
        // Landing error is normalised by the reads' own RMS, so it measures
        // the step relative to the waveform it is in and not relative to the
        // take. A splice in a phrase gap can therefore land at 1.26 -- the
        // worst figure on the whole of Failure -- while sitting at -54 dB,
        // where nothing is audible at all. Round nine turned on a "worst
        // landing 1.95 -> 0.76" that was partly this: a quiet-gap splice
        // setting the headline number for a take whose voice sits 30 dB above
        // it. Quiet splices are still counted and still listed; they are just
        // not allowed to be the number anyone reads.
        //
        // The line is drawn against the take's own voice rather than against
        // full scale, so it travels to a quietly recorded take without being
        // retuned.
        std::vector<double> onPitch, onPitchQuiet, onNoise;

        auto voiceDb = -20.0;

        if (! frameLevels.empty())
        {
            auto sorted = frameLevels;
            std::sort (sorted.begin(), sorted.end());
            voiceDb = sorted[sorted.size() / 2];
        }

        const auto quietBelowDb = voiceDb - kQuietBelowVoiceDb;

        for (const auto& [at, err] : mismatches)
        {
            bool isPeriodic = false;
            long long best = -1;

            for (const auto& [centre, p] : periodic)
            {
                const auto d = centre > at ? centre - at : at - centre;
                if (best < 0 || d < best) { best = d; isPeriodic = p; }
            }

            if (! isPeriodic)                       onNoise.push_back (err);
            else if (levelAt (at) < quietBelowDb)   onPitchQuiet.push_back (err);
            else                                    onPitch.push_back (err);
        }

        const auto line = [] (const char* what, std::vector<double> m)
        {
            if (m.empty())
            {
                std::printf ("  splice landing error, %-16s none\n", what);
                return;
            }

            std::sort (m.begin(), m.end());
            double sum = 0.0;
            int bad = 0;
            for (auto v : m) { sum += v; if (v > 0.5) ++bad; }
            std::printf ("  splice landing error, %-16s median %.2f | p90 %.2f | worst %.2f | mean %.2f | over 0.5: %d of %zu\n",
                         what, m[m.size() / 2], m[(size_t) (0.9 * (double) (m.size() - 1))], m.back(),
                         sum / (double) m.size(), bad, m.size());
        };

        line ("ON PITCH:", onPitch);
        line ("on pitch, in a gap:", onPitchQuiet);
        line ("on noise:", onNoise);

        std::printf ("  (the take's median voiced level is %.1f dB, so 'in a gap' is under %.1f)\n",
                     voiceDb, voiceDb - kQuietBelowVoiceDb);

        // WHETHER THE DETECTOR HAD THE PERIOD when the jump was taken, which
        // is the one thing so far that separates the splices Frosty hears
        // from the ones he does not.
        //
        // Measured on Failure, 2026-09-13, against six timestamps he read
        // cold: in the 40 ms before each of the five audible splices the
        // detector's own f0 spans a ratio of 1.76 to 3.94 -- it is losing the
        // period and reading a harmonic. Before the quiet ones, 1.00 to 1.02.
        // Six against six, cleanly separated, first try.
        //
        // The splice COUNT and the splice LANDING ERROR were both refuted
        // against the same ears (2026-09-12, 2026-09-13). This is the third
        // measure and the first that agrees with them, so it is the one to
        // drive work from -- and it points at the detector, not the engine:
        // the same splices fire at the same millisecond at every rest from
        // 2 to 8 ms, which is why four rests sounded indistinguishable and
        // all of them unacceptable.
        int lost = 0;
        for (auto at : spliceAt)
        {
            double lo = 0.0, hi = 0.0;
            for (const auto& r : ev)
            {
                if (r.n > at || r.n < at - (long long) (0.040 * fs) || ! r.voiced || r.f0 <= 0.0)
                    continue;
                if (lo == 0.0 || r.f0 < lo) lo = r.f0;
                if (r.f0 > hi) hi = r.f0;
            }
            if (lo > 0.0 && hi / lo > 1.5)
                ++lost;
        }

        std::printf ("  splices taken while the detector had LOST the period (f0 spanning >1.5x in the "
                     "40 ms before): %d of %zu\n", lost, spliceAt.size());

        if (listSplices)
        {
            std::printf ("\n  every splice: when, how badly it landed, how loud the take is there,\n"
                         "  and whether the ruler calls the material periodic\n");

            for (const auto& [at, err] : mismatches)
            {
                bool isPeriodic = false;
                long long best = -1;
                for (const auto& [centre, p] : periodic)
                {
                    const auto d = centre > at ? centre - at : at - centre;
                    if (best < 0 || d < best) { best = d; isPeriodic = p; }
                }

                const auto db = levelAt (at);

                std::printf ("    %7.3f s   landing %.2f   %6.1f dB   %s\n",
                             (double) at / fs, err, db,
                             ! isPeriodic         ? "ON NOISE"
                                 : db < quietBelowDb ? "on pitch, in a gap"
                                                     : "on pitch");
            }
        }
    }

    return 0;
}
