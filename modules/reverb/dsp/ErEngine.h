#pragma once

#include "modules/reverb/dsp/ErTable.h"
#include "modules/reverb/dsp/TapTables.h"

#include <cstdint>
#include <vector>

namespace bmo::reverb
{

//==============================================================================
/** The early-reflection engine: M2, "the ER generator, tail silent throughout".

    It plays whatever `erTableFor (type)` hands it and nothing else. The table
    is the other half of M2 and is built in parallel (`ErTable.h` is the
    contract); nothing in this file knows or assumes a number from the
    stand-in that is linked today, and the tests read the table in play rather
    than a literal copied out of it.

    **The signal path, per sample**

        mono input -> one delay line
          -> two tap sets (A, B), each 48 taps per channel read at whole-sample
             delays, weighted by the density bridge, summed into four
             order-banded accumulators per channel
          -> four one-pole low-passes per channel, shared by band
          -> the feed-forward diffuser: three stages of four parallel short
             delays recombined through a 4x4 orthogonal butterfly, each stage
             faded in above DENSITY 0.6 -- identical in both channels, so it
             adds density without moving the L/R correlation the table was
             audited for
          -> ER HI-CUT, one one-pole per channel
          -> the TYPE dip

    **No recursive allpass anywhere** (10 section 1, the owner's hardest
    constraint). The only feedback in the whole path is the one-pole state of
    five low-passes per channel, which cannot ring.

    **The input is the mono sum.** One source in one room: the table's
    per-channel sets are what the two ears hear of it, and Variation 6 puts
    one mono set on the side, +E left and -E right. It is also what makes one
    delay line enough -- a stereo source's image is the dry path's to carry,
    and the dry path is never delayed.

    **What changes how, which is the whole of the click argument**

    - SIZE, ER MODE, VARIATION and ER SPREAD define a tap set. Changing one
      builds the other set from the same delay line and crossfades to it over
      `kCrossfadeMs`, raised cosine, windows summing to one -- `DetuneVoice`'s
      scheme (modules/dim/dsp/DspCore.h). Never a glide: every tap reads a
      whole-sample delay that does not move while it sounds, so a held sine
      comes out at the pitch it went in at. Continuous controls retrigger
      once their accumulated relative change passes 1 %, or once they have
      held still for a crossfade's length, so a slow sweep is a chain of
      crossfades and a small nudge still lands.
    - TYPE dips the wet bus with a raised cosine over `kCrossfadeMs` and swaps
      the table at the minimum. No second engine, no allocation.
    - DENSITY, ER HI-CUT and the band cutoffs are coefficient changes,
      smoothed per sample. Density moves every tap's weight continuously
      (10 section 3), so it needs no crossfade at all.

    `process()` allocates nothing: every buffer is sized in `prepare()` for
    the largest table at the running rate, and a tap set is a fixed-size
    struct built in place.

    Energy and Blend are **CALIBRATE and unheard** (modules/reverb/AGENTS.md,
    "ER Mode's Blend is defined but unheard"). They are made correct and
    deterministic here -- same seed, same pulses, every run and every block
    size -- and deliberately not tuned. */
class ErEngine
{
public:
    /** The ER Mode ordinals, which are `ErMode`'s in DspCore.h. Repeated as
        plain ints because DspCore includes this file and not the other way
        round; DspCore.h asserts that the two agree. */
    static constexpr int kModeTaps   = 0;
    static constexpr int kModeEnergy = 1;
    static constexpr int kModeBlend  = 2;

    /** Everything the engine is steered by, in engine units. DspCore fills it
        from its `Params`; nothing else does. */
    struct Settings
    {
        int   type       = 0;                 ///< the table: `erTableFor (type)`
        int   mode       = kModeTaps;
        float sizeM      = kReferenceSizeM;
        float density    = 0.5f;              ///< 0..1
        float shape      = 1.0f;              ///< the rise exponent p, per type
        float spreadMs   = 80.0f;             ///< sigma, the Energy envelope's length
        float hiCutHz    = 7000.0f;
        int   variation  = 2;                 ///< 0..6; 6 is "mono null": E on the side only
    };

