#pragma once

// BMO Linger's late network: the tail. 10 section 4 is the math and section 2
// the signal flow; 11 section 6's late block is what the tests ask of it
// (tests/dsp/ReverbDspTests.cpp). JUCE-free.
//
// One topology for every type: pre-delay (tail only), the input diffusers,
// then a feedback delay network of `N` lines mixed through a Hadamard
// matrix (not Householder; see `hadamard`), each line ending in an absorbent filter derived from the decay
// time and its two multipliers. A type changes constants and nothing else.
//
// **M3a builds the network without modulation.** The delay reads are at whole
// samples, which is what makes "bit-identical for fixed parameters" hold
// across block sizes; M3b's random modulators turn them into fractional
// reads, and the line lengths already carry the headroom for that.

#include "modules/reverb/params.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

namespace bmo::reverb
{

//==============================================================================
/** The per-type constants the late network reads that have no host lane and
    no place in `TypeConstants`, because nothing outside this file needs them.
    Type order is `kTypeNames`'. **All CALIBRATE** except the shape of the
    rule: 10 section 4 gives tau-bar per type and nothing else. */
struct LateTypeConstants
{
    float meanDelayMs;   ///< tau-bar at the type's own default SIZE
    int   allpasses;     ///< input diffusers: 2, or 4 for Plate (10 section 4)
};

inline constexpr LateTypeConstants kLateTypeConstants[numTypes]
{
    { 25.0f, 2 },   // Room
    { 35.0f, 2 },   // Chamber
    { 55.0f, 2 },   // Hall
    { 80.0f, 2 },   // Cavern
    { 18.0f, 4 },   // Plate
    { 20.0f, 2 },   // Ambience
};

//==============================================================================
/** What the late network is told, in engine units. */
struct LateConfig
{
    int   type         = 0;
    float sizeM        = roomDefaults::kSizeM;
    float preDelayMs   = 0.0f;                          ///< 0..250, the tail only
    float decaySeconds = 1.8f;                          ///< T_mid
    float dampLo       = 1.2f;                          ///< T60 multiplier below the low knee
    float dampHi       = 0.4f;                          ///< T60 multiplier above the high knee
    float loKneeHz     = roomDefaults::kDampLoFreqHz;
    float hiKneeHz     = roomDefaults::kDampHiFreqHz;
};

//==============================================================================
template <int N>
class LateNetwork
{
    static_assert (N >= 4 && (N & (N - 1)) == 0, "a power of two lines, at least four: the Hadamard matrix needs one");

public:
    static constexpr int kLines = N;

    /** Lines spread log-evenly over [tau-bar / kSpread, tau-bar * kSpread]
        (10 section 4). */
    static constexpr float kSpread = 1.3f;

    /** SIZE moves tau-bar in proportion from the type's own default SIZE --
        the late network "scales with the taps under SIZE" (11 section 1), and
        the taps' times are linear in it. CALIBRATE: it is a reading of the
        spec, not a figure from it. Held above kMinMeanDelayMs, below which a
        room is a comb filter rather than a room. */
    static constexpr float kMinMeanDelayMs = 5.0f;

    /** The absorbent filters' ceiling, 10 section 4's clamp. */
    static constexpr float kMaxLoopGain = 1.0f - 1.0e-4f;

    /** Pre-delay's range, and the 30 ms raised-cosine crossfade that SIZE and
        PRE-DELAY share with the ER generator (10 section 3). TYPE dips the
        tail over twice that, down and up, and swaps the lengths at the
        bottom. */
    static constexpr float kMaxPreDelayMs = 250.0f;
    static constexpr float kCrossfadeMs   = 30.0f;
    static constexpr float kSizeRetrigger = 0.01f;

    /** Coefficient smoothing for DECAY and the multipliers, 10 section 5. */
    static constexpr float kSmoothingMs = 20.0f;

