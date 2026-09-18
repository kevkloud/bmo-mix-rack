#pragma once

#include "core/dsp/GainComputer.h"
#include "modules/vcomp/dsp/Detector.h"

#include <algorithm>
#include <cmath>

namespace bmo::vcomp
{

//==============================================================================
// The limiter, last in the chain.
//
// **Why it exists.** The automatic makeup is what makes AMOUNT work and it is
// also what makes the module clip: at the top of the knob it is adding nearly
// 29 dB, and anything the compressor did not catch arrives at the output with
// all of that on top. measure_vcomp's preset report had "In Front" and "Keep
// The Chest" peaking above 0 dBFS on a source whose RMS was -18. RVox is gate
// -> compressor -> limiter for exactly this reason; this is the third stage.
//
// **Zero latency, and therefore not a brickwall in the modern sense.** A true
// brickwall limiter looks ahead so it can begin reducing before the transient
// arrives, and that costs latency -- which is the one property this module is
// built around (see VcompDsp::latencyForParams). So the attack here is
// instantaneous instead: the gain for a sample is computed from that same
// sample, which guarantees the ceiling without a single sample of delay and
// pays for it in distortion on the fastest transients rather than in overshoot.
// That is the same trade RVox makes, and it is why a limiter on a tracking
// vocal is allowed to exist at all.
//
// **The knee only ever pulls down early.** With a soft knee centred on the
// ceiling, the reduction inside the knee is a parabola that reaches exactly
// `level - ceiling` at the top of the knee and is *larger* than it needs to be
// nowhere. Output is therefore at or below the ceiling everywhere:
//
//     out(d) = ceiling + d - (d + K/2)^2 / 2K      for d in [-K/2, +K/2]
//
// which has its maximum at d = K/2, where it equals the ceiling exactly. The
// knee costs up to K/8 dB of early reduction at the ceiling itself and buys a
// gentle onset. It cannot cause an over, which is the property that lets this
// be called a limiter rather than a fast compressor.
//
// **The knee is narrow, and that is the wire promise talking.** A knee K wide
// and centred on the ceiling starts pulling down K/2 below it, so at 3 dB the
// limiter was touching everything above -1.6 dBFS -- including at AMOUNT 0,
// where the module is supposed to be a wire. testAmountZeroIsInert caught it
// on a -1 dBFS tone. At 1 dB the knee starts at -0.6 dBFS, so the claim now
// reads "a wire for anything that was not already at the edge of full scale",
// which is the honest version: a safety limiter that refused to act near 0
// dBFS would not be one.
//
// **No parameters.** RVox's limiter has no controls and neither does this: it
// is a safety net on a gain stage the user did not ask for, not an effect. The
// ceiling sits a hair under full scale rather than at it, so the module is
// inert for anything that was not going to clip anyway -- a signal at -1 dBFS
// comes out at -1 dBFS, untouched.
//==============================================================================

inline constexpr float kLimiterCeilingDb = -0.1f;
inline constexpr float kLimiterKneeDb    =  1.0f;
inline constexpr float kLimiterReleaseMs = 60.0f;

/** The reduction needed to bring `levelDb` to the ceiling, in dB, >= 0.

    Slope 1.0 is the whole of "limiter": every dB over the ceiling is removed,
    which is a ratio of infinity expressed the way core/dsp/GainComputer.h
    wants it. The same knee arithmetic the compressor uses, with the threshold
    at the ceiling. */
inline float limiterReductionDb (float levelDb) noexcept
{
    return dsp::kneeReductionDb (levelDb, { kLimiterCeilingDb, 1.0f, kLimiterKneeDb });
}

//==============================================================================
class Limiter
{
public:
    void prepare (double rate) noexcept
    {
        releasePole = poleFor (kLimiterReleaseMs, rate);
        reset();
    }

    void reset() noexcept { reductionDb = 0.0f; }

    /** The gain to apply to this sample, from the peak the sample would have
        had without it. Both channels are handed the same figure -- a limiter
        that acted per channel would swing the image exactly when the signal is
        loudest. */
    float process (float peakLin) noexcept
    {
        const auto demandDb = limiterReductionDb (levelDbOf (peakLin));

        // Instantaneous attack. Not a time constant that happens to be short:
        // the reduction has to be in place on the sample that asked for it or
        // the ceiling is not a ceiling.
        if (demandDb > reductionDb)
            reductionDb = demandDb;
        else
            reductionDb = releasePole * reductionDb + (1.0f - releasePole) * demandDb;

        if (reductionDb < kEnvelopeFloorDb)
            reductionDb = 0.0f;

        return reductionDb > 0.0f ? std::pow (10.0f, -reductionDb / 20.0f) : 1.0f;
    }

    /** How much the limiter is holding back, in dB, always >= 0. Not metered
        today -- the GR bar is the compressor's and folding this into it would
        say the compressor was working when it was not. */
    float currentReductionDb() const noexcept { return reductionDb; }

private:
    float releasePole = 0.0f;
    float reductionDb = 0.0f;
};

} // namespace bmo::vcomp
