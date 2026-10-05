#pragma once

// The early-reflection generator, milestone M2 of docs/reverb/HANDOFF-linger-dsp.md.
//
// One mono delay line, up to 48 read taps per tap set -- 21 image-source taps
// from TapTables.h scaled by SIZE, plus 27 velvet-noise infill pulses the
// DENSITY bridge fades in -- four order-banded one-pole filters, a
// three-stage feed-forward mixing diffuser that DENSITY brings in above 0.6,
// seven VARIATION positions, and one post-ER high cut. **No recursive allpass
// anywhere in this file**, which is the owner's hardest constraint and the
// reason the density stage is a finite impulse response (10 section 1).
//
// Everything that changes the *table* -- TYPE, SIZE, ER MODE, VARIATION, ER
// SPREAD -- rebuilds a second tap set and crossfades to it over 30 ms with
// raised-cosine windows that sum to one, exactly `DetuneVoice`'s scheme;
// TYPE dips the bus to silence and swaps at the minimum instead (10 section
// 5). DENSITY is a continuous weighting and needs no crossfade. Nothing here
// allocates outside `prepare()`.
//
// JUCE-free. Owned by the DSP pass; the tests in tests/dsp/ReverbDspTests.cpp
// are 11 section 6's ER block.

#include "modules/reverb/dsp/TapTables.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

namespace bmo::reverb
{

//==============================================================================
/** What decides the table. Any change to one of these fields rebuilds a tap
    set; nothing else the generator is told does. */
struct ErConfig
{
    int   type      = 0;       ///< index into kTypeTaps
    float sizeM     = kReferenceSizeM;
    int   mode      = 0;       ///< 0 taps, 1 energy
    int   variation = 4;       ///< 0..6
    float spreadMs  = 80.0f;   ///< Energy's envelope sigma
    float shape     = 1.0f;    ///< the rise exponent p, a per-type constant

    bool operator== (const ErConfig& o) const noexcept
    {
        return type == o.type && sizeM == o.sizeM && mode == o.mode
            && variation == o.variation && spreadMs == o.spreadMs && shape == o.shape;
    }

    bool operator!= (const ErConfig& o) const noexcept { return ! (*this == o); }
};

/** Whether ER SPREAD reaches the cluster in this ER MODE (0 taps, 1 energy).
    It is Energy's envelope sigma and nothing else: Taps places the room's own
    image sources and fills between them on their contour, and never reads it.

    **The panel dims ER SPREAD on this answer**, and `DspCore` stops handing
    the generator a SPREAD it would not use -- which before 2026-10-02 rebuilt
    an identical table and ran a 30 ms crossfade between two copies of it on
    every move of the knob in Taps, and held off any SIZE move meanwhile. One
    function for both, as `eqNodeHasGain` is for GAIN, so the look and the
    engine cannot disagree about which knob is live. */
inline constexpr bool erSpreadIsLive (int mode) noexcept { return mode == 1; }

//==============================================================================
class ErGenerator
{
public:
    static constexpr int   kMaxTaps        = 48;                              ///< 10 section 3
    static constexpr int   kNumInfill      = kMaxTaps - kNumReferenceTaps;    ///< 27
    static constexpr int   kNumBands       = 4;                               ///< proximity + orders 1..3
    static constexpr int   kNumStages      = 3;                               ///< diffuser
    static constexpr float kMaxSizeM       = 80.0f;
    static constexpr float kRampWidth      = 0.08f;                           ///< the density ramp, CALIBRATE
    /** A core tap's threshold: one ramp width *below* zero, so w = 1 at
        DENSITY 0 and the 21 never switch off. With theta at 0 the ramp
        clamp((D - theta) / width, 0, 1) is 0 at D = 0, which is the whole
        cluster silent; the panel's count already assumes the core is always
        on (`LingerScreen::activeTapCount`). */
    static constexpr float kCoreTheta      = -kRampWidth;
    static constexpr float kCrossfadeMs    = 30.0f;
    static constexpr float kSizeRetrigger  = 0.01f;                           ///< 1 % accumulated |dS|
    static constexpr float kProximityMs    = 8.0f;                            ///< taps inside this go through the 1.4 kHz band
    static constexpr float kProximityHz    = 1400.0f;                         ///< "its one-pole below 1.5 kHz" (10 section 3)
    static constexpr float kCutoffTopHz    = 16000.0f;                        ///< f_k = 16 kHz * lambda^n * (1 m / d)^kappa
    static constexpr float kCutoffLambda   = 0.80f;                           ///< CALIBRATE
    static constexpr float kCutoffKappa    = 0.20f;                           ///< CALIBRATE
    static constexpr float kCombDelayMs    = 8.7f;                            ///< VARIATION 6's complementary pair, CALIBRATE
    static constexpr float kEnergyWindowMax = 500.0f;                         ///< Energy mode's window ceiling, ms
    static constexpr float kMaxSplitMs     = 2.0f;                            ///< the per-channel tap offset at full VARIATION
    static constexpr float kMinSeparationMs = 0.9f;                           ///< no two pulses closer than this, 10 section 3

    /** Diffuser stage thresholds on DENSITY and the width of each fade. */
    static constexpr float kStageAt[kNumStages] { 0.60f, 0.75f, 0.90f };
    static constexpr float kStageWidth = 0.10f;

    /** How far DENSITY (0..1) or a filter coefficient (relative) moves before
        a moving control re-runs the diffuser's normaliser; see
        `updateBandCoefs`. A quarter of a percent of DENSITY moves a stage's
        fade by 0.025 of its width, which is under 0.2 dB on the normaliser at
        its steepest, and the exact figure lands the block the control stops. */
    static constexpr float kNormDensityStep = 0.0025f;
    static constexpr float kNormCoefStep    = 0.02f;

    /** The longest ramp a normaliser update takes to land; see
        `updateBandCoefs`. */
    static constexpr float kNormRampMs = 2.0f;

    /** VARIATION 0..5: lateral spread of the bearings, and the fraction of
        each tap's energy moved to channel-specific times. Both rise
        monotonically, which is what makes gamma fall monotonically. Position
        6 is the complementary comb pair and uses neither. */
    static constexpr float kSpread[6] { 0.12f, 0.30f, 0.50f, 0.70f, 0.86f, 1.00f };
    static constexpr float kSplit[6]  { 0.00f, 0.15f, 0.35f, 0.55f, 0.75f, 0.92f };

    //==========================================================================
    void prepare (double newSampleRate, int maxBlockSize)
    {
        sampleRate = newSampleRate > 0.0 ? newSampleRate : 48000.0;
        blockSize  = std::max (1, maxBlockSize);

        // The line holds the longest cluster any table reaches at 80 m, or the
        // Energy window, whichever is longer, plus the split offset -- sized
        // once, here, for the largest type.
        float longestMs = kEnergyWindowMax;
        for (int t = 0; t < kNumTapTypes; ++t)
            longestMs = std::max (longestMs, erSpanMsAt (t, kMaxSizeM));

        lineLength = (int) std::ceil ((longestMs + kMaxSplitMs + 1.0f) * 0.001f * sampleRate) + 8;
        line.assign ((size_t) lineLength, 0.0f);

        combLength = (int) std::ceil (kCombDelayMs * 0.001f * sampleRate) + 4;
        combLine.assign ((size_t) combLength, 0.0f);

        for (int b = 0; b < kNumBands; ++b)
            for (int s = 0; s < kNumStages; ++s)
                for (int ch = 0; ch < 2; ++ch)
                    stageLine[b][s][ch].assign ((size_t) stageLength(), 0.0f);

        fadeLength = std::max (1, (int) std::lround (kCrossfadeMs * 0.001f * sampleRate));

        for (int s = 0; s < kNumStages; ++s)
            for (int i = 0; i < 4; ++i)
                stageDelay[s][i] = stageDelaySamples (s, i);

        lastNormDensity = -1.0f;

        // **The hi-cut is designed at the rate it will run at.** Its target
        // is a coefficient, and it was computed at whatever rate the
        // generator had when ER HI-CUT was last sent -- for a host that sends
        // the values before the first prepare(), the 48 kHz it was
        // constructed with. reset() below snaps the coefficient to that
        // target, so at 96 kHz a fresh instance started +2.11 dB high at a
        // 7 kHz corner (+2.77 dB at 192 kHz) and glided to the right figure
        // over about 120 ms once DspCore re-sent the value; a caller that
        // drives the generator directly kept the wrong corner until it next
        // sent one (2026-10-05).
        if (hiCutHz > 0.0f)
            setHiCut (hiCutHz);

        reset();
    }

