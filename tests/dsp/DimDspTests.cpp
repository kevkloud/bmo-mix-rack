/*
    BMO Dimension's DSP, JUCE-free.

    The headline assertion is the mono sum. Every stage in this module works on
    the side signal alone, so `L + R` out has to equal `L + R` in -- exactly,
    sample for sample, at any width, any shuffle, any diffusion, and with the
    detune stage running. That is not a tolerance to be tuned; it is what the
    topology is for, and if it ever stops holding, something has been wired into
    the mid path that should not be there.

    Rotation and asymmetry are the two controls that deliberately leave that
    guarantee, and there are tests below asserting that they do -- an exception
    nobody has written down is indistinguishable from a bug.
*/

#include "modules/dim/dsp/DimDsp.h"
#include <cmath>
#include <iostream>
#include <vector>

using namespace bmo::dim;

namespace
{
    int failures = 0;

    void check (bool ok, const char* what)
    {
        if (! ok) { std::cerr << "FAIL: " << what << '\n'; ++failures; }
    }

    bool near (float a, float b, float tol = 1.0e-4f) { return std::abs (a - b) <= tol; }

    struct Settings
    {
        float width = 100.0f;
        float shuffle = 1.0f;
        float shuffleFreq = 700.0f;
        float detune = 10.0f;
        bool  detuneOn = false;
        float diffuse = 0.0f;
        float rate = 0.40f;
        float depth = 50.0f;
        float rotation = 0.0f;
        float asymmetry = 0.0f;
    };

    struct Run { std::vector<float> l, r; };

    /** Runs a signal through the DSP. The input is a tone rather than DC so
        the all-pass and the shuffler have something with phase to act on --
        a constant would pass several of these stages trivially and prove
        nothing about them. */
    Run run (const Settings& s, float ampL, float ampR, int n = 8192)
    {
        DimDsp dsp;

        const float v[Index::count] {
            s.width, s.shuffle, s.shuffleFreq,
            s.detune, s.detuneOn ? 1.0f : 0.0f,
            s.diffuse, s.rate, s.depth,
            s.rotation, s.asymmetry
        };

        dsp.setParams (v, Index::count);
        dsp.prepare (48000.0, 512, 2);
        dsp.setParams (v, Index::count);

        std::vector<float> l ((size_t) n), r ((size_t) n);

        for (int i = 0; i < n; ++i)
        {
            const auto t = (float) i / 48000.0f;
            const auto tone = std::sin (2.0f * 3.14159265f * 220.0f * t);
            l[(size_t) i] = ampL * tone;
            r[(size_t) i] = ampR * std::sin (2.0f * 3.14159265f * 330.0f * t);
        }

        float* ch[2] { l.data(), r.data() };
        dsp.process (ch, 2, n);

        return { l, r };
    }

    /** The mono sum of a run, against the mono sum of the same input. */
    float worstMonoError (const Settings& s, float ampL, float ampR)
    {
        constexpr int n = 8192;
        auto out = run (s, ampL, ampR, n);

        float worst = 0.0f;

        for (int i = 0; i < n; ++i)
        {
            const auto t = (float) i / 48000.0f;
            const auto inL = ampL * std::sin (2.0f * 3.14159265f * 220.0f * t);
            const auto inR = ampR * std::sin (2.0f * 3.14159265f * 330.0f * t);

            const auto err = std::abs ((out.l[(size_t) i] + out.r[(size_t) i]) - (inL + inR));
            worst = std::max (worst, err);
        }

        return worst;
    }

    /** How much side content a run carries, as a peak. */
    float peakSide (const Run& out)
    {
        float peak = 0.0f;

        for (size_t i = 0; i < out.l.size(); ++i)
            peak = std::max (peak, std::abs (0.5f * (out.l[i] - out.r[i])));

        return peak;
    }

    /** The pitch shift one voice really delivers, in cents.

        Two identical voices, one fed a 1 kHz cosine and one the matching
        sine. A voice is linear and both follow the same sweep, so their
        outputs are the real and imaginary parts of one complex tone, and its
        phase can be read at any single sample with nothing to average and no
        image to leak in. 1 kHz because the two taps of a voice sit half a
        30 ms window apart, fifteen whole periods at every sample rate, so the
        crossfade between them cannot move that phase either: its slope is
        the shift and nothing else. Read every millisecond to unwrap it, and
        the slope taken from the end of the first window, when the buffer is
        full of signal, to 100 ms later. */
    double deliveredCents (double sampleRate, float cents)
    {
        DetuneVoice re, im;
        re.prepare (sampleRate);
        im.prepare (sampleRate);
        re.setCents (cents);
        im.setCents (cents);

        constexpr double twoPi = 6.283185307179586;
        constexpr double hz    = 1000.0;

        const auto from  = (int) std::lround (sampleRate * 0.040);
        const auto to    = from + (int) std::lround (sampleRate * 0.100);
        const auto every = (int) std::lround (sampleRate * 0.001);

        double first = 0.0, last = 0.0;
        bool   started = false;

        for (int n = 0; n <= to; ++n)
        {
            const auto w = twoPi * hz * n / sampleRate;
            const auto x = (double) re.process ((float) std::cos (w));
            const auto y = (double) im.process ((float) std::sin (w));

            if (n < from || ((n - from) % every != 0 && n != to))
                continue;

            // The tone's phase against the unshifted input's: the shift alone.
            auto ph = std::atan2 (y, x) - std::fmod (w, twoPi);

            if (! started) { first = last = ph; started = true; continue; }

            while (ph - last >  0.5 * twoPi) ph -= twoPi;
            while (ph - last < -0.5 * twoPi) ph += twoPi;
            last = ph;
        }

        const auto seconds = (double) (to - from) / sampleRate;
        const auto shiftHz = (last - first) / twoPi / seconds;
        return 1200.0 * std::log2 (1.0 + shiftHz / hz);
    }
}

