#pragma once

#include "core/dsp/ModuleDsp.h"
#include "modules/deesser/dsp/DspCore.h"
#include "modules/deesser/params.h"

namespace bmo::deesser
{

/** The detent as the shape it names. */
inline Shape shapeFor (int index) noexcept
{
    return index == highShelf ? Shape::highShelf : Shape::bell;
}

/** The adapter: it unpacks the flat parameter array into DspCore::Params in
    `Index` order and does nothing else. Every mapping from a host value to a
    DSP unit is in `setParams` below, so there is one place to read to find out
    what the DSP is actually given -- and here that mapping is deliberately
    thin, because the five parameters are already in the units the engine
    wants.

    **The listen path is not in it.** `setSolo` is the one route from the panel
    to the audio thread that is not a parameter (core/dsp/ModuleDsp.h), and it
    is forwarded rather than unpacked: nothing saves it, nothing automates it,
    and it takes no schema slot. */
class DeesserDsp final : public ModuleDsp
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
        p.freqHz   = v[freq];
        p.q        = v[q];
        p.threshDb = v[thresh];
        p.rangeDb  = v[range];
        p.shape    = shapeFor ((int) v[shape]);

        core.setParams (p);
    }

    void process (float* const* channels, int numChannels, int numSamples) override
    {
        core.process (channels, numChannels, numSamples);
    }

    /** **Zero, at every setting, always.** Not computed from the values
        because nothing here can move it: there is no lookahead parameter and
        no oversampling row, by decision (docs/deesser/10-dsp-spec.md 1). The
        signature still takes them, because the interface does. */
    int latencyForParams (const float* v, int count) const override
    {
        // Not juce::ignoreUnused: this header is on the JUCE-free side, which
        // is what lets tests/dsp and tools/measure link the module without a
        // GUI anywhere near them.
        (void) v;
        (void) count;
        return DspCore::latencySamples();
    }

    /** The ribbon's tap: four floats a frame at DspCore::kRibbonHz. Null for
        every module that does not offer one, which is all of them but this.
        Enabling it is the panel's job and disabling it again is the panel's
        destructor, so a session with no BMO Defang window open costs the
        audio thread one branch a sample. */
    AnalyserTap* analyser() noexcept override { return &core.ribbonTap(); }

    /** The panel's GR meter reads this through ModuleEngine. Signed, positive =
        gain taken away, and it is the **peak band reduction** rather than a
        wideband figure; the placeholder core reports a flat zero. */
    float currentGainReductionDb() const noexcept override { return core.currentGainReductionDb(); }

    /** Momentary listen, straight through. -1 clears it. */
    void setSolo (int index) noexcept override { core.setSolo (index); }

    DspCore& getCore() noexcept { return core; }

private:
    DspCore core;
};

inline std::unique_ptr<ModuleDsp> createDsp() { return std::make_unique<DeesserDsp>(); }

} // namespace bmo::deesser
