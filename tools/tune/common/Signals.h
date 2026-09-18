#pragma once

/*
    Synthetic test signals with exact ground truth (spec T-2).

    Every voiced signal here is additive: a phase accumulator driven by a
    per-sample f0 contour, summed over harmonics below Nyquist. That makes
    three things exact rather than estimated -- the instantaneous f0 (it is the
    contour), band-limitation (no harmonic above fs/2 is ever generated, so
    the aliasing tests measure the plugin and not the test signal), and the
    epochs (the samples where the fundamental's phase wraps), which the
    pitch-mark tests score against.

    Shared by tests/ and tools/gen, so a corpus WAV and a unit test are always
    looking at the same waveform. Deterministic: the only randomness is a
    seeded xorshift, never std::random_device.
*/

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <vector>

namespace bmo::tune::signals
{

inline constexpr double kPi = 3.14159265358979323846;

//==============================================================================
/** xorshift64*: tiny, fast, and identical on every platform and compiler,
    which std::normal_distribution is not. */
class Random
{
public:
    explicit Random (std::uint64_t seed = 0x9E3779B97F4A7C15ull) : state (seed ? seed : 1) {}

    std::uint64_t next() noexcept
    {
        state ^= state >> 12;
        state ^= state << 25;
        state ^= state >> 27;
        return state * 0x2545F4914F6CDD1Dull;
    }

    /** Uniform in [-1, 1). */
    double uniform() noexcept { return (double) (next() >> 11) * (2.0 / 9007199254740992.0) - 1.0; }

