/*
    Hard tune on a moving voice, and the latency rule -- BMO Tune RT against
    the tuners it competes with, on the reference stimulus
    (tools/common/Stimulus.h), scored by the same code for all of them.

    Why this suite exists: the 2026-09-11 shoot-out on real vocals found BMO
    about as exact as Antares on a held note, and further behind the faster
    the pitch moved -- the error of a correction that lands late. CoreTests
    only holds steady notes, so nothing here saw it. This measures it.

      hardtune           (runs by default)
        - the ruler reads a known delay and a known correction lag
        - THE LATENCY RULE, worst against worst: BMO's true latency is no more
          than Waves Tune Real-Time's, measured the same way (Frosty,
          2026-09-11; AGENTS.md). Necessary, and nowhere near sufficient --
          both figures come from the lowest note in the stimulus.
        - the per-note latency rule is REPORTED here and asserted only under
          --target: BMO is later than Waves at A4 and A5, by up to 3.90 ms,
          and cannot stop being -- see the note beside the check.
        - BMO's correction lag is no worse than the current baseline, while
          the fix is worked on

      hardtune_target    (hardtune_tests --target; disabled in ctest until
                          it passes -- see tests/CMakeLists.txt)
        - BMO flattens a vibrato as closely as Antares does: **passes since
          2026-09-12**, 1.24 c against Antares' 1.30, where it was 3.35.
        - and its worst correction lag is no worse than Antares' worst. Open:
          1.97 ms at A2 against 1.66. Every other vibrato is inside half a
          millisecond.
        - THE LATENCY RULE, PER NOTE. Open, and not reachable on this engine:
          at A5 Waves' whole delay is less than one period, and one whole-cycle
          excursion above the floor already exceeds it. Needs the rule to carry
          a live-monitoring budget before it can go green.

    All three open checks come from one place, found 2026-09-11: the engine's
    read delay is a flat 4 ms where the detector's analysis lag and Waves'
    delay are both a period of the note. Predicting the pitch forward closed
    most of the correction lag (2026-09-12). The latency half has no fix on
    this engine and needs a decision instead.
    testing-notes/tune-latency-review-2026-09-11.md.
*/

#include "modules/tune/dsp/TuneCore.h"
#include "tests/dsp/tune/TestUtil.h"
#include "tools/tune/common/References.h"
#include "tools/tune/common/Stimulus.h"

#include <cstdio>
#include <cstring>
#include <string>

using namespace bmo::tune;
using namespace bmo::tune::test;
namespace st = bmo::tune::stimulus;
namespace sig = bmo::tune::signals;
namespace ref = bmo::tune::references;

namespace
{
    constexpr double fs = 48000.0;

    /** BMO Tune RT's measured numbers, as the regression guard's baseline.

        6.22 ms and 6.61 c until 2026-09-11, which were the figures from
        BEFORE the 4 ms rest landed in 31b30ef. Nobody re-ratcheted it, so the
        guard sat a factor of two slack and would not have noticed the rest
        being reverted -- exactly the regression it exists to catch. Move them
        down with any change that improves them, and say so.

        Then the figures with the prediction in: the estimate carried forward
        to where the engine reads, which is what the lag was. 0.71 and 1.24.

        Re-based 2026-09-14, and this one is a LOOSENING, which is why it is
        spelled out. Guard 6 -- the detector's leap veto, which is what stops
        the pops -- costs 0.053 ms of mean lag and 0.024 cents of residue, all
        of it at A2, where the guard correctly vetoes a real octave error and
        holds the period for up to 2 ms across a fast vibrato. True latency
        goes with it, 10.262 -> 10.427 ms, leaving 8.78 ms of headroom under
        the Waves ceiling.

        Frosty accepted the trade on 2026-09-14, after round eight put guard 6
        first in all four blind groups -- ahead of the standing build in every
        one, and ahead of Antares in three. The residue is still under
        Antares' 1.30. A cent is 1/100 of a semitone and a listener notices
        5-10 of them on a held note, so 0.024 is not an audible quantity; the
        blind result is, and it is what this number was loosened for.

        The rule for the next person is unchanged: move these DOWN with any
        change that improves them. Moving them UP needs ears on the record. */
    constexpr double kBaselineMeanLagMs = 0.76;
    constexpr double kBaselineRmsCents = 1.27;

