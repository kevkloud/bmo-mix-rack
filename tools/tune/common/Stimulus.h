#pragma once

/*
    The reference stimulus: one synthetic file that every tuner is put through
    -- BMO Tune RT by its core, Antares and Waves in a host -- and the two
    things measured on what comes back, by the same code for all of them.
    Measured with Analysis.h's ruler, never with any tuner's own detector.

      correction lag   how far behind a moving pitch a hard-tune correction
                       lands. On the vibrato segments, flattened at retune 0
                       and chromatic: a correction L late leaves
                       out - target = L x slope, so the least-squares L over
                       the segment is the lag, in ms. The 2026-09-11
                       shoot-out found this is where BMO trails Antares on
                       real vocals (testing-notes/shootout-2026-09-11.md).

      true latency     how far behind its input the output is, in time --
                       what a singer monitoring through the plugin hears,
                       whatever the plugin tells the host. In-tune notes by
                       waveform cross-correlation (the output is the input,
                       delayed); notes held off pitch and marked with sharp
                       level dips by envelope cross-correlation, which a pitch
                       shift does not hide -- the delay while correcting.

    Every segment is a note a low male voice sings (the shoot-out's material
    and Auto-Tune's "Low Male" input type), in tune at A = 440 or exactly off
    it by a stated amount, so chromatic hard tuning has one target per
    segment and it is known. Layout: 0.5 s of silence, then each segment
    followed by 0.4 s of silence, so every tuner unlocks between them.

    A render is scored as sample 0 = the stimulus' sample 0: a host render
    with delay compensation off, or an offline render, keeps that, and then a
    measured delay is the plugin's true latency.
*/

#include "tools/tune/common/Analysis.h"
#include "tools/tune/common/Signals.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

