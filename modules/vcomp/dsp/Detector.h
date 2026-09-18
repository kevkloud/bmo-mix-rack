#pragma once

#include "core/dsp/GainComputer.h"
#include "modules/vcomp/dsp/Crossover.h"
#include "modules/vcomp/params.h"

#include <algorithm>
#include <cmath>

namespace bmo::vcomp
{

using dsp::Curve;
using dsp::feedforwardSlope;
using dsp::kneeReductionDb;

//==============================================================================
// AMOUNT, and what it actually moves.
//
// One knob, three sweeps, moved together. A real single-knob vocal compressor
// is not a threshold knob with the other controls hidden -- it is a threshold
// knob whose *circuit* gets more aggressive as it goes down, which is why the
// top of the knob sounds like a different compressor from the bottom rather
// than like the same one working harder.
//
//   threshold  -6 dBFS -> -40 dBFS   how much signal is over the line
//   knee       12 dB   ->  6 dB      a soft catch that hardens as it is driven
//   ratio      1:1     ->  8:1       a wire -> gentle levelling -> dense
//
// **The ratio sweep starts at 1:1, and that is what makes AMOUNT 0 inert.**
// A 1:1 ratio is a slope of zero, so the curve returns no reduction at any
// level at all -- the module is a wire at the bottom of the knob, not merely
// quiet. That is the house rule (a freshly inserted instance does nothing
// until the ear asks it to) expressed in the curve itself rather than in a
// special case, and VcompDspTests pins it.
//
// It was first built with the threshold doing that job instead: ratio 2:1 at
// the bottom and the knee's lower edge parked at 0 dBFS, so nothing below full
// scale was touched. That is inert too, and it cost the bottom third of the
// knob -- at AMOUNT 20 the knee had only reached -8.6 dBFS, so a vocal tracked
// at a sensible -10 dBFS got *nothing* until the knob was a quarter round.
// Spending the ratio on being inert instead frees the threshold to start at
// -6 dBFS, where voices actually are, and every degree of the knob does
// something. The test that caught it is testAmountGrabsHarder.
//
// Feedforward, not feedback. BMO Opto's Tele mode is a feedback cell because
// the hardware it models is one, and a feedback cell's delivered ratio wanders
// with programme level, which is most of its charm. This module is the modern
// one: what the curve says is what it does, at every level.
//==============================================================================

inline constexpr float kThresholdTopDb    =  -6.0f;
inline constexpr float kThresholdBottomDb = -40.0f;
inline constexpr float kKneeWideDb        =  12.0f;
inline constexpr float kKneeTightDb       =   6.0f;
inline constexpr float kRatioMin          =   1.0f;
inline constexpr float kRatioMax          =   8.0f;

inline Curve curveFor (float amountPercent) noexcept
{
    const auto a = std::clamp (amountPercent, 0.0f, 100.0f) / 100.0f;

    return { kThresholdTopDb + a * (kThresholdBottomDb - kThresholdTopDb),
             feedforwardSlope (kRatioMin + a * (kRatioMax - kRatioMin)),
             kKneeWideDb + a * (kKneeTightDb - kKneeWideDb) };
}

//==============================================================================
// The makeup AMOUNT pays for itself with.
//
// The reduction this curve applies to a voice sitting at kReferenceDb, added
// straight back. That is the whole trick behind a one-knob vocal compressor:
// the knob buys density, not level, so the ear can judge how much compression
// it wants without also judging how much louder it got, and an A/B against
// bypass stays honest.
//
// The compensation is static -- it depends on AMOUNT and never on the
// programme -- and therefore cannot fight the compression. Compensating the
// *instantaneous* reduction would cancel it exactly and leave a wire. That
// trap is recorded in modules/opto/params.h too, where auto-makeup was
// rejected outright for being untrue to the hardware; this module models a
// unit that has one.
//
// **The reference is a peak figure, not an RMS one**, because the detector
// below is a peak detector: it has to be the level the detector will actually
// be looking at, which on a vocal is 12-15 dB above where the meter sits. -7
// dBFS is a vocal tracked to conventional gain staging.
//
// Getting this wrong is quiet and systematic rather than obviously broken, so
// it is worth saying how it was caught. It was first set to -10 dBFS by
// reading "where a vocal sits" off an RMS meter, and every factory preset then
// came out *below* the level it went in -- worst at the bottom of the knob,
// where the makeup is smallest but the peaks are reduced just as hard. The
// suite's own test source (voice(), tests/plugin/TestUtil.h) is normalised to
// -18 dBFS RMS and peaks at -3.6, a 14.4 dB crest, so a -10 reference was
// compensating for a signal 6 dB quieter than the one the detector was
// hearing. At -7 the six factory presets land inside 1.8 dB, and
// VcompTests' level-matching check is the thing that holds it there.
//
// A quieter source still gets less than full compensation and a hotter one
// more, which is correct -- it is what the curve is genuinely doing to them --
// but the reference now sits where vocals are rather than 6 dB under.
//==============================================================================

inline constexpr float kReferenceDb = -7.0f;

inline float autoMakeupDb (const Curve& curve) noexcept
{
    return kneeReductionDb (kReferenceDb, curve);
}

//==============================================================================
/** exp(-1/(tau*fs)) -- the fraction of a one-pole's state that *survives* a
    sample, not the fraction of the step it takes. Everything below is written
    in terms of the survivor, because the published form of the decoupled
    detector is, and mixing the two conventions up is a bug that sounds like a
    working compressor with the wrong numbers on its knobs. */
inline float poleFor (float tauMs, double rate) noexcept
{
    const auto tauSec = std::max (tauMs, 0.01f) * 0.001f;
    return std::exp (-1.0f / (float) (std::max (rate, 1.0) * (double) tauSec));
}

/** Below this many dB of reduction the envelope is simply zero. Stops a
    one-pole decaying toward 0 from spending the rest of the session in
    denormals, and 1e-6 dB is not a gain change anything can hear. */
inline constexpr float kEnvelopeFloorDb = 1.0e-6f;

//==============================================================================
/** The release half of the detector, and where ARC lives.

    **ARC off** is one branch: the release stage of a smooth decoupled peak
    detector (Giannoulis, Massberg and Reiss, "Digital Dynamic Range Compressor
    Design -- A Tutorial and Analysis", JAES 60(6), 2012 -- the smooth
    decoupled form). The max() against the demand makes the attack instant
    *here*, so the attack smoothing downstream is the only thing shaping it.
    That is what stops the attack time drifting with how far over threshold the
    signal is, which is the well-known fault of the naive branching detector
    and a large part of why a naive compressor sounds dated.

    **ARC on** is two branches, and it is worth saying why it is not simply
    "a slower release", because the obvious implementation does nothing at
    all: two release branches with different times combined with max() is just
    the slower of the two, at every sample, for any input. A slower one-pole
    fed the same signal is never below a faster one. There is no programme
    dependence in it.

    What makes the slow branch programme-dependent is its **attack**. It
    charges over kArcChargeScale x RELEASE, so a consonant or one loud word
    barely moves it and it has nothing to release slowly; a sustained loud
    phrase charges it most of the way, and then it is the branch that decides
    the recovery. Fast branch for fast material, slow branch only once the
    material has earned it. That is a model of the two-time-constant networks
    in the bus compressors this behaviour is named after, and it is why ARC
    needs no control of its own.

    RELEASE still means something with ARC on: it scales all three times
    rather than being ignored, so the knob moves the whole behaviour up and
    down instead of switching off. */
inline constexpr float kArcFastScale   = 0.35f;  //   70 ms at the default 200
inline constexpr float kArcChargeScale = 1.2f;   //  240 ms
inline constexpr float kArcSlowScale   = 10.0f;  // 2000 ms

class ReleaseStage
{
public:
    void setTimes (float releaseMs, bool arcOn, double rate) noexcept
    {
        arc = arcOn;

        if (arc)
        {
            fastPole   = poleFor (releaseMs * kArcFastScale,   rate);
            chargePole = poleFor (releaseMs * kArcChargeScale, rate);
            slowPole   = poleFor (releaseMs * kArcSlowScale,   rate);
        }
        else
        {
            fastPole = poleFor (releaseMs, rate);
        }
    }