    /** **After prepare() and reset() the generator is at its requested
        settings from the first sample**, and a setting sent before the first
        block lands there too.

        Until 2026-10-05 a fresh instance started from the construction
        defaults -- Room's table at the reference SIZE, DENSITY 0.5 -- and
        moved to what it was asked for on its first block: a TYPE dip, a
        SIZE crossfade, a DENSITY glide. Nothing is sounding then, so there
        was nothing for a move to protect, and the move itself was the
        defect: Hall at 40 m and DENSITY 100 % differed from an instance
        already settled there by -37.9 dB re the output peak at 13.7 ms
        (48 kHz, sine plus noise), carried into the tail for 1.4 s. And
        reset(), which kept what it had applied, did not land where a fresh
        instance did. Now both build the requested table directly, start
        DENSITY and the hi-cut at their targets and run the normaliser at
        once; `started` holds that open until the first block, so values
        sent between prepare() or reset() and audio land rather than move.
        After the first block a move behaves as it always has.

        That also covers a move in flight, which the 2026-09-30 review asked
        to be finished rather than dropped: nothing of it is kept. */
    void reset()
    {
        // The normaliser's bookkeeping goes back to where construction left
        // it, so its first run is the forced one a fresh instance makes.
        lastWeightedDensity    = -1.0f;
        lastNormDensity        = -1.0f;
        lastNormHiCut          = -1.0f;
        lastBlockHiCut         = -1.0f;
        lastBlockDensityTarget = -1.0f;
        samplesSinceNormRun    = 0;
        normRampLeft           = 0;

        for (int b = 0; b < kNumBands; ++b)
        {
            bandCoef[b] = 1.0f;
            lastNormBandCoef[b] = -1.0f;

            for (int s = 0; s < kNumStages; ++s)
            {
                stageNorm[b][s]    = 1.0f;
                stageNormNow[b][s] = 1.0f;
                stageNormInc[b][s] = 0.0f;
            }
        }

        std::fill (line.begin(), line.end(), 0.0f);
        std::fill (combLine.begin(), combLine.end(), 0.0f);
        for (auto& b : stageLine)
            for (auto& s : b)
                for (auto& l : s)
                    std::fill (l.begin(), l.end(), 0.0f);

        writeIdx = 0;
        combIdx  = 0;
        stageIdx = 0;
        fadePos  = 0;
        fading   = false;
        dipping  = false;
        dipPos   = 0;

        for (auto& b : band)
            for (auto& f : b)
                f.reset();

        hiCutL.reset();  hiCutL2.reset();
        hiCutR.reset();  hiCutR2.reset();

        land();
        started = false;
    }

    //== Parameters ============================================================

    /** A table change. Cheap when nothing changed; otherwise it schedules a
        rebuild, which happens on the audio thread from the tables and never
        allocates. */
    void setConfig (const ErConfig& c)
    {
        requested = c;
    }

    void setDensity (float d) noexcept { densityTarget = std::clamp (d, 0.0f, 1.0f); }

    void setHiCut (float hz) noexcept
    {
        hiCutHz = hz;
        const auto f = std::clamp (hz, 20.0f, (float) sampleRate * 0.45f);
        hiCutTarget = hiCutCoefFor (f, sampleRate);
    }

    //== Processing ============================================================

    /** `in` is the mono feed; `outL`/`outR` receive the ER bus at table
        level, before the ER fader. Sizes are `numSamples`. */
    void process (const float* in, float* outL, float* outR, int numSamples)
    {
        // Nothing has played since prepare() or reset(): whatever was sent
        // since lands, rather than starting a move from what was there.
        if (! started)
        {
            land();
            started = true;
        }

        applyPendingConfig();
        updateDensity();
        updateBandCoefs();

        const auto smooth = smoothingCoef();

        samplesSinceNormRun = (int) std::min<long long> ((long long) samplesSinceNormRun + numSamples, 1 << 30);

        for (int i = 0; i < numSamples; ++i)
        {
            advanceNormRamp();
            line[(size_t) writeIdx] = in[i];

            float acc[kNumBands][2] {};

            // Crossfade weights, raised cosine, summing to one.
            float wA = 1.0f, wB = 0.0f;

            if (fading)
            {
                const auto ph = (float) fadePos / (float) fadeLength;
                wB = 0.5f * (1.0f - std::cos (ph * 3.14159265f));
                wA = 1.0f - wB;
            }

            accumulate (sets[active], wA, acc);

            if (fading)
                accumulate (sets[1 - active], wB, acc);

            // The diffuser runs on each band's raw tap train, **before** its
            // pole: there its reads see impulses with no tails to correlate
            // against, which is what makes the mix energy-neutral (see
            // `diffuse`). VARIATION 6 bypasses it: its complementary pair is
            // the decorrelation, and the pair needs the centred cluster.
            const bool comb = sets[active].comb;

            if (! comb)
                for (int b = 0; b < kNumBands; ++b)
                    diffuse (b, acc[b][0], acc[b][1]);

            if (++stageIdx >= stageLength())
                stageIdx = 0;

            float l = 0.0f, r = 0.0f;

            for (int b = 0; b < kNumBands; ++b)
            {
                l += band[b][0].process (acc[b][0], bandCoef[b]);
                r += band[b][1].process (acc[b][1], bandCoef[b]);
            }

            // VARIATION 6: L = M + D, R = M - D, D a delayed copy of the
            // centred cluster, so the mono sum is exactly flat (10 section 3).
            //
            // **The comb line is written on every sample at every VARIATION**,
            // and read only at 6. Written only at 6, it kept the last 8.7 ms it
            // heard there for as long as VARIATION was elsewhere, and coming
            // back read that out again -- a burst replayed at -19 dBFS a
            // second after the input had stopped (the review of PR #27,
            // 2026-09-30). Written always, it holds this generator's own last
            // 8.7 ms, so entering 6 reads current audio: nothing stale and no
            // hole where D should be. One store and an index step a sample;
            // clearing it on the way out instead would have cost a sweep of
            // the line inside one sample and left D silent for 8.7 ms on the
            // way back in. At 6 the set is built centred, l == r, and
            // 0.5 (l + r) is l exactly.
            {
                const auto m = 0.5f * (l + r);
                combLine[(size_t) combIdx] = m;
                auto rd = combIdx - sets[active].combDelay;
                if (rd < 0) rd += combLength;
                const auto d = combLine[(size_t) rd];
                if (++combIdx >= combLength) combIdx = 0;

                if (comb)
                {
                    l = (m + d) * 0.70710678f;
                    r = (m - d) * 0.70710678f;
                }
            }

            // **Landing exactly.** In float the step falls under half an ulp
            // of the coefficient some way short of the target, and until
            // 2026-10-05 it stopped there for good: 1.3e-5 to 5.7e-5 short
            // after a move, which held every later render -111 to -117 dB
            // from one that never moved. A step that no longer moves the
            // coefficient has arrived.
            {
                const auto next = hiCutCoef + (hiCutTarget - hiCutCoef) * smooth;
                hiCutCoef = next == hiCutCoef ? hiCutTarget : next;
            }
            l = hiCutL2.process (hiCutL.process (l, hiCutCoef), hiCutCoef);
            r = hiCutR2.process (hiCutR.process (r, hiCutCoef), hiCutCoef);

            // TYPE's dip: raised cosine to silence, swap, and back.
            if (dipping)
            {
                const auto half = fadeLength;
                const auto ph   = (float) dipPos / (float) half;
                const auto g    = dipPos < half ? 0.5f * (1.0f + std::cos (ph * 3.14159265f))
                                                : 0.5f * (1.0f - std::cos ((ph - 1.0f) * 3.14159265f));
                l *= g;
                r *= g;

                if (++dipPos == half)
                {
                    rebuild (current, sets[active]);
                    sizeAtBuild = current.sizeM;
                }
                else if (dipPos >= 2 * half)
                {
                    dipping = false;
                    dipPos  = 0;
                }
            }

            outL[i] = l;
            outR[i] = r;

            if (fading && ++fadePos >= fadeLength)
            {
                fading = false;
                fadePos = 0;
                active  = 1 - active;
            }

            if (++writeIdx >= lineLength)
                writeIdx = 0;
        }
    }

