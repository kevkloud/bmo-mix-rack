#pragma once

#include "modules/sat/dsp/DriveTables.h"
#include "modules/sat/dsp/Shaper.h"
#include "modules/tune/dsp/SincTable.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <cstdint>
#include <vector>

/** Clean's sinc tap count: 24, DECIDED by Frosty 2026-10-01 after a blind A/B
    on AURORA (`testing-notes/dwell-sinc-ab-2026-09-23.md`).

    32 came in with `modules/tune/dsp/SincTable.h`, where Tune measured it for
    *its* job: reading at a rate other than 1, with the kernel doubling as the
    anti-aliasing filter. Dwell's clean read is at rate 1, so that reasoning
    never applied here, and the read was the single largest cost in the module.

    In the blind set no width could be told apart on repeats or a long tail,
    and the two files that stood out were both 16 taps, one liked and one
    disliked, with nothing in their measured envelopes to separate them from 32.
    Frosty's criterion was that the narrowest width shipped should still pass a
    centred mono signal unchanged. 24 is the narrowest that does: 0.00 dB at
    18 kHz where 16 droops 0.13 dB, and the same alias floor as every width.
    It is 13 % cheaper than 32 on Clean bare and 8 % at the heaviest.

    The macro stays so the width can be re-measured without editing the engine;
    do not narrow it without another listening round. */
#ifndef BMO_DWELL_SINC_TAPS
 #define BMO_DWELL_SINC_TAPS 24
#endif

namespace bmo::dwell
{

inline constexpr double kPiD = 3.14159265358979323846;

/** The three characters, by the index the schema's CHARACTER choice carries.

    They are named here rather than in the module so that the engine can be
    lifted out whole (10 §11.1) -- and note that naming them is **not** the
    engine knowing which instance it is. A character is a parameter; an
    instance is not. */
enum Character
{
    kClean = 0,
    kTape = 1,
    kBucketBrigade = 2
};

/** The three stereo modes, by the index the schema's STEREO choice carries
    (10 §8). Like a character, a mode is **a parameter** -- it governs both
    engines from one control (10 §11.3), and an engine handed it still cannot
    ask which instance it is. */
enum StereoMode
{
    kStereoIndependent = 0,   ///< identity matrix, both lines at T
    kPingPong = 1,            ///< input summed to mono, the matrix is the swap
    kDualOffset = 2           ///< identity matrix, D_L = T and D_R = (2/3) T
};

//==============================================================================
/** One-pole parameter smoother, the shape used in modules/dim, modules/sat and
    modules/opto -- see those for why it snaps once inside epsilon. */
class Smoother
{
public:
    void prepare (double sampleRate, double timeMs) noexcept
    {
        const auto tau = std::max (timeMs, 0.01) * 0.001;
        coeff = (float) (1.0 - std::exp (-1.0 / (std::max (sampleRate, 1.0) * tau)));
    }

    void snap (float v) noexcept      { current = target = v; }
    void setTarget (float t) noexcept { target = t; }

    float tick() noexcept
    {
        current += coeff * (target - current);

        if (std::abs (target - current) < 1.0e-9f)
            current = target;

        return current;
    }

    /** `tick`, landing exactly on the target once a step no longer moves the
        value.

        A float one-pole stalls short of its target: once `coeff . (target -
        current)` is under half an ulp the sum rounds back to where it was, and
        the epsilon above is far smaller than an ulp near 1.0. At 20 ms that
        stall sits about 3e-5 below 1.0 at 48 kHz and 1e-4 at 192 kHz -- so a
        gain whose whole point is to arrive at **exactly** 1.0, like the dry
        below MIX's hinge, would never get there. Detecting the stall itself
        rather than picking a wider epsilon lands it at every rate, with a
        final step no larger than the stall: about 1e-4 of the gain at worst. */
    float tickLanding() noexcept
    {
        const auto before = current;
        tick();

        if (current == before)
            current = target;

        return current;
    }

    float value() const noexcept { return current; }

private:
    float coeff = 1.0f, current = 0.0f, target = 0.0f;
};

//==============================================================================
/** A TPT one-pole, prewarped, carrying its own frequency response in closed
    form.

    The response lives beside the filter rather than in the sweep that uses it
    because docs/delay/10 §3 requires `P_c` to be taken from **the coefficients
    the loop actually runs**, so that it cannot drift from whatever a CALIBRATE
    pass lands on. One `g = tan(pi fc / fs)` feeds both the difference equation
    and the magnitude, and there is no second copy of the corner to get wrong.

    `g` depends only on `fc / fs`, which is what makes every coefficient here
    sample-rate invariant across 44.1-192 kHz (10 §4). */
class TptOnePole
{
public:
    void setCutoff (double cutoffHz, double sampleRate) noexcept
    {
        const auto fc = std::clamp (cutoffHz, 0.1, 0.49 * sampleRate);
        gCoeff = std::tan (kPiD * fc / std::max (sampleRate, 1.0));
        gNorm  = gCoeff / (1.0 + gCoeff);
    }

    void reset() noexcept { state = 0.0; }

    double lowPass (double x) noexcept
    {
        const auto v = (x - state) * gNorm;
        const auto y = v + state;
        state = y + v;

        // A one-pole decaying toward zero on silence is the textbook denormal
        // source. FTZ is set for the block as well (ScopedNoDenormals), but
        // this is what makes "silence in, exact zeros out" true rather than
        // merely quiet.
        if (std::abs (state) < 1.0e-25)
            state = 0.0;

        return y;
    }

    double highPass (double x) noexcept { return x - lowPass (x); }

    /** A one-pole low shelf, `gain . LP + HP`: `gain` at DC, unity well above
        the corner, and **the corner is the pole** -- see `kHeadBumpHz` for why
        that convention was taken over the +1 dB midpoint (10 §4). */
    double lowShelf (double x, double gain) noexcept
    {
        const auto lp = lowPass (x);
        return gain * lp + (x - lp);
    }

    double coeff() const noexcept { return gCoeff; }

    /** |H(e^jw)| of the low-pass built from `g`. */
    static double lowPassMagnitude (double g, double omega) noexcept
    {
        const auto z = std::polar (1.0, -omega);
        return std::abs ((g * (1.0 + z)) / ((1.0 + g) + (g - 1.0) * z));
    }

    /** |H(e^jw)| of the high-pass built from `g`, i.e. 1 - the low-pass. */
    static double highPassMagnitude (double g, double omega) noexcept
    {
        const auto z = std::polar (1.0, -omega);
        return std::abs ((1.0 - z) / ((1.0 + g) + (g - 1.0) * z));
    }

    /** |H(e^jw)| of `gain . LP + HP`, which collapses to one ratio:
        ((G g + 1) + (G g - 1) z) / ((1 + g) + (g - 1) z).

        This is the one stage in the loop whose magnitude is allowed above
        unity (10 §4), so it is also the one whose closed form has to be
        right: `P_tape` is this number and almost nothing else. */
    static double lowShelfMagnitude (double g, double gain, double omega) noexcept
    {
        const auto z = std::polar (1.0, -omega);
        const auto gg = gain * g;
        return std::abs (((gg + 1.0) + (gg - 1.0) * z) / ((1.0 + g) + (g - 1.0) * z));
    }

private:
    double gCoeff = 0.0, gNorm = 0.0, state = 0.0;
};

//==============================================================================
/** A TPT state-variable two-pole, low-pass output, carrying its magnitude in
    closed form for the same reason `TptOnePole` carries one.

    Bucket-brigade's anti-alias and reconstruction filters are each one of
    these at `Q = 1/sqrt(2)` exactly -- the no-peaking boundary, so the pair is
    monotonic with |H| <= 1 and equality only at DC (10 §4), which is why BBD's
    filters need no correction of their own. */
class TptSvfLowPass
{
public:
    void set (double cutoffHz, double sampleRate, double q) noexcept
    {
        const auto fc = std::clamp (cutoffHz, 0.1, 0.49 * sampleRate);
        gCoeff = std::tan (kPiD * fc / std::max (sampleRate, 1.0));
        kCoeff = 1.0 / std::max (q, 0.01);

        a1 = 1.0 / (1.0 + gCoeff * (gCoeff + kCoeff));
        a2 = gCoeff * a1;
        a3 = gCoeff * a2;
    }

    void reset() noexcept { ic1 = ic2 = 0.0; }

    double process (double x) noexcept
    {
        const auto v3 = x - ic2;
        const auto v1 = a1 * ic1 + a2 * v3;
        const auto v2 = ic2 + a2 * ic1 + a3 * v3;

        ic1 = 2.0 * v1 - ic1;
        ic2 = 2.0 * v2 - ic2;

        if (std::abs (ic1) < 1.0e-25) ic1 = 0.0;
        if (std::abs (ic2) < 1.0e-25) ic2 = 0.0;

        return v2;
    }

    double coeff() const noexcept   { return gCoeff; }
    double damping() const noexcept { return kCoeff; }

    /** g^2 (1 + z)^2 / ((1 + kg + g^2) + (2g^2 - 2) z + (1 - kg + g^2) z^2),
        the bilinear transform of 1/(s^2 + k s + 1) at the prewarped `g`. */
    static double magnitude (double g, double k, double omega) noexcept
    {
        const auto z = std::polar (1.0, -omega);
        const auto gg = g * g;
        const auto num = gg * (1.0 + z) * (1.0 + z);
        const auto den = (1.0 + k * g + gg) + (2.0 * gg - 2.0) * z + (1.0 - k * g + gg) * z * z;
        return std::abs (num / den);
    }

private:
    double gCoeff = 0.0, kCoeff = 1.41421356237, a1 = 1.0, a2 = 0.0, a3 = 0.0;
    double ic1 = 0.0, ic2 = 0.0;
};

//==============================================================================
/** A log-domain one-pole level follower, in decibels.

    It is in dB rather than in linear amplitude because that is the detector
    10 §4's overshoot model assumes, and the bench that was asked to confirm or
    refute that model has to run against the construction it describes --
    see `DwellDspTests::testTheCompanderIsUnityThroughATransient`, which builds
    a re-detecting pair out of two of these and measures what it does. */
class LevelDetectorDb
{
public:
    static constexpr double kFloorDb = -120.0;

    void prepare (double sampleRate, double attackMs, double releaseMs) noexcept
    {
        attack  = coefficientFor (sampleRate, attackMs);
        release = coefficientFor (sampleRate, releaseMs);
    }

    void reset() noexcept { level = kFloorDb; }

    double tick (double instantDb) noexcept
    {
        const auto k = instantDb > level ? attack : release;
        level += k * (instantDb - level);
        return level;
    }

    /** The level of one sample in dB, floored so that silence is a number and
        not minus infinity. */
    static double levelDb (double x) noexcept
    {
        return 20.0 * std::log10 (std::max (std::abs (x), 1.0e-6));
    }

private:
    static double coefficientFor (double sampleRate, double ms) noexcept
    {
        const auto tau = std::max (ms, 0.01) * 0.001;
        return 1.0 - std::exp (-1.0 / (std::max (sampleRate, 1.0) * tau));
    }

