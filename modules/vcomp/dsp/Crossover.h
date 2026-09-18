#pragma once

#include "modules/vcomp/params.h"

#include <algorithm>
#include <cmath>

namespace bmo::vcomp
{

//==============================================================================
// LOW THRU and HIGH THRU: the bands the compressor does not act on.
//
// **This is not a sidechain filter, and the difference is the point.** The
// SIDECHAIN high-pass changes what the detector *hears*; the low end is still
// ducked, it just stops being the thing that triggers the ducking. These two
// controls change what the compressor *acts on*: the band below LOW THRU and
// the band above HIGH THRU are split off the audio, pass through untouched and
// are added back. The chest of a voice keeps its weight while the midrange is
// levelled; air and sibilance keep their top while the body is held down.
//
// **Linkwitz-Riley, fourth order**, which is two cascaded Butterworth-Q
// sections per band -- the squared Butterworth response whose low and high
// outputs sum to an allpass rather than to a bump at the crossover. Magnitude
// is flat through the split; phase is not, and that is what band-splitting
// costs at zero latency. A linear-phase crossover would fix the phase and
// spend latency, which this module has decided it does not have (see
// VcompDsp::latencyForParams).
//
// **The low band is run through the second crossover's allpass**, and that is
// the part a three-band split gets wrong if nobody says so. Splitting at LOW
// THRU and then splitting only the remainder at HIGH THRU leaves the low band
// having been through one crossover and the other two through two, so the
// three no longer sum flat -- the error is a dip around the upper crossover
// that moves when HIGH THRU moves. Passing the low band through the second
// split's allpass (which is just that split's own low + high summed) puts
// every band through the same phase, and the three then reconstruct to a pure
// allpass of the input. testBandsReconstruct is what holds it.
//
// **At both rails the crossover is bypassed outright**, rather than run with a
// 20 Hz low band and a 20 kHz high band that contain nothing. Nothing is not
// quite nothing: the filters would still be in circuit and would still cost
// the allpass phase shift, so a module that "isn't using" the feature would be
// colouring the signal anyway. DspCore::bandsActive() is the switch and
// testRailsAreExactlyOff is what holds it.
//==============================================================================

/** How close to Nyquist a filter cutoff in this module may be placed, as a
    fraction of Nyquist. tan() runs away at Nyquist itself, so every cutoff in
    this module -- the crossovers here and the sidechain high-pass in
    Detector.h -- is clamped to it.

    **0.98, not the 0.45 this started at**, and the difference is the whole of
    HIGH THRU's upper range. 0.45 of Nyquist is 10.8 kHz at 48 kHz and 9.9 kHz
    at 44.1, so more than half that control's travel was unreachable -- and it
    did not fail visibly, it just put the crossover somewhere other than where
    the panel said. See BandSplit::setCutoffs for what that cost.

    At 0.98 the ceiling is 23.5 kHz at 48 kHz and 21.6 at 44.1, so the whole
    20 kHz range is real at every rate the suite runs at. The coefficient stays
    well conditioned there: g = tan(0.49 pi) is about 32, and a1 = 1/(1 + g(g +
    k)) about 9.5e-4, which is nowhere near the edge of float. */
inline constexpr float kMaxCutoffOfNyquist = 0.98f;

/** One TPT state variable section (Zavalishin, The Art of VA Filter Design),
    giving low-pass and high-pass outputs from the same state. TPT rather than
    a biquad because the coefficients stay well behaved when the cutoff is
    moved while it runs, which is what a user dragging LOW THRU does. */
class Svf
{
public:
    void reset() noexcept { ic1 = ic2 = 0.0f; }

    void setCoefficients (float newA1, float newA2, float newA3) noexcept
    {
        a1 = newA1; a2 = newA2; a3 = newA3;
    }

    void process (float x, float& lp, float& hp) noexcept
    {
        const auto v3 = x - ic2;
        const auto v1 = a1 * ic1 + a2 * v3;
        const auto v2 = ic2 + a2 * ic1 + a3 * v3;

        ic1 = 2.0f * v1 - ic1;
        ic2 = 2.0f * v2 - ic2;

        lp = v2;
        hp = x - kK * v1 - v2;
    }

    static constexpr float kK = 1.41421356237f;  ///< 1/Q, Butterworth

private:
    float a1 = 0.0f, a2 = 0.0f, a3 = 0.0f;
    float ic1 = 0.0f, ic2 = 0.0f;
};

//==============================================================================
/** A fourth-order Linkwitz-Riley split: two Butterworth sections cascaded down
    the low path and two more down the high path, each with its own state.

    The low and high paths must not share filter state. They are two separate
    cascades of the same section, which is the whole construction -- running
    one section and taking its two outputs gives a Butterworth split, not a
    Linkwitz-Riley one, and those sum to a 3 dB bump at the crossover rather
    than flat. */
class LinkwitzRiley4
{
public:
    void prepare (double rate) noexcept
    {
        sampleRate = std::max (rate, 1.0);
        reset();
    }

