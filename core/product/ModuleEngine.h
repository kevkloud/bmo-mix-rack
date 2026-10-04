#pragma once

#include "ModuleDef.h"
#include "core/dsp/FiniteGuard.h"
#include "core/dsp/Meter.h"
#include "core/product/HostTempo.h"
#include "core/state/ParamSet.h"
#include <algorithm>
#include <atomic>

namespace bmo
{

/** A module running: its DSP, the parameters it reads, and its meters.

    The standalone product owns one; the rack owns one per occupied slot.
    Parameter values are read once per block from the ParamSet, in spec
    order, into a pre-sized array, so the audio thread does no allocation and
    does not care whose parameter objects they are.

    Input and gain-reduction metering cost nothing for a module that never
    reads them -- `inputMeter` is measured unconditionally the same way
    `outputMeter` always was, and `gainReduction` is a single atomic float
    fed from `ModuleDsp::currentGainReductionDb()`, whose default is silence.
    Only BMO Opto's panel reads either today.
*/
class ModuleEngine
{
public:
    ModuleEngine (const ModuleDef& d, ParamSet p)
        : moduleDef (d), paramSet (std::move (p)), dsp (d.createDsp()),
          values ((size_t) d.numParams(), 0.0f)
    {
        jassert (paramSet.size() == moduleDef.numParams());

        // The defaults until the first read: what a parameter that is not
        // finite from the start is held at (see read()).
        for (int i = 0; i < paramSet.size(); ++i)
            values[(size_t) i] = paramSet.spec (i).def;

        // A module whose parameters write each other gets its link here and
        // nowhere else. **One engine exists per running module in both
        // products** -- the standalone's own, and one per occupied rack slot
        // -- so this single line covers both, with or without an editor open.
        // `core/state/ParamLink.h` carries the argument for why the engine and
        // not a panel; BMO Linger's TYPE is the only module that has one.
        //
        // After `paramSet` is in place, and bound to the engine's own copy: a
        // link keeps a reference to it for the engine's whole life, which is
        // what makes the ParamSet a member rather than something passed round.
        if (moduleDef.createParamLink != nullptr)
            paramLink = moduleDef.createParamLink (paramSet);
    }

    const ModuleDef& def() const noexcept    { return moduleDef; }
    ParamSet& params() noexcept              { return paramSet; }
    const ParamSet& params() const noexcept  { return paramSet; }
    const Meter& meter() const noexcept      { return outputMeter; }
    const Meter& inputMeter() const noexcept { return inMeter; }
    const GainReductionMeter& gainReduction() const noexcept { return grMeter; }

    /** The rate the module is running at, or 0 before the first prepare.

        Published for a panel that draws something rate-dependent -- BMO DEQ's
        response curve is designed at the running rate, and drew the 48 kHz
        design at every rate until 2026-09-15, which put it up to 1 dB out in
        the top octave at 44.1 and 96 k. Atomic because a panel's timer reads
        it while the message thread may be in prepare(). */
    double sampleRate() const noexcept { return rate.load (std::memory_order_relaxed); }

    void prepare (double sampleRateHz, int maxBlockSize, int numChannels)
    {
        // Parameters go in first: an oversampling choice sizes the buffers.
        read();
        dsp->setParams (values.data(), (int) values.size());
        dsp->prepare (sampleRateHz, maxBlockSize, numChannels);
        rate.store (sampleRateHz, std::memory_order_relaxed);
        outputMeter.reset();
        inMeter.reset();
        grMeter.reset();
    }

    void reset()
    {
        dsp->reset();
        outputMeter.reset();
        inMeter.reset();
        grMeter.reset();
    }

    /** One block. `tempo` is the host's for this block, read by the processor
        that owns the engine; a default-constructed `HostTempo` says there is
        none.

        **An argument and not a member or a setter**, and not defaulted: a
        tempo belongs to one block, so there is nothing to keep between calls,
        and a caller that forgot to read the host should fail to compile rather
        than quietly hand every module "no tempo". It goes to the DSP right
        after the parameters and before the audio, which is the order
        `ModuleDsp::setTempo` promises. */
    void process (float* const* channels, int numChannels, int numSamples, const HostTempo& tempo)
    {
        read();
        run (channels, numChannels, numSamples, tempo);
    }

    /** One block on the values the last block read, without touching the
        parameters at all.

        For the rack while a chain edit is under way: the message thread is
        re-pointing lanes and ParamSets that this engine would otherwise read,
        so the engine keeps running -- its DSP state carries on, which is the
        point -- on what it already holds. Values that do not change across an
        edit, which is all of them unless a hand is on a knob at that moment,
        give exactly the samples `process` would. Never before the first
        `prepare`, which is what fills the held values. */
    void processHeld (float* const* channels, int numChannels, int numSamples, const HostTempo& tempo)
    {
        run (channels, numChannels, numSamples, tempo);
    }

    /** Points the engine at other parameter objects for the same specs, and
        rebuilds the module's link on them. Message thread, with the audio
        thread holding off this engine's reads (`processHeld`). The DSP is not
        touched: a module moved to another rack slot keeps its state. */
    void rebind (std::vector<juce::RangedAudioParameter*> params)
    {
        paramLink.reset();
        paramSet.rebind (std::move (params));

        if (moduleDef.createParamLink != nullptr)
            paramLink = moduleDef.createParamLink (paramSet);
    }