    double attack = 1.0, release = 1.0, level = kFloorDb;
};

/** The 2:1 compressor half of 10 §4's bucket-brigade compander: half the
    level in dB about a 0 dBFS reference, from a detector reading the signal on
    its way **into** the line.

    There is no second law for the expander. The expander is `1 / gain` read
    back out of the control ring, tap by tap, at the same positions the audio
    is read from, which is the whole point of the construction (10 §4, and a
    stability requirement rather than a refinement; `readExpanded` says why it
    is per tap). */
inline double companderGainFor (double levelDb) noexcept
{
    // The floor bounds the boost a near-silent line asks for: at -60 dBFS the
    // compressor is already at +30 dB and nothing quieter needs more. Both
    // ends are CALIBRATE, with the 5/50 ms ballistics.
    const auto clamped = std::clamp (levelDb, -60.0, 6.0);
    return std::pow (10.0, -0.5 * clamped / 20.0);
}

//==============================================================================
/** The three in-loop FX types, by the index the schema's FX TYPE choice
    carries (10 §11a). Like a character, a type is **a parameter**: naming them
    here is not the engine knowing which instance it is.

    **Three, and only three.** Octave up, Octave down and Reverse were cut on
    2026-09-21 and Sweep on 2026-09-22 -- Sweep *because* VOICE was, since it
    was specified as VOICE's resonant centre being moved per repeat and there
    is no resonant filter left for it to move. Re-adding a sweep means
    re-opening the VOICE decision first; it does not mean giving this stage a
    band-pass of its own. */
enum FxType
{
    kDiffuse = 0,      ///< 6-stage allpass chain, delays 7-37 ms scaled by AMOUNT
    kPanTremolo = 1,   ///< one LFO stepped at the delay period; chops on the mono bus
    kCrush = 2         ///< quantise to 16-3 bits, holding the energy of every 1-32 samples
};

//==============================================================================
/** **10 §11a's in-loop FX stage: one per engine, no shared state.**

    It lives inside `DelayEngine` because it is *in-loop* -- it sits in the
    character chain between the mode filters and the shaper, so it recirculates
    and compounds per repeat, which is the point of it and the risk in it.
    Putting it here is what gives each engine its own stage and its own buffers
    **for free**: the main delay's allpass memory, LFO phase, hold counter and
    quantiser are simply different objects from the lane's, so §11a's "the two
    stages share no state" is a consequence of where the class is declared
    rather than a rule anyone has to keep. `fx_link` ties the two stages'
    parameter *values* and lives out in `DspCore`, because choosing which three
    numbers to hand the lane engine is a decision about the two instances --
    and an engine that took an "am I the lane?" flag would be the wrong seam.

    **Every candidate is non-expanding, `|F| <= 1` at every setting** (§11a), so
    §3's `|g| < 1` bound is untouched and `P_c` does not have to be re-swept
    when FX moves. Diffuse is an allpass, so it is exactly unity at every omega;
    Pan/Tremolo is peak-normalised in closed form (see `panGain`); Crush's
    quantiser truncates toward zero, so it can never make a sample larger, and
    its hold carries exactly the energy of the block it holds (see `crush` --
    until 2026-10-01 it rounded and held a single sample, and both expanded;
    for one day after that it held the block's mean, which lost the top end).

    **AMOUNT zero is a wire on Diffuse and Pan/Tremolo** -- the allpass lengths
    round to nothing and every stage is skipped, and the pan normalisation
    collapses to a gain of exactly 1.0. **On Crush it is not**: at AMOUNT 0 the
    spec's law gives 16 bits and a hold divisor of 1, which is a quantiser at
    about -96 dBFS rather than a bypass. That is inaudible but it is not
    bit-exact, and it is recorded here rather than rounded away, because §11a
    says "zero always inaudible" and means the sound rather than the bits.

    **Nothing here reads ahead**, so reported latency stays 0 (§0). */
class FxStage
{
public:
    static constexpr int kMaxChannels = 2;

    /** §11a: "6-stage allpass, delays 7-37 ms" -- the shape of
        `modules/dim`'s `AllPassChain` (00 §1) with real delay lines in place
        of its unit-sample stages, which is what a *smear* needs and a
        phase-scrambler does not.

        The six are mutually near-prime in milliseconds so that their echoes do
        not line up into a pitched comb over the six passes, let alone over the
        `k` laps the loop then puts them through. The range's ends are 10 §11a's
        own; the four between them are CALIBRATE, being a spacing rather than a
        measurement. */
    static constexpr int kStages = 6;
    static constexpr double kStageDelaysMs[kStages] { 7.0, 11.0, 17.0, 23.0, 31.0, 37.0 };

    /** The allpass coefficient at AMOUNT 100 (CALIBRATE). §11a fixes the
        delays and leaves the diffusion depth open; 0.7 is the usual Schroeder
        value and is well inside the `|a| < 1` the structure needs to stay
        bounded. AMOUNT scales it linearly, so zero is a wire in the
        coefficient as well as in the lengths. */
    static constexpr double kDiffuseCoefficient = 0.7;

    /** §11a's crush law: `b = 16 - AMOUNT . 13` bits, and a hold of
        `ceil(1 + AMOUNT . 31)` samples -- §11a wrote "sample-and-hold"; what is
        held is each block's energy (`crush`). */
    static constexpr double kCrushBitsAtZero = 16.0;
    static constexpr double kCrushBitsSpan = 13.0;
    static constexpr double kCrushHoldSpan = 31.0;

    /** How far the stepped LFO turns per repeat (CALIBRATE).

        §11a asks for "one LFO stepped at the delay period, so each repeat gets
        its own position or level rather than a wobble inside one" and does not
        say how fast it turns. A quarter turn on a cosine gives the four-repeat
        cycle hard-one-side, centre, hard-the-other, centre -- every repeat a
        different place, both extremes reached, and no repeat landing on the
        same position as the one before it. A half turn would alternate sides
        and never pass through centre; an irrational fraction would never
        repeat, which is harder to perform against. */
    static constexpr double kPanTurnsPerRepeat = 0.25;

    void prepare (double newSampleRate, int numChannels)
    {
        sampleRate = newSampleRate > 0.0 ? newSampleRate : 48000.0;

        // Sized for **every** channel the engine can run rather than for the
        // count handed over, so that nothing here is ever the first thing to
        // ask for memory later on.
        (void) numChannels;

        for (int s = 0; s < kStages; ++s)
        {
            const auto longest = (int) std::ceil (kStageDelaysMs[s] * 0.001 * sampleRate) + 2;

            int size = 1;
            while (size < longest)
                size <<= 1;

            stageMask[(size_t) s] = size - 1;

            for (auto& ch : lines)
                ch[(size_t) s].assign ((size_t) size, 0.0f);
        }

        setAmount (amountAtBlock);
        reset();
    }

    /** 10 §11.4 lists "the FX stage's buffers" among what a HOLD-off clear
        zeroes, and `DelayEngine::reset()` is that list -- which is why this is
        called from there rather than copied into `DspCore`. */
    void reset() noexcept
    {
        for (auto& ch : lines)
            for (auto& line : ch)
                std::fill (line.begin(), line.end(), 0.0f);

        writeIdx.fill (0);
        held.fill (0.0);
        holdCounter.fill (0);
        heldSum.fill (0.0);
        heldCount.fill (0);

        lfoPhase = 0.0;
        lfoValue = 1.0;
        stepCounter = 0;
    }

    /** Crush coming back into the loop -- FX on again, or the type moved to
        Crush -- starts its hold from nothing. The stage is skipped while it is
        out, so without this its running sum and its hold were picked up again
        from before it went out (sixth round, 2026-10-02). The other types'
        state is left alone: an allpass picked up where it was left is a
        smear, not a level. */
    void engageCrush() noexcept
    {
        held.fill (0.0);
        holdCounter.fill (0);
        heldSum.fill (0.0);
        heldCount.fill (0);
    }

    /** The allpass lengths, taken from AMOUNT **once per block**.

        A Schroeder allpass whose line length moves per sample is a line being
        re-tapped per sample, and at 192 kHz a 20 ms smoother would walk it two
        samples per sample: a sweep of AMOUNT would tear rather than glide. So
        the lengths step at the block boundary -- where the host moves the
        control anyway -- and the *coefficient* is what is smoothed per sample,
        which is the part that carries the audible change in diffusion. A fast
        AMOUNT automation on Diffuse therefore steps its delays at block rate;
        recorded rather than hidden, and neither 10 §11a nor `11` §4 asks for
        more. */
    void setAmount (double amount) noexcept
    {
        amountAtBlock = std::clamp (amount, 0.0, 1.0);

        for (int s = 0; s < kStages; ++s)
        {
            const auto samples = kStageDelaysMs[s] * 0.001 * sampleRate * amountAtBlock;
            stageLength[(size_t) s] = (int) std::lround (samples);
        }
    }

    /** One sample's worth of the shared state: the stepped LFO.

        It is stepped **at the delay period**, so the position changes once a
        repeat rather than wobbling inside one, and it is advanced once per
        sample for the whole engine rather than once per channel -- a pan needs
        one position that the two channels read opposite ends of. That is per
        *engine* shared state, not per module: the lane has its own. */
    void advance (double delaySamples) noexcept
    {
        const auto period = std::max (1, (int) std::lround (delaySamples));

        if (++stepCounter >= period)
        {
            stepCounter = 0;
            lfoPhase += 2.0 * kPiD * kPanTurnsPerRepeat;

            if (lfoPhase >= 2.0 * kPiD)
                lfoPhase -= 2.0 * kPiD;

            lfoValue = std::cos (lfoPhase);
        }
    }

    double process (int ch, int type, double x, double amount, int numChannels) noexcept
    {
        const auto c = (size_t) std::clamp (ch, 0, kMaxChannels - 1);
        const auto a = std::clamp (amount, 0.0, 1.0);

        if (type == kPanTremolo)
            return panGain (ch, a, numChannels) * x;

        if (type == kCrush)
            return crush (c, x, a);

        return diffuse (c, x, a);
    }

