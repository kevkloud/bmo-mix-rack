#pragma once

#include "modules/tune/dsp/SincTable.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <vector>

namespace bmo::dwell
{

inline constexpr double kPiD = 3.14159265358979323846;

//==============================================================================
/** One-pole parameter smoother, the shape used in modules/dim, modules/sat and
    modules/opto -- see those for why it snaps once inside epsilon. */
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

        if (std::abs (target - current) < 1.0e-9f)
            current = target;

        return current;
    }

    float value() const noexcept { return current; }

private:
    float coeff = 1.0f, current = 0.0f, target = 0.0f;
};

//==============================================================================
/** A TPT one-pole, prewarped, carrying its own frequency response in closed
    form.

    The response lives beside the filter rather than in the sweep that uses it
    because docs/delay/10 §3 requires `P_c` to be taken from **the coefficients
    the loop actually runs**, so that it cannot drift from whatever a CALIBRATE
    pass lands on. One `g = tan(pi fc / fs)` feeds both the difference equation
    and the magnitude, and there is no second copy of the corner to get wrong.

    `g` depends only on `fc / fs`, which is what makes every coefficient here
    sample-rate invariant across 44.1-192 kHz (10 §4). */
class TptOnePole
{
public:
    void setCutoff (double cutoffHz, double sampleRate) noexcept
    {
        const auto fc = std::clamp (cutoffHz, 0.1, 0.49 * sampleRate);
        gCoeff = std::tan (kPiD * fc / std::max (sampleRate, 1.0));
        gNorm  = gCoeff / (1.0 + gCoeff);
    }

    void reset() noexcept { state = 0.0; }

    double lowPass (double x) noexcept
    {
        const auto v = (x - state) * gNorm;
        const auto y = v + state;
        state = y + v;

        // A one-pole decaying toward zero on silence is the textbook denormal
        // source. FTZ is set for the block as well (ScopedNoDenormals), but
        // this is what makes "silence in, exact zeros out" true rather than
        // merely quiet.
        if (std::abs (state) < 1.0e-25)
            state = 0.0;

        return y;
    }

    double highPass (double x) noexcept { return x - lowPass (x); }

    double coeff() const noexcept { return gCoeff; }

    /** |H(e^jw)| of the low-pass built from `g`. */
    static double lowPassMagnitude (double g, double omega) noexcept
    {
        const auto z = std::polar (1.0, -omega);
        return std::abs ((g * (1.0 + z)) / ((1.0 + g) + (g - 1.0) * z));
    }

    /** |H(e^jw)| of the high-pass built from `g`, i.e. 1 - the low-pass. */
    static double highPassMagnitude (double g, double omega) noexcept
    {
        const auto z = std::polar (1.0, -omega);
        return std::abs ((1.0 - z) / ((1.0 + g) + (g - 1.0) * z));
    }

private:
    double gCoeff = 0.0, gNorm = 0.0, state = 0.0;
};

//==============================================================================
/** **BMO Dwell's delay engine: one type, instantiated twice.**

    docs/delay/10 §11.1 makes this a structural requirement rather than a
    style preference. The module holds two of these -- the main delay and the
    lane -- and **nothing in here knows which one it is**. An engine is handed
    a time, a character and a loop gain; what feeds it, what reads it, how its
    gain was arrived at and whether a ducker touches its output are all the
    caller's business. The point is that Dwell can later be split into a plain
    delay and a throw delay without redoing the expensive part, and that every
    invariant in `11` §4 is asserted against one piece of code exercised twice.

    The feedback **law** is deliberately outside: §3's `g = 1.05 fb^1.6 / P_c`
    and §11.2's bipolar lane law are two different maps onto the same loop
    gain, and an engine that knew both would be an engine that knew which
    instance it was. What the engine owns is `P_c` itself -- it is a property
    of this engine's filters, interpolator and TIME -- which it computes by
    sweep and publishes through `referenceLoopPeak()` for the caller's law to
    divide by.

    **Stage 2a: clean only.** The ring, the fractional read, the crossfade
    time-change law, the reference sweep and the safety clip are here. Tape,
    bucket-brigade, modulation, the DRIVE shaper and the FX stage are 2b; the
    character chain is written so they drop into `character()` without the
    loop around them moving.

    Latency is 0 and stays 0: no oversampling, no lookahead, and the wet delay
    time is not latency (10 §0, `00` §4).

    `input` and `output` must not alias -- the engine reads its input after it
    has written the sample's output. */
class DelayEngine
{
public:
    using Sinc = bmo::tune::SincTable<32>;

    static constexpr int kMaxChannels = 2;

