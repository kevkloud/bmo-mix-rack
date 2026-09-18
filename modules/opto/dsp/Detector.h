#pragma once

#include "core/dsp/GainComputer.h"

#include <algorithm>
#include <cmath>

namespace bmo::opto
{

// Threshold/ratio/knee for one sample, the slope-not-ratio conversions and
// the soft-knee gain computer all live in core/dsp/GainComputer.h now: they
// are generic compressor arithmetic, and BMO Vcomp needs the same four. They
// were written here first, and the derivation of why a feedback cell cannot
// state what it needs as a ratio -- this module's finding, and the bug that
// was quietly delivering 1.67:1 for 3:1 -- went with them.
//
// Pulled in under these names so the rest of this file reads as it always
// did. Ratio and knee are still fixed per mode; only threshold moves with
// CRUSH, because neither real unit has a ratio control -- CRUSH is a stand-in
// for the Peak Reduction knob, which pushes more signal over a fixed
// circuit's threshold rather than reshaping the circuit itself.
using dsp::Curve;
using dsp::feedforwardSlope;
using dsp::feedbackSlope;
using dsp::kneeReductionDb;

inline float coeffFor (float tauSec, double rate) noexcept
{
    return 1.0f - std::exp (-1.0f / (float) (std::max (rate, 1.0) * (double) tauSec));
}

/** CRUSH, 0-100 on the panel, mapped to threshold only. LA-2A: ~3:1, a soft
    16 dB knee -- both fixed, per the digest's "effectively fixed/soft-knee,
    not a user ratio control."

    The 3:1 goes through feedbackSlope() because La2aCell is a feedback cell
    and this is the figure it must be handed to actually *deliver* 3:1 --
    corrected in 0.2.0, where passing the feedforward figure was quietly
    delivering 1.67:1. */
inline Curve curveForLa2a (float crushPercent) noexcept
{
    const auto c = std::clamp (crushPercent, 0.0f, 100.0f) / 100.0f;
    return { -8.0f + c * -30.0f, feedbackSlope (3.0f), 16.0f };
}

/** Distressor's 10:1 "Opto" ratio setting: fixed 10:1, a harder/shorter 6 dB
    knee -- "reminiscent of 60s/70s gear," a harder catch than a true optical
    unit, per the digest. Same threshold sweep as LA-2A so CRUSH means the
    same thing (how much signal crosses the fixed circuit's threshold) in
    both modes; the character difference is ratio, knee, topology and
    release, not the sweep itself. */
inline Curve curveForDistressor (float crushPercent) noexcept
{
    const auto c = std::clamp (crushPercent, 0.0f, 100.0f) / 100.0f;
    return { -8.0f + c * -30.0f, feedforwardSlope (10.0f), 6.0f };
}

//==============================================================================
/** LA-2A: a feedback cell with a genuine dosage-dependent release.

    Feedback, not feedforward. `process()` returns `input * gainLin` using
    the gain the *previous* sample's envelope produced, then updates the
    envelope from that already-reduced result -- the detector watches the
    output of the cell, not the signal arriving at it, which is what a real
    photo-electric feedback loop does and what makes it self-limiting.

    Two-stage AND dosage-dependent release. The 60 ms-to-50%-recovery stage
    is instantaneous and reductionDb-driven, same as ever. What is new here:
    the slow stage's own time constant is not one fixed number, and not just
    a one-pole low-pass of reductionDb either -- a low-pass alone saturates
    within about a second regardless of how much longer the hit continues,
    so it can't tell a 2-second hit from a 10-second one, which the T4
    cell's CdS photoresistor demonstrably can (its recovery is governed by
    at least two charge-trap populations with different relaxation rates,
    which is exactly why it has "memory" of exposure in the first place).
    `dosageSec` is a second, slower accumulator of *how long* the cell has
    been meaningfully loaded, not just *whether* -- it keeps climbing for as
    long as reductionDb stays engaged, and the slow release tau is a
    continuous, saturating function of it (kReleaseSlowMinTauSec at zero
    dosage, sliding up toward kReleaseSlowMaxTauSec only after real sustained
    exposure). A short transient barely moves it; a long, loud hit does.

    Attack is fixed at ~10 ms, per every source consulted -- there's no
    evidence (here or in the digest) that the real cell's attack is
    level-dependent the way its release is, so this doesn't invent one. */
class La2aCell
{
public:
    void prepare (double sampleRate) noexcept { rate = sampleRate; reset(); }

