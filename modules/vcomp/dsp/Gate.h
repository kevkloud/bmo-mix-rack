#pragma once

#include "modules/vcomp/dsp/Detector.h"

#include <algorithm>
#include <cmath>

namespace bmo::vcomp
{

//==============================================================================
// The gate, which is a downward expander, ahead of the compressor.
//
// **Why it exists at all.** The auto makeup is the whole trick behind AMOUNT,
// and it is indiscriminate: at AMOUNT 80 the module adds about 26 dB, to the
// voice and to whatever else was in the track. Room tone, headphone bleed, mic
// self-noise and the singer breathing between lines all come up by the same
// 26 dB, so a heavily compressed vocal sounds like the room got louder every
// time the singer stops. Cleaning that up is not a separate feature bolted on;
// it is the other half of making a one-knob compressor usable. RVox ships the
// same pairing for the same reason.
//
// **Ahead of the compressor, and keyed off the raw input.** Ahead, because
// what it closes has to be closed before the makeup amplifies it. Keyed off
// the input rather than off anything downstream, because then the threshold is
// an absolute level the user can set against the track -- it does not move
// when AMOUNT does, which a threshold read off the compressed signal would.
//
// **An expander, not a gate**, whatever the knob is called. Below threshold
// the gain falls on a slope with a soft knee and a floor on how far it can
// fall; it does not slam shut. A hard gate chatters on breaths and bites the
// tails off words, and every one of those faults is audible on a vocal in a
// way it is not on a tom. Nothing here is on the panel: RVox has one control
// for this and so does BMO Vcomp.
//
// **Fast to open, slow to close, with a hold.** Opening is the direction that
// costs you a syllable if it is wrong, so it is 3 ms; closing is the direction
// that chatters if it is wrong, so it is ~150 ms with a 40 ms hold after the
// last time the signal was over threshold. The hold is what keeps it from
// starting to close inside a word.
//
// **3 ms and not 0.5, because a gate that opens too fast clicks.** Frosty heard
// it pop on the way in, 2026-09-14. The open time had not changed -- the depth
// it opens *from* had: at 3:1 into a 50 dB floor the gate was only ever a
// little way shut, and at 6:1 into 60 it sits nearly 58 dB down between words.
// Same ramp, much longer drop, and the step in the gain envelope becomes a
// click. The number that predicts it is the slew, and 0.5 ms was moving the
// gain at 113 dB per millisecond (measure_vcomp gateopen).
//
// 3 ms brings that to 19 dB/ms and costs nothing at the front of a word: the
// onset column of measure_vcomp's gate report reads 0.00 dB at every threshold
// a voice would use and -0.33 dB at the most extreme. Slower still is
// available -- 5 ms halves the slew again -- but it takes 20 ms to open, which
// is inside the length of a consonant and is where syllables start to go.
//==============================================================================

inline constexpr float kGateRatio      = 6.0f;    ///< downward expansion below threshold
inline constexpr float kGateKneeDb     = 6.0f;
inline constexpr float kGateRangeDb    = 60.0f;   ///< the most it will ever shut
inline constexpr float kGateOpenMs     = 3.0f;
inline constexpr float kGateCloseMs    = 150.0f;
inline constexpr float kGateHoldMs     = 40.0f;

/** The attenuation a downward expander applies to `levelDb`, in dB, >= 0.

    For a ratio R below threshold T, a signal at L < T is pushed down to
    T - (T - L)R, so the attenuation is (T - L)(R - 1). The knee is the same
    parabola the compressor's gain computer uses, mirrored: it softens the
    corner over kGateKneeDb either side of the threshold so the expander eases
    in instead of catching. Clamped at kGateRangeDb -- an expander with no
    floor keeps going until the noise it was cleaning up is replaced by a hole,
    which is louder. */
inline float gateAttenuationDb (float levelDb, float thresholdDb) noexcept
{
    const auto under = thresholdDb - levelDb;
    const auto half  = kGateKneeDb * 0.5f;
    const auto slope = kGateRatio - 1.0f;

    if (under <= -half)
        return 0.0f;

    const auto raw = under < half ? slope * ((under + half) * (under + half)) / (2.0f * kGateKneeDb)
                                  : slope * under;

    return std::min (raw, kGateRangeDb);
}

//==============================================================================
class Gate
{
public:
    void prepare (double rate) noexcept
    {
        openPole  = poleFor (kGateOpenMs,  rate);
        closePole = poleFor (kGateCloseMs, rate);
        holdSamples = (int) (kGateHoldMs * 0.001f * (float) std::max (rate, 1.0));
        reset();
    }

    void reset() noexcept
    {
        attenuationDb = 0.0f;
        held = 0;
    }

    /** `thresholdDb` is the GATE parameter. At kGateOffDb the gate is exactly
        inert -- checked here rather than left to the arithmetic, because
        "inert" is a promise the module makes and a knee that reaches a
        fraction of a dB below the rail would quietly break it. */
    void setThreshold (float thresholdDb) noexcept
    {
        active = thresholdDb > kGateOffDb;
        threshold = thresholdDb;
    }

    /** Returns the linear gain to apply to this sample. `detectDb` is the
        level of the raw input, both channels linked. */
    float process (float detectDb) noexcept
    {
        if (! active)
        {
            attenuationDb = 0.0f;
            return 1.0f;
        }

        const auto target = gateAttenuationDb (detectDb, threshold);

        // Hold: while the signal has recently been over threshold, the gate is
        // not allowed to start closing. It may still open.
        if (target <= 0.0f)
            held = holdSamples;
        else if (held > 0)
            --held;

        const auto closing = target > attenuationDb;

        if (closing && held > 0)
            return std::pow (10.0f, -attenuationDb / 20.0f);

        const auto pole = closing ? closePole : openPole;
        attenuationDb = pole * attenuationDb + (1.0f - pole) * target;

        if (attenuationDb < kEnvelopeFloorDb)
            attenuationDb = 0.0f;

        return std::pow (10.0f, -attenuationDb / 20.0f);
    }

    float currentAttenuationDb() const noexcept { return attenuationDb; }

private:
    bool  active = false;
    float threshold = kGateOffDb;
    float openPole = 0.0f, closePole = 0.0f;
    float attenuationDb = 0.0f;
    int   holdSamples = 0, held = 0;
};

} // namespace bmo::vcomp
