/*
    bmo-tune-gen: the synthetic test corpus (spec T-2).

    The script is committed; the WAVs are not. Every item is written with its
    ground truth beside it:

        <name>.wav            the signal, 32-bit float mono
        <name>.f0.csv         "n,f0" every half millisecond; f0 = 0 is unvoiced
        <name>.epochs.csv     sample index of every glottal closure, where the
                              source has them (the additive sources do)
        manifest.csv          name, group, description, for bmo-tune-score

        bmo-tune-gen [outdir] [--rate 48000] [--group name] [--list]

    Deterministic: seeded, and the same bytes on every machine the build runs
    on, so a corpus can be regenerated rather than stored.
*/

#include "tools/tune/common/Signals.h"
#include "tools/tune/common/Wav.h"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace sig = bmo::tune::signals;
namespace wav = bmo::tune::wav;

namespace
{
    struct Item
    {
        std::string name, group, description;
        std::function<sig::Rendered (double fs, sig::Contour& truth)> make;
    };

    /** Linear attack from silence over `ms`, applied to a rendered item. */
    void attack (std::vector<float>& x, size_t start, double ms, double fs)
    {
        const auto n = std::max<size_t> (1, (size_t) (ms * 0.001 * fs));
        for (size_t i = start; i < x.size() && i < start + n; ++i)
            x[i] *= (float) ((double) (i - start) / (double) n);
    }