    /** The input diffusers' delays and gain: 10 section 4's 10-35 ms at
        g of about 0.62-0.70. Fixed, not scaled by SIZE. CALIBRATE. */
    static constexpr float kAllpassMs[4] { 11.3f, 17.9f, 24.7f, 33.1f };
    static constexpr float kAllpassGain = 0.66f;

    /** **A diffuser may not ring longer than the room.** An allpass at g and
        delay d decays by 20 log g dB every d, so at 0.66 and 18 ms it has a
        T60 of 0.3 s on its own -- which at DECAY 0.3 s measured as 0.36 s, the
        diffusers outlasting the tail they feed. Each one's gain is held so its
        own T60 is at most this fraction of DECAY; above about 1.1 s of DECAY
        the cap never bites and the diffusers run at kAllpassGain. */
    static constexpr float kAllpassRingFraction = 0.5f;

    /** The shelves sit half an octave outside their knees: centred on the
        knee, a shelf is only half way there, and 10 section 4 says T60 *is*
        T_mid * r below the low knee and above the high one. Half an octave
        out puts the plateau at the knee and keeps the mid band within 1 % of
        DECAY whatever the multipliers (11 section 6 asks for 5). */
    static constexpr float kShelfOffsetOctaves = 0.5f;

    /** Modulation headroom carried on every line now so that M3b only has to
        read it: 10 section 4's deepest modulation is 0.8 ms. */
    static constexpr float kModHeadroomMs = 1.0f;

    //==========================================================================
    void prepare (double newSampleRate, int)
    {
        sampleRate = newSampleRate > 0.0 ? newSampleRate : 48000.0;
        fadeLength = std::max (1, (int) std::lround (kCrossfadeMs * 0.001 * sampleRate));
        smoothCoef = 1.0f - std::exp (-1.0f / (kSmoothingMs * 0.001f * (float) sampleRate));

        // The longest line any type reaches at the top of SIZE, at this rate.
        float longestMs = 0.0f;
        for (int t = 0; t < numTypes; ++t)
            longestMs = std::max (longestMs, meanDelayMsFor (t, kMaxSizeForLines) * kSpread);

        lineLength = (int) std::ceil ((longestMs + kModHeadroomMs) * 0.001 * sampleRate) + 4;
        for (auto& l : lines)
            l.assign ((size_t) lineLength, 0.0f);

        preLength = (int) std::ceil ((kMaxPreDelayMs + 1.0f) * 0.001 * sampleRate) + 4;
        preLine.assign ((size_t) preLength, 0.0f);

        for (int a = 0; a < 4; ++a)
        {
            apLength[a] = std::max (2, (int) std::lround (kAllpassMs[a] * 0.001 * sampleRate));
            apLine[a].assign ((size_t) apLength[a], 0.0f);
        }

        // Fixed sign vectors: the input is spread with alternating signs, and
        // the two outputs read orthogonal +-1 patterns so L and R hear the
        // same lines decorrelated rather than the same sum twice.
        for (int i = 0; i < N; ++i)
        {
            inSign[i] = (i % 2 == 0 ? 1.0f : -1.0f) / std::sqrt ((float) N);
            outL[i]   = ((i / 2) % 2 == 0 ? 1.0f : -1.0f) / std::sqrt ((float) N);
            outR[i]   = (i % 2 == 0 ? 1.0f : -1.0f) * ((i / 4) % 2 == 0 ? 1.0f : -1.0f) / std::sqrt ((float) N);
        }

        current = requested;
        fourTarget = fourWeight = fourFor (current.type);
        primeLengths (current, lengths);
        levelNow = levelTo = levelFor (current);
        sizeAtBuild = current.sizeM;
        preDelaySamples = preDelayFor (current.preDelayMs);

        reset();
    }

