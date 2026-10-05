/*
    Offline measurement harness for BMO Opto.

    Links the DSP core directly -- no plugin host, no GUI, no JUCE -- same
    shape as tools/measure/eq and tools/measure/sat. Two jobs:

      1. Dosage/release verification: renders the exact signal shapes
         tests/dsp/OptoDspTests.cpp already asserts on numerically (a short
         loud transient against a long loud sustain at matched peak, plus a
         gentler hit, in both Mode settings) and prints the same checkpoint
         readings those tests check, so a listen and a number always agree.
         Also writes the dry/wet pairs to disk for by-ear checking.

      2. Comparison-ready rendering: arbitrary WAV in, BMO's processed WAV
         out, with Mode/Crush/Level/Link/Color as flags -- so the result can
         sit next to a hand-bounced competitor pass (Opto plug A or B for Tele,
         Comp plug C or D for Stressed) at a matched input.
         `gen` exports the harness's own built-in test signals as WAV, so the
         *same* file can be fed through a competitor plugin for that
         comparison rather than something only approximately alike.

        measure release [--outdir dir]
        measure gen <short|long|light|ceiling|sine> --out file.wav
                     [--freq hz] [--seconds s] [--amp a]
        measure render [--in in.wav] --out out.wav
                     [--mode tele|stressed] [--crush pct] [--level db]
                     [--link 0|1] [--color 0|1]

    With no --in, render uses a short/long/light composite so a one-shot
    invocation still has something matched-level to show.
*/

#include "modules/opto/dsp/DspCore.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <utility>
#include <vector>

using namespace bmo::opto;