    void reset() noexcept { fast = slow = 0.0f; }

    /** `demandDb` is the reduction the static curve is asking for, always
        >= 0. Returns the reduction the release stage will allow. */
    float tick (float demandDb) noexcept
    {
        fast = std::max (demandDb, fastPole * fast + (1.0f - fastPole) * demandDb);

        if (fast < kEnvelopeFloorDb)
            fast = 0.0f;

        if (! arc)
            return fast;

        const auto pole = demandDb > slow ? chargePole : slowPole;
        slow = pole * slow + (1.0f - pole) * demandDb;

        if (slow < kEnvelopeFloorDb)
            slow = 0.0f;

        return std::max (fast, slow);
    }

private:
    bool  arc = kStandardArc;
    float fastPole = 0.0f, chargePole = 0.0f, slowPole = 0.0f;
    float fast = 0.0f, slow = 0.0f;
};

//==============================================================================
/** Detector high-pass. On the detector's copy of the signal only -- the audio
    itself is never filtered, so this changes what the compressor *listens to*
    and nothing else.

    It matters more on a voice than on anything else: plosives and a singer's
    proximity effect are the loudest thing in a vocal track by peak level and
    the least interesting by content, and a detector that counts them ducks
    the whole phrase every time the singer says a P. 90 Hz standard.

    A topology-preserving-transform state variable filter (Zavalishin, The Art
    of VA Filter Design), at Butterworth Q. TPT rather than a biquad because
    its coefficients stay well behaved when the cutoff moves while it runs,
    which is exactly what a user dragging the SIDECHAIN knob does. */
class SidechainHighpass
{
public:
    void prepare (double rate) noexcept
    {
        sampleRate = std::max (rate, 1.0);
        reset();
    }