    std::vector<Item> corpus()
    {
        std::vector<Item> items;
        const auto add = [&items] (std::string name, std::string group, std::string desc,
                                   std::function<sig::Rendered (double, sig::Contour&)> f)
        {
            items.push_back ({ std::move (name), std::move (group), std::move (desc), std::move (f) });
        };

        //== Pure sines ========================================================
        for (auto hz : { 55.0, 82.41, 110.0, 220.0, 440.0, 880.0, 1760.0 })
        {
            char n[64];
            std::snprintf (n, sizeof n, "sine_%g", hz);
            add (n, "sine", "steady sine", [hz] (double fs, sig::Contour& t)
            {
                t = sig::steady (hz, 2.0, fs);
                return sig::sine (t, fs);
            });
        }

        add ("sweep_linear", "sine", "linear sweep 50 -> 2000 Hz over 10 s", [] (double fs, sig::Contour& t)
        {
            t = sig::linearSweep (50.0, 2000.0, 10.0, fs);
            return sig::sine (t, fs);
        });

        add ("sweep_exponential", "sine", "exponential sweep 50 -> 2000 Hz over 10 s", [] (double fs, sig::Contour& t)
        {
            t = sig::glide (50.0, 2000.0, 10.0, fs);
            return sig::sine (t, fs);
        });

        //== Sawtooth on a scripted contour ===================================
        add ("saw_contour", "saw", "band-limited sawtooth over steps, glides and vibrato", [] (double fs, sig::Contour& t)
        {
            t = sig::concat ({ sig::steady (220.0, 0.5, fs), sig::step (220.0, 246.94, 0.6, 0.0, fs),
                               sig::glide (246.94, 392.0, 0.8, fs), sig::vibrato (392.0, 50.0, 5.5, 1.0, fs),
                               sig::silence (0.2, fs), sig::step (130.81, 196.0, 1.0, 50.0, fs) });
            return sig::sawtooth (t, fs);
        });

        //== Source-filter voices =============================================
        struct VoiceKind { const char* name; double hz; bool female; double jitter; };
        for (const auto& v : { VoiceKind { "voice_male_clean", 130.81, false, 0.0 },
                               VoiceKind { "voice_male_jitter05", 130.81, false, 0.005 },
                               VoiceKind { "voice_male_jitter2", 130.81, false, 0.02 },
                               VoiceKind { "voice_female_clean", 293.66, true, 0.0 },
                               VoiceKind { "voice_female_jitter05", 293.66, true, 0.005 },
                               VoiceKind { "voice_female_jitter2", 293.66, true, 0.02 } })
        {
            add (v.name, "voice", "source-filter vowel with jitter/shimmer/breath", [v] (double fs, sig::Contour& t)
            {
                sig::VoiceSettings s;
                if (v.female)
                {
                    const double f[5] { 850.0, 1610.0, 2850.0, 3900.0, 4950.0 };
                    std::copy (f, f + 5, s.formants);
                }
                s.jitter = v.jitter;
                s.shimmer = v.jitter > 0.0 ? 0.04 : 0.0;
                s.breath = v.jitter > 0.0 ? 0.03 : 0.0;
                t = sig::withJitter (sig::steady (v.hz, 3.0, fs), s.jitter, fs, s.seed);
                return sig::voice (t, fs, s);
            });
        }

        //== Vibrato set ======================================================
        for (auto rate : { 4.0, 5.5, 7.0 })
            for (auto depth : { 20.0, 50.0, 100.0 })
            {
                char n[64];
                std::snprintf (n, sizeof n, "vibrato_%gHz_%gc", rate, depth);
                add (n, "vibrato", "voice at 330 Hz with vibrato", [rate, depth] (double fs, sig::Contour& t)
                {
                    t = sig::vibrato (330.0, depth, rate, 3.0, fs);
                    return sig::voice (t, fs);
                });
            }

        //== Transitions ======================================================
        struct Interval { const char* name; double ratio; };
        for (const auto& iv : { Interval { "semitone", std::exp2 (1.0 / 12.0) }, Interval { "wholetone", std::exp2 (2.0 / 12.0) },
                                Interval { "fifth", 1.5 }, Interval { "octave", 2.0 } })
            for (auto glideMs : { 0.0, 10.0, 50.0, 200.0 })
            {
                char n[64];
                std::snprintf (n, sizeof n, "transition_%s_%gms", iv.name, glideMs);
                const auto ratio = iv.ratio;
                add (n, "transition", "voice step up then back down", [ratio, glideMs] (double fs, sig::Contour& t)
                {
                    t = sig::concat ({ sig::step (220.0, 220.0 * ratio, 1.0, glideMs, fs),
                                       sig::step (220.0 * ratio, 220.0, 1.0, glideMs, fs) });
                    return sig::voice (t, fs);
                });
            }

        //== Onsets ===========================================================
        for (auto hz : { 110.0, 220.0, 440.0, 880.0 })
            for (auto attackMs : { 1.0, 5.0, 20.0, 100.0 })
            {
                char n[64];
                std::snprintf (n, sizeof n, "onset_%gHz_%gms", hz, attackMs);
                add (n, "onset", "voice from silence, linear attack", [hz, attackMs] (double fs, sig::Contour& t)
                {
                    t = sig::concat ({ sig::silence (0.3, fs), sig::steady (hz, 0.7, fs),
                                       sig::silence (0.3, fs), sig::steady (hz, 0.7, fs) });
                    auto r = sig::voice (t, fs);
                    attack (r.samples, (size_t) (0.3 * fs), attackMs, fs);
                    attack (r.samples, (size_t) (1.3 * fs), attackMs, fs);
                    return r;
                });
            }

        //== Pathological =====================================================
        const auto constant = [] (const char* name, const char* desc, std::function<float (size_t)> f)
        {
            return Item { name, "pathological", desc, [f] (double fs, sig::Contour& t)
            {
                t = sig::silence (2.0, fs);
                sig::Rendered r;
                r.samples.resize (t.size());
                for (size_t i = 0; i < t.size(); ++i)
                    r.samples[i] = f (i);
                return r;
            } };
        };

        items.push_back (constant ("path_silence", "digital silence", [] (size_t) { return 0.0f; }));
        items.push_back (constant ("path_dc", "DC offset 0.5", [] (size_t) { return 0.5f; }));
        items.push_back (constant ("path_alternating", "alternating +/-1", [] (size_t i) { return (i & 1) ? 1.0f : -1.0f; }));
        items.push_back (constant ("path_impulses", "an impulse every 4800 samples", [] (size_t i) { return i % 4800 == 0 ? 1.0f : 0.0f; }));
        // A naive (aliased) square is still a 220 Hz note, so unlike the
        // constants above its truth is voiced. It was written as silence at
        // first and scored as 99 % false alarms -- the detector was right.
        add ("path_square", "pathological", "naive full-scale 220 Hz square (aliased)", [] (double fs, sig::Contour& t)
        {
            t = sig::steady (220.0, 2.0, fs);
            sig::Rendered r;
            r.samples.resize (t.size());
            for (size_t i = 0; i < t.size(); ++i)
                r.samples[i] = std::fmod ((double) i * 220.0 / fs, 1.0) < 0.5 ? 1.0f : -1.0f;
            return r;
        });

        add ("path_denormal_noise", "pathological", "white noise at 1e-30", [] (double fs, sig::Contour& t)
        {
            t = sig::silence (2.0, fs);
            return sig::Rendered { sig::whiteNoise (t.size(), 1.0e-30, 3), {} };
        });

        add ("path_sine_0999", "pathological", "0.999 FS sine at 220 Hz", [] (double fs, sig::Contour& t)
        {
            t = sig::steady (220.0, 2.0, fs);
            return sig::sine (t, fs, 0.999);
        });

        add ("path_clipped", "pathological", "voice at 220 Hz driven 12 dB into clipping", [] (double fs, sig::Contour& t)
        {
            t = sig::steady (220.0, 2.0, fs);
            auto r = sig::voice (t, fs);
            for (auto& v : r.samples)
                v = std::max (-0.5f, std::min (0.5f, v * 4.0f));
            return r;
        });

        //== Contamination ====================================================
        for (auto snr : { 30.0, 20.0, 10.0 })
            for (auto pink : { false, true })
            {
                char n[64];
                std::snprintf (n, sizeof n, "noise_%s_%gdB", pink ? "pink" : "white", snr);
                add (n, "noise", "voice gliding 180 -> 360 Hz with vibrato, at SNR", [snr, pink] (double fs, sig::Contour& t)
                {
                    const auto g = sig::glide (180.0, 360.0, 3.0, fs);
                    t = g;
                    for (size_t i = 0; i < t.size(); ++i)
                        t[i] *= std::exp2 (30.0 / 1200.0 * std::sin (2.0 * sig::kPi * 5.0 * (double) i / fs));
                    auto r = sig::voice (t, fs);
                    const auto noise = pink ? sig::pinkNoise (t.size(), 1.0, 17) : sig::whiteNoise (t.size(), 1.0, 17);
                    r.samples = sig::atSnr (r.samples, noise, snr);
                    return r;
                });
            }

        //== Graceful degradation: not a single voice =========================
        add ("poly_two_voices", "poly", "two voices, 220 and 277 Hz: no ground truth, must not explode", [] (double fs, sig::Contour& t)
        {
            t = sig::silence (2.0, fs);
            auto a = sig::voice (sig::steady (220.0, 2.0, fs), fs);
            const auto b = sig::voice (sig::steady (277.18, 2.0, fs), fs);
            for (size_t i = 0; i < a.samples.size(); ++i)
                a.samples[i] = 0.5f * (a.samples[i] + b.samples[i]);
            a.epochs.clear();
            return a;
        });

        return items;
    }

