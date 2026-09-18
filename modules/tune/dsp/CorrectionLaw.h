#pragma once

#include "modules/tune/dsp/Detector.h"
#include "modules/tune/dsp/LatencyContract.h"
#include "modules/tune/dsp/Scale.h"
#include <array>

namespace bmo::tune
{

/** Everything the correction law reads, in real units. */
struct CorrectionSettings
{
    double refA = 440.0;                  ///< concert A, 380-480
    int key = 0;                          ///< 0 = C
    ScaleType scale = ScaleType::chromatic;
    NoteMask allowed = kAllNotes;         ///< per-note allow map, ANDed with the scale

    double retuneMs = 0.0;                ///< one-pole time constant; 0 is a true snap
    double vibratoAmount = 0.0;           ///< beta: 0 flattens vibrato, 1 keeps it, >1 exaggerates
    double flex = 0.0;                    ///< 0..1 soft-knee deadzone; 0 is off
    double hysteresisCents = 8.0;         ///< see nearestAllowed()

    /** At vibrato 0 the note follows the raw pitch, so a singer sitting near
        a boundary between two scale notes flipped between them with every
        wobble -- "hunting", in Frosty's blind test (2026-09-11), who chose
        "hold the note steadier". A switch now needs the new note to be
        noteClearCents closer than the held one (the pitch 30 cents across the
        midpoint: a real step arrives 100-200 cents closer, at once), or to
        stay closer, past the hysteresis, for noteDwellMs. 30 was tried first
        and was too little: on Failure the singer sits on D#, midway between
        D and E, and a +/-15 cent wobble there still flipped every time. */
    double noteClearCents = 60.0;
    double noteDwellMs = 40.0;

    /** What a hold may cost before it is cut, in cents x milliseconds, and the
        pull it carries for free.

        Round three (2026-09-11) heard the flat dwell as pops at retune 20 ms,
        and on Failure every splice it added fell while it held the target off
        the note the singer had reached, pulling about 91 cents. A held note is
        shifted by that pull for as long as it is held, and the engine's read
        drifts at the same rate; far enough, and it splices a whole period
        (see ClassicEngine). So a hold is billed by the pull it cannot afford
        -- whatever exceeds noteHoldFreeCents -- times how long it carries it.

        The allowance is what keeps the thing the dwell was built for. A
        vibrato that only just crosses a boundary sits no more than 60 cents
        from the held note and drifts too slowly to splice, so it is never
        billed and keeps its whole dwell. A singer who has properly moved is
        pulled harder, runs the bill up in a few milliseconds, and switches. */
    double noteHoldFreeCents = 60.0;
    double noteHoldBudgetCentMs = 320.0;

    /** Predicting the pitch forward, which is what stops a correction landing
        late (testing-notes/tune-latency-review-2026-09-11.md).

        The detector's estimate refers to Detector::kAnalysisLagPeriods x T
        behind the newest sample; the engine reads `readDelaySamples` behind
        it. Correct the pitch the engine is about to read rather than the one
        the detector last saw, and the difference -- which is the whole of the
        residue on a moving voice -- goes away. So the distance to predict
        over is (analysis lag - read delay), and where that is zero or
        negative, because the engine already rests at or past the estimate,
        nothing is predicted.

        `readDelaySamples` is the engine's REST, set by TuneCore, and not its
        instantaneous read position. The two differ only while the read has
        wandered off its rest, which happens under a sustained correction,
        which is exactly when the pitch is not moving and the prediction is
        worth nothing anyway.

        predictMaxCents caps it. Vibrato at 45 cents and 6.5 Hz needs about 9
        cents at A2, and a fast scoop about 12; 50 is room for anything real
        and a guard against a slope estimate gone wrong.

        predictSlopeMs is a one-pole on the slope, on top of the median of
        three that the slope is already taken as. **It is 0 -- off -- because
        it only ever cost.** Every smoother delays the slope it reports, and
        that delay comes straight back off the prediction. Swept on the
        reference stimulus (2026-09-11, AURORA); correction lag and residue:

            0 ms   0.970 ms   1.234 c        3 ms   1.052 ms   1.342 c
            1 ms   0.982 ms   1.248 c        5 ms   1.148 ms   1.469 c
            2 ms   1.013 ms   1.289 c

        Monotonic, and by 3 ms it is already past Antares' 1.30 c. On the
        Failure take it buys nothing either: 42 splices against 44 at 2 ms,
        flips and dropouts identical. The median is doing the denoising and
        this was adding lag on top of it. Kept as a knob in case some material
        ever needs it, with the numbers here so nobody turns it up blind. */
    /** How long the target takes to reach a new note, as a one-pole time
        constant. 0 -- the default, and what CLASSIC has always done -- steps
        straight there. Waves has this as a knob of its own, separate from
        Speed, and its minimum is 0.1 ms: it will not do an instantaneous note
        transition at all. BMO does, 341 times in 19 seconds on Failure, each
        one an instant step in the resampling ratio and so an instant step in
        the formants, which is a candidate for the "audible formant shift" and
        the "transition steps on faster words" Frosty has reported.

        Under measurement, not shipped: there is no parameter for it, the
        schema is frozen, and adding one is Frosty's call. */
    double noteTransitionMs = 0.0;

