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
    float modDepthMs   = roomDefaults::kModDepthMs;     ///< 0.1..0.8, each line's peak deviation
    float modRateHz    = roomDefaults::kModRateHz;      ///< 0.1..1.2
    float attack       = 0.0f;                          ///< 0..1 of `kAttackSpanMs`, per type; 0 is immediate
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

    /** **ATTACK, the onset bloom** (10 section 2, M3b, 2026-10-07).

        Each line is fed the diffused input at its own delay and its own
        level: the first line at once and quietly, the last ATTACK x
        `kAttackSpanMs` later and loudest. So an impulse enters the network
        line by line, rising, and the tail's level climbs over that span
        instead of starting at its full height: the tail blooms behind the
        early reflections, and the bulk of it arrives ATTACK x 120 ms late
        without PRE-DELAY having moved. At ATTACK 0 every line is fed now, at
        unity, and the tail is immediate, which is Plate.

        10 section 2 asks for "a rising envelope on the FDN input". An
        envelope needs something to start it, and continuous audio has no
        onsets to start it on; a detector would be a second opinion about the
        music. Delays are linear, need no trigger, and treat every sample of
        the input the same way. Frosty approved rising taps on 2026-10-06.

        **One tap a line, and not N taps into one input.** The first build
        summed eight rising taps ahead of the network, which is a sparse FIR
        in front of everything, and it showed: the late tail's spectral
        flatness fell from 0.77 to 0.53 on Room and from 0.92 to 0.72 on Hall
        (2026-10-07, on ICE QUEEN), a comb on the whole tail. Fed a line
        each, the taps never meet except through the mixing matrix, where
        they add as the lines themselves do.

        **The levels are normalised by energy**, mean square one across the
        lines, so the tail's level does not depend on ATTACK: what the
        network is given is spread in time, not turned up.

        The span, the first line's level, the curve and which line is fed
        when are all CALIBRATE: nothing here has been heard. */
    static constexpr float kAttackSpanMs = 120.0f;
    /** A line's level before normalising: kAttackFloor for the first fed,
        rising as position ^ kAttackCurve to one for the last. */
    static constexpr float kAttackFloor = 0.12f;
    static constexpr float kAttackCurve = 1.5f;

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

        sixPoint = sampleRate < 80000.0;
        // Each line's modulator runs at its own fraction of MOD RATE, spread by
        // the golden ratio so no two share a period.
        for (int i = 0; i < N; ++i)
            modScale[i] = 1.0f - 0.4f * (float) std::fmod ((double) i * 0.6180339887, 1.0);

        moveRamp.resize ((size_t) fadeLength + 1);
        for (int k = 0; k <= fadeLength; ++k)
            moveRamp[(size_t) k] = 0.5f * (1.0f + std::cos ((float) k / (float) fadeLength * 3.14159265f));
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

        bloomLength = (int) std::ceil ((kAttackSpanMs + 1.0f) * 0.001 * sampleRate) + 4;
        bloomLine.assign ((size_t) bloomLength, 0.0f);

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

        // Whatever was in flight belongs to the old rate and buffer; reset()
        // builds everything from the settings last asked for.
        fading = dipping = preFading = bloomFading = false;

        reset();
    }

    void reset()
    {
        for (auto& l : lines) std::fill (l.begin(), l.end(), 0.0f);
        std::fill (preLine.begin(), preLine.end(), 0.0f);
        std::fill (bloomLine.begin(), bloomLine.end(), 0.0f);
        bloomIdx = 0;
        for (auto& a : apLine) std::fill (a.begin(), a.end(), 0.0f);
        for (auto& f : filt) f = {};
        for (auto& f : filtTo) f = {};
        writeIdx = 0; preIdx = 0;
        for (auto& i : apIdx) i = 0;

        // **Everything the move would have reached is built from `current`,
        // never copied from the move.** `current` already names where a SIZE
        // crossfade or a TYPE dip was going, so a fresh build of it is where
        // the move would have landed. Copying the crossfade's stored target
        // instead -- as this did until QA's review of 2026-10-03 -- carried
        // lengths from a higher rate into a smaller buffer through prepare()
        // (reads at index -13,438), kept a stale SIZE after the setting had
        // moved again, and left a TYPE dip cut short on the old type's
        // lengths and diffusers.
        //
        // **And `current` is first brought up to `requested`.** A move takes
        // no new request until it ends, so with one queued behind it
        // `current` is where the move was going and not what the network was
        // last told. Building from it left SIZE, DECAY or both multipliers on
        // the move's values: at f3e91db, reset() mid-move with 30 m queued
        // differed from a fresh instance at 30 m by 0.232 over 2 s of noise,
        // with DECAY 0.5 s queued by 0.000704, and mid-dip with SIZE queued
        // by 0.0318 (QA's probe, 2026-10-03). prepare() already did this
        // before calling here, which is why it was exact and reset() was not.
        land();

        // **And a request sent before the first block lands as well.** A
        // host often sends its values between prepare() and audio; taken as
        // a move, a TYPE there dipped, a SIZE crossfaded, a PRE-DELAY faded
        // and DECAY and both multipliers glided, all over silence that had
        // nothing to protect (2026-10-05). Until a block has played, a
        // request is built, not moved to.
        started = false;
    }

    void setConfig (const LateConfig& c) noexcept { requested = c; }

    /** `in` is the mono tail feed; `outLeft`/`outRight` receive the tail at
        network level, before the REVERB fader. */
    void process (const float* in, float* outLeft, float* outRight, int numSamples) noexcept
    {
        // Never prepared: there are no lines to read, so the tail is silence.
        if (lineLength <= 0)
        {
            std::fill (outLeft, outLeft + numSamples, 0.0f);
            std::fill (outRight, outRight + numSamples, 0.0f);
            return;
        }

        if (! started)
        {
            land();
            started = true;
        }

        applyPendingConfig();
        smoothCoefficients (numSamples);
        modDepth = modDepthSamples();

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
            advanceModulation();
            float mixed[N];
            float l = 0.0f, r = 0.0f;

            if (! fading)
            {
                for (int i = 0; i < N; ++i)
                {
                    const auto y = absorb (filt[(size_t) i], readAt (i, (float) lengths[i] + modNow[i]));
                    mixed[i] = y;
                    l += outL[i] * y;
                    r += outR[i] * y;
                }
                l *= levelNow; r *= levelNow;
            }
            else
            {
                // **A length move, and why it cannot add energy.** Two whole
                // paths run: the old read through the old filters at the old
                // level, the new read through the new filters at the new
                // level. What weights them is the rule that matters, and it
                // is counted in **when a sample was written**, not when it is
                // read:
                //
                //   - a sample written before the move is read at the old
                //     delay, in full, and never again;
                //   - a sample written after it is read at the new delay;
                //   - across the 30 ms after the move starts the two weights
                //     cross over, summing to one for each sample;
                //   - and at no instant do the two paths together weigh more
                //     than one.
                //
                // **What that proves, and what it does not.** With weights a
                // and b, a + b <= 1 at every instant, (a x + b y)^2 <= a x^2 +
                // b y^2; summed over time, each stored sample appears with a
                // total weight of at most one. So the two *reads* of a line
                // together carry no more energy than was written to it,
                // whatever the weights are doing. That is exact.
                //
                // The reads then go through two different filters, each
                // realising a gain under one (tested for both banks, mid-
                // move), and the sum of two differently filtered signals is
                // not covered by that argument. That last step is held by
                // measurement: SIZE and TYPE toggled at every cadence from
                // one block to a third of a second over a 30 s tail, 43 rows
                // in the tests (seven by default, all 43 under --long) and 72
                // in QA's probe, none growing.
                //
                // Until QA's third pass (2026-10-03) both paths were weighted
                // by one crossfade in *read* time. Reading at a longer delay
                // then replayed samples that had already been round the loop,
                // every move put energy back, and SIZE toggling 12 <-> 30 m
                // every 64 blocks at DECAY 20 s reached +573 dBFS in a minute.
                //
                // What it costs: a line that grows goes quiet between its old
                // delay and its new one, because nothing written since the
                // move has reached the new delay yet -- which is what a room
                // getting bigger does. A move takes the longest line plus
                // 30 ms to finish, and the next move waits for it.
                float lOld = 0.0f, rOld = 0.0f, lNew = 0.0f, rNew = 0.0f;

                for (int i = 0; i < N; ++i)
                {
                    const auto a = writtenBefore (movePos - lengths[i]);
                    const auto b = std::min (1.0f - writtenBefore (movePos - fadeTo[i]), 1.0f - a);

                    const auto ya = absorb (filt[(size_t) i],   a * readAt (i, (float) lengths[i] + modNow[i]));
                    const auto yb = absorb (filtTo[(size_t) i], b * readAt (i, (float) fadeTo[i] + modNow[i]));
                    mixed[i] = ya + yb;
                    lOld += outL[i] * ya;  rOld += outR[i] * ya;
                    lNew += outL[i] * yb;  rNew += outR[i] * yb;
                }
                l = levelNow * lOld + levelTo * lNew;
                r = levelNow * rOld + levelTo * rNew;
            }

            hadamard (mixed);

            // ATTACK: each line is fed at its own delay and level, and a
            // change of ATTACK crossfades the two feeds. See kAttackSpanMs.
            bloomLine[(size_t) bloomIdx] = x;
            const auto bloomW = bloomFading ? raisedCosine (bloomPos, fadeLength) : 0.0f;

            for (int i = 0; i < N; ++i)
            {
                auto fed = fedTo (i, bloomNow, x);
                if (bloomFading)
                    fed = fed * (1.0f - bloomW) + fedTo (i, bloomNext, x) * bloomW;

                lines[i][(size_t) writeIdx] = flush (mixed[i] + inSign[i] * fed);
            }

            if (bloomFading && ++bloomPos >= fadeLength)
            {
                bloomFading = false;
                bloomNow = bloomNext;
                attackNow = attackTarget;
            }
            if (++bloomIdx == bloomLength) bloomIdx = 0;

            if (++writeIdx == lineLength) writeIdx = 0;

            if (fading && ++movePos >= moveEnd)
            {
                // The new path becomes the only one, filter state and all.
                fading   = false;
                lengths  = fadeTo;
                levelNow = levelTo;
                filt     = filtTo;
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
                    beginMove();
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
    int  bufferLengthSamples() const noexcept { return lineLength; }
    /** 0 while the diffusers are taken after two, 1 after four. */
    float diffuserWeight() const noexcept { return fourWeight; }
    int  preDelayNow() const noexcept { return preDelaySamples; }
    /** Whether the lines are fed at ATTACK's delays, and line `i`'s. */
    bool bloomIsOn() const noexcept { return bloomNow.on; }
    int  bloomDelaySamples (int i) const noexcept { return bloomNow.delay[i]; }
    bool isMoving() const noexcept { return fading || dipping || preFading || bloomFading; }

    /** |H_i| at DC, at the line's own mid and at Nyquist: the absorbent
        filter's three anchors, for the stability assertion. */
    float maxLoopGain (int i) const noexcept
    {
        const auto& f = filt[(size_t) i];
        return (float) std::max ({ f.g * f.loDc, f.g, f.g * f.hiNyq });
    }

    /** |H_i(f)| of line i's absorbent filter, evaluated in double from the
        coefficients the line **actually runs** -- not the design. The
        anchors above are the design's; this is what the loop sees, and the
        two parted at 96 and 192 kHz (QA, 2026-10-03). `incoming` reads the
        bank a length move is fading to, which runs alongside the live one
        for as long as the move lasts and is only meaningful while one is. */
    double realisedGain (int i, double hz, bool incoming = false) const noexcept
    {
        const auto& f = incoming ? filtTo[(size_t) i] : filt[(size_t) i];
        const auto w = 2.0 * 3.14159265358979 * hz / sampleRate;
        const auto at = [w] (const auto* b, const auto* a)
        {
            const double c1 = std::cos (w), s1 = std::sin (w), c2 = std::cos (2.0 * w), s2 = std::sin (2.0 * w);
            const double nr = (double) b[0] + (double) b[1] * c1 + (double) b[2] * c2;
            const double ni = -((double) b[1] * s1 + (double) b[2] * s2);
            const double dr = 1.0 + (double) a[0] * c1 + (double) a[1] * c2;
            const double di = -((double) a[0] * s1 + (double) a[1] * s2);
            return std::sqrt ((nr * nr + ni * ni) / (dr * dr + di * di));
        };
        return (double) f.g * at (f.lb, f.la) * at (f.hb, f.ha);
    }

    /** Line i's deviation from its length right now, in samples, and the
        steepest that may ever change per sample (3 cents). For the tests. */
    float modulationSamples (int i) const noexcept { return modNow[i]; }
    static constexpr float maxDetunePerSample() noexcept { return 0.0017344f; }
    bool readsSixPoint() const noexcept { return sixPoint; }

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
    /** Everything built from the request as it stands, with no move in
        flight: the lengths, the level, the diffuser tap, the pre-delay read
        point and the smoothed DECAY and multipliers. Only where nothing is
        sounding -- reset(), and the first block after it. */
    void land() noexcept
    {
        current = requested;
        primeLengths (current, lengths);
        sizeAtBuild = current.sizeM;
        levelNow = levelTo = levelFor (current);
        fourWeight = fourTarget = fourFor (current.type);
        fading = dipping = preFading = false;
        movePos = moveEnd = dipPos = prePos = 0;
        preDelaySamples = preDelayFor (current.preDelayMs);
        bloomFading = false;
        bloomPos = 0;
        attackNow = attackTarget = current.attack;
        bloomNow = bloomNext = bloomFor (current.attack);

        smoothed = { current.decaySeconds, current.dampLo, current.dampHi };
        designAll();
        resetModulation();
    }

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

        // ATTACK: a crossfade to the new feed, likewise. It has no host lane,
        // so in a host this only ever happens with a TYPE change.
        if (! bloomFading && r.attack != attackNow)
        {
            attackTarget = r.attack;
            bloomNext = bloomFor (r.attack);
            bloomFading = true;
            bloomPos = 0;
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
        current.attack       = r.attack;

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
            beginMove();
        }
    }

    /** Mutually prime line lengths, re-derived at this rate rather than
        multiplied (10 section 4): each the nearest unused prime to its target
        time. Primes are mutually prime by construction, and 11 section 6 asks
        for primes. */
    /** **The search cannot spin, whatever it is given** (QA, 2026-10-03). It
        ran `for (;;)` until a prime turned up, and on a network that was
        never prepared the buffer is empty, the ceiling negative, and no
        candidate can pass: reset() before prepare() never returned.

        - **Unprepared, there are no lengths**: every line is 0, and
          `process` writes zeros rather than read a line that does not exist.
        - The walk outwards from the target stops when both sides have left
          [3, maxLen], which is at most maxLen steps.
        - If no unused prime is in range -- a buffer too small to hold N of
          them -- the line takes the nearest unused length instead. Mutual
          primality is lost there and the network still runs inside its
          buffer; no rate or SIZE in the schema reaches it. */
    void primeLengths (const LateConfig& c, std::array<int, N>& out) const noexcept
    {
        const auto maxLen = lineLength - 4 - (int) std::ceil (kModHeadroomMs * 0.001 * sampleRate);

        if (lineLength <= 0 || maxLen < 3)
        {
            out.fill (0);
            return;
        }

        const auto tau = meanDelayMsFor (c.type, c.sizeM);

        const auto unused = [&out] (int n, int upTo)
        {
            for (int k = 0; k < upTo; ++k)
                if (out[(size_t) k] == n)
                    return false;
            return true;
        };

        for (int i = 0; i < N; ++i)
        {
            const auto u      = N > 1 ? 2.0f * (float) i / (float) (N - 1) - 1.0f : 0.0f;
            const auto ms     = tau * std::pow (kSpread, u);
            const auto target = std::clamp ((int) std::lround (ms * 0.001 * sampleRate), 3, maxLen);

            int found = 0, fallback = 0;

            for (int step = 0; found == 0 && (target + step <= maxLen || target - step >= 3); ++step)
                for (auto n : { target + step, target - step })
                {
                    if (n < 3 || n > maxLen || ! unused (n, i))
                        continue;
                    if (fallback == 0)
                        fallback = n;
                    if (isPrime (n)) { found = n; break; }
                }

            // `fallback` is 0 only if every length in range is taken, which
            // needs N > maxLen - 2; the target is then as good as any.
            out[(size_t) i] = found != 0 ? found : fallback != 0 ? fallback : target;
        }
    }

    int preDelayFor (float ms) const noexcept
    {
        return std::clamp ((int) std::lround (std::clamp (ms, 0.0f, kMaxPreDelayMs) * 0.001 * sampleRate),
                           0, std::max (0, preLength - 2));
    }

    float readPre (int delay) const noexcept
    {
        auto i = preIdx - delay;
        if (i < 0) i += preLength;
        return preLine[(size_t) i];
    }

    /** When each line is fed the diffused input, in samples after it, and
        how loudly. See `kAttackSpanMs`. */
    struct Bloom
    {
        int   delay[N] {};
        float level[N] {};
        bool  on = false;      ///< false: every line is fed now, at unity
    };

    Bloom bloomFor (float attack) const noexcept
    {
        Bloom b;
        for (auto& l : b.level) l = 1.0f;

        const auto span = (int) std::lround (std::clamp (attack, 0.0f, 1.0f) * kAttackSpanMs * 0.001 * sampleRate);

        // Too short a span to feed N lines at N different samples is no
        // bloom at all: every line now, at unity, as before ATTACK existed.
        if (span < N || bloomLength < span + 2)
            return b;

        float energy = 0.0f;
        for (int i = 0; i < N; ++i)
        {
            // The order the lines are fed in is scattered over them (5 is
            // coprime to any power of two), so that the loudest feeds are
            // not the longest lines'. Positions run 0..1, uneven between.
            const auto k   = (i * 5) % N;
            const auto mid = k > 0 && k < N - 1 ? 0.3f * ((float) std::fmod ((double) k * 0.6180339887, 1.0) - 0.5f) : 0.0f;
            const auto pos = ((float) k + mid) / (float) (N - 1);

            b.delay[i] = (int) std::lround ((float) span * pos);
            b.level[i] = kAttackFloor + (1.0f - kAttackFloor) * std::pow (pos, kAttackCurve);
            energy += b.level[i] * b.level[i];
        }

        const auto norm = std::sqrt ((float) N / energy);
        for (auto& l : b.level)
            l *= norm;

        b.on = true;
        return b;
    }

    /** Line `i`'s share of the diffused input under `b`; `now` is this
        sample's, which is already in the bloom line at `bloomIdx`. */
    float fedTo (int i, const Bloom& b, float now) const noexcept
    {
        if (! b.on)
            return now;

        auto at = bloomIdx - b.delay[i];
        if (at < 0) at += bloomLength;
        return b.level[i] * bloomLine[(size_t) at];
    }

    float read (int line, int delay) const noexcept
    {
        auto i = writeIdx - delay;
        if (i < 0) i += lineLength;
        return lines[(size_t) line][(size_t) i];
    }

    /** A read at a fractional delay, Lagrange: six points at 44.1 and 48 kHz,
        four at 88.2 kHz and above (`sixPoint`).

        **Why not two points.** Linear interpolation is a low-pass that
        depends on the fraction: half way between samples it is 0.78 dB down
        at 6.4 kHz at 48 kHz, on every pass, where a 25 ms line at DECAY 2 s
        is meant to lose 0.75 dB -- the top of the tail would decay twice as
        fast as HIGH x says.

        **Why six at 48 kHz and four above.** The loss is a function of
        frequency over the sample rate, so it matters where the rate is low
        and is nothing where it is high. Four points at 48 kHz are 0.10 dB
        down at 6.4 kHz half way between samples, and that measured: HIGH x
        2.0 came out 1.69 (11 section 6 allows 15 %), and a 6.4 kHz band that
        decays 8-14 % faster at 48 kHz than at 96, where 11 section 6 wants
        5 %. Six points are 0.014 dB down there. At 96 kHz four points are
        0.005 dB down, and that is the rate where the CPU has no room for
        six. An odd-order Lagrange interpolator reading in its middle
        interval, as both always do here, never exceeds unity gain, so the
        loop cannot gain from it. */
    float readAt (int line, float delay) const noexcept
    {
        const auto whole = (int) delay;
        const auto f     = delay - (float) whole;
        const auto& buf  = lines[(size_t) line];

        if (! sixPoint)
        {
            // buf at delays whole + 2, whole + 1, whole, whole - 1.
            auto i = writeIdx - whole - 2;
            if (i < 0) i += lineLength;
            float s[4];
            if (i + 3 < lineLength)
            {
                s[0] = buf[(size_t) i]; s[1] = buf[(size_t) i + 1]; s[2] = buf[(size_t) i + 2]; s[3] = buf[(size_t) i + 3];
            }
            else
            {
                for (int k = 0; k < 4; ++k)
                    s[k] = buf[(size_t) ((i + k) % lineLength)];
            }

            // Nodes at delays -1, 0, 1, 2 about `whole`; the point is at f.
            const auto fm1 = f - 1.0f, fm2 = f - 2.0f, fp1 = f + 1.0f;
            return s[3] * (-f * fm1 * fm2 * (1.0f / 6.0f))
                 + s[2] * (fp1 * fm1 * fm2 * 0.5f)
                 + s[1] * (-fp1 * f * fm2 * 0.5f)
                 + s[0] * (fp1 * f * fm1 * (1.0f / 6.0f));
        }

        // buf at delays whole + 3 ... whole - 2.
        auto i = writeIdx - whole - 3;
        if (i < 0) i += lineLength;
        float s[6];
        if (i + 5 < lineLength)
        {
            for (int k = 0; k < 6; ++k)
                s[k] = buf[(size_t) (i + k)];
        }
        else
        {
            for (int k = 0; k < 6; ++k)
                s[k] = buf[(size_t) ((i + k) % lineLength)];
        }

        // Nodes at delays -2 ... 3 about `whole`; the point is at f.
        const auto a = f + 2.0f, b = f + 1.0f, c = f, d = f - 1.0f, e = f - 2.0f, g = f - 3.0f;
        const auto ab = a * b, eg = e * g, cd = c * d;
        return s[5] * (b * cd * eg * (-1.0f / 120.0f))
             + s[4] * (a * cd * eg * (1.0f / 24.0f))
             + s[3] * (ab * d * eg * (-1.0f / 12.0f))
             + s[2] * (ab * c * eg * (1.0f / 12.0f))
             + s[1] * (ab * cd * g * (-1.0f / 24.0f))
             + s[0] * (ab * cd * e * (1.0f / 120.0f));
    }

    //== Modulation (10 section 4) ==============================================
    //
    // Each line's delay wanders about its length on its own slow random
    // path: a new random target every half period, reached along a
    // smoothstep. **Random and not an LFO**, because a periodic sweep is a
    // chorus and a random one is not (10 section 4), and **slope-bounded by
    // construction**: a line whose delay changes at dT/dt is detuned by
    // 1200 log2 (1 + dT/dt) cents, a smoothstep between two points P apart
    // has a steepest slope of 1.5 times their difference over P, and the
    // depth is held so that never passes kMaxDetune -- 3 cents. So MOD DEPTH
    // and MOD RATE trade against each other at the top: at 1 Hz the deepest
    // a line may go is 0.289 ms, and the full 0.8 ms is only reached under
    // 0.36 Hz.
    //
    // The path is worked out every kModStride samples and walked in straight
    // lines between, one add a line a sample; a chord of a curve is never
    // steeper than the curve, so the bound holds. Counted in samples, never
    // in blocks, so the block size cannot be heard.

    /** 2^(3/1200) - 1: the slope that is 3 cents. */
    static constexpr float kMaxDetune = 0.0017344f;
    static constexpr int   kModStride = 16;

    struct Modulator
    {
        float from = 0.0f, to = 0.0f;
        int   pos = 0, period = 1;
        float step = 1.0f;          ///< 1 / period
        std::uint32_t rng = 1;
    };

    /** The deepest any line may be modulated at this rate, in samples. */
    float modDepthSamples() const noexcept
    {
        const auto rate  = std::clamp (requested.modRateHz, 0.01f, 5.0f);
        const auto depth = std::clamp (requested.modDepthMs, 0.0f, kModHeadroomMs * 0.8f) * 0.001f;
        return std::min (depth, kMaxDetune / (6.0f * rate)) * (float) sampleRate;
    }

    void advanceModulation() noexcept
    {
        if (modTick == 0)
        {
            for (int i = 0; i < N; ++i)
            {
                auto& m = mod[(size_t) i];
                m.pos += kModStride;

                if (m.pos >= m.period)
                {
                    m.rng  = m.rng * 1664525u + 1013904223u;
                    m.from = m.to;
                    m.to   = modDepth * ((float) (m.rng >> 8) * (2.0f / 16777216.0f) - 1.0f);

                    // Half a period at this line's own rate -- and never
                    // shorter than the slope allows, so a depth or rate that
                    // has just moved cannot make this one segment steeper
                    // than 3 cents.
                    const auto half     = (float) sampleRate / (2.0f * std::clamp (requested.modRateHz, 0.01f, 5.0f) * modScale[i]);
                    const auto forSlope = 1.5f * std::abs (m.to - m.from) / kMaxDetune;
                    m.period = std::max (kModStride, (int) std::ceil (std::max (half, forSlope)));
                    m.step   = 1.0f / (float) m.period;
                    m.pos    = 0;
                }

                const auto p      = (float) m.pos * m.step;
                const auto target = m.from + (m.to - m.from) * p * p * (3.0f - 2.0f * p);
                modInc[i] = (target - modNow[i]) * (1.0f / (float) kModStride);
            }
        }

        for (int i = 0; i < N; ++i)
            modNow[i] += modInc[i];

        modTick = (modTick + 1) % kModStride;
    }

    /** Modulators back to rest, seeded per line so a render is repeatable. */
    void resetModulation() noexcept
    {
        for (int i = 0; i < N; ++i)
        {
            mod[(size_t) i] = {};
            mod[(size_t) i].rng = 0x9e3779b9u * (std::uint32_t) (i + 1);
            modNow[i] = modInc[i] = 0.0f;
        }
        modTick = 0;
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

    /** **In double, coefficients and state.** At 192 kHz a shelf at 140 Hz has
        its pole within 0.005 of z = 1, and rounding its coefficients to float
        moved the realised DC gain by more than the loss the line is meant to
        apply: up to 1.0071 where the design said 0.9993, and a tail that grew
        to +416 dBFS in two minutes (QA, 2026-10-03). In double the realised
        gain is the design to 1e-12. Two biquads a line, so the cost is small;
        the lines themselves stay float. */
    struct Filter
    {
        double g = 1.0;
        double lb[3] { 1.0, 0.0, 0.0 }, la[2] {}, lz[2] {};   ///< low shelf, TDF-II
        double hb[3] { 1.0, 0.0, 0.0 }, ha[2] {}, hz[2] {};   ///< high shelf
        double loDc = 1.0, hiNyq = 1.0;                         ///< the anchors, for the tests
    };

    /** **Denormals are flushed by hand, not dodged.** 10 section 4 suggested an
        alternating +-1e-20 into one line; that leaves a reset network that
        is never silent, and 11 section 6 wants zeros in to give exactly zeros
        out and a tail that reaches exactly 0.0f with FTZ off. So every value
        the network stores -- the lines, the diffusers, the filter states --
        is zero below 1e-15, 300 dB down, and nothing it keeps can go
        denormal. */
    static float flush (float v) noexcept { return std::abs (v) < 1.0e-15f ? 0.0f : v; }

    static double flushD (double v) noexcept { return std::abs (v) < 1.0e-15 ? 0.0 : v; }

    static double biquad (const double* b, const double* a, double* z, double x) noexcept
    {
        const auto y = b[0] * x + z[0];
        z[0] = flushD (b[1] * x - a[0] * y + z[1]);
        z[1] = flushD (b[2] * x - a[1] * y);
        return y;
    }

    static float absorb (Filter& f, float x) noexcept
    {
        return (float) biquad (f.hb, f.ha, f.hz, biquad (f.lb, f.la, f.lz, (double) x * f.g));
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
    static void shelf (bool low, double gain, const Knee& k, double* b, double* a) noexcept
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

        b[0] = b0 / a0; b[1] = b1 / a0; b[2] = b2 / a0;
        a[0] = a1 / a0; a[1] = a2 / a0;
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

        f.g     = std::pow (10.0, (double) aMid * 0.05);
        f.loDc  = std::pow (10.0, (double) (aLo - aMid) * 0.05);
        f.hiNyq = std::pow (10.0, (double) (aHi - aMid) * 0.05);
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

        // A length move in flight: the path it is fading to as well. Only
        // coefficients are written, so its running state is left alone.
        if (fading)
            for (int i = 0; i < N; ++i)
                design ((float) fadeTo[(size_t) i], lo, hi, filtTo[(size_t) i]);
    }

    /** A length move starts (`fadeTo` and `levelTo` are already set). It runs
        until the longest line, old or new, has been read out and the 30 ms
        crossover after it is done; see `process` for the weights. The path
        it is going to is designed here and its filters start from empty
        state, which costs nothing: that path's weight is zero until the
        first sample written after this one comes back round. */
    void beginMove() noexcept
    {
        fading  = true;
        movePos = 0;

        int longest = 0;
        for (int i = 0; i < N; ++i)
            longest = std::max ({ longest, lengths[(size_t) i], fadeTo[(size_t) i] });
        moveEnd = longest + fadeLength;

        designAll();
        for (auto& f : filtTo)
            f.lz[0] = f.lz[1] = f.hz[0] = f.hz[1] = 0.0;
    }

    /** The old path's weight for a sample written `k` samples after a move
        began: one for anything written before it, a raised cosine down to
        zero over the crossfade, zero after. The new path's is one minus this. */
    float writtenBefore (int k) const noexcept
    {
        return k <= 0 ? 1.0f : k >= fadeLength ? 0.0f : moveRamp[(size_t) k];
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
    bool started = false;   ///< a block has played since prepare() or reset(); until one has, a request lands
    int movePos = 0, moveEnd = 0, dipPos = 0;   ///< a length move, in samples since it began
    std::vector<float> moveRamp;                ///< `writtenBefore`, built in prepare()

    std::vector<float> preLine;
    int preLength = 0, preIdx = 0, preDelaySamples = 0, preDelayTarget = 0, prePos = 0;
    bool preFading = false;

    std::vector<float> bloomLine;                     ///< the diffused input, `kAttackSpanMs` of it
    int bloomLength = 0, bloomIdx = 0, bloomPos = 0;
    Bloom bloomNow, bloomNext;                        ///< `bloomNext` is the feed an ATTACK change is fading to
    float attackNow = 0.0f, attackTarget = 0.0f;
    bool bloomFading = false;

    std::array<std::vector<float>, 4> apLine;
    std::array<int, 4> apLength {}, apIdx {};
    std::array<float, 4> apGain { kAllpassGain, kAllpassGain, kAllpassGain, kAllpassGain };
    float fourWeight = 0.0f, fourTarget = 0.0f;   ///< 0 takes the diffusers after two, 1 after four
    float levelNow = 1.0f, levelTo = 1.0f;        ///< `levelFor`, crossfaded with the lengths

    std::array<Filter, N> filt {}, filtTo {};   ///< `filtTo` is the path a length move is fading to
    std::array<float, 3> smoothed { 1.8f, 1.2f, 0.4f };
    std::array<float, 2> designedKnees { 0.0f, 0.0f };

    float inSign[N] {}, outL[N] {}, outR[N] {};

    std::array<Modulator, N> mod {};
    float modNow[N] {};      ///< each line's deviation this sample, in samples
    float modScale[N] {};    ///< each line's share of MOD RATE, 0.6..1, so no two keep step
    float modDepth = 0.0f;   ///< `modDepthSamples`, taken once a block
    float modInc[N] {};      ///< each line's step a sample, until the next stride
    int   modTick = 0;
    bool  sixPoint = true;   ///< six-point reads below 88.2 kHz, four above; see readAt

};

} // namespace bmo::reverb
