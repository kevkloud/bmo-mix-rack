#pragma once

#include "ModelTables.h"
#include "Svf.h"
#include "core/dsp/SwitchFade.h"
#include <array>

namespace bmo::eq
{

struct EqSettings
{
    float hfFreqHz   = 12000.0f;
    float hfGainDb   = 0.0f;

    float midFreqHz  = 1600.0f;
    float midGainDb  = 0.0f;
    bool  midHiQ     = false;

    /** Branch Q for the mid band. Zero or less means derive it from the centre
        frequency, which is what everything except the calibration solver
        wants. */
    float midQ       = 0.0f;

    float lfFreqHz   = 60.0f;
    float lfGainDb   = 0.0f;

    float hpfFreqHz  = 0.0f;   // 0 == off
    float lpfFreqHz  = 0.0f;   // 0 == off
};

//==============================================================================
/** The equaliser section.

    The bands are NOT three biquads in series. In the original, all three live
    in a single passive LC network sitting in the feedback path of one gain
    stage, so their branch admittances sum:

        H(s) = (1 + SUM  g_i * B_i(s)) / (1 + SUM (1/g_i) * B_i(s))

    where B_i is branch i's response normalised to peak at 1.0 and g_i is that
    band's linear gain. Two properties fall out of this that cascaded biquads
    cannot reproduce, and they are most of why the hardware sounds the way it
    does:

      - Band interaction. Boosting the mid and the high shelf together yields
        less than the sum of the two curves where they overlap, because both
        branches load the same feedback path. It is why the unit stays civil
        under EQ moves that would make a clean digital EQ harsh.

      - Proportional Q. The realised bell narrows as gain increases, without
        anyone having to model that as a special case.

    Realisation. Rather than multiplying the rational functions out into a
    sixth-order IIR (which would need root-finding on every coefficient change),
    the structure is preserved directly as a zero-delay feedback loop around the
    branch filters:

        u = x - SUM (1/g_i) * B_i(u)        <- denominator
        y = u + SUM  g_i    * B_i(u)        <- numerator

    Each branch reports its response as an affine function of its input,
    b_i = d_i * u + v_i (see Svf::analyse), so the loop resolves in closed form:

        u = (x - SUM (1/g_i) * v_i) / (1 + SUM (1/g_i) * d_i)

    One division per sample, no iteration, and the denominator is provably
    greater than 1 because every d_i and g_i is positive, so it cannot blow up.

    The high- and low-pass filters are separate passive sections in the original
    rather than part of the feedback network, so they sit outside the loop.
*/
class EqNetwork
{
public:
    /** How long a switch in the EQ takes to cross over: Low Cut, High Cut,
        EQ In, Phase and Hi-Q here and in DspCore, and each half of the dip
        around an oversampling change.

        Why 10 ms. Long enough that a 100 Hz tone through Low Cut 360 --
        nearly all of it removed, and what is left shifted by most of a
        cycle -- crosses at 1.03x, inside the 1.5x step bound with room to
        spare; short enough to sit well inside the time a hand takes to move
        from one switch to the next.

        The limit, and it is a real one. A fade of fixed length adds a slope
        of about the signal's own size over its length, while a tone's own
        slope falls with its frequency, so every fixed fade has a frequency
        below which it measures. For 10 ms that is about 30 Hz: at 30 Hz
        Phase and a cut switching on reach 1.43x, at 25 Hz 1.61x and 1.58x,
        at 20 Hz about 1.8x. eq_switch_tests holds the 1.5x bound from 30 Hz
        up and asserts the 25 Hz figures as their own bound (section 2c).
        Measured at 48 kHz, a 13 ms fade holds 25 Hz at 1.37x and a 17 ms
        one holds 20 Hz at 1.41x -- but for every switch and every signal;
        that trade is the owner's to make. */
    static constexpr double kSwitchFadeMs = 10.0;

    /** Sets the rate and starts from rest. The first setSettings() after
        this takes its switches as found rather than fading them in. */
    void prepare (double newSampleRate) noexcept;

    /** Clears the filter state. The cuts stay as they are switched. */
    void reset() noexcept;

    /** Recompute coefficients. Cheap enough to call at control rate (see
        DspCore, which calls it once per sub-block from smoothed values).
        A cut switched on or off crosses over in kSwitchFadeMs.

        `gainRampSamples` is how many of this network's samples the next call
        will be: the band gains move to their new values in a straight line
        over them, a step per sample, rather than all at once. Zero, the
        default, sets them at once, as a curve display wants. */
    void setSettings (const EqSettings&, int gainRampSamples = 0) noexcept;

    float processSample (float x) noexcept;

    /** Complex response of the whole section, filters included. Shared by the
        curve display and the auto-gain calculation so neither can drift from
        the audio path. */
    std::complex<double> responseAt (double frequencyHz) const noexcept;

    double magnitudeDbAt (double frequencyHz) const noexcept;

    /** Broadband insertion gain, as a linear factor, for auto-gain
        compensation. Log-spaced mean of the magnitude response. */
    double broadbandGain() const noexcept;

private:
    static constexpr int kNumBands = 3;

    enum Band { low = 0, mid = 1, high = 2 };

    double sampleRate = 44100.0;

    // Shelving branches are mostly first order with a measured amount of
    // second order blended in; the mid is a fully resonant branch, as the LC
    // tank in the original is. See Svf.h.
    ShelfBranch lowBranch, highBranch;
    Svf         midBranch;

    std::array<float, kNumBands> gain      { 1.0f, 1.0f, 1.0f };   // g_i, in use this sample
    std::array<float, kNumBands> gainRecip { 1.0f, 1.0f, 1.0f };   // 1/g_i, in use this sample

    // Where the gains are going, the per-sample step there, and how many
    // samples are left. The targets are exactly what the settings ask for,
    // and the response the curve and Auto Gain read is theirs.
    std::array<float, kNumBands> gainTarget      { 1.0f, 1.0f, 1.0f };
    std::array<float, kNumBands> gainRecipTarget { 1.0f, 1.0f, 1.0f };
    std::array<float, kNumBands> gainStep        { 0.0f, 0.0f, 0.0f };
    std::array<float, kNumBands> gainRecipStep   { 0.0f, 0.0f, 0.0f };
    int gainRampLeft = 0;

    // Both filters are 18 dB/octave, so both are third order: a real pole plus
    // a complex pair.
    OnePole hpf1;
    Svf     hpf2;
    OnePole lpf1;
    Svf     lpf2;

    // Whether each cut is switched in: the response the curve and Auto Gain
    // read. The audio follows through hpfMix / lpfMix, 0 out and 1 in, which
    // cross from the unfiltered signal to the filtered one and back, so the
    // filter keeps running until its fade out has finished.
    bool hpfActive = false;
    bool lpfActive = false;

    bmo::dsp::Ramp hpfMix, lpfMix;
    bool takeSwitchesAsFound = true;
};

} // namespace bmo::eq