    void reset() noexcept
    {
        envelopeLin = 0.0f;
        gainLin     = 1.0f;
        reductionDb = 0.0f;
        chargeDb    = 0.0f;
        dosageSec   = 0.0f;
    }

    /** One sample through the cell. Returns the already-reduced sample,
        before makeup gain. */
    float process (float inputSample, const Curve& curve) noexcept
    {
        const auto y = inputSample * gainLin;
        updateFromOutputSample (y, curve);
        return y;
    }

    /** For stereo link: advance the cell's state from an output sample that
        was computed elsewhere (e.g. the louder of two linked channels,
        already scaled by this cell's own currentGainLin()), without also
        re-deriving that sample here. */
    void updateFromOutputSample (float y, const Curve& curve) noexcept
    {
        const auto levelLin = std::abs (y);
        const auto rising   = levelLin > envelopeLin;

        const auto attackCoeff = coeffFor (kAttackTauSec, rate);

        const auto dosageT      = std::clamp (dosageSec / kDosageGrowthSec, 0.0f, 4.0f);
        const auto dosageAmount = 1.0f - std::exp (-dosageT);
        const auto slowTau      = kReleaseSlowMinTauSec
                                     + (kReleaseSlowMaxTauSec - kReleaseSlowMinTauSec) * dosageAmount;

        const auto depth        = std::clamp (chargeDb / 20.0f, 0.0f, 1.0f);
        const auto releaseTau   = kReleaseFastTauSec + (slowTau - kReleaseFastTauSec) * depth;
        const auto releaseCoeff = coeffFor (releaseTau, rate);

        envelopeLin += (rising ? attackCoeff : releaseCoeff) * (levelLin - envelopeLin);

        const auto envelopeDb = 20.0f * std::log10 (std::max (envelopeLin, 1.0e-6f));
        reductionDb = std::clamp (kneeReductionDb (envelopeDb, curve), 0.0f, 40.0f);
        gainLin     = std::pow (10.0f, -reductionDb / 20.0f);

        const auto chargeCoeff = reductionDb > chargeDb ? coeffFor (kChargeAttackTauSec, rate)
                                                         : coeffFor (kReleaseSlowMinTauSec, rate);
        chargeDb += chargeCoeff * (reductionDb - chargeDb);

        // Dosage climbs for as long as the cell is meaningfully engaged and
        // forgets slowly once it isn't -- this is what lets a long hit reach
        // a slower release than a short one at the same peak, continuously
        // rather than as a single fast/slow pick.
        const auto dt = (float) (1.0 / std::max (rate, 1.0));
        const auto engaged = reductionDb > kDosageEngageDb;
        dosageSec += engaged ? dt : -dt * (kDosageGrowthSec / kDosageForgetSec);
        dosageSec = std::clamp (dosageSec, 0.0f, kDosageGrowthSec * 4.0f);
    }

    /** Gain reduction the cell is applying right now, in dB, always >= 0. */
    float currentReductionDb() const noexcept { return reductionDb; }

    /** The gain the cell is applying right now -- read before the next
        sample so a linked channel can apply the same gain this cell decided
        without going through process() twice. */
    float currentGainLin() const noexcept { return gainLin; }

private:
    static constexpr float kAttackTauSec         = 0.010f;  // ~10 ms, fixed -- no source supports it moving
    static constexpr float kReleaseFastTauSec    = 0.06f;   // ~60 ms to the first 50% of recovery
    static constexpr float kReleaseSlowMinTauSec = 1.0f;    // slow tail floor: a hit just past "sustained"