    //== For the tests and the measurement tool ================================

    float spanMs() const noexcept { return sets[active].spanMs; }

    /** The ER hi-cut's coefficient in use, and the one it is gliding to. */
    float hiCutCoefficient() const noexcept { return hiCutCoef; }
    float hiCutTargetCoefficient() const noexcept { return hiCutTarget; }
    int   tapCount() const noexcept { return sets[active].count; }
    bool  isFading() const noexcept { return fading || dipping; }

    /** How many times the diffuser's normaliser has run since construction:
        what the tests count to show a moving control no longer runs it on
        every block. */
    long long normaliserRuns() const noexcept { return normRuns; }
    float bandCutoffHz (int b) const noexcept { return sets[active].cutoffHz[(size_t) b]; }
    const ErConfig& config() const noexcept { return current; }

    /** How many taps the density weighting has switched on, so the panel's
        count and the engine's are one arithmetic. */
    int activeTapCount() const noexcept
    {
        int n = 0;
        for (int k = 0; k < sets[active].count; ++k)
            if (sets[active].taps[(size_t) k].weight > 0.0f)
                ++n;
        return n;
    }

    /** Energy mode's window in ms at this ER SPREAD: the plateau's end,
        0.6 sigma, plus two and a half sigmas of the handover, to the
        500 ms the line is sized for. **The one copy**: `rebuild()` lays the
        pulses over it and `DspCore::tailSecondsFor` reports it, so the tail a
        host is told and the cluster that plays cannot come apart -- they had,
        until the 2026-09-30 review, when the tail added the Taps span in both
        modes. */
    static float energyWindowMs (float spreadMs) noexcept
    {
        const auto sigma = std::clamp (spreadMs, 5.0f, 200.0f);
        return std::min (0.60f * sigma + 2.5f * sigma, kEnergyWindowMax);
    }

    /** Infill tap `i`'s activation threshold: the panel's own formula. */
    static constexpr float infillThreshold (int i) noexcept
    {
        return (float) (i + 1) / (float) (kNumInfill + 1);
    }

    //== The band law, shared with the offline generator ========================
    //
    // Both the audit in ImageSource.h and the density renormalisation below
    // need to know how much energy a tap keeps after its band's one-pole,
    // because a tap inside 8 ms through a 1.4 kHz pole keeps a tenth of it.
    // One copy of the arithmetic, here, so the ceilings the tables were cut
    // to and the filters the engine runs cannot disagree.

    /** Which band a tap of this time and order runs through. */
    static constexpr int bandFor (float timeMs, int order) noexcept
    {
        return timeMs < kProximityMs ? 0 : std::clamp (order, 1, 3);
    }

    /** f = 16 kHz * lambda^n * (1 m / d)^kappa at the band's mean path, with
        d ~ 0.44 * SIZE + 0.343 * t_ms: the reference geometry's own direct
        distance scaled, plus the excess path (10 section 3). Band 0 is the
        proximity pole. */
    static float cutoffHzFor (int band, float sizeM, float meanMs) noexcept
    {
        if (band == 0)
            return kProximityHz;

        const auto d = std::max (1.0f, 0.44f * sizeM + 0.343f * meanMs);
        return std::clamp (kCutoffTopHz * std::pow (kCutoffLambda, (float) band) * std::pow (1.0f / d, kCutoffKappa),
                           1500.0f, 20000.0f);
    }

    /** The energy an impulse keeps through a one-pole low-pass at this
        corner: c / (2 - c) for coefficient c. */
    static float onePoleEnergyGain (float hz, double rate) noexcept
    {
        const auto c = onePoleCoefFor (hz, rate);
        return c / (2.0f - c);
    }

    /** The coefficient of y += c (x - y) whose magnitude is **exactly**
        -3 dB at `hz`: with a = 1 - c and w the corner in radians,
        a = (2 - cos w) - sqrt((2 - cos w)^2 - 1). The impulse-invariant
        1 - exp(-w) puts the corner a good way above `hz` once `hz` is a
        fair fraction of the rate -- 16 kHz at 48 kHz lands nearer 22 kHz --
        and the ER hi-cut's test asks for the corner within 10 %. Every
        one-pole in the generator designs through here, so the energy the
        audit assumes is the energy the pole keeps. */
    static float onePoleCoefFor (float hz, double rate) noexcept
    {
        const auto w = 2.0f * 3.14159265f * std::clamp (hz, 1.0f, (float) rate * 0.499f) / (float) rate;
        const auto k = 2.0f - std::cos (w);
        const auto a = k - std::sqrt (std::max (k * k - 1.0f, 0.0f));
        return std::clamp (1.0f - a, 1.0e-6f, 1.0f);
    }

    /** The coefficient for **each** of the ER hi-cut's two identical poles,
        so that the pair is -3 dB at `hz` and falls at 12 dB/octave above it.

        Frosty asked for 12 dB/octave on 2026-09-26: at 6 dB/octave, moving
        the corner from 7 kHz to 3 kHz changed the cluster by only 3-4 dB at
        4-8 kHz, "too subtle for such a big difference". Each pole is solved
        to be -1.5 dB at `hz` (power 1/sqrt2) exactly as `onePoleCoefFor`
        solves one pole to -3 dB (power 1/2): the same quadratic with
        k = (p - cos w) / (p - 1), p = sqrt2 here and 2 there. */
    static float hiCutCoefFor (float hz, double rate) noexcept
    {
        constexpr float p = 1.41421356f;
        const auto w = 2.0f * 3.14159265f * std::clamp (hz, 1.0f, (float) rate * 0.499f) / (float) rate;
        const auto k = (p - std::cos (w)) / (p - 1.0f);
        const auto a = k - std::sqrt (std::max (k * k - 1.0f, 0.0f));
        return std::clamp (1.0f - a, 1.0e-6f, 1.0f);
    }

    /** |H(f)| of the hi-cut's pair of poles with that per-pole coefficient. */
    static double hiCutMagnitudeDb (float coef, double hz, double rate) noexcept
    {
        return 2.0 * onePoleMagnitudeDb (coef, hz, rate);
    }