    void reset()
    {
        for (auto& l : lines) std::fill (l.begin(), l.end(), 0.0f);
        std::fill (preLine.begin(), preLine.end(), 0.0f);
        for (auto& a : apLine) std::fill (a.begin(), a.end(), 0.0f);
        for (auto& f : filt) f = {};
        writeIdx = 0; preIdx = 0;
        for (auto& i : apIdx) i = 0;

        // A move in flight lands, as the ER generator's does: `current`
        // already names where it was going.
        if (fading)
            lengths = fadeTo;
        levelNow = levelTo;
        fourWeight = fourTarget;
        fading = dipping = preFading = false;
        fadePos = dipPos = prePos = 0;
        preDelaySamples = preDelayFor (current.preDelayMs);

        smoothed = { current.decaySeconds, current.dampLo, current.dampHi };
        designAll();
    }

    void setConfig (const LateConfig& c) noexcept { requested = c; }

    /** `in` is the mono tail feed; `outLeft`/`outRight` receive the tail at
        network level, before the REVERB fader. */
    void process (const float* in, float* outLeft, float* outRight, int numSamples) noexcept
    {
        applyPendingConfig();
        smoothCoefficients (numSamples);

        const auto half = fadeLength;

        for (int s = 0; s < numSamples; ++s)
        {
            // Pre-delay: the tail only, crossfaded between two read points
            // when it moves.
            preLine[(size_t) preIdx] = in[s];
            auto x = readPre (preDelaySamples);
            if (preFading)
            {
                const auto w = raisedCosine (prePos, fadeLength);
                x = x * (1.0f - w) + readPre (preDelayTarget) * w;
                if (++prePos >= fadeLength)
                {
                    preFading = false;
                    preDelaySamples = preDelayTarget;
                }
            }
            if (++preIdx == preLength) preIdx = 0;

            // Input diffusion: Schroeder allpasses in series, tail only.
            // All four always run; a type takes its output after two or after
            // four, and a TYPE change between the two crossfades the tap rather
            // than switching it, which would be a step fed into the loop.
            float afterTwo = 0.0f;
            for (int a = 0; a < 4; ++a)
            {
                auto& buf = apLine[a];
                const auto d = buf[(size_t) apIdx[a]];
                const auto v = x + apGain[a] * d;
                buf[(size_t) apIdx[a]] = flush (v);
                x = d - apGain[a] * v;
                if (++apIdx[a] == apLength[a]) apIdx[a] = 0;
                if (a == 1) afterTwo = x;
            }
            if (fourWeight != fourTarget)
                fourWeight = std::clamp (fourWeight + (fourTarget > fourWeight ? 1.0f : -1.0f) / (float) fadeLength, 0.0f, 1.0f);
            x = afterTwo + (x - afterTwo) * fourWeight;

            // Read, absorb, mix, write.
            float y[N], mixed[N];
            const auto w = fading ? raisedCosine (fadePos, fadeLength) : 0.0f;
            if (fading)
                blendFilters (w);

            for (int i = 0; i < N; ++i)
            {
                auto d = read (i, lengths[i]);
                if (fading)
                    d = d * (1.0f - w) + read (i, fadeTo[i]) * w;

                y[i] = absorb (i, d);
                mixed[i] = y[i];
            }

            hadamard (mixed);
            float l = 0.0f, r = 0.0f;

            for (int i = 0; i < N; ++i)
            {
                lines[i][(size_t) writeIdx] = flush (mixed[i] + inSign[i] * x);
                l += outL[i] * y[i];
                r += outR[i] * y[i];
            }

            if (++writeIdx == lineLength) writeIdx = 0;

            const auto level = fading ? levelNow + (levelTo - levelNow) * w : levelNow;
            l *= level; r *= level;

            if (fading && ++fadePos >= fadeLength)
            {
                fading   = false;
                lengths  = fadeTo;
                levelNow = levelTo;
                designAll();
            }

            // TYPE: down over one crossfade, swap at the bottom, up over
            // another.
            if (dipping)
            {
                const auto g = dipPos < half ? 0.5f * (1.0f + std::cos ((float) dipPos / (float) half * 3.14159265f))
                                             : 0.5f * (1.0f - std::cos ((float) (dipPos - half) / (float) half * 3.14159265f));
                l *= g; r *= g;
                if (++dipPos == half)
                {
                    // At the bottom: the lengths crossfade inside the loop the
                    // way SIZE's do, rather than jumping. A jump here is
                    // muted on the way out but not inside the loop, where it
                    // recirculates and arrives as the dip comes back up.
                    primeLengths (current, fadeTo);
                    levelTo = levelFor (current);
                    sizeAtBuild = current.sizeM;
                    fading  = true;
                    fadePos = 0;
                    designAll();
                    fourTarget = fourFor (current.type);
                }
                else if (dipPos >= 2 * half)
                {
                    dipping = false;
                    dipPos  = 0;
                }
            }

            outLeft[s]  = l;
            outRight[s] = r;
        }
    }

