#pragma once

#include "core/dsp/ModuleDsp.h"
#include "modules/opto/dsp/DspCore.h"
#include "modules/opto/params.h"

namespace bmo::opto
{

class OptoDsp final : public ModuleDsp
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
        p.crushPercent = v[crush];
        p.levelDb      = v[level];
        p.mode         = v[mode] > 0.5f ? Mode::CompUnitB : Mode::OptoUnitA;
        p.link         = v[link]  > 0.5f;
        p.color        = v[color] > 0.5f;

        core.setParams (p);
    }

    void process (float* const* channels, int numChannels, int numSamples) override
    {
        core.process (channels, numChannels, numSamples);
    }

    /** No lookahead, no oversampling: nothing here ever costs the host a
        sample of delay. */
    int latencyForParams (const float*, int) const override { return 0; }

    /** Not part of ModuleDsp -- BMO Opto's own contract with ModuleEngine,
        which polls this once per block for the gain-reduction meter. See
        core/dsp/ModuleDsp.h's currentGainReductionDb() default. */
    float currentGainReductionDb() const noexcept override { return core.currentGainReductionDb(); }

    DspCore& getCore() noexcept { return core; }

private:
    DspCore core;
};

inline std::unique_ptr<ModuleDsp> createDsp() { return std::make_unique<OptoDsp>(); }

} // namespace bmo::opto
