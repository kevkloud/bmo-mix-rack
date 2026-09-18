#pragma once

#include "modules/tune/dsp/DifferenceKernel.h"
#include "modules/tune/dsp/Filters.h"
#include <cstdint>
#include <vector>

namespace bmo::tune
{

/** What the detector knows about the input, as of the newest sample. */
struct PitchEstimate
{
    double period  = 0.0;    ///< fractional samples at the host rate; 0 until the first lock
    double hz      = 0.0;
    double clarity = 0.0;    ///< NSDF peak of the refined estimate, [0, 1]
    double rms     = 0.0;    ///< over the last refinement window
    bool   voiced  = false;  ///< the hysteretic decision, not the raw per-frame test
    bool   onset   = false;  ///< true on the one sample voicing opened
    double candidate = 0.0;  ///< this evaluation's raw period, before voicing and hold; 0 if none
};

/** The pitch detector the two engines share (spec §3).

    Two passes over one difference function:

      1. Coarse. The DC-blocked input is lowpassed and decimated to about
         12 kHz, and a DifferenceKernel keeps E/H for every lag in range at
         O(1) per lag per decimated sample. At each evaluation the NSDF is
         read off it and peak-picked McLeod's way, with the patent's
         threshold as the early exit.
      2. Fine. The coarse lag is refined at the full rate by computing the
         NSDF directly over a one-period window for the few integer lags
         either side of it, then parabolically interpolated. Sub-sample
         accuracy is not optional: at 48 kHz a 440 Hz period rounded to the
         nearest sample is up to 8 cents off.

    Evaluation happens every `hop` samples, where the hop is the larger of
    half a millisecond and a quarter of the tracked period, so the fine
    pass costs about the same per second at every pitch.

    Everything here is per-sample state, so the estimate stream does not
    depend on how the host slices blocks -- that is what the block-size
    invariance harness checks.
*/
class Detector
{
public:
    struct Settings
    {
        double minHz = 80.0;
        double maxHz = 1400.0;

        // Voicing (spec §3.4). Clarity above hi opens, below lo closes, and
        // the band between them is the hysteresis that stops a decaying note
        // chattering.
        double clarityHi = 0.85;
        double clarityLo = 0.60;
        double gateDb    = -55.0;      ///< rms gate; unvoiced below half of it
        double zcrMaxHz  = 6000.0;     ///< zero crossings per second
        double stabilityCents = 30.0;  ///< max candidate movement between hops to open voicing
        double cycleMeanLimit = 0.3;   ///< |mean| / rms a one-period window may carry and still be a period

        // Octave guards (spec §3.5).
        double peakFraction    = 0.88; ///< McLeod's k: first key maximum above k * max
        double subMultipleRatio = 1.15;///< prefer tau/k if its difference is within this factor
        double continuityWeight = 0.10;///< penalty per octave of distance from the held period
        double onsetGraceMs     = 30.0;///< continuity is off this long after an onset
        double multipleRatio    = 0.25;///< take 2 or 3 x the period if its aperiodicity is under this x the period's
        double multipleFloor    = 0.006;///< ...and the period's own aperiodicity is at least this (not noise)
        double heldMultipleRatio = 0.9;///< the ratio when the multiple is the period just held
        double heldMultipleFloor = 0.02;///< ...if the period's own aperiodicity is at least this

        // Guard 6 (spec addendum 2026-09-14). A leap to a SHORTER period is
        // the direction every harmonic error takes, so one of more than
        // leapVetoCents, while a note is held, must first beat the held period
        // on a window long enough to judge them both. A run of vetoes expires
        // after leapVetoHoldMs, so a real leap is delayed by an evaluation or
        // two and the guard can never latch. Costs nothing when the estimate
        // is steady: heldSurvives() runs only on a proposed leap.
        double leapVetoCents  = 100.0;
        double leapVetoHoldMs = 2.0;
    };

    /** The widest range any Settings may ask for; prepare() allocates for it. */
    static constexpr double kCapacityMinHz = 40.0;
    static constexpr double kCapacityMaxHz = 2000.0;

    /** How far behind the newest sample an estimate actually refers to, in
        periods of the note it reports.

        A property of the windows rather than a choice: refine() correlates
        one period against the period before it, so the data an estimate is
        drawn from spans two periods back and its centroid is one period
        back, and the hop -- max(0.5 ms, T/4) -- adds about T/8 of staleness
        on top. That predicts 1.125 x T.

        MEASURED 1.07 x T, at four notes across two octaves: correction lag
        plus in-tune delay came to 1.078, 1.038, 1.076 and 1.071 x T at A2,
        D3, E3 and A3 (testing-notes/latency-and-lag-2026-09-11.md, read as
        tune-latency-review-2026-09-11.md explains). The measured figure is
        the one used; the derivation is a sanity check on it, not its source.

        This is the whole reason a hard-tune correction lands late: the engine
        reads the input somewhere else entirely, and the difference times the
        pitch slope is the residue off the note. CorrectionLaw predicts the
        pitch forward by it. Anything that changes refine()'s window or the
        hop changes this number, and a wrong value shows up directly as
        correction lag in HardTuneTests. */
    static constexpr double kAnalysisLagPeriods = 1.07;