    //== For the tests and the measurement tool =================================

    int  lineLengthSamples (int i) const noexcept { return lengths[(size_t) i]; }
    int  preDelayNow() const noexcept { return preDelaySamples; }
    bool isMoving() const noexcept { return fading || dipping || preFading; }

    /** |H_i| at DC, at the line's own mid and at Nyquist: the absorbent
        filter's three anchors, for the stability assertion. */
    float maxLoopGain (int i) const noexcept
    {
        const auto& f = filt[(size_t) i];
        return std::max ({ f.g * f.loDc, f.g, f.g * f.hiNyq });
    }

    /** **The tail's level does not rise as the room shrinks.** For a given T60 a
        network's energy grows with how often it recirculates, about T60 /
        tau-bar, so at a fifth of a type's SIZE its tail was 7 dB louder and
        Hall at 1 m peaked at +0.8 dBFS on pink noise at -18 dBFS RMS (2026-10-02).
        The output is scaled by sqrt(tau-bar / the type's own tau-bar), so every
        type at its own SIZE is exactly as it was and SIZE no longer moves the
        level; DECAY still does, as it should. A real small room *is* louder at
        the same T60 -- this is a plugin's choice over a room's, CALIBRATE, the
        same call M2 made for the early reflections under 6 m. */
    static float levelFor (const LateConfig& c) noexcept
    {
        const auto t = std::clamp (c.type, 0, numTypes - 1);
        return std::sqrt (meanDelayMsFor (t, c.sizeM) / meanDelayMsFor (t, constantsFor (t).sizeM));
    }

    /** 1 for a type that runs four input diffusers, 0 for two. */
    static float fourFor (int type) noexcept
    {
        return kLateTypeConstants[(size_t) std::clamp (type, 0, numTypes - 1)].allpasses >= 4 ? 1.0f : 0.0f;
    }

    /** tau-bar, ms, for a type at a SIZE. */
    static float meanDelayMsFor (int type, float sizeM) noexcept
    {
        const auto t   = std::clamp (type, 0, numTypes - 1);
        const auto ref = constantsFor (t).sizeM;
        return std::max (kMinMeanDelayMs, kLateTypeConstants[(size_t) t].meanDelayMs * std::max (sizeM, 0.1f) / ref);
    }

    /** **The feedback matrix: Hadamard, not 10 section 4's Householder.**
        Householder at N lines is I - (2/N) 11^T, so at eight its diagonal is
        0.75 against -0.25 off it: three quarters of every line goes straight
        back into itself, and each line's own period survives as flutter.
        Measured on 2026-10-02 before this changed, the late tail's envelope
        recurred at each type's shortest line (Chamber 27 ms, Hall 42 ms) with
        autocorrelation up to 0.28 against 11 section 6's 0.2. Householder is
        only maximally mixing at four lines. A normalised Hadamard matrix has
        every entry at 1/sqrt(N), so each pass spreads a line evenly over all
        of them; done as a fast Walsh-Hadamard transform it is N log2 N adds
        and one scale, about what Householder cost, and orthogonal by
        construction. The price is that N must be a power of two -- 16 is
        still open to M4, 12 is not. */
    static void hadamard (float* v) noexcept
    {
        for (int h = 1; h < N; h <<= 1)
            for (int i = 0; i < N; i += 2 * h)
                for (int j = i; j < i + h; ++j)
                {
                    const auto a = v[j], b = v[j + h];
                    v[j]     = a + b;
                    v[j + h] = a - b;
                }

        const auto scale = 1.0f / std::sqrt ((float) N);
        for (int i = 0; i < N; ++i)
            v[i] *= scale;
    }