int main()
{
    //== The mono sum ==========================================================
    // One assertion per stage, and then all of them at once. The tolerance is
    // float rounding, not a fudge factor: the mid path is literally untouched,
    // so these are exact up to the arithmetic.

    check (worstMonoError ({}, 0.5f, 0.3f) < 1.0e-6f,
           "defaults leave the mono sum exact");

    {
        Settings s; s.width = 200.0f;
        check (worstMonoError (s, 0.5f, 0.3f) < 1.0e-6f, "width 200 does not touch the mono sum");
    }
    {
        Settings s; s.width = 0.0f;
        check (worstMonoError (s, 0.5f, 0.3f) < 1.0e-6f, "width 0 does not touch the mono sum");
    }
    {
        Settings s; s.shuffle = 3.0f; s.shuffleFreq = 650.0f;
        check (worstMonoError (s, 0.5f, 0.3f) < 1.0e-6f, "full shuffle does not touch the mono sum");
    }
    {
        Settings s; s.diffuse = 100.0f; s.depth = 100.0f; s.rate = 2.0f;
        check (worstMonoError (s, 0.5f, 0.3f) < 1.0e-6f, "full diffusion does not touch the mono sum");
    }
    {
        // The one that matters most, and the one a conventional spreader fails:
        // detuning left against right combs in mono. Injecting the detuned
        // difference into the side signal alone cancels instead.
        Settings s; s.detuneOn = true; s.detune = 25.0f;
        check (worstMonoError (s, 0.5f, 0.3f) < 1.0e-6f, "detune does not touch the mono sum");
    }
    {
        Settings s;
        s.width = 175.0f; s.shuffle = 2.4f; s.diffuse = 80.0f;
        s.depth = 70.0f; s.detuneOn = true; s.detune = 14.0f;
        check (worstMonoError (s, 0.5f, 0.3f) < 1.0e-6f,
               "every stage at once still leaves the mono sum exact");
    }

    //== The two documented exceptions ========================================
    // These must NOT be mono-exact. If one of them ever starts passing the
    // test above, it has stopped doing its job.

    {
        Settings s; s.rotation = 30.0f;
        check (worstMonoError (s, 0.5f, 0.3f) > 1.0e-3f,
               "rotation changes the mono sum, as it must");
    }
    {
        Settings s; s.asymmetry = 50.0f;
        check (worstMonoError (s, 0.5f, 0.3f) > 1.0e-3f,
               "asymmetry changes the mono sum, as it must");
    }
    {
        Settings s; s.rotation = 0.0f; s.asymmetry = 0.0f;
        check (worstMonoError (s, 0.5f, 0.3f) < 1.0e-6f,
               "both exceptions are identity at their defaults");
    }

    //== Rotation turns the way a pan knob turns ==============================
    // The two tests above are sign-agnostic: they ask whether rotation moves
    // the mono sum, which is true whichever way it turns. So nothing here
    // caught the sign being inverted, and it reached a listening pass with
    // + moving the image LEFT -- the opposite of every pan control -- before
    // an ear found it on 2026-09-09.
    //
    // The S1 manual fixes the rotation law and says nothing about the knob's
    // direction, so the sign is a free choice rather than something derivable.
    // A free choice is exactly what needs pinning down: there is no formula to
    // re-derive it from, only this.
    //
    // Measured on a dead-centre source, +30 degrees: L 0.1464, R 0.5464.
    {
        // Both channels identical, so the source has no side content and any
        // L/R difference in the output is the rotation's doing.
        const auto peaksAt = [] (float degrees, float& peakL, float& peakR)
        {
            constexpr int n = 8192;
            DimDsp dsp;

            const float v[Index::count] {
                100.0f, 1.0f, 700.0f, 10.0f, 0.0f,
                0.0f, 0.40f, 50.0f, degrees, 0.0f
            };

            dsp.setParams (v, Index::count);
            dsp.prepare (48000.0, 512, 2);
            dsp.setParams (v, Index::count);

            std::vector<float> l ((size_t) n), r ((size_t) n);
            for (int i = 0; i < n; ++i)
            {
                const auto tone = 0.4f * std::sin (2.0f * 3.14159265f * 220.0f
                                                   * (float) i / 48000.0f);
                l[(size_t) i] = tone;
                r[(size_t) i] = tone;
            }

            float* ch[2] { l.data(), r.data() };
            dsp.process (ch, 2, n);

            // Second half only: the smoothers reach the target well inside
            // the first, and a ramp would drag the peak toward centre.
            peakL = peakR = 0.0f;
            for (int i = n / 2; i < n; ++i)
            {
                peakL = std::max (peakL, std::abs (l[(size_t) i]));
                peakR = std::max (peakR, std::abs (r[(size_t) i]));
            }
        };

        float l = 0.0f, r = 0.0f;

        peaksAt (+30.0f, l, r);
        check (r > l * 1.5f, "positive rotation moves a centre source RIGHT");

        peaksAt (-30.0f, l, r);
        check (l > r * 1.5f, "negative rotation moves a centre source LEFT");

        peaksAt (0.0f, l, r);
        check (std::abs (l - r) < 1.0e-6f, "zero rotation leaves it centred");
    }

    //== Asymmetry keeps the centre where it is ================================
    // The S1's manual is explicit that this is what separates the control from
    // a balance: it "does not affect central mono in-phase sounds in any way",
    // and "differs from conventional balance control in that it keeps center
    // sounds in the center". A source with no side content has to come through
    // untouched -- level and position both -- at any setting.
    //
    // This shipped as an unequal output trim, which is a balance control, and
    // put a dead-centre 0.5/0.5 source at 0.75/0.25. Nothing caught it, because
    // every test above feeds a source that already has side content.
    {
        for (float a : { -100.0f, -50.0f, 25.0f, 100.0f })
        {
            DimDsp dsp;
            const float v[Index::count] { 100.0f, 1.0f, 700.0f, 10.0f, 0.0f,
                                          0.0f, 0.4f, 50.0f, 0.0f, a };
            dsp.setParams (v, Index::count);
            dsp.prepare (48000.0, 512, 2);
            dsp.setParams (v, Index::count);

            constexpr int n = 4096;
            std::vector<float> l ((size_t) n), r ((size_t) n);

            for (int i = 0; i < n; ++i)
            {
                const auto t = (float) i / 48000.0f;
                l[(size_t) i] = r[(size_t) i] = 0.5f * std::sin (2.0f * 3.14159265f * 220.0f * t);
            }

            float* ch[2] { l.data(), r.data() };
            dsp.process (ch, 2, n);

            float worstSide = 0.0f, worstLevel = 0.0f;

            for (int i = 0; i < n; ++i)
            {
                const auto t = (float) i / 48000.0f;
                const auto in = 0.5f * std::sin (2.0f * 3.14159265f * 220.0f * t);

                worstSide  = std::max (worstSide,  std::abs (0.5f * (l[(size_t) i] - r[(size_t) i])));
                worstLevel = std::max (worstLevel, std::abs (l[(size_t) i] - in));
            }

            check (worstSide < 1.0e-6f,
                   "asymmetry leaves a centred source centred");
            check (worstLevel < 1.0e-6f,
                   "asymmetry leaves a centred source at its own level");
        }
    }
    {
        // ...and still does its job on material that is off centre, in the
        // stereo image and in the mono sum together.
        Settings s; s.asymmetry = 50.0f;
        auto skewed = run (s, 0.5f, 0.3f);
        auto flat   = run ({}, 0.5f, 0.3f);

        float worst = 0.0f;
        for (size_t i = 0; i < flat.l.size(); ++i)
            worst = std::max (worst, std::abs (skewed.l[i] - flat.l[i]));

        check (worst > 1.0e-3f, "asymmetry does change off-centre material");
    }

    //== Asymmetry favours the side its knob turns toward ======================
    // Same trap as rotation: the tests above ask whether asymmetry moves
    // off-centre material, which is true whichever way it moves it. It reached
    // the UI pass with + favouring the LEFT, and was found only because the
    // panel was about to print an R at that end (2026-09-16). Frosty's call was
    // to flip the DSP so ASYM agrees with ROTATE: + is right.
    //
    // Absolutes, not a comparison. A hard-panned 0.4 tone, second half only so
    // the smoother has arrived. At +50 the shear coefficient is -0.25, so the
    // favoured side comes out at 0.45 and the other at 0.35 -- and at +100 it is
    // 0.50 and 0.30, the disfavoured side 6 dB down in the mono sum, which is
    // what asymCoeff's comment says the end of the knob means.
    {
        const auto peaksAt = [] (float asym, float ampL, float ampR, float& peakL, float& peakR)
        {
            constexpr int n = 8192;
            DimDsp dsp;

            const float v[Index::count] {
                100.0f, 1.0f, 700.0f, 10.0f, 0.0f,
                0.0f, 0.40f, 50.0f, 0.0f, asym
            };

            dsp.setParams (v, Index::count);
            dsp.prepare (48000.0, 512, 2);
            dsp.setParams (v, Index::count);

            std::vector<float> l ((size_t) n), r ((size_t) n);
            for (int i = 0; i < n; ++i)
            {
                const auto tone = std::sin (2.0f * 3.14159265f * 220.0f * (float) i / 48000.0f);
                l[(size_t) i] = ampL * tone;
                r[(size_t) i] = ampR * tone;
            }

            float* ch[2] { l.data(), r.data() };
            dsp.process (ch, 2, n);

            peakL = peakR = 0.0f;
            for (int i = n / 2; i < n; ++i)
            {
                peakL = std::max (peakL, std::abs (l[(size_t) i]));
                peakR = std::max (peakR, std::abs (r[(size_t) i]));
            }
        };

        float l = 0.0f, r = 0.0f;

        peaksAt (+50.0f, 0.0f, 0.4f, l, r);
        check (near (r, 0.45f, 1.0e-3f), "positive asymmetry lifts a hard-RIGHT source to 0.45");

        peaksAt (+50.0f, 0.4f, 0.0f, l, r);
        check (near (l, 0.35f, 1.0e-3f), "positive asymmetry lowers a hard-LEFT source to 0.35");

        peaksAt (-50.0f, 0.4f, 0.0f, l, r);
        check (near (l, 0.45f, 1.0e-3f), "negative asymmetry lifts a hard-LEFT source to 0.45");

        peaksAt (-50.0f, 0.0f, 0.4f, l, r);
        check (near (r, 0.35f, 1.0e-3f), "negative asymmetry lowers a hard-RIGHT source to 0.35");

        peaksAt (+100.0f, 0.0f, 0.4f, l, r);
        check (near (r, 0.50f, 1.0e-3f), "full positive asymmetry takes a hard-RIGHT source to 0.50");

        peaksAt (+100.0f, 0.4f, 0.0f, l, r);
        check (near (l, 0.30f, 1.0e-3f), "full positive asymmetry takes a hard-LEFT source to 0.30");
    }

    //== A mono instance is a wire ============================================
    // SingleModuleProcessor::isBusesLayoutSupported accepts a mono layout, so
    // this path is reachable from a host. There is no stereo image on one
    // channel to work on, and the generate stage would fold its two voices
    // straight back into it -- the comb the whole topology exists to avoid.
    {
        DimDsp dsp;
        const float v[Index::count] { 175.0f, 2.4f, 650.0f, 15.0f, 1.0f,
                                      80.0f, 0.4f, 70.0f, 30.0f, 100.0f };
        dsp.setParams (v, Index::count);
        dsp.prepare (48000.0, 512, 1);
        dsp.setParams (v, Index::count);

        constexpr int n = 8192;
        std::vector<float> m ((size_t) n), ref ((size_t) n);

        for (int i = 0; i < n; ++i)
        {
            const auto t = (float) i / 48000.0f;
            m[(size_t) i] = ref[(size_t) i] = 0.5f * std::sin (2.0f * 3.14159265f * 220.0f * t);
        }

        float* ch[1] { m.data() };
        dsp.process (ch, 1, n);

        float worst = 0.0f;
        for (int i = 0; i < n; ++i)
            worst = std::max (worst, std::abs (m[(size_t) i] - ref[(size_t) i]));

        check (worst == 0.0f, "a mono instance passes through untouched, every stage on");
    }

    //== CENTS at 0 is off, however the knob got there ========================
    // A voice at 0 cents stops sweeping and freezes wherever it was, so with
    // DETUNE on the pair used to become two fixed taps of the mid, differenced:
    // a static comb whose level depended on where the sweep had got to when
    // the knob arrived -- after CENTS 10 -> 0 it measured louder than at 10
    // (0.2.4 review, 2026-09-14). The injected difference is scaled by the
    // first cent of the knob now, so the bottom of the range is silence.
    {
        DimDsp dsp;
        float v[Index::count] { 100.0f, 1.0f, 700.0f, 10.0f, 1.0f,
                                0.0f, 0.4f, 50.0f, 0.0f, 0.0f };

        dsp.setParams (v, Index::count);
        dsp.prepare (48000.0, 512, 2);
        dsp.setParams (v, Index::count);

        int at = 0;   // one continuous tone across the three feeds, no phase jump

        const auto feed = [&] (int n)
        {
            std::vector<float> l ((size_t) n), r ((size_t) n);
            for (int i = 0; i < n; ++i, ++at)
                l[(size_t) i] = r[(size_t) i] = 0.5f * std::sin (2.0f * 3.14159265f * 220.0f * (float) at / 48000.0f);

            float* ch[2] { l.data(), r.data() };
            dsp.process (ch, 2, n);
            return peakSide ({ l, r });
        };

        const auto atTen = feed (96000);          // two seconds at CENTS 10
        check (atTen > 0.01f, "CENTS 10 with DETUNE on manufactures side content");

        v[Index::detune] = 0.0f;                  // the knob goes to 0 mid-sweep
        dsp.setParams (v, Index::count);
        feed (24000);                             // half a second to fade

        const auto atZero = feed (9600);
        check (atZero < 1.0e-5f, "CENTS at 0 with DETUNE on leaves no side content, wherever the sweep had got to");
    }

    //== The detune stage does not replay what it last held ===================
    // The voice buffers carry 30 ms. Switching the stage out, letting a second
    // go by and switching it back in used to burst that 30 ms back out at
    // whatever level it was captured -- 0.90 peak, measured, over silence.
    {
        DimDsp dsp;
        const float on[Index::count]  { 100.0f, 1.0f, 700.0f, 10.0f, 1.0f,
                                        0.0f, 0.4f, 50.0f, 0.0f, 0.0f };
        const float off[Index::count] { 100.0f, 1.0f, 700.0f, 10.0f, 0.0f,
                                        0.0f, 0.4f, 50.0f, 0.0f, 0.0f };

        dsp.setParams (on, Index::count);
        dsp.prepare (48000.0, 512, 2);
        dsp.setParams (on, Index::count);

        constexpr int n = 48000;
        std::vector<float> l ((size_t) n), r ((size_t) n);
        float* ch[2] { l.data(), r.data() };

        for (int i = 0; i < n; ++i)          // a loud second, to fill the voices
        {
            const auto t = (float) i / 48000.0f;
            l[(size_t) i] = r[(size_t) i] = 0.9f * std::sin (2.0f * 3.14159265f * 1000.0f * t);
        }
        dsp.process (ch, 2, n);

        dsp.setParams (off, Index::count);   // out, and a second of silence
        std::fill (l.begin(), l.end(), 0.0f);
        std::fill (r.begin(), r.end(), 0.0f);
        dsp.process (ch, 2, n);

        dsp.setParams (on, Index::count);    // back in, still silent
        std::fill (l.begin(), l.end(), 0.0f);
        std::fill (r.begin(), r.end(), 0.0f);
        dsp.process (ch, 2, 4096);

        float peak = 0.0f;
        for (int i = 0; i < 4096; ++i)
            peak = std::max (peak, std::abs (0.5f * (l[(size_t) i] - r[(size_t) i])));

        check (peak < 1.0e-6f, "re-engaging detune over silence stays silent");
    }

    //== Switching the detune stage is not a click =============================
    // The test above switches over silence, so it cannot see a step: there is
    // nothing to step from. This one switches over a sustained tone at the
    // worst moment -- the peak of the beat between the two voices, where their
    // difference, which on a mono source is the whole side signal, is largest.
    // Ungated, the stage dropped out in one sample and took all of it with it.
    //
    // The bound is relative. A 220 Hz tone moves every sample anyway, so the
    // question is whether the switch moves the side signal more than the tone
    // already does, not whether it moves at all.
    {
        const float on[Index::count]  { 100.0f, 1.0f, 700.0f, 10.0f, 1.0f,
                                        0.0f, 0.4f, 50.0f, 0.0f, 0.0f };
        const float off[Index::count] { 100.0f, 1.0f, 700.0f, 10.0f, 0.0f,
                                        0.0f, 0.4f, 50.0f, 0.0f, 0.0f };

        constexpr int total = 48000, settle = 4800, tail = 9600;

        std::vector<float> dry ((size_t) total);
        for (int i = 0; i < total; ++i)
            dry[(size_t) i] = 0.5f * std::sin (2.0f * 3.14159265f * 220.0f * (float) i / 48000.0f);

        // A mono tone with detune on, switched off at `at` and -- if
        // `backOnAfter` is positive -- on again that many samples later.
        // Returns the side signal, which is all the detune stage produces.
        const auto render = [&] (int at, int backOnAfter)
        {
            DimDsp dsp;
            dsp.setParams (on, Index::count);
            dsp.prepare (48000.0, 512, 2);

            std::vector<float> l (dry), r (dry);

            const auto block = [&] (int from, int to, const float* v)
            {
                dsp.setParams (v, Index::count);
                float* ch[2] { l.data() + from, r.data() + from };
                dsp.process (ch, 2, to - from);
            };

            const auto backOn = backOnAfter > 0 ? std::min (at + backOnAfter, total) : total;

            block (0, at, on);
            block (at, backOn, off);
            block (backOn, total, on);

            std::vector<float> side ((size_t) total);
            for (size_t i = 0; i < side.size(); ++i)
                side[i] = 0.5f * (l[i] - r[i]);
            return side;
        };

        const auto worstStepFrom = [] (const std::vector<float>& s, int from)
        {
            float worst = 0.0f;
            for (size_t i = (size_t) from; i < s.size(); ++i)
                worst = std::max (worst, std::abs (s[i] - s[i - 1]));
            return worst;
        };

        // Never switched: where the beat peaks, and how far the side signal
        // moves in one sample on its own.
        const auto steady = render (total, 0);
        int peakAt = settle;

        for (int i = settle; i < total - tail; ++i)
            if (std::abs (steady[(size_t) i]) > std::abs (steady[(size_t) peakAt]))
                peakAt = i;

        const auto steadyStep = worstStepFrom (steady, settle);

        check (worstStepFrom (render (peakAt, 0), peakAt) < 1.5f * steadyStep,
               "switching detune off mid-beat is not a click");

        // Back on before the fade-out has finished: the voices are mid-sweep
        // and still contributing, so restarting them here -- which is what
        // makes a switch-on from fully off instant -- would itself be a step.
        check (worstStepFrom (render (peakAt, 240), peakAt) < 1.5f * steadyStep,
               "switching detune back on mid-fade is not a click");

        // Back on from fully off, well after the fade-out has finished, at a
        // peak of the tone -- the worst sample for anything that restarts
        // from silence, because that sample is the first one a restarted
        // voice has to reach.
        int onAt = settle + tail;
        for (int i = onAt; i < settle + tail + 220; ++i)
            if (std::abs (dry[(size_t) i]) > std::abs (dry[(size_t) onAt]))
                onAt = i;

        const auto reengaged = render (settle, onAt - settle);

        check (worstStepFrom (reengaged, onAt) < 1.5f * steadyStep,
               "switching detune on from fully off is not a click");

        // And instant, which is Frosty's call from the 2026-09-10 ear test:
        // fade out, but come straight back in. The output cannot show this on
        // its own -- the width starts from zero either way and grows as the
        // voices drift apart -- so ask the stage for its level directly.
        {
            DimDsp dsp;
            dsp.setParams (on, Index::count);
            dsp.prepare (48000.0, 512, 2);

            std::vector<float> l (dry), r (dry);
            float* ch[2] { l.data(), r.data() };

            dsp.setParams (on, Index::count);
            dsp.process (ch, 2, settle);

            ch[0] += settle; ch[1] += settle;
            dsp.setParams (off, Index::count);
            dsp.process (ch, 2, tail);

            check (dsp.getCore().detuneLevel() == 0.0f, "detune has faded fully out");

            dsp.setParams (on, Index::count);
            check (dsp.getCore().detuneLevel() == 1.0f,
                   "switching detune on from fully off is instant");
        }
    }

    //== DETUNE delivers the cents it is set to, at every rate =================
    // The sweep's phase was a float accumulating an increment of
    // (1 - ratio) / window per sample, and near the top of [0, 1) a float has
    // only 6e-8 of resolution. At DETUNE 0.1 the increment is 4.0e-8 at
    // 48 kHz, and smaller the higher the rate, so it was rounded to whole
    // steps of that resolution: the up voice ran 49 % fast at 48 kHz and did
    // not sweep at all at 96 kHz (0.1) and 192 kHz (0.1 and 0.2), and at 1
    // cent it swung between 0.89 and 1.19 depending on where the sweep was.
    // Every step of the knob, both voices, every rate the suite supports; the
    // bound is a hundredth of a cent, a tenth of the knob's smallest step.
    {
        for (double sr : { 44100.0, 48000.0, 96000.0, 192000.0 })
        {
            double worst = 0.0;
            float  worstAt = 0.0f;

            for (int step = 1; step <= 250; ++step)
            {
                const auto cents = 0.1f * (float) step;

                for (float sign : { 1.0f, -1.0f })
                {
                    const auto err = std::abs (deliveredCents (sr, sign * cents) - (double) (sign * cents));

                    if (err > worst) { worst = err; worstAt = sign * cents; }
                }
            }

            if (worst >= 0.01)
                std::cerr << "  at " << sr << " Hz: worst " << worst << " cents off, at DETUNE "
                          << worstAt << '\n';

            check (worst < 0.01, "every DETUNE step delivers its cents within 0.01, both voices, at this rate");
        }
    }

    //== Width =================================================================
    {
        Settings s; s.width = 0.0f;
        auto out = run (s, 0.5f, 0.3f);
        check (peakSide (out) < 1.0e-5f, "width 0 collapses to mono");
    }
    {
        auto unity  = run ({}, 0.5f, 0.3f);
        Settings s; s.width = 200.0f;
        auto doubled = run (s, 0.5f, 0.3f);
        check (near (peakSide (doubled), 2.0f * peakSide (unity), 1.0e-3f),
               "width 200 doubles the side signal");
    }

    //== The generate stage ====================================================
    // The whole reason the stage exists: on a mono source there is no side
    // content for the other two stages to work on, and this is what makes some.

    {
        // Mono in, detune out: S stays exactly zero, because an all-pass of
        // zero is zero and a width control on nothing is nothing.
        Settings s; s.width = 200.0f; s.diffuse = 100.0f;
        auto out = run (s, 0.5f, 0.5f);

        // Both channels fed the same tone would need equal amplitudes; run()
        // gives them different frequencies, so drive it directly instead.
        DimDsp dsp;
        const float v[Index::count] { 200.0f, 1.0f, 700.0f, 10.0f, 0.0f, 100.0f, 0.4f, 50.0f, 0.0f, 0.0f };
        dsp.setParams (v, Index::count);
        dsp.prepare (48000.0, 512, 2);
        dsp.setParams (v, Index::count);

        constexpr int n = 8192;
        std::vector<float> l ((size_t) n), r ((size_t) n);

        for (int i = 0; i < n; ++i)
        {
            const auto t = (float) i / 48000.0f;
            l[(size_t) i] = r[(size_t) i] = 0.5f * std::sin (2.0f * 3.14159265f * 220.0f * t);
        }

        float* ch[2] { l.data(), r.data() };
        dsp.process (ch, 2, n);

        float peak = 0.0f;
        for (int i = 0; i < n; ++i)
            peak = std::max (peak, std::abs (0.5f * (l[(size_t) i] - r[(size_t) i])));

        check (peak < 1.0e-5f, "a mono source stays mono with detune off, whatever else is set");
    }
    {
        // Same input, detune on: side content now exists.
        DimDsp dsp;
        const float v[Index::count] { 100.0f, 1.0f, 700.0f, 15.0f, 1.0f, 0.0f, 0.4f, 50.0f, 0.0f, 0.0f };
        dsp.setParams (v, Index::count);
        dsp.prepare (48000.0, 512, 2);
        dsp.setParams (v, Index::count);

        constexpr int n = 48000;
        std::vector<float> l ((size_t) n), r ((size_t) n);

        for (int i = 0; i < n; ++i)
        {
            const auto t = (float) i / 48000.0f;
            l[(size_t) i] = r[(size_t) i] = 0.5f * std::sin (2.0f * 3.14159265f * 220.0f * t);
        }

        float* ch[2] { l.data(), r.data() };
        dsp.process (ch, 2, n);

        float peak = 0.0f;
        for (int i = 0; i < n; ++i)
            peak = std::max (peak, std::abs (0.5f * (l[(size_t) i] - r[(size_t) i])));

        check (peak > 0.01f, "detune manufactures side content from a mono source");
    }

    //== Shuffle ===============================================================
    {
        // At 1.0 the low and high bands sum back to the input, so the stage is
        // a wire rather than a near-wire. Asserted absolutely.
        auto a = run ({}, 0.5f, 0.3f);
        Settings s; s.shuffle = 1.0f; s.shuffleFreq = 350.0f;
        auto b = run (s, 0.5f, 0.3f);

        float worst = 0.0f;
        for (size_t i = 0; i < a.l.size(); ++i)
            worst = std::max (worst, std::abs (a.l[i] - b.l[i]));

        check (worst < 1.0e-6f, "shuffle 1.0 is exactly a wire at any frequency");
    }
    {
        Settings s; s.shuffle = 3.0f;
        auto shuffled = run (s, 0.5f, 0.3f);
        auto flat     = run ({}, 0.5f, 0.3f);
        check (peakSide (shuffled) > peakSide (flat), "shuffle above 1.0 widens the low end");
    }

    //== BELOW glides rather than jumping ======================================
    // BELOW sets the shuffler's corner, and it used to take a new corner in
    // one sample at the next block. The shuffler's low band is a one-pole, and
    // a corner that quadruples at once makes that pole catch up with its input
    // in a few samples: with BLOOM at 3 that catch-up is scaled by two, and on
    // a 100 Hz side tone the step was 2.64x the tone's own largest step, 1.80x
    // at 300 Hz. The bound is the house one, 1.5x, over a full-range move each
    // way, at BLOOM's extremes, at 44.1, 48 and 96 kHz.
    {
        const auto valuesFor = [] (float bloom, float below)
        {
            std::vector<float> v { 100.0f, bloom, below, 10.0f, 0.0f,
                                   0.0f, 0.4f, 50.0f, 0.0f, 0.0f };
            v.resize ((size_t) Index::count, 0.0f);   // later parameters at 0
            return v;
        };

        // A side-only tone (L = -R), the settings switching from `a` to `b` at
        // sample `at`, in host blocks of `block`. Returns L, then R, then side.
        const auto render = [] (double sr, int n, int block, float hz,
                                const std::vector<float>& a, const std::vector<float>& b, int at)
        {
            DimDsp dsp;
            dsp.setParams (a.data(), Index::count);
            dsp.prepare (sr, block, 2);

            std::vector<float> l ((size_t) n), r ((size_t) n);
            for (int i = 0; i < n; ++i)
            {
                l[(size_t) i] = 0.125f * (float) std::sin (6.283185307179586 * hz * i / sr);
                r[(size_t) i] = -l[(size_t) i];
            }

            for (int i = 0; i < n; i += block)
            {
                dsp.setParams ((i >= at ? b : a).data(), Index::count);
                float* ch[2] { l.data() + i, r.data() + i };
                dsp.process (ch, 2, std::min (block, n - i));
            }

            std::vector<float> s ((size_t) n);
            for (size_t i = 0; i < s.size(); ++i)
                s[i] = 0.5f * (l[i] - r[i]);

            return std::vector<std::vector<float>> { l, r, s };
        };

        const auto worstStep = [] (const std::vector<float>& x, int from, int to)
        {
            float w = 0.0f;
            for (int i = std::max (from, 1); i < to; ++i)
                w = std::max (w, std::abs (x[(size_t) i] - x[(size_t) i - 1]));
            return w;
        };

        double worst = 0.0;
        const char* worstWhere = "";

        for (double sr : { 44100.0, 48000.0, 96000.0 })
            for (float bloom : { 1.0f, 3.0f })
                for (float hz : { 30.0f, 100.0f, 300.0f })
                    for (int dir = 0; dir < 2; ++dir)
                    {
                        const auto lo = valuesFor (bloom, 350.0f), hi = valuesFor (bloom, 1400.0f);
                        const auto& a = dir == 0 ? lo : hi;
                        const auto& b = dir == 0 ? hi : lo;

                        const int n = (int) (sr * 0.5), settle = (int) (sr * 0.1), win = (int) (sr * 0.1);

                        const auto steadyA = render (sr, n, 64, hz, a, a, n);
                        const auto steadyB = render (sr, n, 64, hz, b, b, n);

                        // Eight moments across a period of the tone, block-aligned.
                        const int period = (int) (sr / hz);

                        for (int m = 0; m < 8; ++m)
                        {
                            const int at = (settle + m * period / 8) / 64 * 64;
                            const auto moved = render (sr, n, 64, hz, a, b, at);

                            for (int c = 0; c < 3; ++c)
                            {
                                const auto steady = std::max (worstStep (steadyA[(size_t) c], settle, n),
                                                              worstStep (steadyB[(size_t) c], settle, n));
                                const auto ratio = worstStep (moved[(size_t) c], at, at + win) / steady;

                                if (ratio > worst)
                                {
                                    worst = ratio;
                                    worstWhere = dir == 0 ? "350 -> 1400" : "1400 -> 350";
                                }
                            }
                        }
                    }

        if (worst >= 1.5)
            std::cerr << "  BELOW " << worstWhere << ": worst step " << worst << "x steady\n";

        check (worst < 1.5, "a full-range BELOW move is under 1.5x the signal's own step, either way, at BLOOM 1 and 3");
    }
    {
        // The glide runs in samples, not in host blocks: the same automation
        // at any block size is the same output, bit for bit. 225792 is a
        // block boundary for every size here, so the move lands on the same
        // sample in each.
        constexpr int at = 441 * 512, n = at + 9600;
        constexpr double sr = 48000.0;

        const auto renderAt = [&] (int block)
        {
            const float a[Index::count] { 100.0f, 3.0f, 350.0f, 10.0f, 0.0f, 0.0f, 0.4f, 50.0f, 0.0f, 0.0f };
            const float b[Index::count] { 100.0f, 3.0f, 1400.0f, 10.0f, 0.0f, 0.0f, 0.4f, 50.0f, 0.0f, 0.0f };

            DimDsp dsp;
            dsp.setParams (a, Index::count);
            dsp.prepare (sr, block, 2);

            std::vector<float> l ((size_t) n), r ((size_t) n);
            for (int i = 0; i < n; ++i)
            {
                l[(size_t) i] = 0.125f * (float) std::sin (6.283185307179586 * 100.0 * i / sr);
                r[(size_t) i] = 0.1f * (float) std::sin (6.283185307179586 * 150.0 * i / sr);
            }

            for (int i = 0; i < n; i += block)
            {
                dsp.setParams (i >= at ? b : a, Index::count);
                float* ch[2] { l.data() + i, r.data() + i };
                dsp.process (ch, 2, std::min (block, n - i));
            }

            l.insert (l.end(), r.begin(), r.end());
            return l;
        };

        const auto reference = renderAt (512);

        for (int block : { 1, 7, 32, 441 })
            check (renderAt (block) == reference, "a BELOW move is the same output at any host block size");
    }
    {
        // It lands on the setting exactly, at every rate, and stops: from
        // then on the coefficient is the parameter's own, as it always was.
        for (double sr : { 44100.0, 48000.0, 96000.0 })
        {
            DimDsp dsp;
            float v[Index::count] { 100.0f, 3.0f, 350.0f, 10.0f, 0.0f, 0.0f, 0.4f, 50.0f, 0.0f, 0.0f };
            dsp.setParams (v, Index::count);
            dsp.prepare (sr, 512, 2);
            dsp.setParams (v, Index::count);

            std::vector<float> l (512, 0.1f), r (512, -0.1f);
            float* ch[2] { l.data(), r.data() };

            v[Index::shuffleFreq] = 1400.0f;
            dsp.setParams (v, Index::count);
            dsp.process (ch, 2, 64);
            check (dsp.getCore().belowMoving() && dsp.getCore().belowHz() > 350.0f
                       && dsp.getCore().belowHz() < 1400.0f,
                   "BELOW is part way there 64 samples into a move");

            for (int b = 0; b < (int) (sr * 0.05) / 512 + 1; ++b)
            {
                dsp.setParams (v, Index::count);
                dsp.process (ch, 2, 512);
            }

            check (! dsp.getCore().belowMoving() && dsp.getCore().belowHz() == 1400.0f,
                   "BELOW lands on 1400 exactly within 50 ms, and stops");
        }
    }

    //== The controls the panel dims are the ones the audio ignores ===========
    // The panel dims a control a mode makes inert, and asks the functions
    // beside DspCore which ones those are. This is what keeps those functions
    // honest: in every combination of the four controls that decide it, each
    // control is rendered at both ends of its range, and where its function
    // says dead the two renders must be the same bits; where it says live
    // they must differ, so a function that called everything dead would fail
    // too. One exception, stated rather than hidden: BELOW at BLOOM 1.0 is
    // dead only to float rounding, because the shuffler's z + (s - z) is not
    // always exactly s, and making it exact would move the default output.
    {
        constexpr int    n  = 12000;
        constexpr double sr = 48000.0;

        // Mid at 110 Hz for the generate stage to read, side at 330 Hz for the
        // rest to shape.
        const auto renderWith = [] (const std::vector<float>& v)
        {
            DimDsp dsp;
            dsp.setParams (v.data(), Index::count);
            dsp.prepare (sr, 512, 2);

            std::vector<float> l ((size_t) n), r ((size_t) n);
            for (int i = 0; i < n; ++i)
            {
                const auto m = 0.3f * (float) std::sin (6.283185307179586 * 110.0 * i / sr);
                const auto s = 0.2f * (float) std::sin (6.283185307179586 * 330.0 * i / sr);
                l[(size_t) i] = m + s;
                r[(size_t) i] = m - s;
            }

            for (int i = 0; i < n; i += 512)
            {
                dsp.setParams (v.data(), Index::count);
                float* ch[2] { l.data() + i, r.data() + i };
                dsp.process (ch, 2, std::min (512, n - i));
            }

            l.insert (l.end(), r.begin(), r.end());
            return l;
        };

        const auto maxDiff = [] (const std::vector<float>& a, const std::vector<float>& b)
        {
            float m = 0.0f;
            for (size_t i = 0; i < a.size(); ++i)
                m = std::max (m, std::abs (a[i] - b[i]));
            return m;
        };

        struct Control { int index; float lo, hi; const char* name; };

        const Control controls[]
        {
            { Index::detune,      0.0f,    25.0f,   "DETUNE" },
            { Index::diffuse,     0.0f,    100.0f,  "DRIFT" },
            { Index::shuffle,     1.0f,    3.0f,    "BLOOM" },
            { Index::shuffleFreq, 350.0f,  1400.0f, "BELOW" },
            { Index::asymmetry,   -100.0f, 100.0f,  "TILT" },
        };

        float belowAtUnityBloom = 0.0f;
        int   bitIdenticalCases = 0, roundingCases = 0, liveCases = 0;

        for (bool generate : { false, true })
            for (float width : { 0.0f, 100.0f })
                for (float bloom : { 1.0f, 3.0f })
                    for (float turn : { 0.0f, 30.0f })
                        for (const auto& c : controls)
                        {
                            std::vector<float> v { width, bloom, 700.0f, 10.0f, generate ? 1.0f : 0.0f,
                                                   50.0f, 0.4f, 50.0f, turn, 40.0f };
                            v.resize ((size_t) Index::count, 0.0f);   // later parameters at 0

                            bool live = true;
                            switch (c.index)
                            {
                                case Index::detune:      live = centsIsLive (generate, width); break;
                                case Index::diffuse:     live = diffuseIsLive (width); break;
                                case Index::shuffle:     live = shuffleIsLive (width); break;
                                case Index::shuffleFreq: live = shuffleFreqIsLive (width, bloom); break;
                                case Index::asymmetry:   live = asymmetryIsLive (width, turn); break;
                                default: break;
                            }

                            v[(size_t) c.index] = c.lo;
                            const auto a = renderWith (v);
                            v[(size_t) c.index] = c.hi;
                            const auto b = renderWith (v);

                            const auto where = std::string (c.name) + " with GENERATE "
                                             + (generate ? "on" : "off") + ", DIMENSION "
                                             + std::to_string ((int) width) + ", BLOOM "
                                             + std::to_string ((int) bloom) + ", TURN "
                                             + std::to_string ((int) turn);

                            if (live)
                            {
                                ++liveCases;
                                if (! (maxDiff (a, b) > 1.0e-4f))
                                    std::cerr << "  " << where << ": called live, but both ends render the same\n";
                                check (maxDiff (a, b) > 1.0e-4f, "a control its function calls live moves the output");
                            }
                            else if (c.index == Index::shuffleFreq && width > 0.0f)
                            {
                                ++roundingCases;
                                belowAtUnityBloom = std::max (belowAtUnityBloom, maxDiff (a, b));
                            }
                            else
                            {
                                ++bitIdenticalCases;
                                if (a != b)
                                    std::cerr << "  " << where << ": called dead, but the ends differ by "
                                              << maxDiff (a, b) << '\n';
                                check (a == b, "a control its function calls dead leaves the output bit-identical (every dead case but BELOW at BLOOM 1.0)");
                            }
                        }

        // The table is 80 cases: 36 live, 40 dead and compared bit for bit, and
        // 4 dead only to rounding -- BELOW at BLOOM 1.0 with DIMENSION above 0,
        // once per GENERATE and TURN position. Those four are bounded rather
        // than compared, because the shuffler at unity is z * 1 + (s - z), which
        // is not always exactly s; making it exact would move the default output.
        // Counted, so the split cannot change without this line saying so.
        check (liveCases == 36, "live cases, each moving the output: 36");
        check (bitIdenticalCases == 40, "dead cases compared bit for bit: 40");
        check (roundingCases == 4, "dead cases bounded at rounding (BELOW at BLOOM 1.0): 4");

        // Measured 5.96e-8 on ICE QUEEN, 2026-10-03: one float step at 0.5,
        // -144 dBFS.
        check (belowAtUnityBloom < 1.0e-6f, "BELOW at BLOOM 1.0, the 4 cases not compared bit for bit, moves the output by under 1e-6 (rounding only)");

        // GENERATE is the switch and the panel never dims it; the four
        // controls that decide the dims are not themselves dimmed except as
        // above. DIMENSION and TURN are always live.
        check (centsIsLive (true, 100.0f) && ! centsIsLive (false, 100.0f) && ! centsIsLive (true, 0.0f),
               "DETUNE is live only with GENERATE on and DIMENSION above 0");
    }

    //== OUTPUT: a trim on what leaves the module =============================
    // There was none, and at the extremes the module reaches +7.2 dBFS peak
    // and +15.3 dB of side gain with nothing downstream of it to pull that
    // back. It is a plain trim, per sample in dB, landing exactly, the same
    // behaviour as the equaliser's; at 0 dB with nothing moving it is not in
    // the path at all.
    {
        constexpr int kOut = Index::output;   // the eleventh parameter, the last

        // The module at `rate`, `n` samples of a stereo pair, OUTPUT at `fromDb`
        // until sample `at` and `toDb` after, in host blocks of `block`.
        const auto renderTrim = [] (double rate, int n, int block, float fromDb, float toDb, int at,
                                    bool mono = false)
        {
            std::vector<float> a ((size_t) kOut + 1, 0.0f), b;
            const float base[] { 140.0f, 2.0f, 700.0f, 10.0f, 1.0f, 30.0f, 0.4f, 50.0f, 10.0f, 20.0f };
            std::copy (std::begin (base), std::end (base), a.begin());
            b = a;
            a[(size_t) kOut] = fromDb;
            b[(size_t) kOut] = toDb;

            DimDsp dsp;
            dsp.setParams (a.data(), kOut + 1);
            dsp.prepare (rate, block, mono ? 1 : 2);

            // -18 dBFS RMS, 100 Hz on the left and 150 Hz on the right.
            const auto amp = 0.125892541f * 1.41421356f;
            std::vector<float> l ((size_t) n), r ((size_t) n);
            for (int i = 0; i < n; ++i)
            {
                l[(size_t) i] = amp * (float) std::sin (6.283185307179586 * 100.0 * i / rate);
                r[(size_t) i] = amp * (float) std::sin (6.283185307179586 * 150.0 * i / rate);
            }

            for (int i = 0; i < n; i += block)
            {
                dsp.setParams ((i >= at ? b : a).data(), kOut + 1);
                float* ch[2] { l.data() + i, r.data() + i };
                dsp.process (ch, mono ? 1 : 2, std::min (block, n - i));
            }

            if (! mono)
                l.insert (l.end(), r.begin(), r.end());

            return l;
        };

        // Settled, the trim is the gain its dB says, on both channels, and
        // the same on a mono instance -- it is a level, not imaging.
        {
            constexpr double sr = 48000.0;
            constexpr int n = 24000;
            const auto flat = renderTrim (sr, n, 512, 0.0f, 0.0f, n);

            for (float db : { -24.0f, -6.0f, 6.0f, 24.0f })
            {
                const auto trimmed = renderTrim (sr, n, 512, db, db, n);
                const auto g = std::pow (10.0f, db * 0.05f);

                float worst = 0.0f;
                for (size_t i = 0; i < flat.size(); ++i)
                    worst = std::max (worst, std::abs (trimmed[i] - flat[i] * g));

                check (worst < 1.0e-6f * std::max (1.0f, g), "OUTPUT scales both channels by its gain");

                const auto monoFlat = renderTrim (sr, n, 512, 0.0f, 0.0f, n, true);
                const auto monoTrim = renderTrim (sr, n, 512, db, db, n, true);
                float worstMono = 0.0f;
                for (int i = 0; i < n; ++i)
                    worstMono = std::max (worstMono, std::abs (monoTrim[(size_t) i] - monoFlat[(size_t) i] * g));

                check (worstMono < 1.0e-6f * std::max (1.0f, g), "OUTPUT scales a mono instance too");
            }
        }

        // A full-range move either way is under 1.5x the signal's own largest
        // step, at 44.1, 48 and 96 kHz, and the same at every host block size.
        for (double sr : { 44100.0, 48000.0, 96000.0 })
        {
            const int n = (int) (sr * 1.0), at = (int) (sr * 0.2) / 512 * 512;

            const auto worstStep = [] (const std::vector<float>& x, size_t from, size_t to)
            {
                float w = 0.0f;
                for (size_t i = std::max<size_t> (from, 1); i < to; ++i)
                    w = std::max (w, std::abs (x[i] - x[i - 1]));
                return w;
            };

            for (int dir = 0; dir < 2; ++dir)
            {
                const auto lo = dir == 0 ? -24.0f : 24.0f, hi = dir == 0 ? 24.0f : -24.0f;
                const auto moved   = renderTrim (sr, n, 512, lo, hi, at);
                const auto steadyA = renderTrim (sr, n, 512, lo, lo, n);
                const auto steadyB = renderTrim (sr, n, 512, hi, hi, n);

                float ratio = 0.0f;
                for (size_t c = 0; c < 2; ++c)
                {
                    const auto o = c * (size_t) n, settle = (size_t) (sr * 0.1);
                    const auto steady = std::max (worstStep (steadyA, o + settle, o + (size_t) n),
                                                  worstStep (steadyB, o + settle, o + (size_t) n));
                    ratio = std::max (ratio, worstStep (moved, o + (size_t) at, o + (size_t) n) / steady);
                }

                if (ratio >= 1.5f)
                    std::cerr << "  OUTPUT " << lo << " -> " << hi << " dB at " << sr << " Hz: "
                              << ratio << "x steady\n";

                check (ratio < 1.5f, "a full-range OUTPUT move is under 1.5x the signal's own step");
            }
        }

        // It lands on its target exactly and stops, at every rate; and at
        // 0 dB with nothing moving it is out of the path, from the start and
        // again after a move back.
        for (double sr : { 44100.0, 48000.0, 96000.0, 192000.0 })
        {
            DimDsp dsp;
            float v[Index::count] { 100.0f, 1.0f, 700.0f, 10.0f, 0.0f, 0.0f, 0.4f, 50.0f, 0.0f, 0.0f, 0.0f };
            dsp.setParams (v, Index::count);
            dsp.prepare (sr, 512, 2);
            dsp.setParams (v, Index::count);

            check (dsp.getCore().outputBypassed(), "OUTPUT at 0 dB is out of the path from the start");

            std::vector<float> l (512, 0.1f), r (512, -0.1f);
            float* ch[2] { l.data(), r.data() };

            const auto runFor = [&] (double seconds)
            {
                for (int b = 0; b < (int) (sr * seconds) / 512 + 1; ++b)
                {
                    dsp.setParams (v, Index::count);
                    dsp.process (ch, 2, 512);
                }
            };

            v[Index::output] = 24.0f;
            runFor (0.3);
            check (dsp.getCore().outputDb() == 24.0f && ! dsp.getCore().outputBypassed(),
                   "OUTPUT lands on +24 dB exactly within 300 ms");

            v[Index::output] = 0.0f;
            runFor (0.3);
            check (dsp.getCore().outputDb() == 0.0f && dsp.getCore().outputBypassed(),
                   "OUTPUT lands back on 0 dB exactly and leaves the path");
        }

        // The same automation at any host block size is the same output, bit
        // for bit. 225792 is a block boundary for every size here.
        {
            constexpr int at = 441 * 512, n = at + 14400;

            for (int dir = 0; dir < 2; ++dir)
            {
                const auto lo = dir == 0 ? -24.0f : 24.0f, hi = -lo;
                const auto reference = renderTrim (48000.0, n, 512, lo, hi, at);

                for (int block : { 1, 7, 32, 441 })
                    check (renderTrim (48000.0, n, block, lo, hi, at) == reference,
                           "an OUTPUT move is the same output at any host block size");
            }
        }
    }

    //== Latency ===============================================================
    {
        DimDsp dsp;
        const float v[Index::count] { 100.0f, 1.0f, 700.0f, 25.0f, 1.0f, 100.0f, 0.4f, 50.0f, 0.0f, 0.0f };
        check (dsp.latencyForParams (v, Index::count) == 0,
               "latency is zero with the detune stage running");

        const float off[Index::count] { 100.0f, 1.0f, 700.0f, 10.0f, 0.0f, 0.0f, 0.4f, 50.0f, 0.0f, 0.0f };
        check (dsp.latencyForParams (off, Index::count) == 0,
               "latency is zero with it bypassed, so switching never renegotiates PDC");
    }

    if (failures == 0)
        std::cout << "dim_dsp: all checks passed\n";

    return failures == 0 ? 0 : 1;
}
