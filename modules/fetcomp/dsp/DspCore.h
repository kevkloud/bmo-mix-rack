#pragma once

#include "core/dsp/Oversampler.h"
#include "modules/fetcomp/params.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace bmo::fetcomp
{

//==============================================================================
/** Which curve family the ratio buttons select, and the one that is not a
    ratio at all.

    `allButtons` is every button pushed in together: a different curve with a
    standing reduction of its own, a very high effective slope near threshold
    and a plateau beyond it. It is in this enum rather than in a separate flag
    because it is one of the five states the one control has. */
enum class Ratio { four = 0, eight, twelve, twenty, allButtons };

/** Two constant sets over one topology, gain-matched, identical latency. */
enum class Voicing { blue = 0, black };

/** The knob positions, in the units the coefficients actually want.

    Derived here rather than stored, so nothing can hold a stale time after a
    position moves, and so the laws in modules/fetcomp/params.h stay the only
    definition of what a position means. */
inline float attackSecondsFor (float position) noexcept
{
    return attackMicrosecondsFor (position) * 1.0e-6f;
}

inline float releaseSecondsFor (float position) noexcept
{
    return releaseMillisecondsFor (position) * 1.0e-3f;
}

//==============================================================================
/** One-pole parameter smoother, the same shape as the ones in modules/sat/dsp,
    modules/opto/dsp and modules/vcomp/dsp -- see any of them for why it snaps
    to the target inside an epsilon, so a settled parameter compares exactly
    equal.

    INPUT, OUTPUT and MIX each get one. All three are steady knobs most of the
    time and all three are automatable, and stepping a gain block to block with
    no ramp is an audible zipper. 20 ms, per docs/1176-comp/10-dsp-spec.md 10. */
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

private:
    float coeff = 1.0f, current = 0.0f, target = 0.0f;
};

//==============================================================================
/** BMO FET -- **placeholder core. There is no compressor in this file yet.**

    What is here is the frame the real one is written into, and everything it
    does is deliberately trivial: INPUT gain, OUTPUT gain, the MIX blend, and a
    delay of exactly the length `latencyForParams` reports. No detector, no FET
    cell, no divider law, no nonlinearity, no voicing difference, and
    `currentGainReductionDb()` is a flat zero.

    **What must survive the real implementation** -- the contract the rest of
    the module is already built against:

    - `Params` below carries every parameter `specs()` has, in real units, and
      the adapter (FetcompDsp.h) is the only thing that unpacks the flat array.
      Add DSP state here, not parameters.
    - Latency is 0 / 40 / 60 samples at Off / 2x / 4x and **0 at the default**,
      it is `Oversampler::latencyForFactor` and nothing hand-written, and it is
      the same in both voicings.
    - The dry path of MIX is delay-matched to the wet one. Here that is one
      shared line both paths pass through; once the cell is oversampled the wet
      path carries the oversampler's own round trip and the dry path takes a
      line of exactly that length. Undelayed, a partial blend combs -- two
      copies ~1.2 kHz apart in notch spacing at 48 k -- and `mix` = 0 stops
      nulling, which is also how bypass is proven.
    - The ring is cleared when the factor changes. The EQ review records what
      going without costs: the stages reset, the ring does not, and it clicks
      and misaligns for up to 70 samples.
    - `currentGainReductionDb()` is **signed, positive = gain taken away**
      (core/dsp/ModuleDsp.h), and it keeps reporting the true figure past the
      24 dB the meter pins at. The clamp is a drawing limit, not a measurement
      one.

    The spec is docs/1176-comp/10-dsp-spec.md; what the tests will ask of it is
    docs/1176-comp/11-integration-and-test-plan.md 3. */
class DspCore
{
public:
    struct Params
    {
        float   inputDb         = 0.0f;
        float   outputDb        = 0.0f;
        float   attackPosition  = kPositionDefault;   ///< 1..7, 7 fastest
        float   releasePosition = kPositionDefault;   ///< 1..7, 7 fastest
        Ratio   ratio           = Ratio::four;
        float   mixPercent      = 100.0f;
        Voicing voicing         = Voicing::black;
        int     oversampling    = 1;                  ///< the factor, 1 / 2 / 4
    };

