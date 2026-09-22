#pragma once

#include "ModuleDef.h"
#include "core/dsp/Meter.h"
#include "core/state/ParamSet.h"
#include <atomic>

namespace bmo
{

/** A module running: its DSP, the parameters it reads, and its meters.

    The standalone product owns one; the rack owns one per occupied slot.
    Parameter values are read once per block from the ParamSet, in spec
    order, into a pre-sized array, so the audio thread does no allocation and
    does not care whose parameter objects they are.

    Input and gain-reduction metering cost nothing for a module that never
    reads them -- `inputMeter` is measured unconditionally the same way
    `outputMeter` always was, and `gainReduction` is a single atomic float
    fed from `ModuleDsp::currentGainReductionDb()`, whose default is silence.
    Only BMO Opto's panel reads either today.
*/
class ModuleEngine
{
public:
    ModuleEngine (const ModuleDef& d, ParamSet p)
        : moduleDef (d), paramSet (std::move (p)), dsp (d.createDsp()),
          values ((size_t) d.numParams(), 0.0f)
    {
        jassert (paramSet.size() == moduleDef.numParams());

        // A module whose parameters write each other gets its link here and
        // nowhere else. **One engine exists per running module in both
        // products** -- the standalone's own, and one per occupied rack slot
        // -- so this single line covers both, with or without an editor open.
        // `core/state/ParamLink.h` carries the argument for why the engine and
        // not a panel; BMO Linger's TYPE is the only module that has one.
        //
        // After `paramSet` is in place, and bound to the engine's own copy: a
        // link keeps a reference to it for the engine's whole life, which is
        // what makes the ParamSet a member rather than something passed round.
        if (moduleDef.createParamLink != nullptr)
            paramLink = moduleDef.createParamLink (paramSet);
    }

    const ModuleDef& def() const noexcept    { return moduleDef; }
    ParamSet& params() noexcept              { return paramSet; }
    const ParamSet& params() const noexcept  { return paramSet; }
    const Meter& meter() const noexcept      { return outputMeter; }
    const Meter& inputMeter() const noexcept { return inMeter; }
    const GainReductionMeter& gainReduction() const noexcept { return grMeter; }

    /** The rate the module is running at, or 0 before the first prepare.

        Published for a panel that draws something rate-dependent -- BMO DEQ's
        response curve is designed at the running rate, and drew the 48 kHz
        design at every rate until 2026-09-15, which put it up to 1 dB out in
        the top octave at 44.1 and 96 k. Atomic because a panel's timer reads
        it while the message thread may be in prepare(). */
    double sampleRate() const noexcept { return rate.load (std::memory_order_relaxed); }

    void prepare (double sampleRateHz, int maxBlockSize, int numChannels)
    {
        // Parameters go in first: an oversampling choice sizes the buffers.
        read();
        dsp->setParams (values.data(), (int) values.size());
        dsp->prepare (sampleRateHz, maxBlockSize, numChannels);
        rate.store (sampleRateHz, std::memory_order_relaxed);
        outputMeter.reset();
        inMeter.reset();
        grMeter.reset();
    }

    void reset()
    {
        dsp->reset();
        outputMeter.reset();
        inMeter.reset();
        grMeter.reset();
    }

    void process (float* const* channels, int numChannels, int numSamples)
    {
        read();
        dsp->setParams (values.data(), (int) values.size());
        inMeter.measure (channels, numChannels, numSamples);
        dsp->process (channels, numChannels, numSamples);
        outputMeter.measure (channels, numChannels, numSamples);
        grMeter.publish (dsp->currentGainReductionDb());
    }

    /** Latency for the parameters as they are now. Safe from any thread. */
    int latency() const
    {
        std::vector<float> now ((size_t) paramSet.size());
        paramSet.readAll (now.data());
        return dsp->latencyForParams (now.data(), (int) now.size());
    }

    /** The tail for the parameters as they are now, in seconds. Safe from any
        thread.

        Here and not in the processors for `latency()`'s reason: the DSP is
        private to the engine, so neither processor can ask it directly, and
        neither should be growing its own copy of the read-all into a scratch
        array. Every module but BMO Linger takes `ModuleDsp`'s zero default. */
    double tailSeconds() const
    {
        std::vector<float> now ((size_t) paramSet.size());
        paramSet.readAll (now.data());
        return dsp->tailSecondsForParams (now.data(), (int) now.size());
    }

    /** Momentary, from the panel; -1 clears it. Not a parameter, so it is not
        in paramSet, not in a preset and not in a saved session. */
    void setSolo (int index) noexcept { dsp->setSolo (index); }

    /** The module's analyser window, or null if it has none. */
    AnalyserTap* analyser() noexcept { return dsp->analyser(); }

private:
    void read() noexcept { paramSet.readAll (values.data()); }

    const ModuleDef& moduleDef;
    ParamSet paramSet;

    /** Declared after `paramSet` and before everything it does not touch, so
        it is destroyed before the parameters it is listening to go away. */
    std::unique_ptr<ParamLink> paramLink;

    std::unique_ptr<ModuleDsp> dsp;
    std::vector<float> values;
    Meter outputMeter;
    Meter inMeter;
    GainReductionMeter grMeter;
    std::atomic<double> rate { 0.0 };
};

} // namespace bmo
