/*
    The detector (spec §3), against signals whose f0 is known exactly.

    Gates from spec §9: fine pitch error under 5 cents RMS on correctly
    detected frames, gross pitch error (> 50 cents) under 1 %. Those are
    corpus-level numbers and bmo-tune-score owns them; what is here is the
    per-signal floor underneath them -- if a steady sine is not detected to a
    fraction of a cent, no corpus score will be good.
*/

#include "modules/tune/dsp/Detector.h"
#include "modules/tune/dsp/Pitch.h"
#include "tests/dsp/tune/TestUtil.h"
#include "tools/tune/common/Signals.h"

#include <cstdio>
#include <string>

using namespace bmo::tune;
using namespace bmo::tune::test;
namespace sig = bmo::tune::signals;

namespace
{
    struct Score
    {
        double rmsCents = 0.0;    ///< over voiced frames within 50 cents
        double grossRate = 0.0;   ///< fraction of voiced-truth frames off by > 50 cents or unvoiced
        double lockMs = -1.0;     ///< first voiced frame within 20 cents, from signal start
        int frames = 0;
    };

    /** The truth a detector with a two-period trailing span can be held to:
        the contour's mean in log-frequency over [i - 2T, i]. On a steady tone
        that is the tone. On a glide it is the pitch one period ago -- which
        is where the detector's window is centred, and where the spec's
        time-aligned GPE/FPE measure it. On jitter it is the average of the
        periods the window actually contains, rather than the one period that
        happens to be current, which no causal detector could report. */
    std::vector<double> alignedTruth (const sig::Contour& truth, double fs)
    {
        std::vector<double> prefix (truth.size() + 1, 0.0);
        for (size_t i = 0; i < truth.size(); ++i)
            prefix[i + 1] = prefix[i] + (truth[i] > 0.0 ? std::log2 (truth[i]) : 0.0);

        std::vector<double> out (truth.size(), 0.0);
        for (size_t i = 0; i < truth.size(); ++i)
        {
            if (truth[i] <= 0.0)
                continue;

            const auto span = (size_t) std::lround (2.0 * fs / truth[i]);
            if (span > i)
            {
                out[i] = truth[i];
                continue;
            }

            bool voicedThroughout = true;
            for (size_t j = i - span; j <= i && voicedThroughout; j += std::max<size_t> (1, span / 8))
                voicedThroughout = truth[j] > 0.0;

            out[i] = voicedThroughout
                       ? std::exp2 ((prefix[i + 1] - prefix[i - span]) / (double) (span + 1))
                       : truth[i];
        }

        return out;
    }

    /** Runs a signal through a fresh detector and scores every evaluation
        after `skipSeconds` against the contour. */
    Score score (const std::vector<float>& x, const sig::Contour& rawTruth, double fs,
                 Detector::Settings settings = {}, double skipSeconds = 0.05)
    {
        Detector d;
        d.prepare (fs, settings);

        Score s;
        double sumSq = 0.0;
        int good = 0, gross = 0, total = 0;
        const auto skip = (size_t) (skipSeconds * fs);
        const auto truth = alignedTruth (rawTruth, fs);

        for (size_t i = 0; i < x.size(); ++i)
        {
            d.push (x[i]);

            if (! d.evaluatedThisSample() || truth[i] <= 0.0)
                continue;

            const auto& e = d.estimate();
            const auto cents = e.voiced && e.hz > 0.0 ? pitch::centsBetween (e.hz, truth[i]) : 1.0e9;

            if (s.lockMs < 0.0 && std::abs (cents) < 20.0)
                s.lockMs = 1000.0 * (double) i / fs;

            if (i < skip)
                continue;

            ++total;
            if (std::abs (cents) > 50.0)
                ++gross;
            else
            {
                sumSq += cents * cents;
                ++good;
            }
        }

        s.frames = total;
        s.rmsCents = good ? std::sqrt (sumSq / good) : 1.0e9;
        s.grossRate = total ? (double) gross / total : 1.0;
        return s;
    }

    std::string label (const char* what, double hz, double fs)
    {
        char buf[128];
        std::snprintf (buf, sizeof buf, "%s %.2f Hz @ %.1f kHz", what, hz, fs / 1000.0);
        return buf;
    }
}