    /** Whether n is prime; trial division, which on line lengths of at most
        tens of thousands is a few hundred steps. */
    static bool isPrime (int n) noexcept
    {
        if (n < 2) return false;
        if (n % 2 == 0) return n == 2;
        for (int d = 3; d * d <= n; d += 2)
            if (n % d == 0) return false;
        return true;
    }

private:
    /** SIZE's top, from the schema, that the lines are sized for. */
    static constexpr float kMaxSizeForLines = 80.0f;

    //==========================================================================
    void applyPendingConfig() noexcept
    {
        const auto& r = requested;

        // Pre-delay: a crossfade to the new read point, when one is not
        // already running.
        if (! preFading)
        {
            const auto target = preDelayFor (r.preDelayMs);
            if (target != preDelaySamples)
            {
                preDelayTarget = target;
                preFading = true;
                prePos = 0;
            }
        }

        if (fading || dipping)
            return;

        const bool typeChanged = r.type != current.type;
        const bool sizeMoved   = std::abs (r.sizeM - sizeAtBuild) >= kSizeRetrigger * sizeAtBuild;

        // DECAY, the multipliers and the knees are coefficients: taken now,
        // smoothed on the way in.
        current.decaySeconds = r.decaySeconds;
        current.dampLo       = r.dampLo;
        current.dampHi       = r.dampHi;
        current.loKneeHz     = r.loKneeHz;
        current.hiKneeHz     = r.hiKneeHz;
        current.preDelayMs   = r.preDelayMs;

        if (typeChanged)
        {
            current.type  = r.type;
            current.sizeM = r.sizeM;
            dipping = true;
            dipPos  = 0;
            return;
        }

        if (sizeMoved)
        {
            current.sizeM = r.sizeM;
            primeLengths (current, fadeTo);
            levelTo = levelFor (current);
            sizeAtBuild = current.sizeM;
            fading  = true;
            fadePos = 0;
            designAll();
        }
    }

    /** Mutually prime line lengths, re-derived at this rate rather than
        multiplied (10 section 4): each the nearest unused prime to its target
        time. Primes are mutually prime by construction, and 11 section 6 asks
        for primes. */
    void primeLengths (const LateConfig& c, std::array<int, N>& out) const noexcept
    {
        const auto tau = meanDelayMsFor (c.type, c.sizeM);
        const auto maxLen = lineLength - 4 - (int) std::ceil (kModHeadroomMs * 0.001 * sampleRate);

        for (int i = 0; i < N; ++i)
        {
            const auto u   = N > 1 ? 2.0f * (float) i / (float) (N - 1) - 1.0f : 0.0f;
            const auto ms  = tau * std::pow (kSpread, u);
            auto target    = std::clamp ((int) std::lround (ms * 0.001 * sampleRate), 3, std::max (3, maxLen));

            for (int step = 0; ; ++step)
            {
                const int candidates[2] { target + step, target - step };
                bool done = false;
                for (auto n : candidates)
                {
                    if (n < 3 || n > maxLen || ! isPrime (n))
                        continue;
                    bool used = false;
                    for (int k = 0; k < i; ++k)
                        used = used || out[(size_t) k] == n;
                    if (! used) { out[(size_t) i] = n; done = true; break; }
                }
                if (done) break;
            }
        }
    }

    int preDelayFor (float ms) const noexcept
    {
        return std::clamp ((int) std::lround (std::clamp (ms, 0.0f, kMaxPreDelayMs) * 0.001 * sampleRate), 0, preLength - 2);
    }