    /** Lets go of the module's link before the parameters it listens to are
        given to another module. For an engine a chain edit retires: it may run
        on for a few milliseconds while the output fades, and is destroyed only
        once the audio thread has let go of it too, but it must not react to
        the next occupant's values in the meantime. Message thread. */
    void dropLink() noexcept { paramLink.reset(); }

    /** True once since the last call if a block found a parameter that was
        not finite (and held its last finite value instead). Audio thread. */
    bool takeNonFiniteSeen() noexcept { return nonFiniteSeen.exchange (false, std::memory_order_relaxed); }

    /** The value the module is running on for parameter `i`, in real units:
        for a parameter that is not finite, the last finite one. Read on the
        message thread only for such a parameter, whose held value the audio
        thread no longer writes. */
    float heldValue (int i) const noexcept { return values[(size_t) i]; }

private:
    void run (float* const* channels, int numChannels, int numSamples, const HostTempo& tempo)
    {
        dsp->setParams (values.data(), (int) values.size());
        dsp->setTempo (tempo.bpm, tempo.valid, tempo.playing);

        // **The suite's one guard against NaN and infinity**, at the one call
        // every module goes through: the standalone product, every rack slot
        // and BMO Tune RT. A non-finite sample left in a filter's memory, a
        // detector or a delay line stays there and makes every sample after it
        // non-finite until a reset, and in a rack it then reaches every slot
        // downstream. core/AGENTS.md states what this guarantees; no module
        // adds a guard of its own.
        //
        // In: a bad sample from the host or from the slot before -- not
        // finite, or at or over finite::kCeiling (+192.7 dBFS), which can hold
        // a compressor down for a minute as surely as an infinity can -- is
        // written over with zero before the module sees it (finite::scrub
        // says why zero). A clean block is only read.
        finite::scrub (channels, numChannels, numSamples);

        inMeter.measure (channels, numChannels, numSamples);
        dsp->process (channels, numChannels, numSamples);

        // Out: a module that blows up by itself, on clean input, is reset and
        // its block is silenced on every channel, so nothing that is not
        // audio reaches the host or the next slot and the module is running
        // again from the next block with no one touching it. The same test as
        // the input's: a finite sample at or over the ceiling counts, or a
        // standalone module, and the rack's last slot, could hand the host
        // one. The whole block, not only the bad samples: what came before
        // them was made by the same broken state. Rare by construction, so the
        // reset's cost is not the concern; every module's reset only clears
        // memory it already owns.
        if (finite::anyNotAudio (channels, numChannels, numSamples))
        {
            dsp->reset();

            for (int ch = 0; ch < numChannels; ++ch)
                std::fill (channels[ch], channels[ch] + numSamples, 0.0f);
        }

        outputMeter.measure (channels, numChannels, numSamples);
        grMeter.publish (dsp->currentGainReductionDb());
    }

public:
    /** Latency for the parameters as they are now. Safe from any thread. */
    int latency() const
    {
        std::vector<float> now ((size_t) paramSet.size());
        paramSet.readAll (now.data());
        return dsp->latencyForParams (now.data(), (int) now.size());
    }

    /** The tail for the parameters as they are now, in seconds. Safe from any
        thread.

        Here and not in the processors for `latency()`'s reason: the DSP is
        private to the engine, so neither processor can ask it directly, and
        neither should be growing its own copy of the read-all into a scratch
        array. Every module but BMO Linger takes `ModuleDsp`'s zero default. */
    double tailSeconds() const
    {
        std::vector<float> now ((size_t) paramSet.size());
        paramSet.readAll (now.data());
        return dsp->tailSecondsForParams (now.data(), (int) now.size());
    }

    /** Applies a saved state -- a session, a rack slot's carried state, a
        preset file -- and then tells the module's link that it happened.

        **Every state restore goes through here rather than straight to
        `params().applyXml`**, in both products, because the order matters: the
        link has to hear about the restore after the last value has landed, and
        only the engine holds both. `ParamLink::stateRestored` says what went
        wrong without it. */
    void restoreState (const juce::XmlElement& xml)
    {
        paramSet.applyXml (xml);

        if (paramLink != nullptr)
            paramLink->stateRestored();
    }

    /** Momentary, from the panel; -1 clears it. Not a parameter, so it is not
        in paramSet, not in a preset and not in a saved session. */
    void setSolo (int index) noexcept { dsp->setSolo (index); }

    /** The module's analyser window, or null if it has none. */
    AnalyserTap* analyser() noexcept { return dsp->analyser(); }

private:
    // A value that is not finite -- a host can set one on a parameter that
    // stores what it is given -- is not read: the module keeps the last finite
    // value it had, and the owner is told (`takeNonFiniteSeen`) so it can put
    // the parameter back. One isfinite per parameter per block.
    void read() noexcept
    {
        for (int i = 0; i < paramSet.size(); ++i)
        {
            const auto v = paramSet.getReal (i);

            if (std::isfinite (v))
                values[(size_t) i] = v;
            else
                nonFiniteSeen.store (true, std::memory_order_relaxed);
        }
    }

    const ModuleDef& moduleDef;
    ParamSet paramSet;

    /** Declared after `paramSet` and before everything it does not touch, so
        it is destroyed before the parameters it is listening to go away. */
    std::unique_ptr<ParamLink> paramLink;

    std::unique_ptr<ModuleDsp> dsp;
    std::vector<float> values;
    Meter outputMeter;
    Meter inMeter;
    GainReductionMeter grMeter;
    std::atomic<double> rate { 0.0 };
    std::atomic<bool> nonFiniteSeen { false };
};

} // namespace bmo