    /** Everything the stage holds, as one number, so that `11` §4l's "the
        stage is skipped, not run at a zero coefficient" can be asserted
        rather than reviewed: with FX off this must stay exactly at its reset
        value however hard the loop is driven. A coefficient of zero would
        still fill the allpass lines and still turn the LFO, and this is what
        tells the two apart. */
    double stateSignature() const noexcept
    {
        auto sum = lfoPhase + lfoValue + (double) stepCounter;

        for (int ch = 0; ch < kMaxChannels; ++ch)
        {
            sum += held[(size_t) ch] + (double) holdCounter[(size_t) ch]
                 + heldSum[(size_t) ch] + (double) heldCount[(size_t) ch]
                 + (double) writeIdx[(size_t) ch];

            for (const auto& line : lines[(size_t) ch])
                for (const auto v : line)
                    sum += std::abs ((double) v);
        }

        return sum;
    }

private:
    /** 10 §11a's Diffuse. Six allpasses in series: unit magnitude at every
        omega, so `|F| = 1` exactly and the smear accumulates over `k` passes
        into a pseudo-reverb without ever adding energy.

        A stage whose length rounds to nothing is **skipped**, which is what
        makes AMOUNT 0 a wire rather than a six-sample delay. */
    double diffuse (size_t ch, double x, double amount) noexcept
    {
        const auto a = kDiffuseCoefficient * amount;
        const auto w = writeIdx[ch];

        for (int s = 0; s < kStages; ++s)
        {
            const auto length = stageLength[(size_t) s];

            if (length < 1)
                continue;

            auto& line = lines[ch][(size_t) s];
            const auto mask = stageMask[(size_t) s];

            const auto d = (double) line[(size_t) ((w - length) & mask)];
            const auto y = -a * x + d;

            line[(size_t) (w & mask)] = (float) (x + a * y);
            x = y;
        }

        writeIdx[ch] = w + 1;

        return x;
    }

    /** 10 §11a's Pan / Tremolo, and the one candidate whose bound had to be
        argued rather than inherited.

        An equal-power pan cannot be both unity at centre and `<= 1` at the
        extremes: normalised to unity at centre it reaches `sqrt(2)` at the
        sides, and normalised to unity at the sides it sits at -3 dB in the
        middle -- so a depth of zero would not be inaudible, which §11a
        requires of every AMOUNT. The way out is §11a's own instruction to
        **peak-normalise in closed form**: blend from unity to the equal-power
        pair by AMOUNT and divide by that blend's own maximum,

            g = ((1 - depth) + depth . p) / ((1 - depth) + depth . p_max)

        which is exactly 1.0 at depth 0, is `p / p_max <= 1` at depth 1, and is
        monotone between. What it costs is that a deep setting attenuates the
        loop slightly on average -- the same direction every user stage in §4
        moves, so the tail can only shorten and §3's reference still bounds it.

        **On the mono bus it chops rather than pans** (§8): with one line there
        is no second output to move into, so the same construction runs on a
        level `1 + u` instead of on a pair, and at full depth it is a tremolo
        gated once per repeat. */
    double panGain (int ch, double depth, int numChannels) const noexcept
    {
        if (numChannels >= 2)
        {
            const auto theta = 0.25 * kPiD * (1.0 + lfoValue);
            const auto pan = kRootTwo * (ch == 0 ? std::cos (theta) : std::sin (theta));

            return ((1.0 - depth) + depth * pan) / ((1.0 - depth) + depth * kRootTwo);
        }

        return ((1.0 - depth) + depth * (1.0 + lfoValue)) / ((1.0 - depth) + depth * 2.0);
    }

    /** 10 §11a's Crush, and **deliberate aliasing**.

        The hold's images are made *inside* the loop and meet §4's 18 kHz cap on
        the *next* lap, so the cap tames them one repeat late rather than
        preventing them -- musical and bounded, and always ahead of the shaper.
        That is why Crush is **exempt from §4's -60 dBFS alias floor**, which is
        measured FX-off, and why its own acceptance is only that the
        non-harmonic floor **stops growing by repeat 10** (`11` §4l).

        **Every lap under unity FEEDBACK loses energy through this stage, by
        construction** (Frosty's rule, 2026-10-01: "under 100% feedback should
        lose energy, not be indefinite"). Two choices make it so, and each was
        forced by a measured limit cycle on AURORA:

        - **The hold matches the block's energy** (DECIDED, Frosty,
          2026-10-02: "energy match it"). Each held value carries the energy
          of the samples since the last hold -- the block, `M` samples --
          spread over the `N` it is held for or over the block, whichever is
          longer: `sqrt(sum x^2 / max(N, M))`, with the sign of the newest
          sample, the one a frozen hold would have taken. In steady state
          `M = N` and that is the block's RMS, so the hold carries exactly the
          block's energy and the stage keeps its top end. When they differ --
          the first hold after a clear (`M = 1`), or AMOUNT moving `N` -- the
          held block carries at most its input's energy and is never above
          its RMS, so never above its peak (sixth and seventh rounds,
          2026-10-02: spread over `N` alone, a hold that AMOUNT had just
          shortened rose to 1.22 times its input's peak).
          Two holds were built before it and measured on AURORA:
          - a **frozen sample** (until 2026-10-01) is not energy-bounded:
            phase-locked to a tone it turns a sine into a square whose
            fundamental is up to 4/pi of the sine's, and from about FEEDBACK
            90 % that grew loops to a steady peak above their input -- 0.7277
            from a 0.5 burst at 44.1 kHz on bucket-brigade, AMOUNT 35, TIME
            50 ms, FEEDBACK 95 %;
          - the **block mean** (2026-10-01) is bounded, but it is a box filter:
            one pass at AMOUNT 60 took 5 and 10 kHz down about 30 dB, and at
            AMOUNT 100 everything from 1 kHz up truncated to silence.
          A tone at exactly the hold rate, sampled at its crest every block,
          comes out of this hold as DC; the loop's 10 Hz blocker after the stage
          keeps it out of the ring (`testCrushCarriesNoDcOutOfTheLoop`).
        - **The quantiser truncates toward zero, so `|q| <= |x|`.** It rounded
          to the nearest step until 2026-10-01, and rounding expands: at 3
          bits a 0.13 becomes 0.25, and above about 60 % FEEDBACK the loop held
          -7.1 dB forty seconds after a burst. A signal under one step is gone
          on its first crushed lap.

        With both, no dead zone is needed (a 0.35-step one was built for the
        frozen hold's one-step cycle on 2026-10-01 and removed the same day),
        and on the full grid (six rates, every character, AMOUNT 35-100,
        FEEDBACK to 96.9 %) nothing is left holding a level. **The hold is
        causal by one block**: what is held from sample `k` is the block that
        ends at `k`, so a lap through Crush is about N - 1 samples later than
        the same lap without it (the first repeat never passes through it). A
        non-finite input counts as 0, in the sum and in the sign. Bounded work:
        a multiply-add a sample and, once a hold, one divide and one square
        root.

        The clamp to +-1 stays: a loud lap can still hand this stage more than
        full scale, and the shaper and clip come after it. */
    double crush (size_t ch, double x, double amount) noexcept
    {
        const auto divisor = (int) std::ceil (1.0 + amount * kCrushHoldSpan);

        const auto clean = std::isfinite (x) ? x : 0.0;

        heldSum[ch] += clean * clean;
        ++heldCount[ch];

        if (holdCounter[ch] <= 0)
        {
            // The block's energy spread over the hold or over the block,
            // whichever is longer (see above): over the hold alone, the first
            // hold after a clear stays inside the energy rule; over the block
            // too, a hold AMOUNT has just shortened stays under its RMS.
            const auto holdLength = std::max (1, divisor);
            const auto spread = std::max (holdLength, heldCount[ch]);
            const auto magnitude = std::sqrt (heldSum[ch] / (double) spread);
            held[ch] = clean > 0.0 ? magnitude : (clean < 0.0 ? -magnitude : 0.0);
            heldSum[ch] = 0.0;
            heldCount[ch] = 0;
            holdCounter[ch] = holdLength;
        }

        --holdCounter[ch];

        const auto bits = kCrushBitsAtZero - amount * kCrushBitsSpan;
        const auto step = std::exp2 (1.0 - bits);
        const auto q = std::trunc (held[ch] / step) * step;

        return std::clamp (q, -1.0, 1.0);
    }

    static constexpr double kRootTwo = 1.41421356237309505;

    double sampleRate = 48000.0;

    std::array<std::array<std::vector<float>, kStages>, kMaxChannels> lines;
    std::array<int, kStages> stageMask {};
    std::array<int, kStages> stageLength {};
    std::array<int, kMaxChannels> writeIdx {};

    std::array<double, kMaxChannels> held {};
    std::array<int, kMaxChannels> holdCounter {};

    /** Crush's running sum of squares and count since the last hold: the
        block whose energy the next held value carries. */
    std::array<double, kMaxChannels> heldSum {};
    std::array<int, kMaxChannels> heldCount {};

    double amountAtBlock = 0.0;
    double lfoPhase = 0.0, lfoValue = 1.0;
    int stepCounter = 0;
};

//==============================================================================
/** **BMO Dwell's delay engine: one type, instantiated twice.**

    docs/delay/10 §11.1 makes this a structural requirement rather than a
    style preference. The module holds two of these -- the main delay and the
    lane -- and **nothing in here knows which one it is**. An engine is handed
    a time, a character and a loop gain; what feeds it, what reads it, how its
    gain was arrived at and whether a ducker touches its output are all the
    caller's business. The point is that Dwell can later be split into a plain
    delay and a throw delay without redoing the expensive part, and that every
    invariant in `11` §4 is asserted against one piece of code exercised twice.

    The feedback **law** is deliberately outside: §3's `g = 1.05 fb^1.6 / P_c`
    and §11.2's bipolar lane law are two different maps onto the same loop
    gain, and an engine that knew both would be an engine that knew which
    instance it was. What the engine owns is `P_c` itself -- it is a property
    of this engine's filters, interpolator and TIME -- which it computes by
    sweep and publishes through `referenceLoopPeak()` for the caller's law to
    divide by.

    **Stage 2b: the three characters.** 2a left the ring, the fractional read,
    the clean crossfade, the reference sweep and the safety clip. 2b adds
    everything that makes the characters differ -- the wired cuts, tape's
    rolloff and head bump, bucket-brigade's clock-derived Butterworths and its
    delayed-gain compander, the pitch glide, the DRIVE shaper and §5's
    modulation with tape's character floor. **Every one of them is a property
    of the parameters**, which is the governing requirement restated as code:
    two engines handed the same values produce the same colour, and neither can
    ask which one it is.

    **Stage 2d adds 10 §8's stereo matrix**, and it is the last thing that
    belongs in here: the mode is one shared control governing both engines, so
    it is a parameter like the character and reaches the engine the same way.
    The ducker and the lane's three gates are **not** here and must not be --
    they are the caller's, because they are what tells the two instances
    apart.

    **Stage 2e adds 10 §11a's in-loop FX stage, and it belongs in here for the
    same reason the character does and the gates do not: it is *in the loop*.**
    Each engine therefore gets its own stage and its own buffers for free, so
    neither can disturb the other's without anything being written to make that
    true. Which three values the stage runs on is the caller's business --
    `fx_link` is a decision about the two instances and lives in `DspCore` --
    and an engine handed them still cannot ask which one it is.

    Latency is 0 and stays 0: no oversampling, no lookahead, and the wet delay
    time is not latency (10 §0, `00` §4).

    `input` and `output` must not alias -- the engine reads its input after it
    has written the sample's output. */
class DelayEngine
{
public:
    /** 24 (`BMO_DWELL_SINC_TAPS`, at the head of this file) unless a build overrides it. */
    using Sinc = bmo::tune::SincTable<BMO_DWELL_SINC_TAPS>;