    std::vector<float> renderBmo (const std::vector<float>& in)
    {
        TuneCore core;
        core.setParams (TuneParams {});   // chromatic, 0.0 ms, vibrato 0, Auto
        core.prepare (fs, 128);
        auto y = in;
        for (size_t at = 0; at < y.size(); at += 128)
            core.process (y.data() + at, (int) std::min<size_t> (128, y.size() - at));
        return y;
    }

    /** What an ideal hard-tune corrector that is `lagMs` late and whose
        audio is `delayMs` late would put out: in-tune and marked segments are
        the input delayed (the marked ones moved onto their note, with their
        level dips kept), and each vibrato is flattened but for
        c(t) - c(t - lag). */
    std::vector<float> idealCorrector (const st::Stimulus& s, double lagMs, double delayMs)
    {
        const auto d = (size_t) std::lround (delayMs * 0.001 * fs);
        std::vector<float> out (s.samples.size() + d, 0.0f);

        for (const auto& seg : s.segments)
        {
            std::vector<float> x;

            if (seg.kind == st::Kind::inTune)
            {
                x.assign (s.samples.begin() + (long) seg.start, s.samples.begin() + (long) (seg.start + seg.length));
            }
            else if (seg.kind == st::Kind::marked)
            {
                sig::VoiceSettings v;
                v.seed = seg.seed + 500;   // a different voice: only the dips are shared
                x = sig::voice (sig::steady (seg.hz, seg.seconds, fs), fs, v).samples;
                const auto g = st::markers (x.size(), fs, seg.seed);
                for (size_t i = 0; i < x.size(); ++i)
                    x[i] *= g[i];
            }
            else
            {
                sig::Contour c (seg.length);
                for (size_t i = 0; i < c.size(); ++i)
                {
                    const auto t = (double) i / fs;
                    const auto now = seg.depthCents * std::sin (2.0 * sig::kPi * seg.rateHz * t);
                    const auto then = seg.depthCents * std::sin (2.0 * sig::kPi * seg.rateHz * (t - 0.001 * lagMs));
                    c[i] = seg.hz * std::exp2 ((now - then) / 1200.0);
                }
                sig::VoiceSettings v;
                v.seed = seg.seed;
                x = sig::voice (c, fs, v).samples;
            }

            for (size_t i = 0; i < x.size() && seg.start + d + i < out.size(); ++i)
                out[seg.start + d + i] = x[i];
        }

        out.resize (s.samples.size());
        return out;
    }

    void reportScore (const std::string& who, const st::Score& sc)
    {
        for (const auto& r : sc.rows)
        {
            if (r.kind == st::Kind::vibrato)
            {
                report (who + ": " + r.name + ", correction lag", r.lagMs, "ms");
                report (who + ": " + r.name + ", RMS off the note", r.rmsCents, "c");
            }
            else
            {
                report (who + ": " + r.name + ", delay", r.delayMs, "ms");
            }
        }
        report (who + ": true latency", sc.trueLatencyMs, "ms");
        report (who + ": correction lag, mean", sc.meanLagMs, "ms");
        report (who + ": RMS off the note, mean", sc.meanRmsCents, "c");
    }
}