    void prepare (double newSampleRate, int /*maxBlockSize*/, int numChannels) noexcept
    {
        rate = newSampleRate;
        numActiveChannels = std::clamp (numChannels, 1, (int) delays.size());

        inputGain .prepare (rate, kSmoothingMs);
        outputGain.prepare (rate, kSmoothingMs);
        mix       .prepare (rate, kSmoothingMs);

        inputGain .snap (gainFor (params.inputDb));
        outputGain.snap (gainFor (params.outputDb));
        mix       .snap (params.mixPercent * 0.01f);

        setDelay (latencyFor (params.oversampling));
        reset();
    }

    void reset() noexcept
    {
        for (auto& d : delays)
        {
            std::fill (d.begin(), d.end(), 0.0f);
        }

        writeIndex = 0;
    }

    void setParams (const Params& p) noexcept
    {
        // A factor change re-primes the delay line rather than leaving a ring
        // full of samples that belong to a different alignment.
        if (p.oversampling != params.oversampling)
        {
            setDelay (latencyFor (p.oversampling));
            reset();
        }

        params = p;

        inputGain .setTarget (gainFor (p.inputDb));
        outputGain.setTarget (gainFor (p.outputDb));
        mix       .setTarget (p.mixPercent * 0.01f);
    }

    void process (float* const* channels, int numChannels, int numSamples) noexcept
    {
        const auto active = std::clamp (numChannels, 1, numActiveChannels);

        for (int n = 0; n < numSamples; ++n)
        {
            // One tick per sample for the whole block, not per channel: the
            // three gains are shared, and ticking them per channel would run
            // the ramp at twice the rate in stereo.
            const auto in  = inputGain .tick();
            const auto out = outputGain.tick();
            const auto wet = mix       .tick();

            for (int ch = 0; ch < active; ++ch)
            {
                const auto dry = channels[ch][n];

                // The whole of the placeholder: gain in, nothing, gain out.
                const auto processed = dry * in * out;
                const auto blended   = wet * processed + (1.0f - wet) * dry;

                channels[ch][n] = pushPop ((size_t) ch, blended);
            }

            advance();
        }

        // Anything past the channels prepare() sized for is left alone. This
        // is a stereo module; a host that hands over more gets them back
        // untouched rather than half-processed.
    }

    /** Delay this core adds at the base rate, for a factor. Whole samples by
        construction -- see core/dsp/Oversampler.h. */
    static int latencyFor (int factor) noexcept
    {
        return Oversampler::latencyForFactor (factor);
    }

    /** **Placeholder: always zero.** The real core reports the reduction it is
        applying, signed, positive = gain taken away, and keeps reporting the
        true figure past the meter's 24 dB pin. */
    float currentGainReductionDb() const noexcept { return 0.0f; }

    const Params& getParams() const noexcept { return params; }

private:
    static constexpr double kSmoothingMs = 20.0;
    static constexpr int kMaxChannels = 2;

    /** Sized from the oversampler's worst case rather than from the factors
        this module offers: stating the number by hand got it wrong once
        elsewhere in the tree and overran the buffer. One extra slot, because
        the write and the read are a whole `length` apart. */
    static constexpr size_t kLineLength = (size_t) Oversampler::kMaxLatency + 1;

    static float gainFor (float db) noexcept { return std::pow (10.0f, db * 0.05f); }

    void setDelay (int samples) noexcept
    {
        delaySamples = std::clamp (samples, 0, (int) kLineLength - 1);
    }

    /** Writes `v` and returns what was written `delaySamples` ago. At zero it
        hands `v` straight back, so the default state is a wire. */
    float pushPop (size_t channel, float v) noexcept
    {
        if (delaySamples == 0)
            return v;

        auto& line = delays[channel];
        line[writeIndex] = v;

        const auto read = (writeIndex + kLineLength - (size_t) delaySamples) % kLineLength;
        return line[read];
    }

    void advance() noexcept
    {
        if (delaySamples != 0)
            writeIndex = (writeIndex + 1) % kLineLength;
    }

    Params params;
    double rate = 48000.0;
    int numActiveChannels = kMaxChannels;

    Smoother inputGain, outputGain, mix;

    std::array<std::array<float, kLineLength>, kMaxChannels> delays {};
    size_t writeIndex = 0;
    int delaySamples = 0;
};

} // namespace bmo::fetcomp