    //== The laws, public so the tests and the measurement tool can name them ==

    /** The feed-forward diffuser's shape. Three stages of four is 10 section
        6's CPU line, and 64 distinct path delays per pulse. */
    static constexpr int kDiffuserStages = 3;
    static constexpr int kDiffuserWidth  = 4;

    /** Above this density the diffuser stages fade in, one after another,
        each over an equal third of what is left of the knob (10 section 3:
        "above D ~ 0.6 the diffuser goes 1 -> 2 -> 3 stages"). CALIBRATE. */
    static constexpr float kDiffuserStartDensity = 0.6f;

    /** The cluster ends on a ramp and not on a tap (10 section 3, "the last
        tap ramps out over >= 5 ms, so the cluster does not end on a
        discontinuity"). Taps in the last `kTaperMs` of the window are faded by
        a half cosine that reaches zero at the window's end. */
    static constexpr float kTaperMs = 5.0f;

    /** 10 section 3's window clamp has a floor as well as a ceiling: the span
        never shrinks below 5 ms, whatever SIZE says. The ceiling is the
        table's own `windowClampMs`. */
    static constexpr float kWindowFloorMs = 5.0f;

    /** ER HI-CUT's top. **At the top of its range the filter is a wire**: the
        one-pole's pole is walked to zero over the last few per cent of the
        range, so "open" means open and a measurement of the rest of the path
        can be made against it. At 16 kHz the pole is 99.92 % of its exact
        value, so the -3 dB point everywhere a user can hear the knob is
        where the knob says. */
    static constexpr float kHiCutOpenHz = 20000.0f;

    /** Size law with the window clamp: the factor every table time is
        multiplied by and every table gain divided by. `S / S_ref`, clamped so
        the table's window stays inside [5 ms, windowClampMs]. The pattern is
        kept whole -- no tap is dropped at the clamp -- because scaling the
        pattern is what keeps a room's identity (10 section 3). */
    static float sizeScale (const ErTable& table, float sizeM) noexcept;

    /** The end-of-cluster ramp at time `tMs` for a window ending at
        `windowEndMs`: 1 before the last `kTaperMs`, a half cosine to 0 across
        it, 0 after. */
    static float endTaper (float tMs, float windowEndMs) noexcept;

    /** The one-pole low-pass `y = (1 - a) x + a y` whose response is exactly
        -3 dB at `hz`. Solved rather than approximated, because ER HI-CUT's
        corner is asserted to +-10 % from 2 kHz up to 16 kHz and the usual
        exp(-2 pi f / fs) is 25 % out by 8 kHz at 48 kHz. Unity at DC, no zero,
        an impulse response that peaks on its first sample and decays
        monotonically -- which is what lets a tap's time be peak-picked
        through it and a tap's gain be read off its area. */
    static float onePoleCoefficient (double hz, double sampleRate) noexcept;

    /** ER HI-CUT's coefficient, which is `onePoleCoefficient` walked to a wire
        at `kHiCutOpenHz`. */
    static float hiCutCoefficient (double hz, double sampleRate) noexcept;

    /** A diffuser stage's four delays in samples at `sampleRate`. */
    static int diffuserDelaySamples (int stage, int line, double sampleRate) noexcept;

    /** The longest time the diffuser adds to a pulse: the sum of each stage's
        longest delay. What "span" means at the top of DENSITY is the window
        plus this. */
    static float diffuserSpreadMs() noexcept;

    /** The weight of diffuser stage `stage` at density `d`, 0..1. */
    static float diffuserStageWeight (int stage, float d) noexcept;

    //==========================================================================

    void prepare (double sampleRate, float rampWidth, float crossfadeMs, float smoothingMs);
    void reset();

    /** Stores the targets. Cheap and allocation-free; it may be called every
        block. What it changes is picked up sample-accurately inside
        `process`, so the same values delivered in different block sizes make
        the same output. */
    void setSettings (const Settings& s) noexcept;

