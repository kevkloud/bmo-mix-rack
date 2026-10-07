#pragma once

#include "core/dsp/Svf.h"
#include "modules/reverb/dsp/EqNodes.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace bmo::reverb
{

//==============================================================================
/** **What the room is given: a fixed 20 Hz high-pass, DARKEN, then the three
    Reverb EQ nodes, in that order, ahead of both generators** (10 section 2).

    It runs on the one signal both generators are fed -- the mid of the input
    -- so it is one channel whatever the bus, and it never touches the dry
    path: the dry signal is the input, unfiltered and undelayed, which is what
    keeps 11 section 6's null at MIX 50 % with the faders off.

    ## The three parts

    - **The high-pass** is one pole at `kHighPassHz`, fixed, with no control.
      It keeps DC and subsonic rumble out of a loop that can hold them for
      forty seconds.
    - **DARKEN** (`inhicut`) is one pole, 1-20 kHz. Its coefficient is solved
      so the corner is -3.01 dB at the knob's frequency at every rate
      (`darkenCoefFor`); the usual 1 - exp(-w) puts the corner a third of an
      octave low at 20 kHz / 48 kHz. **It is not transparent at the top of its
      range**: at 20 kHz it is 3 dB down there, as a one-pole is. That is the
      knob's own law and not a fault to fix with a bypass at the end stop --
      a bypass there would be a step in the response one detent wide.
    - **The Reverb EQ** is `EqNodes::design`'s three biquads, each run on the
      suite's state-variable filter (`core/dsp/Svf.h`) so that its state means
      the same thing while its coefficients move.

    ## A flat node is exact, and its filter still runs

    A node with no gain reaching it designs to numerator == denominator, and
    such a node's output mix is written as (1, 0, 0) rather than as what the
    closed form rounds to, so it **returns its input bit for bit** -- not
    through a filter that is unity to fourteen places. At the schema defaults
    the EQ therefore adds nothing at all. The node's filter still runs, so
    that its state is the running signal's when a gain move starts: a filter
    woken on a stale or an empty state plays that state's settling into the
    room.

    ## Moves

    DARKEN's coefficient and each node's five SVF coefficients travel a
    straight line to a new design over `kGlideMs`, sample by sample, and land
    exactly. A line between two stable SVFs is stable at every point
    (`core/dsp/Svf.h`), which a direct-form biquad cannot promise. A new
    request part-way through starts a new line from wherever the filter is.
    The count is in samples, never blocks, so the output is the same at every
    block size for the same requests at the same sample.

    Everything here is double: the high-pass's pole is 0.9974 at 48 kHz and
    0.99935 at 192 kHz, and the late network already paid for learning what
    float coefficients do that close to one (10 section 4, "As built").
*/
class InputStage
{
public:
    /** The fixed high-pass corner, Hz (10 section 2). */
    static constexpr double kHighPassHz = 20.0;

    /** How long a coefficient move takes. 10 section 5's figure for every
        coefficient change. */
    static constexpr double kGlideMs = 20.0;

    /** States are flushed to zero below 1e-30 every this many samples, on an
        absolute count so it cannot depend on the host's block size. Without
        it the two poles decay through the subnormal doubles for seconds after
        the input stops. */
    static constexpr int kFlushInterval = 64;

    //== The one-pole laws ======================================================

    /** The coefficient `c` of y += c (x - y) whose response is exactly
        -3.01 dB at `hz`. From |H|^2 = 1/2 with pole b = 1 - c:
        b = (2 - cos w) - sqrt ((2 - cos w)^2 - 1). */
    static double onePoleCoefFor (double hz, double rate) noexcept
    {
        const auto w = 2.0 * dsp::kPi * std::clamp (hz, 1.0, rate * 0.49) / rate;
        const auto k = 2.0 - std::cos (w);
        return std::clamp (1.0 - (k - std::sqrt (k * k - 1.0)), 1.0e-9, 1.0);
    }

    /** |H(f)| of that low-pass, in dB. */
    static double lowPassDbAt (double coef, double hz, double rate) noexcept
    {
        const auto b = 1.0 - coef;
        const auto w = 2.0 * dsp::kPi * hz / rate;
        return 20.0 * std::log10 (coef / std::sqrt (1.0 - 2.0 * b * std::cos (w) + b * b));
    }

    /** |H(f)| of the high-pass built from it, x - lowpass (x), in dB. */
    static double highPassDbAt (double coef, double hz, double rate) noexcept
    {
        const auto b = 1.0 - coef;
        const auto w = 2.0 * dsp::kPi * hz / rate;
        const auto num = b * std::sqrt (2.0 - 2.0 * std::cos (w));
        return 20.0 * std::log10 (std::max (num, 1.0e-300) / std::sqrt (1.0 - 2.0 * b * std::cos (w) + b * b));
    }

    /** DARKEN's coefficient for a knob frequency. */
    static double darkenCoefFor (double hz, double rate) noexcept { return onePoleCoefFor (hz, rate); }

    /** The high-pass's low-pass half. The high-pass is x minus this, so its
        own -3 dB point sits where this one's does only while the corner is
        far below the rate, which 20 Hz is at every rate a host offers: the
        high-pass is within 0.02 dB of -3.01 at 20 Hz from 44.1 to 192 kHz,
        which `reverb_dsp_tests` asserts. */
    static double highPassCoefFor (double rate) noexcept { return onePoleCoefFor (kHighPassHz, rate); }

    //==========================================================================

    void prepare (double newSampleRate)
    {
        rate = newSampleRate > 0.0 ? newSampleRate : 48000.0;
        glideSamples = std::max (1, (int) std::lround (kGlideMs * 0.001 * rate));
        hpCoef = highPassCoefFor (rate);
        designed = false;
        reset();
    }

    /** Empties the filters and lands every coefficient on its request: what
        follows a reset starts from silence, so there is nothing for a move
        to be smooth against. */
    void reset() noexcept
    {
        hpState = lpState = 0.0;
        flushPhase = 0;
        started = false;
        land();
    }

    /** The request. Designs nothing when nothing changed, so it can be called
        every block. Before the first sample after prepare() or reset() the
        request lands; after it, it is a move. */
    void set (const EqSettings& s, float darkenHz, const dsp::DesignGrid& grid) noexcept
    {
        if (designed && darkenHz == lastDarkenHz && same (s, lastSettings))
            return;

        const auto eq = EqNodes::design (s, grid);

        for (int i = 0; i < kNumEqNodes; ++i)
        {
            auto& n = nodes[(size_t) i];
            const auto& q = eq.node[(size_t) i];

            n.target = dsp::SvfCoeffs::fromBiquad (q);

            // Numerator equal to denominator is `designMatched`'s exact unity
            // case, and the mix is written as the identity it is rather than
            // as what the closed form rounds to.
            if (q.b0 == 1.0 && q.b1 == q.a1 && q.b2 == q.a2)
            {
                n.target.m0 = 1.0;
                n.target.m1 = n.target.m2 = 0.0;
            }

            startMove (n);
        }

        darkTarget = darkenCoefFor ((double) darkenHz, rate);
        darkLeft = glideSamples;
        darkStep = (darkTarget - darkCoef) / (double) glideSamples;

        lastSettings = s;
        lastDarkenHz = darkenHz;
        designed = true;

        if (! started)
            land();
    }

    /** In place, one channel. */
    void process (float* buffer, int numSamples) noexcept
    {
        started = true;

        if (bypassed)
            return;

        for (int i = 0; i < numSamples; ++i)
        {
            auto x = (double) buffer[i];

            hpState += hpCoef * (x - hpState);
            x -= hpState;

            if (darkLeft > 0)
            {
                darkCoef += darkStep;

                if (--darkLeft == 0)
                    darkCoef = darkTarget;
            }

            lpState += darkCoef * (x - lpState);
            x = lpState;

            for (auto& n : nodes)
            {
                if (n.left > 0)
                {
                    n.cur.g  += n.step.g;   n.cur.k  += n.step.k;
                    n.cur.m0 += n.step.m0;  n.cur.m1 += n.step.m1;  n.cur.m2 += n.step.m2;

                    if (--n.left == 0)
                        n.cur = n.target;

                    n.taps = dsp::SvfTaps::of (n.cur.g, n.cur.k);
                }

                // A flat node's mix is (1, 0, 0), so this is x exactly; see
                // the class comment for why its filter runs anyway.
                x = n.state.process (n.taps, n.cur, x);
            }

            buffer[i] = (float) x;

            if (++flushPhase >= kFlushInterval)
            {
                flushPhase = 0;

                if (std::abs (hpState) < 1.0e-30) hpState = 0.0;
                if (std::abs (lpState) < 1.0e-30) lpState = 0.0;

                for (auto& n : nodes)
                    n.state.flushTiny();
            }
        }
    }

    /** True while any coefficient is still travelling. */
    bool isMoving() const noexcept
    {
        if (darkLeft > 0)
            return true;

        for (const auto& n : nodes)
            if (n.left > 0)
                return true;

        return false;
    }

    /** DARKEN's coefficient as it is this sample, for the tests. */
    double darkenCoefNow() const noexcept { return darkCoef; }

    /** **For the tests and the measurement tool, never for a host.** With the
        stage out, what `DspCore` plays is the two generators on the bare
        input, which is the condition 11 section 6's early-reflection block
        was written in: tap gains read off DC sums, tap times to the sample.
        Neither survives a high-pass and a 20 kHz pole in front, and neither
        is a claim about them. Nothing outside `tests/` and `tools/` calls
        this, and there is no parameter that reaches it. */
    void setBypassedForMeasurement (bool shouldBypass) noexcept { bypassed = shouldBypass; }

private:
    struct Node
    {
        dsp::SvfCoeffs cur, target, step { 0.0, 0.0, 0.0, 0.0, 0.0 };
        dsp::SvfTaps taps = dsp::SvfTaps::of (1.0, 2.0);
        dsp::SvfState state;
        int left = 0;
    };

    void startMove (Node& n) const noexcept
    {
        const auto inv = 1.0 / (double) glideSamples;

        n.step.g  = (n.target.g  - n.cur.g)  * inv;
        n.step.k  = (n.target.k  - n.cur.k)  * inv;
        n.step.m0 = (n.target.m0 - n.cur.m0) * inv;
        n.step.m1 = (n.target.m1 - n.cur.m1) * inv;
        n.step.m2 = (n.target.m2 - n.cur.m2) * inv;
        n.left = glideSamples;
    }

    void land() noexcept
    {
        darkCoef = darkTarget;
        darkLeft = 0;

        for (auto& n : nodes)
        {
            n.cur = n.target;
            n.left = 0;
            n.taps = dsp::SvfTaps::of (n.cur.g, n.cur.k);
            n.state.reset();
        }
    }

    static bool same (const EqSettings& a, const EqSettings& b) noexcept
    {
        return a.filter == b.filter
            && a.loFreqHz  == b.loFreqHz  && a.loDb  == b.loDb  && a.loQ  == b.loQ
            && a.midFreqHz == b.midFreqHz && a.midDb == b.midDb && a.midQ == b.midQ
            && a.hiFreqHz  == b.hiFreqHz  && a.hiDb  == b.hiDb  && a.hiQ  == b.hiQ;
    }

    std::array<Node, kNumEqNodes> nodes;

    double rate = 48000.0;
    int glideSamples = 960;

    double hpCoef = 0.0, hpState = 0.0;
    double darkCoef = 1.0, darkTarget = 1.0, darkStep = 0.0, lpState = 0.0;
    int darkLeft = 0;

    EqSettings lastSettings;
    float lastDarkenHz = 0.0f;
    bool designed = false;
    bool started = false;
    bool bypassed = false;
    int flushPhase = 0;
};

} // namespace bmo::reverb