    float readPre (int delay) const noexcept
    {
        auto i = preIdx - delay;
        if (i < 0) i += preLength;
        return preLine[(size_t) i];
    }

    float read (int line, int delay) const noexcept
    {
        auto i = writeIdx - delay;
        if (i < 0) i += lineLength;
        return lines[(size_t) line][(size_t) i];
    }

    static float raisedCosine (int pos, int length) noexcept
    {
        return 0.5f * (1.0f - std::cos ((float) pos / (float) length * 3.14159265f));
    }

    //== The absorbent filters (10 section 4) ===================================
    //
    // Per pass through line i the attenuation is A_i(w) = -60 m_i / (fs T60(w))
    // dB, with T60 = T_mid * r_lo below the low knee and T_mid * r_hi above the
    // high one. A broadband gain g = 10^(A_mid / 20), then a low shelf whose DC
    // gain is 10^((A_lo - A_mid) / 20) and a high shelf whose Nyquist gain is
    // 10^((A_hi - A_mid) / 20), each half an octave outside its knee
    // (`kShelfOffsetOctaves`) so its plateau starts there. Every
    // gain is derived from T60 and m_i, so the multipliers are accurate rather
    // than fitted -- the reason 10 section 1 chose an FDN.
    //
    // **Second-order shelves, not 10 section 4's first-order.** 11 section 6
    // asks for the mid band within 5 % of DECAY whatever the multipliers, and
    // the bands two octaves outside each knee within 15 % of theirs. A
    // first-order shelf is still 11 % short of its plateau 1.5 octaves from its
    // knee, which is where the mid band sits between Room's 200 Hz and 1.6 kHz,
    // and puts that band 25 % off at a 0.25 multiplier; a Butterworth-Q
    // second-order shelf is 1.5 % short there. At that Q the response is
    // monotonic, so the three anchors below are still its extremes and 10
    // section 4's clamp still bounds it. Two more multiply-adds a filter.

    struct Filter
    {
        float g = 1.0f;
        float lb[3] { 1.0f, 0.0f, 0.0f }, la[2] {}, lz[2] {};   ///< low shelf, TDF-II
        float hb[3] { 1.0f, 0.0f, 0.0f }, ha[2] {}, hz[2] {};   ///< high shelf
        float loDc = 1.0f, hiNyq = 1.0f;                        ///< the anchors, for the tests
    };

    /** **Denormals are flushed by hand, not dodged.** 10 section 4 suggested an
        alternating +-1e-20 into one line; that leaves a reset network that
        is never silent, and 11 section 6 wants zeros in to give exactly zeros
        out and a tail that reaches exactly 0.0f with FTZ off. So every value
        the network stores -- the lines, the diffusers, the filter states --
        is zero below 1e-15, 300 dB down, and nothing it keeps can go
        denormal. */
    static float flush (float v) noexcept { return std::abs (v) < 1.0e-15f ? 0.0f : v; }

    static float biquad (const float* b, const float* a, float* z, float x) noexcept
    {
        const auto y = b[0] * x + z[0];
        z[0] = flush (b[1] * x - a[0] * y + z[1]);
        z[1] = flush (b[2] * x - a[1] * y);
        return y;
    }

    float absorb (int i, float x) noexcept
    {
        auto& f = filt[(size_t) i];
        return biquad (f.hb, f.ha, f.hz, biquad (f.lb, f.la, f.lz, x * f.g));
    }

    /** The two knees' trigonometry, shared by every line. */
    struct Knee { double cosW = 1.0, sinW = 0.0; };

    static Knee kneeAt (float hz, double fs) noexcept
    {
        const auto w = 2.0 * 3.14159265358979 * std::clamp ((double) hz, 10.0, 0.45 * fs) / fs;
        return { std::cos (w), std::sin (w) };
    }