    static constexpr int kMaxChannels = 2;

    /** The sweep grid of 10 §3: "a log grid of at least 512 points from 10 Hz
        to 0.45 f_s". 1024 is taken rather than the floor because the grid is
        built once at `prepare` and the extra resolution is free at the point
        where it matters -- resolving a peak that sits between two points as
        lower than it is would make the loop hotter than the law intends. */
    static constexpr int kSweepPoints = 1024;

    /** The read is taken before the sample's own write, so the newest valid
        sample is `writeIdx - 1`: the sinc's `kHalf` taps of headroom hold from
        `D >= kHalf + 1` rather than `D >= kHalf`. 10 §1's figure is the same
        bound written against a write-first ordering. */
    static constexpr int kSincFloor = Sinc::kHalf + 1;

    /** Hermite reads `floor(pos) - 1 ... floor(pos) + 2`, so it needs three
        samples of headroom. TIME bottoms out at 1 ms, which is 44 samples at
        the lowest rate the suite supports, so this clamp is a guard rather
        than a working limit. */
    static constexpr double kMinDelaySamples = 3.0;

    //==========================================================================
    // 10 §4 and §5's fixed values. Everything marked CALIBRATE is a nominal
    // that `14`'s listening round is expected to move; nothing here is a
    // figure the spec settled and this file then re-guessed.
    //==========================================================================

    /** Tape's per-pass rolloff (10 §4, CALIBRATE; 01's per-pass loss). */
    static constexpr double kTapeLowPassHz = 4500.0;

    /** Tape's head bump: +2 dB, and **55 Hz read as the pole**.

        10 §4 leaves the convention open and names both readings. The pole is
        taken because it is the one the spec's own expected figure belongs to
        -- 1.054 at 63 Hz, against 1.040 at 64 Hz for the +1 dB midpoint -- so
        choosing it keeps `P_c`'s acceptance band the one already written down
        rather than inventing a second. CALIBRATE: the choice moves the sound,
        not the stability, because §3's sweep takes the figure from the built
        coefficients either way.

        Its nameplate gain is 1.2589, but that asymptote lives below the 10 Hz
        blocker and LOW CUT's 20 Hz floor, so what the loop sees is the +0.45 dB
        that survives them. **This is the only in-loop stage above unity and it
        stays that way on purpose**: a head bump that did not compound per
        repeat would not be a head bump. */
    static constexpr double kHeadBumpHz = 55.0;
    static constexpr double kHeadBumpDb = 2.0;

    /** Bucket-brigade's modelled clock (10 §4; 01's datasheet pair).
        `f_clk = N / (2T)`, `f_c = 0.6 . f_clk / 2`, clamped. At T = 205 ms
        that is f_clk ~ 10 kHz and f_c ~ 3 kHz, and the darkening with TIME
        falls out of the arithmetic rather than being drawn as a curve. */
    static constexpr double kBbdStages = 4096.0;
    static constexpr double kBbdCutoffFactor = 0.6;
    static constexpr double kBbdCutoffMinHz = 800.0;
    static constexpr double kBbdCutoffMaxHz = 16000.0;
    static constexpr double kButterworthQ = 0.70710678118654752;

    /** The compander's ballistics (10 §4, CALIBRATE). */
    static constexpr double kCompanderAttackMs = 5.0;
    static constexpr double kCompanderReleaseMs = 50.0;

    /** §2's pitch glide: a 120 ms exponential, rate-limited to 0.25
        samples/sample so that `rho = 1 - dD/dn` stays inside [0.75, 1.25] --
        01's glide without runaway transposition. */
    static constexpr double kGlideTauSeconds = 0.120;
    static constexpr double kGlideRateLimit = 0.25;

    /** §5. MOD DEPTH 0-100 % reaches 0.5 % of speed on tape and
        bucket-brigade; clean takes the wow sine alone at 0-8 ms **absolute**,
        which is what makes it a chorus rather than a pitch wobble. */
    static constexpr double kModDepthFraction = 0.005;
    static constexpr double kCleanModSeconds = 0.008;
    static constexpr double kFlutterHz = 11.7;
    static constexpr double kFlutterRatio = 0.25;
    static constexpr double kModNoiseRatio = 0.3;
    static constexpr double kModNoiseCornerHz = 1.5;

    /** **The character floor** (DECIDED, Frosty 2026-09-23; not yet in 10 §5).

        Tape gets a little inherent wow that belongs to the character rather
        than to the MOD DEPTH knob, so that selecting TAPE at defaults sounds
        like tape rather than dead steady. MOD DEPTH **adds on top of it**: at
        0 you get the floor, at 100 % the floor plus §5's full 0.5 %.

        **Bucket-brigade gets none.** Its signature is the clock darkening as
        TIME lengthens and the compander breathing, both of which are there
        with no modulation at all, so it does not need pitch movement to sound
        like itself. Clean gets none because clean is clean.

        **0.03 % of speed is CALIBRATE** -- a nominal for `14`'s listening
        round, not a settled figure. It is 6 % of MOD DEPTH's full travel,
        about 0.52 cents of peak deviation; the target is something a listener
        notices the absence of rather than the presence of. One number is
        deliberate: §5's own ratios then carry the flutter (0.25x) and the wear
        noise (0.3x) with it, so what is added is a transport rather than three
        unrelated nominals.

        It rides the same oscillators the knob does, so MOD RATE is the
        machine's wow rate at every depth including zero -- and the default
        0.6 Hz is already a tape wow rate. The alternative reading, a second
        oscillator at a fixed rate of its own, is recorded here because `14`
        may prefer it once it is heard.

        **It is modulation, never gain.** The floor moves the read position and
        adds nothing to the output, so a zeroed ring read at any fractional
        position is still zero: silence in stays exact zeros out with no gate
        needed, and `P_c` -- swept from coefficients, not from audio -- cannot
        see it at all.

        Measured on AURORA, both ways, on the unity render: tape drifts
        **+0.044 dB** over 186 laps with the floor running and **+0.078 dB**
        with it at zero, against the law's own +0.09 dB. The floor therefore
        does not move where unity lands; the 0.034 dB between them is the
        sliver of energy the wow spreads out of the measured bin, which is what
        a wow is. */
    static constexpr double kTapeWowFloor = 0.0003;

    /** **Dual offset's ratio: the right line runs at 2/3 of the left's**
        (10 §8, §12 -- CALIBRATE there, and taken here as the figure the spec
        names rather than as a second guess at it). It is a ratio of the
        *steered* delay, so it survives a TIME move, the glide and the
        crossfade without a second law: whatever §2 does to `D`, the right line
        reads two thirds of the way along it.

        Note what the ratio does **not** move: `P_c` is swept at the left
        line's delay (§3), so the right line's interpolator phase is not in the
        sweep. Both interpolators are unity at DC and fall from there, so the
        right line cannot be the hotter of the two; the reading is recorded
        rather than hidden. */
    static constexpr double kDualOffsetRatio = 2.0 / 3.0;

    //==========================================================================
    /** What an engine is told. Note what is **not** here: which instance it
        is, what feeds it, and how its gain was arrived at.

        Everything 2b added is a field in this struct rather than a branch on
        the engine's identity, which is 10 §11.1's requirement written as a
        data structure. */
    struct Params
    {
        float timeMs      = 375.0f;     ///< the requested delay, in ms
        int   character   = kClean;     ///< clean, tape or bucket-brigade
        int   stereoMode  = kStereoIndependent;  ///< 10 §8's matrix, shared by both engines
        float lowCutHz    = 20.0f;      ///< in-loop HP, 20 Hz - 1 kHz
        float highCutHz   = 20000.0f;   ///< in-loop LP, capped at min(18k, 0.45 fs)
        float modRateHz   = 0.6f;       ///< wow, 0.1 - 8 Hz
        float modDepthPct = 0.0f;       ///< 0 - 100 %
        float drivePct    = 0.0f;       ///< into the ADAA residual shaper

        /** 10 §11a's in-loop FX stage, **one per engine**. Which three values
            arrive here is the caller's decision -- `fx_link` hands the lane
            engine the main's trio or its own (10 §11.3) -- and the engine
            cannot tell which it was given, which is the whole seam. */
        bool  fx          = false;      ///< off **skips** the stage, at no cost
        int   fxType      = kDiffuse;   ///< Diffuse, Pan/Tremolo or Crush
        float fxAmountPct = 35.0f;      ///< 0 - 100 %, its meaning per type
    };

    //==========================================================================
    /** Allocates both rings and every grid the sweep reads, from `maxTimeMs`
        and **never from a parameter** (10 §10). At 192 kHz the audio ring is
        524 288 samples a channel -- 2.0 MB -- and 0.5 MB at 44.1 kHz.

        **The control ring beside it is the same size again.** 10 §4 requires
        the compressor's gain to travel with the audio so the expander can
        apply its exact reciprocal to every tap the read takes, and a
        control ring shorter than the delay cannot do that. 10 §10's memory
        figure predates the compander and does not include it: the real cost is
        twice what §10 and `11` §4k quote. Flagged rather than quietly paid. */
    void prepare (double newSampleRate, int /*maxBlockSize*/, int numChannels, double maxTimeMs)
    {
        sampleRate = newSampleRate > 0.0 ? newSampleRate : 48000.0;
        channels   = std::clamp (numChannels, 1, kMaxChannels);

        maxDelay = (int) std::ceil (std::max (maxTimeMs, 1.0) * 0.001 * sampleRate);

        int size = 1;
        while (size < maxDelay + kRingGuard)
            size <<= 1;

        mask = size - 1;

        for (auto& line : ring)
            line.assign ((size_t) size, 0.0f);

        for (auto& line : gainRing)
            line.assign ((size_t) size, 1.0f);

        sinc.build (1.0, 8.0);
        probe.assign ((size_t) kProbeSize, 0.0f);

        gridOmega.assign ((size_t) kSweepPoints, 0.0);
        gridHz.assign ((size_t) kSweepPoints, 0.0);
        filterMagnitude.assign ((size_t) kSweepPoints, 1.0);
        kernelMagnitude.assign ((size_t) kSweepPoints, 1.0);

        const auto lo = 10.0;
        const auto hi = std::max (0.45 * sampleRate, lo * 2.0);

        for (int i = 0; i < kSweepPoints; ++i)
        {
            const auto f = lo * std::pow (hi / lo, (double) i / (double) (kSweepPoints - 1));
            gridHz[(size_t) i]    = f;
            gridOmega[(size_t) i] = 2.0 * kPiD * f / sampleRate;
        }

        fadeLength = std::max (1, (int) std::lround (sampleRate * kCrossfadeSeconds));
        glideAlpha = 1.0 - std::exp (-1.0 / (kGlideTauSeconds * sampleRate));

        // 30 ms on the feedback gain and on DRIVE, 10 §9. TIME is not smoothed
        // -- §2's two laws own it.
        feedback.prepare (sampleRate, 30.0);
        driveBlend.prepare (sampleRate, 30.0);
        driveCurve.prepare (sampleRate, 30.0);

        // 20 ms on FX AMOUNT, 10 §11's schema. The allpass *lengths* are not
        // smoothed -- see `FxStage::setAmount` for why they cannot be.
        fxAmount.prepare (sampleRate, 20.0);

        fx.prepare (sampleRate, FxStage::kMaxChannels);

        for (auto& f : filters)
            f.compressor.prepare (sampleRate, kCompanderAttackMs, kCompanderReleaseMs);

        prepareModulationNoise();

        primed = false;
        gainPrimed = false;
        drivePrimed = false;
        fxPrimed = false;

        buildUserFilters();
        buildModeFilters();
        applyDrive (true);
        applyFx (true);
        applyTime (params.timeMs, true);
        reset();
    }

