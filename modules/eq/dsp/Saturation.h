#pragma once

#include "Svf.h"
#include <algorithm>
#include <cmath>

namespace bmo::eq
{

//==============================================================================
/** log(cosh(u)), accurate at both ends.

    The obvious stable-for-large-u form, |u| + log1p(exp(-2|u|)) - log 2, is
    catastrophic for small u: the last two terms cancel to the true value of
    u^2/2, which for u near zero is far below the rounding error of either.
    That matters more than it looks, because ADAA divides the difference of two
    antiderivatives by a small dx and so amplifies any error in them by 1/dx.
    With the naive form this plugin had a constant error floor around 5e-4 --
    inaudible at full scale, but 66 % "distortion" on a -60 dBFS signal.

    Writing the correction as log1p(expm1(-2a)/2) keeps both limits exact:
    expm1 and log1p are precisely the routines that do not lose the small
    difference, and at a = 0 the whole term is exactly zero.
*/
inline double logCosh (double u) noexcept
{
    const auto a = std::abs (u);
    return a + std::log1p (std::expm1 (-2.0 * a) * 0.5);
}

//==============================================================================
/** Asymmetric soft saturation with first-order antiderivative anti-aliasing.

    shape(x) = (tanh(a*x + b) - tanh(b)) / a

    The bias b is what makes this useful. A plain tanh is an odd function and
    produces only odd harmonics; offsetting the operating point breaks that
    symmetry and brings in even ones, second harmonic foremost. That is the
    signature of a single-ended class-A stage, and a large part of why these
    units are described as warm rather than merely distorted -- the second
    harmonic is an octave, so it stays consonant with whatever produced it.

    ADAA: rather than evaluating the nonlinearity pointwise, which generates
    harmonics above Nyquist that fold back as inharmonic rubbish, integrate it
    and take the difference quotient,

        y[n] = (F(x[n]) - F(x[n-1])) / (x[n] - x[n-1])

    which is the average of the shaping function over the segment the signal
    actually traversed. tanh has a closed-form antiderivative, log(cosh(.)), so
    this costs little more than the shaper itself.
*/
class Saturator
{
public:
    void setDrive (float newDrive) noexcept
    {
        drive = std::max ((double) newDrive, 1.0e-3);
    }

    void setAsymmetry (float newBias) noexcept
    {
        bias     = newBias;
        tanhBias = std::tanh ((double) newBias);

        // The shaper's slope at the origin is sech^2(bias), which is less than
        // one for any offset. Without this the chain would quietly lose level
        // as asymmetry was added, and every A/B would be confounded by it.
        const auto c = std::cosh ((double) newBias);
        gainComp = c * c;
    }

    void reset() noexcept
    {
        previousX = 0.0;
        previousR = residualAntiderivative (0.0);
    }

    float process (float x) noexcept
    {
        const double xd = x;
        const auto r  = residualAntiderivative (xd);
        const auto dx = xd - previousX;

        // The difference quotient is ill-conditioned when the signal barely
        // moves; fall back to the residual at the midpoint.
        const auto correction = std::abs (dx) > 1.0e-9 ? (r - previousR) / dx
                                                       : residual (0.5 * (xd + previousX));

        previousX = xd;
        previousR = r;
        return (float) (xd + correction);
    }

    double shape (double x) const noexcept
    {
        return gainComp * (std::tanh (drive * x + bias) - tanhBias) / drive;
    }

private:
    /** Only the nonlinear part is anti-aliased.

        Applied to the whole shaper, the ADAA difference quotient costs real
        high-frequency response. Where the shaper is locally linear the
        quotient reduces exactly to (x[n] + x[n-1]) / 2 -- a two-point moving
        average, magnitude cos(pi*f/fs). That is -3 dB at 12 kHz for a 48 kHz
        chain, and with four saturating stages in series it measured -12 dB
        there: the plugin was quietly acting as a tone control.

        Splitting f(x) = x + residual(x) and anti-aliasing only the residual
        leaves the linear path untouched, so a signal below the knee passes
        with its treble intact. The residual is the distortion itself, so the
        slight smoothing it still receives does no harm.
    */
    double residual (double x) const noexcept
    {
        return shape (x) - x;
    }

    double residualAntiderivative (double x) const noexcept
    {
        return antiderivative (x) - 0.5 * x * x;
    }

    double antiderivative (double x) const noexcept
    {
        const auto u = drive * x + bias;
        return gainComp * (logCosh (u) - tanhBias * u) / (drive * drive);
    }

