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
// colouring the signal anyway. BandSplit::inCircuit() says when it is
// in, and testRailsAreExactlyOff is what holds it.
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

/** How a crossover in circuit glides to a new frequency. See
    LinkwitzRiley4::glideTo.

    **At a rate that keeps pace with the frequency it is passing**, not in a
    fixed time. A crossover's allpass turns the phase of everything within
    about two octaves of it, and how much the level of a tone wobbles while
    that happens depends only on how many of the tone's own cycles the move
    takes per octave: measured on a sweeping LR4 at 15 Hz, 100 Hz and 1 kHz
    alike, half a cycle per octave swings a tone -3.1 / +5.1 dB, one cycle
    -1.8 / +2.6, two -1.0 / +1.2, four -0.5 / +0.6. So the crossover moves
    kCrossoverGlideCycles of its own period per octave, which is the same
    figure at every frequency it passes: the period itself moves in a
    straight line, quickly through the top of the range and slowly through
    the bottom. A glide is never shorter than kCrossoverGlideMs; the first
    build glided everything in a fixed 20 ms, which was quick enough at a
    few kHz and swung a 100 Hz tone by 7 dB. */
inline constexpr double kCrossoverGlideMs     = 20.0;
inline constexpr double kCrossoverGlideCycles = 2.5;

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

    /** Moves the crossover to `hz`, from wherever it is, for a split that is in circuit. Asking for the frequency already
        being approached (or held) changes nothing, so this is safe to call
        every block with the control's current value.

        **Glided, not crossfaded between two splits**, because the filter
        stays well behaved while it moves. The TPT section's poles are the
        bilinear transform of a Butterworth pair, so at every cutoff along the
        way they sit inside the unit circle: the radius is
        sqrt ((1 - k g + g^2) / (1 + k g + g^2)), largest where the low side
        parks at 5 Hz: 0.999884 at 192 kHz, the worst of any rate. And the
        state is
        trapezoidal integrator memory, not past outputs, so a coefficient
        that changes does not turn old output into a new click -- which is
        why this module uses TPT sections at all. A second split to fade
        into would double the crossover's cost for the length of every move,
        and a knob dragged across a block boundary would start a new fade
        each block.

        How fast it moves is kCrossoverGlideCycles' business: see there. */
    void glideTo (float hz) noexcept { glideToWarped (warpedFor (hz)); }

    /** Puts the crossover at the warped frequency `warped` at once. */
    void setWarped (float warped) noexcept
    {
        glideLeft = 0;
        g = gTarget = warped;
        applyWarped (g);
    }

    /** glideTo() for a warped frequency. */
    void glideToWarped (float target) noexcept
    {
        if (! (g > 0.0f))
        {
            setWarped (target);
            return;
        }

        if (! (target < gTarget) && ! (gTarget < target))
            return;

        // The period, 1/g, moves in a straight line: kCrossoverGlideCycles of
        // it per octave is d(1/g) = ln 2 / (pi N) a sample, whatever the
        // frequency, while g is well below Nyquist -- and never less than
        // kCrossoverGlideMs in all.
        const auto from = 1.0 / (double) g, to = 1.0 / (double) target;
        const auto samples = std::abs (to - from) * 3.14159265358979323846 * kCrossoverGlideCycles / 0.69314718055994531;

        gTarget   = target;
        glideLeft = std::max (glideLength, (int) std::min (samples, 1.0e8));
        period    = from;
        periodStep = (to - from) / (double) glideLeft;
    }

    bool  isGliding() const noexcept    { return glideLeft > 0; }
    float warpedTarget() const noexcept { return gTarget; }

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
            period += periodStep;
            g = (float) (1.0 / period);
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

    /** tan(pi fc / fs) for `hz`, clamped to the range every cutoff in this
        module is placed in. */
    float warpedFor (float hz) const noexcept
    {
        const auto nyquist = (float) (sampleRate * 0.5);
        const auto fc = std::clamp (hz, 10.0f, nyquist * kMaxCutoffOfNyquist);
        return std::tan (3.14159265358979323846f * fc / (float) sampleRate);
    }