    /** The sweep grid of 10 §3: "a log grid of at least 512 points from 10 Hz
        to 0.45 f_s". 1024 is taken rather than the floor because the grid is
        built once at `prepare` and the extra resolution is free at the point
        where it matters -- resolving a peak that sits between two points as
        lower than it is would make the loop hotter than the law intends. */
    static constexpr int kSweepPoints = 1024;

    /** The read is taken before the sample's own write, so the newest valid
        sample is `writeIdx - 1`: the sinc's `kHalf` taps of headroom hold from
        `D >= kHalf + 1` rather than `D >= kHalf`. 10 §1's figure is the same
        bound written against a write-first ordering. */
    static constexpr int kSincFloor = Sinc::kHalf + 1;

    /** Hermite reads `floor(pos) - 1 ... floor(pos) + 2`, so it needs three
        samples of headroom. TIME bottoms out at 1 ms, which is 44 samples at
        the lowest rate the suite supports, so this clamp is a guard rather
        than a working limit. */
    static constexpr double kMinDelaySamples = 3.0;

    //==========================================================================
    /** What an engine is told. Note what is **not** here: which instance it
        is, what feeds it, and how its gain was arrived at. */
    struct Params
    {
        float timeMs    = 375.0f;   ///< the requested delay, in ms
        int   character = 0;        ///< 0 clean; 1 tape and 2 bucket-brigade are 2b
    };

    //==========================================================================
    /** Allocates both the ring and every grid the sweep reads, from
        `maxTimeMs` and **never from a parameter** (10 §10). At 192 kHz that is
        524 288 samples a channel -- 2.0 MB -- and 0.5 MB at 44.1 kHz. */
    void prepare (double newSampleRate, int /*maxBlockSize*/, int numChannels, double maxTimeMs)
    {
        sampleRate = newSampleRate > 0.0 ? newSampleRate : 48000.0;
        channels   = std::clamp (numChannels, 1, kMaxChannels);

        maxDelay = (int) std::ceil (std::max (maxTimeMs, 1.0) * 0.001 * sampleRate);

        int size = 1;
        while (size < maxDelay + kRingGuard)
            size <<= 1;

        mask = size - 1;

        for (auto& line : ring)
            line.assign ((size_t) size, 0.0f);

        sinc.build (1.0, 8.0);
        probe.assign ((size_t) kProbeSize, 0.0f);

        gridOmega.assign ((size_t) kSweepPoints, 0.0);
        gridHz.assign ((size_t) kSweepPoints, 0.0);
        filterMagnitude.assign ((size_t) kSweepPoints, 1.0);
        kernelMagnitude.assign ((size_t) kSweepPoints, 1.0);

        const auto lo = 10.0;
        const auto hi = std::max (0.45 * sampleRate, lo * 2.0);

        for (int i = 0; i < kSweepPoints; ++i)
        {
            const auto f = lo * std::pow (hi / lo, (double) i / (double) (kSweepPoints - 1));
            gridHz[(size_t) i]    = f;
            gridOmega[(size_t) i] = 2.0 * kPiD * f / sampleRate;
        }

        fadeLength = std::max (1, (int) std::lround (sampleRate * kCrossfadeSeconds));

        // 30 ms on the feedback gain, 10 §9. TIME is not smoothed -- §2's
        // crossfade owns it.
        feedback.prepare (sampleRate, 30.0);

        primed = false;
        gainPrimed = false;

        buildFilters();
        applyTime (params.timeMs, true);
        reset();
    }

    /** Clears audio state and leaves the coefficients, `P_c` and the targets
        alone: a reset is a silence, not a re-tune. */
    void reset() noexcept
    {
        for (auto& line : ring)
            std::fill (line.begin(), line.end(), 0.0f);

        for (auto& f : filters)
        {
            f.lowCut.reset();
            f.highCut.reset();
            f.blocker.reset();
        }

        writeIdx = 0;
        fadeCounter = -1;
        delayCurrent = delayNext = delayTarget;
    }

    /** TIME and CHARACTER. Re-sweeps `P_c` whenever either moves, per 10 §3,
        which is why the caller must set these **before** reading
        `referenceLoopPeak()` to build its gain.

        `snapNow` takes the new time without §2's crossfade -- what `prepare`
        wants, and nothing else: a delay that faded in from its default on
        every insert would be a delay that ignored the session it was asked
        for. */
    void setParams (const Params& p, bool snapNow = false) noexcept
    {
        const auto characterMoved = (p.character != params.character) || ! primed || snapNow;
        params = p;

        if (snapNow)
            primed = false;

        if (characterMoved)
            buildFilters();

        applyTime (p.timeMs, characterMoved);
    }