namespace bmo::tune::stimulus
{

enum class Kind { inTune, vibrato, marked };

struct Segment
{
    Kind kind;
    const char* name;
    double hz;              ///< the note, in tune at A = 440; the target
    double offsetCents;     ///< marked: held this far off the note
    double depthCents;      ///< vibrato: +/- this
    double rateHz;          ///< vibrato rate
    double seconds;
    size_t start = 0, length = 0;   ///< filled by make()
    std::uint64_t seed = 0;         ///< filled by make(): the voice's, and the markers'
};

inline std::vector<Segment> layout()
{
    const double A2 = 110.0, D3 = 146.8324, E3 = 164.8138, A3 = 220.0;
    const double E2 = 82.4069, A4 = 440.0, A5 = 880.0;
    return {
        { Kind::inTune,  "in tune A2",        A2,   0.0,  0.0, 0.0, 1.0 },
        { Kind::inTune,  "in tune D3",        D3,   0.0,  0.0, 0.0, 1.0 },
        { Kind::inTune,  "in tune E3",        E3,   0.0,  0.0, 0.0, 1.0 },
        { Kind::inTune,  "in tune A3",        A3,   0.0,  0.0, 0.0, 1.0 },
        { Kind::vibrato, "vibrato A2 40c 5.5Hz", A2, 0.0, 40.0, 5.5, 2.0 },
        { Kind::vibrato, "vibrato D3 40c 5.5Hz", D3, 0.0, 40.0, 5.5, 2.0 },
        { Kind::vibrato, "vibrato E3 45c 6.5Hz", E3, 0.0, 45.0, 6.5, 2.0 },
        { Kind::vibrato, "vibrato A3 40c 5.5Hz", A3, 0.0, 40.0, 5.5, 2.0 },
        { Kind::marked,  "held +30c A3",      A3,  30.0,  0.0, 0.0, 1.5 },
        { Kind::marked,  "held -35c D3",      D3, -35.0,  0.0, 0.0, 1.5 },
        // Appended 2026-09-11, never inserted: make() seeds each segment from
        // a running counter, so anything added ahead of these would re-voice
        // every segment after it and move figures that are already recorded.
        //
        // Until this, the stimulus held a correction only on A3 and D3, so
        // the whole-plugin latency gate saw a 2.3-octave range through a
        // 5-semitone window and read 6.53 ms where the engine reaches 15.33
        // at the bottom and is later than Waves at the top. A2 is the lowest
        // note the rest of the stimulus already holds, so it adds the
        // coverage without widening the range the ruler and the references
        // were measured over.
        // testing-notes/tune-latency-review-2026-09-11.md.
        { Kind::marked,  "held +30c A2",      A2,  30.0,  0.0, 0.0, 1.5 },

        // And the rest of the range, for the same reason. The latency rule
        // compares BMO with Waves, but what it compares is pitch-dependent
        // for both -- Waves' delay tracks the period, BMO's rest does not --
        // so a ceiling taken at one note says nothing about any other. These
        // four give each tuner's delay a curve to be read off instead of a
        // scalar: E2 is the bottom of the Auto range, A4 and A5 the octaves
        // where BMO's flat rest is the larger term and where it turns out to
        // be later than Waves.
        //
        // These are also what forced the ruler's envelope wider: a marked A2
        // under the old 10 ms envelope read an ideal corrector 0.31 ms out
        // against its own 0.25 ms tolerance, because 10 ms barely spans A2's
        // 9.09 ms period. See markers().
        { Kind::marked,  "held +30c E2",      E2,  30.0,  0.0, 0.0, 1.5 },
        { Kind::marked,  "held +30c A4",      A4,  30.0,  0.0, 0.0, 1.5 },
        { Kind::marked,  "held +30c A5",      A5,  30.0,  0.0, 0.0, 1.5 },
    };
}

struct Stimulus
{
    double fs = 48000.0;
    std::vector<float> samples;
    std::vector<Segment> segments;
};

/** Level dips, 40 ms wide and 85 % deep, at uneven spacing: features an
    envelope cross-correlation cannot mistake for one another. Wide enough to
    survive an envelope smoothed over more than a period -- which it has to
    be, or the pitch-rate ripple a pitch shift moves dominates it (the first
    version, 8 ms dips under a 1 ms envelope, read 0.15 correlation).

    20 ms under a 10 ms envelope until 2026-09-11, which was enough only while
    the lowest marked note was D3. "More than a period" has to mean
    comfortably more: at A2, whose 9.09 ms period a 10 ms window barely spans,
    the ruler read an ideal corrector 0.31 ms out against its own 0.25 ms
    tolerance -- and the marked A2 and E2 added that day are exactly the low
    notes the latency rule needed measuring at. 40 ms dips under a 25 ms
    envelope clear E2's 12.13 ms period by two to one.
    testing-notes/tune-latency-review-2026-09-11.md. */
inline std::vector<float> markers (size_t n, double fs, std::uint64_t seed)
{
    std::vector<float> g (n, 1.0f);
    signals::Random rng (seed);
    const auto width = 0.040 * fs;
    double at = 0.15 * fs;

    while (at + width < (double) n)
    {
        for (size_t i = (size_t) at; i < (size_t) (at + width); ++i)
        {
            const auto u = ((double) i - at) / width;
            g[i] = (float) (1.0 - 0.85 * 0.5 * (1.0 - std::cos (2.0 * signals::kPi * u)));
        }
        at += (0.12 + 0.08 * (0.5 + 0.5 * rng.uniform())) * fs;
    }

    return g;
}

inline Stimulus make (double fs = 48000.0)
{
    Stimulus s;
    s.fs = fs;
    s.segments = layout();

    const auto gap = (size_t) std::lround (0.4 * fs);
    s.samples.assign ((size_t) std::lround (0.5 * fs), 0.0f);
    std::uint64_t seed = 2026;

    for (auto& seg : s.segments)
    {
        signals::Contour c;
        signals::VoiceSettings v;
        v.seed = seg.seed = ++seed;

        switch (seg.kind)
        {
            case Kind::inTune:
                c = signals::steady (seg.hz, seg.seconds, fs);
                v.shimmer = 0.3;   // each cycle its own level: one true delay (see tools/latency)
                break;
            case Kind::vibrato:
                c = signals::vibrato (seg.hz, seg.depthCents, seg.rateHz, seg.seconds, fs);
                break;
            case Kind::marked:
                c = signals::steady (seg.hz * std::exp2 (seg.offsetCents / 1200.0), seg.seconds, fs);
                break;
        }

        auto x = signals::voice (c, fs, v).samples;

        if (seg.kind == Kind::marked)
        {
            const auto g = markers (x.size(), fs, seed);
            for (size_t i = 0; i < x.size(); ++i)
                x[i] *= g[i];
        }

        seg.start = s.samples.size();
        seg.length = x.size();
        s.samples.insert (s.samples.end(), x.begin(), x.end());
        s.samples.insert (s.samples.end(), gap, 0.0f);
    }

    return s;
}

//==============================================================================
struct Row
{
    std::string name;
    Kind kind = Kind::inTune;
    double delayMs = NAN;         ///< inTune, marked: how late the output is
    double correlation = NAN;     ///< the peak the delay was read at
    double lagMs = NAN;           ///< vibrato: correction lag
    double rmsCents = NAN;        ///< vibrato: RMS of out - target
    double p95Cents = NAN;        ///< vibrato: 95th percentile of |out - target|
    double unexplainedCents = NAN;///< vibrato: RMS left after removing lag x slope
    int frames = 0;
};

struct Score
{
    std::vector<Row> rows;
    double inTuneLatencyMs = NAN;      ///< worst in-tune delay
    double correctingLatencyMs = NAN;  ///< worst delay while correcting
    double trueLatencyMs = NAN;        ///< the worse of the two: the figure the latency rule uses
    double meanLagMs = NAN, worstLagMs = NAN;
    double meanRmsCents = NAN;
};

namespace detail
{
    /** Normalised cross-correlation of y against x over x[a, a + n), lags
        [lo, hi]; the peak, parabolically refined. */
    inline double delayBetween (const std::vector<float>& x, const std::vector<float>& y,
                                size_t a, size_t n, int lo, int hi, double& peakOut)
    {
        std::vector<double> r ((size_t) (hi - lo + 1), -2.0);
        double ex = 0.0;
        for (size_t i = a; i < a + n; ++i)
            ex += (double) x[i] * x[i];

        for (int L = lo; L <= hi; ++L)
        {
            double sxy = 0.0, ey = 0.0;
            for (size_t i = a; i < a + n; ++i)
            {
                const auto j = (long long) i + L;
                if (j < 0 || (size_t) j >= y.size())
                    continue;
                sxy += (double) x[i] * y[(size_t) j];
                ey += (double) y[(size_t) j] * y[(size_t) j];
            }
            r[(size_t) (L - lo)] = ex > 0.0 && ey > 0.0 ? sxy / std::sqrt (ex * ey) : -2.0;
        }

        size_t best = 0;
        for (size_t k = 1; k < r.size(); ++k)
            if (r[k] > r[best])
                best = k;

        peakOut = r[best];
        double frac = 0.0;
        if (best > 0 && best + 1 < r.size())
        {
            const auto d = r[best - 1] - 2.0 * r[best] + r[best + 1];
            if (std::abs (d) > 1.0e-12)
                frac = 0.5 * (r[best - 1] - r[best + 1]) / d;
        }

        return (double) lo + (double) best + frac;
    }