    /** Approximately Gaussian, unit variance: the sum of four uniforms. */
    double gaussian() noexcept
    {
        return (uniform() + uniform() + uniform() + uniform()) * std::sqrt (3.0) * 0.5;
    }

private:
    std::uint64_t state;
};

//==============================================================================
// Contours: f0 in Hz per sample. Zero means unvoiced (the harmonic source is
// silent there, and scoring treats the frame as unvoiced ground truth).

using Contour = std::vector<double>;

inline Contour steady (double hz, double seconds, double fs)
{
    return Contour ((size_t) std::lround (seconds * fs), hz);
}

/** A glide from a to b, linear in semitones (which is how a voice moves). */
inline Contour glide (double hzA, double hzB, double seconds, double fs)
{
    const auto n = (size_t) std::lround (seconds * fs);
    Contour c (n);
    for (size_t i = 0; i < n; ++i)
        c[i] = hzA * std::pow (hzB / hzA, (double) i / (double) std::max<size_t> (1, n - 1));
    return c;
}

inline Contour linearSweep (double hzA, double hzB, double seconds, double fs)
{
    const auto n = (size_t) std::lround (seconds * fs);
    Contour c (n);
    for (size_t i = 0; i < n; ++i)
        c[i] = hzA + (hzB - hzA) * (double) i / (double) std::max<size_t> (1, n - 1);
    return c;
}

/** Sinusoidal vibrato of +/- depthCents around a centre, at rateHz. */
inline Contour vibrato (double centreHz, double depthCents, double rateHz, double seconds, double fs)
{
    const auto n = (size_t) std::lround (seconds * fs);
    Contour c (n);
    for (size_t i = 0; i < n; ++i)
        c[i] = centreHz * std::exp2 (depthCents / 1200.0 * std::sin (2.0 * kPi * rateHz * (double) i / fs));
    return c;
}

/** A note step: `a` for half the time, then `b`, with a glide of glideMs
    between them (0 = instant). */
inline Contour step (double hzA, double hzB, double seconds, double glideMs, double fs)
{
    const auto n = (size_t) std::lround (seconds * fs);
    const auto half = n / 2;
    const auto g = (size_t) std::lround (glideMs * 0.001 * fs);
    Contour c (n);

    for (size_t i = 0; i < n; ++i)
    {
        if (i < half)
            c[i] = hzA;
        else if (i >= half + g)
            c[i] = hzB;
        else
            c[i] = hzA * std::pow (hzB / hzA, (double) (i - half) / (double) std::max<size_t> (1, g));
    }

    return c;
}

inline Contour silence (double seconds, double fs)
{
    return Contour ((size_t) std::lround (seconds * fs), 0.0);
}

inline Contour concat (std::initializer_list<Contour> parts)
{
    Contour out;
    for (const auto& p : parts)
        out.insert (out.end(), p.begin(), p.end());
    return out;
}

//==============================================================================
/** The additive harmonic source. `amplitude(k)` is harmonic k's weight
    (k from 1); harmonics at or above `nyquistFraction * fs/2` are skipped
    per sample, so a gliding tone gains and loses its top harmonic cleanly.

    Epochs are written where the fundamental's phase wraps -- for the glottal
    source below that is the instant of closure, by construction. */
struct Rendered
{
    std::vector<float> samples;
    std::vector<long long> epochs;
};

inline Rendered harmonic (const Contour& hz, double fs,
                          const std::function<double (int)>& amplitude,
                          double gain = 0.5, double nyquistFraction = 0.98,
                          double startPhase = 0.0)
{
    Rendered r;
    r.samples.assign (hz.size(), 0.0f);

    // Precompute weights up to the most harmonics any sample can carry.
    double lowest = 1.0e9;
    for (auto f : hz)
        if (f > 0.0)
            lowest = std::min (lowest, f);

    const auto maxHarmonics = lowest < 1.0e9 ? (int) (0.5 * fs / lowest) + 1 : 0;
    std::vector<double> weight ((size_t) maxHarmonics + 1, 0.0);
    double norm = 0.0;

    for (int k = 1; k <= maxHarmonics; ++k)
    {
        weight[(size_t) k] = amplitude (k);
        norm += std::abs (weight[(size_t) k]);
    }

    norm = norm > 0.0 ? 1.0 / norm : 0.0;

    double phase = startPhase;   // in cycles

    for (size_t i = 0; i < hz.size(); ++i)
    {
        const auto f = hz[i];

        if (f <= 0.0)
            continue;

        const auto limit = nyquistFraction * 0.5 * fs;
        double sum = 0.0;

        for (int k = 1; k <= maxHarmonics && k * f < limit; ++k)
            sum += weight[(size_t) k] * std::sin (2.0 * kPi * k * phase);

        r.samples[i] = (float) (gain * sum * norm * 2.0);

        const auto before = phase;
        phase += f / fs;

        if (std::floor (phase) != std::floor (before))
            r.epochs.push_back ((long long) i + 1);

        if (phase > 1.0e6)
            phase -= std::floor (phase);
    }

    return r;
}

inline Rendered sine (const Contour& hz, double fs, double gain = 0.5)
{
    return harmonic (hz, fs, [] (int k) { return k == 1 ? 1.0 : 0.0; }, gain * 0.5);
}

/** Band-limited sawtooth: 1/k. */
inline Rendered sawtooth (const Contour& hz, double fs, double gain = 0.5)
{
    return harmonic (hz, fs, [] (int k) { return 1.0 / k; }, gain, 0.999);
}

//==============================================================================
/** A source-filter voice: a glottal-like source (harmonics falling 12 dB per
    octave, which is the Rosenberg/LF pulse's long-run slope) through a bank
    of formant resonators. Jitter perturbs f0 per period, shimmer the level,
    and breath adds noise -- all seeded. */
struct VoiceSettings
{
    double formants[5]   { 700.0, 1220.0, 2600.0, 3300.0, 3750.0 };  // /a/, male
    double bandwidths[5] { 80.0, 90.0, 120.0, 150.0, 200.0 };
    int numFormants = 5;
    double jitter = 0.0;         ///< fractional f0 deviation per period, e.g. 0.005
    double shimmer = 0.0;        ///< fractional amplitude deviation per period
    double breath = 0.0;         ///< noise level relative to the voiced source
    /** The fundamental's level against the source's usual 1/k^2, in dB. A
        real voice can carry a fundamental well under its second harmonic --
        the Failure take of the 2026-09-11 shoot-out does, on a D4 /a/ whose
        first formant sits on the octave -- and a detector that has only met
        strong fundamentals locks an octave up on it. -20 puts it 8 dB under
        the second harmonic at the source, before the formants. */
    double fundamentalDb = 0.0;
    double gain = 0.4;
    std::uint64_t seed = 1234;
};

/** Applies jitter to a contour period by period, so the ground truth that
    comes back is the jittered f0 the source actually used. */
inline Contour withJitter (const Contour& hz, double jitter, double fs, std::uint64_t seed)
{
    if (jitter <= 0.0)
        return hz;

    Random rng (seed);
    Contour out = hz;
    double phase = 0.0, factor = 1.0;

    for (size_t i = 0; i < out.size(); ++i)
    {
        if (hz[i] <= 0.0)
            continue;

        out[i] = hz[i] * factor;
        const auto before = phase;
        phase += out[i] / fs;

        if (std::floor (phase) != std::floor (before))
            factor = 1.0 + jitter * rng.gaussian();
    }

    return out;
}

/** A two-pole resonator, peak-normalised, for the formant bank. */
inline std::vector<float> resonate (const std::vector<float>& x, double fs, double hz, double bw)
{
    const auto r = std::exp (-kPi * bw / fs);
    const auto a1 = -2.0 * r * std::cos (2.0 * kPi * hz / fs);
    const auto a2 = r * r;
    const auto g = (1.0 - r) * std::sqrt (1.0 - 2.0 * r * std::cos (4.0 * kPi * hz / fs) + r * r);

    std::vector<float> y (x.size());
    double y1 = 0.0, y2 = 0.0;

    for (size_t i = 0; i < x.size(); ++i)
    {
        const auto v = g * x[i] - a1 * y1 - a2 * y2;
        y2 = y1;
        y1 = v;
        y[i] = (float) v;
    }

    return y;
}

inline Rendered voice (const Contour& hz, double fs, const VoiceSettings& s = {})
{
    const auto h1 = std::pow (10.0, s.fundamentalDb / 20.0);
    auto src = harmonic (hz, fs, [h1] (int k) { return (k == 1 ? h1 : 1.0) / ((double) k * k); }, 1.0);

    Random rng (s.seed ^ 0xABCDEFull);

    if (s.shimmer > 0.0 && ! src.epochs.empty())
    {
        size_t e = 0;
        double level = 1.0;
        for (size_t i = 0; i < src.samples.size(); ++i)
        {
            while (e < src.epochs.size() && (long long) i >= src.epochs[e])
            {
                level = 1.0 + s.shimmer * rng.gaussian();
                ++e;
            }
            src.samples[i] = (float) (src.samples[i] * level);
        }
    }

    if (s.breath > 0.0)
        for (size_t i = 0; i < src.samples.size(); ++i)
            if (hz[i] > 0.0)
                src.samples[i] += (float) (s.breath * 0.3 * rng.uniform());

    // Parallel formant bank: each resonator gets the source, weighted down
    // with formant number the way a real vowel's upper formants fall.
    std::vector<float> sum (src.samples.size(), 0.0f);

    for (int f = 0; f < s.numFormants; ++f)
    {
        const auto band = resonate (src.samples, fs, s.formants[f], s.bandwidths[f]);
        const auto w = 1.0 / (1.0 + f);
        for (size_t i = 0; i < sum.size(); ++i)
            sum[i] += (float) (w * band[i]);
    }

    // Normalise to the requested peak over the voiced region.
    float peak = 0.0f;
    for (auto v : sum)
        peak = std::max (peak, std::abs (v));

    const auto scale = peak > 0.0f ? (float) s.gain / peak : 0.0f;
    for (auto& v : sum)
        v *= scale;

    return { sum, src.epochs };
}

//==============================================================================
// Contamination.

inline std::vector<float> whiteNoise (size_t n, double level, std::uint64_t seed = 99)
{
    Random rng (seed);
    std::vector<float> out (n);
    for (auto& v : out)
        v = (float) (level * rng.uniform());
    return out;
}

/** Paul Kellet's economy pink filter over white noise. */
inline std::vector<float> pinkNoise (size_t n, double level, std::uint64_t seed = 77)
{
    Random rng (seed);
    std::vector<float> out (n);
    double b0 = 0, b1 = 0, b2 = 0;

    for (auto& v : out)
    {
        const auto w = rng.uniform();
        b0 = 0.99765 * b0 + w * 0.0990460;
        b1 = 0.96300 * b1 + w * 0.2965164;
        b2 = 0.57000 * b2 + w * 1.0526913;
        v = (float) (level * 0.25 * (b0 + b1 + b2 + w * 0.1848));
    }

    return out;
}

inline double rms (const std::vector<float>& x)
{
    double s = 0.0;
    for (auto v : x)
        s += (double) v * v;
    return x.empty() ? 0.0 : std::sqrt (s / (double) x.size());
}

/** Adds noise scaled so the result sits at `snrDb` against the signal's rms
    over its non-silent samples. */
inline std::vector<float> atSnr (const std::vector<float>& signal, const std::vector<float>& noise, double snrDb)
{
    double s = 0.0; size_t count = 0;
    for (auto v : signal)
        if (v != 0.0f) { s += (double) v * v; ++count; }

    const auto signalRms = count ? std::sqrt (s / (double) count) : 0.0;
    const auto noiseRms = rms (noise);
    const auto scale = noiseRms > 0.0 ? signalRms / noiseRms * std::pow (10.0, -snrDb / 20.0) : 0.0;

    auto out = signal;
    for (size_t i = 0; i < out.size() && i < noise.size(); ++i)
        out[i] += (float) (scale * noise[i]);

    return out;
}

} // namespace bmo::tune::signals