    /** The loop gain the caller's law arrived at. Smoothed at 30 ms (10 §9),
        snapped on the first call after `prepare` so a freshly placed instance
        is not ramping up from zero. */
    void setFeedbackGain (float g, bool snapNow = false) noexcept
    {
        if (! gainPrimed || snapNow)
        {
            feedback.snap (g);
            gainPrimed = true;
            return;
        }

        feedback.setTarget (g);
    }

    /** `P_c` -- the maximum of |H(e^jw) . I(e^jw)| over 10 Hz to 0.45 f_s,
        with this engine's built filter coefficients and its built interpolator
        kernel at this engine's own TIME (10 §3, §11.2). **Computed by sweep,
        never hardcoded.** Near 0.9988 on clean. */
    double referenceLoopPeak() const noexcept { return loopPeak; }

    /** Where in the band that peak sits. Not used by the loop -- it is what a
        listening or CALIBRATE pass needs in order to drive the loop at the one
        frequency the unity claim is about (10 §3: "unity means the loudest
        band neither grows nor decays"). */
    double referencePeakHz() const noexcept { return loopPeakHz; }

    /** The ring's length in samples: a power of two, sized from the fixed
        maximum. */
    int ringSize() const noexcept { return mask + 1; }

    /** The delay the engine is reading now, in samples. Whole-sample times
        give a whole number here, which is what makes |I| exactly 1. */
    double currentDelaySamples() const noexcept { return delayCurrent; }

    //==========================================================================
    /** One block. Allocates nothing: every buffer and every grid came from
        `prepare`.

        The loop is 10 §3's `v[n] = x[n] + g . C(y[n])` with **no input gate**
        -- the `s` term is gone, not repurposed (§11, §11.1) -- and the engine's
        output is the raw read `y`, tapped before the character chain, so
        colour accumulates one pass per lap. */
    void process (const float* const* input, float* const* output, int numChannels, int numSamples) noexcept
    {
        if (mask <= 0 || numSamples <= 0 || numChannels <= 0)
            return;

        const auto nch = std::min (numChannels, channels);

        for (int n = 0; n < numSamples; ++n)
        {
            const auto gain = (double) feedback.tick();

            // §2's clean law: the old tap freezes, a new one starts at the
            // target, equal-power raised cosine across it. No pitch bend; the
            // cost is a momentary doubling. Tape and BBD's glide is 2b and
            // replaces these two lines, not the loop around them.
            const auto fading = fadeCounter >= 0;
            auto fadeOld = 1.0, fadeNew = 0.0;

            if (fading)
            {
                const auto u = (double) fadeCounter / (double) fadeLength;
                fadeOld = std::cos (0.5 * kPiD * u);
                fadeNew = std::sin (0.5 * kPiD * u);
            }

            for (int ch = 0; ch < nch; ++ch)
            {
                const auto* line = ring[(size_t) ch].data();

                auto y = readAt (line, delayCurrent);

                if (fading)
                    y = fadeOld * y + fadeNew * readAt (line, delayNext);

                output[ch][n] = (float) y;

                auto& f = filters[(size_t) ch];
                const auto c = character (f, y);

                const auto x = (double) input[ch][n];
                auto v = (std::isfinite (x) ? x : 0.0) + gain * c;

                // A NaN that reached the ring would circulate for ever, so it
                // is stopped at the write rather than at the output.
                if (! std::isfinite (v))
                    v = 0.0;

                if (std::abs (v) < 1.0e-25)
                    v = 0.0;

                ring[(size_t) ch][(size_t) writeIdx] = (float) v;
            }

            for (int ch = nch; ch < numChannels; ++ch)
                output[ch][n] = 0.0f;

            writeIdx = (writeIdx + 1) & mask;

            if (fading && ++fadeCounter >= fadeLength)
            {
                fadeCounter  = -1;
                delayCurrent = delayNext;

                // A target that arrived mid-fade is picked up here rather than
                // interrupting the fade in flight.
                if (delayTarget != delayCurrent)
                {
                    delayNext = delayTarget;
                    fadeCounter = 0;
                }
            }
        }
    }

private:
    //==========================================================================
    struct ChannelFilters
    {
        TptOnePole lowCut, highCut, blocker;
    };