int main()
{
    //== Steady tones across the default range, 48 kHz =========================
    std::printf ("steady tones, default range 80-1400 Hz, 48 kHz\n");

    for (auto hz : { 82.41, 110.0, 146.83, 220.0, 311.13, 440.0, 659.26, 880.0, 1174.66, 1318.51 })
    {
        const double fs = 48000.0;
        const auto c = sig::steady (hz, 0.6, fs);

        const auto sine = score (sig::sine (c, fs).samples, c, fs);
        report (label ("sine rms cents", hz, fs), sine.rmsCents, "c");
        check (sine.grossRate == 0.0, label ("no gross errors on a sine", hz, fs));
        check (sine.rmsCents < 1.0, label ("sine detected within 1 cent rms", hz, fs));

        const auto saw = score (sig::sawtooth (c, fs).samples, c, fs);
        report (label ("saw rms cents", hz, fs), saw.rmsCents, "c");
        check (saw.grossRate == 0.0, label ("no gross errors on a sawtooth", hz, fs));
        check (saw.rmsCents < 2.0, label ("sawtooth detected within 2 cents rms", hz, fs));

        const auto voice = score (sig::voice (c, fs).samples, c, fs);
        report (label ("voice rms cents", hz, fs), voice.rmsCents, "c");
        check (voice.grossRate < 0.01, label ("gross error under 1 % on the synthetic voice", hz, fs));
        check (voice.rmsCents < 5.0, label ("synthetic voice within 5 cents rms (spec §9)", hz, fs));
    }

    //== Every supported sample rate ===========================================
    std::printf ("sample rates\n");

    for (auto fs : { 44100.0, 48000.0, 88200.0, 96000.0, 176400.0, 192000.0 })
    {
        for (auto hz : { 98.0, 440.0, 1046.5 })
        {
            const auto c = sig::steady (hz, 0.4, fs);
            const auto v = score (sig::voice (c, fs).samples, c, fs);
            report (label ("voice rms cents", hz, fs), v.rmsCents, "c");
            check (v.grossRate < 0.01 && v.rmsCents < 5.0, label ("voice detected", hz, fs));
        }
    }

    //== The Bass range reaches 55 Hz ==========================================
    {
        Detector::Settings bass;
        bass.minHz = 55.0;
        bass.maxHz = 500.0;
        const double fs = 48000.0;

        for (auto hz : { 55.0, 61.74, 73.42 })
        {
            const auto c = sig::steady (hz, 0.8, fs);
            const auto v = score (sig::voice (c, fs).samples, c, fs, bass, 0.1);
            report (label ("bass-range voice rms cents", hz, fs), v.rmsCents, "c");
            check (v.grossRate < 0.01 && v.rmsCents < 5.0, label ("bass range detects", hz, fs));
        }
    }

    //== Moving pitch ==========================================================
    {
        const double fs = 48000.0;
        const auto glide = sig::glide (110.0, 880.0, 3.0, fs);
        const auto g = score (sig::voice (glide, fs).samples, glide, fs);
        report ("3-octave glide rms cents", g.rmsCents, "c");
        report ("3-octave glide gross rate", 100.0 * g.grossRate, "%");
        check (g.grossRate < 0.01, "a three-octave glide tracks with under 1 % gross error");
        check (g.rmsCents < 10.0, "and within 10 cents rms while moving");

        const auto vib = sig::vibrato (330.0, 100.0, 5.5, 2.0, fs);
        const auto v = score (sig::voice (vib, fs).samples, vib, fs);
        report ("5.5 Hz +/-100 c vibrato rms cents", v.rmsCents, "c");
        check (v.grossRate < 0.01 && v.rmsCents < 8.0, "a wide vibrato is followed");
    }

    //== Jitter, shimmer, breath, noise =========================================
    {
        const double fs = 48000.0;
        sig::VoiceSettings rough;
        rough.jitter = 0.005;
        rough.shimmer = 0.05;
        rough.breath = 0.05;
        const auto truth = sig::withJitter (sig::steady (196.0, 1.0, fs), rough.jitter, fs, rough.seed);
        const auto x = sig::voice (truth, fs, rough).samples;

        const auto s = score (x, truth, fs);
        report ("0.5 % jitter voice rms cents", s.rmsCents, "c");
        check (s.grossRate < 0.01 && s.rmsCents < 10.0, "0.5 % jitter, shimmer and breath still track");

        const auto noisy = sig::atSnr (sig::voice (sig::steady (220.0, 1.0, fs), fs).samples,
                                       sig::whiteNoise ((size_t) fs, 1.0, 21), 20.0);
        const auto c = sig::steady (220.0, 1.0, fs);
        const auto n = score (noisy, c, fs);
        report ("20 dB SNR voice gross rate", 100.0 * n.grossRate, "%");
        report ("20 dB SNR voice rms cents", n.rmsCents, "c");
        check (n.grossRate < 0.03, "gross error under 3 % at 20 dB SNR (spec §9)");
    }

    //== Voicing ===============================================================
    {
        const double fs = 48000.0;

        const auto voicedCount = [fs] (const std::vector<float>& x)
        {
            Detector d;
            d.prepare (fs, {});
            int voiced = 0, frames = 0;
            for (auto v : x)
            {
                d.push (v);
                if (d.evaluatedThisSample()) { ++frames; voiced += d.estimate().voiced ? 1 : 0; }
            }
            return frames ? (double) voiced / frames : 0.0;
        };

        check (voicedCount (std::vector<float> ((size_t) fs, 0.0f)) == 0.0, "silence is never voiced");
        check (voicedCount (sig::whiteNoise ((size_t) fs, 0.3, 2)) < 0.01, "white noise is not voiced");
        check (voicedCount (std::vector<float> ((size_t) fs, 0.5f)) == 0.0, "a DC offset is not voiced");
        check (voicedCount (sig::whiteNoise ((size_t) fs, 1.0e-30, 3)) == 0.0, "denormal-level noise is not voiced");

        // Alternating +/-1 is periodic at 2 samples = 24 kHz, far above the
        // range; it must not be called a note inside it.
        std::vector<float> alt ((size_t) fs);
        for (size_t i = 0; i < alt.size(); ++i)
            alt[i] = (i & 1) ? 1.0f : -1.0f;
        check (voicedCount (alt) == 0.0, "alternating +/-1 is not a note");
    }

    //== Time to lock, per pitch ===============================================
    // Two sources, two claims. A sawtooth has no resonance, so its lock time
    // is the detector's own: that is the number spec §2 predicts (1.5-2
    // periods of window plus the voicing attack) and it is gated tightly.
    // The synthetic voice's formant resonators take a few milliseconds to
    // ring up -- F1 at 700 Hz with 80 Hz of bandwidth has a 4 ms time
    // constant -- and until they have, the waveform genuinely is not
    // periodic at f0; at 880 Hz the detector reads the ringing F1 for the
    // first few ms. That is reported, and gated only loosely.
    std::printf ("time to lock from silence (onset at 100 ms)\n");
    {
        const double fs = 48000.0;
        for (auto hz : { 82.41, 110.0, 220.0, 440.0, 880.0, 1318.51 })
        {
            const auto c = sig::concat ({ sig::silence (0.1, fs), sig::steady (hz, 0.4, fs) });

            const auto saw = score (sig::sawtooth (c, fs).samples, c, fs);
            const auto sawLock = saw.lockMs - 100.0;
            const auto sawPeriods = sawLock / (1000.0 / hz);

            const auto voice = score (sig::voice (c, fs).samples, c, fs);
            const auto voiceLock = voice.lockMs - 100.0;

            char buf[128];
            std::snprintf (buf, sizeof buf, "lock %.2f Hz, sawtooth (%.2f periods)", hz, sawPeriods);
            report (buf, sawLock, "ms");
            std::snprintf (buf, sizeof buf, "lock %.2f Hz, synthetic voice", hz);
            report (buf, voiceLock, "ms");

            check (saw.lockMs > 0.0 && sawPeriods < 3.0, label ("sawtooth locks within three periods", hz, fs));
            check (voice.lockMs > 0.0 && voiceLock < std::max (12.0, 4000.0 / hz),
                   label ("voice locks within 12 ms or four periods", hz, fs));
        }
    }

    return finish ("detector");
}