    /** Clears audio state and leaves the coefficients, `P_c` and the targets
        alone: a reset is a silence, not a re-tune. */
    void reset() noexcept
    {
        for (auto& line : ring)
            std::fill (line.begin(), line.end(), 0.0f);

        // 1.0, not 0: the expander divides by what it reads here, and a zeroed
        // control ring would be a division by nothing.
        for (auto& line : gainRing)
            std::fill (line.begin(), line.end(), 1.0f);

        for (auto& f : filters)
        {
            f.lowCut.reset();
            f.highCut.reset();
            f.blocker.reset();
            f.tapeLowPass.reset();
            f.headBump.reset();
            f.bbdAntiAlias.reset();
            f.bbdReconstruct.reset();
            f.compressor.reset();
            f.shaper.reset();
        }

        // 10 §11.4 lists the FX stage's buffers among what a HOLD-off clear
        // zeroes, and a clear is `reset()` -- so they are cleared here and
        // nowhere else, which is why `DspCore::clearLane` needs no second copy
        // of this list.
        fx.reset();

        writeIdx = 0;
        fadeCounter = -1;
        delayCurrent = delayNext = delayTarget;

        // The gain ring is all 1.0 again, so there is nothing to expand.
        samplesSinceCompanding = ringSize();

        wowPhase = flutterPhase = 0.0;
        noiseA = noiseB = 0.0;
        noiseState = kNoiseSeed;
    }

    /** TIME, CHARACTER, the two cuts, the modulation pair and DRIVE.

        Re-sweeps `P_c` whenever anything the reference is defined against
        moves -- character, TIME on bucket-brigade, or the sample rate -- per
        10 §3, which is why the caller must set these **before** reading
        `referenceLoopPeak()` to build its gain. **The cuts and DRIVE never
        re-sweep**: §3 fixes the reference at the user stages' neutral limits
        on purpose, so that a user's cut can only ever shorten the tail.

        `snapNow` takes the new values without §2's crossfade -- what `prepare`
        wants, and nothing else: a delay that faded in from its default on
        every insert would be a delay that ignored the session it was asked
        for. */
    void setParams (const Params& p, bool snapNow = false) noexcept
    {
        const auto characterMoved = (p.character != params.character) || ! primed || snapNow;
        const auto cutsMoved = (p.lowCutHz != params.lowCutHz) || (p.highCutHz != params.highCutHz);
        const auto timeMoved = (p.timeMs != params.timeMs);

        // Crush coming (back) into the loop starts from nothing; see
        // `FxStage::engageCrush`.
        const auto crushIn = p.fx && p.fxType == kCrush;
        const auto crushWasIn = params.fx && params.fxType == kCrush;

        if (crushIn && ! crushWasIn)
            fx.engageCrush();

        params = p;

        if (snapNow)
            primed = false;

        if (characterMoved || cutsMoved)
            buildUserFilters();

        // Bucket-brigade's mode filters are derived from TIME (`f_clk = N/2T`),
        // so on that character a TIME move **is** a filter move and the
        // reference has to be re-swept with it. On clean and tape it is not.
        const auto modeMoved = characterMoved || (timeMoved && modeFiltersFollowTime());

        if (modeMoved)
            buildModeFilters();

        if (characterMoved && primed)
            handOverTimeLaw();

        applyDrive (snapNow);
        applyFx (snapNow);
        applyTime (p.timeMs, modeMoved);
    }

    /** Puts the read on the target delay now: no glide, no crossfade. `P_c`
        is already the target's -- `setParams` sweeps at the target, not at the
        read -- so nothing else has to move. On a ring with content this would
        jump the read, which is why the caller uses it only where the ring is
        empty (`DwellDsp::setTempo`). */
    void landTime() noexcept
    {
        delayCurrent = delayNext = delayTarget;
        fadeCounter = -1;
    }

    /** The loop gain the caller's law arrived at. Smoothed at 30 ms (10 §9),
        snapped on the first call after `prepare` so a freshly placed instance
        is not ramping up from zero. */
    void setFeedbackGain (float g, bool snapNow = false) noexcept
    {
        if (! gainPrimed || snapNow)
        {
            feedback.snap (g);
            gainPrimed = true;
            return;
        }

        feedback.setTarget (g);
    }

    /** `P_c` -- the maximum of |H(e^jw) . I(e^jw)| over 10 Hz to 0.45 f_s,
        with this engine's built filter coefficients and its built interpolator
        kernel at this engine's own TIME (10 §3, §11.2). **Computed by sweep,
        never hardcoded.** Near 0.999 on clean, 1.054 on tape, and 0.990-0.999
        on bucket-brigade as TIME moves its clock. */
    double referenceLoopPeak() const noexcept { return loopPeak; }

    /** Where in the band that peak sits. Not used by the loop -- it is what a
        listening or CALIBRATE pass needs in order to drive the loop at the one
        frequency the unity claim is about (10 §3: "unity means the loudest
        band neither grows nor decays"). */
    double referencePeakHz() const noexcept { return loopPeakHz; }

    /** The ring's length in samples: a power of two, sized from the fixed
        maximum. */
    int ringSize() const noexcept { return mask + 1; }

    /** The delay the engine is reading now, in samples. Whole-sample times
        give a whole number here, which is what makes |I| exactly 1. */
    double currentDelaySamples() const noexcept { return delayCurrent; }

    /** Bucket-brigade's anti-alias and reconstruction corner at the current
        TIME, in Hz -- 0 on the characters that have no clock. Published for
        the measurement tools and for `11` §4d, which has to see the corner
        track the clock rather than sit at a constant. */
    double modeCutoffHz() const noexcept { return bbdCutoffHz; }

    /** The FX stage's whole state as one number, for `11` §4l.

        With FX off this must sit at its reset value however hard the loop is
        driven -- which is the difference between a stage that is **skipped**
        and one that is run at a zero coefficient (10 §11a). The latter would
        pass a bit-identity test on the audio while still filling its allpass
        lines and turning its LFO, and would then jump when FX came on. */
    double fxStateSignature() const noexcept { return fx.stateSignature(); }

    /** The smoothed loop gain and DRIVE pair as they stand, for the test
        that holds them to landing exactly on their targets. */
    float smoothedFeedbackGain() const noexcept { return feedback.value(); }
    float smoothedDriveBlend() const noexcept   { return driveBlend.value(); }
    float smoothedDriveCurve() const noexcept   { return driveCurve.value(); }
    float smoothedFxAmount() const noexcept     { return fxAmount.value(); }