    /** |H(f)| of that pole, for the tests to compare the running filter
        against its own design. */
    static double onePoleMagnitudeDb (float coef, double hz, double rate) noexcept
    {
        const auto a = 1.0 - (double) coef;
        const auto w = 2.0 * 3.14159265358979 * hz / rate;
        const auto den = std::sqrt (1.0 - 2.0 * a * std::cos (w) + a * a);
        return 20.0 * std::log10 ((double) coef / den);
    }

private:
    //==========================================================================
    struct OnePole
    {
        float z = 0.0f;
        void  reset() noexcept { z = 0.0f; }
        // **Flushed below 1e-15, 300 dB down**, as everything the late network
        // keeps is. Decaying geometrically in float, the state reaches the
        // denormal range and then sticks there -- z (1 - c) rounds back to the
        // same smallest value -- so a silent instance went on emitting 1e-45
        // for good with FTZ off (QA's long run, 2026-10-03). 11 section 6 asks
        // for a tail that reaches exactly 0.0f.
        float process (float x, float coef) noexcept
        {
            z += coef * (x - z);
            if (std::abs (z) < 1.0e-15f) z = 0.0f;
            return z;
        }
    };

    struct RtTap
    {
        int   delay  = 0;      ///< the common read, samples
        int   delayL = 0;      ///< the channel-specific reads (VARIATION)
        int   delayR = 0;
        float base   = 0.0f;   ///< table gain at this SIZE, before density
        float theta  = 0.0f;   ///< density threshold; 0 for a core tap
        float gL = 0.0f, gR = 0.0f;   ///< pan gains, common part
        float sL = 0.0f, sR = 0.0f;   ///< pan gains, split part
        float weight = 0.0f;   ///< base * w(D) * norm, refreshed per block
        float wl = 0.0f, wr = 0.0f, wsl = 0.0f, wsr = 0.0f;   ///< weight folded into the four gains
        float bandEnergy = 1.0f;   ///< what its band's one-pole keeps of its energy
        int   bandIdx = 1;
    };

    struct TapSet
    {
        std::array<RtTap, kMaxTaps> taps {};
        int   count = 0;
        float spanMs = 0.0f;
        float coreEnergy = 0.0f;   ///< sum of base^2 over theta == 0 taps
        std::array<float, kNumBands> cutoffHz { kProximityHz, 8000.0f, 6000.0f, 4500.0f };
        bool  comb = false;
        int   combDelay = 0;
    };

    //==========================================================================
    float onePoleCoef (float hz) const noexcept { return onePoleCoefFor (hz, sampleRate); }

    float smoothingCoef() const noexcept
    {
        return 1.0f - std::exp (-1.0f / (0.020f * (float) sampleRate));
    }

    int stageLength() const noexcept
    {
        return (int) std::ceil (2.0f * 0.001f * sampleRate) + 8;   // the longest stage read is 84/48 ms
    }

    /** The requested settings, taken as they are: the table built from
        them, DENSITY and the hi-cut at their targets, and the normaliser
        due to run at once on the next block. Only where nothing is sounding
        -- reset(), and the first block after it. */
    void land() noexcept
    {
        current = requested;
        active  = 0;
        density = densityTarget;
        lastWeightedDensity = density;   // rebuild() weighs the set at it

        // Never prepared, there is no line for a table to index: its delay
        // ceiling is negative and the build's clamps would be handed
        // inverted bounds (a host may reset() before it prepares). The
        // first prepare() lands again and builds it.
        if (lineLength > 0)
            rebuild (current, sets[active]);
        sizeAtBuild = current.sizeM;
        hiCutCoef = hiCutTarget;
        lastNormDensity = -1.0f;
    }

    void applyPendingConfig()
    {
        if (fading || dipping)
            return;

        if (requested == current)
            return;

        const bool typeChanged = requested.type != current.type;

        // SIZE alone accumulates: a move under 1 % of the size the live table
        // was built at is remembered but does not rebuild.
        if (! typeChanged
             && requested.mode == current.mode && requested.variation == current.variation
             && requested.spreadMs == current.spreadMs && requested.shape == current.shape
             && std::abs (requested.sizeM - sizeAtBuild) < kSizeRetrigger * sizeAtBuild)
        {
            current = requested;
            return;
        }

        current = requested;

        if (typeChanged)
        {
            dipping = true;
            dipPos  = 0;
            return;
        }

        rebuild (current, sets[1 - active]);
        sizeAtBuild = current.sizeM;
        fading  = true;
        fadePos = 0;
    }

    void updateDensity()
    {
        // 20 ms one-pole on the target, stepped per block; the weighting is
        // continuous in D so this is all the smoothing it needs.
        const auto coef = 1.0f - std::exp (-(float) blockSize / (0.020f * (float) sampleRate));
        density += (densityTarget - density) * coef;

        if (std::abs (density - densityTarget) < 1.0e-5f)
            density = densityTarget;

        if (density == lastWeightedDensity && ! fading && ! dipping)
            return;

        lastWeightedDensity = density;
        weigh (sets[active]);
        if (fading)
            weigh (sets[1 - active]);
    }

    void updateBandCoefs()
    {
        const auto& a = sets[active];
        const auto& b = sets[1 - active];
        const auto  w = fading ? 0.5f * (1.0f - std::cos ((float) fadePos / (float) fadeLength * 3.14159265f)) : 0.0f;

        for (int i = 0; i < kNumBands; ++i)
            bandCoef[i] = onePoleCoef (a.cutoffHz[(size_t) i] * (1.0f - w) + b.cutoffHz[(size_t) i] * w);

        // **The normaliser runs on a step, not on every block a control is
        // moving.** It was every block until 2026-10-02, which at 192 kHz / 32
        // cost 13.0 % of a core under a DENSITY ramp and 36.6 % under an ER
        // HI-CUT ramp (`measure_reverb bench`, ICE QUEEN; 3.7 % held), for a
        // figure that moved by hundredths of a dB between blocks. Now 2.9 %
        // and 4.3 %, inside 10 section 6's 5 %. It runs
        // when DENSITY has moved kNormDensityStep, or a band's or the hi-cut's
        // coefficient kNormCoefStep (relative), since the last run -- and once
        // more, exactly, the block everything has settled, so a held setting
        // is always normalised for exactly what it is. A negative
        // `lastNormDensity` is the "now, unconditionally" the rebuild paths
        // ask for.
        const auto rel = [] (float x, float ref) { return std::abs (x - ref) > kNormCoefStep * std::max (ref, 1.0e-6f); };

        bool stale = density != lastNormDensity || hiCutTarget != lastNormHiCut;
        bool far   = lastNormDensity < 0.0f
                  || std::abs (density - lastNormDensity) > kNormDensityStep
                  || rel (hiCutTarget, lastNormHiCut);

        for (int i = 0; i < kNumBands; ++i)
        {
            stale = stale || bandCoef[i] != lastNormBandCoef[i];
            far   = far   || rel (bandCoef[i], lastNormBandCoef[i]);
        }

        // **Moving is the target changing, not the smoother lagging it.** A
        // ramp slower than 1e-5 of DENSITY a block is snapped to its target
        // every block by `updateDensity`, so "density has not reached its
        // target" read false all through it, the settle rule fired on every
        // block, and the step bought nothing: 119 999 runs of 120 000 at
        // 192 kHz / 32 for 0.6 to 1.0 over 20 s (QA, 2026-10-02). The target
        // is compared block to block, as the hi-cut's always was.
        const bool moving = density != densityTarget || densityTarget != lastBlockDensityTarget
                         || fading || hiCutTarget != lastBlockHiCut;
        lastBlockDensityTarget = densityTarget;
        lastBlockHiCut         = hiCutTarget;

        if (far || (stale && ! moving))
        {
            const bool forced = lastNormDensity < 0.0f;
            updateStageNorms();
            ++normRuns;

            // **An update is a ramp, not a step** (QA, 2026-10-02: stepped,
            // it measured x14 on the second difference at 48 kHz / 32). Linear,
            // from the value in use to the new one, over kNormRampMs or the time
            // since the last update, whichever is shorter -- so the sparse
            // updates of a slow ramp get 2 ms, and when it runs every block the
            // ramp spans that block and lags no further than the every-block
            // engine did. 2 ms rather than 5: both pass the second-difference
            // bound, and 5 ms let the level drift 0.29 dB from the every-block
            // engine mid-ramp where 2 ms holds it to 0.18. A forced run
            // (prepare, reset, a rebuilt table) has nothing to fade from and
            // lands at once.
            const auto ramp = std::clamp (samplesSinceNormRun, 1, std::max (1, (int) std::lround (kNormRampMs * 0.001f * sampleRate)));
            normRampLeft = forced ? 0 : ramp;
            for (int b = 0; b < kNumBands; ++b)
                for (int s = 0; s < kNumStages; ++s)
                {
                    if (forced)
                        stageNormNow[b][s] = stageNorm[b][s];
                    stageNormInc[b][s] = (stageNorm[b][s] - stageNormNow[b][s]) / (float) ramp;
                }
            samplesSinceNormRun = 0;
        }
    }

