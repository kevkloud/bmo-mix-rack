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
    back out of the control ring at the same fractional position, which is the
    whole point of the construction (10 §4, and a stability requirement rather
    than a refinement). */
inline double companderGainFor (double levelDb) noexcept
{
    // The floor bounds the boost a near-silent line asks for: at -60 dBFS the
    // compressor is already at +30 dB and nothing quieter needs more. Both
    // ends are CALIBRATE, with the 5/50 ms ballistics.
    const auto clamped = std::clamp (levelDb, -60.0, 6.0);
    return std::pow (10.0, -0.5 * clamped / 20.0);
}

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

    The FX stage, the ducker, the stereo matrix and the lane's gates are 2c.

    Latency is 0 and stays 0: no oversampling, no lookahead, and the wet delay
    time is not latency (10 §0, `00` §4).

    `input` and `output` must not alias -- the engine reads its input after it
    has written the sample's output. */
class DelayEngine
{
public:
    using Sinc = bmo::tune::SincTable<32>;

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
        float lowCutHz    = 20.0f;      ///< in-loop HP, 20 Hz - 1 kHz
        float highCutHz   = 20000.0f;   ///< in-loop LP, capped at min(18k, 0.45 fs)
        float modRateHz   = 0.6f;       ///< wow, 0.1 - 8 Hz
        float modDepthPct = 0.0f;       ///< 0 - 100 %
        float drivePct    = 0.0f;       ///< into the ADAA residual shaper
    };

    //==========================================================================
    /** Allocates both rings and every grid the sweep reads, from `maxTimeMs`
        and **never from a parameter** (10 §10). At 192 kHz the audio ring is
        524 288 samples a channel -- 2.0 MB -- and 0.5 MB at 44.1 kHz.

        **The control ring beside it is the same size again.** 10 §4 requires
        the compressor's gain to travel with the audio so the expander can
        apply its exact reciprocal at the same fractional position, and a
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

        for (auto& f : filters)
            f.compressor.prepare (sampleRate, kCompanderAttackMs, kCompanderReleaseMs);

        prepareModulationNoise();

        primed = false;
        gainPrimed = false;
        drivePrimed = false;

        buildUserFilters();
        buildModeFilters();
        applyDrive (true);
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

        writeIdx = 0;
        fadeCounter = -1;
        delayCurrent = delayNext = delayTarget;

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
        applyTime (p.timeMs, modeMoved);
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

    //==========================================================================
    /** One block. Allocates nothing: every buffer and every grid came from
        `prepare`.

        The loop is 10 §3's `v[n] = x[n] + g . C(y[n])` with **no input gate**
        -- the `s` term is gone, not repurposed (§11, §11.1) -- and the engine's
        output is the raw read `y`, tapped before the character chain, so
        colour accumulates one pass per lap.

        On bucket-brigade the compander straddles the ring rather than sitting
        inside `C(.)`: the compressor's gain multiplies what is written and is
        written **beside** it, and the expander divides the read by the same
        gain read at the same fractional position. That pair is unity at every
        instant, transient included, which is why `P_bbd` comes from the
        filters alone (10 §4). There is no second detector, so there is nothing
        to overshoot. */
    void process (const float* const* input, float* const* output, int numChannels, int numSamples) noexcept
    {
        if (mask <= 0 || numSamples <= 0 || numChannels <= 0)
            return;

        const auto nch = std::min (numChannels, channels);
        const auto glide = usesGlide();
        const auto compand = usesCompander();

        for (int n = 0; n < numSamples; ++n)
        {
            const auto gain = (double) feedback.tick();
            const auto blend = (double) driveBlend.tick();
            const auto curve = (double) driveCurve.tick();

            advanceTime (glide);

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

            const auto readCurrent = readPosition (delayCurrent, modulated);
            const auto readNext    = fading ? readPosition (delayNext, modulated) : readCurrent;

            for (int ch = 0; ch < nch; ++ch)
            {
                const auto* line = ring[(size_t) ch].data();

                auto y = readAt (line, readCurrent);

                if (fading)
                    y = fadeOld * y + fadeNew * readAt (line, readNext);

                if (compand)
                {
                    // **The exact reciprocal at the same fractional position**
                    // (10 §4). The same kernel reads both rings, so at a whole
                    // sample the pair is unity to the bit, and away from one it
                    // is unity to the extent the gain is constant across four
                    // taps -- which at 5/50 ms it is.
                    const auto* gains = gainRing[(size_t) ch].data();
                    auto g = readAt (gains, readCurrent);

                    if (fading)
                        g = fadeOld * g + fadeNew * readAt (gains, readNext);

                    y /= std::max (g, 1.0e-6);
                }

                output[ch][n] = (float) y;

                auto& f = filters[(size_t) ch];
                const auto c = character (f, y, blend, curve);

                const auto x = (double) input[ch][n];
                auto v = (std::isfinite (x) ? x : 0.0) + gain * c;

                // A NaN that reached the ring would circulate for ever, so it
                // is stopped at the write rather than at the output.
                if (! std::isfinite (v))
                    v = 0.0;

                auto gc = 1.0;

                if (compand)
                {
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
    double character (ChannelFilters& f, double y, double blend, double curve) noexcept
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

        // 10 §1: 32-tap polyphase Kaiser sinc on clean -- unity gain at every
        // phase and an exact delay at whole samples, so the repeat chain
        // accumulates no phase-dependent HF loss. Tape and bucket-brigade take
        // 4-point 3rd-order Hermite instead, where the per-repeat HF loss is
        // *wanted*; so does clean below the sinc's headroom.
        if (usesSinc() && delay >= (double) kSincFloor)
            return (double) sinc.read (line, mask, pos);

        return hermite (line, pos);
    }

    double hermite (const float* line, double pos) const noexcept
    {
        const auto whole = std::floor (pos);
        const auto f = pos - whole;
        const auto i = (int) (long long) whole;

        const auto at = [line, this] (int k) noexcept
        {
            return (double) line[(size_t) (k & mask)];
        };

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

    /** The scratch ring the kernel is probed out of. 128 holds all 32 taps
        around the centre without wrapping. */
    static constexpr int kProbeSize = 128;
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

    Smoother feedback, driveBlend, driveCurve;
    bool primed = false, gainPrimed = false, drivePrimed = false;

    double delayCurrent = 0.0, delayNext = 0.0, delayTarget = 0.0;
    int fadeCounter = -1, fadeLength = 1;
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
