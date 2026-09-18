#pragma once

#include <algorithm>
#include <cmath>

namespace bmo::deq
{

/** Time constants use the tau convention: `attackMs` is the time a one-pole
    takes to cover 63.2 % of a step. BMO Opto's detector already does
    (`coeffFor` in modules/opto/dsp/Detector.h), and two dynamics modules in
    one suite reading the same knob differently would be worse than either
    convention. The 10-90 % rise is 2.2 tau; quote that in a tooltip if a
    number is wanted there, never mix the two in the code. */
inline double onePoleCoeff (double timeMs, double sampleRate) noexcept
{
    const auto tau = std::max (timeMs, 1.0e-3) * 1.0e-3;
    return std::exp (-1.0 / (tau * std::max (sampleRate, 1.0)));
}

/** A smooth, decoupled peak detector (Giannoulis, Massberg & Reiss 2012):

        y1[n] = max (x[n], ar y1[n-1] + (1 - ar) x[n])
        y[n]  = aa y[n-1] + (1 - aa) y1[n]

    Causal, no lookahead: the output at n has seen x[n] and nothing later.

    Two properties the tests hold it to, both easy to get wrong in a test
    rather than in the code:

    - **Attack is a pure one-pole** (y1 jumps straight to a rising input), so
      its 63.2 % time is tau -- measured from the sample *before* the step,
      because y[0] already includes the step. At 0.1 ms and 44.1 kHz tau is
      4.4 samples, and starting the clock one sample late reads as -22 %.
    - **Release is the two stages in cascade**, the release pole feeding the
      attack pole. It is only tau_R when tau_A is much shorter: at attack
      100 ms / release 10 ms it measures 110 ms. That is the topology, not a
      bug; the tests compare against the cascade.

    In RMS mode the same recursion runs on x^2 and the square root is taken
    after, which is what "Smooth" timing maps onto.
*/
class Detector
{
public:
    void configure (double attackMs, double releaseMs, bool rmsMode, double sampleRate) noexcept
    {
        aa  = onePoleCoeff (attackMs, sampleRate);
        ar  = onePoleCoeff (releaseMs, sampleRate);
        rms = rmsMode;
    }

    void reset() noexcept { y1 = y = 0.0; }

    /** Feed one rectified level (>= 0); returns the envelope, linear. */
    double process (double level) noexcept
    {
        const auto x = rms ? level * level : level;
        y1 = std::max (x, ar * y1 + (1.0 - ar) * x);
        y  = aa * y + (1.0 - aa) * y1;
        return envelope();
    }

    double envelope() const noexcept { return rms ? std::sqrt (y) : y; }

    void flushTiny() noexcept
    {
        if (y1 < 1.0e-30) y1 = 0.0;
        if (y  < 1.0e-30) y  = 0.0;
    }

    bool isNormal() const noexcept
    {
        return (y1 == 0.0 || std::isnormal (y1)) && (y == 0.0 || std::isnormal (y));
    }

private:
    double aa = 0.0, ar = 0.0, y1 = 0.0, y = 0.0;
    bool rms = false;
};

/** Which side of the threshold a dynamic band acts on. With the sign of
    `rangeDb` this covers all four cases -- cut or boost, above or below --
    without calling any of them "upward" or "expansion", which mean different
    things to different people. */
enum class Direction { above, below };

/** The static curve: how far the envelope is past the threshold, in dB,
    through a soft knee (Giannoulis et al.; the same parabola BMO Opto uses),
    turned into a gain offset that moves toward `rangeDb` and stops there.

        over  = envelope - threshold          (above)
              = threshold - envelope          (below)
        knee  = 0                              over <= -W/2
              = (over + W/2)^2 / (2W)          |over| < W/2
              = over                           over >= W/2
        offset = sign(range) * min(|range|, (1 - 1/R) * knee)

    For a cut above threshold this is exactly the textbook compressor,
    y = T + (x - T)/R. A zero-width knee is the hard corner, handled as its
    own case rather than divided by. */
struct GainComputer
{
    double thresholdDb = -24.0;
    double ratio       = 2.0;
    double kneeDb      = 6.0;
    double rangeDb     = -6.0;
    Direction direction = Direction::above;

    double offsetDb (double envelopeDb) const noexcept
    {
        const auto over = direction == Direction::above ? envelopeDb - thresholdDb
                                                        : thresholdDb - envelopeDb;
        const auto w = std::max (kneeDb, 0.0);

        double knee;
        if (w <= 0.0)              knee = std::max (over, 0.0);
        else if (over <= -0.5 * w) knee = 0.0;
        else if (over <  0.5 * w)  knee = (over + 0.5 * w) * (over + 0.5 * w) / (2.0 * w);
        else                       knee = over;

        const auto slope  = ratio > 1.0 ? 1.0 - 1.0 / ratio : 0.0;
        const auto amount = std::min (std::abs (rangeDb), slope * knee);
        return rangeDb < 0.0 ? -amount : amount;
    }
};

} // namespace bmo::deq