    /** One sample of the normaliser's ramp. Lands exactly on the target. */
    void advanceNormRamp() noexcept
    {
        if (normRampLeft <= 0)
            return;

        if (--normRampLeft == 0)
        {
            for (int b = 0; b < kNumBands; ++b)
                for (int s = 0; s < kNumStages; ++s)
                    stageNormNow[b][s] = stageNorm[b][s];
            return;
        }

        for (int b = 0; b < kNumBands; ++b)
            for (int s = 0; s < kNumStages; ++s)
                stageNormNow[b][s] += stageNormInc[b][s];
    }

    /** The diffuser stage delays, in samples at this rate: a mixed-radix
        ruler in units of 1/48 ms (see `diffuse`). */
    static constexpr int kStageUnits[kNumStages][4] { { 1, 9, 2, 10 }, { 3, 13, 4, 14 }, { 5, 17, 6, 18 } };

    int stageDelaySamples (int stage, int read) const noexcept
    {
        return std::max (1, (int) std::lround ((float) kStageUnits[stage][read] / 48.0f * 0.001f * sampleRate));
    }

    /** The energy an impulse train keeps through the cascade **and its
        band's pole**, made exact for isolated taps.

        Before the pole the four reads of a stage are orthogonal on an
        impulse train, so the crossfade (1 - w) x + w d would need only the
        uncorrelated normaliser; after the pole they are not, because the
        pole's memory spreads each read over its neighbours and they
        overlap. The correction is the pole's own autocorrelation at the
        stage's lags, which for a cascade of three is easiest to get by
        building the cascade's short FIRs for the current weights, running
        them through the pole, and normalising each stage so the filtered
        energy is what the undiffused taps' would be.

        **A panned tap feeds the two channels unequally**, and the stage
        mixes left reads with right reads, so the answer depends on how the
        band's energy is split: `pLL`, `pRR` and `pLR` are the band's summed
        left, right and cross tap energies from `weigh()`, and the model is
        run for an impulse in each channel and combined with them. Exact for
        taps that do not overlap, which the 0.9 ms separation and a cascade
        under 0.75 ms wide give. A few hundred multiplies per band, only when
        DENSITY or a corner moves. */
    void updateStageNorms()
    {
        lastNormDensity = density;
        lastNormHiCut   = hiCutTarget;
        for (int i = 0; i < kNumBands; ++i)
            lastNormBandCoef[i] = bandCoef[i];

        // Below the first stage's threshold no stage is engaged and every
        // normaliser is exactly one; skip the filtered reference entirely.
        if (density <= kStageAt[0])
        {
            for (auto& band : stageNorm)
                for (auto& n : band)
                    n = 1.0f;
            return;
        }

        for (int b = 0; b < kNumBands; ++b)
        {
            const auto c = bandCoef[b];

            // Inner product of two FIRs after each has been through the band's
            // pole **and the ER hi-cut's two**, which also sit after the
            // diffuser and add their own memory to the correlation -- at its
            // most open, 20 kHz, one of them alone moved the lag-1 figure by
            // 0.06 and the sweep by 0.3 dB. On to where the tails are below
            // -80 dB; two poles in cascade ring for about twice as long.
            const auto h = hiCutTarget;
            const auto filteredDot = [c, h] (const std::array<float, kMaxFir>& x, const std::array<float, kMaxFir>& y, int length)
            {
                const auto tail = (int) std::ceil (2.0f * 9.2f / std::max (std::min (c, h), 1.0e-3f));
                float zx = 0.0f, zy = 0.0f, hx = 0.0f, hy = 0.0f, gx = 0.0f, gy = 0.0f, e = 0.0f;
                for (int i = 0; i < length + tail; ++i)
                {
                    zx += c * ((i < length ? x[(size_t) i] : 0.0f) - zx);
                    zy += c * ((i < length ? y[(size_t) i] : 0.0f) - zy);
                    hx += h * (zx - hx);
                    hy += h * (zy - hy);
                    gx += h * (hx - gx);
                    gy += h * (hy - gy);
                    e += gx * gy;
                }
                return e;
            };

            // Four FIRs: the stage outputs (L, R) for an impulse in L, and
            // for an impulse in R.
            std::array<float, kMaxFir> lL {}, rL {}, lR {}, rR {};
            int length = 1;
            lL[0] = 1.0f;
            rR[0] = 1.0f;

            const auto pLL = bandPower[b][0], pRR = bandPower[b][1], pLR = bandPower[b][2];

            const auto energyOf = [&] (const std::array<float, kMaxFir>& a, const std::array<float, kMaxFir>& bb,
                                       const std::array<float, kMaxFir>& cc, const std::array<float, kMaxFir>& d, int n)
            {
                // E = pLL (|H a|^2 + |H b|^2) + pRR (|H c|^2 + |H d|^2) + 2 pLR (<H a, H c> + <H b, H d>)
                return pLL * (filteredDot (a, a, n) + filteredDot (bb, bb, n))
                     + pRR * (filteredDot (cc, cc, n) + filteredDot (d, d, n))
                     + 2.0f * pLR * (filteredDot (a, cc, n) + filteredDot (bb, d, n));
            };

            const auto reference = energyOf (lL, rL, lR, rR, length);

            for (int s = 0; s < kNumStages; ++s)
            {
                const auto w = std::clamp ((density - kStageAt[s]) / kStageWidth, 0.0f, 1.0f);

                if (w <= 0.0f)
                {
                    stageNorm[b][s] = 1.0f;
                    continue;
                }

                const int* d = stageDelay[s];
                const auto longest = std::max (std::max (d[0], d[1]), std::max (d[2], d[3]));

                if (length + longest > kMaxFir || reference <= 1.0e-20f)
                {
                    // Off the end of the scratch at an extreme rate, or a
                    // silent band: the uncorrelated normaliser.
                    stageNorm[b][s] = 1.0f / std::sqrt ((1.0f - w) * (1.0f - w) + w * w);
                    continue;
                }

                std::array<float, kMaxFir> nlL {}, nrL {}, nlR {}, nrR {};
                const auto newLength = length + longest;
                const auto hw = 0.5f * w;

                // dl = (la + lb + rc - rd) / 2, dr = (la - lb + rc + rd) / 2,
                // applied to both impulse cases.
                const auto stage = [&] (const std::array<float, kMaxFir>& l, const std::array<float, kMaxFir>& r,
                                        std::array<float, kMaxFir>& nl, std::array<float, kMaxFir>& nr)
                {
                    for (int i = 0; i < length; ++i)
                    {
                        const auto li = l[(size_t) i], ri = r[(size_t) i];
                        nl[(size_t) i] += (1.0f - w) * li;
                        nr[(size_t) i] += (1.0f - w) * ri;
                        nl[(size_t) (i + d[0])] += hw * li;  nr[(size_t) (i + d[0])] += hw * li;
                        nl[(size_t) (i + d[1])] += hw * li;  nr[(size_t) (i + d[1])] -= hw * li;
                        nl[(size_t) (i + d[2])] += hw * ri;  nr[(size_t) (i + d[2])] += hw * ri;
                        nl[(size_t) (i + d[3])] -= hw * ri;  nr[(size_t) (i + d[3])] += hw * ri;
                    }
                };

                stage (lL, rL, nlL, nrL);
                stage (lR, rR, nlR, nrR);

                const auto e = energyOf (nlL, nrL, nlR, nrR, newLength);
                const auto norm = e > 1.0e-20f ? std::sqrt (reference / e) : 1.0f;

                for (int i = 0; i < newLength; ++i)
                {
                    lL[(size_t) i] = nlL[(size_t) i] * norm;
                    rL[(size_t) i] = nrL[(size_t) i] * norm;
                    lR[(size_t) i] = nlR[(size_t) i] * norm;
                    rR[(size_t) i] = nrR[(size_t) i] * norm;
                }

                length = newLength;
                stageNorm[b][s] = norm;
            }
        }
    }