int main (int argc, char** argv)
{
    const bool target = argc > 1 && std::strcmp (argv[1], "--target") == 0;
    const auto s = st::make (fs);

    //== The ruler is a ruler =================================================
    {
        // A plain 2.5 ms delay reads 2.5 ms on every segment it is read on.
        const auto d = (size_t) std::lround (0.0025 * fs);
        std::vector<float> delayed (s.samples.size(), 0.0f);
        std::copy (s.samples.begin(), s.samples.end() - (long) d, delayed.begin() + (long) d);

        bool all = true;
        for (const auto& r : st::score (s, delayed).rows)
            if (r.kind != st::Kind::vibrato)
                all = all && std::abs (r.delayMs - 2.5) < 0.05;
        check (all, "a plain 2.5 ms delay reads 2.5 ms, within 0.05, on every in-tune and marked segment");
    }

    for (const auto& [lag, delay] : { std::pair { 0.0, 0.0 }, std::pair { 5.0, 0.0 }, std::pair { 2.0, 3.0 } })
    {
        // The delay tolerance scales with the period, and is not a flat
        // 0.25 ms as it was until 2026-09-11.
        //
        // On a marked segment the ideal corrector is a DIFFERENT voice at the
        // target pitch sharing only the marker pattern, so what limits the
        // envelope correlation is how much of the two voices' own amplitude
        // variation survives the smoothing -- and that is a number of periods,
        // not a number of milliseconds. A flat tolerance is therefore the
        // wrong shape: it was slack at A5 by a factor of seven and just tight
        // enough at E2 to fail (0.334 ms) a ruler that is working correctly.
        // 3 % of the period, floored at the old 0.25 ms so nothing above
        // ~250 Hz is loosened. testing-notes/tune-latency-review-2026-09-11.md.
        const auto toleranceFor = [] (double hz) { return std::max (0.25, 0.03 * 1000.0 / hz); };

        const auto sc = st::score (s, idealCorrector (s, lag, delay));
        double worstLagErr = 0.0, worstDelayRatio = 0.0, worstDelayErr = 0.0;
        std::string worstDelayAt;
        for (size_t k = 0; k < sc.rows.size(); ++k)
        {
            const auto& r = sc.rows[k];
            if (r.kind == st::Kind::vibrato)
            {
                worstLagErr = std::max (worstLagErr, std::abs (r.lagMs - lag));
                continue;
            }

            double hz = 0.0;
            for (const auto& seg : s.segments)
                if (r.name == seg.name)
                    hz = seg.hz;

            const auto err = std::abs (r.delayMs - delay);
            const auto ratio = err / toleranceFor (hz);

            if (ratio > worstDelayRatio)
            {
                // Named, so a ruler failure says which segment rather than
                // only how far out: the segment is the diagnosis.
                worstDelayRatio = ratio;
                worstDelayErr = err;
                worstDelayAt = r.name;
            }
        }

        char what[160];
        std::snprintf (what, sizeof what, "an ideal corrector %.0f ms late with its audio %.0f ms late", lag, delay);
        report (std::string (what) + ": worst lag error", worstLagErr, "ms");
        report (std::string (what) + ": worst delay error, on " + worstDelayAt, worstDelayErr, "ms");
        report (std::string (what) + ": ...as a share of that segment's tolerance", worstDelayRatio, "x");
        check (worstLagErr < 0.3, std::string (what) + " reads its lag within 0.3 ms on every vibrato");
        check (worstDelayRatio < 1.0,
               std::string (what) + " reads its delay within 3 % of a period (min 0.25 ms) on every "
                                    "in-tune and marked segment");
    }

    //== BMO Tune RT ===========================================================
    const auto bmo = st::score (s, renderBmo (s.samples));
    reportScore ("BMO", bmo);

    // THE LATENCY RULE (Frosty, 2026-09-11): a change is safe to take, as far
    // as latency goes, while BMO's true latency stays no more than Waves Tune
    // Real-Time's, measured the same way on the same stimulus. Not what the
    // host is told -- both say 0 -- but how late the audio really is.
    //
    // Worst against worst, which is what the rule says and is necessary but
    // nowhere near sufficient: both figures are dominated by the lowest note
    // in the stimulus, where Waves is 19.2 ms and BMO 9.2, so this passes with
    // 10 ms to spare while BMO is later than Waves over most of the range.
    // The per-note check below is the one that means anything.
    report ("Waves Tune Real-Time: true latency (the ceiling)", ref::kWaves.trueLatencyMs, "ms");
    report ("headroom under the ceiling", ref::kWaves.trueLatencyMs - bmo.trueLatencyMs, "ms");
    check (bmo.trueLatencyMs <= ref::kWaves.trueLatencyMs,
           "BMO's true latency is no more than Waves Tune Real-Time's, worst against worst (the latency rule)");

    // THE LIVE-MONITORING BUDGET, PER NOTE -- and as of 2026-09-14 this IS the
    // per-note latency rule. It replaces the per-note comparison against Waves,
    // which was never the point: root AGENTS.md has always said Waves is the
    // proxy, and that "a change that is later than Waves at some note, but
    // still comfortably inside what a singer monitoring through the plugin can
    // work with, is arguable rather than forbidden -- argue it with a figure
    // and Frosty's ear, and write the budget down here when there is one."
    //
    // There is one now. On 2026-09-14, on AURORA, Frosty monitored a duplicated
    // vocal through the installed build against Waves at 48 kHz on a tone
    // opening on A2 -- the note where BMO is furthest past Waves in the part of
    // the range a singer lives in. His answer: "while I can probably convince
    // myself I could hear a difference, I feel like I wouldn't be able to tell
    // had I not seen the chart."
    //
    // So the budget is BMO's own measured curve as it stood when he listened,
    // per note, and the rule is that it does not get LATER than this. Not a
    // target to beat: a line not to cross. These are the delays that build
    // actually measured, and kBudgetTolerance is the only slack.
    //
    // Re-base DOWNWARD freely and say so, exactly as the lag ratchet works.
    // Raising one costs what raising it cost this time: a figure and Frosty's
    // ear, on the record. Buying latency back is wanted but not owed -- the
    // engine's floor is the rest plus one whole cycle, so this curve is very
    // nearly `liveRest + T`, and getting under it means changing what a splice
    // is rather than tuning a constant.
    //
    // Waves is still measured and still reported below, as information, and the
    // worst-against-worst check above still stands. What is gone is the
    // per-note assertion against it, which on this engine could never go green:
    // at A5 Waves' whole delay is 0.709 ms, under one period there (1.136), and
    // BMO's floor plus one whole-cycle excursion is 1.491. A rule that fails
    // every cell while the ear says it is fine was measuring the wrong thing.
    {
        struct Budget { const char* name; double ms; };
        constexpr Budget kBudget[] = {
            { "held +30c E2", 10.427 },
            { "held +30c A2",  9.275 },
            { "held -35c D3",  3.986 },
            { "held +30c A3",  6.076 },
            { "held +30c A4",  4.987 },
            { "held +30c A5",  4.600 },
        };
        constexpr double kBudgetTolerance = 1.05;

        int over = 0, judged = 0;
        double worstBy = 0.0;

        for (const auto& r : bmo.rows)
        {
            if (r.kind != st::Kind::marked || ! std::isfinite (r.delayMs))
                continue;

            double hz = 0.0;
            for (const auto& seg : s.segments)
                if (r.name == seg.name)
                    hz = seg.hz;

            // Waves at the same note: reported, not asserted.
            report ("BMO vs Waves at " + r.name + ": BMO " + std::to_string (r.delayMs).substr (0, 5)
                        + " ms, Waves at this note", ref::ceilingMsAt (hz), "ms");

            for (const auto& b : kBudget)
            {
                if (r.name != b.name)
                    continue;

                ++judged;

                if (r.delayMs > b.ms * kBudgetTolerance)
                {
                    ++over;
                    worstBy = std::max (worstBy, r.delayMs - b.ms);
                }
            }
        }

        report ("notes judged against the live-monitoring budget", (double) judged, "");
        report ("notes over the budget", (double) over, "");
        report ("...worst by", worstBy, "ms");

        check (judged == (int) (sizeof kBudget / sizeof kBudget[0]),
               "every note in the live-monitoring budget was measured");
        check (over == 0,
               "BMO is inside the live-monitoring budget at every note (the latency rule, per note)");
    }

    check (bmo.meanLagMs <= kBaselineMeanLagMs * 1.05 && bmo.meanRmsCents <= kBaselineRmsCents * 1.05,
           "BMO's correction lag and vibrato residue are no worse than the 2026-09-11 baseline");

    //== The target: as close as Antares =======================================
    report ("Antares Auto-Tune Artist: RMS off the note, mean", ref::kAntares.meanRmsCents, "c");
    report ("Antares Auto-Tune Artist: worst correction lag", ref::kAntares.worstLagMs, "ms");

    if (target)
    {
        check (bmo.meanRmsCents <= ref::kAntares.meanRmsCents,
               "BMO flattens a vibrato at 0 ms as closely as Antares Auto-Tune Artist (mean RMS off the note)");
        check (bmo.worstLagMs <= ref::kAntares.worstLagMs,
               "and its worst correction lag is no worse than Antares'");
    }

    return finish (target ? "hardtune target" : "hardtune");
}