    //==========================================================================
    /** One block. Allocates nothing: every buffer and every grid came from
        `prepare`.

        The loop is 10 §3's `v[n] = x[n] + g . C(y[n])` with **no input gate**
        -- the `s` term is gone, not repurposed (§11, §11.1) -- and the engine's
        output is the raw read `y`, tapped before the character chain, so
        colour accumulates one pass per lap.

        On bucket-brigade the compander straddles the ring rather than sitting
        inside `C(.)`: the compressor's gain multiplies what is written and is
        written **beside** it, and the expander divides every tap the read
        takes by the gain written beside that tap, before the kernel sums them
        (`readExpanded`). That pair is unity at every instant, transient
        included, which is why `P_bbd` comes from the filters alone (10 §4). There is no second detector, so there is nothing
        to overshoot.

        **10 §8's stereo matrix is here, between the read and the write**, and
        it is the reason the loop is written as two passes over the channels
        rather than one: ping-pong's injection needs *both* lines' post-chain
        signals before either can be written, so `C(.)` has to have run on
        every channel before the first ring write. Stereo and dual offset are
        the identity matrix and would not need it; running one shape for all
        three is what keeps the mode a parameter rather than three loops. */
    void process (const float* const* input, float* const* output, int numChannels, int numSamples) noexcept
    {
        if (mask <= 0 || numSamples <= 0 || numChannels <= 0)
            return;

        const auto nch = std::min (numChannels, channels);
        const auto glide = usesGlide();
        const auto compand = usesCompander();

        // **The expander outlives the compressor by one ring.** A CHARACTER
        // move off bucket-brigade leaves companded samples in the ring -- up
        // to +30 dB on a quiet line -- and they still have to be divided back
        // when they are read, or the repeat in flight comes back that much
        // too loud (measured on AURORA 2026-10-01: a 0.05 tone replayed at a
        // 0.558 peak). Writes made without companding store a gain of exactly
        // 1.0, so dividing across the boundary is the identity on the new
        // side; once a whole ring has been written since the last companded
        // sample there is nothing left to undo and the branch goes quiet, so
        // clean and tape are bit-identical to before whenever no
        // bucket-brigade content is left to read.
        const auto expand = compand || samplesSinceCompanding < ringSize();

        // 10 §8's mono bus rule: with one line there is no second output to
        // alternate into, so ping-pong collapses to plain stereo. Dual offset
        // collapses too -- `lineRatio` only ever moves line 1.
        const auto pingPong = params.stereoMode == kPingPong && nch >= 2;

        for (int n = 0; n < numSamples; ++n)
        {
            // These four *land* on their targets (`Smoother::tickLanding`):
            // a plain tick stalls up to 1.7e-4 short at 192 kHz, and near
            // unity a loop gain left that far over its target sits at or past
            // unity (2026-10-01, measured on AURORA).
            const auto gain = (double) feedback.tickLanding();
            const auto blend = (double) driveBlend.tickLanding();
            const auto curve = (double) driveCurve.tickLanding();
            const auto fxDepth = (double) fxAmount.tickLanding();

            advanceTime (glide);

            // The FX stage's one piece of per-engine shared state: a pan wants
            // **one** position that the two channels read opposite ends of, so
            // the LFO turns once a sample for the engine rather than once a
            // sample per channel. It is stepped at the delay period, so each
            // repeat gets its own place (10 §11a). Turned only when FX is on,
            // which is what `11` §4l's state signature measures.
            if (params.fx)
                fx.advance (delayCurrent);

            // §5's modulation follows §2's law and is never limited by it, so
            // it is taken here once a sample and applied to **both** taps of a
            // crossfade rather than to the delay the law is steering.
            const auto modulated = advanceModulation();

            const auto fading = ! glide && fadeCounter >= 0;
            auto fadeOld = 1.0, fadeNew = 0.0;

            if (fading)
            {
                const auto u = (double) fadeCounter / (double) fadeLength;
                fadeOld = std::cos (0.5 * kPiD * u);
                fadeNew = std::sin (0.5 * kPiD * u);
            }

            // Pass one: read every line, publish the tap, and run the
            // character chain. Nothing is written to a ring here, because
            // ping-pong's write needs the *other* channel's `c`.
            std::array<double, kMaxChannels> chain {};

            for (int ch = 0; ch < nch; ++ch)
            {
                // 10 §8's dual offset: line 1 reads two thirds of the way
                // along the same steered delay, so §2's law and §5's
                // modulation carry over untouched.
                const auto ratio = lineRatio (ch);
                const auto readCurrent = readPosition (delayCurrent * ratio, modulated);
                const auto readNext    = fading ? readPosition (delayNext * ratio, modulated)
                                                : readCurrent;

                const auto* line = ring[(size_t) ch].data();

                double y;

                if (expand)
                {
                    // **The exact reciprocal of each tap, then the kernel**
                    // (10 §4) -- see `readExpanded`. A crossfade's two reads
                    // are each expanded before they are mixed, for the same
                    // reason.
                    const auto* gains = gainRing[(size_t) ch].data();
                    y = readExpanded (line, gains, readCurrent);

                    if (fading)
                        y = fadeOld * y + fadeNew * readExpanded (line, gains, readNext);
                }
                else
                {
                    y = readAt (line, readCurrent);

                    if (fading)
                        y = fadeOld * y + fadeNew * readAt (line, readNext);
                }

                output[ch][n] = (float) y;

                chain[(size_t) ch] = character (filters[(size_t) ch], ch, y, blend, curve,
                                                fxDepth, nch);
            }

            // Pass two: 10 §8's matrix, then the write. **There is still no
            // input gate** -- `v = u + g . C(y)`, and the `s` term 10 §11
            // removed is not reintroduced here under another name. What the
            // matrix changes is *which* line a sample is injected into and
            // *which* line's chain output feeds it back, never whether the
            // input arrives.
            for (int ch = 0; ch < nch; ++ch)
            {
                double u = 0.0;
                double back = 0.0;

                if (pingPong)
                {
                    // `u' = (L + R)/2` into line L only; the matrix is the
                    // swap, so repeats alternate sides every T.
                    if (ch == 0)
                    {
                        const auto l = (double) input[0][n], r = (double) input[1][n];
                        u = 0.5 * ((std::isfinite (l) ? l : 0.0) + (std::isfinite (r) ? r : 0.0));
                    }

                    back = gain * chain[(size_t) (ch ^ 1)];
                }
                else
                {
                    const auto x = (double) input[ch][n];
                    u = std::isfinite (x) ? x : 0.0;
                    back = gain * chain[(size_t) ch];
                }

                auto v = u + back;

                // A NaN that reached the ring would circulate for ever, so it
                // is stopped at the write rather than at the output.
                if (! std::isfinite (v))
                    v = 0.0;

                auto gc = 1.0;

                if (compand)
                {
                    auto& f = filters[(size_t) ch];
                    gc = companderGainFor (f.compressor.tick (LevelDetectorDb::levelDb (v)));
                    v *= gc;

                    if (! std::isfinite (v))
                        v = 0.0;
                }

                if (std::abs (v) < 1.0e-25)
                    v = 0.0;

                ring[(size_t) ch][(size_t) writeIdx] = (float) v;
                gainRing[(size_t) ch][(size_t) writeIdx] = (float) gc;
            }

            for (int ch = nch; ch < numChannels; ++ch)
                output[ch][n] = 0.0f;

            writeIdx = (writeIdx + 1) & mask;

            if (compand)
                samplesSinceCompanding = 0;
            else if (samplesSinceCompanding < ringSize())
                ++samplesSinceCompanding;

            if (fading && ++fadeCounter >= fadeLength)
            {
                fadeCounter  = -1;
                delayCurrent = delayNext;

                // A target that arrived mid-fade is picked up here rather than
                // interrupting the fade in flight.
                if (delayTarget != delayCurrent)
                {
                    delayNext = delayTarget;
                    fadeCounter = 0;
                }
            }
        }
    }

private:
    //==========================================================================
    struct ChannelFilters
    {
        TptOnePole lowCut, highCut, blocker;
        TptOnePole tapeLowPass, headBump;
        TptSvfLowPass bbdAntiAlias, bbdReconstruct;
        LevelDetectorDb compressor;
        bmo::sat::AsymmetricShaper shaper;
    };

    bool usesGlide() const noexcept            { return params.character != kClean; }
    bool usesCompander() const noexcept        { return params.character == kBucketBrigade; }
    bool modeFiltersFollowTime() const noexcept { return params.character == kBucketBrigade; }
    bool usesSinc() const noexcept             { return params.character == kClean; }

    /** How far along the steered delay this line reads, as a fraction of it.
        1 everywhere except dual offset's right line (10 §8). */
    double lineRatio (int ch) const noexcept
    {
        return (params.stereoMode == kDualOffset && ch == 1) ? kDualOffsetRatio : 1.0;
    }

    /** 10 §4's loop order: LOW CUT -> HIGH CUT -> mode filters -> (FX, 2c) ->
        shaper -> DC blocker -> clip. **The last two are swapped against what
        §4 writes**, for a measured reason set out at the blocker below.

        The two cuts are **wired** from 2b on. They start on the same rails 2a
        pinned them to -- 20 Hz and the 18 kHz cap -- which is exactly the
        reference §3 defines `P_c` against, so a fresh instance's running loop
        peak still *is* `P_c` and unity still lands where the law says it does.
        Moving either can only attenuate, so a cut can only shorten the tail;
        §3 chose that over tracking them live.

        The mode filters are the characters themselves: tape's 4.5 kHz rolloff
        and its head bump, bucket-brigade's clock-derived Butterworth pair.
        Clean has none, so at DRIVE 0 this function is bit-identical to 2a's --
        which is what keeps 2a's measured figures standing rather than merely
        close.

        The shaper is the repo's ADAA residual (00 §1), faded in by DRIVE so
        that **DRIVE 0 is exactly inert** rather than nearly so: the stage is
        branched past entirely, so nothing is spent and nothing is changed.
        Above zero the residual is scaled by the same blend, which is what
        makes the control continuous at its own bottom stop. The shaper's
        incremental gain is `sech^2(a x + b) <= 1` at every drive, so §3's
        `kappa <= 1` holds and the loop bound is unaffected.

        The safety clip is not optional: it is what makes the top of FEEDBACK's
        travel a limit cycle instead of a divergence (§3, §11.6), and it is on
        regardless of DRIVE. */
    double character (ChannelFilters& f, int ch, double y, double blend, double curve,
                      double fxDepth, int nch) noexcept
    {
        auto c = f.lowCut.highPass (y);
        c = f.highCut.lowPass (c);

        if (params.character == kTape)
        {
            c = f.tapeLowPass.lowPass (c);
            c = f.headBump.lowShelf (c, headBumpGain);
        }
        else if (params.character == kBucketBrigade)
        {
            c = f.bbdAntiAlias.process (c);
            c = f.bbdReconstruct.process (c);
        }

        // **10 §11a's FX stage: after the mode filters, before the shaper, and
        // skipped outright when off.** The position is the whole of what makes
        // it an FX *in the loop* -- it is ahead of the blocker, so an offset a
        // candidate introduces is removed rather than compounded, and ahead of
        // the clip, so §3's bound still ends where it did.
        //
        // **This branch is the acceptance, not an optimisation.** §11a says off
        // means the stage is skipped rather than run at a zero coefficient, so
        // that FX off is bit-identical to the loop without it -- and because
        // each engine holds its own `FxStage`, that identity holds **per path**
        // without anything here knowing which path it is on.
        if (params.fx)
            c = fx.process (ch, params.fxType, c, fxDepth, nch);

        if (blend > 0.0)
        {
            f.shaper.setDrive ((float) curve);
            c += blend * (double) f.shaper.processResidual ((float) c);
        }

        // **The 10 Hz blocker sits AFTER the shaper, not before it, and this
        // is a deliberate departure from 10 §4's written order** (found on
        // AURORA, 2026-09-23 -- flag it for the spec).
        //
        // §4 says "a 10 Hz blocker before the shaper, which is asymmetric --
        // offset would compound per repeat", and the reason is right while the
        // placement does not serve it. Before the shaper the blocker is
        // **redundant**: LOW CUT is a one-pole high-pass whose gain at DC is
        // exactly zero and whose lowest setting, 20 Hz, is already above the
        // blocker's corner, so nothing with an offset ever reaches that point
        // in the chain. What does have an offset is the shaper's own output --
        // `shape(x) = (tanh(a x + b) - tanh(b)) / a` is asymmetric by design,
        // and at DRIVE 100 its mean over a hard-driven symmetric signal is
        // about -0.012. **Measured on AURORA both ways, at FEEDBACK 100 and
        // DRIVE 100 on a 537 Hz tone: -38.7 dBFS with the blocker in §4's
        // place, -114.3 dBFS with it here.** The first went straight into the
        // ring and out of the engine's tap, against `11` §4c's DC <= -80 dBFS;
        // the second is 34 dB the right side of it.
        //
        // It did not *compound* -- the next lap's LOW CUT removed it -- so §4's
        // stated fear was answered even as its acceptance was missed. Moving
        // the blocker here answers both, and costs nothing anywhere else: with
        // DRIVE 0 the shaper is branched past entirely, so the cascade and
        // therefore `P_c` are identical either way, and the order of linear
        // stages never changes a magnitude.
        c = f.blocker.highPass (c);

        return std::tanh (c);
    }

    //==========================================================================
    /** Where the read lands this sample: §2's steered delay with §5's
        modulation on top of it, clamped so that a deep wow can never walk the
        read past the write or past the ring's end. */
    double readPosition (double delay, double modulated) const noexcept
    {
        const auto d = params.character == kClean ? delay + modulated
                                                  : delay * (1.0 + modulated);

        return std::clamp (d, kMinDelaySamples, (double) std::max (maxDelay, 3));
    }

    double readAt (const float* line, double delay) const noexcept
    {
        const auto pos = (double) writeIdx - delay;

        // 10 §1: a polyphase Kaiser sinc on clean, 24 taps by default (see
        // BMO_DWELL_SINC_TAPS at the head of this file) -- unity gain at every
        // phase and an exact delay at whole samples, so the repeat chain
        // accumulates no phase-dependent HF loss. Tape and bucket-brigade take
        // 4-point 3rd-order Hermite instead, where the per-repeat HF loss is
        // *wanted*; so does clean below the sinc's headroom.
        if (usesSinc() && delay >= (double) kSincFloor)
            return (double) sinc.read (line, mask, pos);

        return hermite (pos, [line, this] (int k) noexcept
        {
            return (double) line[(size_t) (k & mask)];
        });
    }