    /** The density weighting: w_k = clamp((D - theta_k) / ramp, 0, 1), and a
        renormalisation that holds the cluster's energy at the core's --
        **the energy after the band filters**, since an infill pulse in the
        order-2 band and a core tap in the proximity band keep very different
        fractions of what they are given. */
    void weigh (TapSet& s) const noexcept
    {
        float sum = 0.0f;

        for (int k = 0; k < s.count; ++k)
        {
            auto& t = s.taps[(size_t) k];
            const auto w = std::clamp ((density - t.theta) / kRampWidth, 0.0f, 1.0f);
            t.weight = t.base * w;
            sum += t.weight * t.weight * t.bandEnergy;
        }

        const auto norm = sum > 1.0e-12f ? std::sqrt (s.coreEnergy / sum) : 0.0f;

        for (int k = 0; k < s.count; ++k)
        {
            auto& t = s.taps[(size_t) k];
            t.weight *= norm;
            t.wl  = t.weight * t.gL;
            t.wr  = t.weight * t.gR;
            t.wsl = t.weight * t.sL;
            t.wsr = t.weight * t.sR;
        }

        // The band's left, right and cross energies, for the diffuser's
        // normaliser. A split half lands at its own time in its own channel
        // and so counts as left-only or right-only energy.
        if (&s == &sets[active])
        {
            for (auto& p : bandPower)
                p[0] = p[1] = p[2] = 0.0f;

            for (int k = 0; k < s.count; ++k)
            {
                const auto& t = s.taps[(size_t) k];
                auto& p = bandPower[t.bandIdx];
                p[0] += t.wl * t.wl + t.wsl * t.wsl;
                p[1] += t.wr * t.wr + t.wsr * t.wsr;
                p[2] += t.wl * t.wr;
            }
        }
    }

    inline float read (int delay) const noexcept
    {
        auto rd = writeIdx - delay;
        if (rd < 0) rd += lineLength;
        return line[(size_t) rd];
    }

    inline void accumulate (const TapSet& s, float fade, float (&acc)[kNumBands][2]) const noexcept
    {
        for (int k = 0; k < s.count; ++k)
        {
            const auto& t = s.taps[(size_t) k];

            if (t.weight == 0.0f)
                continue;

            const auto x = read (t.delay) * fade;
            acc[t.bandIdx][0] += x * t.wl;
            acc[t.bandIdx][1] += x * t.wr;

            if (t.wsl != 0.0f)
            {
                acc[t.bandIdx][0] += read (t.delayL) * fade * t.wsl;
                acc[t.bandIdx][1] += read (t.delayR) * fade * t.wsr;
            }
        }
    }

    /** Three feed-forward stages, per band. Each mixes four short delayed
        reads of the pair through an orthogonal 2x4 matrix -- every output
        hears every read, rows orthogonal, norm one -- and fades in under
        DENSITY with the uncorrelated-crossfade normaliser. FIR throughout:
        no poles, cannot ring (10 section 1).

        **The rulers are chosen for a flat mono sum** (the owner's rule,
        2026-09-24: octave-smoothed ripple within 6 dB at DENSITY 100 %).
        With the reads a, b from the left line and c, d from the right, and
        a mono cluster in both, the mono sum of a stage is (z^-a + z^-c) / 2
        and the summed power of its two outputs is 2 + cos(w(a - c)) -
        cos(w(b - d)). So a - c = b - d = -1 unit of 1/48 ms: the power sum is
        flat, the mono sum is a two-sample average per stage (-3.7 dB at
        8 kHz over three, which the tests' tilt removal takes out), and the
        long side pair b, d -- 9..18 units -- carries the decorrelation and
        the density. The earlier mixed-radix ruler put the mono sum's notches
        at 2 and 4 kHz and cost 7.7 dB of ripple. The whole cascade is 42
        units, under the 0.9 ms tap spacing, so no copy of a tap lands on
        another; the coincidences among a tap's own paths are in the
        normaliser's model. It runs before the band's pole so that the train
        it sees is impulses, not tails. CALIBRATE: the listening pass
        decides. */
    void diffuse (int bandIdx, float& l, float& r) noexcept
    {
        const auto len = stageLength();

        for (int s = 0; s < kNumStages; ++s)
        {
            const auto w = std::clamp ((density - kStageAt[s]) / kStageWidth, 0.0f, 1.0f);

            auto& bl = stageLine[bandIdx][s][0];
            auto& br = stageLine[bandIdx][s][1];
            bl[(size_t) stageIdx] = l;
            br[(size_t) stageIdx] = r;

            if (w > 0.0f)
            {
                const auto rd = [this, len] (const std::vector<float>& buf, int delay)
                {
                    auto i = stageIdx - delay;
                    if (i < 0) i += len;
                    return buf[(size_t) i];
                };

                const auto la = rd (bl, stageDelay[s][0]), lb = rd (bl, stageDelay[s][1]);
                const auto rc = rd (br, stageDelay[s][2]), rd_ = rd (br, stageDelay[s][3]);

                const auto dl = 0.5f * (la + lb + rc - rd_);
                const auto dr = 0.5f * (la - lb + rc + rd_);

                const auto norm = stageNormNow[bandIdx][s];
                l = ((1.0f - w) * l + w * dl) * norm;
                r = ((1.0f - w) * r + w * dr) * norm;
            }
            else if (stageNormNow[bandIdx][s] != 1.0f)
            {
                // **A stage that has just faded out keeps its ramp until it lands.**
                // At w = 0 the stage passes the signal straight through, but the
                // normaliser in use is still ramping down to one from where the
                // last update left it -- up to 2.5 % above, one DENSITY step from
                // the threshold. Dropping it the moment w reached zero was a gain
                // step at every stage threshold on a falling ramp (QA probe,
                // 2026-10-02: the largest second differences of a full-range
                // triangle sat at 0.90 and 0.75 exactly). Applied, the output is
                // continuous through the threshold, and it is exactly one, and
                // free, once the ramp lands.
                l *= stageNormNow[bandIdx][s];
                r *= stageNormNow[bandIdx][s];
            }
        }
    }

