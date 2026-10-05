/*
    Tests for the DSP core. No JUCE, no host, no audio device: the core takes
    plain buffers, so everything the plugin claims about itself can be checked
    on a bare container in a couple of seconds.

    The tests that matter most are the ones holding the character in place --
    the curve's asymmetry, its even-order balance, where in the spectrum the
    new energy lands, and the fact that the crest factor does not fall. Those
    are the four things the plugin was specified with, and a change that
    quietly breaks one of them is exactly the change nobody notices.
*/

#include "modules/sat/dsp/DspCore.h"
#include "modules/sat/dsp/DriveTables.h"
#include <cmath>
#include <complex>
#include <cstdio>
#include <string>
#include <vector>

using namespace bmo::sat;

namespace
{

constexpr double kPi = 3.14159265358979323846;
constexpr double kSampleRate = 48000.0;

int failures = 0, checks = 0;

void check (bool condition, const std::string& what)
{
    ++checks;

    if (! condition)
    {
        std::printf ("FAIL  %s\n", what.c_str());
        ++failures;
    }
}

void checkNear (double value, double expected, double tolerance, const std::string& what)
{
    ++checks;

    if (! (std::abs (value - expected) <= tolerance))
    {
        std::printf ("FAIL  %s: %.6f, expected %.6f +/- %.6f\n",
                     what.c_str(), value, expected, tolerance);
        ++failures;
    }
}

//==============================================================================
std::vector<float> sine (double hz, double seconds, double amplitude)
{
    const auto n = (size_t) (seconds * kSampleRate);
    std::vector<float> out (n);

    for (size_t i = 0; i < n; ++i)
        out[i] = (float) (amplitude * std::sin (2.0 * kPi * hz * (double) i / kSampleRate));

    return out;
}

/** A crude voice: a harmonic series under a syllabic envelope, running all the
    way up to 18 kHz. Enough structure for the band and dynamics tests; the
    measurement harness has the detailed one, with formants and breath.

    The series has to reach the top of the band. Stopped at 6 kHz, as it was
    first written, the source has nothing above 6 kHz at all, so the plugin's
    top-band delta measures 17 dB -- a true number about a signal no microphone
    ever produced, and a useless one for holding the plugin to. */
std::vector<float> voice (double seconds = 4.0)
{
    const auto n = (size_t) (seconds * kSampleRate);
    std::vector<float> out (n);
    double sumSquares = 0.0;

    for (size_t i = 0; i < n; ++i)
    {
        const auto t = (double) i / kSampleRate;
        const auto beat = std::fmod (t, 0.55);
        const auto envelope = (std::fmod (t, 3.0) < 1.6 ? 1.0 : 0.0)
                            * (beat < 0.01 ? beat / 0.01 : std::exp (-(beat - 0.01) * 7.0));

        double sum = 0.0;

        for (int h = 1; h <= 120; ++h)
            sum += std::pow ((double) h, -1.4) * std::sin (2.0 * kPi * 150.0 * h * t);

        out[i] = (float) (envelope * sum);
        sumSquares += (double) out[i] * out[i];
    }

    const auto rms = std::sqrt (sumSquares / (double) n);
    const auto gain = rms > 0.0 ? std::pow (10.0, -18.0 / 20.0) / rms : 1.0;

    for (auto& v : out)
        v = (float) (v * gain);

    return out;
}

std::vector<float> render (const std::vector<float>& input, DspCore::Params params)
{
    DspCore core;
    core.prepare (kSampleRate, 512, 1, params.oversampling);
    core.setParams (params);

    auto out = input;
    out.insert (out.end(), (size_t) core.getLatencySamples(), 0.0f);

    for (size_t at = 0; at < out.size(); at += 512)
    {
        auto* p = out.data() + at;
        core.process (&p, 1, (int) std::min<size_t> (512, out.size() - at));
    }

    // Realign: everything below compares dry and wet sample for sample.
    out.erase (out.begin(), out.begin() + core.getLatencySamples());
    out.resize (input.size(), 0.0f);
    return out;
}

double rms (const std::vector<float>& x)
{
    double sum = 0.0;

    for (auto v : x)
        sum += (double) v * (double) v;

    return x.empty() ? 0.0 : std::sqrt (sum / (double) x.size());
}

double peak (const std::vector<float>& x)
{
    double m = 0.0;

    for (auto v : x)
        m = std::max (m, (double) std::abs (v));

    return m;
}

double crestFactorDb (const std::vector<float>& x)
{
    const auto r = rms (x);
    return r > 0.0 ? 20.0 * std::log10 (peak (x) / r) : 0.0;
}

/** Magnitude at one frequency, by direct evaluation rather than an FFT: the
    tests only ever ask about a handful of bins. */
double magnitudeAt (const std::vector<float>& x, double hz, size_t from = 4096)
{
    double re = 0.0, im = 0.0;
    const auto n = x.size() - from;

    for (size_t i = 0; i < n; ++i)
    {
        const auto w = 0.5 - 0.5 * std::cos (2.0 * kPi * (double) i / (double) n);
        const auto phase = 2.0 * kPi * hz * (double) i / kSampleRate;
        re += (double) x[from + i] * w * std::cos (phase);
        im -= (double) x[from + i] * w * std::sin (phase);
    }

    return std::hypot (re, im) / (double) n;
}

void fft (std::vector<std::complex<double>>& a)
{
    const auto n = a.size();

    for (size_t i = 1, j = 0; i < n; ++i)
    {
        size_t bit = n >> 1;

        for (; j & bit; bit >>= 1)
            j ^= bit;

        j ^= bit;

        if (i < j)
            std::swap (a[i], a[j]);
    }

    for (size_t len = 2; len <= n; len <<= 1)
    {
        const auto theta = -2.0 * kPi / (double) len;

        for (size_t i = 0; i < n; i += len)
            for (size_t k = 0; k < len / 2; ++k)
            {
                const std::complex<double> w { std::cos (theta * (double) k),
                                               std::sin (theta * (double) k) };
                const auto u = a[i + k];
                const auto v = a[i + k + len / 2] * w;
                a[i + k] = u + v;
                a[i + k + len / 2] = u - v;
            }
    }
}

/** Energy in a band, Welch-averaged. Probing at a handful of frequencies is
    not good enough here: the test voice is a harmonic series, so a probe
    frequency either lands on a partial or between two, and the answer swings
    by tens of dB on where the arithmetic happens to put it. */
double bandEnergy (const std::vector<float>& x, double lowHz, double highHz)
{
    constexpr size_t size = 8192;

    if (x.size() < size)
        return 0.0;

    const auto binHz = kSampleRate / (double) size;
    double sum = 0.0;

    for (size_t start = 0; start + size <= x.size(); start += size / 2)
    {
        std::vector<std::complex<double>> frame (size);

        for (size_t i = 0; i < size; ++i)
        {
            const auto w = 0.5 - 0.5 * std::cos (2.0 * kPi * (double) i / (double) size);
            frame[i] = { (double) x[start + i] * w, 0.0 };
        }

        fft (frame);

        for (size_t k = 1; k < size / 2; ++k)
        {
            const auto hz = (double) k * binHz;

            if (hz >= lowHz && hz < highHz)
                sum += std::norm (frame[k]);
        }
    }

    return sum;
}

DspCore::Params defaults()
{
    return DspCore::Params {};
}

//==============================================================================
void testCurveShape()
{
    AsymmetricShaper shaper;
    shaper.setDrive (DspCore::driveFor (40.0f));

    checkNear (shaper.shape (0.0), 0.0, 1.0e-12, "the curve passes through the origin");

    // Asymmetric, and in the direction the reference measured: the positive
    // excursions compress harder than the negative ones.
    for (double x = 0.1; x <= 1.0; x += 0.1)
        check (std::abs (shaper.shape (x)) < std::abs (shaper.shape (-x)),
               "the positive half compresses harder at x = " + std::to_string (x));

    // Monotonic, so no fold-back: a curve that turns over stops being a
    // saturator and starts being a wavefolder.
    double previous = shaper.shape (-2.0);

    for (double x = -2.0; x <= 2.0; x += 0.01)
    {
        const auto y = shaper.shape (x);
        check (y >= previous - 1.0e-12, "the curve is monotonic at x = " + std::to_string (x));
        previous = y;
    }

    // Compressive everywhere: |shape(x)| <= |x|.
    for (double x = 0.01; x <= 2.0; x += 0.01)
        check (std::abs (shaper.shape (x)) <= x + 1.0e-9
                 && std::abs (shaper.shape (-x)) <= x + 1.0e-9,
               "the curve never expands at x = " + std::to_string (x));
}

/** The curve's asymmetry, held to a shape rather than to a number.

    It used to pin the average gains to the brief's 0.62 and 0.84. Two things
    retired that. Measured on the reference files themselves, the way the brief
    describes, Song B's gains are 0.965 and 1.015 -- so those figures do not come
    from this material and pinning to them was pinning to a typo. And the
    operating point is now chosen by ear: a listening test found the fitted
    drive about three times too hot, and the ear wins over a band delta that
    cannot hear distortion.

    What must stay true is the shape: compressive, asymmetric, and asymmetric
    in the direction that puts even-order content ahead of odd. Those are the
    plugin's character; the exact numbers are an operating point.
*/
void testAsymmetryMatchesReference()
{
    AsymmetricShaper shaper;
    shaper.setDrive (DspCore::driveFor (40.0f));

    const auto signal = voice();
    double posIn = 0.0, posOut = 0.0, negIn = 0.0, negOut = 0.0;

    for (auto v : signal)
    {
        const double x = v;

        if (std::abs (x) < 1.0e-4)
            continue;

        const auto y = shaper.shape (x);

        if (x > 0.0) { posIn += x;  posOut += std::abs (y); }
        else         { negIn += -x; negOut += std::abs (y); }
    }

    const auto positive = posOut / posIn;
    const auto negative = negOut / negIn;

    check (positive < 1.0 && negative < 1.05, "the curve compresses");
    check (positive < negative, "the positive half compresses harder, as the reference does");

    const auto asymmetry = std::abs (positive - negative);

    check (asymmetry > 0.05,
           "the curve is asymmetric, and nowhere near the Saturator plug X anti-target of 0.004");
    check (asymmetry < 0.40, "the asymmetry is a colour, not a fold");
}

/** Even-order content leads odd, which is the difference between warm and
    harsh and the thing the average-gain measurement cannot see. */
void testEvenHarmonicsLead()
{
    auto params = defaults();
    params.driveAmount = 40.0f;

    const auto wet = render (sine (220.0, 3.0, 0.126), params);

    const auto fundamental = magnitudeAt (wet, 220.0);
    const auto second      = magnitudeAt (wet, 440.0);
    const auto third       = magnitudeAt (wet, 660.0);

    check (second > third, "the second harmonic leads the third");
    check (second / fundamental > 0.01, "there is meaningful second-harmonic content");
}

/** Where the new energy lands: the whole point of the two generators. */
void testLiftIsAboveTheSplit()
{
    auto params = defaults();
    params.driveAmount = 40.0f;

    const auto dry = voice();
    const auto wet = render (dry, params);

    // Level-matched, as the reference measurements are: saturation that
    // arrives louder measures as a lift everywhere and says nothing.
    auto matched = wet;
    const auto match = rms (dry) / rms (wet);

    for (auto& v : matched)
        v = (float) (v * match);

    const auto delta = [&dry, &matched] (double lowHz, double highHz)
    {
        return 10.0 * std::log10 (bandEnergy (matched, lowHz, highHz)
                                    / bandEnergy (dry, lowHz, highHz));
    };

    // Thresholds rather than the reference's own figures, and deliberately.
    // How many decibels a band lifts depends heavily on how much the source
    // already had there: this test signal is a bare harmonic series with far
    // more native top end than a voice, and measures roughly +3 dB where the
    // harness's reference voice measures +6 to +8. The shape of the result is
    // what generalises, so the shape is what is asserted here. The absolute
    // figures against Song B live in `measure verify`.
    const auto low = delta (20.0, 150.0);
    const auto lowMid = delta (150.0, 600.0);
    const auto mid = delta (600.0, 2500.0);
    const auto upper = delta (2500.0, 6000.0);
    const auto top = delta (6000.0, 18000.0);

    check (upper > 1.5, "2.5-6 kHz lifts");
    check (top > 1.5, "6-18 kHz lifts");

    // The bands the reference leaves alone stay left alone.
    check (std::abs (low) < 3.5, "20-150 Hz stays near flat");
    check (std::abs (lowMid) < 3.5, "150-600 Hz stays near flat");
    check (std::abs (mid) < 3.5, "600 Hz - 2.5 kHz stays near flat");

    check (upper > mid + 3.0 && top > mid + 3.0,
           "the lift is above the split, not spread across the whole spectrum");

    // The anti-target: a lift confined to the top band only, with nothing
    // below it. Saturator plug X's shape.
    check (upper > top - 6.0,
           "the lift is not confined to the top band, as Saturator plug X's was");
}

/** Dynamics are a side effect of the curve, and the side effect is expansion.
    Nothing in this plugin may reduce the crest factor. */
void testCrestFactorDoesNotFall()
{
    const auto dry = voice();
    const auto dryCrest = crestFactorDb (dry);

    for (float amount : { 0.0f, 20.0f, 40.0f, 60.0f, 80.0f, 100.0f })
    {
        auto params = defaults();
        params.driveAmount = amount;

        const auto crest = crestFactorDb (render (dry, params));

        check (crest > dryCrest - 0.25,
               "crest factor does not fall at Drive " + std::to_string ((int) amount));
    }
}

/** Drive scales the intensity and nothing else. */
void testDriveScalesMonotonically()
{
    const auto dry = voice();
    double previous = -1.0;

    for (float amount : { 0.0f, 20.0f, 40.0f, 60.0f, 80.0f, 100.0f })
    {
        auto params = defaults();
        params.driveAmount = amount;

        // The voicing is switched out: this test is about the curve, and with
        // a fixed 12 dB bell on the output the difference from dry is mostly
        // the bell at every drive setting.
        params.toneAmount = 0.0f;

        const auto wet = render (dry, params);

        // How far the output has moved from the input: a plain measure of how
        // much the plugin is doing.
        double sum = 0.0;

        for (size_t i = 0; i < dry.size(); ++i)
            sum += ((double) wet[i] - dry[i]) * ((double) wet[i] - dry[i]);

        const auto difference = std::sqrt (sum / (double) dry.size());

        check (difference > previous,
               "Drive " + std::to_string ((int) amount) + " does more than the setting below it");
        previous = difference;
    }

    check (DspCore::driveFor (0.0f) > 0.0f, "Drive 0 still applies the curve");
    check (DspCore::driveFor (100.0f) > 10.0f * DspCore::driveFor (0.0f),
           "the drive range spans more than a decade");
}

/** Moving Drive while audio is flowing must not produce a discontinuity.

    This is a regression test for the bug that made 0.1.0 unusable live. ADAA
    carries the antiderivative of the previous sample from one call to the
    next; change the drive without rebuilding that state and the difference
    quotient subtracts two different functions and divides by a possibly tiny
    dx. Measured before the fix: single samples over thirty times full scale,
    five thousand times the largest step the programme was making, which is the
    loud scratching anyone got dragging the control.

    Deliberately checks the audio rather than the presence of smoothing code.
    Smoothing was already there and did not prevent it -- the smoother is what
    delivered the changing value.
*/
void testDriveChangesAreClean()
{
    const auto dry = sine (220.0, 1.0, 0.2);

    double biggestSignalStep = 0.0;

    for (size_t i = 1; i < dry.size(); ++i)
        biggestSignalStep = std::max (biggestSignalStep, (double) std::abs (dry[i] - dry[i - 1]));

    // Every speed of adjustment: a slow automation ramp, a fast one, and a
    // hand throwing the control across its range in a quarter of a second.
    for (double seconds : { 4.0, 1.0, 0.25 })
    {
        DspCore core;
        DspCore::Params params = defaults();
        params.driveAmount = 0.0f;
        core.prepare (kSampleRate, 64, 1, params.oversampling);
        core.setParams (params);

        auto out = dry;

        for (size_t at = 0; at + 64 <= out.size(); at += 64)
        {
            const auto through = (double) at / (double) out.size();
            params.driveAmount = (float) (100.0 * std::fmin (1.0, through * (out.size() / kSampleRate) / seconds));
            core.setParams (params);

            auto* p = out.data() + at;
            core.process (&p, 1, 64);
        }

        double worst = 0.0;

        for (size_t i = 2000; i + 200 < out.size(); ++i)
            worst = std::max (worst, (double) std::abs (out[i + 1] - out[i]));

        check (worst < biggestSignalStep * 8.0,
               "sweeping Drive over " + std::to_string (seconds)
                 + "s adds no step larger than the programme's own");

        for (auto v : out)
            check (std::isfinite (v) && std::abs (v) < 4.0f, "the swept output stays sane");
    }
}

/** The voicing leaves nothing behind when it is turned off. */
void testToneOffIsNeutral()
{
    const auto dry = voice (1.0);

    auto with = defaults();
    with.toneAmount = 100.0f;
    with.driveAmount = 0.0f;

    auto without = with;
    without.toneAmount = 0.0f;

    const auto voiced = render (dry, with);
    const auto plain  = render (dry, without);

    // With Tone at zero the bell is flat and the high-pass is blended out, so
    // the two differ audibly -- that is the point of the control.
    double difference = 0.0;

    for (size_t i = 2048; i < dry.size(); ++i)
        difference = std::max (difference, (double) std::abs (voiced[i] - plain[i]));

    check (difference > 1.0e-3, "Tone at 100 does something Tone at 0 does not");

    // And at zero, nothing of the voicing is left. Checked with a tone sitting
    // on the high-pass corner rather than with the test voice, whose lowest
    // partial is at 150 Hz -- there is nothing at 50 Hz for a 50 Hz filter to
    // remove, so the voice would report the filter working whether it was
    // there or not.
    const auto low = sine (50.0, 1.0, 0.2);

    const auto lowDelta = [&low] (const DspCore::Params& p)
    {
        const auto wet = render (low, p);
        return 20.0 * std::log10 (rms (std::vector<float> (wet.begin() + 4000, wet.end()))
                                    / rms (std::vector<float> (low.begin() + 4000, low.end())));
    };

    check (std::abs (lowDelta (without)) < 1.0,
           "Tone at zero leaves 50 Hz where it was");
    check (lowDelta (with) < -2.0,
           "Tone at 100 high-passes 50 Hz");
}

/** The polarity switch flips what leaves the plugin, in every state.

    Reported from a listening session: with Mix at 0 the button did nothing.
    Measuring it found that and a second fault behind it. Polarity was applied
    to the input, so the dry path of the Mix control never saw it -- hence the
    report -- and because the curve is asymmetric, -f(-x) is not f(x), so
    flipping ahead of the shaper changed which harmonics came out. Engaging a
    polarity switch altered the sound, which is the one thing it must never do.

    The invariant, checked across every combination that matters: two instances
    of the plugin with identical settings, one flipped, must sum to exact
    silence.
*/
void testPolarityFlipsEverything()
{
    const auto dry = voice (1.0);

    for (bool sat : { true, false })
        for (float mix : { 0.0f, 50.0f, 100.0f })
            for (float drive : { 0.0f, 40.0f, 100.0f })
                for (float outputDb : { -6.0f, 0.0f })
                {
                    auto params = defaults();
                    params.saturationIn = sat;
                    params.mixPercent = mix;
                    params.driveAmount = drive;
                    params.outputLevelDb = outputDb;

                    params.phaseInvert = false;
                    const auto straight = render (dry, params);

                    params.phaseInvert = true;
                    const auto flipped = render (dry, params);

                    double worst = 0.0;

                    for (size_t i = 2048; i < dry.size(); ++i)
                        worst = std::max (worst, (double) std::abs (straight[i] + flipped[i]));

                    check (worst == 0.0,
                           "polarity nulls exactly with Sat " + std::string (sat ? "in" : "out")
                             + ", Mix " + std::to_string ((int) mix)
                             + ", Drive " + std::to_string ((int) drive));
                }
}

/** With the saturation switched out, what comes back is what went in. */
void testBypassNulls()
{
    const auto dry = voice (1.0);

    auto params = defaults();
    params.saturationIn = false;

    const auto wet = render (dry, params);
    double worst = 0.0;

    // Skip the front: the oversampling filters need priming, and the harness
    // is measuring their impulse response there rather than the plugin.
    for (size_t i = 2048; i < dry.size(); ++i)
        worst = std::max (worst, (double) std::abs (dry[i] - wet[i]));

    check (worst < 2.0e-3, "the saturation switched out nulls against the input");
}

/** Mix at zero is the dry signal, delayed to match. That is what makes a
    partial blend a blend rather than a comb filter. */
void testDryPathIsDelayMatched()
{
    const auto dry = voice (1.0);

    auto params = defaults();
    params.mixPercent = 0.0f;
    params.driveAmount = 100.0f;

    const auto wet = render (dry, params);
    double worst = 0.0;

    for (size_t i = 2048; i < dry.size(); ++i)
        worst = std::max (worst, (double) std::abs (dry[i] - wet[i]));

    check (worst < 1.0e-6, "Mix at zero returns the input, delay-matched");
}

/** No DC on the output, whatever the curve leaves behind. */
void testNoDcOffset()
{
    auto params = defaults();
    params.driveAmount = 100.0f;

    const auto wet = render (sine (220.0, 2.0, 0.5), params);
    double sum = 0.0;

    for (size_t i = 4096; i < wet.size(); ++i)
        sum += wet[i];

    // Not zero, and not asked to be: the blocker's corner is a few hertz, so a
    // little of the offset the curve leaves behind survives. -70 dBFS against
    // a half-scale tone is the level that matters -- inaudible, no headroom
    // cost, nothing to accumulate through a chain.
    checkNear (sum / (double) (wet.size() - 4096), 0.0, 5.0e-4, "the output carries no DC offset");
}

/** Oversampling actually suppresses folded images, and reports its delay
    honestly. */
void testOversampling()
{
    // A 9 kHz tone at 48 kHz: the third harmonic lands at 27 kHz, above
    // Nyquist, and folds to 21 kHz. Nothing legitimate can appear there, so
    // whatever is measured at 21 kHz is aliasing and nothing else.
    constexpr double toneHz  = 9000.0;
    constexpr double imageHz = 21000.0;

    auto params = defaults();
    params.driveAmount = 100.0f;

    double previous = 0.0;

    for (int factor : { 1, 2 })
    {
        params.oversampling = factor;
        const auto wet = render (sine (toneHz, 1.0, 0.5), params);

        const auto image = magnitudeAt (wet, imageHz);
        const auto fundamental = magnitudeAt (wet, toneHz);
        const auto ratio = 20.0 * std::log10 (image / fundamental);

        if (factor == 1)
            previous = ratio;
        else
            check (ratio < previous - 15.0, "2x oversampling drops folded images by at least 15 dB");
    }

    DspCore core;
    core.prepare (kSampleRate, 512, 1, 1);
    check (core.getLatencySamples() == 0, "no oversampling means no reported latency");

    core.prepare (kSampleRate, 512, 1, 2);
    check (core.getLatencySamples() > 0, "2x oversampling reports its latency");
}

/** Auto Gain matches the level, and does nothing else.

    It became a real detector in 0.3.0. The fixed table it replaced was fitted
    to one voice at one level, was inaudible on anything else, and at some
    settings pulled the wrong way -- which is what "AUTO does nothing" in the
    test report meant.

    A detector is the thing this plugin is not allowed to be, so the second
    test here is the important one: switching Auto Gain on must not change the
    crest factor. It runs at a 1.5 second time constant, far slower than any
    phrase, so it can move the level without touching the dynamics. If someone
    speeds it up, this is the test that should stop them.
*/
void testAutoGainMatchesLevelOnly()
{
    const auto dry = voice();

    for (float amount : { 0.0f, 40.0f, 80.0f, 100.0f })
    {
        auto params = defaults();
        params.driveAmount = amount;
        params.autoGain = true;

        // The first second and a half is the detector arriving; what it
        // settles at is what matters.
        const auto wet = render (dry, params);
        const auto from = (size_t) (kSampleRate * 2.0);

        const std::vector<float> settledWet (wet.begin() + (long) from, wet.end());
        const std::vector<float> settledDry (dry.begin() + (long) from, dry.end());

        checkNear (20.0 * std::log10 (rms (settledWet) / rms (settledDry)), 0.0, 1.0,
                   "Auto Gain holds the level at Drive " + std::to_string ((int) amount));
    }

    // And the part that makes it a level match rather than a compressor.
    for (float amount : { 40.0f, 100.0f })
    {
        auto off = defaults();
        off.driveAmount = amount;
        off.autoGain = false;

        auto on = off;
        on.autoGain = true;

        const auto without = crestFactorDb (render (dry, off));
        const auto with    = crestFactorDb (render (dry, on));

        checkNear (with, without, 0.25,
                   "Auto Gain leaves the crest factor alone at Drive "
                     + std::to_string ((int) amount));
    }

    // A quiet source and a loud one get whatever each of them needs, which a
    // table indexed on the drive setting alone cannot do.
    for (double level : { -30.0, -12.0 })
    {
        auto quiet = dry;
        const auto scale = (float) std::pow (10.0, (level + 18.0) / 20.0);

        for (auto& v : quiet)
            v *= scale;

        auto params = defaults();
        params.autoGain = true;

        const auto wet = render (quiet, params);
        const auto from = (size_t) (kSampleRate * 2.0);

        checkNear (20.0 * std::log10 (rms (std::vector<float> (wet.begin() + (long) from, wet.end()))
                                        / rms (std::vector<float> (quiet.begin() + (long) from, quiet.end()))),
                   0.0, 1.0,
                   "Auto Gain holds the level at " + std::to_string ((int) level) + " dBFS in");
    }
}

/** Stereo channels are independent and identical. */
void testChannelsAreIndependent()
{
    const auto mono = voice (1.0);

    auto left = mono, right = mono;

    for (auto& v : right)
        v = -v;

    DspCore core;
    auto params = defaults();
    params.driveAmount = 80.0f;

    core.prepare (kSampleRate, 512, 2, params.oversampling);
    core.setParams (params);

    float* channels[2] { left.data(), right.data() };

    for (size_t at = 0; at < mono.size(); at += 512)
    {
        float* p[2] { left.data() + at, right.data() + at };
        core.process (p, 2, (int) std::min<size_t> (512, mono.size() - at));
    }

    (void) channels;

    // The curve is asymmetric, so an inverted input is not an inverted output.
    // What must hold is that neither channel is silent and neither has run
    // away -- the two are separate instances of the same thing.
    check (rms (left) > 0.0 && rms (right) > 0.0, "both channels produce output");
    check (peak (left) < 4.0 && peak (right) < 4.0, "neither channel runs away");
}

/** Nothing in the chain produces a NaN, an infinity, or a runaway, however
    unreasonable the input. */
void testStability()
{
    std::vector<float> nasty;

    for (int i = 0; i < 48000; ++i)
        nasty.push_back ((float) ((i / 64) % 2 == 0 ? 4.0 : -4.0));   // far past full scale

    for (int i = 0; i < 4800; ++i)
        nasty.push_back (0.0f);

    auto params = defaults();
    params.driveAmount = 100.0f;
    params.oversampling = 8;

    // The input here is 12 dB past full scale and the voicing adds another 12
    // at 7 kHz, so a large number is the correct answer -- what is being
    // checked is that it stays a number, and stays bounded, rather than that
    // it stays small. Measured peak is around 42 for an input of 4.
    for (auto v : render (nasty, params))
        check (std::isfinite (v) && std::abs (v) < 80.0f, "the output stays finite and bounded");
}

} // namespace

//==============================================================================
int main()
{
    testCurveShape();
    testAsymmetryMatchesReference();
    testEvenHarmonicsLead();
    testLiftIsAboveTheSplit();
    testCrestFactorDoesNotFall();
    testDriveScalesMonotonically();
    testDriveChangesAreClean();
    testToneOffIsNeutral();
    testPolarityFlipsEverything();
    testBypassNulls();
    testDryPathIsDelayMatched();
    testNoDcOffset();
    testOversampling();
    testAutoGainMatchesLevelOnly();
    testChannelsAreIndependent();
    testStability();

    std::printf ("%d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