    /// Negative means "ask the contract", which is what TuneCore leaves it at
    /// so the law and the engine cannot disagree. The tests set it to state
    /// the prediction law at chosen distances.
    double readDelaySamples = -1.0;
    double predictMaxCents = 50.0;
    double predictSlopeMs = 0.0;

    double clarityLo = 0.60, clarityHi = 0.85;   ///< confidence ramp (spec §4.4)
    double maxCorrectionCents = 1200.0;          ///< hard clamp (spec §6.1)
};

/** What the law decided on the latest sample; --dump-analysis writes it. */
struct CorrectionState
{
    double pitchIn = 0.0;      ///< semitones, as detected
    double pitchUsed = 0.0;    ///< that, predicted forward to where the engine reads
    double target = 0.0;       ///< semitones: the quantized note
    int note = -1;             ///< the quantized note, -1 for none
    double errorCents = 0.0;   ///< target - input
    double appliedCents = 0.0; ///< a_final: what the engine is told to do
    int noteChanges = 0;       ///< running count, for the flip-flop metric
};

/** Detection in, cents of correction out, one sample at a time (spec §4).

    The chain, in order:

        confirm    a pitch jump of more than 3/4 semitone waits for the next
                   estimate to agree; see confirmPitch() for why this replaces
                   the spec's median on the note
        quantize   nearest allowed note, with hysteresis toward the held note;
                   at vibrato 0, a switch by a small margin must also hold
                   for noteDwellMs (see holdOrSwitch())
        error      e = 100 (target - p_in)
        vibrato    e - beta (e - LP3Hz(e)): correct the slow part of the
                   error, pass beta of the fast part
        flex       e g(|e|), a C1 soft knee; identity when flex is 0
        retune     one-pole toward that, alpha = exp(-1 / (fs tau)); tau = 0
                   bypasses the pole entirely, so the snap is exact
        confidence scaled by clarity between the voicing thresholds
        voicing    ramped in over a period after lock, out over 10 ms

    The vibrato split is written on the error rather than on the pitch as
    the spec writes it. The two are the same thing -- with the target held,
    e - LP(e) is exactly -(p_in - LP(p_in)) -- but on the error a note change
    is a clean step in the target that the slow state can be shifted by, so
    the split survives a legato note change without re-injecting the whole
    interval as "vibrato".
*/
class CorrectionLaw
{
public:
    void prepare (double sampleRate);
    void reset();
    void setSettings (const CorrectionSettings&) noexcept;

    /** One sample. `evaluated` is the detector's evaluatedThisSample(). */
    double tick (const PitchEstimate&, bool evaluated) noexcept;

    const CorrectionState& state() const noexcept { return st; }

    /** The period the law is actually working from, in samples: the one that
        matches the pitch it accepted, not the raw estimate. The engine sizes
        its window from this -- see where it is set. */
    double heldPeriod() const noexcept { return period; }

    /** The flex soft knee on its own, for its test: gain in [0, 1]. */
    static double flexGain (double absCents, double flex) noexcept;

    /** The pitch the note and the correction are both taken from: the
        detector's estimate predicted forward to where the engine reads. Equal
        to the raw estimate when there is nothing to predict over. */
    double pitchUsed() const noexcept { return predicted; }

private:
    bool decideNote (double pitchForDecision, int& note) noexcept;
    int holdOrSwitch (double pitch, int candidate) noexcept;
    double confirmPitch (double latest) noexcept;
    void setNote (int note) noexcept;

    CorrectionSettings s;
    double fs = 48000.0;

    double retuneAlpha = 0.0, slowCoeff = 0.0, confidenceCoeff = 0.0;
    double decideCoeff = 0.0;
    int fadeOutSamples = 480;

    // Detection-side state.
    double pitchIn = 0.0, pitchSlow = 0.0;
    bool havePitch = false, voiced = false;
    double clarity = 0.0;
    double period = 0.0;

    // Prediction: the slope of the estimate, in semitones per sample, the
    // pitch it implies where the engine reads, and the last estimate the
    // slope was taken against.
    double pitchSlope = 0.0, predicted = 0.0;
    // The last three differences, each with the estimate and the sample it
    // was measured at, so the median can be extrapolated from its own anchor.
    struct Slope { double slope = 0.0, pitch = 0.0; long long at = 0; };
    Slope history[3];
    int haveHistory = 0;
    double slopeAnchorPitch = 0.0;
    long long slopeAnchorAt = 0;
    bool haveAnchor = false;
    double slopeFrom = 0.0;
    long long slopeAt = 0;
    bool haveSlopeFrom = false;

    // Target-side state.
    int note = -1;
    bool haveNote = false;
    double pendingJump = 0.0;
    bool havePendingJump = false, jumpTaken = false;
    double target = 0.0, targetGoal = 0.0, transitionAlpha = 0.0;

    // A note switch waiting out noteDwellMs: which note, since which sample,
    // what the hold has cost so far (cents x ms) and when it was last billed.
    int pendingNote = -1;
    long long pendingNoteSince = 0, samples = 0;
    long long dwellSamples = 0, pendingBilledAt = 0;
    double pendingCostCentMs = 0.0;

    // The law's own state.
    double errorSlow = 0.0, applied = 0.0, confidence = 0.0, gate = 0.0;

    CorrectionState st;
};

} // namespace bmo::tune
