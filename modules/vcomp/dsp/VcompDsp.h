#pragma once

#include "core/dsp/ModuleDsp.h"
#include "modules/vcomp/dsp/DspCore.h"
#include "modules/vcomp/params.h"

namespace bmo::vcomp
{

class VcompDsp final : public ModuleDsp
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
        p.amountPercent = v[amount];
        p.gateDb        = v[gate];
        p.outputDb      = v[output];
        p.complex       = v[complex] > 0.5f;
        p.attackMs      = v[attack];
        p.releaseMs     = v[release];
        p.arc           = v[arc] > 0.5f;
        p.sidechainHz   = v[sidechain];
        p.lowThruHz     = v[lowThru];
        p.highThruHz    = v[highThru];

        core.setParams (p);
    }

    void process (float* const* channels, int numChannels, int numSamples) override
    {
        core.process (channels, numChannels, numSamples);
    }

    /** Zero, at every setting. There is no lookahead and no oversampling, so
        nothing here ever costs the host a sample of delay -- which is what
        lets it sit on a vocal while the singer is listening to it. Lookahead
        is the one thing a Pro-C-class compressor has that this deliberately
        does not; adding it later would change this line and the module's place
        in a tracking chain, so it is a product decision rather than a feature
        to slip in.

        The band split does not change this either: the crossover is IIR, so it
        costs phase rather than samples. That is the trade recorded in
        Crossover.h. */
    int latencyForParams (const float*, int) const override { return 0; }

    /** Polled once a block by ModuleEngine for the GR meter. See
        core/dsp/ModuleDsp.h's currentGainReductionDb() default. */
    float currentGainReductionDb() const noexcept override { return core.currentGainReductionDb(); }

    DspCore& getCore() noexcept { return core; }

private:
    DspCore core;
};

inline std::unique_ptr<ModuleDsp> createDsp() { return std::make_unique<VcompDsp>(); }

} // namespace bmo::vcomp