    /** Slow tail ceiling: a long, heavy hit. 15 s until 0.2.1, which measured
        badly against a real one.

        Rendered against a competitor LA-2A on the same vocal, gain-matched,
        the reference recovered *completely* in every phrase gap of 0.3-1.0 s
        -- entering gaps at 1.47 dB and leaving them at -0.07. Ours entered at
        3.78 and left at 1.39, recovering only 63%, and the shortfall grew
        across the take: the first gaps recovered 112-122%, the last five
        32-47%. At a 15 s ceiling a routine 4 dB hit already buys a
        multi-second tail, and `chargeDb` never falls far enough between
        phrases to give it back, so the cell ratchets.

        At 4 s the same render reproduces the reference on all four figures --
        peak 4.26 against 4.15, entering 1.76 against 1.47, leaving 0.00
        against -0.07, recovering 100% against 105%. It is also inside the
        0.5-5 s the sources give for the T4 cell's second stage, which 15 s
        never was.

        The dosage memory this ceiling exists for is untouched: driven hard
        (CRUSH 85) the cell still only gives back 70% across a gap, against
        22% before. It recovers between phrases and holds on when leaned on,
        which is the behaviour the mode was always described as having. */
    static constexpr float kReleaseSlowMaxTauSec = 4.0f;
    static constexpr float kChargeAttackTauSec   = 0.3f;    // how long a hit has to last to "count"
    static constexpr float kDosageEngageDb       = 1.0f;    // reduction below this doesn't accrue dosage
    static constexpr float kDosageGrowthSec      = 3.0f;    // how long sustained drive takes to matter
    static constexpr float kDosageForgetSec      = 4.0f;    // how long the cell takes to forget exposure

    double rate = 44100.0;
    float envelopeLin = 0.0f;
    float gainLin     = 1.0f;
    float reductionDb = 0.0f;
    float chargeDb    = 0.0f;
    float dosageSec   = 0.0f;
};

//==============================================================================
/** Distressor, 10:1 "Opto" ratio: a feedforward cell with electronically-
    timed auto-release -- a different topology from the LA-2A on purpose.
    The Distressor is a VCA-based feedforward compressor; its Opto setting
    switches in dedicated detector/timing circuitry built to *emulate* an
    optical unit's feel, not an actual photoresistor. There's no published
    schematic and no source found describing multi-population charge-trap
    behavior for it the way real CdS cells have -- so unlike La2aCell, this
    doesn't get a continuously-growing dosage state. A single charge-driven
    blend between a fast floor and this mode's own (much longer, ~20 s)
    ceiling is the more honest model: it's the standard way this class of
    analogue auto-release circuit is built (a capacitor charged by gain-
    reduction depth/duration sets the release rate), and there's no evidence
    to justify claiming more precision than that.

    Feedforward: the detector reads the input directly, not the cell's own
    output, matching the real unit's topology. Attack is static at ~10 ms
    and does not lengthen with programme material, per the digest -- unlike
    the LA-2A, this is stated explicitly rather than just unconfirmed. */
class DistressorCell
{
public:
    void prepare (double sampleRate) noexcept { rate = sampleRate; reset(); }

    void reset() noexcept
    {
        envelopeLin = 0.0f;
        gainLin     = 1.0f;
        reductionDb = 0.0f;
        chargeDb    = 0.0f;
    }

    /** One sample through the cell: feedforward, so the detector reads
        `inputSample` directly rather than the cell's own output. */
    float process (float inputSample, const Curve& curve) noexcept
    {
        updateFromInputSample (inputSample, curve);
        return inputSample * gainLin;
    }