namespace
{

constexpr double kPi = 3.14159265358979323846;
constexpr double kSampleRate = 48000.0;

//==============================================================================
// Signals -- the same shapes tests/dsp/OptoDspTests.cpp drives the DSP with.
//==============================================================================

std::vector<float> sine (double hz, double seconds, double amplitude, double rate = kSampleRate)
{
    const auto n = (size_t) (seconds * rate);
    std::vector<float> out (n);

    for (size_t i = 0; i < n; ++i)
        out[i] = (float) (amplitude * std::sin (2.0 * kPi * hz * (double) i / rate));

    return out;
}

std::vector<float> withSilence (const std::vector<float>& hit, double silenceSeconds, double rate = kSampleRate)
{
    auto out = hit;
    const std::vector<float> silence ((size_t) (rate * silenceSeconds), 0.0f);
    out.insert (out.end(), silence.begin(), silence.end());
    return out;
}

/** Short loud transient: 200 Hz, 0.2 s, matching reductionAfter()'s "short"
    case in the test file. */
std::vector<float> shortHit()  { return sine (200.0, 0.2, 0.9); }

/** Long loud sustain, matched peak to shortHit() -- reductionAfter()'s
    "long" case. The point of the pair is that they hit the same threshold
    the same amount and only differ in how long they hold it there. */
std::vector<float> longHit()   { return sine (200.0, 2.5, 0.9); }

/** A hit that barely engages the cell, for contrast against the two loud
    ones above -- lower amplitude and left at the DSP's own default CRUSH
    (35 %) rather than the 80 % the loud cases use, so "light" means both
    quieter and less aggressively set, not just quieter. */
std::vector<float> lightHit()  { return sine (200.0, 1.0, 0.3); }

/** The long, heavy hit testCompUnitBReleaseCeilingExceedsOptoUnitA() uses to
    show Stressed's ~20 s ceiling reaching further than Tele's ~15 s one. */
std::vector<float> ceilingHit() { return sine (200.0, 8.0, 0.9); }

//==============================================================================
// Driving the real signal path.
//==============================================================================

/** N-channel render through a fresh core. `input` supplies the channel
    count (1 = mono, 2 = stereo -- DspCore supports no more). */
std::vector<std::vector<float>> render (const std::vector<std::vector<float>>& input,
                                        const DspCore::Params& params, double rate = kSampleRate)
{
    DspCore core;
    core.prepare (rate, 512, (int) input.size());
    core.setParams (params);

    auto out = input;
    const auto n = out.empty() ? (size_t) 0 : out[0].size();
    std::vector<float*> ptrs (out.size());

    for (size_t at = 0; at < n; at += 512)
    {
        for (size_t c = 0; c < out.size(); ++c)
            ptrs[c] = out[c].data() + at;

        core.process (ptrs.data(), (int) out.size(), (int) std::min<size_t> (512, n - at));
    }

    return out;
}

std::vector<float> renderMono (const std::vector<float>& input, const DspCore::Params& params, double rate = kSampleRate)
{
    return render (std::vector<std::vector<float>> { input }, params, rate).front();
}

/** Reduction dB right when a hit of `loudSeconds` ends, and again
    `silenceSeconds` after that -- the same pairing
    tests/dsp/OptoDspTests.cpp's reductionAtEndAndAfter() checks, read in
    512-sample blocks the same way, so the two numbers printed here are
    checkable against the assertions there rather than a different
    measurement that happens to have a similar name. */
std::pair<float, float> reductionAtEndAndAfter (const std::vector<float>& hit, double silenceSeconds,
                                                float crushPercent, Mode mode, double rate = kSampleRate)
{
    DspCore core;
    DspCore::Params p;
    p.crushPercent = crushPercent;
    p.mode = mode;
    core.prepare (rate, 512, 1);
    core.setParams (p);

    auto signal = withSilence (hit, silenceSeconds, rate);
    const auto loudSamples = hit.size();

    auto atEnd = 0.0f, atCheckpoint = 0.0f;

    for (size_t at = 0; at < signal.size(); at += 512)
    {
        const auto n = (int) std::min<size_t> (512, signal.size() - at);
        auto* pp = signal.data() + at;
        core.process (&pp, 1, n);
        const auto reduction = core.currentGainReductionDb();

        if (at < loudSamples && at + (size_t) n >= loudSamples)
            atEnd = reduction;

        atCheckpoint = reduction;
    }

    return { atEnd, atCheckpoint };
}

//==============================================================================
/** Minimal WAV reader: PCM 16/24/32 and IEEE float 32, any channel count,
    deinterleaved (not summed -- Link needs real per-channel content). */
bool readWav (const std::string& path, std::vector<std::vector<float>>& channels, double& rate)
{
    std::ifstream file (path, std::ios::binary);

    if (! file)
        return false;

    const std::vector<char> bytes { std::istreambuf_iterator<char> (file),
                                    std::istreambuf_iterator<char>() };

    if (bytes.size() < 44 || std::memcmp (bytes.data(), "RIFF", 4) != 0
                          || std::memcmp (bytes.data() + 8, "WAVE", 4) != 0)
        return false;

    const auto u16 = [&bytes] (size_t at) { return (uint32_t) (uint8_t) bytes[at]
                                                 | ((uint32_t) (uint8_t) bytes[at + 1] << 8); };
    const auto u32 = [&u16] (size_t at)   { return u16 (at) | (u16 (at + 2) << 16); };

    uint32_t format = 1, numChannels = 1, bits = 16;
    size_t at = 12;

    while (at + 8 <= bytes.size())
    {
        const std::string id (bytes.data() + at, 4);
        const auto size = (size_t) u32 (at + 4);
        const auto body = at + 8;

        if (id == "fmt " && body + 16 <= bytes.size())
        {
            format      = u16 (body);
            numChannels = std::max (1u, u16 (body + 2));
            rate        = (double) u32 (body + 4);
            bits        = u16 (body + 14);
        }
        else if (id == "data")
        {
            const auto bytesPerSample = bits / 8;

            if (bytesPerSample == 0)
                return false;

            const auto frames = std::min (size, bytes.size() - body) / (bytesPerSample * numChannels);
            channels.assign (numChannels, std::vector<float> (frames, 0.0f));

            for (size_t f = 0; f < frames; ++f)
            {
                for (uint32_t c = 0; c < numChannels; ++c)
                {
                    const auto p = body + (f * numChannels + c) * bytesPerSample;
                    double v = 0.0;

                    if (format == 3 && bits == 32)
                    {
                        float bits32 = 0.0f;
                        std::memcpy (&bits32, bytes.data() + p, 4);
                        v = bits32;
                    }
                    else if (bits == 16)
                    {
                        v = (double) (int16_t) (uint16_t) u16 (p) / 32768.0;
                    }
                    else if (bits == 24)
                    {
                        auto raw = (int32_t) (u16 (p) | ((uint32_t) (uint8_t) bytes[p + 2] << 16));
                        if (raw & 0x800000) raw -= 0x1000000;
                        v = (double) raw / 8388608.0;
                    }
                    else if (bits == 32)
                    {
                        v = (double) (int32_t) u32 (p) / 2147483648.0;
                    }
                    else
                    {
                        return false;
                    }

                    channels[c][f] = (float) v;
                }
            }

            return ! channels.empty() && ! channels.front().empty();
        }

        at = body + size + (size & 1);
    }

    return false;
}

/** 24-bit PCM out, any channel count, interleaved. Creates the destination's
    parent directory if it doesn't exist yet, so `release`'s default output
    directory (and any --out path a caller points at a fresh folder) doesn't
    silently fail to write. */
bool writeWav (const std::string& path, const std::vector<std::vector<float>>& channels, double rate)
{
    if (channels.empty())
        return false;

    const std::filesystem::path p (path);

    if (p.has_parent_path())
        std::filesystem::create_directories (p.parent_path());

    std::ofstream file (path, std::ios::binary);

    if (! file)
        return false;

    const auto numChannels = (uint32_t) channels.size();
    const auto frames = channels.front().size();
    const uint32_t dataBytes = (uint32_t) (frames * numChannels * 3);
    const uint32_t byteRate  = (uint32_t) rate * numChannels * 3;

    const auto u32 = [&file] (uint32_t v) { file.put ((char) (v & 0xff)); file.put ((char) ((v >> 8) & 0xff));
                                            file.put ((char) ((v >> 16) & 0xff)); file.put ((char) ((v >> 24) & 0xff)); };
    const auto u16 = [&file] (uint16_t v) { file.put ((char) (v & 0xff)); file.put ((char) ((v >> 8) & 0xff)); };

    file.write ("RIFF", 4); u32 (36 + dataBytes); file.write ("WAVE", 4);
    file.write ("fmt ", 4); u32 (16); u16 (1); u16 ((uint16_t) numChannels); u32 ((uint32_t) rate);
    u32 (byteRate); u16 ((uint16_t) (numChannels * 3)); u16 (24);
    file.write ("data", 4); u32 (dataBytes);

    for (size_t f = 0; f < frames; ++f)
    {
        for (uint32_t c = 0; c < numChannels; ++c)
        {
            const auto clamped = std::max (-1.0f, std::min (1.0f, channels[c][f]));
            const auto value = (int32_t) std::lround ((double) clamped * 8388607.0);
            file.put ((char) (value & 0xff));
            file.put ((char) ((value >> 8) & 0xff));
            file.put ((char) ((value >> 16) & 0xff));
        }
    }

    return true;
}

//==============================================================================
double rms (const std::vector<float>& x)
{
    double sum = 0.0;
    for (auto v : x) sum += (double) v * (double) v;
    return x.empty() ? 0.0 : std::sqrt (sum / (double) x.size());
}

double peak (const std::vector<float>& x)
{
    double m = 0.0;
    for (auto v : x) m = std::max (m, (double) std::abs (v));
    return m;
}

double dbfs (double linear) { return 20.0 * std::log10 (std::max (linear, 1.0e-9)); }

const char* modeName (Mode m) { return m == Mode::OptoUnitA ? "Tele" : "Stressed"; }

bool parseMode (const std::string& s, Mode& mode)
{
    if (s == "tele")     { mode = Mode::OptoUnitA; return true; }
    if (s == "stressed") { mode = Mode::CompUnitB; return true; }
    return false;
}

//==============================================================================
/** The checkpoint table testReleaseIsProgramDependent(),
    testCompUnitBReleaseCeilingExceedsOptoUnitA() and testQuietSignalIsLeftAlone()
    assert against, printed as numbers rather than pass/fail -- so a change
    that moves these shows up here before it shows up as a broken test. Also
    writes each case's dry/wet pair to `outdir` for a listen. */
void printReleaseReport (const std::string& outdir)
{
    std::printf ("Dosage/release verification. Same signal shapes as\n"
                 "tests/dsp/OptoDspTests.cpp -- these numbers should track those\n"
                 "tests' pass/fail thresholds, not merely resemble them.\n\n");

    std::printf ("%-11s %-8s %6s %8s %10s %10s %8s\n",
                 "mode", "hit", "crush", "hold s", "at end dB", "+N s dB", "retain %");

    struct Case { const char* name; std::vector<float> (*signal)(); float crush; double checkpointS; };
    const Case cases[]
    {
        { "short",   shortHit,   80.0f, 3.0 },
        { "long",    longHit,    80.0f, 3.0 },
        { "light",   lightHit,   35.0f, 3.0 },
        { "ceiling", ceilingHit, 90.0f, 10.0 },
    };

    for (auto mode : { Mode::OptoUnitA, Mode::CompUnitB })
    {
        for (const auto& c : cases)
        {
            const auto hit = c.signal();
            const auto [atEnd, atCheckpoint] = reductionAtEndAndAfter (hit, c.checkpointS, c.crush, mode);
            const auto retained = atEnd > 1.0e-6f ? 100.0 * atCheckpoint / atEnd : 0.0;

            std::printf ("%-11s %-8s %6.0f %8.1f %10.2f %10.2f %7.1f%%\n",
                         modeName (mode), c.name, c.crush, hit.size() / kSampleRate,
                         atEnd, atCheckpoint, retained);

            if (! outdir.empty())
            {
                DspCore::Params p; p.crushPercent = c.crush; p.mode = mode;
                const auto dry = withSilence (hit, c.checkpointS);
                const auto wet = renderMono (dry, p);

                const auto prefix = outdir + "/" + modeName (mode) + "_" + c.name;
                writeWav (prefix + "_dry.wav", { dry }, kSampleRate);
                writeWav (prefix + "_wet.wav", { wet }, kSampleRate);
            }
        }
    }

    std::printf ("\n\"ceiling\" is read as retained %% (checkpoint / end), not raw dB, because\n"
                 "Stressed's fixed 10:1 ratio starts from a deeper reduction than Tele's\n"
                 "fixed 3:1 at the same crush -- comparing raw dB left over would mostly\n"
                 "re-measure that ratio gap rather than the release timing this is about.\n"
                 "Stressed's retained %% should still come out higher: its ~20 s release\n"
                 "ceiling reaches further than Tele's ~15 s one for the same long, heavy hit.\n");

    // testQuietSignalIsLeftAlone(): well under Crush 0's threshold, a quiet
    // tone should survive essentially untouched.
    std::printf ("\nquiet signal, Crush 0 (should pass through essentially unchanged):\n");

    for (auto mode : { Mode::OptoUnitA, Mode::CompUnitB })
    {
        DspCore::Params p; p.crushPercent = 0.0f; p.mode = mode;
        const auto dry = sine (1000.0, 1.0, 0.05);
        const auto wet = renderMono (dry, p);

        double worst = 0.0;
        for (size_t i = 4000; i < dry.size(); ++i)
            worst = std::max (worst, (double) std::abs (dry[i] - wet[i]));

        std::printf ("  %-11s worst sample difference %.5f\n", modeName (mode), worst);
    }

    // Stereo Link: same L/R pair testStereoLink() uses (R proportionally
    // quieter than L), linked vs. unlinked, both modes -- linked should keep
    // R's RMS exactly proportional to L's; unlinked should not.
    std::printf ("\nstereo Link, R held at 1/4 of L's level:\n");
    std::printf ("%-11s %-6s %10s\n", "mode", "link", "R/L ratio");

    const auto l = sine (1000.0, 1.5, 0.8);
    std::vector<float> r (l.size());
    for (size_t i = 0; i < l.size(); ++i) r[i] = l[i] * 0.25f;

    for (auto mode : { Mode::OptoUnitA, Mode::CompUnitB })
    {
        for (bool link : { false, true })
        {
            DspCore::Params p; p.crushPercent = 70.0f; p.mode = mode; p.link = link;
            const auto out = render ({ l, r }, p);

            const auto half = l.size() / 2;
            const auto ratio = rms (std::vector<float> (out[1].begin() + (long) half, out[1].end()))
                              / rms (std::vector<float> (out[0].begin() + (long) half, out[0].end()));

            std::printf ("%-11s %-6s %10.3f\n", modeName (mode), link ? "on" : "off", ratio);

            if (! outdir.empty())
            {
                const auto prefix = outdir + "/" + modeName (mode) + "_link_" + (link ? "on" : "off");
                writeWav (prefix + ".wav", out, kSampleRate);
            }
        }
    }

    if (! outdir.empty())
        std::printf ("\nwrote dry/wet renders to %s/\n", outdir.c_str());
}

//==============================================================================
int gen (const std::string& which, const std::string& outPath, double freq, double seconds, double amp)
{
    std::vector<float> signal;

    if      (which == "short")   signal = shortHit();
    else if (which == "long")    signal = longHit();
    else if (which == "light")   signal = lightHit();
    else if (which == "ceiling") signal = ceilingHit();
    else if (which == "sine")    signal = sine (freq, seconds, amp);
    else
    {
        std::printf ("unknown signal '%s' -- expected short, long, light, ceiling or sine\n", which.c_str());
        return 1;
    }

    if (! writeWav (outPath, { signal }, kSampleRate))
    {
        std::printf ("could not write %s\n", outPath.c_str());
        return 1;
    }

    std::printf ("wrote %s (%.2f s, %.1f dBFS peak)\n", outPath.c_str(),
                 signal.size() / kSampleRate, dbfs (peak (signal)));
    return 0;
}

int renderCommand (const std::string& inPath, const std::string& outPath, const DspCore::Params& params)
{
    std::vector<std::vector<float>> dry;
    auto rate = kSampleRate;

    if (! inPath.empty())
    {
        if (! readWav (inPath, dry, rate))
        {
            std::printf ("could not read %s\n", inPath.c_str());
            return 1;
        }
    }
    else
    {
        // No source given: a short/long/light composite at a matched level,
        // so a bare `measure render --out x.wav` still produces something
        // directly comparable to the release report's own cases.
        auto composite = shortHit();
        const std::vector<float> gap ((size_t) (0.5 * kSampleRate), 0.0f);
        for (auto sig : { longHit(), lightHit() })
        {
            composite.insert (composite.end(), gap.begin(), gap.end());
            composite.insert (composite.end(), sig.begin(), sig.end());
        }
        dry = { composite };
        std::printf ("no --in given: using the built-in short/long/light composite\n");
    }

    if (dry.size() > 2)
    {
        std::printf ("NOTE: %zu channels in, DspCore handles at most 2 -- using the first 2.\n", dry.size());
        dry.resize (2);
    }

    const auto wet = render (dry, params, rate);

    if (! writeWav (outPath, wet, rate))
    {
        std::printf ("could not write %s\n", outPath.c_str());
        return 1;
    }

    std::printf ("Mode %s, Crush %.0f, Level %+.1f dB, Link %s, Color %s, %.0f Hz, %zu ch\n\n",
                 modeName (params.mode), params.crushPercent, params.levelDb,
                 params.link ? "on" : "off", params.color ? "on" : "off", rate, dry.size());

    for (size_t c = 0; c < dry.size(); ++c)
        std::printf ("ch %zu   in %6.1f dBFS RMS / %6.1f dBFS peak   out %6.1f dBFS RMS / %6.1f dBFS peak\n",
                     c, dbfs (rms (dry[c])), dbfs (peak (dry[c])), dbfs (rms (wet[c])), dbfs (peak (wet[c])));

    std::printf ("\nwrote %s\n", outPath.c_str());
    return 0;
}

} // namespace