    //==========================================================================
    /** The build: a `TapSet` from a config, on the audio thread, from the
        baked tables. No allocation. */
    void rebuild (const ErConfig& c, TapSet& s) const noexcept
    {
        const auto type  = std::clamp (c.type, 0, kNumTapTypes - 1);
        const auto sizeM = std::clamp (c.sizeM, 0.5f, kMaxSizeM);
        const auto* table = kTypeTaps[(size_t) type];
        const auto  seed  = (std::uint32_t) (type * 7919 + c.mode * 104729 + 17);

        struct Slot { float timeMs, gain, pan, sign; int order; float theta; };
        std::array<Slot, kMaxTaps> slot {};
        int count = 0;

        // Core: the table at this size.
        float firstMs = 1.0e9f, lastMs = 0.0f;
        for (int k = 0; k < kNumReferenceTaps; ++k)
        {
            const auto& t = table[k];
            slot[(size_t) count++] = { tapTimeMsAt (t, sizeM), tapGainAt (t, sizeM), t.pan, 1.0f, t.order, kCoreTheta };
            firstMs = std::min (firstMs, tapTimeMsAt (t, sizeM));
            lastMs  = std::max (lastMs,  tapTimeMsAt (t, sizeM));
        }

        // The contour: a least-squares line in dB against time through the
        // core taps, which is "the same 1/r * beta^order envelope evaluated
        // at their own times" for the infill (10 section 3).
        float sx = 0.0f, sy = 0.0f, sxx = 0.0f, sxy = 0.0f;
        for (int k = 0; k < kNumReferenceTaps; ++k)
        {
            const auto x = slot[(size_t) k].timeMs;
            const auto y = 20.0f * std::log10 (std::max (slot[(size_t) k].gain, 1.0e-6f));
            sx += x; sy += y; sxx += x * x; sxy += x * y;
        }
        const auto n = (float) kNumReferenceTaps;
        const auto slope = (n * sxy - sx * sy) / std::max (n * sxx - sx * sx, 1.0e-6f);
        const auto icept = (sy - slope * sx) / n;
        const auto contourDb = [slope, icept] (float ms) { return icept + slope * ms; };

        // Energy mode's envelope: a rise (t / tau_r)^p to a plateau, then an
        // exponential handover with sigma = SPREAD. CALIBRATE end to end.
        const auto sigma   = std::clamp (c.spreadMs, 5.0f, 200.0f);
        const auto tauR    = 0.20f * sigma;
        const auto tauP    = 0.60f * sigma;
        const auto p       = std::clamp (c.shape, 0.0f, 3.0f);
        const auto windowE = energyWindowMs (c.spreadMs);
        const auto envelope = [tauR, tauP, sigma, p] (float ms)
        {
            if (ms < tauR) return std::pow (ms / tauR, p);
            if (ms < tauP) return 1.0f;
            return std::exp (-(ms - tauP) / sigma);
        };

        if (c.mode == 1)
        {
            // Energy: 48 grid pulses over the window, envelope gains, velvet
            // signs. The 21 with theta 0 are spread through the window rather
            // than clustered at its start. The same 0.9 ms separation as the
            // infill, for the same reason.
            count = 0;
            for (int i = 0; i < kMaxTaps; ++i)
            {
                const auto cell = windowE / (float) kMaxTaps;
                auto ms = std::max (0.5f, (float) i * cell + cell * (0.1f + 0.8f * unit (seed, (std::uint32_t) i)));

                for (int k = 0; k < count; ++k)
                    if (std::abs (ms - slot[(size_t) k].timeMs) < kMinSeparationMs)
                        ms = slot[(size_t) k].timeMs + kMinSeparationMs;
                const auto sign = unit (seed + 1, (std::uint32_t) i) < 0.5f ? -1.0f : 1.0f;
                const auto pan  = signedUnit (seed + 2, (std::uint32_t) i) * 0.8f;
                slot[(size_t) count++] = { ms, envelope (ms), pan, sign, 2, kCoreTheta };
            }
        }
        else
        {
            // Taps: the room's times and its physical gains.
            //
            // Infill: one jittered pulse per equal cell of the window, gain
            // from the contour, nudged clear of **every** tap already placed
            // -- core and infill alike -- by the 0.9 ms separation rule. The
            // rule is what keeps the diffuser energy-neutral: its reads are
            // all inside 0.9 ms, so a copy of one pulse can never land on
            // another (see `diffuse`). Where the window is too small to hold
            // 48 pulses 0.9 ms apart, which is a SIZE under about 9 m for
            // Room, the last ones stack at the end and that rule lapses.
            const auto cell = (lastMs - firstMs) / (float) kNumInfill;
            for (int i = 0; i < kNumInfill; ++i)
            {
                auto ms = firstMs + (float) i * cell + cell * (0.1f + 0.8f * unit (seed, (std::uint32_t) i));

                for (int pass = 0; pass < 4; ++pass)
                    for (int k = 0; k < count; ++k)
                        if (std::abs (ms - slot[(size_t) k].timeMs) < kMinSeparationMs)
                            ms = slot[(size_t) k].timeMs + kMinSeparationMs;

                const auto gain = std::pow (10.0f, contourDb (ms) * 0.05f);
                const auto pan  = signedUnit (seed + 2, (std::uint32_t) i) * 0.8f;
                // Velvet noise is signed by definition: a random +-1 per pulse is
                // what keeps a dense cluster from building up at low frequencies
                // and combing the way an all-positive train does.
                const auto sign = unit (seed + 1, (std::uint32_t) i) < 0.5f ? -1.0f : 1.0f;
                slot[(size_t) count++] = { ms, gain, pan, sign, 2, 0.0f };
            }
        }

        // Thresholds: the 21 core (theta 0) and the 27 infill, the infill's
        // thresholds dealt over the cells in a shuffled order so DENSITY fills
        // the whole window rather than sweeping across it.
        {
            std::array<int, kNumInfill> order {};
            for (int i = 0; i < kNumInfill; ++i) order[(size_t) i] = i;
            for (int i = kNumInfill - 1; i > 0; --i)
                std::swap (order[(size_t) i], order[(size_t) (hash32 (seed + 3, (std::uint32_t) i) % (std::uint32_t) (i + 1))]);

            if (c.mode == 1)
            {
                // Every (48/21)th pulse is core; the rest take infill thresholds.
                int infill = 0;
                for (int i = 0; i < kMaxTaps; ++i)
                {
                    const bool core = (i * kNumReferenceTaps) / kMaxTaps != ((i + 1) * kNumReferenceTaps) / kMaxTaps;
                    slot[(size_t) i].theta = core ? kCoreTheta : infillThreshold (order[(size_t) std::min (infill++, kNumInfill - 1)]);
                }
            }
            else
            {
                for (int i = 0; i < kNumInfill; ++i)
                    slot[(size_t) (kNumReferenceTaps + i)].theta = infillThreshold (order[(size_t) i]);
            }
        }

        // Sort by time: the first two of the sorted set are the ones kept
        // centred and unsplit.
        std::sort (slot.begin(), slot.begin() + count, [] (const Slot& a, const Slot& b) { return a.timeMs < b.timeMs; });

        // Band cutoffs at this size, from the mean time of each band's **core**
        // taps -- the same 21 the offline audit sees, so the ceilings the
        // table was cut to and the poles it plays through agree exactly; the
        // infill inherits the bands -- and what each tap keeps through its
        // band's pole.
        std::array<float, kMaxTaps> bandEnergy {};
        {
            float sumT[kNumBands] {};
            int   n[kNumBands] {};
            for (int i = 0; i < count; ++i)
            {
                if (slot[(size_t) i].theta > 0.0f)
                    continue;

                const auto b = bandFor (slot[(size_t) i].timeMs, slot[(size_t) i].order);
                sumT[b] += slot[(size_t) i].timeMs;
                ++n[b];
            }

            for (int b = 0; b < kNumBands; ++b)
                s.cutoffHz[(size_t) b] = cutoffHzFor (b, sizeM, n[b] > 0 ? sumT[b] / (float) n[b] : 20.0f);

            for (int i = 0; i < count; ++i)
                bandEnergy[(size_t) i] = onePoleEnergyGain (s.cutoffHz[(size_t) bandFor (slot[(size_t) i].timeMs, slot[(size_t) i].order)], sampleRate);
        }

        // Energy renormalisation: the theta <= 0 set carries the room's core
        // energy **after the band filters**, whichever law drew its contour.
        // In Taps mode that is the table's own filtered energy and the scale
        // is exactly one.
        {
            float coreTable = 0.0f;
            for (int k = 0; k < kNumReferenceTaps; ++k)
            {
                const auto g = tapGainAt (table[k], sizeM);
                const auto b = bandFor (tapTimeMsAt (table[k], sizeM), table[k].order);
                coreTable += g * g * onePoleEnergyGain (s.cutoffHz[(size_t) b], sampleRate);
            }

            if (c.mode != 0)
            {
                float coreNow = 0.0f;
                for (int i = 0; i < count; ++i)
                    if (slot[(size_t) i].theta <= 0.0f)
                        coreNow += slot[(size_t) i].gain * slot[(size_t) i].gain * bandEnergy[(size_t) i];

                const auto g = coreNow > 1.0e-12f ? std::sqrt (coreTable / coreNow) : 0.0f;
                for (int i = 0; i < count; ++i)
                    slot[(size_t) i].gain *= g;
            }

            s.coreEnergy = coreTable;
        }

        // VARIATION.
        const auto v = std::clamp (c.variation, 0, 6);
        s.comb      = v == 6;
        s.combDelay = std::min (combLength - 1, (int) std::lround (kCombDelayMs * 0.001f * sampleRate));
        const auto spread = v == 6 ? 0.0f : kSpread[v];
        const auto split  = v == 6 ? 0.0f : kSplit[v];

        s.count  = count;
        s.spanMs = 0.0f;

        const auto maxDelay = lineLength - 2;

        for (int i = 0; i < count; ++i)
        {
            const auto& in = slot[(size_t) i];
            auto& t = s.taps[(size_t) i];

            const auto delay = (int) std::lround (in.timeMs * 0.001f * sampleRate);
            t.delay = std::clamp (delay, 0, maxDelay);
            t.base  = in.gain * in.sign;
            t.theta = in.theta;
            t.bandIdx = bandFor (in.timeMs, in.order);
            t.bandEnergy = bandEnergy[(size_t) i];

            const auto first  = i < 2;
            const auto pan    = std::clamp (in.pan * spread, -1.0f, 1.0f);
            const auto phi    = (pan + 1.0f) * 0.78539816f;   // constant power
            const auto gL     = std::cos (phi), gR = std::sin (phi);
            const auto f      = first ? 0.0f : split;
            const auto common = std::sqrt (1.0f - f);
            const auto moved  = std::sqrt (f);

            if (s.comb)
            {
                t.gL = t.gR = 0.70710678f;
                t.sL = t.sR = 0.0f;
                t.delayL = t.delayR = t.delay;
            }
            else
            {
                t.gL = gL * common;
                t.gR = gR * common;
                t.sL = gL * moved;
                t.sR = gR * moved;

                const auto epsMs = 0.6f + (kMaxSplitMs - 0.6f) * unit (seed + 4, (std::uint32_t) i);
                const auto eps   = (int) std::lround (epsMs * 0.001f * sampleRate);
                t.delayL = std::clamp (t.delay - eps, 0, maxDelay);
                t.delayR = std::clamp (t.delay + eps, 0, maxDelay);
            }

            s.spanMs = std::max (s.spanMs, in.timeMs);
        }

        // **A set leaves here weighed, at the density in force now.** The
        // build used to zero every weight and leave the weighing to the next
        // block's `updateDensity()` -- which skips when DENSITY has not
        // moved, so a second prepare(), a first block longer than the TYPE
        // dip, or a TYPE change at a 4096 block left the table built and
        // silent for good, and at small blocks every TYPE change dropped out
        // until the block ended (the review of PR #27, 2026-09-30). Weighing
        // here is what makes that impossible from any caller: a built table
        // is a playing table. It is 48 taps of arithmetic, cheap enough for
        // the dip's midpoint, and the TYPE dip then brings up a set that is
        // already at its level.
        weigh (s);
    }