    /** For stereo link: advance the cell's state from a detection sample
        computed elsewhere (e.g. the louder of two linked channels). */
    void updateFromInputSample (float x, const Curve& curve) noexcept
    {
        const auto levelLin = std::abs (x);
        const auto rising   = levelLin > envelopeLin;

        const auto attackCoeff = coeffFor (kAttackTauSec, rate);

        const auto depth        = std::clamp (chargeDb / 20.0f, 0.0f, 1.0f);
        const auto releaseTau   = kReleaseFastTauSec + (kReleaseSlowTauSec - kReleaseFastTauSec) * depth;
        const auto releaseCoeff = coeffFor (releaseTau, rate);

        envelopeLin += (rising ? attackCoeff : releaseCoeff) * (levelLin - envelopeLin);

        const auto envelopeDb = 20.0f * std::log10 (std::max (envelopeLin, 1.0e-6f));
        reductionDb = std::clamp (kneeReductionDb (envelopeDb, curve), 0.0f, 40.0f);
        gainLin     = std::pow (10.0f, -reductionDb / 20.0f);

        const auto chargeCoeff = reductionDb > chargeDb ? coeffFor (kChargeAttackTauSec, rate)
                                                         : coeffFor (kChargeReleaseTauSec, rate);
        chargeDb += chargeCoeff * (reductionDb - chargeDb);
    }

    float currentReductionDb() const noexcept { return reductionDb; }
    float currentGainLin() const noexcept { return gainLin; }

private:
    static constexpr float kAttackTauSec       = 0.010f; // ~10 ms, static -- confirmed non-adaptive
    static constexpr float kReleaseFastTauSec  = 0.06f;

    /** This mode's slow ceiling. 20 s until 0.2.1, for the same reason
        La2aCell's was 15 -- and measured just as badly.

        Against a real Distressor on the same vocal, gain-matched: the
        reference peaked at 7.77 dB, entered phrase gaps at 3.60 and left them
        at -0.51, recovering fully every time. Ours recovered **23%**, leaving
        2.21 dB of reduction standing when the next phrase arrived, which is
        what Frosty heard as "it falls slower" -- correctly distinguishing it
        from a tail that lasts too long, which is a different complaint and
        was not this one.

        At 3 s, with kChargeReleaseTauSec below, the same render gives peak
        7.48, entering 3.66, leaving 0.27, recovering 93%. Shorter than
        La2aCell's ceiling and that is not a mistake: the Distressor is a VCA
        feedforward unit whose Opto setting is electronically timed, and it
        measurably recovers faster than the optical unit while reducing more.
        La2aCell's tau also slides with dosage where this one does not, so at
        low exposure the LA-2A is nearer 1 s regardless. */
    static constexpr float kReleaseSlowTauSec  = 3.0f;
    static constexpr float kChargeAttackTauSec = 0.3f;

    /** How fast the cell forgets a hit. Until 0.2.0 this reused
        kReleaseSlowTauSec -- the *ceiling* -- so any hit deep enough to move
        chargeDb pinned release near 20 s for a long time afterwards, which
        is what "Stressed's release feels too long" was. La2aCell has always
        used a separate, much shorter constant (1 s) for the same job; this
        gives Stressed its own, still slower than Tele's.

        4 s in 0.2.0, which was still longer than the phrase gaps it has to
        forget across -- 0.25 to 1.0 s on real material -- so charge only ever
        ratcheted upward through a take and the release it selects never came
        back down. 0.7 s is shorter than the shortest gap, which is the whole
        requirement: the cell must be able to forget a phrase before the next
        one starts, or its memory is of the take rather than of the note.

        Still deliberately a single constant rather than a duration-gated
        accumulator. The measurement says this and the ceiling together are
        enough; the bigger redesign stays unnecessary. */
    static constexpr float kChargeReleaseTauSec = 0.7f;

    double rate = 44100.0;
    float envelopeLin = 0.0f;
    float gainLin     = 1.0f;
    float reductionDb = 0.0f;
    float chargeDb    = 0.0f;
};

//==============================================================================
/** A one-pole DC blocker: needed after La2aDrive's asymmetric term, which
    would otherwise push a DC offset through the rest of the chain. */
class DcBlocker
{
public:
    /** The pole has to be derived from the sample rate, not hard-coded: a
        fixed coefficient is a fixed fraction of the *sample* rate, so its
        corner frequency rises with it. The old hard-coded 0.995 put the
        corner at ~35 Hz at 44.1 kHz but ~76 Hz at 96 kHz and ~153 Hz at
        192 kHz -- and Tele runs this stage unconditionally, so a 96 kHz
        session was quietly high-passing the source at 76 Hz. Fixed in
        0.2.0; kCornerHz is now the same corner at every rate. */
    void prepare (double sampleRate) noexcept
    {
        r = (float) std::exp (-2.0 * 3.14159265358979 * kCornerHz / std::max (sampleRate, 1.0));
        reset();
    }

