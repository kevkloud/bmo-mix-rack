#pragma once

#include "core/dsp/ModuleDsp.h"
#include "modules/tune/dsp/TuneCore.h"
#include "modules/tune/params.h"
#include <algorithm>
#include <memory>

namespace bmo::tune
{

/** BMO Tune RT behind the suite's ModuleDsp interface -- the same adapter
    shape as modules/dim/dsp/DimDsp.h in BMO Mix Rack, so a wrapper built on
    the rack's core/product drives it exactly as it drives any module.

    It needs nothing ModuleDsp does not carry -- the voice is the only input
    (no MIDI, no sidechain; Frosty, 2026-09-10) -- so the rack's own
    SingleModuleProcessor can host it unchanged. The core is mono: the first
    channel is processed and copied to the rest, since a tuner on a stereo
    channel is still correcting one voice.
*/
class TuneDsp final : public ModuleDsp
{
public:
    void prepare (double sampleRate, int maxBlockSize, int) override
    {
        core.prepare (sampleRate, maxBlockSize);
    }

    void reset() override { core.reset(); }

    void setParams (const float* v, int count) override
    {
        if (count < Index::count)
            return;

        core.setParams (TuneParams::fromValues (v, count));
    }

    void process (float* const* channels, int numChannels, int numSamples) override
    {
        if (numChannels <= 0 || numSamples <= 0)
            return;

        core.process (channels[0], numSamples);

        for (int ch = 1; ch < numChannels; ++ch)
            std::copy (channels[0], channels[0] + numSamples, channels[ch]);
    }

    /** Always 0: Live only. See TuneCore::kReportedLatency. */
    int latencyForParams (const float*, int) const override { return TuneCore::kReportedLatency; }

    TuneCore& getCore() noexcept { return core; }

private:
    TuneCore core;
};

inline std::unique_ptr<ModuleDsp> createDsp() { return std::make_unique<TuneDsp>(); }

} // namespace bmo::tune
