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

    **Almost nothing else crosses.** There is no solo path and no
    gain-reduction figure -- a reverb has no part to hear on its own and
    nothing to report reducing -- and `ModuleDsp`'s defaults cover both, so
    their absence is silence rather than an override that does nothing.
    `analyser()` **is** overridden, as of 2026-09-21: the EQ page draws a
    spectrum behind its response curve, which is the owner's call and the one
    place on this panel that is not parameter-driven. See it below. */
class ReverbDsp final : public ModuleDsp
{
public:
    void prepare (double sampleRate, int maxBlockSize, int numChannels) override
    {
        core.prepare (sampleRate, maxBlockSize, numChannels);
    }

    void reset() override { core.reset(); }

    /** The flat host array as the engine's own struct.

        Pulled out of `setParams` when `tailSecondsForParams` arrived: that one
        is `const`, answers from values rather than from the core, and needs
        four of these fields. Unpacking twice would have been two places for
        the per-cent-to-0..1 conversions to disagree, which is the one thing
        the class comment above promises does not happen.

        **Six fields do not come from the array**, since the 2026-09-21
        control-set trim. `linkEr` is `kPreLinkFixed`; `decayShape`, `attack`,
        `dampLoFreqHz`, `dampHiFreqHz` and `erShape` come from `constantsFor`
        on the TYPE value that *is* in the array. So this is still one unpack
        of one array and still the only place a host value becomes an engine
        value -- the type is just read twice, once as a detent and once as a
        row. `TypeVoicing` cannot help here: a `Setting` names a parameter id
        and these five no longer have one. */
    static DspCore::Params paramsFrom (const float* v, int count)
    {
        DspCore::Params p;

        if (count < Index::count)
            return p;                   // the schema's own defaults, which are Room's row

        const auto& c = constantsFor ((int) v[Index::type]);

        p.type          = typeFor ((int) v[Index::type]);
        p.sizeM         = v[Index::size];
        p.preDelayMs    = v[Index::predelay];
        p.linkEr        = kPreLinkFixed;
        p.decaySeconds  = v[Index::decay];
        p.decayShape    = c.decayShape;
        p.attack        = c.attack * 0.01f;
        p.feed          = v[Index::feed] * 0.01f;

        p.dampLoFreqHz  = c.dampLoFreqHz;
        p.dampLo        = v[Index::damplo];
        p.dampHiFreqHz  = c.dampHiFreqHz;
        p.dampHi        = v[Index::damphi];

        // The Reverb EQ, ten fields, and one more conversion: `eqfilter` is a
        // bool on a float lane, so it crosses as `> 0.5f` the way every other
        // bool in the suite does.
        p.eqFilter      = v[Index::eqfilter] > 0.5f;
        p.eqLoFreqHz    = v[Index::eqlofreq];
        p.eqLoDb        = v[Index::eqlo];
        p.eqLoQ         = v[Index::eqloq];
        p.eqMidFreqHz   = v[Index::eqmidfreq];
        p.eqMidDb       = v[Index::eqmid];
        p.eqMidQ        = v[Index::eqmidq];
        p.eqHiFreqHz    = v[Index::eqhifreq];
        p.eqHiDb        = v[Index::eqhi];
        p.eqHiQ         = v[Index::eqhiq];

        p.erMode        = erModeFor ((int) v[Index::ermode]);
        p.erDensity     = v[Index::erdensity] * 0.01f;
        p.erShape       = c.erShape;
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

        return p;
    }

    void setParams (const float* v, int count) override
    {
        // Still a no-op on a short array rather than a stamp of the defaults:
        // a half-sized array is a caller bug, and quietly resetting the engine
        // to Room would be a worse answer than leaving it where it was.
        if (count < Index::count)
            return;

        core.setParams (paramsFrom (v, count));
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

    /** **The one module in the suite that has a tail to report.**

        `DspCore::tailSecondsFor` is the formula and its only copy -- pre-delay
        plus T_mid at the largest damping multiplier plus the last early
        reflection plus 50 ms, clamped to 30 s (docs/reverb/10-dsp-spec.md 5).
        This is only the unpacking in front of it, so a change to the
        arithmetic lands in one file and reaches the host through here without
        being retyped.

        A short array answers with the schema defaults' tail rather than with
        zero: `paramsFrom` hands back the defaults, and the default instance of
        BMO Linger does ring. Zero would be a lie that truncates. */
    double tailSecondsForParams (const float* v, int count) const override
    {
        return (double) DspCore::tailSecondsFor (paramsFrom (v, count));
    }

    /** The EQ page's spectrum window, at the point the Reverb EQ acts on.

        The default in `ModuleDsp` is null and every other module in the suite
        still takes it, so adding this changes nothing for any of them -- a
        panel with no tap draws no spectrum, which is the state BMO EQ, the
        Saturator, Util, Opto, Dimension, LTV Comp and the rack's own slots are
        all in. BMO DEQ is the other module that overrides it.

        `DspCore::eqAnalyser` carries the important part: **the samples are the
        dry input until there is a reverb under them**, because the core is a
        marked pass-through, and the tap is nonetheless at the point it belongs
        at rather than at the output. */
    AnalyserTap* analyser() noexcept override { return &core.eqAnalyser(); }

    DspCore& getCore() noexcept { return core; }

private:
    DspCore core;
};

inline std::unique_ptr<ModuleDsp> createDsp() { return std::make_unique<ReverbDsp>(); }

} // namespace bmo::reverb
