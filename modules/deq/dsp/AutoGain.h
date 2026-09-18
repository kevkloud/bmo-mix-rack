#pragma once

#include "modules/deq/dsp/Design.h"
#include "modules/deq/dsp/DspCore.h"
#include <cmath>
#include <complex>

namespace bmo::deq
{

/** AUTO: the output gain that puts the EQ's broadband level back where it was.

    BMO EQ's rule, so the suite has one meaning for the word
    (modules/eq/dsp/EqNetwork.h, broadbandGain): the mean linear magnitude of
    the static curve at 48 log-spaced points from 20 Hz to 20 kHz, and AUTO
    applies its reciprocal.

    Static only, deliberately:

    - **Dynamic offsets are left out.** Compensating a band's moment-to-moment
      gain would be a second compressor working against the first -- a de-esser
      whose cut is made up again by the output is not a de-esser. A dynamic
      band counts at its knob gain, which is where it sits when idle.
    - **Side bands are left out**, and mid bands count in full: what is
      measured is a centred source, the same thing the panel's curve draws.
      Side content is usually a small part of the level, and a compensation
      that moved with the stereo image would move with the programme.

    Returns the mean magnitude (1 for a flat curve). JUCE-free and allocation-
    free; it designs each enabled band once, so it costs about what a dozen
    shelf redesigns do -- the adapter calls it only when a static setting has
    changed.
*/
inline double staticBroadbandGain (const Settings& s, const DesignGrid& grid) noexcept
{
    constexpr int kPoints = 48;
    constexpr double kLowHz = 20.0, kHighHz = 20000.0;

    if (grid.sampleRate <= 0.0)
        return 1.0;

    std::array<Biquad, kMaxBands> designs {};
    std::array<bool, kMaxBands> counts {};
    auto any = false;

    for (size_t i = 0; i < s.bands.size(); ++i)
    {
        const auto& b = s.bands[i];
        counts[i] = b.enabled && b.placement != Placement::side;

        if (counts[i])
        {
            designs[i] = designMatched (b.shape, b.frequencyHz, b.q, b.gainDb, grid);
            any = true;
        }
    }

    if (! any)
        return 1.0;

    // A point past 0.45 Fs is left out rather than evaluated: at a low device
    // rate the top of the audio band is not there to be compensated.
    double sum = 0.0;
    int used = 0;

    for (int k = 0; k < kPoints; ++k)
    {
        const auto hz = kLowHz * std::pow (kHighHz / kLowHz, (double) k / (double) (kPoints - 1));
        if (hz > 0.45 * grid.sampleRate)
            break;

        const auto w = 2.0 * kPi * hz / grid.sampleRate;
        std::complex<double> h = 1.0;

        for (size_t i = 0; i < s.bands.size(); ++i)
            if (counts[i])
                h *= designs[i].responseAt (w);

        sum += std::abs (h);
        ++used;
    }

    return used > 0 ? sum / (double) used : 1.0;
}

} // namespace bmo::deq