private:
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

    float  g = 0.0f, gTarget = 0.0f;
    double period = 0.0, periodStep = 0.0;
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
        lowMix.prepare (rate, kLowFadeMs);
        highMix.prepare (rate, kBandSwitchMs);

        lowEdge  = std::tan (3.14159265358979323846f * kLowSplitEdgeHz / (float) std::max (rate, 1.0));
        highEdge = upper.warpedFor (1.0e9f);
        lowWarmLength  = (int) std::lround (std::max (rate, 1.0) * kLowWarmUpMs * 0.001);
        highWarmLength = (int) std::lround (std::max (rate, 1.0) * kHighWarmUpMs * 0.001);
        reset();
    }

    /** Clears every filter and puts each side where its control last said,
        with nothing fading or gliding: a side that is wanted in circuit, at
        its setting; one that is not, out and parked at its edge. */
    void reset() noexcept
    {
        lowWarm = highWarm = 0;
        primeLow = primeHigh = false;
        lowMix.snap (wantLow ? 1.0f : 0.0f);
        highMix.snap (wantHigh ? 1.0f : 0.0f);

        lower.reset();
        upper.reset();
        lowAlign.reset();

        if (wantLow)  lower.setCutoff (lowCutoffHz);
        else          lower.setWarped (lowEdge);

        if (wantHigh) { upper.setCutoff (highCutoffHz); lowAlign.setCutoff (highCutoffHz); }
        else          { upper.setWarped (highEdge);     lowAlign.setWarped (highEdge); }
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
        wantLow  = lowHz  > kLowThruOffHz;
        wantHigh = highHz < kHighThruOffHz;

        // A side that is out is cleared, so that it comes back from silence
        // rather than replaying what it held when it went out. A side left out
        // while the other ran on used to keep its state frozen: with both
        // sides in, HIGH THRU to its rail and back 0.8 s into digital silence
        // put out a 0.13 peak, LOW THRU 0.05. Out means faded all the way out,
        // not merely asked to go: a side that is fading is still running.
        // A side being primed for a switch is out but is not idle: it is
        // running on the input, unheard, so the switch finds it warm.
        if (! lowRunning() && ! primeLow)
            lower.reset();

        if (! highRunning() && ! primeHigh)
            upper.reset();

        if (! (lowRunning() && highRunning()) && ! (primeLow && primeHigh))
            lowAlign.reset();

        if (wantLow)
        {
            lowTarget   = lower.warpedFor (lowHz);
            lowCutoffHz = lowHz;
        }

        if (wantHigh)
        {
            highTarget   = upper.warpedFor (highHz);
            highCutoffHz = highHz;
        }

        steerLow();
        steerHigh();
    }

    /** True while either side is in circuit or on its way in or out, and
        therefore while process() has to be called at all. With both sides
        out the whole split is skipped: a crossover left in circuit costs its
        allpass phase shift whether or not anything is in its outer bands. */
    bool inCircuit() const noexcept { return lowRunning() || highRunning(); }

    /** Whether each side was last told to be in, whatever it is doing about it. */
    bool wantsLow() const noexcept  { return wantLow; }
    bool wantsHigh() const noexcept { return wantHigh; }

    //== Switching, as against moving a knob ===================================
    //
    // A side brought in or taken out by a *switch* -- COMPLEX, which puts both
    // sides at their rails when it goes off -- does not take the knob's way in
    // by its edge, which takes up to a second to arrive and carries whatever
    // the compressor makes of the band in transit for all of it. The module
    // dips its output to nothing instead (DspCore), and these three are the
    // split's half of that: while the output fades down, prime() runs the
    // sides the switch will bring in, already at their settings, on the live
    // input, unheard; at the bottom, switchTo() puts the split where the
    // controls say at once, keeping what was primed; and the output fades
    // back up with the split already settled.

    /** The sides that `lowHz` / `highHz` will bring in at a switch, and that
        are not in circuit now, run from here on in prime() at those
        settings. Safe to call every block; a setting that changes moves the
        primed crossover at once -- nothing is listening to it. */
    void primeFor (float lowHz, float highHz) noexcept
    {
        primeLow  = lowHz  > kLowThruOffHz  && ! lowRunning();
        primeHigh = highHz < kHighThruOffHz && ! highRunning();

        if (primeLow && lower.warpedTarget() != lower.warpedFor (lowHz))
            lower.setCutoff (lowHz);

        if (primeHigh && upper.warpedTarget() != upper.warpedFor (highHz))
        {
            upper.setCutoff (highHz);
            lowAlign.setCutoff (highHz);
        }
    }

    /** Stops priming; what was primed is cleared at the next setCutoffs(). */
    void cancelPrime() noexcept { primeLow = primeHigh = false; }

    bool isPriming() const noexcept { return primeLow || primeHigh; }

    /** One sample of the input through the sides being primed, exactly as
        the split would run them, with nothing heard. */
    void prime (float x) noexcept
    {
        float below = 0.0f, rest = x, inner = 0.0f, above = 0.0f;

        if (primeLow)
            lower.process (x, below, rest);

        if (primeHigh)
            upper.process (rest, inner, above);

        if (primeLow && primeHigh)
            lowAlign.allpass (below);
    }

    /** At the bottom of a switch: each side in or out as `lowHz` / `highHz`
        say, at once, at its setting, nothing fading or gliding. A side that
        was primed keeps its state; one that goes out is cleared and parked. */
    void switchTo (float lowHz, float highHz) noexcept
    {
        wantLow  = lowHz  > kLowThruOffHz;
        wantHigh = highHz < kHighThruOffHz;
        lowWarm = highWarm = 0;

        lowMix.snap (wantLow ? 1.0f : 0.0f);
        highMix.snap (wantHigh ? 1.0f : 0.0f);

        if (wantLow)
        {
            lower.setCutoff (lowHz);
            lowTarget   = lower.warpedFor (lowHz);
            lowCutoffHz = lowHz;
        }
        else
        {
            lower.reset();
            lower.setWarped (lowEdge);
        }

        if (wantHigh)
        {
            upper.setCutoff (highHz);
            lowAlign.setCutoff (highHz);
            highTarget   = upper.warpedFor (highHz);
            highCutoffHz = highHz;
        }
        else
        {
            upper.reset();
            lowAlign.reset();
            upper.setWarped (highEdge);
            lowAlign.setWarped (highEdge);
        }

        if (! (wantLow && wantHigh))
            lowAlign.reset();

        primeLow = primeHigh = false;
    }

    /** Splits `x` into the band to compress and the band that passes through.

        With one side at its rail there is only one crossover in circuit, so
        there is no second split for the low band's phase to be aligned to and
        `lowAlign` is not used -- the two bands are that one crossover's own
        outputs and sum to its allpass by construction.

        **A side comes in and goes out at the far edge of its range.** It used
        to switch in one sample, and a crossover in circuit is not the signal
        it replaces -- it is an allpass of it: on a 150 Hz tone LOW THRU 200 ->
        20 stepped 104x the signal's own largest step, COMPLEX on -> off 144x,
        HIGH THRU 20k -> 6k 35x. The first cure faded the side from the signal
        to its split over 10 ms where it stood, and that traded the step for a
        notch: half way through, the dry signal and the allpass are in
        anti-phase at the crossover, so a tone there cancelled completely and
        anything within an octave of it dipped more than 3 dB.

        So a side its *knob* brings in starts with its crossover parked at the
        edge -- kLowSplitEdgeHz for the low side, the top of the range for the
        high one -- where its allpass is a wire across the audio band, runs
        there unheard until its start-up transient has gone, fades in there,
        and then glides to its setting; going out, it glides to the edge first
        and fades out there. Fading at the edge costs nothing anybody can
        hear, and a crossover in circuit gliding stays within 1 dB of an
        allpass at kCrossoverGlideCycles. The time it all takes, at any rate up
        to 192 kHz: the low side in at most 250 + 30 ms and a glide of up to
        0.69 s (LOW THRU 500), 0.97 s in all, and out in a glide and 30 ms;
        the high side in 5 + 10 ms and a glide of at least 20 ms, out in a
        glide and 10 ms. A *switch* -- COMPLEX -- does not come this way: see
        primeFor() and DspCore.

        With nothing moving the arithmetic is exactly what it always was. */
    void process (float x, float& mid, float& thru) noexcept
    {
        const auto lowWasMoving  = lower.isGliding() || lowMix.isMoving();
        const auto highWasMoving = upper.isGliding() || highMix.isMoving();

        split (x, mid, thru);

        // A side that has run long enough at its edge for its filters to
        // have forgotten starting from rest starts to fade in.
        if (lowWarm > 0 && --lowWarm == 0)
            lowMix.setTarget (1.0f);

        if (highWarm > 0 && --highWarm == 0)
            highMix.setTarget (1.0f);

        // A glide or a fade that has just finished hands each side on to the
        // next step of its way in or out.
        if (lowWasMoving && ! lower.isGliding() && ! lowMix.isMoving())
            steerLow();

        if (highWasMoving && ! upper.isGliding() && ! highMix.isMoving())
            steerHigh();
    }