    void reset() noexcept { x1 = y1 = 0.0f; }

    float process (float x) noexcept
    {
        const auto y = x - x1 + r * y1;
        x1 = x; y1 = y;
        return y;
    }

private:
    static constexpr double kCornerHz = 20.0;

    float r  = 0.9971f;   // kCornerHz at 44.1 kHz, until prepare() says otherwise
    float x1 = 0.0f, y1 = 0.0f;
};

/** LA-2A Drive: the 12AX7/12BH7 makeup stage, 6AQ5 and output transformer's
    mild, mostly low-order warmth -- modelled as a symmetric soft clip (odd
    harmonics, the bulk of any tube stage's output) plus a small asymmetric
    (quadratic) term that adds the low-order *even* harmonics a single-ended
    tube stage is known for, DC-blocked afterward since the asymmetry alone
    would offset the signal. Not level/GR-dependent -- Drive is on or off,
    not a knob, so this is one fixed, tasteful amount.

    `tanh (k * x) / k`, not `tanh (k * x) / tanh (k)`: the latter normalizes
    full-scale input to exactly unity, which sounds reasonable but means the
    *small*-signal gain is `k / tanh (k)` -- always greater than one, so a
    quiet passage comes out louder than it went in before any "warmth" is
    even audible. `/ k` instead gives unity gain at the origin (a quiet
    signal passes essentially untouched) and only compresses as level
    approaches and exceeds where the curve bends -- the shape a passive
    tube/transformer stage actually has, and the one testQuietSignalIsLeftAlone
    and testMakeupGainIsExact both hold this to. */
class La2aDrive
{
public:
    void prepare (double sampleRate) noexcept { dc.prepare (sampleRate); }
    void reset() noexcept { dc.reset(); }

    float process (float x) noexcept
    {
        constexpr float k = 0.6f;
        constexpr float evenAmount = 0.18f;

        // `shaped * shaped` and nothing else. Until 0.2.0 this term was
        // multiplied by sgn(x), which was the whole bug: `shaped` is odd, so
        // `shaped * shaped` is even, and multiplying an even term by an odd
        // one makes it odd again. The stage was odd end to end, and an odd
        // memoryless nonlinearity produces *only odd* harmonics -- so the
        // "even harmonic" term generated no even harmonics at all, and the
        // DC blocker below had no DC to block. Without the sgn the curve is
        // genuinely asymmetric: 2nd harmonic, plus the DC offset that
        // asymmetry implies, which is what dc is actually for.
        const auto shaped = std::tanh (k * x) / k;
        const auto biased = shaped + evenAmount * shaped * shaped;

        return dc.process (biased);
    }

private:
    DcBlocker dc;
};

/** Distressor Drive: the tape-like 3rd-harmonic flattening stage (the
    grittier of its two switchable harmonic options, chosen over the gentler
    Class-A 2nd-harmonic stage as the character this toggle represents) --
    a purely symmetric soft clip, which is what generates odd harmonics
    (3rd, 5th, ...) without needing a separate DC blocker. A larger `k` than
    La2aDrive's on purpose -- see La2aDrive for why `/ k` and not `/ tanh (k)`
    -- so it still passes a quiet signal through near enough unchanged but
    compresses considerably more at the levels it's meant to be heard on,
    reading as grittier and further from the LA-2A's own tone. */
class DistressorDrive
{
public:
    void reset() noexcept {}

    float process (float x) noexcept
    {
        constexpr float k = 1.2f;
        return std::tanh (k * x) / k;
    }
};

} // namespace bmo::opto