    /** **The expander's read: every tap divided by its own gain, then the
        kernel** (10 §4, fixed 2026-10-01, fourth round).

        Until then the read divided one interpolation by another,
        `I(v . g) / I(g)`, which is the reciprocal only while `g` is constant
        across the kernel. Where it is not -- the gain ring holds 1.0 wherever
        nothing was companded, beside compressor gains of up to 31.6 -- a
        kernel with negative taps carries a step from 1.0 to 31.6 through zero,
        the clamp below turned that into a division by 1e-6, and one output
        sample came out at 5.5e5 (+115 dBFS): on the first sample after
        `prepare` or `reset`, on a move onto bucket-brigade mid-signal, on a
        move off it with signal starting at the move, under MOD, under a TIME
        move, and in the lane. Measured on AURORA, on every fractional read.

        `I(v . g / g)` is the reciprocal at every instant whatever the gain
        does, so the read is an interpolation of what was written and can be no
        larger than that times the kernel's absolute sum (1.25 for Hermite,
        2.21 for the 24-tap sinc). At a whole-sample position both forms are
        the same division of the same two floats, so a steady whole-sample
        bucket-brigade read is bit-identical to before; at a fractional one
        they differ only by how far the gain moved across the kernel.

        The ring's gains are 1.0 or `companderGainFor`'s 0.708 to 31.6, so the
        clamp can no longer fire; it stays so that a corrupted ring cannot
        divide by zero. Clean's sinc reads the expanded taps out of a small
        stack copy, laid out at the same indices modulo its size, so the table
        in `modules/tune` is used as it is. */
    double readExpanded (const float* line, const float* gains, double delay) const noexcept
    {
        const auto pos = (double) writeIdx - delay;

        const auto expanded = [line, gains, this] (int k) noexcept
        {
            const auto j = (size_t) (k & mask);
            return (double) line[j] / std::max ((double) gains[j], 1.0e-6);
        };

        if (usesSinc() && delay >= (double) kSincFloor)
        {
            std::array<float, kExpandScratch> taps {};
            const auto base = (int) (long long) std::floor (pos) - Sinc::kHalf + 1;

            for (int k = base; k < base + Sinc::kTaps; ++k)
                taps[(size_t) (k & (kExpandScratch - 1))] = (float) expanded (k);

            return (double) sinc.read (taps.data(), kExpandScratch - 1, pos);
        }

        return hermite (pos, expanded);
    }

    /** The smallest power of two that holds the sinc's taps. */
    static constexpr int kExpandScratch = Sinc::kTaps <= 32 ? 32 : 64;
    static_assert (Sinc::kTaps <= 64, "readExpanded's scratch holds at most 64 taps");

    template <typename Tap>
    static double hermite (double pos, Tap&& at) noexcept
    {
        const auto whole = std::floor (pos);
        const auto f = pos - whole;
        const auto i = (int) (long long) whole;

        const auto ym1 = at (i - 1), y0 = at (i), y1 = at (i + 1), y2 = at (i + 2);

        const auto c0 = y0;
        const auto c1 = 0.5 * (y1 - ym1);
        const auto c2 = ym1 - 2.5 * y0 + 2.0 * y1 - 0.5 * y2;
        const auto c3 = 0.5 * (y2 - ym1) + 1.5 * (y0 - y1);

        return ((c3 * f + c2) * f + c1) * f + c0;
    }

    /** The four Hermite coefficients as a kernel, for the sweep. */
    static void hermiteKernel (double f, double* k) noexcept
    {
        k[0] = -0.5 * f +       f * f - 0.5 * f * f * f;   // y[-1]
        k[1] =  1.0     - 2.5 * f * f + 1.5 * f * f * f;   // y[0]
        k[2] =  0.5 * f + 2.0 * f * f - 1.5 * f * f * f;   // y[+1]
        k[3] =          - 0.5 * f * f + 0.5 * f * f * f;   // y[+2]
    }

    //==========================================================================
    /** LOW CUT and HIGH CUT as the user set them -- **and the reference
        coefficients, which are not**.

        §3 defines `P_c` at the neutral limits, so the two are built side by
        side here and the sweep reads the second set. That is the whole of what
        "the user's cuts only ever shorten the tail" amounts to in code. */
    void buildUserFilters() noexcept
    {
        const auto cap = std::min (18000.0, 0.45 * sampleRate);

        const auto lowCutHz  = std::clamp ((double) params.lowCutHz, 20.0, 1000.0);
        const auto highCutHz = std::clamp ((double) params.highCutHz, 1000.0, cap);
        const auto blockerHz = 10.0;

        for (auto& f : filters)
        {
            f.lowCut.setCutoff (lowCutHz, sampleRate);
            f.highCut.setCutoff (highCutHz, sampleRate);
            f.blocker.setCutoff (blockerHz, sampleRate);
        }

        // The reference: LOW CUT on its 20 Hz rail, HIGH CUT at the cap, the
        // 10 Hz blocker in. The cap is load-bearing -- it keeps the shaper's
        // input away from Nyquist, where first-order ADAA is weakest.
        TptOnePole reference;
        reference.setCutoff (20.0, sampleRate);
        referenceLowCut = reference.coeff();
        reference.setCutoff (cap, sampleRate);
        referenceHighCut = reference.coeff();
        reference.setCutoff (blockerHz, sampleRate);
        referenceBlocker = reference.coeff();
    }

    /** The mode filters -- the characters, as coefficients.

        Bucket-brigade's corner is taken from **the requested TIME, not the
        gliding delay**, so that a time move settles the tone once rather than
        sweeping it through the glide; `modeCutoffHz()` publishes the figure so
        `11` §4d can assert it tracks the clock. */
    void buildModeFilters() noexcept
    {
        headBumpGain = std::pow (10.0, kHeadBumpDb / 20.0);

        for (auto& f : filters)
        {
            f.tapeLowPass.setCutoff (kTapeLowPassHz, sampleRate);
            f.headBump.setCutoff (kHeadBumpHz, sampleRate);
        }

        if (params.character == kBucketBrigade)
        {
            const auto seconds = std::max ((double) params.timeMs, 1.0) * 0.001;
            const auto clockHz = kBbdStages / (2.0 * seconds);

            bbdCutoffHz = std::clamp (kBbdCutoffFactor * clockHz * 0.5,
                                      kBbdCutoffMinHz, kBbdCutoffMaxHz);
            bbdCutoffHz = std::min (bbdCutoffHz, 0.45 * sampleRate);

            for (auto& f : filters)
            {
                f.bbdAntiAlias.set (bbdCutoffHz, sampleRate, kButterworthQ);
                f.bbdReconstruct.set (bbdCutoffHz, sampleRate, kButterworthQ);
            }
        }
        else
        {
            bbdCutoffHz = 0.0;
        }
    }

    /** DRIVE, as a blend and a curve. The curve reuses `modules/sat`'s own
        fitted ends (00 §1) rather than inventing a second range for the same
        shaper; the blend is what makes DRIVE 0 inert. */
    void applyDrive (bool snapNow) noexcept
    {
        const auto t = std::clamp ((double) params.drivePct * 0.01, 0.0, 1.0);
        const auto lo = (double) bmo::sat::tables::kDriveMin;
        const auto hi = (double) bmo::sat::tables::kDriveMax;
        const auto curve = lo * std::pow (hi / lo, t);

        if (! drivePrimed || snapNow)
        {
            driveBlend.snap ((float) t);
            driveCurve.snap ((float) curve);
            drivePrimed = true;
            return;
        }

        driveBlend.setTarget ((float) t);
        driveCurve.setTarget ((float) curve);
    }

    /** FX AMOUNT, as a smoothed 0-1 and as the block-rate figure the allpass
        lengths are cut from.

        **`setAmount` is called whether or not FX is on**, and that costs
        nothing: it writes six integers and touches no audio. What must not
        happen while FX is off is the stage *running*, and that is a branch in
        `character()` rather than a coefficient of zero -- §11a is explicit that
        off means skipped, and `11` §4l asserts it against the stage's own
        state signature rather than against a timing. */
    void applyFx (bool snapNow) noexcept
    {
        const auto t = std::clamp ((double) params.fxAmountPct * 0.01, 0.0, 1.0);

        fx.setAmount (t);

        if (! fxPrimed || snapNow)
        {
            fxAmount.snap ((float) t);
            fxPrimed = true;
            return;
        }

        fxAmount.setTarget ((float) t);
    }

    /** A character change swaps §2's law under a delay that may be mid-move.
        Neither direction may jump `D`: a glide picks the read up wherever the
        fade had got to, and a crossfade starts from wherever the glide had. */
    void handOverTimeLaw() noexcept
    {
        if (usesGlide())
        {
            fadeCounter = -1;
            return;
        }

        if (fadeCounter < 0 && delayCurrent != delayTarget)
        {
            delayNext = delayTarget;
            fadeCounter = 0;
        }
    }

    void applyTime (float timeMs, bool filtersMoved) noexcept
    {
        const auto target = std::clamp ((double) timeMs * 0.001 * sampleRate,
                                        kMinDelaySamples, (double) std::max (maxDelay, 3));

        if (! primed)
        {
            delayCurrent = delayNext = delayTarget = target;
            fadeCounter  = -1;
            primed = true;
            refreshLoopPeak (true);
            return;
        }

        if (target != delayTarget)
        {
            delayTarget = target;

            // §2's clean law: the old tap freezes, a new one starts at the
            // target, equal-power raised cosine across it. Tape and
            // bucket-brigade glide instead, per sample, in `advanceTime`.
            if (! usesGlide() && fadeCounter < 0)
            {
                delayNext = target;
                fadeCounter = 0;
            }

            refreshLoopPeak (filtersMoved);
        }
        else if (filtersMoved)
        {
            refreshLoopPeak (true);
        }
    }

    /** §2's tape / BBD law: a rate-limited exponential, tau 120 ms, capped at
        0.25 samples/sample so the read rate `rho = 1 - dD/dn` stays inside
        [0.75, 1.25] -- 01's glide without runaway transposition. */
    void advanceTime (bool glide) noexcept
    {
        if (! glide)
            return;

        const auto diff = delayTarget - delayCurrent;

        if (diff == 0.0)
            return;

        delayCurrent += std::clamp (diff * glideAlpha, -kGlideRateLimit, kGlideRateLimit);

        if (std::abs (delayTarget - delayCurrent) < 1.0e-9)
            delayCurrent = delayTarget;
    }