    /** RMS over a sliding 25 ms: comfortably longer than any period in the
        stimulus (E2's is 12.13 ms), so the pitch-rate ripple is gone and only
        the level dips remain. 10 ms until 2026-09-11 -- see markers() for
        what that cost at the bottom of the range. */
    inline std::vector<float> envelope (const std::vector<float>& x, double fs)
    {
        const auto w = (size_t) std::lround (0.025 * fs);
        std::vector<float> e (x.size(), 0.0f);
        double acc = 0.0;
        for (size_t i = 0; i < x.size(); ++i)
        {
            acc += (double) x[i] * x[i];
            if (i >= w)
                acc -= (double) x[i - w] * x[i - w];
            e[i] = (float) std::sqrt (std::max (acc, 0.0) / (double) w);
        }
        return e;
    }

    inline double centsOf (double hz) { return 1200.0 * std::log2 (hz / 440.0); }
}

/** Scores a render of the stimulus. `out` is the tuner's output, sample 0
    at the stimulus' sample 0; `offset` is added to every delay search
    centre for a render known to start late or early by that many samples. */
inline Score score (const Stimulus& s, const std::vector<float>& out, long long offset = 0)
{
    Score sc;
    const auto fs = s.fs;
    const int lo = (int) offset - (int) std::lround (0.005 * fs);
    const int hi = (int) offset + (int) std::lround (0.060 * fs);

    const auto envIn = detail::envelope (s.samples, fs);
    const auto envOut = detail::envelope (out, fs);

    // Delays first: the vibrato segments are read at their pitch's delay.
    for (const auto& seg : s.segments)
    {
        if (seg.kind == Kind::vibrato)
            continue;

        Row row;
        row.name = seg.name;
        row.kind = seg.kind;

        const auto a = seg.start + (size_t) std::lround (0.3 * fs);
        const auto n = seg.length - (size_t) std::lround (0.4 * fs);

        if (seg.kind == Kind::inTune)
        {
            row.delayMs = 1000.0 * detail::delayBetween (s.samples, out, a, n, lo, hi, row.correlation) / fs;
        }
        else
        {
            // Mean-removed envelopes over the held part.
            std::vector<float> ei (envIn), eo (envOut);
            double mi = 0.0, mo = 0.0;
            for (size_t i = a; i < a + n; ++i) { mi += ei[i]; mo += eo[i]; }
            mi /= (double) n; mo /= (double) n;
            for (auto& v : ei) v = (float) (v - mi);
            for (auto& v : eo) v = (float) (v - mo);
            row.delayMs = 1000.0 * detail::delayBetween (ei, eo, a, n, lo, hi, row.correlation) / fs;
        }

        sc.rows.push_back (row);
    }

    auto delayAt = [&] (double hz)
    {
        for (size_t k = 0; k < s.segments.size(); ++k)
            if (s.segments[k].kind == Kind::inTune && std::abs (s.segments[k].hz - hz) < 1.0e-6)
                for (const auto& r : sc.rows)
                    if (r.name == s.segments[k].name)
                        return r.delayMs;
        return 0.0;
    };

    for (const auto& seg : s.segments)
    {
        if (seg.kind != Kind::vibrato)
            continue;

        Row row;
        row.name = seg.name;
        row.kind = seg.kind;

        // Frames long enough for the ruler to see two of the longest period
        // it may be asked about, 5 ms apart over the held part: ~320 points
        // to fit per vibrato, and the brute-force ruler stays affordable in
        // a test that scores six renders.
        const auto minHz = seg.hz / 1.3, maxHz = seg.hz * 1.3;
        const auto len = (size_t) std::ceil (2.2 * fs / minHz) + 32;
        const auto hop = (size_t) std::lround (0.005 * fs);
        const auto D = delayAt (seg.hz) * 0.001 * fs;   // output sample t is input sample t - D
        const auto target = detail::centsOf (seg.hz);

        std::vector<double> dev, slope;
        for (size_t f = seg.start + (size_t) (0.3 * fs); f + len < seg.start + seg.length - (size_t) (0.1 * fs); f += hop)
        {
            const auto outStart = (long long) f + (long long) std::lround (D);
            if (outStart < 0 || (size_t) outStart + len >= out.size())
                continue;

            const auto hz = analysis::measureHz (out, (size_t) outStart, len, fs, minHz, maxHz);
            if (hz <= 0.0)
                continue;

            // The input's slope over the same stretch of input, cents per second.
            const auto t0 = (double) (f - seg.start) / fs, t1 = t0 + (double) len / fs;
            const auto c0 = seg.depthCents * std::sin (2.0 * signals::kPi * seg.rateHz * t0);
            const auto c1 = seg.depthCents * std::sin (2.0 * signals::kPi * seg.rateHz * t1);

            dev.push_back (detail::centsOf (hz) - target);
            slope.push_back ((c1 - c0) / (t1 - t0));
        }

        row.frames = (int) dev.size();
        if (dev.size() >= 20)
        {
            double sds = 0.0, sss = 0.0, sdd = 0.0;
            for (size_t k = 0; k < dev.size(); ++k)
            {
                sds += dev[k] * slope[k];
                sss += slope[k] * slope[k];
                sdd += dev[k] * dev[k];
            }

            const auto L = sds / sss;                       // seconds
            row.lagMs = 1000.0 * L;
            row.rmsCents = std::sqrt (sdd / (double) dev.size());

            double left = 0.0;
            std::vector<double> ad;
            for (size_t k = 0; k < dev.size(); ++k)
            {
                const auto e = dev[k] - L * slope[k];
                left += e * e;
                ad.push_back (std::abs (dev[k]));
            }
            row.unexplainedCents = std::sqrt (left / (double) dev.size());
            std::sort (ad.begin(), ad.end());
            row.p95Cents = ad[(size_t) (0.95 * (double) (ad.size() - 1))];
        }

        sc.rows.push_back (row);
    }

    double worstIn = -1.0e9, worstMarked = -1.0e9, lagSum = 0.0, rmsSum = 0.0, worstLag = -1.0e9;
    int lags = 0;
    for (const auto& r : sc.rows)
    {
        if (r.kind == Kind::inTune && std::isfinite (r.delayMs)) worstIn = std::max (worstIn, r.delayMs);
        if (r.kind == Kind::marked && std::isfinite (r.delayMs)) worstMarked = std::max (worstMarked, r.delayMs);
        if (r.kind == Kind::vibrato && std::isfinite (r.lagMs))
        {
            lagSum += r.lagMs; rmsSum += r.rmsCents; ++lags;
            worstLag = std::max (worstLag, r.lagMs);
        }
    }

    sc.inTuneLatencyMs = worstIn;
    sc.correctingLatencyMs = worstMarked;
    sc.trueLatencyMs = std::max (worstIn, worstMarked);
    if (lags > 0)
    {
        sc.meanLagMs = lagSum / lags;
        sc.worstLagMs = worstLag;
        sc.meanRmsCents = rmsSum / lags;
    }

    return sc;
}

} // namespace bmo::tune::stimulus
