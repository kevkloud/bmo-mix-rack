#pragma once

#include "core/dsp/SwitchFade.h"
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

/** How long a crossover in circuit takes to glide to a new frequency, in ms.
    See LinkwitzRiley4::glideTo. Twice the suite's 10 ms switch time on
    purpose: a glide moves a band edge across the signal rather than blending
    two settings of it, and at 10 ms LOW THRU 21 -> 500 stepped 1.59x the
    signal's own largest step on a 150 Hz tone, 1.33x at 20 ms. */
inline constexpr double kCrossoverGlideMs = 20.0;

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
        glideLength = std::max (1, (int) std::lround (sampleRate * kCrossoverGlideMs * 0.001));
        reset();
    }

    void reset() noexcept
    {
        for (auto& s : low)  s.reset();
        for (auto& s : high) s.reset();
    }

    /** Puts the crossover at `hz` at once, ending any glide. For a split that
        is not running, or is starting from rest. */
    void setCutoff (float hz) noexcept
    {
        glideLeft = 0;
        g = gTarget = warpedFor (hz);
        applyWarped (g);
    }

    /** Moves the crossover to `hz` over kCrossoverGlideMs, from wherever it
        is, for a split that is in circuit. Asking for the frequency already
        being approached (or held) changes nothing, so this is safe to call
        every block with the control's current value.

        **Glided, not crossfaded between two splits**, because the filter
        stays well behaved while it moves. The TPT section's poles are the
        bilinear transform of a Butterworth pair, so at every cutoff along the
        way they sit inside the unit circle: the radius is
        sqrt ((1 - k g + g^2) / (1 + k g + g^2)), largest at the bottom of
        LOW THRU's range, 0.99907 at 20 Hz and 96 kHz. And the state is
        trapezoidal integrator memory, not past outputs, so a coefficient
        that changes does not turn old output into a new click -- which is
        why this module uses TPT sections at all. A second split to fade
        into would double the crossover's cost for the length of every move,
        and a knob dragged across a block boundary would start a new fade
        each block.

        The glide is geometric in the warped frequency g = tan(pi fc / fs),
        which is the frequency itself on a log scale everywhere but the top
        octave: equal time for equal ratios, as the knob is laid out. */
    void glideTo (float hz) noexcept
    {
        const auto target = warpedFor (hz);

        if (! (target < gTarget) && ! (gTarget < target))
            return;

        gTarget    = target;
        glideLeft  = glideLength;
        glideRatio = std::pow (gTarget / g, 1.0f / (float) glideLength);
    }

    bool isGliding() const noexcept { return glideLeft > 0; }

    /** One sample of a glide. Lands on the target exactly, so a crossover
        that has finished moving has the coefficients it would have had if it
        had been put there. */
    void advanceGlide() noexcept
    {
        if (--glideLeft <= 0)
        {
            glideLeft = 0;
            g = gTarget;
        }
        else
        {
            g *= glideRatio;
        }

        applyWarped (g);
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
    float warpedFor (float hz) const noexcept
    {
        const auto nyquist = (float) (sampleRate * 0.5);
        const auto fc = std::clamp (hz, 10.0f, nyquist * kMaxCutoffOfNyquist);
        return std::tan (3.14159265358979323846f * fc / (float) sampleRate);
    }

    void applyWarped (float warped) noexcept
    {
        const auto a1 = 1.0f / (1.0f + warped * (warped + Svf::kK));
        const auto a2 = warped * a1;
        const auto a3 = warped * a2;

        for (auto& s : low)  s.setCoefficients (a1, a2, a3);
        for (auto& s : high) s.setCoefficients (a1, a2, a3);
    }

    double sampleRate = 44100.0;
    Svf low[2], high[2];

    float g = 0.0f, gTarget = 0.0f, glideRatio = 1.0f;
    int   glideLength = 1, glideLeft = 0;
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
        lowMix.prepare (rate, kBandSwitchMs);
        highMix.prepare (rate, kBandSwitchMs);
        reset();
    }

    /** Clears every filter and puts each side where its control last said,
        with nothing fading or gliding. */
    void reset() noexcept
    {
        lowMix.snap (lowMix.target());
        highMix.snap (highMix.target());

        lower.reset();
        upper.reset();
        lowAlign.reset();

        lower.setCutoff (lowCutoffHz);
        upper.setCutoff (highCutoffHz);
        lowAlign.setCutoff (highCutoffHz);
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
        const auto wantLow  = lowHz  > kLowThruOffHz;
        const auto wantHigh = highHz < kHighThruOffHz;

        // A side that is out is cleared, so that it comes back from silence
        // rather than replaying what it held when it went out. A side left out
        // while the other ran on used to keep its state frozen: with both
        // sides in, HIGH THRU to its rail and back 0.8 s into digital silence
        // put out a 0.13 peak, LOW THRU 0.05. Out means faded all the way out,
        // not merely asked to go: a side that is fading is still running.
        if (! lowRunning())
            lower.reset();

        if (! highRunning())
            upper.reset();

        if (! (lowRunning() && highRunning()))
            lowAlign.reset();

        // A side coming in from rest is put at its frequency; one already in
        // circuit glides there. A side going out keeps the frequency it had
        // while it fades: its rail means "not in circuit", not a frequency to
        // glide towards. lowAlign is always where the upper split is.
        if (wantLow)
        {
            if (lowRunning())  lower.glideTo (lowHz);
            else               lower.setCutoff (lowHz);

            lowCutoffHz = lowHz;
        }

        if (wantHigh)
        {
            if (highRunning()) { upper.glideTo (highHz);   lowAlign.glideTo (highHz); }
            else               { upper.setCutoff (highHz); lowAlign.setCutoff (highHz); }

            highCutoffHz = highHz;
        }

        lowMix.setTarget  (wantLow  ? 1.0f : 0.0f);
        highMix.setTarget (wantHigh ? 1.0f : 0.0f);
    }

    /** True while either side is in circuit or fading, and therefore while
        process() has to be called at all. With both sides at rest at their
        rails the whole split is skipped -- see DspCore::bandsActive(). */
    bool inCircuit() const noexcept { return lowRunning() || highRunning(); }

    /** Splits `x` into the band to compress and the band that passes through.

        With one side at its rail there is only one crossover in circuit, so
        there is no second split for the low band's phase to be aligned to and
        `lowAlign` is not used -- the two bands are that one crossover's own
        outputs and sum to its allpass by construction.

        **A side comes in and goes out over kBandSwitchMs.** It used to switch
        in one sample, and a crossover in circuit is not the signal it
        replaces -- it is an allpass of it, so the two disagree in phase
        everywhere near the split: on a 150 Hz tone LOW THRU 200 -> 20 stepped
        104x the signal's own largest step, COMPLEX on -> off 144x, HIGH THRU
        20k -> 6k 35x. Fading, a side's own split blends with the signal it
        was handed and its outer band comes up with it, and the low band is
        aligned to the upper split in the same proportion as that split is
        in. With nothing fading the arithmetic is exactly what it always was. */
    void process (float x, float& mid, float& thru) noexcept
    {
        if (lower.isGliding())
            lower.advanceGlide();

        if (upper.isGliding())
        {
            upper.advanceGlide();
            lowAlign.advanceGlide();
        }

        const auto runLow  = lowRunning();
        const auto runHigh = highRunning();

        if (! lowMix.isMoving() && ! highMix.isMoving())
        {
            if (runLow && runHigh)
            {
                float below = 0.0f, rest = 0.0f, above = 0.0f;

                lower.process (x, below, rest);
                upper.process (rest, mid, above);

                thru = lowAlign.allpass (below) + above;
                return;
            }

            if (runLow)
            {
                lower.process (x, thru, mid);
                return;
            }

            if (runHigh)
            {
                upper.process (x, mid, thru);
                return;
            }

            // Not reached while DspCore calls this only while inCircuit(),
            // and correct rather than merely unreachable if that changes.
            mid = x;
            thru = 0.0f;
            return;
        }

        auto toUpper = x, below = 0.0f, lowAmount = 0.0f, highAmount = 0.0f, highBand = 0.0f;

        if (runLow)
        {
            float rest = 0.0f;
            lower.process (x, below, rest);

            lowAmount = lowMix.next();
            toUpper   = dsp::crossfade (x, rest, lowAmount);
        }

        mid = toUpper;

        if (runHigh)
        {
            float inner = 0.0f, above = 0.0f;
            upper.process (toUpper, inner, above);

            highAmount = highMix.next();
            mid        = dsp::crossfade (toUpper, inner, highAmount);
            highBand   = above * highAmount;
        }

        // The low band is aligned before its fade is applied, not after: an
        // allpass fed a faded band would ring on past the end of the fade
        // and be cut off when the side stops running.
        auto lowBand = 0.0f;

        if (runLow)
            lowBand = (runHigh ? dsp::crossfade (below, lowAlign.allpass (below), highAmount) : below) * lowAmount;

        thru = lowBand + highBand;
    }

private:
    /** How long a side of the split takes to come in or go out, in ms: the
        suite's switch time. */
    static constexpr double kBandSwitchMs = 10.0;

    bool lowRunning() const noexcept  { return lowMix.isMoving()  || lowMix.value()  > 0.0f; }
    bool highRunning() const noexcept { return highMix.isMoving() || highMix.value() > 0.0f; }

    LinkwitzRiley4 lower, upper, lowAlign;
    dsp::Ramp lowMix, highMix;
    float lowCutoffHz = kLowThruOffHz, highCutoffHz = kHighThruOffHz;
};

} // namespace bmo::vcomp