    /** 10 §4's loop order: LOW CUT -> HIGH CUT -> mode filters -> DC blocker
        -> shaper -> clip.

        **Stage 2a runs the user stages at their neutral limits** -- LOW CUT
        20 Hz, HIGH CUT at the 18 kHz cap -- which is exactly the reference
        §3 defines `P_c` against, so the running loop's peak *is* `P_c` and
        unity lands where the law says it does. 2b wires the two parameters to
        these same filters; the reference stays at the neutral limits either
        way, and a user's cut can then only shorten the tail.

        There is no mode filter and no shaper yet (clean, DRIVE 0). The safety
        clip is not optional and is not 2b: it is what makes the top of
        FEEDBACK's travel a limit cycle instead of a divergence (§3, §11.6),
        and it is on regardless of DRIVE. */
    double character (ChannelFilters& f, double y) const noexcept
    {
        auto c = f.lowCut.highPass (y);
        c = f.highCut.lowPass (c);
        c = f.blocker.highPass (c);
        return std::tanh (c);
    }

    double readAt (const float* line, double delay) const noexcept
    {
        const auto pos = (double) writeIdx - delay;

        // 10 §1: 32-tap polyphase Kaiser sinc on clean -- unity gain at every
        // phase and an exact delay at whole samples, so the repeat chain
        // accumulates no phase-dependent HF loss. Below its headroom, and for
        // the character modes in 2b, 4-point 3rd-order Hermite.
        if (delay >= (double) kSincFloor)
            return (double) sinc.read (line, mask, pos);

        return hermite (line, pos);
    }

    double hermite (const float* line, double pos) const noexcept
    {
        const auto whole = std::floor (pos);
        const auto f = pos - whole;
        const auto i = (int) (long long) whole;

        const auto at = [line, this] (int k) noexcept
        {
            return (double) line[(size_t) (k & mask)];
        };

        const auto ym1 = at (i - 1), y0 = at (i), y1 = at (i + 1), y2 = at (i + 2);

        const auto c0 = y0;
        const auto c1 = 0.5 * (y1 - ym1);
        const auto c2 = ym1 - 2.5 * y0 + 2.0 * y1 - 0.5 * y2;
        const auto c3 = 0.5 * (y2 - ym1) + 1.5 * (y0 - y1);

        return ((c3 * f + c2) * f + c1) * f + c0;
    }

    /** The four Hermite coefficients as a kernel, for the sweep. */
    static void hermiteKernel (double f, double* k) noexcept
    {
        k[0] = -0.5 * f +       f * f - 0.5 * f * f * f;   // y[-1]
        k[1] =  1.0     - 2.5 * f * f + 1.5 * f * f * f;   // y[0]
        k[2] =  0.5 * f + 2.0 * f * f - 1.5 * f * f * f;   // y[+1]
        k[3] =          - 0.5 * f * f + 0.5 * f * f * f;   // y[+2]
    }

    //==========================================================================
    void buildFilters() noexcept
    {
        // The neutral limits of 10 §3's reference, which 2a also runs: LOW CUT
        // on its 20 Hz rail, HIGH CUT at min(18 kHz, 0.45 f_s), and the 10 Hz
        // DC blocker in. The 18 kHz cap is load-bearing -- it keeps 2b's
        // shaper away from Nyquist, where first-order ADAA is weakest.
        const auto lowCutHz  = 20.0;
        const auto highCutHz = std::min (18000.0, 0.45 * sampleRate);
        const auto blockerHz = 10.0;

        for (auto& f : filters)
        {
            f.lowCut.setCutoff (lowCutHz, sampleRate);
            f.highCut.setCutoff (highCutHz, sampleRate);
            f.blocker.setCutoff (blockerHz, sampleRate);
        }

        referenceLowCut  = filters[0].lowCut.coeff();
        referenceHighCut = filters[0].highCut.coeff();
        referenceBlocker = filters[0].blocker.coeff();
    }

    void applyTime (float timeMs, bool filtersMoved) noexcept
    {
        const auto target = std::clamp ((double) timeMs * 0.001 * sampleRate,
                                        kMinDelaySamples, (double) std::max (maxDelay, 3));

        if (! primed)
        {
            delayCurrent = delayNext = delayTarget = target;
            fadeCounter  = -1;
            primed = true;
            refreshLoopPeak (true);
            return;
        }

        if (target != delayTarget)
        {
            delayTarget = target;

            if (fadeCounter < 0)
            {
                delayNext = target;
                fadeCounter = 0;
            }

            refreshLoopPeak (filtersMoved);
        }
        else if (filtersMoved)
        {
            refreshLoopPeak (true);
        }
    }