    /** `in` is the mono sum; `outL` and `outR` receive the ER bus, before the
        ER fader. Any of the three may alias. */
    void process (const float* in, float* outL, float* outR, int numSamples) noexcept;

    //== Read-outs for the tests =================================================

    /** The gain the engine is currently playing on tap `tap` of channel
        `channel` of the set in charge (the incoming one during a crossfade),
        after the density weighting and the energy renormalisation. Read-only,
        and here so the density sweep can assert "no tap appears at a
        non-zero level" on the weights themselves rather than on an IR where a
        filter has smeared them. */
    float currentTapGain (int channel, int tap) const noexcept;
    int   currentTapCount (int channel) const noexcept;

    /** Whether a set crossfade or a TYPE dip is running. */
    bool isCrossfading() const noexcept { return fading; }
    bool isDipping() const noexcept { return dipping; }

    /** The samples the delay line holds: the allocation, for the notes. */
    size_t memoryBytes() const noexcept;

private:
    struct TapSet
    {
        int   delay [2][kErMaxTaps] {};
        float base  [2][kErMaxTaps] {};   ///< gain before density weighting and renormalisation
        float theta [2][kErMaxTaps] {};
        int   band  [2][kErMaxTaps] {};
        float gain  [2][kErMaxTaps] {};   ///< what is played
        int   num   [2] {};
        float energy[2] {};               ///< the renormalisation target, per channel
        float eta   [kErBands] {};        ///< each band filter's impulse energy

        /** Pairs of taps close enough that their filtered pulses overlap,
            with the overlap -- the inner product of the two filtered pulses.
            Built once per set; see `applyDensity`. */
        struct Pair
        {
            std::uint8_t i, j;
            float overlap;
        };

        static constexpr int kMaxPairs = kErMaxTaps * (kErMaxTaps - 1) / 2;
        Pair pairs[2][kMaxPairs] {};
        int  numPairs[2] {};
        float bandA [kErBands] {};        ///< each band filter's target coefficient

        bool  side = false;               ///< Variation 6: channel 0 is E, played as L = +E, R = -E

        Settings built;                   ///< what it was built from
    };

    void build (TapSet& set, const Settings& s) noexcept;
    void applyDensity (TapSet& set, float d) noexcept;
    float weightedEnergy (TapSet& set, int ch, float d) noexcept;
    void buildPairs (TapSet& set, int ch) noexcept;
    void prime() noexcept;
    void updateTargets() noexcept;
    bool definesDifferentSet (const Settings& a, const Settings& b) const noexcept;
    void runSet (const TapSet& set, int readBase, float* accL, float* accR) const noexcept;

    double sampleRate = 48000.0;
    float  rampWidth = 0.08f;

    std::vector<float> line;
    int mask = 0;
    int writePos = 0;

    TapSet sets[2];
    int  active = 0;

    // The crossfade between sets.
    bool  fading = false;
    float fadePhase = 0.0f, fadeInc = 0.0f;
    int   crossfadeSamples = 1;

    // The TYPE dip.
    bool  dipping = false, swapped = false;
    float dipPhase = 0.0f;

    // Retrigger bookkeeping for the continuous set-defining controls.
    float accumulated = 0.0f;
    int   sinceChange = 0;

    Settings target;
    bool targetsDirty = true;
    bool primed = false;

    // Smoothed coefficients and their targets.
    float smoothCoeff = 1.0f;
    float density = 0.5f, densityApplied = -1.0f;
    float hiCutA = 0.0f, hiCutTarget = 0.0f;
    float bandA[kErBands] {};
    float stageW[kDiffuserStages] {}, stageNorm[kDiffuserStages] {};

    // Filter state, per channel.
    float bandZ[2][kErBands] {};
    float hiCutZ[2] {};

    // The diffuser's lines: [channel][stage][line], each a power-of-two ring.
    struct Ring
    {
        std::vector<float> data;
        int mask = 0, pos = 0, delay = 1;
    };

    Ring diffuser[2][kDiffuserStages][kDiffuserWidth];
};

} // namespace bmo::reverb