    //==========================================================================
    /** §5, once a sample.

        Returns **a fraction of speed** on tape and bucket-brigade and **an
        offset in samples** on clean, because §5 gives clean an absolute
        0-8 ms swing: a chorus, not a pitch wobble, and one that does not grow
        with TIME the way a transport's does.

        Everything here advances per sample and nothing per block, which is
        what `11` §4k's block-size invariance is really testing. */
    double advanceModulation() noexcept
    {
        const auto knob = std::clamp ((double) params.modDepthPct * 0.01, 0.0, 1.0);

        // 01: on a transport both rate and depth scale with time.
        const auto timeScale = params.character == kClean
                             ? 1.0
                             : std::clamp ((double) params.timeMs / 300.0, 0.5, 2.0);

        const auto wowRate = std::clamp ((double) params.modRateHz, 0.1, 8.0) * timeScale;

        wowPhase += 2.0 * kPiD * wowRate / sampleRate;
        if (wowPhase >= 2.0 * kPiD) wowPhase -= 2.0 * kPiD;

        flutterPhase += 2.0 * kPiD * kFlutterHz / sampleRate;
        if (flutterPhase >= 2.0 * kPiD) flutterPhase -= 2.0 * kPiD;

        const auto noise = advanceNoise();

        if (params.character == kClean)
        {
            // Clean has no floor, no flutter and no wear noise: §5 gives it
            // the wow sine alone, in milliseconds.
            const auto peak = knob * kCleanModSeconds * sampleRate;
            return peak * std::sin (wowPhase);
        }

        // The character floor sums into the depth rather than replacing it, so
        // MOD DEPTH 0 is the floor and 100 % is the floor plus §5's full
        // 0.5 %. Bucket-brigade's floor is zero by decision, not by omission.
        const auto floorDepth = params.character == kTape ? kTapeWowFloor : 0.0;
        const auto depth = (knob * kModDepthFraction + floorDepth) * timeScale;

        if (depth <= 0.0)
            return 0.0;

        auto m = std::sin (wowPhase);

        // Flutter is tape's alone (01: 10-100 Hz, the low end is the musical
        // one). Its 11.7 Hz is a mechanical resonance rather than the MOD RATE
        // knob, so it is **not** scaled by TIME the way the wow rate is. §5
        // does not say either way; the reading is recorded rather than hidden.
        if (params.character == kTape)
            m += kFlutterRatio * std::sin (flutterPhase);

        m += kModNoiseRatio * noise;

        return depth * m;
    }

    /** §5's wear: white through two cascaded 1.5 Hz one-poles.

        Normalised to unit standard deviation in closed form rather than by
        measurement, so that "0.3 x wow" is a ratio of like to like at every
        sample rate. For a cascade of two one-poles with coefficient k and pole
        p = 1 - k, the output variance for unit-variance white input is
        `k^4 (1 + p^2) / (1 - p^2)^3` -- the sum of (n+1)^2 p^2n weighted by
        k^4.

        Bounded at +-3 sigma: a random walk allowed an arbitrarily large
        excursion is a delay allowed an arbitrarily large pitch jump, and 01's
        wear is neither. */
    double advanceNoise() noexcept
    {
        noiseState = noiseState * 1103515245u + 12345u;
        const auto white = (double) (noiseState >> 8) / 8388608.0 - 1.0;

        noiseA += noiseCoeff * (white - noiseA);
        noiseB += noiseCoeff * (noiseA - noiseB);

        if (std::abs (noiseA) < 1.0e-25) noiseA = 0.0;
        if (std::abs (noiseB) < 1.0e-25) noiseB = 0.0;

        return std::clamp (noiseB * noiseNorm, -3.0, 3.0);
    }

    void prepareModulationNoise() noexcept
    {
        noiseCoeff = 1.0 - std::exp (-2.0 * kPiD * kModNoiseCornerHz / sampleRate);

        const auto p = 1.0 - noiseCoeff;
        const auto pp = p * p;
        const auto variance = std::pow (noiseCoeff, 4.0) * (1.0 + pp)
                            / std::pow (std::max (1.0 - pp, 1.0e-30), 3.0);

        // The generator above is uniform on [-1, 1), variance 1/3, so the white
        // input is brought to unit variance before the cascade's own gain is
        // divided out.
        noiseNorm = 1.0 / std::max (std::sqrt (variance / 3.0), 1.0e-30);
    }

    //==========================================================================
    /** 10 §3's sweep. `P_c` is re-taken on any change of character, TIME or
        sample rate and at no other time -- `setParams` runs on the audio
        thread, so the two grids it reads are cached and only the part that
        actually moved is rebuilt. The interpolator's part is rebuilt only once
        the read phase has moved by more than the table's own 1/256-sample
        quantisation, below which there is nothing new to measure. */
    void refreshLoopPeak (bool filtersMoved) noexcept
    {
        if (filtersMoved)
            buildFilterMagnitudes();

        if (filtersMoved || std::abs (delayTarget - kernelDelay) > (1.0 / 256.0))
            buildKernelMagnitudes (delayTarget);

        auto peak = 0.0;
        auto peakHz = gridHz.empty() ? 0.0 : gridHz[0];

        for (int i = 0; i < (int) filterMagnitude.size(); ++i)
        {
            const auto m = filterMagnitude[(size_t) i] * kernelMagnitude[(size_t) i];

            if (m > peak)
            {
                peak = m;
                peakHz = gridHz[(size_t) i];
            }
        }

        loopPeak   = std::max (peak, 1.0e-6);
        loopPeakHz = peakHz;
    }

    /** The reference chain of §3: the cuts at their neutral limits, the
        blocker, and **the character's own mode filters**, which are not user
        stages and so appear at their working values. The compander is unity by
        construction and so contributes exactly 1 and is not swept. */
    void buildFilterMagnitudes() noexcept
    {
        for (int i = 0; i < (int) filterMagnitude.size(); ++i)
        {
            const auto w = gridOmega[(size_t) i];

            auto m = TptOnePole::highPassMagnitude (referenceLowCut,  w)
                   * TptOnePole::lowPassMagnitude  (referenceHighCut, w)
                   * TptOnePole::highPassMagnitude (referenceBlocker, w);

            if (params.character == kTape)
            {
                m *= TptOnePole::lowPassMagnitude (filters[0].tapeLowPass.coeff(), w);
                m *= TptOnePole::lowShelfMagnitude (filters[0].headBump.coeff(), headBumpGain, w);
            }
            else if (params.character == kBucketBrigade)
            {
                const auto g = filters[0].bbdAntiAlias.coeff();
                const auto k = filters[0].bbdAntiAlias.damping();
                const auto one = TptSvfLowPass::magnitude (g, k, w);
                m *= one * one;
            }

            filterMagnitude[(size_t) i] = m;
        }
    }

    /** |I(e^jw)| for the interpolator **as built**, not as specified.

        The sinc's coefficients live inside `SincTable`, so the kernel is taken
        back out of it by reading a unit impulse at the phase the loop will
        actually read at -- 32 reads of a 128-sample scratch ring, off any
        audio. That is the difference between sweeping the table that ships and
        sweeping a second copy of the formula that built it.

        Which interpolator is swept follows the character, because which one
        the loop runs does (10 §1). */
    void buildKernelMagnitudes (double delay) noexcept
    {
        kernelDelay = delay;

        // The read position is `writeIdx - delay` and writeIdx is an integer,
        // so the phase the table sees is the fraction of -delay.
        const auto negative = -delay;
        const auto phase = negative - std::floor (negative);

        std::array<double, Sinc::kTaps> kernel {};
        int taps = 0;

        if (usesSinc() && delay >= (double) kSincFloor)
        {
            taps = Sinc::kTaps;

            const auto probePos = (double) kProbeCentre + phase;
            const auto base = kProbeCentre - Sinc::kHalf + 1;

            for (int k = 0; k < taps; ++k)
            {
                std::fill (probe.begin(), probe.end(), 0.0f);
                probe[(size_t) (base + k)] = 1.0f;
                kernel[(size_t) k] = (double) sinc.read (probe.data(), kProbeSize - 1, probePos);
            }
        }
        else
        {
            taps = 4;
            hermiteKernel (phase, kernel.data());
        }

        for (int i = 0; i < (int) kernelMagnitude.size(); ++i)
        {
            const auto step = std::polar (1.0, -gridOmega[(size_t) i]);
            std::complex<double> power { 1.0, 0.0 };
            std::complex<double> acc { 0.0, 0.0 };

            for (int k = 0; k < taps; ++k)
            {
                acc += kernel[(size_t) k] * power;
                power *= step;
            }

            kernelMagnitude[(size_t) i] = std::abs (acc);
        }
    }

    //==========================================================================
    /** Room for the interpolator's taps past the longest delay, so the oldest
        tap can never wrap onto the newest write. */
    static constexpr int kRingGuard = Sinc::kTaps + 4;

    /** The scratch ring the kernel is probed out of. 128 holds a 32-tap kernel
        around the centre without wrapping, and so holds any narrower kernel
        BMO_DWELL_SINC_TAPS can ask for. */
    static constexpr int kProbeSize = 128;
    static_assert (Sinc::kTaps <= 64, "the probe ring has to hold the kernel around kProbeCentre");
    static constexpr int kProbeCentre = 32;

    /** 20 ms, 10 §12's clean crossfade. */
    static constexpr double kCrossfadeSeconds = 0.020;

    /** The wear noise is deterministic on purpose: `11` §4k's block-size
        invariance compares two renders of the same settings sample for sample,
        and a modulation source seeded from the clock would fail it for the
        wrong reason. */
    static constexpr std::uint32_t kNoiseSeed = 22695477u;

    Params params;

    double sampleRate = 48000.0;
    int channels = 2, maxDelay = 0, mask = 0, writeIdx = 0;

    std::array<std::vector<float>, kMaxChannels> ring, gainRing;
    std::array<ChannelFilters, kMaxChannels> filters;

    Sinc sinc;
    std::vector<float> probe;

    Smoother feedback, driveBlend, driveCurve, fxAmount;
    bool primed = false, gainPrimed = false, drivePrimed = false, fxPrimed = false;

    /** **10 §11a's stage, held by value, one per engine.** That it is a member
        here rather than a thing `DspCore` owns is the answer to "which
        instance am I?": the main delay's buffers and the lane's are different
        objects because there are two engines, so no flag has to be passed in
        and none can be. */
    FxStage fx;

    double delayCurrent = 0.0, delayNext = 0.0, delayTarget = 0.0;
    int fadeCounter = -1, fadeLength = 1;

    /** Writes since the last companded one, capped at the ring's length: below
        it the gain ring may still hold a compressor gain the read has to
        divide out (see `process`). */
    int samplesSinceCompanding = 1 << 30;
    double glideAlpha = 1.0;

    double headBumpGain = 1.0, bbdCutoffHz = 0.0;

    double wowPhase = 0.0, flutterPhase = 0.0;
    double noiseA = 0.0, noiseB = 0.0, noiseCoeff = 1.0, noiseNorm = 1.0;
    std::uint32_t noiseState = kNoiseSeed;

    std::vector<double> gridOmega, gridHz, filterMagnitude, kernelMagnitude;
    double referenceLowCut = 0.0, referenceHighCut = 0.0, referenceBlocker = 0.0;
    double kernelDelay = -1.0;
    double loopPeak = 1.0, loopPeakHz = 0.0;
};

} // namespace bmo::dwell