    /** That lag in samples for the period currently held; 0 before a lock. */
    double analysisLagSamples() const noexcept
    {
        return heldPeriod > 1.0 ? kAnalysisLagPeriods * heldPeriod : 0.0;
    }

    /** Allocates everything; nothing afterwards does. */
    void prepare (double sampleRate, const Settings&);

    /** Real-time safe. A change of range re-targets the kernel and drops
        the held estimate; anything else just takes effect. */
    void setSettings (const Settings&) noexcept;

    void reset();

    /** Push one sample; the estimate is updated in place. */
    void push (float x) noexcept;

    const PitchEstimate& estimate() const noexcept { return current; }

    /** True when the last push ran an evaluation -- for --dump-analysis. */
    bool evaluatedThisSample() const noexcept { return evaluated; }

    /** The pitch range in force, for anyone sizing buffers off it. */
    double getMinHz() const noexcept { return settings.minHz; }
    double getMaxHz() const noexcept { return settings.maxHz; }
    int maxPeriodSamples() const noexcept { return maxFullLag; }

    int getDecimation() const noexcept { return decimation; }

    /** Direct access for the unit tests. */
    const DifferenceKernel& coarseKernel() const noexcept { return kernel; }

    /** Parabolic vertex through three equally spaced points, as an offset
        from the middle one, clamped to [-1, 1]; 0 when the three are
        collinear, so a flat run never divides by zero (spec §3.2, T-4). */
    static double parabolicOffset (double left, double centre, double right) noexcept;

private:
    void evaluate() noexcept;
    bool coarseSearch (double& coarseLag) noexcept;
    bool refine (double coarseLagFull, double& period, double& clarity) noexcept;
    void preferWholeCycle (double& period, double& clarity) noexcept;
    bool multipleOf (double base, int& factor, int& lag) const noexcept;
    double lowPeak (double centre, int reach, int window, int& atLag) const noexcept;
    double fullNsdf (int lag, int window) const noexcept;
    double lowNsdf (int lag, int window) const noexcept;
    bool spansWholeCycles (int lag) const noexcept;
    bool heldSurvives (double candidate) const noexcept;
    void updateVoicing (bool frameVoiced, bool frameUnvoiced) noexcept;

    float fullAt (int d) const noexcept { return fullRing[(size_t) ((fullWrite - 1 - d) & fullMask)]; }
    float lowAt (int d) const noexcept  { return lowRing[(size_t) ((fullWrite - 1 - d) & fullMask)]; }
    static constexpr int kRebaseInterval = 1 << 20;
    double cumSumAt (int d) const noexcept { return cumSum[(size_t) ((fullWrite - 1 - d) & fullMask)]; }
    double cumSqAt (int d) const noexcept  { return cumSq [(size_t) ((fullWrite - 1 - d) & fullMask)]; }

    Settings settings;
    bool configured = false;
    double sampleRate = 48000.0;
    int decimation = 4;
    double coarseRate = 12000.0;

    Biquad highpass;
    ButterworthLowpass<3> antiAlias;
    DifferenceKernel kernel;
    int decimationPhase = 0;

    std::vector<float> fullRing;   // highpassed, full rate
    std::vector<float> lowRing;    // the same through the anti-alias lowpass, full rate: guard 4 reads it
    std::vector<double> cumSum, cumSq;   // running sums of it, for window means in O(1)
    double runningSum = 0.0, runningSq = 0.0;
    int rebaseCountdown = 0;
    int fullMask = 0, fullWrite = 0;
    int minFullLag = 2, maxFullLag = 600;

    std::vector<double> coarseNsdf;   // scratch, sized at prepare

    // Zero-crossing and energy trackers, one-pole, over ~10 ms.
    double zcrState = 0.0, zcrCoeff = 0.0, energyState = 0.0;
    float previousSign = 0.0f;

    int baseHop = 24, hopCountdown = 24, lastHop = 24;
    double candidatePeriod = 0.0;
    std::int64_t samplesSeen = 0, lastOnsetAt = -1'000'000;
    std::int64_t leapHoldFrom = -1;   // guard 6: when the current veto run began, or -1

    // Hysteresis counters, in samples.
    int voicedRun = 0, unvoicedRun = 0;

    double heldPeriod = 0.0;

    // Guard 4's standing decision: the period is guardFactor x what the
    // search finds, decided for guardPeriod, due again at guardDueAt.
    int guardFactor = 1, guardLag = 0;   // guardLag: where the multiple was found, for guardPeriod
    int guardPending = 0;                // a multiple found once, waiting for a second run to agree
    double guardPeriod = 0.0;
    std::int64_t guardDueAt = 0;

    PitchEstimate current;
    bool evaluated = false;
};

} // namespace bmo::tune
