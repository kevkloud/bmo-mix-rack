#pragma once

#include "core/dsp/ModuleDsp.h"
#include "modules/reverb/dsp/DspCore.h"
#include "modules/reverb/params.h"

namespace bmo::reverb
{

/** The detent as the type it names. */
inline Type typeFor (int index) noexcept
{
    return index >= 0 && index < numTypes ? (Type) index : Type::room;
}

/** The detent as the ER mode it names. */
inline ErMode erModeFor (int index) noexcept
{
    return index >= 0 && index < numErModes ? (ErMode) index : ErMode::taps;
}

/** The adapter: it unpacks the flat parameter array into `DspCore::Params` in
    `Index` order and does nothing else.

    **Every mapping from a host value to a DSP unit is in `setParams` below**,
    so there is one place to read to find out what the engine is actually
    given. Unlike BMO Defang's, this one is not thin: four controls are on the
    knob in per cent and reach the engine as 0..1, WIDTH is per cent and
    arrives as a 0..2 M/S gain, and the two choice indices become enums. Those
    are the only conversions, and they are all here.

    **Nothing else crosses.** There is no solo path, no gain-reduction figure
    and no analyser tap: the panel's display is drawn from the parameters and
    from `TapTables.h`, which is why it cannot affect the sound or the latency.
    `ModuleDsp`'s defaults cover all three, so their absence is silence rather
    than an override that does nothing. */
class ReverbDsp final : public ModuleDsp
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

        p.type          = typeFor ((int) v[Index::type]);
        p.sizeM         = v[Index::size];
        p.preDelayMs    = v[Index::predelay];
        p.linkEr        = v[Index::prelink] >= 0.5f;
        p.decaySeconds  = v[Index::decay];
        p.decayShape    = v[Index::decayshape];
        p.attack        = v[Index::attack] * 0.01f;
        p.feed          = v[Index::feed] * 0.01f;

        p.dampLoFreqHz  = v[Index::damplofreq];
        p.dampLo        = v[Index::damplo];
        p.dampHiFreqHz  = v[Index::damphifreq];
        p.dampHi        = v[Index::damphi];

        p.eqLoFreqHz    = v[Index::eqlofreq];
        p.eqLoDb        = v[Index::eqlo];
        p.eqHiFreqHz    = v[Index::eqhifreq];
        p.eqHiDb        = v[Index::eqhi];

        p.erMode        = erModeFor ((int) v[Index::ermode]);
        p.erDensity     = v[Index::erdensity] * 0.01f;
        p.erShape       = v[Index::ershape];
        p.erSpreadMs    = v[Index::erspread];
        p.erHiCutHz     = v[Index::erhicut];
        p.erVariation   = (int) (v[Index::ervariation] + 0.5f);

        p.modDepthMs    = v[Index::moddepth];
        p.modRateHz     = v[Index::modrate];
        p.width         = v[Index::width] * 0.01f;
        p.inHiCutHz     = v[Index::inhicut];

        p.erLevelDb     = v[Index::erlevel];
        p.verbLevelDb   = v[Index::verblevel];
        p.mix           = v[Index::mix] * 0.01f;
        p.outputDb      = v[Index::output];

        core.setParams (p);
    }

    void process (float* const* channels, int numChannels, int numSamples) override
    {
        core.process (channels, numChannels, numSamples);
    }

    /** **Zero, at every setting, always.** Not computed from the values
        because nothing in this schema can move it: pre-delay cannot go
        negative, there is no lookahead and there is no oversampling row, all
        three by decision (docs/reverb/10-dsp-spec.md sections 1 and 2). The
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

    DspCore& getCore() noexcept { return core; }

private:
    DspCore core;
};

inline std::unique_ptr<ModuleDsp> createDsp() { return std::make_unique<ReverbDsp>(); }

} // namespace bmo::reverb
