/*
    BMO Util's DSP, JUCE-free. Each control is checked on its own against
    the arithmetic it claims: a gain of -6 dB halves, pan is a balance law,
    width 0 is mono, width 200 doubles the side, polarity flips, mono sums.
*/

#include "modules/util/dsp/UtilDsp.h"
#include <iostream>
#include <random>
#include <string>
#include <vector>

using namespace bmo::util;

namespace
{
    int failures = 0;

    void check (bool ok, const char* what)
    {
        if (! ok) { std::cerr << "FAIL: " << what << '\n'; ++failures; }
    }

    bool near (float a, float b, float tol = 1.0e-3f) { return std::abs (a - b) <= tol; }

    struct Run
    {
        std::vector<float> l, r;
    };

    /** Runs a constant stereo pair through the DSP long enough for the
        smoothers to settle, and returns the last sample. */
    Run run (float gainDb, float pan, float width, bool phL, bool phR, bool mono,
             float inL, float inR)
    {
        UtilDsp dsp;
        const float v[Index::count] { gainDb, pan, width, phL ? 1.0f : 0.0f, phR ? 1.0f : 0.0f, mono ? 1.0f : 0.0f };
        dsp.setParams (v, Index::count);
        dsp.prepare (48000.0, 512, 2);

        constexpr int n = 48000;
        std::vector<float> l ((size_t) n, inL), r ((size_t) n, inR);
        float* ch[2] { l.data(), r.data() };
        dsp.process (ch, 2, n);

        return { l, r };
    }

    constexpr double kRates[] { 44100.0, 48000.0, 96000.0, 192000.0 };

    /** An instance driven the way core/product/ModuleEngine.h drives it:
        setParams before prepare, then setParams once per host block. */
    struct Rig
    {
        UtilDsp dsp;
        float v[Index::count] { 0.0f, 0.0f, 100.0f, 0.0f, 0.0f, 0.0f };

        explicit Rig (double rate)
        {
            dsp.setParams (v, Index::count);
            dsp.prepare (rate, 512, 2);
        }

        void render (float* l, float* r, int n, int block = 512)
        {
            for (int at = 0; at < n; at += block)
            {
                float* ch[2] { l + at, r + at };
                dsp.setParams (v, Index::count);
                dsp.process (ch, 2, std::min (block, n - at));
            }
        }
    };

    /** Uniform noise at an RMS level in dBFS, the same every run. */
    std::vector<float> noise (int n, double rmsDb, unsigned seed)
    {
        std::mt19937 rng (seed);
        const auto a = (float) (std::sqrt (3.0) * std::pow (10.0, rmsDb / 20.0));
        std::uniform_real_distribution<float> u (-a, a);
        std::vector<float> out ((size_t) n);
        for (auto& s : out)
            s = u (rng);
        return out;
    }

    std::vector<float> sine (int n, double hz, double rate)
    {
        // -18 dBFS RMS.
        const auto a = std::sqrt (2.0) * std::pow (10.0, -18.0 / 20.0);
        std::vector<float> out ((size_t) n);
        for (int i = 0; i < n; ++i)
            out[(size_t) i] = (float) (a * std::sin (2.0 * 3.141592653589793 * hz * i / rate));
        return out;
    }
}

