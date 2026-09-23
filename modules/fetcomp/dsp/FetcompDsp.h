#pragma once

#include "core/dsp/ModuleDsp.h"
#include "modules/fetcomp/dsp/DspCore.h"
#include "modules/fetcomp/params.h"

namespace bmo::fetcomp
{

/** The oversampling detent as the factor it names. Off / 2x / 4x -- there is
    no 8x here, unlike the Saturator's row: the FET cell is what costs, and
    docs/fet-comp/10-dsp-spec.md 9 stops at 4x. */
inline int oversamplingFactor (int index) noexcept
{
    constexpr int factors[] { 1, 2, 4 };
    return factors[index < 0 ? 0 : (index > 2 ? 2 : index)];
}

/** The adapter: it unpacks the flat parameter array into DspCore::Params in
    `Index` order and does nothing else. Every mapping from a host value to a
    DSP unit is in `setParams` below, so there is one place to read to find out
    what the DSP is actually given. */
class FetcompDsp final : public ModuleDsp
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
        p.inputDb         = v[input];
        p.outputDb        = v[output];
        p.attackPosition  = v[attack];
        p.releasePosition = v[release];
        p.ratio           = ratioFor ((int) v[ratio]);
        p.mixPercent      = v[mix];
        p.voicing         = v[voicing] > 0.5f ? Voicing::black : Voicing::blue;
        p.oversampling    = oversamplingFactor ((int) v[oversampling]);

        core.setParams (p);
    }

    void process (float* const* channels, int numChannels, int numSamples) override
    {
        core.process (channels, numChannels, numSamples);
    }

    /** 0 / 40 / 60 samples at Off / 2x / 4x, zero at the default, and the same
        in both voicings -- the shared oversampler's own figure, computed from
        the parameter values rather than from the core's state so the host can
        be told before the audio thread has picked the change up. */
    int latencyForParams (const float* v, int count) const override
    {
        return count > oversampling
                 ? DspCore::latencyFor (oversamplingFactor ((int) v[oversampling]))
                 : 0;
    }

    /** The panel's VU reads this through ModuleEngine. Signed, positive = gain
        taken away; the placeholder core reports a flat zero. */
    float currentGainReductionDb() const noexcept override { return core.currentGainReductionDb(); }

    DspCore& getCore() noexcept { return core; }

private:
    static Ratio ratioFor (int index) noexcept
    {
        switch (index)
        {
            case ratio8:   return Ratio::eight;
            case ratio12:  return Ratio::twelve;
            case ratio20:  return Ratio::twenty;
            case ratioAll: return Ratio::allButtons;
            default:       return Ratio::four;
        }
    }

    DspCore core;
};

inline std::unique_ptr<ModuleDsp> createDsp() { return std::make_unique<FetcompDsp>(); }

} // namespace bmo::fetcomp
