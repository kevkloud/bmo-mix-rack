#pragma once

#include "core/dsp/ModuleDsp.h"
#include "modules/dim/dsp/DspCore.h"
#include "modules/dim/params.h"

namespace bmo::dim
{

class DimDsp final : public ModuleDsp
{
public:
    void prepare (double sampleRate, int maxBlockSize, int numChannels) override
    {
        core.prepare (sampleRate, maxBlockSize, numChannels);
    }

    void reset() override { core.reset(); }

    void setParams (const float* v, int count) override
    {
        if (count < Index::count)
            return;

        DspCore::Params p;
        p.widthPercent     = v[width];
        p.shuffleAmount    = v[shuffle];
        p.shuffleFreqHz    = v[shuffleFreq];
        p.detuneCents      = v[detune];
        p.detuneOn         = v[detuneOn] > 0.5f;
        p.diffusePercent   = v[diffuse];
        p.rateHz           = v[rate];
        p.depthPercent     = v[depth];
        p.rotationDegrees  = v[rotation];
        p.asymmetryPercent = v[asymmetry];
        p.outputDb         = v[output];

        core.setParams (p);
    }

    void process (float* const* channels, int numChannels, int numSamples) override
    {
        core.process (channels, numChannels, numSamples);
    }

    /** Zero, in every configuration, including with the detune stage running.

        This looked like it would be the awkward one: a pitch shifter needs a
        window, and a window is samples. But the mid path through this module is
        a plain wire, and the detune voices only ever *add* to the side signal --
        nothing the host receives is a delayed copy of what it sent. There is no
        alignment for PDC to restore, so reporting a latency here would push the
        whole track early against a module that does not need it.

        That also settles what bypassing the detune stage does to the reported
        figure, which was an open question while the answer looked non-zero: it
        does nothing, because the figure is zero either way and never changes
        mid-session. */
    int latencyForParams (const float*, int) const override { return 0; }

    DspCore& getCore() noexcept { return core; }

private:
    DspCore core;
};

inline std::unique_ptr<ModuleDsp> createDsp() { return std::make_unique<DimDsp>(); }

} // namespace bmo::dim
