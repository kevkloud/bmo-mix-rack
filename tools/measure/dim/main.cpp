/*
    Offline measurement harness for BMO Dimension.

    Links the DSP core directly -- no plugin host, no GUI, no JUCE -- same
    shape as tools/measure/eq, /sat and /opto. Four modes, each of which was
    written to answer a question that had already been got wrong once.

        measure_dim source <file.wav>
        measure_dim pass   <file.wav>
        measure_dim corr   <file.wav> [--window ms] [--gate dbfs]
        measure_dim comb   <reference.wav> <test.wav> [--lag samples]

    ------------------------------------------------------------------------
    `source` -- what is actually in the file you are about to test with

    Reports mid/side content, peak, RMS, crest, and the share of samples where
    L and R are bit-identical.

    Written because the 2026-09-09 listening pass nearly ran section 2 of the
    checklist on a file that could not exercise it. `songb NOT SATURATED.wav`
    looks like a stereo vocal and is a MONO one in a stereo container: side
    peak 0.0000305, exactly one 16-bit LSB, with 55.5 % of samples identical.
    ASYMMETRY is a shear -- `mid += a * side` -- so with no side content it
    measured -102 dBFS, which is the LSB noise and not the control working.
    **Run this on any source before drawing a conclusion from it.**

    ------------------------------------------------------------------------
    `pass` -- the meter pass, offline

    Runs testing-notes/dim-meter-pass.md sections 02, 04 and the measurable
    half of 3 and 4, through the real DSP at a real block size:

      - the null at Init, which must be bit-exact
      - the mono-sum null with WIDTH/DIFFUSE/DETUNE/SHUFFLE all up
      - ROTATE and ASYM failing to null, which they must
      - correlation across WIDTH and CENTS, level-gated
      - the side-envelope depth table on tones, against the published figures
      - DETUNE re-engaged over silence, which used to burst 0.90 out of nothing
      - DETUNE switched over a tone -- out mid-beat, which used to step 0.49
        in one sample (32x the tone's own largest move), back in from fully
        off, which used to step 0.18 off a cleared buffer, and back in
        mid-fade. None of those was ever heard; all measured.
      - headroom: SHUFFLE 3.0 x WIDTH 200 % on anti-phase 80 Hz
      - WIDTH 0 gating everything upstream of it

    The correlation figures it prints predicted the host bounce closely --
    18.4 % of windows anti-phase offline against 16.1 % measured in Live -- so
    this is a usable stand-in when a DAW pass is not available. It is not a
    stand-in for the ear: 16.1 % anti-phase sits in the meter pass's "do not
    want to see" column and the module was cleared by ear anyway.

    ------------------------------------------------------------------------
    `corr` -- correlation of an existing render

    For bounces that came out of a DAW. `r = (M^2 - S^2) / (M^2 + S^2)`, per
    window, reported as min/median/mean/max plus the share below zero.

    **Gate on level or the number is meaningless.** Ungated, the dry mono Song B
    file read min r = -0.2583, which looked like real side content and was the
    LSB noise in the gaps between phrases. Gated at -80 dBFS it reads 0.9999
    with a swing of 0.0017, which is the truth.

    ------------------------------------------------------------------------
    `comb` -- per-frame band deviation, which is how you find a MOVING comb

    The one that earned its keep. Section 05 predicts Host stock chorus A will
    comb in mono and Dimension will not, and a time-averaged spectrum of the
    two mono sums showed **no notches at all** -- which the meter pass says
    would invalidate the whole comparison.

    Wrong instrument, not a wrong premise. A modulated chorus sweeps its
    notches, so they average away to nothing over 20 seconds. Measured per
    85 ms frame instead, Host stock chorus A's mono sum wanders with a standard
    deviation of 2.53 dB and individual frames 19.78 dB down, against 0.78 dB
    and -4.16 dB for Vocal doubler A and exactly zero for Dimension.

    **A long-term average will not find a moving comb. Measure per frame.**
*/

