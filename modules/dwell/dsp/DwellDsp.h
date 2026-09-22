#pragma once

#include "core/dsp/ModuleDsp.h"
#include "modules/dwell/dsp/DspCore.h"
#include "modules/dwell/params.h"

namespace bmo::dwell
{

/** The adapter between the suite's parameter array and BMO Dwell's core.

    `setParams` is the only place in the module that turns a spec index into a
    named value, which is why it is worth a test of its own: the schema is
    permanent, so a lane read off by one here would be wrong for the life of
    the product and would look like a DSP fault rather than a wiring one. */
class DwellDsp final : public ModuleDsp
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
        p.timeMs          = v[Index::time];
        p.sync            = kSyncIsEnabled && v[Index::sync] > 0.5f;
        p.noteChoice      = (int) v[Index::note];
        p.feedbackPct     = v[Index::feedback];
        p.characterChoice = (int) v[Index::character];
        p.stereoChoice    = (int) v[Index::stereo];
        p.lowCutHz        = v[Index::lowCut];
        p.highCutHz       = v[Index::highCut];
        p.voicePct        = v[Index::voice];
        p.modRateHz       = v[Index::modRate];
        p.modDepthPct     = v[Index::modDepth];
        p.drivePct        = v[Index::drive];
        p.duckDb          = v[Index::duck];
        p.mixPct          = v[Index::mix];
        p.throwHeld       = v[Index::throwHeld] > 0.5f;
        p.throwModeChoice = (int) v[Index::throwMode];
        p.freeze          = v[Index::freeze] > 0.5f;
        p.fx              = v[Index::fx] > 0.5f;
        p.fxTypeChoice    = (int) v[Index::fxType];
        p.fxAmountPct     = v[Index::fxAmount];

        core.setParams (p);
    }

    void process (float* const* channels, int numChannels, int numSamples) override
    {
        core.process (channels, numChannels, numSamples);
    }

    /** **Zero, at every setting, permanently.**

        There is no oversampling (docs/delay/10 §0 drops it outright, which is
        what pays for the 32-tap interpolator) and no lookahead, so nothing
        here ever costs the host a sample of delay.

        **The wet delay time is not latency and is never reported as such.**
        What the host compensates for is a delayed copy of what it sent; a
        delay's repeats are new signal arriving late on purpose. Reporting
        TIME here would pull the whole track forward by up to two seconds and
        move the dry signal with it. BMO Dimension's detune voices set the same
        precedent -- `modules/dim/dsp` reports 0 while manufacturing content
        that is not time-aligned with the input.

        If 10 §4's half-band fallback is ever added around the shaper, its
        group delay is a whole number of samples and is subtracted from D, so
        the figure stays 0 and the delay time stays exact. */
    int latencyForParams (const float*, int) const override { return 0; }

    DspCore& getCore() noexcept { return core; }

private:
    DspCore core;
};

inline std::unique_ptr<ModuleDsp> createDsp() { return std::make_unique<DwellDsp>(); }

} // namespace bmo::dwell