    void writeTruth (const std::string& path, const sig::Contour& t, double fs)
    {
        auto* f = std::fopen (path.c_str(), "w");
        std::fprintf (f, "n,f0\n");
        const auto step = std::max<size_t> (1, (size_t) std::lround (fs * 0.0005));
        for (size_t i = 0; i < t.size(); i += step)
            std::fprintf (f, "%zu,%.6f\n", i, t[i]);
        std::fclose (f);
    }

    void writeEpochs (const std::string& path, const std::vector<long long>& epochs)
    {
        auto* f = std::fopen (path.c_str(), "w");
        std::fprintf (f, "n\n");
        for (auto e : epochs)
            std::fprintf (f, "%lld\n", e);
        std::fclose (f);
    }
}

int main (int argc, char** argv)
{
    std::string outdir = "corpus", group;
    double rate = 48000.0;
    bool list = false;

    for (int i = 1; i < argc; ++i)
    {
        const std::string a = argv[i];
        if (a == "--rate" && i + 1 < argc)       rate = std::atof (argv[++i]);
        else if (a == "--group" && i + 1 < argc) group = argv[++i];
        else if (a == "--list")                  list = true;
        else if (a[0] != '-')                    outdir = a;
        else
        {
            std::fprintf (stderr, "usage: bmo-tune-gen [outdir] [--rate hz] [--group name] [--list]\n");
            return 2;
        }
    }

    const auto items = corpus();

    if (list)
    {
        for (const auto& it : items)
            std::printf ("%-28s %-13s %s\n", it.name.c_str(), it.group.c_str(), it.description.c_str());
        return 0;
    }

    std::filesystem::create_directories (outdir);
    auto* manifest = std::fopen ((outdir + "/manifest.csv").c_str(), "w");
    std::fprintf (manifest, "name,group,rate,description\n");
    int written = 0;

    for (const auto& it : items)
    {
        if (! group.empty() && it.group != group)
            continue;

        sig::Contour truth;
        const auto r = it.make (rate, truth);
        const auto base = outdir + "/" + it.name;

        wav::writeMono (base + ".wav", r.samples, rate);
        writeTruth (base + ".f0.csv", truth, rate);
        if (! r.epochs.empty())
            writeEpochs (base + ".epochs.csv", r.epochs);

        std::fprintf (manifest, "%s,%s,%.0f,%s\n", it.name.c_str(), it.group.c_str(), rate, it.description.c_str());
        ++written;
    }

    std::fclose (manifest);
    std::fprintf (stderr, "bmo-tune-gen: %d items at %.0f Hz into %s\n", written, rate, outdir.c_str());
    return 0;
}