    void reset() noexcept
    {
        for (auto& s : low)  s.reset();
        for (auto& s : high) s.reset();
    }

    void setCutoff (float hz) noexcept
    {
        const auto nyquist = (float) (sampleRate * 0.5);
        const auto fc = std::clamp (hz, 10.0f, nyquist * kMaxCutoffOfNyquist);
        const auto g  = std::tan (3.14159265358979323846f * fc / (float) sampleRate);

        const auto a1 = 1.0f / (1.0f + g * (g + Svf::kK));
        const auto a2 = g * a1;
        const auto a3 = g * a2;

        for (auto& s : low)  s.setCoefficients (a1, a2, a3);
        for (auto& s : high) s.setCoefficients (a1, a2, a3);
    }

    void process (float x, float& lowOut, float& highOut) noexcept
    {
        float lp = 0.0f, hp = 0.0f;

        auto l = x;
        for (auto& s : low)  { s.process (l, lp, hp); l = lp; }

        auto h = x;
        for (auto& s : high) { s.process (h, lp, hp); h = hp; }

        lowOut = l;
        highOut = h;
    }

    /** The allpass this split is equivalent to: its own two outputs summed.
        Used to put a band that skipped a crossover through the same phase as
        the bands that did not -- see the note at the top of this file. */
    float allpass (float x) noexcept
    {
        float l = 0.0f, h = 0.0f;
        process (x, l, h);
        return l + h;
    }

private:
    double sampleRate = 44100.0;
    Svf low[2], high[2];
};

//==============================================================================
/** The three-band arrangement this module uses: everything below LOW THRU and
    above HIGH THRU passes through, the middle is handed to the caller to
    compress, and the three sum back to an allpass of the input.

    `lowAlign` is not a third crossover the signal is being filtered by twice.
    It is the second split run on the low band purely to take its allpass --
    see the file header. */
class BandSplit
{
public:
    void prepare (double rate) noexcept
    {
        lower.prepare (rate);
        upper.prepare (rate);
        lowAlign.prepare (rate);
        reset();
    }

    void reset() noexcept
    {
        lower.reset();
        upper.reset();
        lowAlign.reset();
    }

    void setCutoffs (float lowHz, float highHz) noexcept
    {
        // **Each side is engaged independently, and a side at its rail is not
        // run at all.** Running it anyway is not harmless, and the way it goes
        // wrong is worth recording because nothing about it is audible as a
        // fault -- it is audible as the module quietly not compressing.
        //
        // A crossover cannot be placed above Nyquist, so setCutoff clamps. It
        // now clamps at 0.98 of Nyquist, which is above the rail at every rate
        // and therefore harmless -- but the first version clamped at 0.45 of
        // Nyquist, i.e. 10.8 kHz at 48 kHz, and then a user who moved LOW
        // THRU alone got an upper split sitting at 10.8 kHz while HIGH THRU
        // read 20 kHz on the panel. Everything above 10.8 kHz was being
        // handed to the thru band and passed through uncompressed, with no
        // control on the panel saying so.
        //
        // tools/measure/vcomp's bands report is what caught it: with LOW THRU
        // at 300 and HIGH THRU at its rail, a 12 kHz tone was pumped by 4.7 dB
        // where it should have been the full 11.5, and the number was there in
        // a table next to three that were right.
        splitLow  = lowHz  > kLowThruOffHz;
        splitHigh = highHz < kHighThruOffHz;

        lower.setCutoff (lowHz);
        upper.setCutoff (highHz);
        lowAlign.setCutoff (highHz);
    }

    /** Splits `x` into the band to compress and the band that passes through.

        With one side at its rail there is only one crossover in circuit, so
        there is no second split for the low band's phase to be aligned to and
        `lowAlign` is not used -- the two bands are that one crossover's own
        outputs and sum to its allpass by construction. */
    void process (float x, float& mid, float& thru) noexcept
    {
        if (splitLow && splitHigh)
        {
            float below = 0.0f, rest = 0.0f, above = 0.0f;

            lower.process (x, below, rest);
            upper.process (rest, mid, above);

            thru = lowAlign.allpass (below) + above;
            return;
        }

        if (splitLow)
        {
            lower.process (x, thru, mid);
            return;
        }

        if (splitHigh)
        {
            upper.process (x, mid, thru);
            return;
        }

        // Not reached while DspCore::bandsActive() guards the call, and
        // correct rather than merely unreachable if that ever changes.
        mid = x;
        thru = 0.0f;
    }

private:
    LinkwitzRiley4 lower, upper, lowAlign;
    bool splitLow = false, splitHigh = false;
};

} // namespace bmo::vcomp
