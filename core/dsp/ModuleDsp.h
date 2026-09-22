#pragma once

#include "core/dsp/AnalyserTap.h"

#include <memory>

namespace bmo
{

/** The audio side of a module, with no dependency on JUCE or on a host.

    A module's DSP is driven by an array of real-unit parameter values in the
    order of its ParamSpecs -- a dB figure, a detent index, 0 or 1 for a
    switch. The same adapter serves the standalone product, the rack, the
    tests and the measurement harness, which is the reason it takes an array
    rather than a host's parameter objects.
*/
class ModuleDsp
{
public:
    virtual ~ModuleDsp() = default;

    virtual void prepare (double sampleRate, int maxBlockSize, int numChannels) = 0;
    virtual void reset() = 0;

    /** Once per block, before process(). Cheap: stores targets only. */
    virtual void setParams (const float* values, int count) = 0;

    virtual void process (float* const* channels, int numChannels, int numSamples) = 0;

    /** Delay the module adds at the host's rate, for the current parameters.
        Computed from the values rather than from the DSP's state, so the host
        can be told about a change before the audio thread has picked it up. */
    virtual int latencyForParams (const float* values, int count) const = 0;

    /** How long the module keeps making sound after its input stops, in
        seconds, for the current parameters.

        Mirrors `latencyForParams` above deliberately: computed from the values
        rather than from DSP state, so the host can be told about a change
        before the audio thread has picked it up, and so a test can ask the
        question without preparing or running anything.

        **Defaulted to zero rather than pure**, unlike its neighbour. Every
        module that has shipped is a filter, a gain stage or a compressor;
        none of them rings on past its input, and BMO Linger is the first for
        which the honest answer is anything else. Making this pure would put
        an identical `return 0.0;` in eight adapters and in every module
        written after them -- eight places for one of them to drift. The
        default is the right answer for all eight, and
        tests/plugin/TailTests.cpp asserts it for all eight rather than
        trusting it.

        Both processors feed this into `getTailLengthSeconds()`, which a host
        may poll from any thread, so what they publish is cached in an atomic
        and refreshed on parameter change rather than computed in the getter. */
    virtual double tailSecondsForParams (const float* values, int count) const
    {
        (void) values;
        (void) count;
        return 0.0;
    }

    /** Gain this module is currently moving, in dB, **signed: positive is gain
        taken away, negative is gain added**.

        Called from the audio thread right after process(), same as a Meter's
        measure() -- see core/product/ModuleEngine.h. Only a dynamics module
        has anything to report here; the default is silence, so EQ/Sat/Util
        need no change to keep building.

        The sign was added 2026-09-15 for BMO DEQ, whose bands can expand
        upward as well as compress downward and which was reporting a flat
        zero for the upward half. A compressor -- BMO Opto, LTV Comp -- only
        ever cuts and so only ever returns >= 0, and nothing here requires the
        other sign; it is available to a module that needs it. */
    virtual float currentGainReductionDb() const noexcept { return 0.0f; }

    /** Hear one part of the module on its own, or -1 for the whole thing.

        **Momentary, and never a parameter.** It is set from the panel while a
        control is held and cleared when it is released, so it is not in the
        parameter set, not in a preset, not automatable and not saved with a
        session. A solo left on in a saved session is a support ticket.

        What an index means is the module's own business -- for BMO DEQ it is a
        band -- and the default here does nothing, so a module that has no such
        idea needs no change. This is the first path in the suite from a panel
        to the audio thread that is not a parameter: an atomic store on one
        side, a relaxed load on the other, no allocation and no lock.
    */
    virtual void setSolo (int) noexcept {}

    /** The module's analyser tap, or null if it has none. The panel reads the
        window; the DSP writes it. See core/dsp/AnalyserTap.h for why it cannot
        change the sound or the latency. */
    virtual AnalyserTap* analyser() noexcept { return nullptr; }
};

} // namespace bmo