    void reset() noexcept { s1 = s2 = 0.0f; }

    void setCutoff (float hz) noexcept
    {
        const auto nyquist = (float) (sampleRate * 0.5);
        const auto fc = std::clamp (hz, 10.0f, nyquist * kMaxCutoffOfNyquist);
        const auto g  = std::tan (3.14159265358979323846f * fc / (float) sampleRate);

        a1 = 1.0f / (1.0f + g * (g + kK));
        a2 = g * a1;
        a3 = g * a2;
    }

    float process (float x) noexcept
    {
        const auto v3 = x - s2;
        const auto v1 = a1 * s1 + a2 * v3;
        const auto v2 = s2 + a2 * s1 + a3 * v3;

        s1 = 2.0f * v1 - s1;
        s2 = 2.0f * v2 - s2;

        return x - kK * v1 - v2;
    }

private:
    static constexpr float kK = 1.41421356237f;  // 1/Q, Butterworth

    double sampleRate = 44100.0;
    float  a1 = 0.0f, a2 = 0.0f, a3 = 0.0f;
    float  s1 = 0.0f, s2 = 0.0f;
};

//==============================================================================
/** Level in dBFS from one sample, floored so log10 never sees zero. Peak, not
    RMS: it is what gives a vocal compressor its grip on consonants, and the
    ripple it leaves under sustained low notes is what the sidechain high-pass
    and the attack smoothing are there to deal with. */
inline float levelDbOf (float sample) noexcept
{
    return 20.0f * std::log10 (std::max (std::abs (sample), 1.0e-7f));
}

} // namespace bmo::vcomp