    double drive = 1.0, bias = 0.0, tanhBias = 0.0, gainComp = 1.0;
    double previousX = 0.0, previousR = 0.0;
};

//==============================================================================
/** First-order shelf built from a one-pole low-pass, used in exactly invertible
    pairs for the transformer's emphasis and de-emphasis.

        H(s) = (1 + m*s/w) / (1 + s/w)

    Unity at DC, m at high frequency. The inverse is the same structure with
    corner w/m and mix 1/m, so the pair cancels to unity wherever the core is
    not saturating -- the transformer colours without tilting the response.
*/
class ShelfBlend
{
public:
    void set (double cornerHz, float highFrequencyGain, double sampleRate) noexcept
    {
        mix = highFrequencyGain;
        pole.setCutoff (std::clamp (cornerHz, 1.0, sampleRate * 0.45), sampleRate);
    }

    void reset() noexcept { pole.reset(); }

    float process (float x) noexcept
    {
        return pole.process (OnePole::Output::lowpass, x) * (1.0f - mix) + x * mix;
    }

private:
    OnePole pole;
    float   mix = 1.0f;
};

//==============================================================================
/** Transformer.

    The important property, and the one a plain waveshaper cannot reproduce, is
    that a transformer distorts low frequencies far harder than high ones at the
    same voltage. Core flux is the integral of applied voltage, so for a fixed
    level the flux a signal produces falls as 1/f: bass drives the core towards
    its knee while treble never gets near it. Vendor 15's own figures for the
    line transformer used in these units show it plainly -- 0.1 % at
    40 Hz against 0.01 % at 1 kHz and 10 kHz, all at +20 dB in.

    So the signal is pre-emphasised into a flux-like domain, saturated there,
    and de-emphasised by the exact inverse. Below the knee the pair cancels and
    the transformer is transparent; above it, the low end compresses and grows
    harmonics while the top stays clean.

    The emphasis is shelved rather than a true integrator: a real core's
    response to flux is not unbounded, and 26 dB of tilt between 25 Hz and
    500 Hz puts the 40 Hz / 1 kHz distortion ratio at very nearly the 10:1
    Vendor 15 measured.
*/
class TransformerStage
{
public:
    void prepare (double newSampleRate) noexcept
    {
        sampleRate = newSampleRate;

        // 26 dB of tilt, hinged at 25 Hz.
        constexpr double kEmphasisHz = 25.0;
        constexpr float  kTilt       = 0.05f;          // -26 dB

        emphasis  .set (kEmphasisHz,           kTilt,        sampleRate);
        deEmphasis.set (kEmphasisHz / kTilt,   1.0f / kTilt, sampleRate);

        // Finite primary inductance rolls off the bottom. Two of these in the
        // chain land within the unit's quoted +/-0.5 dB at 20 Hz.
        lowRolloff.setCutoff (5.0, sampleRate);

        // Leakage inductance and winding capacitance roll off the top, but the
        // corner is far above the audio band and cannot be represented at base
        // rate. Its in-band effect is a few tenths of a dB, so it is simply
        // omitted unless the chain is running oversampled.
        highRolloffActive = kHighRolloffHz < sampleRate * 0.4;

        if (highRolloffActive)
            highRolloff.setCutoff (kHighRolloffHz, sampleRate);

        core.setAsymmetry (0.02f);   // cores are near symmetric; the amps are not
        reset();
    }

    void reset() noexcept
    {
        emphasis.reset();
        deEmphasis.reset();
        lowRolloff.reset();
        highRolloff.reset();
        core.reset();
    }

    void setDrive (float newDrive) noexcept
    {
        fluxDrive = newDrive;
        core.setDrive (1.0f);
    }

    float process (float x) noexcept
    {
        x = lowRolloff.process (OnePole::Output::highpass, x);

        const auto flux = emphasis.process (x) * fluxDrive;
        const auto saturated = core.process (flux) / fluxDrive;

        auto y = deEmphasis.process (saturated);

        if (highRolloffActive)
            y = highRolloff.process (OnePole::Output::lowpass, y);

        return y;
    }

private:
    static constexpr double kHighRolloffHz = 62000.0;   // pair gives -3 dB at 40 kHz

    double sampleRate = 48000.0;

    ShelfBlend emphasis, deEmphasis;
    OnePole    lowRolloff, highRolloff;
    Saturator  core;

    float fluxDrive = 1.0f;
    bool  highRolloffActive = false;
};

//==============================================================================
/** Single-ended class-A discrete gain stage.

    Asymmetric by construction, so second harmonic dominates -- this, not the
    transformers, is where most of the even-order content comes from.
*/
class ClassAStage
{
public:
    void prepare (double) noexcept { reset(); }

    void setAsymmetry (float bias) noexcept { stage.setAsymmetry (bias); }

    void reset() noexcept { stage.reset(); }

    void setDrive (float newDrive) noexcept
    {
        drive = newDrive;
        stage.setDrive (newDrive);
    }

    float process (float x) noexcept { return stage.process (x); }

private:
    Saturator stage;
    float drive = 1.0f;
};

} // namespace bmo::eq