int main()
{
    // Defaults are a wire, bit for bit. Until 2026-10-03 the mid/side matrix
    // rounded at defaults: on uncorrelated -18 dBFS RMS noise 37 % of samples
    // differed from the input, by up to -150.5 dBFS, and with the left at
    // -18 and the right at -100 dBFS RMS the right's error was -69.9 dB
    // relative to the right.
    {
        auto out = run (0.0f, 0.0f, 100.0f, false, false, false, 0.5f, -0.25f);
        check (out.l.back() == 0.5f && out.r.back() == -0.25f, "defaults pass a constant pair unchanged");

        const struct { double l, r; const char* name; } cases[] {
            { -18.0, -18.0, "uncorrelated noise at -18/-18 dBFS RMS" },
            { -18.0, -100.0, "noise at -18 left, -100 dBFS RMS right" },
        };

        for (const auto& c : cases)
        {
            const auto n = 48000;
            const auto inL = noise (n, c.l, 3), inR = noise (n, c.r, 4);
            auto l = inL, r = inR;
            Rig rig (48000.0);
            rig.render (l.data(), r.data(), n);

            int differ = 0;
            for (size_t i = 0; i < (size_t) n; ++i)
                differ += (l[i] != inL[i]) + (r[i] != inR[i]);

            const auto what = std::string ("defaults return ") + c.name + " bit for bit ("
                            + std::to_string (differ) + " of " + std::to_string (2 * n) + " samples differ)";
            check (differ == 0, what.c_str());
        }
    }

    // Width 0 is exactly mono: both channels the same bits, and those the
    // sum's half.
    {
        const auto n = 48000;
        const auto inL = noise (n, -18.0, 5), inR = noise (n, -18.0, 6);
        auto l = inL, r = inR;
        Rig rig (48000.0);
        rig.v[Index::width] = 0.0f;
        rig.dsp.setParams (rig.v, Index::count);
        rig.dsp.reset();
        rig.render (l.data(), r.data(), n);

        int differ = 0;
        for (size_t i = 0; i < (size_t) n; ++i)
            differ += (l[i] != r[i]) + (l[i] != 0.5f * (inL[i] + inR[i]));

        check (differ == 0, "width 0 is exactly mono: L == R == (L + R) / 2, bit for bit");
    }

    // Gain.
    {
        auto out = run (-6.0206f, 0.0f, 100.0f, false, false, false, 1.0f, 1.0f);
        check (near (out.l.back(), 0.5f, 2.0e-3f), "-6 dB halves");
    }

    // Pan: balance law, centre is unity and each side only attenuates the other.
    {
        auto out = run (0.0f, -100.0f, 100.0f, false, false, false, 1.0f, 1.0f);
        check (near (out.l.back(), 1.0f) && near (out.r.back(), 0.0f), "hard left silences the right");

        out = run (0.0f, 50.0f, 100.0f, false, false, false, 1.0f, 1.0f);
        check (near (out.l.back(), 0.5f) && near (out.r.back(), 1.0f), "half right takes half off the left");
    }

    // Width.
    {
        auto out = run (0.0f, 0.0f, 0.0f, false, false, false, 1.0f, 0.0f);
        check (near (out.l.back(), 0.5f) && near (out.r.back(), 0.5f), "width 0 is mono");

        out = run (0.0f, 0.0f, 200.0f, false, false, false, 1.0f, 0.0f);
        // mid 0.5, side 0.5 * 2 = 1.0 -> L 1.5, R -0.5
        check (near (out.l.back(), 1.5f) && near (out.r.back(), -0.5f), "width 200 doubles the side");
    }

    // Polarity.
    {
        auto out = run (0.0f, 0.0f, 100.0f, true, false, false, 0.5f, 0.5f);
        check (near (out.l.back(), -0.5f) && near (out.r.back(), 0.5f), "phase L flips only the left");

        out = run (0.0f, 0.0f, 100.0f, true, true, false, 0.5f, 0.5f);
        check (near (out.l.back(), -0.5f) && near (out.r.back(), -0.5f), "both flip both");
    }

    // Mono.
    {
        auto out = run (0.0f, 0.0f, 100.0f, false, false, true, 1.0f, 0.0f);
        check (near (out.l.back(), 0.5f) && near (out.r.back(), 0.5f), "mono sums to (L+R)/2 on both");
    }

    // A parameter change ramps rather than steps, and the ramp is the one
    // claimed: a 5 ms one-pole that lands exactly. Gain 0 -> -24 dB on DC 1.0
    // at 48 kHz: the 240th sample (5 ms) is 1/e of the way from the target,
    // and after 1 s the output is the target gain exactly.
    {
        UtilDsp dsp;
        float v[Index::count] { 0.0f, 0.0f, 100.0f, 0.0f, 0.0f, 0.0f };
        dsp.setParams (v, Index::count);
        dsp.prepare (48000.0, 512, 2);

        v[Index::gain] = -24.0f;
        dsp.setParams (v, Index::count);

        std::vector<float> l (48000, 1.0f), r (48000, 1.0f);
        float* ch[2] { l.data(), r.data() };
        dsp.process (ch, 2, 48000);

        const auto target = std::pow (10.0f, -24.0f / 20.0f);
        const auto atTau  = target + (1.0f - target) * (float) std::exp (-1.0);

        bool falling = true;
        for (size_t i = 1; i < 240; ++i)
            falling = falling && l[i] < l[i - 1];

        check (falling && near (l[239], atTau, 1.0e-4f), "a gain change is a 5 ms one-pole ramp");
        check (l.back() == target && r.back() == target, "and lands on the target gain exactly");
    }

    // A switch is a fade, not a step: polarity L, polarity R and mono, on a
    // -18 dBFS RMS sine at 100 Hz and 1 kHz, at 48 and 192 kHz. The largest
    // sample-to-sample step stays under 1.5 times the sine's own, and the
    // level has no hole beyond the polarity flip's own zero crossing: a flip
    // is under half level for at most the 5.49 ms (tau ln 3) the sign takes
    // from +0.5 to -0.5, and mono on a hard-left signal moves each channel
    // monotonically between its two settled levels.
    for (const auto rate : { 48000.0, 192000.0 })
        for (const auto hz : { 100.0, 1000.0 })
        {
            const auto n = (int) rate;
            const auto at = (int) (0.5 * rate) / 512 * 512;
            const auto in = sine (n, hz, rate);
            const auto amp = (float) (std::sqrt (2.0) * std::pow (10.0, -18.0 / 20.0));

            float ownStep = 0.0f;
            for (size_t i = 1; i < in.size(); ++i)
                ownStep = std::max (ownStep, std::abs (in[i] - in[i - 1]));

            const auto tag = std::to_string ((int) hz) + " Hz at " + std::to_string ((int) rate) + " Hz: ";

            for (const int which : { (int) Index::phaseL, (int) Index::phaseR, (int) Index::mono })
            {
                const auto isMono = which == Index::mono;
                auto l = in;
                auto r = isMono ? std::vector<float> ((size_t) n, 0.0f) : in;

                Rig rig (rate);
                rig.render (l.data(), r.data(), at);
                rig.v[which] = 1.0f;
                rig.render (l.data() + at, r.data() + at, n - at);

                float biggest = 0.0f;
                for (size_t i = 1; i < (size_t) n; ++i)
                    biggest = std::max ({ biggest, std::abs (l[i] - l[i - 1]), std::abs (r[i] - r[i - 1]) });

                const auto name = which == Index::phaseL ? "phase L" : which == Index::phaseR ? "phase R" : "mono";
                const auto stepWhat = tag + name + " switches with a largest step of "
                                    + std::to_string (biggest / ownStep) + " x the sine's own, under 1.5";
                check (biggest < 1.5f * ownStep, stepWhat.c_str());

                // Level, sample by sample, where the input is not near a zero.
                int first = -1, last = -1;
                bool inside = true;
                for (int i = at; i < n; ++i)
                {
                    const auto x = in[(size_t) i];
                    if (std::abs (x) < 0.1f * amp)
                        continue;

                    if (isMono)
                    {
                        const auto gl = l[(size_t) i] / x, gr = r[(size_t) i] / x;
                        inside = inside && gl >= 0.5f - 1.0e-6f && gl <= 1.0f + 1.0e-6f
                                        && gr >= -1.0e-6f && gr <= 0.5f + 1.0e-6f;
                    }
                    else
                    {
                        const auto& out = which == Index::phaseL ? l : r;
                        if (std::abs (out[(size_t) i] / x) < 0.5f)
                        {
                            if (first < 0) first = i;
                            last = i;
                        }
                    }
                }

                const auto holeMs = first < 0 ? 0.0 : 1000.0 * (last - first + 1) / rate;
                const auto levelWhat = tag + name + " has no level hole beyond its own crossing (under half level for "
                                     + std::to_string (holeMs) + " ms, 5.49 at most)";
                check (inside && holeMs <= 5.5 + 1000.0 / hz * 0.05, levelWhat.c_str());
            }
        }

    // Mono input does not crash and gets gain and polarity.
    {
        UtilDsp dsp;
        const float v[Index::count] { -6.0206f, 0.0f, 100.0f, 1.0f, 0.0f, 0.0f };
        dsp.setParams (v, Index::count);
        dsp.prepare (48000.0, 512, 1);

        std::vector<float> l (48000, 1.0f);
        float* ch[1] { l.data() };
        dsp.process (ch, 1, 48000);
        check (near (l.back(), -0.5f, 2.0e-3f), "mono input gets gain and polarity");
    }

    // Every smoother lands exactly on its target. A one-pole stops short of it
    // once a step rounds away to nothing: until 2026-10-03 gain and polarity
    // came back from a move 7.2e-6 short at 48 kHz and 2.9e-5 at 192 kHz.
    // DC 1.0 left and 0.5 right, moved away for 2 s and back for 2 s.
    {
        struct Move { int index; float away; const char* name; };
        const Move moves[] {
            { Index::gain,   -6.0f,   "gain -6"   }, { Index::gain,  24.0f,  "gain +24"  },
            { Index::pan,    -100.0f, "pan L100"  }, { Index::pan,   100.0f, "pan R100"  },
            { Index::width,  0.0f,    "width 0"   }, { Index::width, 200.0f, "width 200" },
            { Index::phaseL, 1.0f,    "phase L"   }, { Index::phaseR, 1.0f,  "phase R"   },
            { Index::mono,   1.0f,    "mono"      },
        };

        for (const auto rate : kRates)
            for (const auto& m : moves)
            {
                Rig rig (rate);
                const auto n = (int) (2.0 * rate);
                std::vector<float> l ((size_t) n, 1.0f), r ((size_t) n, 0.5f);
                const auto home = rig.v[m.index];

                rig.v[m.index] = m.away;
                rig.render (l.data(), r.data(), n);

                std::fill (l.begin(), l.end(), 1.0f);
                std::fill (r.begin(), r.end(), 0.5f);
                rig.v[m.index] = home;
                rig.render (l.data(), r.data(), n);

                const auto what = std::to_string ((int) rate) + " Hz, " + m.name + " and back lands exactly (L "
                                + std::to_string (l.back() - 1.0f) + ", R " + std::to_string (r.back() - 0.5f) + " off)";
                check (l.back() == 1.0f && r.back() == 0.5f, what.c_str());
            }
    }

    // A copy flipped WHILE RUNNING nulls against the original exactly, as a
    // copy loaded flipped does. Until 2026-10-03 it nulled to -102.9 dB at
    // 48 kHz and -90.9 dB at 192 kHz on -18 dBFS RMS noise.
    for (const auto rate : kRates)
    {
        const auto n = (int) (3.0 * rate);
        const auto inL = noise (n, -18.0, 1), inR = noise (n, -18.0, 2);

        Rig a (rate), b (rate);
        auto al = inL, ar = inR, bl = inL, br = inR;
        a.render (al.data(), ar.data(), n);

        const auto flipAt = (int) (0.5 * rate) / 512 * 512;
        b.render (bl.data(), br.data(), flipAt);
        b.v[Index::phaseL] = b.v[Index::phaseR] = 1.0f;
        b.render (bl.data() + flipAt, br.data() + flipAt, n - flipAt);

        int differ = 0;
        for (auto i = (size_t) (2.0 * rate); i < (size_t) n; ++i)
            differ += (al[i] + bl[i] != 0.0f) + (ar[i] + br[i] != 0.0f);

        const auto what = std::to_string ((int) rate) + " Hz: a copy flipped while running nulls exactly over the last 1 s ("
                        + std::to_string (differ) + " samples do not)";
        check (differ == 0, what.c_str());
    }

    // The time constant is 5 ms at every rate: gain -24 -> 0 dB on DC 1.0
    // reaches 63.2 % of the step 5.00 ms after it, to the next sample.
    for (const auto rate : kRates)
    {
        Rig rig (rate);
        rig.v[Index::gain] = -24.0f;
        rig.dsp.setParams (rig.v, Index::count);
        rig.dsp.reset();
        rig.v[Index::gain] = 0.0f;

        const auto n = (int) (0.05 * rate);
        std::vector<float> l ((size_t) n, 1.0f), r ((size_t) n, 1.0f);
        rig.render (l.data(), r.data(), n);

        const auto g0 = std::pow (10.0, -24.0 / 20.0);
        const auto mark = g0 + (1.0 - g0) * (1.0 - std::exp (-1.0));
        int at = 0;
        while (at < n && l[(size_t) at] < mark)
            ++at;

        const auto ms = 1000.0 * (at + 1) / rate;
        const auto what = std::to_string ((int) rate) + " Hz: 63.2 % of a gain step at " + std::to_string (ms) + " ms, 5.00 expected";
        check (ms >= 4.999 && ms <= 5.0 + 1000.0 / rate + 1.0e-3, what.c_str());
    }

    // The block size changes nothing, landing included: a gain -24 -> 0 step
    // and a polarity flip on a 1 kHz sine, 0.2 s, at blocks 1, 7, 32, 441, 512.
    {
        const auto n = 9600;
        std::vector<float> refL, refR;

        for (const int block : { 1, 7, 32, 441, 512 })
        {
            Rig rig (48000.0);
            rig.v[Index::gain] = -24.0f;
            rig.dsp.setParams (rig.v, Index::count);
            rig.dsp.reset();
            rig.v[Index::gain] = 0.0f;
            rig.v[Index::phaseL] = 1.0f;

            auto l = sine (n, 1000.0, 48000.0), r = l;
            rig.render (l.data(), r.data(), n, block);

            if (block == 1) { refL = l; refR = r; continue; }

            const auto what = "block " + std::to_string (block) + " renders bit for bit as block 1";
            check (l == refL && r == refR, what.c_str());
        }
    }

    check (UtilDsp().latencyForParams (nullptr, 0) == 0, "no latency");

    if (failures == 0)
        std::cout << "All BMO Util DSP tests passed.\n";

    return failures == 0 ? 0 : 1;
}