#include "modules/dim/dsp/DspCore.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace
{

using bmo::dim::DspCore;
using Buf = std::vector<float>;

constexpr double kPi = 3.14159265358979323846;

double db (double x) { return x > 1e-30 ? 20.0 * std::log10 (x) : -999.0; }

struct Wav
{
    Buf L, R;
    int sampleRate = 0, bits = 0, channels = 0;
    bool ok = false;
};

/** 16/24/32-int and 32-float, plain or WAVE_FORMAT_EXTENSIBLE, which is
    everything Live bounces. Any other format is refused rather than guessed
    at: a wrong decode prints plausible numbers, not an error. */
Wav readWav (const std::string& path)
{
    Wav w;
    FILE* f = std::fopen (path.c_str(), "rb");
    if (f == nullptr) { std::printf ("cannot open %s\n", path.c_str()); return w; }

    std::fseek (f, 0, SEEK_END);
    const long len = std::ftell (f);
    std::fseek (f, 0, SEEK_SET);
    std::vector<uint8_t> b ((size_t) std::max (len, 0L));
    const bool read = ! b.empty() && std::fread (b.data(), 1, b.size(), f) == b.size();
    std::fclose (f);

    if (! read || b.size() < 12 || std::memcmp (b.data(), "RIFF", 4) != 0
        || std::memcmp (b.data() + 8, "WAVE", 4) != 0)
    {
        std::printf ("not a RIFF/WAVE file: %s\n", path.c_str());
        return w;
    }

    size_t p = 12, dataOffset = 0, dataLength = 0;
    uint16_t formatTag = 1;

    while (p + 8 <= b.size())
    {
        char id[5] = {};
        std::memcpy (id, &b[p], 4);
        uint32_t size;
        std::memcpy (&size, &b[p + 4], 4);
        const size_t body = p + 8;

        if (std::strcmp (id, "fmt ") == 0 && body + 16 <= b.size())
        {
            uint16_t ch, bits;
            uint32_t sr;
            std::memcpy (&formatTag, &b[body], 2);
            std::memcpy (&ch, &b[body + 2], 2);
            std::memcpy (&sr, &b[body + 4], 4);
            std::memcpy (&bits, &b[body + 14], 2);
            w.channels = ch; w.sampleRate = (int) sr; w.bits = bits;

            // WAVE_FORMAT_EXTENSIBLE carries the real format in the first two
            // bytes of its SubFormat GUID, 24 bytes into the chunk. Writers
            // use it above 16-bit, and reading only the outer tag decoded a
            // 32-bit float file as integers -- plausible nonsense, no error.
            if (formatTag == 0xFFFE && size >= 26 && body + 26 <= b.size())
                std::memcpy (&formatTag, &b[body + 24], 2);
        }
        else if (std::strcmp (id, "data") == 0)
        {
            dataOffset = body; dataLength = size;
        }

        if (size == 0) break;
        p = body + size + (size & 1);
    }

    if (dataOffset == 0 || w.channels < 1) return w;
    if (dataOffset + dataLength > b.size()) dataLength = b.size() - dataOffset;

    if (formatTag != 1 && formatTag != 3)
    {
        std::printf ("unsupported WAV format 0x%04x (PCM or IEEE float only): %s\n",
                     (unsigned) formatTag, path.c_str());
        return w;
    }

    const int bytes = w.bits / 8;
    if (bytes < 2 || bytes > 4 || (formatTag == 3 && w.bits != 32))
    {
        std::printf ("unsupported bit depth %d%s\n", w.bits, formatTag == 3 ? " float" : "");
        return w;
    }

    const size_t frames = dataLength / (size_t) (bytes * w.channels);
    w.L.resize (frames); w.R.resize (frames);

    for (size_t i = 0; i < frames; ++i)
        for (int c = 0; c < 2; ++c)
        {
            const size_t o = dataOffset
                           + (i * (size_t) w.channels + (size_t) std::min (c, w.channels - 1))
                             * (size_t) bytes;
            float v = 0.0f;

            if (w.bits == 16)      { int16_t x; std::memcpy (&x, &b[o], 2); v = x / 32768.0f; }
            else if (w.bits == 24) { int32_t x = b[o] | (b[o+1] << 8) | (b[o+2] << 16);
                                     if ((x & 0x800000) != 0) x -= 0x1000000;
                                     v = x / 8388608.0f; }
            else                   { if (formatTag == 3) std::memcpy (&v, &b[o], 4);
                                     else { int32_t x; std::memcpy (&x, &b[o], 4);
                                            v = x / 2147483648.0f; } }

            (c == 0 ? w.L : w.R)[i] = v;
        }

    w.ok = true;
    return w;
}

Buf monoOf (const Wav& w)
{
    Buf m (w.L.size());
    for (size_t i = 0; i < m.size(); ++i) m[i] = 0.5f * (w.L[i] + w.R[i]);
    return m;
}

double rmsOf (const Buf& a)
{
    double s = 0;
    for (float v : a) s += (double) v * v;
    return a.empty() ? 0.0 : std::sqrt (s / a.size());
}

double peakOf (const Buf& a)
{
    double p = 0;
    for (float v : a) p = std::max (p, (double) std::fabs (v));
    return p;
}

/** Process a copy through the real DSP, in host-sized blocks. */
void run (const Buf& inL, const Buf& inR, const DspCore::Params& p, int sampleRate,
          Buf& outL, Buf& outR, int blockSize = 512)
{
    outL = inL; outR = inR;
    DspCore d;
    d.prepare ((double) sampleRate, 0, 0);
    d.setParams (p);

    for (size_t i = 0; i < outL.size(); i += (size_t) blockSize)
    {
        const int n = (int) std::min ((size_t) blockSize, outL.size() - i);
        float* channels[2] = { outL.data() + i, outR.data() + i };
        d.process (channels, 2, n);
    }
}

void residual (const Buf& a, const Buf& b, double& peakDb, double& rmsDb)
{
    double pk = 0, ss = 0;
    const size_t n = std::min (a.size(), b.size());
    for (size_t i = 0; i < n; ++i)
    {
        const double d = (double) a[i] - b[i];
        pk = std::max (pk, std::fabs (d));
        ss += d * d;
    }
    peakDb = db (pk);
    rmsDb  = db (n != 0 ? std::sqrt (ss / n) : 0.0);
}

struct Corr { double min = 2, max = -2, mean = 0, median = 0, belowZeroPct = 0; int windows = 0; };

/** Level-gated, because ungated this reads the noise between phrases. */
Corr correlation (const Buf& L, const Buf& R, int sampleRate,
                  double windowMs = 50.0, double gateDb = -80.0)
{
    const size_t w = (size_t) (windowMs * 0.001 * sampleRate);
    const double gate = std::pow (10.0, gateDb / 20.0);
    std::vector<double> rs;
    double sum = 0;
    int below = 0;

    for (size_t st = 0; st + w <= L.size(); st += w)
    {
        double mm = 0, ss = 0;
        for (size_t i = st; i < st + w; ++i)
        {
            const double m = 0.5 * ((double) L[i] + R[i]);
            const double s = 0.5 * ((double) L[i] - R[i]);
            mm += m * m; ss += s * s;
        }
        if (std::sqrt (mm / w) < gate) continue;

        const double r = (mm - ss) / (mm + ss + 1e-30);
        rs.push_back (r); sum += r;
        if (r < 0) ++below;
    }

    Corr c;
    if (rs.empty()) return c;
    std::sort (rs.begin(), rs.end());
    c.min = rs.front(); c.max = rs.back();
    c.mean = sum / rs.size(); c.median = rs[rs.size() / 2];
    c.belowZeroPct = 100.0 * below / rs.size();
    c.windows = (int) rs.size();
    return c;
}

void fft (std::vector<std::complex<double>>& x)
{
    const size_t n = x.size();
    if (n <= 1) return;

    for (size_t i = 1, j = 0; i < n; ++i)
    {
        size_t bit = n >> 1;
        for (; (j & bit) != 0; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) std::swap (x[i], x[j]);
    }

    for (size_t l = 2; l <= n; l <<= 1)
    {
        const double ang = -2.0 * kPi / (double) l;
        const std::complex<double> wl (std::cos (ang), std::sin (ang));
        for (size_t i = 0; i < n; i += l)
        {
            std::complex<double> w (1.0, 0.0);
            for (size_t k = 0; k < l / 2; ++k)
            {
                const auto u = x[i + k], t = w * x[i + k + l / 2];
                x[i + k] = u + t;
                x[i + k + l / 2] = u - t;
                w *= wl;
            }
        }
    }
}

//==============================================================================
int modeSource (const std::string& path)
{
    const Wav w = readWav (path);
    if (! w.ok) return 1;

    double midPeak = 0, sidePeak = 0, midSq = 0, sideSq = 0;
    size_t identical = 0;

    for (size_t i = 0; i < w.L.size(); ++i)
    {
        const double m = 0.5 * ((double) w.L[i] + w.R[i]);
        const double s = 0.5 * ((double) w.L[i] - w.R[i]);
        if (w.L[i] == w.R[i]) ++identical;
        midPeak = std::max (midPeak, std::fabs (m));
        sidePeak = std::max (sidePeak, std::fabs (s));
        midSq += m * m; sideSq += s * s;
    }

    const size_t n = std::max<size_t> (w.L.size(), 1);
    std::printf ("%s\n", path.c_str());
    std::printf ("  %d Hz, %d-bit, %d ch, %.2f s\n",
                 w.sampleRate, w.bits, w.channels, w.L.size() / (double) w.sampleRate);
    std::printf ("  mid    peak %10.7f  %8.2f dBFS   rms %8.2f dBFS\n",
                 midPeak, db (midPeak), db (std::sqrt (midSq / n)));
    std::printf ("  side   peak %10.7f  %8.2f dBFS   rms %8.2f dBFS\n",
                 sidePeak, db (sidePeak), db (std::sqrt (sideSq / n)));
    std::printf ("  L == R exactly on %zu of %zu samples (%.2f %%)\n",
                 identical, w.L.size(), 100.0 * identical / n);

    const bool effectivelyMono = db (sidePeak) < -80.0;
    std::printf ("  -> %s\n", effectivelyMono
        ? "EFFECTIVELY MONO. Fine for the throb test; CANNOT exercise ASYMMETRY."
        : "has real side content; usable for the asymmetry and rotation tests.");
    return 0;
}

int modePass (const std::string& path)
{
    const Wav dry = readWav (path);
    if (! dry.ok) return 1;

    const int sr = dry.sampleRate;
    const DspCore::Params init;

    std::printf ("source: %s\n  %d Hz, %.2f s\n\n", path.c_str(), sr,
                 dry.L.size() / (double) sr);

    std::printf ("== 02 n1  the null at Init ==\n");
    {
        Buf oL, oR; run (dry.L, dry.R, init, sr, oL, oR);
        double pl, rl, pr, rr;
        residual (oL, dry.L, pl, rl);
        residual (oR, dry.R, pr, rr);
        std::printf ("   L peak %7.2f dBFS   R peak %7.2f dBFS  -> %s\n\n", pl, pr,
                     (pl < -120 && pr < -120) ? "bit-exact, Init is a wire" : "RESIDUAL");
    }

    std::printf ("== 02 n2  the mono null, everything up ==\n");
    {
        DspCore::Params p = init;
        p.widthPercent = 200.0f; p.diffusePercent = 100.0f;
        p.detuneOn = true; p.shuffleAmount = 3.0f;
        Buf oL, oR; run (dry.L, dry.R, p, sr, oL, oR);

        Buf dm (dry.L.size()), wm (dry.L.size());
        for (size_t i = 0; i < dm.size(); ++i)
        {
            dm[i] = 0.5f * (dry.L[i] + dry.R[i]);
            wm[i] = 0.5f * (oL[i] + oR[i]);
        }
        double pk, rm; residual (wm, dm, pk, rm);
        std::printf ("   residual %7.2f dBFS   level delta %+0.4f dB  -> %s\n\n",
                     pk, db (rmsOf (wm)) - db (rmsOf (dm)),
                     pk < -120 ? "nulls, the mid path is a wire" : "LEAK INTO MID");
    }

    std::printf ("== 02 n3  ROTATE and ASYM must FAIL to null ==\n");
    for (int which = 0; which < 2; ++which)
    {
        DspCore::Params p = init;
        if (which == 0) p.rotationDegrees = 30.0f; else p.asymmetryPercent = 50.0f;
        Buf oL, oR; run (dry.L, dry.R, p, sr, oL, oR);
        Buf dm (dry.L.size()), wm (dry.L.size());
        for (size_t i = 0; i < dm.size(); ++i)
        {
            dm[i] = 0.5f * (dry.L[i] + dry.R[i]);
            wm[i] = 0.5f * (oL[i] + oR[i]);
        }
        double pk, rm; residual (wm, dm, pk, rm);

        // -102 dBFS "breaks the null" on a mono source, and it is the 16-bit
        // LSB rather than the control doing anything. ASYM is a shear on the
        // side signal, so with no side content there is nothing for it to
        // shear. Anything this quiet gets called out rather than passed.
        const char* verdict = pk < -120.0 ? "NULLS -- wrong, unless the source has no side content"
                            : pk < -60.0  ? "barely moves: on a mono source this is LSB noise,\n"
                                            "                                    not the control. Run `source` on it."
                                          : "breaks the null, correct";
        std::printf ("   %-11s residual %7.2f dBFS  -> %s\n",
                     which == 0 ? "ROTATE +30" : "ASYM +50%", pk, verdict);
    }
    std::printf ("\n");

    std::printf ("== 04  correlation, DETUNE on, 50 ms windows gated at -80 dBFS ==\n");
    std::printf ("   %-22s %8s %8s %8s %8s %7s\n",
                 "setting", "min r", "median", "mean", "max", "r<0 %");
    {
        struct Row { const char* label; bool on; float cents; float width; };
        const Row rows[] = {
            { "dry (DETUNE out)",     false, 10.0f, 130.0f },
            { "CENTS 10  W 70",       true,  10.0f,  70.0f },
            { "CENTS 10  W100 (dflt)",true,  10.0f, 100.0f },
            { "CENTS 10  W130",       true,  10.0f, 130.0f },
            { "CENTS 10  W200",       true,  10.0f, 200.0f },
            { "CENTS 5   W130",       true,   5.0f, 130.0f },
            { "CENTS 25  W130",       true,  25.0f, 130.0f },
        };

        for (const auto& r : rows)
        {
            DspCore::Params p = init;
            p.detuneOn = r.on; p.detuneCents = r.cents; p.widthPercent = r.width;
            Buf oL, oR; run (dry.L, dry.R, p, sr, oL, oR);
            const Corr c = correlation (oL, oR, sr);
            std::printf ("   %-22s %8.4f %8.4f %8.4f %8.4f %6.1f%%\n",
                         r.label, c.min, c.median, c.mean, c.max, c.belowZeroPct);
        }
    }
    std::printf ("\n");

    std::printf ("== 04  side-envelope depth on tones, against the published table ==\n");
    std::printf ("   %-22s %10s %12s\n", "case", "measured", "published");
    {
        struct T { const char* label; double freq; float cents; const char* published; };
        const T ts[] = {
            { "1 kHz, CENTS 5",   1000.0,  5.0f, "25.2 dB" },
            { "1 kHz, CENTS 10",  1000.0, 10.0f, "19.6 dB" },
            { "1 kHz, CENTS 25",  1000.0, 25.0f, "11.5 dB" },
            { "110 Hz, CENTS 10",  110.0, 10.0f, "34.6 dB" },
        };

        for (const auto& t : ts)
        {
            const size_t n = (size_t) (sr * 8);
            Buf tl (n), tr (n);
            for (size_t i = 0; i < n; ++i)
            {
                const float v = 0.25f * (float) std::sin (2.0 * kPi * t.freq * i / sr);
                tl[i] = v; tr[i] = v;
            }
            DspCore::Params p = init;
            p.detuneOn = true; p.detuneCents = t.cents;
            Buf oL, oR; run (tl, tr, p, sr, oL, oR);

            const size_t skip = (size_t) (sr * 2), w = (size_t) (0.010 * sr);
            double mn = 1e9, mx = 0;
            for (size_t st = skip; st + w <= n; st += w)
            {
                double ss = 0;
                for (size_t i = st; i < st + w; ++i)
                {
                    const double s = 0.5 * ((double) oL[i] - oR[i]);
                    ss += s * s;
                }
                const double v = std::sqrt (ss / w);
                mn = std::min (mn, v); mx = std::max (mx, v);
            }
            std::printf ("   %-22s %9.1f dB %12s\n", t.label, db (mx) - db (mn), t.published);
        }
    }
    std::printf ("\n");

    std::printf ("== 3  DETUNE re-engaged over silence (used to burst 0.90) ==\n");
    {
        DspCore d;
        d.prepare ((double) sr, 0, 0);
        DspCore::Params p = init;
        p.detuneOn = true; p.widthPercent = 130.0f;
        d.setParams (p);

        const size_t n = (size_t) sr;
        Buf a (n), b (n);
        for (size_t i = 0; i < n; ++i)
        {
            const float v = 0.5f * (float) std::sin (2.0 * kPi * 440.0 * i / sr);
            a[i] = v; b[i] = v;
        }
        for (size_t i = 0; i < n; i += 512)
        {
            const int m = (int) std::min ((size_t) 512, n - i);
            float* c[2] = { a.data() + i, b.data() + i };
            d.process (c, 2, m);
        }

        p.detuneOn = false; d.setParams (p);
        Buf s1 (n, 0.0f), s2 (n, 0.0f);
        for (size_t i = 0; i < n; i += 512)
        {
            const int m = (int) std::min ((size_t) 512, n - i);
            float* c[2] = { s1.data() + i, s2.data() + i };
            d.process (c, 2, m);
        }

        p.detuneOn = true; d.setParams (p);
        Buf z1 (n, 0.0f), z2 (n, 0.0f);
        for (size_t i = 0; i < n; i += 512)
        {
            const int m = (int) std::min ((size_t) 512, n - i);
            float* c[2] = { z1.data() + i, z2.data() + i };
            d.process (c, 2, m);
        }
        std::printf ("   peak out of silence: L %.8f  R %.8f  -> %s\n\n",
                     peakOf (z1), peakOf (z2),
                     (peakOf (z1) < 1e-6 && peakOf (z2) < 1e-6) ? "silent, buffers cleared"
                                                                : "BURST");
    }

    std::printf ("== 3  DETUNE switched over a sustained tone ==\n");
    {
        // The check above is over silence, where a step has nothing to step
        // from. These switch over a tone, each at its worst sample, and
        // compare the biggest one-sample move of the side signal against the
        // biggest the tone makes by itself. A 220 Hz tone moves every sample,
        // so "moves at all" is the wrong question; "moves more than the tone
        // does" is the click.
        const size_t n = (size_t) sr, settle = (size_t) (sr / 10), tail = (size_t) (sr / 5);
        Buf tone (n);
        for (size_t i = 0; i < n; ++i)
            tone[i] = 0.5f * (float) std::sin (2.0 * kPi * 220.0 * i / sr);

        DspCore::Params on = init;
        on.detuneOn = true;
        DspCore::Params off = on;
        off.detuneOn = false;

        // Detune on, switched out at `offAt` and back in at `onAt` (n: never),
        // in host-sized blocks split so each switch lands on its own sample.
        auto sideOf = [&] (size_t offAt, size_t onAt)
        {
            DspCore d;
            d.prepare ((double) sr, 0, 0);
            Buf l = tone, r = tone;
            for (size_t i = 0; i < n; )
            {
                size_t end = std::min (i + 512, n);
                for (size_t cut : { offAt, onAt })
                    if (cut > i && cut < end) end = cut;
                d.setParams (i >= offAt && i < onAt ? off : on);
                float* c[2] = { l.data() + i, r.data() + i };
                d.process (c, 2, (int) (end - i));
                i = end;
            }
            Buf s (n);
            for (size_t i = 0; i < n; ++i) s[i] = 0.5f * (l[i] - r[i]);
            return s;
        };

        auto worstStep = [] (const Buf& s, size_t from)
        {
            double w = 0;
            for (size_t i = std::max<size_t> (from, 1); i < s.size(); ++i)
                w = std::max (w, (double) std::fabs (s[i] - s[i - 1]));
            return w;
        };

        const Buf steady = sideOf (n, n);
        const double calm = worstStep (steady, settle);

        // Off at the peak of the beat, where the voices' difference -- the
        // whole side signal on a mono source -- is largest.
        size_t beatPeak = settle;
        for (size_t i = settle; i + tail < n; ++i)
            if (std::fabs (steady[i]) > std::fabs (steady[beatPeak])) beatPeak = i;

        // On from fully off at a peak of the tone: the first sample a
        // restarted voice has to reach is the worst one for it to meet.
        size_t tonePeak = settle + tail;
        for (size_t i = tonePeak; i < settle + tail + (size_t) (sr / 220); ++i)
            if (std::fabs (tone[i]) > std::fabs (tone[tonePeak])) tonePeak = i;

        struct Case { const char* label; size_t offAt, onAt, from; };
        const Case cases[] = {
            { "off, at the beat peak",      beatPeak, n,                               beatPeak },
            { "on, from fully off",         settle,   tonePeak,                        tonePeak },
            { "on again 5 ms into fade-out", beatPeak, beatPeak + (size_t) (sr / 200), beatPeak },
        };

        for (const auto& c : cases)
        {
            const double step = worstStep (sideOf (c.offAt, c.onAt), c.from);
            std::printf ("   %-28s largest step %.5f = %5.2fx steady  -> %s\n",
                         c.label, step, step / calm, step < 1.5 * calm ? "no click" : "CLICK");
        }
        std::printf ("   (steady: the tone's own largest move, %.5f)\n\n", calm);
    }

    std::printf ("== 4  headroom, SHUFFLE 3.0 x WIDTH 200%% on anti-phase 80 Hz ==\n");
    {
        const size_t n = (size_t) (sr * 4);
        Buf a (n), b (n);
        for (size_t i = 0; i < n; ++i)
        {
            const float v = 0.5f * (float) std::sin (2.0 * kPi * 80.0 * i / sr);
            a[i] = v; b[i] = -v;
        }
        DspCore::Params p = init;
        p.shuffleAmount = 3.0f; p.widthPercent = 200.0f;
        Buf oL, oR; run (a, b, p, sr, oL, oR);
        std::printf ("   0.5 peak in -> %.3f out (%+.2f dB). There is no output trim.\n\n",
                     peakOf (oL), db (peakOf (oL) / 0.5));
    }

    std::printf ("== 4  WIDTH 0 gates everything upstream ==\n");
    {
        DspCore::Params p = init;
        p.widthPercent = 0.0f; p.detuneOn = true; p.diffusePercent = 100.0f;
        Buf oL, oR; run (dry.L, dry.R, p, sr, oL, oR);
        double side = 0;
        for (size_t i = 0; i < oL.size(); ++i)
            side = std::max (side, std::fabs (0.5 * ((double) oL[i] - oR[i])));
        std::printf ("   peak side with DETUNE on and DIFFUSE 100: %.8f  -> %s\n",
                     side, side < 1e-6 ? "fully gated" : "side survives");
    }
    return 0;
}

int modeCorr (const std::string& path, double windowMs, double gateDb)
{
    const Wav w = readWav (path);
    if (! w.ok) return 1;

    const Corr c = correlation (w.L, w.R, w.sampleRate, windowMs, gateDb);
    std::printf ("%s\n", path.c_str());
    std::printf ("  %d Hz, %.2f s, peak %.2f dBFS, rms %.2f dBFS\n",
                 w.sampleRate, w.L.size() / (double) w.sampleRate,
                 db (peakOf (w.L)), db (rmsOf (monoOf (w))));
    std::printf ("  %.0f ms windows gated at %.0f dBFS, %d counted\n", windowMs, gateDb, c.windows);
    std::printf ("  min %.4f  median %.4f  mean %.4f  max %.4f   below zero %.1f %%\n",
                 c.min, c.median, c.mean, c.max, c.belowZeroPct);
    return 0;
}

int modeComb (const std::string& refPath, const std::string& testPath, long lag)
{
    const Wav a = readWav (refPath), b = readWav (testPath);
    if (! a.ok || ! b.ok) return 1;
    if (a.sampleRate != b.sampleRate)
    {
        std::printf ("sample rates differ (%d vs %d). Bounce both at the same rate --\n"
                     "a resampled comparison cannot null and its numbers mean nothing.\n",
                     a.sampleRate, b.sampleRate);
        return 1;
    }

    const Buf A = monoOf (a), B = monoOf (b);
    const size_t N = 4096;
    std::vector<double> win (N);
    for (size_t i = 0; i < N; ++i) win[i] = 0.5 * (1 - std::cos (2 * kPi * i / (N - 1)));

    std::vector<double> devs;
    for (size_t st = 0; st + N <= A.size(); st += N)
    {
        const long j = (long) st + lag;
        if (j < 0 || (size_t) j + N > B.size()) continue;

        double e = 0;
        for (size_t i = 0; i < N; ++i) e += (double) A[st + i] * A[st + i];
        if (std::sqrt (e / N) < 3e-4) continue;

        std::vector<std::complex<double>> xa (N), xb (N);
        for (size_t i = 0; i < N; ++i)
        {
            xa[i] = A[st + i] * win[i];
            xb[i] = B[(size_t) j + i] * win[i];
        }
        fft (xa); fft (xb);

        for (double fc = 120.0; fc < 12000.0; fc *= std::pow (2.0, 1.0 / 6.0))
        {
            const double lo = fc / std::pow (2.0, 1.0 / 12.0);
            const double hi = fc * std::pow (2.0, 1.0 / 12.0);
            const size_t k0 = (size_t) std::max (1.0, lo * N / a.sampleRate);
            const size_t k1 = (size_t) std::min ((double) (N / 2 - 1), hi * N / a.sampleRate);
            if (k1 <= k0) continue;

            double sa = 0, sb = 0;
            for (size_t k = k0; k <= k1; ++k) { sa += std::norm (xa[k]); sb += std::norm (xb[k]); }
            if (sa < 1e-16) continue;
            devs.push_back (10.0 * std::log10 ((sb + 1e-30) / (sa + 1e-30)));
        }
    }

    if (devs.empty()) { std::printf ("no frames above the gate\n"); return 1; }

    double mean = 0;
    for (double v : devs) mean += v;
    mean /= devs.size();
    double sd = 0;
    for (double v : devs) sd += (v - mean) * (v - mean);
    sd = std::sqrt (sd / devs.size());
    std::sort (devs.begin(), devs.end());

    std::printf ("reference %s\n     test %s\n", refPath.c_str(), testPath.c_str());
    std::printf ("  85 ms frames, 1/6-octave bands, %zu measurements\n", devs.size());
    std::printf ("  mean %+.2f dB   std dev %.2f dB   5th pct %+.2f dB   worst frame %+.2f dB\n",
                 mean, sd, devs[(size_t) (devs.size() * 0.05)], devs.front());
    std::printf ("  -> %s\n",
        sd < 0.25 && devs.front() > -3.0
            ? "no combing"
            : (std::fabs (mean) < 1.0 && sd > 1.0
                 ? "a MOVING comb: notches that wander and average away. A long-term\n"
                   "     average spectrum would have shown nothing here."
                 : "a static deviation: check the mean, not just the spread."));
    return 0;
}

void usage()
{
    std::printf (
        "measure_dim -- offline measurement for BMO Dimension\n\n"
        "  measure_dim source <file.wav>\n"
        "      mid/side content. Run this BEFORE testing with a file: a mono file\n"
        "      cannot exercise ASYMMETRY at all.\n\n"
        "  measure_dim pass <file.wav>\n"
        "      the meter pass offline -- nulls, correlation, the throb table,\n"
        "      the re-engage burst, headroom and WIDTH gating.\n\n"
        "  measure_dim corr <file.wav> [--window ms] [--gate dbfs]\n"
        "      correlation of a DAW bounce. Gate on level or the number lies.\n\n"
        "  measure_dim comb <reference.wav> <test.wav> [--lag samples]\n"
        "      per-frame band deviation, which is how a MOVING comb is found.\n"
        "      A time-averaged spectrum will not see one.\n");
}

} // namespace

int main (int argc, char** argv)
{
    if (argc < 3) { usage(); return 1; }

    const std::string mode = argv[1];
    auto flag = [argc, argv] (const char* name, double fallback)
    {
        for (int i = 2; i + 1 < argc; ++i)
            if (std::strcmp (argv[i], name) == 0) return std::atof (argv[i + 1]);
        return fallback;
    };

    if (mode == "source") return modeSource (argv[2]);
    if (mode == "pass")   return modePass (argv[2]);
    if (mode == "corr")   return modeCorr (argv[2], flag ("--window", 50.0), flag ("--gate", -80.0));
    if (mode == "comb")
    {
        if (argc < 4) { usage(); return 1; }
        return modeComb (argv[2], argv[3], (long) flag ("--lag", 0.0));
    }

    usage();
    return 1;
}
