#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace bmo::dim
{

inline constexpr float kPi = 3.14159265358979323846f;

/** One-pole parameter smoother, same shape as the ones in modules/sat/dsp and
    modules/opto/dsp -- see those for why it snaps once inside epsilon. */
class Smoother
{
public:
    void prepare (double sampleRate, double timeMs) noexcept
    {
        const auto tau = std::max (timeMs, 0.01) * 0.001;
        coeff = (float) (1.0 - std::exp (-1.0 / (std::max (sampleRate, 1.0) * tau)));
    }

    void snap (float v) noexcept      { current = target = v; }
    void setTarget (float t) noexcept { target = t; }

    float tick() noexcept
    {
        current += coeff * (target - current);

        if (std::abs (target - current) < 1.0e-6f)
            current = target;

        return current;
    }

    float value() const noexcept { return current; }

private:
    float coeff = 1.0f, current = 0.0f, target = 0.0f;
};

//==============================================================================
/** A crossfading delay-line pitch shifter, one voice.

    A read pointer moving at a rate other than one sample per sample is a pitch
    shift, and it is also a pointer that eventually runs into the writer. The
    standard fix, and the one here: run two taps half a window apart and
    crossfade between them with raised cosines that sum to one, so whichever
    tap is about to wrap is the one being faded out.

    The window is the whole compromise. Short windows make the crossfade
    audible as flutter; long ones smear transients. 30 ms is the usual landing
    point for a detuner at these depths.

    This costs no *reported* latency, which is worth being explicit about: the
    mid path is a wire and this voice only ever adds to the side signal, so
    there is nothing for the host to compensate. The window delay is part of
    the effect, not a delay through the module. See DimDsp::latencyForParams.
*/
class DetuneVoice
{
public:
    void prepare (double sampleRate)
    {
        window = std::max (64, (int) std::lround (sampleRate * 0.030));
        buffer.assign ((size_t) window * 2 + 4, 0.0f);
        reset();
    }

    void reset()
    {
        std::fill (buffer.begin(), buffer.end(), 0.0f);
        writeIdx = 0;
        phase = 0.0f;
    }

    /** Back to the start of the sweep, keeping what the buffer holds. Two
        voices that have been fed the same input and are restarted together
        produce identical output until their opposite detunes pull them apart
        -- which is what lets DETUNE come back in instantly. See
        DspCore::setParams. */
    void restart() noexcept { phase = 0.0f; }

    /** cents > 0 shifts up, < 0 down. */
    void setCents (float cents) noexcept
    {
        // A pitch ratio is 2^(cents/1200); the read pointer has to drift by the
        // difference from unity, so that is what the phase accumulates.
        const auto ratio = std::pow (2.0f, cents / 1200.0f);
        phaseInc = (1.0f - ratio) / (float) std::max (window, 1);
    }

    float process (float x) noexcept
    {
        if (buffer.empty())
            return 0.0f;

        const auto len = (int) buffer.size();

        buffer[(size_t) writeIdx] = x;

        // Two taps, half a window apart, each faded by a raised cosine. The two
        // windows sum to exactly one, so a steady input comes out steady.
        auto tap = [this, len] (float ph) noexcept
        {
            const auto delay = ph * (float) window;
            const auto rd    = (float) writeIdx - delay;

            auto i0 = (int) std::floor (rd);
            const auto frac = rd - (float) i0;

            i0 %= len; if (i0 < 0) i0 += len;
            auto i1 = i0 + 1; if (i1 >= len) i1 -= len;

            return buffer[(size_t) i0] + frac * (buffer[(size_t) i1] - buffer[(size_t) i0]);
        };

        const auto ph2 = phase >= 0.5f ? phase - 0.5f : phase + 0.5f;
        const auto g1  = 0.5f * (1.0f - std::cos (2.0f * kPi * phase));
        const auto g2  = 0.5f * (1.0f - std::cos (2.0f * kPi * ph2));

        const auto out = g1 * tap (phase) + g2 * tap (ph2);

        phase += phaseInc;
        while (phase >= 1.0f) phase -= 1.0f;
        while (phase <  0.0f) phase += 1.0f;

        if (++writeIdx >= len)
            writeIdx = 0;

        return out;
    }

private:
    std::vector<float> buffer;
    int   window = 0, writeIdx = 0;
    float phase = 0.0f, phaseInc = 0.0f;
};

//==============================================================================
/** A cascade of first-order all-passes, coefficient swept from outside.

    Magnitude flat by construction: each stage only rotates phase. Sweeping the
    coefficient is what makes this a phaser rather than a fixed decorrelator,
    which is why BMO Dimension has no separate phaser stage -- they are the
    same object with the LFO connected.

    Six stages decorrelates audibly without the sweep turning into a comb
    whistle at high depth.
*/
class AllPassChain
{
public:
    static constexpr int kStages = 6;

    void reset() noexcept { state.fill (0.0f); }

    float process (float x, float a) noexcept
    {
        a = std::clamp (a, -0.95f, 0.95f);

        for (int i = 0; i < kStages; ++i)
        {
            const auto y = -a * x + state[(size_t) i];
            state[(size_t) i] = x + a * y;
            x = y;
        }

        return x;
    }

private:
    std::array<float, (size_t) kStages> state {};
};

//==============================================================================
/** Gerzon's bass shuffler, as a complementary one-pole split.

    The low band of the side signal is scaled and the high band is not, because
    the ears hear stereo as narrower in the bass than in the treble and the
    shuffler is the correction for it. Low and high sum back to the input
    exactly, so at unity this stage is a wire rather than an approximation of
    one -- which is what "fully phase compensated" has to mean if it is going
    to be asserted rather than hoped for.
*/
class Shuffler
{
public:
    void prepare (double sr) noexcept { sampleRate = std::max (sr, 1.0); reset(); }
    void reset() noexcept { z = 0.0f; }

    void setFrequency (float hz) noexcept
    {
        const auto f = std::clamp (hz, 20.0f, (float) (sampleRate * 0.45));
        coeff = 1.0f - std::exp (-2.0f * kPi * f / (float) sampleRate);
    }

    float process (float s, float amount) noexcept
    {
        z += coeff * (s - z);
        const auto high = s - z;
        return z * amount + high;
    }

private:
    double sampleRate = 44100.0;
    float  coeff = 0.1f, z = 0.0f;
};

//==============================================================================
/** BMO Dimension: split to mid/side, work on the side, sum back.

    Three stages in series on S -- generate, diffuse, image -- and a mid path
    that is a plain wire. Because L + R = 2M, anything done to S alone is
    invisible in the mono sum, so width, shuffle and diffuse cannot damage mono
    compatibility however they are set.

    **Rotation and asymmetry are the two exceptions, and they are deliberate.**
    Rotation turns the whole soundfield, which necessarily moves centre material
    off centre and therefore changes the mono sum; asymmetry adds a share of the
    side signal to the mid, and the S1's own manual says outright that it
    "changes relative balance of left & right both in stereo and in mono". Both
    are identity at their defaults, so a Dimension left alone is still
    mono-exact -- but a claim that the module is unconditionally mono-safe would
    be wrong once either is turned, and it is worth stating accurately rather
    than broadly.

    Note the two exceptions are not the same shape. Rotation moves a centre
    source off centre and is meant to. Asymmetry does not: a source with no side
    content passes it untouched, and only material that is already off centre
    changes level. The mono sum moves for the second reason, not the first.

    Mono instances return early. See process().
*/
class DspCore
{
public:
    struct Params
    {
        float widthPercent     = 100.0f;
        float shuffleAmount    = 1.0f;
        float shuffleFreqHz    = 700.0f;
        float detuneCents      = 10.0f;
        bool  detuneOn         = false;
        float diffusePercent   = 0.0f;
        float rateHz           = 0.40f;
        float depthPercent     = 50.0f;
        float rotationDegrees  = 0.0f;
        float asymmetryPercent = 0.0f;
    };

    void prepare (double sr, int, int)
    {
        sampleRate = std::max (sr, 1.0);

        up.prepare (sampleRate);
        down.prepare (sampleRate);
        shuffler.prepare (sampleRate);

        // 8 ms on the continuous controls: fast enough that a knob move feels
        // immediate, slow enough that an automated width sweep does not step.
        widthSm.prepare (sampleRate, 8.0);
        shuffleSm.prepare (sampleRate, 8.0);
        diffuseSm.prepare (sampleRate, 8.0);
        depthSm.prepare (sampleRate, 8.0);
        rotSm.prepare (sampleRate, 8.0);
        asymSm.prepare (sampleRate, 8.0);

        // DETUNE fades out on the same 8 ms, and comes back in instantly.
        // See setParams.
        detuneSm.prepare (sampleRate, 8.0);
        centsSm.prepare (sampleRate, 8.0);

        reset();
    }

    void reset()
    {
        up.reset();
        down.reset();
        chain.reset();
        shuffler.reset();
        lfoPhase = 0.0f;
        primed = false;
    }

    void setParams (const Params& p)
    {
        // DETUNE fades out and comes straight back in.
        //
        // Out: gated, switching the stage out dropped the voices' difference
        // in one sample -- and on a mono source that difference is the whole
        // side signal. Measured at the peak of the beat: a 0.49 step on a 0.5
        // tone, 32x the largest move the tone makes by itself. So it fades,
        // on the same 8 ms as every other control.
        //
        // In: instant, which is Frosty's call from the 2026-09-10 ear test.
        // What makes that safe is that the voices never stop -- process()
        // runs them with the stage in or out, so their buffers always hold
        // the last 30 ms of live audio. Both earlier ways of handling those
        // buffers clicked on the way in:
        //
        //   left stale     replayed a buffer filled a second earlier: 0.90
        //                  peak out of silence
        //   cleared to 0   the voices reached the edge of the cleared region
        //                  ~15 ms later, a few samples apart, so for those
        //                  samples one had signal and the other did not: a
        //                  0.18 step on a 0.5 tone, 11.7x. This was in the
        //                  build that passed the 2026-09-09 listening pass,
        //                  and nobody heard it either.
        //
        // With live buffers there is no edge. Restarting both voices at the
        // same point of their sweep makes them identical, so their difference
        // -- the width -- starts at exactly zero and grows as their opposite
        // detunes pull them apart. That is what lets the level jump straight
        // to full without a step.
        //
        // A re-engage that catches the tail of a fade-out is the exception.
        // The voices are mid-sweep and still contributing, so restarting them
        // would itself be a step; that case glides back up from wherever the
        // fade had got to.
        if (p.detuneOn && ! params.detuneOn && detuneSm.value() == 0.0f)
        {
            up.restart();
            down.restart();
            detuneSm.snap (1.0f);
        }

        params = p;

        detuneSm.setTarget (p.detuneOn ? 1.0f : 0.0f);

        widthSm  .setTarget (p.widthPercent * 0.01f);
        shuffleSm.setTarget (p.shuffleAmount);
        diffuseSm.setTarget (p.diffusePercent * 0.01f);
        depthSm  .setTarget (p.depthPercent * 0.01f);
        // Negated so the knob reads like a pan control: + moves the image
        // right, - moves it left. The S1 manual fixes the rotation law but
        // says nothing about which way the knob turns, so the sign is a free
        // choice -- and the unnegated form put +30 degrees to the LEFT, which
        // is backwards from every pan knob a user has ever touched. Confirmed
        // by ear on a stereo source, 2026-09-09.
        rotSm    .setTarget (-p.rotationDegrees * kPi / 180.0f);
        asymSm   .setTarget (asymCoeff (p.asymmetryPercent));

        // The two voices are opposed, so the pair sums back toward the centre
        // rather than pulling the whole image one way.
        up  .setCents ( p.detuneCents);
        down.setCents (-p.detuneCents);

        // CENTS at 0 has to mean off. A voice at 0 cents stops sweeping and
        // freezes wherever it was, so with DETUNE on the pair became two fixed
        // taps of the mid, differenced: a static comb whose level depended on
        // where the sweep had got to when the knob arrived (measured
        // 2026-09-14: after CENTS 10 -> 0, a side signal louder than at 10).
        // The injected difference is scaled by the first cent of the knob
        // instead, on the same 8 ms as everything else, so the bottom of the
        // range fades to exactly nothing.
        centsSm.setTarget (std::clamp (std::abs (p.detuneCents), 0.0f, 1.0f));

        shuffler.setFrequency (p.shuffleFreqHz);

        lfoInc = (float) (std::max (p.rateHz, 0.0f) / sampleRate);

        if (! primed)
        {
            widthSm.snap (p.widthPercent * 0.01f);
            shuffleSm.snap (p.shuffleAmount);
            diffuseSm.snap (p.diffusePercent * 0.01f);
            depthSm.snap (p.depthPercent * 0.01f);
            rotSm.snap (-p.rotationDegrees * kPi / 180.0f);   // sign: see setParams
            asymSm.snap (asymCoeff (p.asymmetryPercent));
            detuneSm.snap (p.detuneOn ? 1.0f : 0.0f);
            centsSm.snap (std::clamp (std::abs (p.detuneCents), 0.0f, 1.0f));
            primed = true;
        }
    }

    void process (float* const* channels, int numChannels, int numSamples)
    {
        // A stereo imager on a mono bus is a wire, and has to be left as one.
        // The host contract allows a mono instance -- see
        // SingleModuleProcessor::isBusesLayoutSupported -- and folding L into
        // R to fake a stereo pair puts every stage on a signal whose side is
        // zero by definition. The generate stage would then manufacture side
        // content and sum it straight back into the single channel, which is
        // the comb this module's whole topology exists to avoid: measured at
        // +1.17 dB and 0.67 of sample error before this guard.
        if (numChannels < 2 || channels[0] == nullptr || channels[1] == nullptr)
            return;

        auto* l = channels[0];
        auto* r = channels[1];

        for (int i = 0; i < numSamples; ++i)
        {
            const auto inL = l[i];
            const auto inR = r[i];

            auto mid  = 0.5f * (inL + inR);
            auto side = 0.5f * (inL - inR);

            // -- Generate ----------------------------------------------------
            // Detune reads the mid, because on a mono source that is the only
            // thing there. The two shifted voices differenced give side content
            // that did not exist a sample ago.
            //
            // The voices run whether the stage is in or out, so their buffers
            // are never stale and never empty -- see setParams for the two
            // clicks each of those caused. The level fades out and snaps
            // straight back in; at either end it is exactly 0 or 1, so a
            // settled stage is bit-identical to a gate.
            const auto upOut   = up.process (mid);
            const auto downOut = down.process (mid);
            const auto detuneGain = detuneSm.tick() * centsSm.tick();

            if (detuneGain > 0.0f)
                side += detuneGain * 0.5f * (upOut - downOut);

            // -- Diffuse -----------------------------------------------------
            const auto diffuse = diffuseSm.tick();
            const auto depth   = depthSm.tick();

            if (diffuse > 0.0f)
            {
                const auto lfo = std::sin (2.0f * kPi * lfoPhase);
                const auto a   = 0.55f + 0.40f * depth * lfo;
                const auto wet = chain.process (side, a);
                side += diffuse * (wet - side);
            }

            lfoPhase += lfoInc;
            if (lfoPhase >= 1.0f)
                lfoPhase -= 1.0f;

            // -- Image -------------------------------------------------------
            side = shuffler.process (side, shuffleSm.tick());
            side *= widthSm.tick();

            // Rotation mixes mid and side, which is exactly what turning a
            // soundfield does -- and the one place the mono sum stops being 2M.
            // Identity at zero.
            const auto theta = rotSm.tick();

            if (theta != 0.0f)
            {
                const auto c = std::cos (theta), s = std::sin (theta);
                const auto m2 = mid * c - side * s;
                const auto s2 = mid * s + side * c;
                mid = m2; side = s2;
            }

            // Gerzon's asymmetry, taken from the S1 manual rather than guessed
            // at. Three sentences constrain it, and together they leave one
            // linear answer:
            //
            //   "does not affect central mono in-phase sounds in any way, but
            //    adjusts the relative level of left and right sounds"
            //   "differs from conventional balance control in that it keeps
            //    center sounds in the center"
            //   "changes relative balance of left & right both in stereo and
            //    in mono"
            //
            // Centre untouched means no mid-to-side term and a unity mid-to-mid
            // one; a balance that moves in mono means the side-to-mid term has
            // to survive. That is a shear: mid takes a share of side, side is
            // left alone. A centre source has side == 0, so it keeps both its
            // level and its position, and off-centre material changes level in
            // the stereo image and in the sum together.
            //
            // This replaces the unequal output trim it shipped as, which was
            // outL *= 1 + asym, outR *= 1 - asym. In mid/side that is
            // mid += asym * side AND side += asym * mid, and the second term is
            // exactly the one the manual rules out -- it manufactured side
            // content from centre material and moved a dead-centre 0.5/0.5
            // source to 0.75/0.25. It was a conventional balance control, which
            // is the one thing this control is defined as not being.
            //
            // No linear matrix can hold the centre and also pin hard-panned
            // material at the edges. What the shear does instead is widen the
            // side it attenuates -- 114 % at a quarter of the knob, 133 % at
            // half, 200 % at the top -- which on a module whose headline
            // control is WIDTH is its own vocabulary rather than a fault. The
            // mono sum stays positive and lands on the intended figure either
            // way; nothing cancels.
            //
            // Whether that widening reads as depth or as phasiness is the one
            // thing measurement could not settle, so it is on the Ableton
            // checklist. If it reads badly, the whole family is
            //
            //     mid  += a * side
            //     side += b * mid        // b is the free choice
            //
            // and b buys drift with width at a fixed rate. Measured, at half
            // knob, with all four moving the balance by the same -2.50 dB:
            //
            //     b = 0        centre  0.00 dB    far side 133 %   <- shipped
            //     b = a/2      centre +2.18 dB    far side 117 %
            //     b = a(a/aMax)^2   centre +1.09 dB    far side 125 %
            //     b = a        centre +4.44 dB    far side 100 %   <- was
            //
            // b = a/2 is the fallback. The quadratic is not: it keeps drift
            // out of the usable range, which is what it was written for, but
            // its width climbs to 126 % and then falls back to 100 % at the
            // top, so the knob undoes one of its own side effects near the
            // end. b = a is the balance control this shipped as, and it moves
            // the centre from the first quarter of the knob -- 0.87 dB at 10 %,
            // 2.18 at 25 % -- not only at the extremes.
            const auto asym = asymSm.tick();

            if (asym != 0.0f)
                mid += asym * side;

            l[i] = mid + side;
            r[i] = mid - side;
        }
    }

    /** The generate stage's current level: 0 out, 1 in, between while it
        fades out. Read-only, for tests -- it is what "instant on" is
        asserted against. */
    float detuneLevel() const noexcept { return detuneSm.value(); }

private:
    /** The knob's percentage as the shear coefficient, at half scale.

        The shear itself is unbounded and goes bad long before the knob would
        run out. It works by taking mid down as side goes up, and for a
        hard-panned source mid and side are equal -- so a coefficient of 1
        cancels that source's mid outright and leaves it as pure anti-phase
        content, which disappears in the mono sum rather than being reduced by
        it. Measured: 0.00/1.00 in, out -0.50/0.50, sum 0.000.

        Half scale puts the end of the knob at the point where fully-panned
        material on the disfavoured side is 6 dB down in the sum -- a lot of
        asymmetry, and still a signal. The knob keeps its frozen -100..+100 %
        range; only what the end of it means is set here.

        **Negated, so + favours the right** -- Frosty, 2026-09-16. With
        mid += a * side, a positive coefficient lifts material on the left
        (whose side is positive) and lowers the right, so the knob's + end
        leaned the image left: the opposite of ROTATE, and of every pan knob.
        It was caught when the panel was about to print an R at that end. The
        law is unchanged; only which end of the knob is which. The sign is a
        free choice, the same as rotation's, so DimDspTests pins it. A session
        that automated ASYM before this now leans the other way. */
    static float asymCoeff (float percent) noexcept
    {
        return -std::clamp (percent * 0.01f, -1.0f, 1.0f) * 0.5f;
    }

    double sampleRate = 44100.0;

    Params       params;
    DetuneVoice  up, down;
    AllPassChain chain;
    Shuffler     shuffler;

    Smoother widthSm, shuffleSm, diffuseSm, depthSm, rotSm, asymSm, detuneSm, centsSm;

    float lfoPhase = 0.0f, lfoInc = 0.0f;
    bool  primed = false;
};

} // namespace bmo::dim