    /** RBJ's shelves at S = 1, with A = sqrt(gain) so the plateau is `gain`
        (its DC gain for the low shelf, its Nyquist gain for the high) and the
        half-way point in dB is the knee. */
    static void shelf (bool low, double gain, const Knee& k, float* b, float* a) noexcept
    {
        const auto A  = std::sqrt (std::max (gain, 1.0e-9));
        const auto sA = std::sqrt (A);
        const auto al = k.sinW * 0.5 * std::sqrt (2.0);
        const auto c  = k.cosW;

        double b0, b1, b2, a0, a1, a2;
        if (low)
        {
            b0 = A * ((A + 1) - (A - 1) * c + 2 * sA * al);
            b1 = 2 * A * ((A - 1) - (A + 1) * c);
            b2 = A * ((A + 1) - (A - 1) * c - 2 * sA * al);
            a0 = (A + 1) + (A - 1) * c + 2 * sA * al;
            a1 = -2 * ((A - 1) + (A + 1) * c);
            a2 = (A + 1) + (A - 1) * c - 2 * sA * al;
        }
        else
        {
            b0 = A * ((A + 1) + (A - 1) * c + 2 * sA * al);
            b1 = -2 * A * ((A - 1) + (A + 1) * c);
            b2 = A * ((A + 1) + (A - 1) * c - 2 * sA * al);
            a0 = (A + 1) - (A - 1) * c + 2 * sA * al;
            a1 = 2 * ((A - 1) - (A + 1) * c);
            a2 = (A + 1) - (A - 1) * c - 2 * sA * al;
        }

        b[0] = (float) (b0 / a0); b[1] = (float) (b1 / a0); b[2] = (float) (b2 / a0);
        a[0] = (float) (a1 / a0); a[1] = (float) (a2 / a0);
    }

    void design (float m, const Knee& lo, const Knee& hi, Filter& f) const noexcept
    {
        const auto fs   = (float) sampleRate;
        const auto tMid = std::clamp (smoothed[0], 0.05f, 60.0f);
        const auto dbAt = [m, fs] (float t60) { return -60.0f * m / (fs * std::max (t60, 0.01f)); };

        // The ceiling (10 section 4): no anchor above kMaxLoopGain.
        const auto ceilingDb = 20.0f * std::log10 (kMaxLoopGain);
        const auto aMid = std::min (dbAt (tMid), ceilingDb);
        const auto aLo  = std::min (dbAt (tMid * smoothed[1]), ceilingDb);
        const auto aHi  = std::min (dbAt (tMid * smoothed[2]), ceilingDb);

        f.g     = std::pow (10.0f, aMid * 0.05f);
        f.loDc  = std::pow (10.0f, (aLo - aMid) * 0.05f);
        f.hiNyq = std::pow (10.0f, (aHi - aMid) * 0.05f);
        shelf (true,  f.loDc,  lo, f.lb, f.la);
        shelf (false, f.hiNyq, hi, f.hb, f.ha);
    }

    void designAll() noexcept
    {
        designedKnees = { current.loKneeHz, current.hiKneeHz };
        const auto offset = std::pow (2.0f, kShelfOffsetOctaves);
        const auto lo = kneeAt (current.loKneeHz / offset, sampleRate);
        const auto hi = kneeAt (current.hiKneeHz * offset, sampleRate);

        const auto tMid = std::clamp (smoothed[0], 0.05f, 60.0f);
        for (int a = 0; a < 4; ++a)
        {
            const auto dS = (float) apLength[(size_t) a] / (float) sampleRate;
            apGain[(size_t) a] = std::min (kAllpassGain, std::pow (10.0f, -3.0f * dS / (kAllpassRingFraction * tMid)));
        }
        for (int i = 0; i < N; ++i)
            design ((float) lengths[(size_t) i], lo, hi, filt[(size_t) i]);

        // A length move in flight: both ends of it, so the per-sample blend
        // in `process` lands exactly where this would have.
        if (fading)
            for (int i = 0; i < N; ++i)
            {
                design ((float) lengths[(size_t) i], lo, hi, filtFrom[(size_t) i]);
                design ((float) fadeTo[(size_t) i],  lo, hi, filtTo[(size_t) i]);
            }
    }