//==============================================================================
int main (int argc, char** argv)
{
    const std::string command = argc > 1 ? argv[1] : "release";

    if (command == "release")
    {
        std::string outdir = "opto_measurements";

        for (int i = 2; i + 1 < argc; ++i)
            if (std::string (argv[i]) == "--outdir")
                outdir = argv[i + 1];

        printReleaseReport (outdir);
        return 0;
    }

    if (command == "gen")
    {
        if (argc < 3)
        {
            std::printf ("usage: measure gen <short|long|light|ceiling|sine> --out file.wav "
                         "[--freq hz] [--seconds s] [--amp a]\n");
            return 1;
        }

        std::string outPath;
        double freq = 1000.0, seconds = 1.0, amp = 0.5;

        for (int i = 3; i + 1 < argc; ++i)
        {
            const std::string a = argv[i];
            if      (a == "--out")     outPath = argv[i + 1];
            else if (a == "--freq")    freq    = std::atof (argv[i + 1]);
            else if (a == "--seconds") seconds = std::atof (argv[i + 1]);
            else if (a == "--amp")     amp     = std::atof (argv[i + 1]);
        }

        if (outPath.empty())
        {
            std::printf ("--out is required\n");
            return 1;
        }

        return gen (argv[2], outPath, freq, seconds, amp);
    }

    if (command == "render")
    {
        std::string inPath, outPath;
        DspCore::Params params;

        for (int i = 2; i + 1 < argc; ++i)
        {
            const std::string a = argv[i];
            const auto* next = argv[i + 1];

            if      (a == "--in")    inPath  = next;
            else if (a == "--out")   outPath = next;
            else if (a == "--mode")  { Mode m = params.mode; if (parseMode (next, m)) params.mode = m; }
            else if (a == "--crush") params.crushPercent = (float) std::atof (next);
            else if (a == "--level") params.levelDb      = (float) std::atof (next);
            else if (a == "--link")  params.link  = std::atoi (next) != 0;
            else if (a == "--color") params.color = std::atoi (next) != 0;
        }

        if (outPath.empty())
        {
            std::printf ("usage: measure render [--in in.wav] --out out.wav [--mode tele|stressed]\n"
                         "                       [--crush pct] [--level db] [--link 0|1] [--color 0|1]\n");
            return 1;
        }

        return renderCommand (inPath, outPath, params);
    }

    std::printf ("usage: measure [release [--outdir dir]\n"
                 "               | gen <short|long|light|ceiling|sine> --out file.wav [--freq hz] [--seconds s] [--amp a]\n"
                 "               | render [--in in.wav] --out out.wav [--mode tele|stressed]\n"
                 "                        [--crush pct] [--level db] [--link 0|1] [--color 0|1]]\n");
    return 1;
}