    //==========================================================================
    double sampleRate = 48000.0;
    int    blockSize  = 512;

    std::vector<float> line;
    int lineLength = 0, writeIdx = 0;

    std::vector<float> combLine;
    int combLength = 0, combIdx = 0;

    std::vector<float> stageLine[kNumBands][kNumStages][2];
    int stageIdx = 0;

    TapSet sets[2];
    int    active = 0;

    ErConfig current, requested;
    float    sizeAtBuild = kReferenceSizeM;

    bool fading = false;
    int  fadePos = 0, fadeLength = 1;

    bool dipping = false;
    int  dipPos = 0;

    bool started = false;   ///< a block has played since prepare() or reset(); until one has, a request lands

    float density = 0.5f, densityTarget = 0.5f, lastWeightedDensity = -1.0f;

    OnePole band[kNumBands][2];
    float   bandCoef[kNumBands] { 1.0f, 1.0f, 1.0f, 1.0f };

    static constexpr int kMaxFir = 1024;   ///< the cascade's FIR at up to ~230 kHz
    float   stageNorm[kNumBands][kNumStages] { { 1.0f, 1.0f, 1.0f }, { 1.0f, 1.0f, 1.0f }, { 1.0f, 1.0f, 1.0f }, { 1.0f, 1.0f, 1.0f } };      ///< the target
    float   stageNormNow[kNumBands][kNumStages] { { 1.0f, 1.0f, 1.0f }, { 1.0f, 1.0f, 1.0f }, { 1.0f, 1.0f, 1.0f }, { 1.0f, 1.0f, 1.0f } };   ///< what `diffuse` applies
    float   stageNormInc[kNumBands][kNumStages] {};
    int     normRampLeft = 0, samplesSinceNormRun = 0;
    mutable float bandPower[kNumBands][3] {};   ///< per band: sum of left, right and cross tap energies; written by the const weigh()
    int     stageDelay[kNumStages][4] {};
    float   lastNormDensity = -1.0f, lastNormHiCut = -1.0f;
    float   lastNormBandCoef[kNumBands] { -1.0f, -1.0f, -1.0f, -1.0f };
    float   lastBlockHiCut = -1.0f, lastBlockDensityTarget = -1.0f;
    long long normRuns = 0;

    OnePole hiCutL, hiCutL2, hiCutR, hiCutR2;   ///< two poles a side: 12 dB/octave
    float   hiCutCoef = 1.0f, hiCutTarget = 1.0f;
    float   hiCutHz = -1.0f;   ///< the last ER HI-CUT sent, so prepare() can design it at its own rate; negative until one is
};

} // namespace bmo::reverb
