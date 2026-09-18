#include "modules/eq/dsp/EqNetwork.h"
#include "modules/eq/dsp/DspCore.h"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <vector>

using namespace bmo::eq;
using bmo::Oversampler;
using bmo::oversamplerLatency;

namespace
{
    int failures = 0;
    constexpr double kPi = 3.14159265358979323846;   // kPi is not portable to MSVC
    constexpr double kSampleRate = 48000.0;

    void check (bool ok, const std::string& what)
    {
        if (! ok) { std::cerr << "FAIL: " << what << '\n'; ++failures; }
    }

    void checkClose (double actual, double expected, double tol, const std::string& what)
    {
        if (! (std::abs (actual - expected) <= tol))
        {
            std::cerr << "FAIL: " << what << " -- expected " << expected
                      << " +/- " << tol << ", got " << actual << '\n';
            ++failures;
        }
    }

    EqNetwork makeNetwork (const EqSettings& s)
    {
        EqNetwork n;
        n.prepare (kSampleRate);
        n.setSettings (s);
        return n;
    }

    /** Impulse response of the actual sample-by-sample audio path. */
    std::vector<float> impulseResponse (EqNetwork& net, int length)
    {
        net.reset();
        std::vector<float> h ((size_t) length);
        h[0] = net.processSample (1.0f);

        for (int i = 1; i < length; ++i)
            h[(size_t) i] = net.processSample (0.0f);

        return h;
    }

    /** Single-frequency DFT by incremental rotation. */
    std::complex<double> dftAt (const std::vector<float>& h, double hz)
    {
        const auto w = -2.0 * kPi * hz / kSampleRate;
        const std::complex<double> step { std::cos (w), std::sin (w) };
        std::complex<double> rot { 1.0, 0.0 }, acc { 0.0, 0.0 };

        for (float v : h) { acc += (double) v * rot; rot *= step; }

        return acc;
    }

    double toDb (double magnitude)
    {
        return 20.0 * std::log10 (std::max (magnitude, 1.0e-12));
    }


    //==========================================================================
    // Phase 4 helpers: drive the whole chain, saturation included.
    //==========================================================================

    constexpr int kChainSize = 1 << 15;

    double snapToBin (double hz)
    {
        const auto bin = std::max (1.0, std::round (hz * (double) kChainSize / kSampleRate));
        return bin * kSampleRate / (double) kChainSize;
    }

    std::vector<float> renderChain (double binHz, double levelDb, float inputGainDb,
                                    int oversampling, DspCore::Params extra = {})
    {
        DspCore core;
        core.prepare (kSampleRate, 512, 1, oversampling);

        auto p = extra;
        p.inputGainDb  = inputGainDb;
        p.oversampling = oversampling;
        core.setParams (p);

        const auto amplitude = std::pow (10.0, levelDb / 20.0);
        std::vector<float> buffer ((size_t) kChainSize);
        float* channels[1] { buffer.data() };

        for (int pass = 0; pass < 2; ++pass)
        {
            for (int i = 0; i < kChainSize; ++i)
                buffer[(size_t) i] = (float) (amplitude
                    * std::sin (2.0 * kPi * binHz * (double) i / kSampleRate));

            core.process (channels, 1, kChainSize);
        }

        return buffer;
    }

    double magnitudeAt (const std::vector<float>& x, double hz)
    {
        const auto w = -2.0 * kPi * hz / kSampleRate;
        const std::complex<double> step { std::cos (w), std::sin (w) };
        std::complex<double> rot { 1.0, 0.0 }, acc { 0.0, 0.0 };

        for (float v : x) { acc += (double) v * rot; rot *= step; }

        return 2.0 * std::abs (acc) / (double) x.size();
    }

    double thdPercent (double hz, double levelDb, float inputGainDb, int oversampling)
    {
        const auto f0 = snapToBin (hz);
        const auto x  = renderChain (f0, levelDb, inputGainDb, oversampling);

        const auto fundamental = magnitudeAt (x, f0);
        double power = 0.0;

        for (int n = 2; n <= 8 && f0 * n < kSampleRate * 0.5; ++n)
        {
            const auto m = magnitudeAt (x, f0 * n);
            power += m * m;
        }

        return fundamental > 0.0 ? 100.0 * std::sqrt (power) / fundamental : 0.0;
    }

    /** Width in octaves at a fixed number of dB below the peak -- the classical
        bandwidth definition. Measuring at a *fraction* of the peak instead is
        misleading: that metric widens with gain even for a constant-Q bell. */
    double bellWidthOctaves (const EqNetwork& net, double centreHz, double dbBelowPeak)
    {
        const auto threshold = net.magnitudeDbAt (centreHz) - dbBelowPeak;

        const auto edge = [&] (double direction)
        {
            double hz = centreHz;

            for (int i = 0; i < 4000; ++i)
            {
                hz *= std::pow (2.0, direction * 0.002);   // 1/500 octave steps

                if (hz < 10.0 || hz > 22000.0)
                    break;

                if (net.magnitudeDbAt (hz) < threshold)
                    return hz;
            }

            return hz;
        };

        return std::log2 (edge (1.0) / edge (-1.0));
    }
}