private:
    void split (float x, float& mid, float& thru) noexcept
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

        if (! lowMix.isMoving() && ! highMix.isMoving() && lowWarm == 0 && highWarm == 0)
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

    /** How long the high side takes to fade in or out at its edge, in ms: the
        suite's switch time. */
    static constexpr double kBandSwitchMs = 10.0;

    /** The low side's fade at its edge, in ms. Longer than the high side's
        because its edge is 5 Hz, not above the audio band: a 20 Hz tone is
        39 degrees out of phase with that allpass, and a 10 ms fade between
        the two stepped 1.52x the tone's own largest step. */
    static constexpr double kLowFadeMs = 30.0;

    /** Where the low side's crossover is parked while it fades in or out.
        Below the bottom of LOW THRU's range by more than three octaves, so
        that fading the dry signal into its allpass there moves the level of
        nothing a voice has: -0.5 dB at 20 Hz, the bottom of what the dip is
        measured at; below it, at 10 Hz, the reviewer measured -2.68 dB. The
        high side parks at the top of
        the range a crossover can be placed in, 0.98 of Nyquist, which is as
        far above 20 kHz as each rate allows. */
    static constexpr float kLowSplitEdgeHz = 5.0f;

    /** How long a low side coming in runs at its edge, heard by nothing,
        before it starts to fade in, in ms. A crossover started from rest is
        not yet the allpass it settles into: at 5 Hz its start-up transient
        takes about 45 ms per time constant to die away, and faded in at once
        it moved a 30 Hz tone by -1.9 to -2.7 dB. Five time constants and
        more. */
    static constexpr double kLowWarmUpMs = 250.0;

    /** The same for the high side, in ms. Its edge is near Nyquist, where a
        section started from rest rings at Nyquist for a few samples; heard
        under the fade, that stepped a 20 Hz tone 1.60x its own largest step
        (the reviewer, 48 kHz). The ringing decays by 0.69 a sample at its
        slowest, so 5 ms leaves nothing. */
    static constexpr double kHighWarmUpMs = 5.0;

    bool lowRunning() const noexcept  { return lowWarm > 0 || lowMix.isMoving()  || lowMix.value()  > 0.0f; }
    bool highRunning() const noexcept { return highWarm > 0 || highMix.isMoving() || highMix.value() > 0.0f; }

    /** One side's next step on its way in or out. Called whenever its
        control is set and whenever its glide or its fade finishes, so a side
        always knows where it is going: in at the edge, then a glide to its
        setting; or a glide to the edge, then out. A control that changes its
        mind part way turns the side round from wherever it is. */
    static void steer (bool want, float target, float edge, dsp::Ramp& mix, int& warm, int warmLength,
                       LinkwitzRiley4& xo, LinkwitzRiley4* mirror) noexcept
    {
        const auto running = warm > 0 || mix.isMoving() || mix.value() > 0.0f;

        auto glide = [&] (float to)
        {
            xo.glideToWarped (to);
            if (mirror != nullptr) mirror->glideToWarped (to);
        };

        if (want)
        {
            if (! running)
            {
                // From rest, parked at the edge: run there unheard while
                // the filters settle, then fade in there.
                xo.setWarped (edge);
                if (mirror != nullptr) mirror->setWarped (edge);

                if (warmLength > 0) warm = warmLength;
                else                mix.setTarget (1.0f);

                return;
            }

            if (warm > 0)
                return;

            if (mix.target() < 1.0f)
            {
                // Fading out at the edge: turn round there.
                mix.setTarget (1.0f);
                return;
            }

            if (! mix.isMoving())
                glide (target);

            return;
        }

        if (! running)
            return;

        if (warm > 0)
        {
            // Still settling, unheard: simply stop.
            warm = 0;
            return;
        }

        if (mix.isMoving())
        {
            // Fading in at the edge: turn round there.
            mix.setTarget (0.0f);
            return;
        }

        if (xo.warpedTarget() < edge || edge < xo.warpedTarget())
        {
            glide (edge);
            return;
        }

        if (! xo.isGliding())
            mix.setTarget (0.0f);
    }

    void steerLow() noexcept  { steer (wantLow,  lowTarget,  lowEdge,  lowMix,  lowWarm,  lowWarmLength, lower, nullptr); }
    void steerHigh() noexcept { steer (wantHigh, highTarget, highEdge, highMix, highWarm, highWarmLength, upper, &lowAlign); }

    LinkwitzRiley4 lower, upper, lowAlign;
    dsp::Ramp lowMix, highMix;
    bool  wantLow = false, wantHigh = false;
    int   lowWarm = 0, highWarm = 0, lowWarmLength = 0, highWarmLength = 0;
    bool  primeLow = false, primeHigh = false;
    float lowTarget = 0.0f, highTarget = 0.0f, lowEdge = 0.0f, highEdge = 0.0f;
    float lowCutoffHz = kLowThruOffHz, highCutoffHz = kHighThruOffHz;
};

} // namespace bmo::vcomp