    /** **The absorbent filters move with the lengths, sample by sample.** Each
        line's loss is per pass, so a new length is a new filter -- and until
        2026-10-02 the new design landed at the end of a SIZE crossfade in one
        step, on every line at once, which on a sustained sine measured as a
        step 3.2 times anything in the tail before it. Both ends are designed
        when a move starts and every coefficient is blended across it with the
        reads, so the end of the fade is where the blend already is. */
    void blendFilters (float w) noexcept
    {
        const auto mix = [w] (float a, float b) { return a + (b - a) * w; };
        for (int i = 0; i < N; ++i)
        {
            auto& f = filt[(size_t) i];
            const auto& a = filtFrom[(size_t) i];
            const auto& b = filtTo[(size_t) i];
            f.g = mix (a.g, b.g);
            for (int k = 0; k < 3; ++k) { f.lb[k] = mix (a.lb[k], b.lb[k]); f.hb[k] = mix (a.hb[k], b.hb[k]); }
            for (int k = 0; k < 2; ++k) { f.la[k] = mix (a.la[k], b.la[k]); f.ha[k] = mix (a.ha[k], b.ha[k]); }
            f.loDc  = std::max (a.loDc, b.loDc);
            f.hiNyq = std::max (a.hiNyq, b.hiNyq);
        }
    }

    /** DECAY and the two multipliers glide 20 ms one-pole, stepped per
        block; the filters are redesigned only while something is moving, so
        a held setting costs nothing here. */
    void smoothCoefficients (int numSamples) noexcept
    {
        const std::array<float, 3> target { current.decaySeconds, current.dampLo, current.dampHi };
        const auto coef = 1.0f - std::pow (1.0f - smoothCoef, (float) numSamples);

        bool moved = current.loKneeHz != designedKnees[0] || current.hiKneeHz != designedKnees[1];
        for (int k = 0; k < 3; ++k)
        {
            if (smoothed[(size_t) k] == target[(size_t) k])
                continue;
            smoothed[(size_t) k] += (target[(size_t) k] - smoothed[(size_t) k]) * coef;
            if (std::abs (smoothed[(size_t) k] - target[(size_t) k]) <= 1.0e-6f * std::abs (target[(size_t) k]))
                smoothed[(size_t) k] = target[(size_t) k];
            moved = true;
        }

        if (moved)
            designAll();
    }

    //==========================================================================
    double sampleRate = 48000.0;
    int fadeLength = 1;
    float smoothCoef = 0.0f;

    LateConfig requested, current;
    float sizeAtBuild = roomDefaults::kSizeM;

    std::array<std::vector<float>, N> lines;
    int lineLength = 0, writeIdx = 0;
    std::array<int, N> lengths {}, fadeTo {};
    bool fading = false, dipping = false;
    int fadePos = 0, dipPos = 0;

    std::vector<float> preLine;
    int preLength = 0, preIdx = 0, preDelaySamples = 0, preDelayTarget = 0, prePos = 0;
    bool preFading = false;

    std::array<std::vector<float>, 4> apLine;
    std::array<int, 4> apLength {}, apIdx {};
    std::array<float, 4> apGain { kAllpassGain, kAllpassGain, kAllpassGain, kAllpassGain };
    float fourWeight = 0.0f, fourTarget = 0.0f;   ///< 0 takes the diffusers after two, 1 after four
    float levelNow = 1.0f, levelTo = 1.0f;        ///< `levelFor`, crossfaded with the lengths

    std::array<Filter, N> filt {}, filtFrom {}, filtTo {};
    std::array<float, 3> smoothed { 1.8f, 1.2f, 0.4f };
    std::array<float, 2> designedKnees { 0.0f, 0.0f };

    float inSign[N] {}, outL[N] {}, outR[N] {};

};

} // namespace bmo::reverb