    /** 10 §3's sweep. `P_c` is re-taken on any change of character, TIME or
        sample rate and at no other time -- `setParams` runs on the audio
        thread, so the two grids it reads are cached and only the part that
        actually moved is rebuilt. The interpolator's part is rebuilt only once
        the read phase has moved by more than the table's own 1/256-sample
        quantisation, below which there is nothing new to measure. */
    void refreshLoopPeak (bool filtersMoved) noexcept
    {
        if (filtersMoved)
            buildFilterMagnitudes();

        if (filtersMoved || std::abs (delayTarget - kernelDelay) > (1.0 / 256.0))
            buildKernelMagnitudes (delayTarget);

        auto peak = 0.0;
        auto peakHz = gridHz.empty() ? 0.0 : gridHz[0];

        for (int i = 0; i < (int) filterMagnitude.size(); ++i)
        {
            const auto m = filterMagnitude[(size_t) i] * kernelMagnitude[(size_t) i];

            if (m > peak)
            {
                peak = m;
                peakHz = gridHz[(size_t) i];
            }
        }

        loopPeak   = std::max (peak, 1.0e-6);
        loopPeakHz = peakHz;
    }

    void buildFilterMagnitudes() noexcept
    {
        for (int i = 0; i < (int) filterMagnitude.size(); ++i)
        {
            const auto w = gridOmega[(size_t) i];

            filterMagnitude[(size_t) i] = TptOnePole::highPassMagnitude (referenceLowCut,  w)
                                        * TptOnePole::lowPassMagnitude  (referenceHighCut, w)
                                        * TptOnePole::highPassMagnitude (referenceBlocker, w);
        }
    }

    /** |I(e^jw)| for the interpolator **as built**, not as specified.

        The sinc's coefficients live inside `SincTable`, so the kernel is taken
        back out of it by reading a unit impulse at the phase the loop will
        actually read at -- 32 reads of a 128-sample scratch ring, off any
        audio. That is the difference between sweeping the table that ships and
        sweeping a second copy of the formula that built it. */
    void buildKernelMagnitudes (double delay) noexcept
    {
        kernelDelay = delay;

        // The read position is `writeIdx - delay` and writeIdx is an integer,
        // so the phase the table sees is the fraction of -delay.
        const auto negative = -delay;
        const auto phase = negative - std::floor (negative);

        std::array<double, Sinc::kTaps> kernel {};
        int taps = 0;

        if (delay >= (double) kSincFloor)
        {
            taps = Sinc::kTaps;

            const auto probePos = (double) kProbeCentre + phase;
            const auto base = kProbeCentre - Sinc::kHalf + 1;

            for (int k = 0; k < taps; ++k)
            {
                std::fill (probe.begin(), probe.end(), 0.0f);
                probe[(size_t) (base + k)] = 1.0f;
                kernel[(size_t) k] = (double) sinc.read (probe.data(), kProbeSize - 1, probePos);
            }
        }
        else
        {
            taps = 4;
            hermiteKernel (phase, kernel.data());
        }

        for (int i = 0; i < (int) kernelMagnitude.size(); ++i)
        {
            const auto step = std::polar (1.0, -gridOmega[(size_t) i]);
            std::complex<double> power { 1.0, 0.0 };
            std::complex<double> acc { 0.0, 0.0 };

            for (int k = 0; k < taps; ++k)
            {
                acc += kernel[(size_t) k] * power;
                power *= step;
            }

            kernelMagnitude[(size_t) i] = std::abs (acc);
        }
    }

    //==========================================================================
    /** Room for the interpolator's taps past the longest delay, so the oldest
        tap can never wrap onto the newest write. */
    static constexpr int kRingGuard = Sinc::kTaps + 4;

    /** The scratch ring the kernel is probed out of. 128 holds all 32 taps
        around the centre without wrapping. */
    static constexpr int kProbeSize = 128;
    static constexpr int kProbeCentre = 32;

    /** 20 ms, 10 §12's clean crossfade. */
    static constexpr double kCrossfadeSeconds = 0.020;

    Params params;

    double sampleRate = 48000.0;
    int channels = 2, maxDelay = 0, mask = 0, writeIdx = 0;

    std::array<std::vector<float>, kMaxChannels> ring;
    std::array<ChannelFilters, kMaxChannels> filters;

    Sinc sinc;
    std::vector<float> probe;

    Smoother feedback;
    bool primed = false, gainPrimed = false;

    double delayCurrent = 0.0, delayNext = 0.0, delayTarget = 0.0;
    int fadeCounter = -1, fadeLength = 1;

    std::vector<double> gridOmega, gridHz, filterMagnitude, kernelMagnitude;
    double referenceLowCut = 0.0, referenceHighCut = 0.0, referenceBlocker = 0.0;
    double kernelDelay = -1.0;
    double loopPeak = 1.0, loopPeakHz = 0.0;
};

} // namespace bmo::dwell