//==============================================================================
int main()
{
    //== 1. Unity when flat ===================================================
    // The most important invariant of the shared-feedback structure: with every
    // band at 0 dB the numerator and denominator sums cancel exactly.
    {
        EqSettings s;
        auto net = makeNetwork (s);

        for (double hz : { 20.0, 100.0, 1000.0, 5000.0, 15000.0, 20000.0 })
            checkClose (net.magnitudeDbAt (hz), 0.0, 1.0e-6,
                        "flat EQ must be unity at " + std::to_string ((int) hz) + " Hz");

        auto h = impulseResponse (net, 4096);
        checkClose (h[0], 1.0f, 1.0e-6, "flat EQ impulse response should be a unit impulse");

        double tail = 0.0;
        for (size_t i = 1; i < h.size(); ++i) tail += std::abs ((double) h[i]);
        checkClose (tail, 0.0, 1.0e-5, "flat EQ should have no impulse tail");
    }

    //== 2. Single-band gain lands on target ==================================
    {
        EqSettings s;
        s.midFreqHz = 1600.0f;
        s.midGainDb = 12.0f;
        auto net = makeNetwork (s);

        // Not exactly 12.00: the other branches still load the shared feedback
        // path even at unity gain, which shifts the realised peak by a fraction
        // of a dB. That is the topology behaving correctly, not slop.
        checkClose (net.magnitudeDbAt (1600.0), 12.0, 0.15,
                    "mid bell should hit its nominal boost at centre");

        s.midGainDb = -12.0f;
        net.setSettings (s);
        checkClose (net.magnitudeDbAt (1600.0), -12.0, 0.15,
                    "mid bell should be symmetric in cut");
    }
    {
        EqSettings s;
        s.lfFreqHz = 60.0f;
        s.lfGainDb = 10.0f;
        auto net = makeNetwork (s);

        checkClose (net.magnitudeDbAt (2.0), 10.0, 0.4,
                    "low shelf should approach its nominal boost well below the corner");
    }

    //== 3. The analytic response matches the audio path ======================
    // responseAt() drives the curve display and auto-gain. If it disagrees with
    // what processSample() actually does, the plugin lies to the user.
    {
        EqSettings s;
        s.lfFreqHz  = 110.0f; s.lfGainDb  = 8.0f;
        s.midFreqHz = 3200.0f; s.midGainDb = -10.0f; s.midHiQ = true;
        s.hfFreqHz  = 16000.0f; s.hfGainDb = 6.0f;
        s.hpfFreqHz = 45.0f;
        s.lpfFreqHz = 14000.0f;

        auto net = makeNetwork (s);
        auto h   = impulseResponse (net, 1 << 16);

        for (double hz : { 30.0, 60.0, 110.0, 400.0, 1000.0, 3200.0, 8000.0, 14000.0, 18000.0 })
        {
            const auto measured = toDb (std::abs (dftAt (h, hz)));
            const auto analytic = net.magnitudeDbAt (hz);

            checkClose (measured, analytic, 0.05,
                        "analytic response must match the measured impulse response at "
                            + std::to_string ((int) hz) + " Hz");
        }
    }

    //== 4. Bands interact -- the defining property ===========================
    // Cascaded independent biquads would sum in dB. A shared feedback path
    // yields strictly less than that sum wherever two boosted bands overlap.
    // Probe at the low shelf corner and the mid centre, which is where the
    // overlap actually is -- far apart, the bands barely see each other.
    {
        EqSettings lfOnly;  lfOnly.lfFreqHz   = 220.0f; lfOnly.lfGainDb   = 12.0f;
        EqSettings midOnly; midOnly.midFreqHz = 360.0f; midOnly.midGainDb = 12.0f;

        EqSettings both = lfOnly;
        both.midFreqHz = 360.0f;
        both.midGainDb = 12.0f;

        auto a = makeNetwork (lfOnly);
        auto b = makeNetwork (midOnly);
        auto c = makeNetwork (both);

        for (double hz : { 220.0, 360.0 })
        {
            const auto sumOfParts = a.magnitudeDbAt (hz) + b.magnitudeDbAt (hz);
            const auto combined   = c.magnitudeDbAt (hz);

            check (combined < sumOfParts - 2.0,
                   "two overlapping boosted bands must produce clearly less than the sum "
                   "of their curves at " + std::to_string ((int) hz) + " Hz (got "
                   + std::to_string (combined) + " dB vs a naive sum of "
                   + std::to_string (sumOfParts) + " dB)");

            check (combined > 0.0, "two boosted bands should still boost");
        }
    }

    //== 4b. A boosted shelf dips a little, and only a little ===============
    // Real shelves do dip past the corner -- a measured board falls about 6 %
    // of its boost below flat -- so this is not a defect to eliminate. What it
    // guards is the failure that made the shelves first order in the first
    // place: a fully second-order branch rotates phase far enough to carve a
    // 6.5 dB hole an octave from a 12 dB boost, which is 54 % of the boost.
    {
        for (bool high : { false, true })
        {
            EqSettings s;
            (high ? s.hfGainDb : s.lfGainDb) = 12.0f;
            (high ? s.hfFreqHz : s.lfFreqHz) = high ? 12000.0f : 110.0f;

            auto net = makeNetwork (s);

            double worst = 0.0, worstHz = 0.0;

            for (int i = 0; i < 400; ++i)
            {
                const auto hz = 20.0 * std::pow (1000.0, (double) i / 399.0);
                const auto db = net.magnitudeDbAt (hz);

                if (db < worst) { worst = db; worstHz = hz; }
            }

            check (std::abs (worst) < 0.12 * 12.0,
                   std::string (high ? "high" : "low") + " shelf boost should dip only slightly "
                   "past the corner (worst " + std::to_string (worst) + " dB at "
                   + std::to_string ((int) worstHz) + " Hz, more than 12 % of the boost)");
        }
    }

    //== 4c. Shelf slope matches the measured hardware ======================
    // 7.0 dB/octave through the transition, repeatable across the 35, 60 and
    // 110 Hz positions of the board that was measured. A first-order branch
    // alone gives about 4.9.
    {
        for (double hz : { 35.0, 60.0, 110.0 })
        {
            EqSettings s;
            s.lfFreqHz = (float) hz;
            s.lfGainDb = 20.0f;
            auto net = makeNetwork (s);

            const auto plateau = net.magnitudeDbAt (5.0);

            const auto find = [&] (double below)
            {
                for (int i = 0; i < 2000; ++i)
                {
                    const auto f = hz * std::pow (2.0, 6.0 * i / 1999.0);
                    if (net.magnitudeDbAt (f) < plateau - below) return f;
                }
                return 0.0;
            };

            const auto a = find (3.0), b = find (15.0);
            const auto slope = (a > 0.0 && b > a) ? 12.0 / std::log2 (b / a) : 0.0;

            checkClose (slope, 6.8, 1.2,
                        "low shelf slope at " + std::to_string ((int) hz)
                            + " Hz should be near the measured 7 dB/octave");
        }
    }

    //== 5. Proportional Q ====================================================
    // The bell narrows as gain rises, and nothing in the code models that
    // explicitly -- it falls out of the shared feedback structure.
    {
        EqSettings s; s.midFreqHz = 1600.0f;

        s.midGainDb = 6.0f;
        auto gentle = makeNetwork (s);
        const auto wideOct = bellWidthOctaves (gentle, 1600.0, 3.0);

        s.midGainDb = 18.0f;
        auto steep = makeNetwork (s);
        const auto narrowOct = bellWidthOctaves (steep, 1600.0, 3.0);

        check (narrowOct < wideOct - 0.5,
               "bell should narrow with increasing gain (proportional Q): "
               + std::to_string (wideOct) + " oct at +6 dB vs "
               + std::to_string (narrowOct) + " oct at +18 dB");
    }

    //== 6. High-pass slope is 18 dB/octave ===================================
    {
        EqSettings s; s.hpfFreqHz = 300.0f;
        auto net = makeNetwork (s);

        const auto perOctave = net.magnitudeDbAt (75.0) - net.magnitudeDbAt (37.5);
        checkClose (perOctave, 18.0, 0.5, "high-pass should fall 18 dB per octave");

        check (net.magnitudeDbAt (20000.0) > -0.1, "high-pass must not affect the top end");

        // No resonance. A third-order Butterworth is maximally flat, and that
        // is what the hardware measures -- response plots of an assembled board
        // show no peak whatever. A guessed Q of 1.30 here had invented a
        // 0.87 dB one.
        for (int position = 1; position <= 4; ++position)
        {
            EqSettings h;
            h.hpfFreqHz = hpfFreqHz (position);
            auto filter = makeNetwork (h);

            double peak = -100.0, peakHz = 0.0;

            for (int i = 0; i < 1200; ++i)
            {
                const auto hz = 20.0 * std::pow (1000.0, (double) i / 1199.0);
                const auto db = filter.magnitudeDbAt (hz);

                if (db > peak) { peak = db; peakHz = hz; }
            }

            check (peak < 0.1,
                   "the high-pass must not resonate (" + std::to_string ((int) h.hpfFreqHz)
                       + " Hz peaks " + std::to_string (peak) + " dB at "
                       + std::to_string ((int) peakHz) + " Hz)");

            // Butterworth puts -3 dB exactly on the marked frequency.
            checkClose (filter.magnitudeDbAt (h.hpfFreqHz), -3.0, 0.35,
                        "the high-pass should be 3 dB down at its marked frequency");
        }
    }

    //== 7. Stability under extremes ==========================================
    {
        for (int model = 0; model < 2; ++model)
            for (int f = 0; f < 6; ++f)
            {
                EqSettings s;
                s.midFreqHz = kMidFreqs[(size_t) f];
                s.midGainDb = 18.0f;
                s.lfGainDb  = 16.0f;
                s.hfGainDb  = 16.0f;
                s.midHiQ    = true;
                s.hpfFreqHz = 45.0f;
                s.lpfFreqHz = model == 1 ? 6000.0f : 0.0f;

                auto net = makeNetwork (s);
                auto h   = impulseResponse (net, 8192);

                bool finite = true, decayed = true;
                for (float v : h) finite = finite && std::isfinite (v);
                for (size_t i = 6000; i < h.size(); ++i) decayed = decayed && std::abs (h[i]) < 0.01f;

                check (finite,  "impulse response must stay finite at extreme settings");
                check (decayed, "impulse response must decay at extreme settings");
            }
    }

    //== 8. DspCore plumbing ==================================================
    {
        constexpr int n = 4096;
        std::vector<float> left ((size_t) n), right ((size_t) n);
        float* channels[2] { left.data(), right.data() };

        const auto fill = [&]
        {
            for (int i = 0; i < n; ++i)
            {
                const auto v = 0.25f * std::sin (2.0f * (float) kPi * 220.0f * (float) i / 48000.0f);
                left[(size_t) i] = right[(size_t) i] = v;
            }
        };

        // Phase invert should negate the signal. Checked well below the knee,
        // where the colour stage is linear -- higher up the saturation makes
        // this a comparison of two slightly different waveforms rather than a
        // test of polarity.
        {
            DspCore core;
            core.prepare (kSampleRate, n, 1);
            DspCore::Params p;
            p.eqIn = false;
            p.phaseInvert = true;
            p.oversampling = 1;          // keep it latency-free for a direct comparison
            core.setParams (p);

            constexpr float amplitude = 1.0e-4f;

            for (int i = 0; i < n; ++i)
                left[(size_t) i] = right[(size_t) i] = amplitude
                    * std::sin (2.0f * (float) kPi * 220.0f * (float) i / 48000.0f);

            std::vector<float> reference = left;
            core.process (channels, 2, n);

            // Correlation rather than a sample-by-sample match: the
            // transformer's low-frequency rolloff shifts phase by a degree or
            // so at 220 Hz, which is correct behaviour and would defeat a
            // direct comparison without saying anything about polarity.
            double dot = 0.0, energyA = 0.0, energyB = 0.0;

            for (int i = 512; i < n; ++i)
            {
                const double a = left[(size_t) i], b = reference[(size_t) i];
                dot += a * b;
                energyA += a * a;
                energyB += b * b;
            }

            const auto correlation = dot / std::sqrt (energyA * energyB);

            checkClose (correlation, -1.0, 0.01,
                        "phase invert should negate the signal");
        }

        // Mix at 0% must be bit-identical to the input, whatever the EQ does.
        {
            DspCore core;
            core.prepare (kSampleRate, n, 2);
            DspCore::Params p;
            p.mixPercent = 0.0f; p.midGainDb = 18.0f; p.lfGainDb = -16.0f; p.hpfIndex = 3;
            core.setParams (p);
            fill();
            std::vector<float> reference = left;
            core.process (channels, 2, n);

            bool identical = true;
            for (int i = 0; i < n; ++i)
                identical = identical && std::abs (left[(size_t) i] - reference[(size_t) i]) < 1.0e-6f;

            check (identical, "mix at 0% must pass the dry signal through untouched");
        }

        // Phase must flip the blend, not only the wet path. Until 0.2.4 the dry
        // ring held the un-flipped input, so at Mix 50 % a flat EQ cancelled
        // against its own dry copy and the output was near silence. Asserted
        // as a level ratio against the input as well as a correlation, since
        // the cancelled version still correlates perfectly with whatever is
        // left of it.
        {
            DspCore core;
            core.prepare (kSampleRate, n, 1);
            DspCore::Params p;
            p.eqIn = false;
            p.phaseInvert = true;
            p.oversampling = 1;
            p.mixPercent = 50.0f;
            core.setParams (p);

            constexpr float amplitude = 1.0e-4f;

            for (int i = 0; i < n; ++i)
                left[(size_t) i] = right[(size_t) i] = amplitude
                    * std::sin (2.0f * (float) kPi * 220.0f * (float) i / 48000.0f);

            std::vector<float> reference = left;
            core.process (channels, 2, n);

            double dot = 0.0, energyA = 0.0, energyB = 0.0;

            for (int i = 512; i < n; ++i)
            {
                const double a = left[(size_t) i], b = reference[(size_t) i];
                dot += a * b;
                energyA += a * a;
                energyB += b * b;
            }

            checkClose (dot / std::sqrt (energyA * energyB), -1.0, 0.01,
                        "phase invert at mix 50% still negates the signal");
            checkClose (std::sqrt (energyA / energyB), 1.0, 0.1,
                        "phase invert at mix 50% keeps the level: the dry half is flipped with the wet half");
        }

        // Auto-gain should pull a heavily boosted setting back toward unity.
        {
            const auto rms = [&] (bool autoGain)
            {
                DspCore core;
                core.prepare (kSampleRate, n, 2);
                DspCore::Params p;
                p.lfGainDb = 16.0f; p.midGainDb = 12.0f; p.autoGain = autoGain;
                core.setParams (p);
                fill();
                core.process (channels, 2, n);

                double acc = 0.0;
                for (int i = n / 2; i < n; ++i) acc += (double) left[(size_t) i] * left[(size_t) i];
                return std::sqrt (acc / (double) (n / 2));
            };

            const auto boosted = rms (false);
            const auto levelled = rms (true);

            check (levelled < boosted,
                   "auto-gain should reduce level relative to the uncompensated boost");
        }

        // And it must act when it is the only thing that changed. Two fresh
        // cores, as above, cannot see the 0.2.4 fault: the target was only set
        // after a band moved, so on one core the switch was inert until the
        // next knob touch and then jumped.
        {
            DspCore core;
            core.prepare (kSampleRate, n, 2);
            DspCore::Params p;
            p.lfGainDb = 16.0f; p.midGainDb = 12.0f; p.autoGain = false;
            core.setParams (p);
            fill();
            core.process (channels, 2, n);

            const auto rmsOf = [&]
            {
                double acc = 0.0;
                for (int i = n / 2; i < n; ++i) acc += (double) left[(size_t) i] * left[(size_t) i];
                return std::sqrt (acc / (double) (n / 2));
            };

            fill();
            core.process (channels, 2, n);
            const auto boosted = rmsOf();

            p.autoGain = true;                 // nothing else moves
            core.setParams (p);
            fill();
            core.process (channels, 2, n);     // the 20 ms smoother settles inside one block
            fill();
            core.process (channels, 2, n);
            const auto levelled = rmsOf();

            check (levelled < boosted * 0.9,
                   "toggling Auto Gain on its own, with no band moving, changes the level");
        }

        // Nothing may allocate or blow up when the model is switched mid-stream.
        {
            DspCore core;
            core.prepare (kSampleRate, 512, 2);
            std::vector<float> l (512), r (512);
            float* ch[2] { l.data(), r.data() };

            bool finite = true;
            for (int block = 0; block < 200; ++block)
            {
                DspCore::Params p;
                                p.midFreqIndex = block % 6;
                p.hpfIndex     = block % 5;
                p.midGainDb    = ((block % 7) - 3) * 6.0f;
                core.setParams (p);

                for (int i = 0; i < 512; ++i)
                    l[(size_t) i] = r[(size_t) i] = 0.3f * std::sin (0.05f * (float) (block * 512 + i));

                core.process (ch, 2, 512);

                for (int i = 0; i < 512; ++i)
                    finite = finite && std::isfinite (l[(size_t) i]) && std::abs (l[(size_t) i]) < 50.0f;
            }

            check (finite, "rapid model and selector changes must stay finite and bounded");
        }
    }


    //== 9. The colour stage leaves the response alone when it is not pushed ==
    // The transformer emphasises into a flux-like domain, saturates there, and
    // de-emphasises by the exact inverse. If that pair does not cancel, the
    // plugin tilts the spectrum even at rest, which would be a far worse fault
    // than any amount of distortion.
    {
        for (double hz : { 30.0, 60.0, 200.0, 1000.0, 5000.0, 12000.0 })
        {
            const auto f0 = snapToBin (hz);
            const auto x  = renderChain (f0, -60.0, 0.0f, 2);

            const auto db = 20.0 * std::log10 (magnitudeAt (x, f0) / std::pow (10.0, -60.0 / 20.0));

            checkClose (db, 0.0, 0.6,
                        "chain should be within a fraction of a dB of flat at low level at "
                            + std::to_string ((int) hz) + " Hz");
        }
    }

    //== 10. Distortion falls with level, and keeps falling =================
    // Guards a real defect this code had: log(cosh(u)) computed the obvious
    // way loses all precision for small u, and because ADAA divides the
    // difference of two antiderivatives by a small dx, that error was
    // amplified into a constant noise floor -- 66 % "THD" on a quiet signal.
    {
        double previous = 100.0;

        for (double level : { 0.0, -12.0, -24.0, -36.0, -48.0, -60.0 })
        {
            const auto thd = thdPercent (1000.0, level, 0.0f, 2);

            check (thd < previous,
                   "THD must fall as level falls (at " + std::to_string ((int) level)
                       + " dBFS got " + std::to_string (thd) + " %, previously "
                       + std::to_string (previous) + " %)");

            previous = thd;
        }

        check (previous < 0.01,
               "a -60 dBFS signal should be essentially undistorted, got "
                   + std::to_string (previous) + " %");
    }

    //== 11. Low frequencies distort far harder -- the transformer's signature =
    // Core flux is the integral of applied voltage, so at a fixed level the
    // bottom of the band drives the core far harder than the top. Marinair
    // measured 0.1 % at 40 Hz against 0.01 % at 1 kHz for the line transformer
    // in these units. A memoryless waveshaper distorts every frequency alike
    // and cannot produce this at all.
    {
        const auto low  = thdPercent (40.0,   0.0, 0.0f, 2);
        const auto mid  = thdPercent (1000.0, 0.0, 0.0f, 2);

        check (low > mid * 3.0,
               "40 Hz should distort several times harder than 1 kHz at the same level ("
                   + std::to_string (low) + " % against " + std::to_string (mid) + " %)");

        check (mid < 2.0, "1 kHz at full scale should stay civil, got " + std::to_string (mid) + " %");
    }

    //== 12. Second harmonic dominates through the midrange =================
    // Single-ended class-A stages are asymmetric, and the second harmonic is
    // an octave, so it stays consonant with whatever produced it.
    {
        const auto f0 = snapToBin (1000.0);
        const auto x  = renderChain (f0, 0.0, 0.0f, 2);

        const auto h2 = magnitudeAt (x, f0 * 2.0);
        const auto h3 = magnitudeAt (x, f0 * 3.0);

        check (h2 > h3 * 2.0,
               "second harmonic should dominate the third in the midrange ("
                   + std::to_string (20.0 * std::log10 (h2 / h3)) + " dB apart)");
    }

    //== 13. Aliasing stays buried ===========================================
    {
        const auto f0 = snapToBin (7500.0);
        const auto x  = renderChain (f0, -3.0, 18.0f, 2);

        const auto fundamental = magnitudeAt (x, f0);
        double worst = 0.0;

        for (int bin = 4; bin < kChainSize / 2; bin += 7)
        {
            const auto f = bin * kSampleRate / (double) kChainSize;
            bool harmonic = false;

            for (int n = 1; n <= 12; ++n)
                if (std::abs (f - n * f0) < 4.0 * kSampleRate / (double) kChainSize)
                    harmonic = true;

            if (! harmonic)
                worst = std::max (worst, magnitudeAt (x, f));
        }

        const auto rejection = 20.0 * std::log10 (worst / fundamental);
        check (rejection < -80.0,
               "folded images should sit below -80 dB even when driven hard, got "
                   + std::to_string (rejection) + " dB");
    }

    //== 14. The dry path is delayed to match the oversampling filters =======
    // Otherwise a partial Mix combs and a full bypass fails to null.
    {
        // 8x included deliberately: the dry-delay buffer was sized from a
        // constant that held one stage's delay rather than the cascade's, so
        // the highest factor overran it and corrupted the heap. Nothing below
        // 8x touched the bug.
        for (int factor : { 1, 2, 4, 8 })
        {
            DspCore core;
            core.prepare (kSampleRate, 512, 1, factor);

            DspCore::Params p;
            p.mixPercent   = 0.0f;      // dry only
            p.oversampling = factor;
            p.midGainDb    = 18.0f;     // the wet path is doing plenty; it must not leak
            p.inputGainDb  = 12.0f;
            core.setParams (p);

            constexpr int n = 2048;
            std::vector<float> buffer ((size_t) n, 0.0f);
            buffer[64] = 1.0f;          // a lone impulse is easy to locate
            float* channels[1] { buffer.data() };

            core.process (channels, 1, n);

            int peak = 0;
            for (int i = 0; i < n; ++i)
                if (std::abs (buffer[(size_t) i]) > std::abs (buffer[(size_t) peak]))
                    peak = i;

            check (peak - 64 == core.getLatencySamples(),
                   "at 0 % mix the output should be the input delayed by exactly the "
                   "reported latency (factor " + std::to_string (factor) + ": delay "
                       + std::to_string (peak - 64) + ", reported "
                       + std::to_string (core.getLatencySamples()) + ")");

            checkClose (buffer[(size_t) peak], 1.0f, 1.0e-3,
                        "the dry path must pass at unity");
        }
    }

    //== 15. Nothing blows up when driven into the weeds =====================
    // Stereo at the highest factor, which is where the dry-delay overrun was.
    {
        static_assert (Oversampler::kMaxLatency >= oversamplerLatency (Oversampler::kMaxFactor),
                       "the dry delay is sized from kMaxLatency; it must cover every factor");

        DspCore core;
        core.prepare (kSampleRate, 512, 2, 8);

        DspCore::Params p;
        p.inputGainDb = 24.0f;
        p.midGainDb   = 18.0f;
        p.lfGainDb    = 16.0f;
        p.hfGainDb    = 16.0f;
        p.oversampling = 8;
        core.setParams (p);

        std::vector<float> l (512), r (512);
        float* ch[2] { l.data(), r.data() };
        bool sane = true;

        for (int block = 0; block < 100; ++block)
        {
            for (int i = 0; i < 512; ++i)
                l[(size_t) i] = r[(size_t) i] = (float) std::sin (0.03 * (block * 512 + i));

            core.process (ch, 2, 512);

            for (int i = 0; i < 512; ++i)
                sane = sane && std::isfinite (l[(size_t) i]) && std::abs (l[(size_t) i]) < 100.0f;
        }

        check (sane, "the chain must stay finite and bounded when driven hard");
    }


    //== 16. The mid band is tuned where the panel says it is ================
    // Band interaction pulls the realised peak away from the branch frequency.
    // With the Q of every detent derived from the circuit this stays small, but
    // it was 15.8 % flat at 7.2 kHz when the whole band shared one Q value --
    // you selected 7.2 kHz and got 6.1 kHz, which is more than a quarter tone.
    {
        for (int position = 0; position < 6; ++position)
        {
            const auto nominal = (double) kMidFreqs[(size_t) position];

            EqSettings s;
            s.midFreqHz = (float) nominal;
            s.midGainDb = 9.0f;
            auto net = makeNetwork (s);

            double best = -1.0e9, peak = nominal;

            for (int i = 0; i < 3000; ++i)
            {
                const auto hz = nominal * std::pow (2.0, -1.5 + 3.0 * i / 2999.0);
                const auto db = net.magnitudeDbAt (hz);

                if (db > best) { best = db; peak = hz; }
            }

            const auto errorPercent = 100.0 * (peak - nominal) / nominal;

            check (std::abs (errorPercent) < 2.5,
                   "the mid bell should peak within a few percent of its marked frequency ("
                       + std::to_string ((int) nominal) + " Hz reads "
                       + std::to_string ((int) peak) + " Hz, "
                       + std::to_string (errorPercent) + " %)");
        }
    }

    //== 16b. Bell widths match the measured hardware =======================
    // Regression guard on the calibration. Targets are realised -3 dB widths
    // at +18 dB, traced from a response plot of an assembled board published
    // with the Nyan-1073-EQ hardware project (CC BY-SA 4.0). `measure fitq`
    // solves the branch Q values from these.
    {
        const double target[6] { 1.13, 1.00, 1.06, 1.15, 0.74, 0.52 };

        for (int position = 0; position < 6; ++position)
        {
            EqSettings s;
            s.midFreqHz = kMidFreqs[(size_t) position];
            s.midGainDb = 18.0f;
            auto net = makeNetwork (s);

            const auto width = bellWidthOctaves (net, kMidFreqs[(size_t) position], 3.0);

            checkClose (width, target[position], 0.06,
                        "bell width at " + std::to_string ((int) kMidFreqs[(size_t) position])
                            + " Hz should match the measured hardware");
        }
    }

    //== 17. Q rises across the mid selector ================================
    // The lower three detents switch inductance as well as capacitance, which
    // holds Q roughly level; the upper three share one winding and switch
    // capacitance alone, so Q climbs with frequency. 360 Hz is broad, 7.2 kHz
    // is a presence peak. A single Q constant cannot do this.
    {
        std::vector<double> widths;

        for (int position = 0; position < 6; ++position)
        {
            EqSettings s;
            s.midFreqHz = kMidFreqs[(size_t) position];
            s.midGainDb = 9.0f;
            auto net = makeNetwork (s);

            widths.push_back (bellWidthOctaves (net, kMidFreqs[(size_t) position], 3.0));
        }

        check (widths.front() > widths.back() * 1.8,
               "360 Hz should be markedly broader than 7.2 kHz ("
                   + std::to_string (widths.front()) + " against "
                   + std::to_string (widths.back()) + " octaves)");

        check (widths[5] < widths[3] && widths[4] < widths[3],
               "the upper detents should tighten as frequency rises");

        // Hi-Q narrows whatever the position already was.
        EqSettings s;
        s.midFreqHz = 1600.0f;
        s.midGainDb = 9.0f;
        auto wide = makeNetwork (s);

        s.midHiQ = true;
        auto narrow = makeNetwork (s);

        check (bellWidthOctaves (narrow, 1600.0, 3.0) < bellWidthOctaves (wide, 1600.0, 3.0) * 0.8,
               "Hi-Q should clearly narrow the mid band");
    }


    //== 18. Every control tunes where the manual says ======================
    // Frequencies live both in the tables the filters are tuned from and in the
    // strings the panel prints, and nothing ties the two together, so they are
    // checked against each other in the parameter tests as well. These figures
    // are from the Neve 1073 & 1084 user manual, issue 5.
    {
        // High cut: 18 dB/octave, like the low cut. It was built as a
        // second-order 12 dB/octave section until the manual was checked.
        {
            EqSettings s;
            s.lpfFreqHz = 6000.0f;

            // Well above the host rate, so bilinear warping does not flatter
            // the measurement; the asymptote is what 18 dB/octave refers to.
            EqNetwork wide;
            wide.prepare (768000.0);
            wide.setSettings (s);

            checkClose (wide.magnitudeDbAt (24000.0) - wide.magnitudeDbAt (12000.0), -18.0, 1.0,
                        "the high cut should fall 18 dB per octave");

            checkClose (wide.magnitudeDbAt (6000.0), -3.0, 0.35,
                        "the high cut should be 3 dB down at its marked frequency");

            double peak = -100.0;
            for (int i = 0; i < 1200; ++i)
                peak = std::max (peak, wide.magnitudeDbAt (20.0 * std::pow (1000.0, (double) i / 1199.0)));

            check (peak < 0.1, "the high cut must not resonate, got " + std::to_string (peak) + " dB");
        }

        // Cut filter tables.
        {
            const float lowCut[4]  { 45.0f, 70.0f, 160.0f, 360.0f };
            const float highCut[5] { 6000.0f, 8000.0f, 10000.0f, 14000.0f, 18000.0f };

            for (int position = 1; position <= 4; ++position)
                checkClose (hpfFreqHz (position), lowCut[position - 1], 0.5,
                            "low cut detent " + std::to_string (position));

            for (int position = 1; position <= 5; ++position)
                checkClose (lpfFreqHz (position), highCut[position - 1], 0.5,
                            "high cut detent " + std::to_string (position));

            check (hpfFreqHz (0) == 0.0f && lpfFreqHz (0) == 0.0f, "detent 0 is Off on both");
        }

        // The high shelf moves; it is not fixed.
        {
            for (int position = 0; position < 3; ++position)
                checkClose (highShelfFreqHz (position), kHighShelfFreqs[(size_t) position], 1.0,
                            "the high shelf should follow its selector");

            EqSettings a; a.hfFreqHz = 10000.0f; a.hfGainDb = 12.0f;
            EqSettings b = a; b.hfFreqHz = 16000.0f;

            auto low = makeNetwork (a);
            auto high = makeNetwork (b);

            check (low.magnitudeDbAt (8000.0) > high.magnitudeDbAt (8000.0) + 1.0,
                   "a 10 kHz shelf should lift 8 kHz more than a 16 kHz one does");
        }

        // Hi-Q narrows the mid without detuning it.
        {
            EqSettings s;
            s.midFreqHz = 1600.0f;
            s.midGainDb = 12.0f;

            auto wide = makeNetwork (s);
            s.midHiQ = true;
            auto narrow = makeNetwork (s);

            const auto wideOct   = bellWidthOctaves (wide, 1600.0, 3.0);
            const auto narrowOct = bellWidthOctaves (narrow, 1600.0, 3.0);

            check (narrowOct < wideOct * 0.75,
                   "Hi-Q should clearly narrow the mid ("
                       + std::to_string (wideOct) + " to " + std::to_string (narrowOct)
                       + " octaves)");

            for (auto* net : { &narrow, &wide })
            {
                double best = -1.0e9, peak = 1600.0;
                for (int i = 0; i < 3000; ++i)
                {
                    const auto hz = 1600.0 * std::pow (2.0, -1.5 + 3.0 * i / 2999.0);
                    const auto db = net->magnitudeDbAt (hz);
                    if (db > best) { best = db; peak = hz; }
                }
                checkClose (peak, 1600.0, 1600.0 * 0.025, "Hi-Q must not detune the mid band");
            }
        }
    }

    //== 19. Turning a gain control up must boost ===========================
    // The panel marks a plus at one end of each gain control and a minus at the
    // other. They were on the wrong sides -- the pointer reached the plus at
    // minimum gain -- so this pins the direction in the DSP, which is what the
    // marks have to describe. Nothing here is about drawing.
    {
        struct Band { const char* name; float EqSettings::* gain; double probeHz; };

        const Band bands[] {
            { "mid",        &EqSettings::midGainDb, 1600.0 },
            { "low shelf",  &EqSettings::lfGainDb,  40.0 },
            { "high shelf", &EqSettings::hfGainDb,  16000.0 },
        };

        for (const auto& band : bands)
        {
            EqSettings up;
            up.*(band.gain) = 12.0f;
            check (makeNetwork (up).magnitudeDbAt (band.probeHz) > 3.0,
                   std::string ("a positive ") + band.name + " setting must boost");

            EqSettings down;
            down.*(band.gain) = -12.0f;
            check (makeNetwork (down).magnitudeDbAt (band.probeHz) < -3.0,
                   std::string ("a negative ") + band.name + " setting must cut");
        }
    }

    if (failures == 0)
        std::cout << "All DSP tests passed.\n";

    return failures == 0 ? 0 : 1;
}
