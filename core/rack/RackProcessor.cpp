#include "RackProcessor.h"
#include "RackEditor.h"
#include "core/product/BusLayouts.h"

#include <algorithm>
#include <thread>

namespace bmo
{

RackProcessor::RackProcessor (std::vector<const ModuleDef*> reg, ProductInfo i,
                              std::vector<RackPreset> rackPresets)
    : juce::AudioProcessor (BusesProperties()
          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      registry (std::move (reg)), info (std::move (i)),
      presets (*this, info.presets, factoryEntries (std::move (rackPresets)))
{
    for (int s = 0; s < kSlots; ++s)
    {
        auto group = std::make_unique<juce::AudioProcessorParameterGroup> (
            "slot" + juce::String (s + 1), "Slot " + juce::String (s + 1), "|");

        for (int p = 0; p < kParamsPerSlot; ++p)
        {
            auto param = std::make_unique<SlotParameter> (s, p, info.versionHint);
            params[(size_t) s][(size_t) p] = param.get();
            param->addListener (this);
            group->addChild (std::move (param));
        }

        addParameterGroup (std::move (group));
    }

    // The empty chain, generation 0, which the audio thread starts on.
    chains.push_back (std::make_unique<Chain>());
    live = chains.back().get();
    pending.store (live, std::memory_order_release);
}

RackProcessor::~RackProcessor()
{
    for (auto& slot : params)
        for (auto* p : slot)
            p->removeListener (this);

    cancelPendingUpdate();
}

//==============================================================================
const ModuleDef* RackProcessor::findModule (const juce::String& id) const noexcept
{
    for (auto* def : registry)
        if (id == def->id)
            return def;

    return nullptr;
}

int RackProcessor::getNumModules() const noexcept
{
    int n = 0;

    for (const auto& s : slots)
        if (s.def != nullptr)
            ++n;

    return n;
}

const ModuleDef* RackProcessor::getModuleAt (int slot) const noexcept
{
    return slot >= 0 && slot < kSlots ? slots[(size_t) slot].def : nullptr;
}

ModuleEngine* RackProcessor::getEngineAt (int slot) noexcept
{
    return slot >= 0 && slot < kSlots ? slots[(size_t) slot].engine.get() : nullptr;
}

ui::ModuleContext RackProcessor::makeContext (int slot)
{
    auto* engine = getEngineAt (slot);
    jassert (engine != nullptr);

    return { engine->params(), engine->def(),
             [engine] { return engine->meter().maxPeak(); },
             [engine] { return engine->meter().maxRms(); },
             [engine] { return engine->inputMeter().maxPeak(); },
             [engine] { return engine->inputMeter().maxRms(); },
             [engine] { return engine->gainReduction().get(); },
             [engine] { return engine->sampleRate(); },
             [engine] (int band) { engine->setSolo (band); },
             engine->analyser() };
}

bool RackProcessor::isSlotExpanded (int slot) const noexcept
{
    return slot >= 0 && slot < kSlots && slots[(size_t) slot].expanded;
}

void RackProcessor::setSlotExpanded (int slot, bool shouldBe)
{
    if (slot < 0 || slot >= kSlots)
        return;

    bool changed = false;

    {
        // Under the edit lock because a capture on another thread reads it.
        const juce::ScopedLock edit (editLock);
        auto& s = slots[(size_t) slot];
        const auto next = shouldBe && s.def != nullptr && s.def->isExpandable();
        changed = next != s.expanded;
        s.expanded = next;
    }

    // The view is saved with the session, so a host is told it changed.
    if (changed)
        updateHostDisplay (ChangeDetails().withNonParameterStateChanged (true));
}

//==============================================================================
std::vector<RackProcessor::Entry> RackProcessor::currentChain() const
{
    std::vector<Entry> chain;

    for (int s = 0; s < kSlots; ++s)
        if (slots[(size_t) s].def != nullptr && slots[(size_t) s].engine != nullptr)
            chain.push_back ({ slots[(size_t) s].def, s, nullptr });

    return chain;
}

std::uint64_t RackProcessor::holdAudioReads()
{
    // Half of a two-flag handshake; processBlock has the other half. Both
    // sides store their own flag and then load the other's, all sequentially
    // consistent, so at least one of them sees the other: either the block
    // starting now sees the new generation and runs on held values, or this
    // sees the block in progress and waits for it to end. Either way no
    // block reads a lane or a ParamSet while `rebuild` is changing it.
    const auto generation = editGeneration.fetch_add (1) + 1;

    while (inBlock.load())
        std::this_thread::yield();

    return generation;
}

bool RackProcessor::audioIsRunning() const
{
    const auto last = lastBlockMs.load (std::memory_order_relaxed);

    if (! prepared || last == 0)
        return false;

    // Four blocks or 200 ms, whichever is longer: a host that has not asked
    // for a block in that long is not running this instance, and an edit can
    // be put in place at once rather than left for a block that may not come.
    const auto blockMs = currentRate > 0.0 ? 1000.0 * (double) currentBlock / currentRate : 0.0;
    const auto patience = (juce::uint32) std::max (200.0, 4.0 * blockMs);

    return juce::Time::getMillisecondCounter() - last < patience;
}

void RackProcessor::completeSwap()
{
    const juce::ScopedLock audio (chainLock);

    live = pending.load (std::memory_order_acquire);
    audioGeneration.store (live->generation, std::memory_order_release);
    editDip.reset();
    dipDownLeft = 0;
}

void RackProcessor::collectRetired()
{
    const auto done = audioGeneration.load (std::memory_order_acquire);

    // An engine first missing from chain `generation` is unreachable once the
    // audio thread has finished a block on that chain or a later one; so is
    // every chain older than the one it is on. Destroyed here, off the audio
    // thread, engine before overflow (Retired's member order).
    retired.erase (std::remove_if (retired.begin(), retired.end(),
                                   [done] (const Retired& r) { return r.generation <= done; }),
                   retired.end());

    chains.erase (std::remove_if (chains.begin(), chains.end(),
                                  [done] (const std::unique_ptr<Chain>& c) { return c->generation < done; }),
                  chains.end());
}

/*  How an edit reaches the audio without a click, a burst or a lock.

    The message thread builds the new chain at once -- lanes re-assigned, new
    engines made, restored and prepared, a moved engine re-pointed at its new
    slot's lanes -- so everything that asks the rack about its chain sees the
    edit as soon as the call returns. What the audio thread runs is a separate,
    published `Chain` of engine pointers, and it changes over at its own pace:

      1. `holdAudioReads` makes every block from now on run the chain it has
         on the values its engines already hold, so nothing below is read
         while it moves.
      2. The new chain is published. The next block sees it and fades its
         output to zero over `kEditDipMs`, placed so that the bottom lands on
         the block's last sample; the block after swaps chains at its first
         sample and fades back up. An engine the edit kept is in both chains
         and runs every block of the edit, so its state never misses a sample;
         one the edit brings in runs unheard alongside (`runWarming`), so it
         is mid-stream at the swap.
      3. What the edit took out is retired, not destroyed, until the audio
         thread has finished a block without it (`collectRetired`).

    The audio thread waits on nothing. The message thread waits at most for
    the block in progress, in step 1. If no audio is running, the swap is made
    here instead, under `chainLock`, so nothing is left waiting for a block. */
void RackProcessor::rebuild (std::vector<Entry> chain)
{
    jassert (juce::MessageManager::getInstance()->currentThreadHasLockedMessageManager());
    jassert ((int) chain.size() <= kSlots);

    listeners.call ([] (Listener& l) { l.rackChainWillChange(); });

    {
        const juce::ScopedLock edit (editLock);

        collectRetired();
        const auto generation = holdAudioReads();

        std::array<Slot, kSlots> old;

        for (int s = 0; s < kSlots; ++s)
            old[(size_t) s] = std::move (slots[(size_t) s]);

        // Where each engine goes: its new slot, or -1 if the edit takes it out.
        std::array<int, kSlots> to;
        to.fill (-1);

        for (int j = 0; j < (int) chain.size(); ++j)
        {
            const auto from = chain[(size_t) j].from;
            jassert (from < 0 || (old[(size_t) from].engine != nullptr && old[(size_t) from].def == chain[(size_t) j].def));

            if (from >= 0)
                to[(size_t) from] = j;
        }

        // Every engine that does not stay where it is lets go of its link
        // before any lane changes hands, and one that moves has its values read
        // off its old lanes now, while they are still its own. Normalised, so
        // the copy onto the same specs in the new slot is exact.
        std::array<std::vector<float>, kSlots> carried;

        for (int s = 0; s < kSlots; ++s)
        {
            auto& o = old[(size_t) s];

            if (o.engine == nullptr || to[(size_t) s] == s)
                continue;

            o.engine->dropLink();

            if (to[(size_t) s] >= 0)
            {
                auto& p = o.engine->params();
                carried[(size_t) s].resize ((size_t) p.size());

                for (int i = 0; i < p.size(); ++i)
                    carried[(size_t) s][(size_t) i] = p.param (i).getValue();
            }
        }

        // What leaves the chain is retired with its overflow. A moving engine's
        // overflow stays behind, retired on its own, because its parameters
        // are named and numbered for the slot it is leaving; the engine gets a
        // new one below.
        for (int s = 0; s < kSlots; ++s)
        {
            auto& o = old[(size_t) s];

            if (o.engine != nullptr && to[(size_t) s] < 0)
                retired.push_back ({ std::move (o.overflow), std::move (o.engine), generation });
            else if (to[(size_t) s] >= 0 && to[(size_t) s] != s && o.overflow != nullptr)
                retired.push_back ({ std::move (o.overflow), nullptr, generation });
        }

        for (int s = 0; s < kSlots; ++s)
        {
            auto& slot = slots[(size_t) s];
            auto* entry = s < (int) chain.size() ? &chain[(size_t) s] : nullptr;

            // Untouched: the same engine in the same slot keeps its lanes, its
            // overflow, its link and its view exactly as they are.
            if (entry != nullptr && entry->from == s)
            {
                slot = std::move (old[(size_t) s]);
                continue;
            }

            slot.def = entry != nullptr ? entry->def : nullptr;
            slot.expanded = false;

            // Not a ternary with a temporary on one side: the specs have to
            // be the module's own static list, because the parameters keep
            // pointers into it.
            static const ParamSpecs none;
            const auto& specs = slot.def != nullptr ? slot.def->specs : none;

            std::vector<juce::RangedAudioParameter*> assigned;

            for (int p = 0; p < kParamsPerSlot; ++p)
            {
                auto* param = params[(size_t) s][(size_t) p];
                param->assign (p < (int) specs.size() ? &specs[(size_t) p] : nullptr);

                if (p < (int) specs.size())
                    assigned.push_back (param);
            }

            // Past the slot's lanes, the module's parameters go off the grid
            // rather than into the next slot's. Spec order continues, so the
            // ParamSet cannot tell the two apart.
            if ((int) specs.size() > kParamsPerSlot)
            {
                juce::AudioProcessorParameter::Listener& listener = *this;
                slot.overflow = std::make_unique<SlotOverflow> (s, specs, kParamsPerSlot,
                                                                info.versionHint, listener);

                for (auto* param : slot.overflow->parameters())
                    assigned.push_back (param);
            }

            if (slot.def == nullptr)
                continue;

            if (entry->from >= 0)
            {
                // Moved: the engine itself, its DSP state and its view come
                // along; its values go onto this slot's lanes, the host is told
                // as it would be for a restore, and then the engine reads them.
                auto& from = old[(size_t) entry->from];
                const auto& values = carried[(size_t) entry->from];

                for (size_t i = 0; i < assigned.size(); ++i)
                    assigned[i]->setValueNotifyingHost (values[i]);

                slot.engine   = std::move (from.engine);
                slot.expanded = from.expanded;
                slot.engine->rebind (std::move (assigned));
                continue;
            }

            // New: compact unless its state says otherwise -- a module newly
            // added, a preset's chain and an old session all arrive compact.
            const auto* state = entry->state.get();
            slot.expanded = slot.def->isExpandable() && state != nullptr
                         && state->getStringAttribute (kViewAttribute) == kViewExpanded;

            slot.engine = std::make_unique<ModuleEngine> (*slot.def, ParamSet (slot.def->specs, assigned));

            if (state != nullptr)
                slot.engine->restoreState (*state);

            if (prepared)
                slot.engine->prepare (currentRate, currentBlock, currentChannels);
        }

        auto next = std::make_unique<Chain>();
        next->generation = generation;

        for (auto& s : slots)
            if (s.engine != nullptr)
                next->engines[(size_t) next->size++] = s.engine.get();

        pending.store (next.get(), std::memory_order_release);
        chains.push_back (std::move (next));

        if (! audioIsRunning())
        {
            completeSwap();
            collectRetired();
        }
    }

    // Names, ranges and steps of up to 256 parameters just changed, and so did
    // the chain, which is session state but no parameter's value: without the
    // second flag a host need not mark the session dirty, and adding to an
    // empty rack or removing the last module changes no value at all.
    updateHostDisplay (ChangeDetails().withParameterInfoChanged (true)
                                      .withNonParameterStateChanged (true));
    triggerAsyncUpdate();

    listeners.call ([] (Listener& l) { l.rackChainChanged(); });
}

bool RackProcessor::addModule (const ModuleDef& def)
{
    if (getNumModules() >= kSlots)
        return false;

    auto chain = currentChain();
    chain.push_back ({ &def, -1, nullptr });
    rebuild (std::move (chain));
    presets.noteChange();
    return true;
}

void RackProcessor::setModule (int slot, const ModuleDef& def)
{
    auto chain = currentChain();

    if (slot >= 0 && slot < (int) chain.size())
        chain[(size_t) slot] = { &def, -1, nullptr };
    else if ((int) chain.size() < kSlots)
        chain.push_back ({ &def, -1, nullptr });
    else
        return;

    rebuild (std::move (chain));
    presets.noteChange();
}

void RackProcessor::removeModule (int slot)
{
    auto chain = currentChain();

    if (slot < 0 || slot >= (int) chain.size())
        return;

    chain.erase (chain.begin() + slot);
    rebuild (std::move (chain));
    presets.noteChange();
}

void RackProcessor::moveModule (int from, int to)
{
    auto chain = currentChain();
    const auto n = (int) chain.size();

    if (from < 0 || from >= n || to < 0 || to >= n || from == to)
        return;

    auto item = std::move (chain[(size_t) from]);
    chain.erase (chain.begin() + from);
    chain.insert (chain.begin() + to, std::move (item));
    rebuild (std::move (chain));
    presets.noteChange();
}

void RackProcessor::clearChain()
{
    rebuild ({});
}

//==============================================================================
std::unique_ptr<juce::XmlElement> RackProcessor::captureState()
{
    // A host may ask for the state from any thread, while the message thread
    // is in the middle of an edit: before this lock, a worker saving during a
    // run of moves read slots being emptied and refilled and crashed. The
    // edit lock is the one the audio thread never takes, so a capture can
    // wait for an edit, never the other way round with audio involved.
    const juce::ScopedLock edit (editLock);

    auto xml = std::make_unique<juce::XmlElement> (kRootTag);
    xml->setAttribute ("stateVersion", info.stateVersion);

    int index = 0;

    for (auto& s : slots)
    {
        if (s.def == nullptr || s.engine == nullptr)
            continue;

        auto* e = xml->createNewChildElement (kSlotTag);
        e->setAttribute ("index", index++);
        e->setAttribute ("module", s.def->id);
        e->setAttribute ("schema", s.def->schemaVersion);
        e->addChildElement (s.engine->params().toXml (s.def->schemaVersion).release());
    }

    return xml;
}

bool RackProcessor::restoreState (const juce::XmlElement& xml)
{
    if (! xml.hasTagName (kRootTag))
        return false;

    std::vector<Entry> chain;

    for (auto* e : xml.getChildWithTagNameIterator (kSlotTag))
    {
        // A module this build does not have is dropped, and the chain closes
        // up. Future schema versions migrate here, keyed off "schema".
        auto* def = findModule (e->getStringAttribute ("module"));

        if (def == nullptr || (int) chain.size() >= kSlots)
            continue;

        std::unique_ptr<juce::XmlElement> state;

        if (auto* p = e->getChildByName (ParamSet::kRootTag))
            state = std::make_unique<juce::XmlElement> (*p);

        chain.push_back ({ def, -1, std::move (state) });
    }

    rebuild (std::move (chain));
    return true;
}

void RackProcessor::resetToDefaults()
{
    clearChain();
}

void RackProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    // Held across the capture and the views, so both come from one chain.
    const juce::ScopedLock edit (editLock);

    if (auto xml = captureState())
    {
        // Views go in the session and not in captureState, which rack preset
        // files are also written from. A slot's PARAMS element is the one its
        // module's state travels in, so that is where the view is read back.
        int s = 0;

        for (auto* e : xml->getChildWithTagNameIterator (kSlotTag))
        {
            while (s < kSlots && (slots[(size_t) s].def == nullptr || slots[(size_t) s].engine == nullptr))
                ++s;

            if (s < kSlots && slots[(size_t) s].expanded)
                if (auto* p = e->getChildByName (ParamSet::kRootTag))
                    p->setAttribute (kViewAttribute, kViewExpanded);

            ++s;
        }

        copyXmlToBinary (*xml, destData);
    }
}

void RackProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    auto xml = getXmlFromBinary (data, sizeInBytes);

    if (xml == nullptr)
        return;

    // Rebuilding tears down panels, so it has to be on the message thread.
    // Hosts call this from there; the lock is a no-op then and a safety net
    // otherwise.
    const juce::MessageManagerLock lock;
    restoreState (*xml);
}

//==============================================================================
std::vector<FactoryEntry> RackProcessor::factoryEntries (std::vector<RackPreset> rackPresets)
{
    std::vector<FactoryEntry> out;

    // The list is moved into a shared holder the lambdas keep alive.
    auto held = std::make_shared<std::vector<RackPreset>> (std::move (rackPresets));

    for (size_t i = 0; i < held->size(); ++i)
        out.push_back ({ (*held)[i].name, [this, held, i] { applyPreset ((*held)[i]); } });

    return out;
}

void RackProcessor::applyPreset (const RackPreset& preset)
{
    std::vector<Entry> chain;

    for (const auto& entry : preset.chain)
    {
        auto* def = findModule (entry.moduleId);

        if (def == nullptr || (int) chain.size() >= kSlots)
            continue;

        // Settings become the same XML a saved state carries, so one path
        // applies both.
        auto state = std::make_unique<juce::XmlElement> (ParamSet::kRootTag);

        for (const auto& s : entry.settings)
        {
            auto* e = state->createNewChildElement (ParamSet::kParamTag);
            e->setAttribute ("id", s.id);
            e->setAttribute ("value", (double) s.value);
        }

        chain.push_back ({ def, -1, std::move (state) });
    }

    rebuild (std::move (chain));
}

//==============================================================================
void RackProcessor::parameterValueChanged (int, float)
{
    presets.noteChange();
    triggerAsyncUpdate();
}

int RackProcessor::totalLatency() const
{
    int total = 0;

    for (const auto& s : slots)
        if (s.engine != nullptr)
            total += s.engine->latency();

    return total;
}

double RackProcessor::totalTail() const
{
    double total = 0.0;

    // Summed, not maxed -- see the declaration. An empty rack adds nothing up
    // and reports 0.0, which is what it did when this was hardcoded.
    for (const auto& s : slots)
        if (s.engine != nullptr)
            total += s.engine->tailSeconds();

    // And clamped at the suite's own ceiling, exactly as a module clamps its
    // own figure: `addModule` counts slots and never looks for duplicates, so
    // eight BMO Lingers is a legal chain and eight honest thirties is a
    // four-minute tail rendered onto the end of every offline bounce. See
    // `bmo::kMaxTailSeconds`, which is where both clamps get the number.
    return std::min (total, kMaxTailSeconds);
}

void RackProcessor::handleAsyncUpdate()
{
    // Also the moment to let go of what the last edit retired, if the audio
    // thread has moved on from it by now.
    {
        const juce::ScopedLock edit (editLock);
        collectRetired();
    }

    const auto latency = totalLatency();

    if (reportedLatency.exchange (latency, std::memory_order_relaxed) != latency)
        setLatencySamples (latency);

    reportedTail.store (totalTail(), std::memory_order_relaxed);
}

//==============================================================================
void RackProcessor::prepareToPlay (double sampleRate, int maximumExpectedSamplesPerBlock)
{
    const juce::ScopedLock edit (editLock);

    // No block runs during a prepare, so an edit still waiting for one is put
    // in place now and every engine prepared below is one the audio will run.
    completeSwap();
    collectRetired();

    {
        const juce::ScopedLock audio (chainLock);

        currentRate     = sampleRate;
        currentBlock    = maximumExpectedSamplesPerBlock;
        currentChannels = getTotalNumOutputChannels();
        prepared        = true;

        for (auto& s : slots)
            if (s.engine != nullptr)
                s.engine->prepare (currentRate, currentBlock, currentChannels);

        bypassDelay.prepare (currentChannels);

        // One group of channels per slot for the taps, one for the chain an
        // edit is warming up (runWarming), one spare. Sized here, never in a
        // block.
        workChannels = std::max (currentChannels, 1);
        workspace.setSize (workChannels * (kSlots + 2), std::max (currentBlock, 1));
        workspace.clear();

        editDip.prepare (currentRate, kEditDipMs);
        dipLength   = std::max (1, (int) std::lround (std::max (currentRate, 0.0) * kEditDipMs * 0.001));
        dipDownLeft = 0;
        lastBlockMs.store (0, std::memory_order_relaxed);
    }

    const auto latency = totalLatency();
    reportedLatency.store (latency, std::memory_order_relaxed);
    setLatencySamples (latency);

    reportedTail.store (totalTail(), std::memory_order_relaxed);
}

void RackProcessor::releaseResources()
{
    const juce::ScopedLock edit (editLock);

    completeSwap();
    collectRetired();

    const juce::ScopedLock audio (chainLock);
    lastBlockMs.store (0, std::memory_order_relaxed);

    for (auto& s : slots)
        if (s.engine != nullptr)
            s.engine->reset();
}

bool RackProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    // The same contract a standalone module states -- see
    // core/product/BusLayouts.h. The rack can hold this one because the
    // widening happens once, at its input, ahead of slot 1: every slot still
    // sees the same channel count as every other, and no module has to know
    // that the instance is fed from a single channel.
    //
    // **Mono in to stereo out is offered if any module the rack can host opts
    // in** (ModuleDef::acceptsMonoInput), which today means BMO Linger. A host
    // fixes the layout before there is a chain and does not ask again when the
    // chain changes, so the answer cannot depend on what is loaded; the rack
    // offers what its registry could use. A module that did not opt in and is
    // loaded into such a rack is handed the duplicated pair BusLayouts.h
    // describes, which is the signal a stereo instance fed the same input on
    // both sides has always had.
    const auto anyAcceptsMono = std::any_of (registry.begin(), registry.end(),
                                             [] (const ModuleDef* d) { return d->acceptsMonoInput; });

    return buses::isSupported (layouts, anyAcceptsMono);
}

void RackProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    // The host's tempo, read ONCE for the whole chain: every slot below is
    // handed this same value, so two modules synced to the tempo cannot
    // disagree within a block. A block the try-lock skips gets no tempo, as it
    // gets no processing. A block on held values during an edit gets it as
    // usual: the tempo is the host's, not a parameter.
    const auto tempo = readHostTempo (getPlayHead());

    const auto numSamples = buffer.getNumSamples();
    const auto numIn      = getTotalNumInputChannels();
    const auto numOut     = getTotalNumOutputChannels();

    // Mono in, stereo out: the chain is given the input in both channels, not
    // one channel and silence. BusLayouts.h says why at length.
    buses::spreadInputAcrossOutputs (buffer, numIn, numOut);

    // The host's own bad samples (NaN, infinity, and anything at or over
    // finite::kCeiling) are stopped here, at the rack's edge, and not only in
    // slot 1's engine: an empty chain hands the host this buffer back without
    // any engine seeing it. On entry is enough for every path out, because
    // every engine already guarantees a finite output (ModuleEngine::process)
    // and the edit dip only scales. A clean block is only read.
    finite::scrub (buffer.getArrayOfWritePointers(), numOut, numSamples);

    auto* const* channels = buffer.getArrayOfWritePointers();

    // The input as the chain gets it, kept for the host's bypass: the moment
    // it switches, the bypass carries on from where the chain's output was.
    bypassDelay.push (channels, numOut, numSamples);

    const juce::ScopedTryLock lock (chainLock);

    // Only prepare, release and an edit made while no audio was running take
    // this lock, so a block meets it only when the host has just started
    // calling again. It goes out silent: never the input, which would be the
    // dry signal and early by the chain's latency.
    if (! lock.isLocked())
    {
        for (int ch = 0; ch < numOut; ++ch)
            juce::FloatVectorOperations::clear (channels[ch], numSamples);

        return;
    }

    // The other half of holdAudioReads' handshake: say a block is running,
    // then look for an edit.
    inBlock.store (true);
    const auto edit = editGeneration.load();
    auto* const newest = pending.load (std::memory_order_acquire);

    // At the bottom of the dip, swap to the newest chain if its edit is
    // finished -- if another has started since, wait there for that one.
    if (newest != live && editDip.ready() && newest->generation == edit)
    {
        live = newest;
        editDip.changed();
    }

    // A chain newer than the one running: fade out, from wherever the gain
    // is, ready to swap at the next block's first sample.
    if (newest != live && ! editDip.isPending())
    {
        editDip.request();
        dipDownLeft = editDip.ready() ? 0 : dipLength;
    }

    // While an edit is being made or waits to be swapped in, the chain runs
    // on what its engines already hold; see ModuleEngine::processHeld.
    const auto held = live->generation != edit;

    if (newest != live)
        runWarming (*newest, channels, numOut, numSamples, tempo, held, newest->generation != edit);
    else
        for (int s = 0; s < live->size; ++s)
            runEngine (*live->engines[(size_t) s], channels, numOut, numSamples, tempo, held);

    applyDip (channels, numOut, numSamples);

    audioGeneration.store (live->generation, std::memory_order_release);
    lastBlockMs.store (std::max (1u, juce::Time::getMillisecondCounter()), std::memory_order_relaxed);
    inBlock.store (false);
}

void RackProcessor::runEngine (ModuleEngine& engine, float* const* channels, int numChannels, int numSamples,
                               const HostTempo& tempo, bool held)
{
    if (held)
        engine.processHeld (channels, numChannels, numSamples, tempo);
    else
        engine.process (channels, numChannels, numSamples, tempo);
}

void RackProcessor::runWarming (const Chain& next, float* const* channels, int numChannels, int numSamples,
                                const HostTempo& tempo, bool held, bool nextHeld)
{
    const auto liveIndexOf = [this] (const ModuleEngine* e)
    {
        for (int s = 0; s < live->size; ++s)
            if (live->engines[(size_t) s] == e)
                return s;

        return -1;
    };

    bool anyArriving = false;

    for (int j = 0; j < next.size; ++j)
        anyArriving = anyArriving || liveIndexOf (next.engines[(size_t) j]) < 0;

    // Nothing arriving (a remove, a move), or a block larger than the host
    // promised at prepare, which the workspace was sized for: the running
    // chain alone. An engine that then arrives in the swap starts cold.
    if (! anyArriving || numSamples > workspace.getNumSamples() || numChannels > workChannels)
    {
        for (int s = 0; s < live->size; ++s)
            runEngine (*live->engines[(size_t) s], channels, numChannels, numSamples, tempo, held);

        return;
    }

    const auto group = [this] (int g) { return workspace.getArrayOfWritePointers() + g * workChannels; };
    const auto copy  = [numChannels, numSamples] (float* const* to, const float* const* from)
    {
        for (int ch = 0; ch < numChannels; ++ch)
            juce::FloatVectorOperations::copy (to[ch], from[ch], numSamples);
    };

    // The running chain as usual, with what each engine hands on kept.
    auto* const* warm = group (kSlots);
    copy (warm, channels);

    for (int s = 0; s < live->size; ++s)
    {
        runEngine (*live->engines[(size_t) s], channels, numChannels, numSamples, tempo, held);
        copy (group (s), channels);
    }

    // Then the next chain in its own order, unheard, so that every engine it
    // brings in is fed what its slot will be fed and is mid-stream when the
    // swap comes: a kept engine is not run twice -- its output from the
    // running chain stands in for it -- and an arriving one runs on the
    // signal that reaches it. Exact wherever the engines before the arriving
    // one are in the same order in both chains, which is every add and every
    // replace; after a move as well, what is fed is the nearest the running
    // chain has.
    for (int j = 0; j < next.size; ++j)
    {
        auto* engine = next.engines[(size_t) j];
        const auto k = liveIndexOf (engine);

        if (k >= 0)
            copy (warm, group (k));
        else
            runEngine (*engine, warm, numChannels, numSamples, tempo, nextHeld);
    }
}

void RackProcessor::applyDip (float* const* channels, int numChannels, int numSamples) noexcept
{
    // Bit-exact while no edit is in flight: nothing is multiplied at all.
    if (editDip.isIdle())
        return;

    for (int i = 0; i < numSamples; ++i)
    {
        float gain;

        if (editDip.isPending() && numSamples - i > dipDownLeft)
        {
            // Going down, with more of the block left than of the fade: hold
            // the gain where it is, so the fade ends on the block's last
            // sample and the swap can happen on the next one's first. However
            // long the host's blocks, the silence at the bottom is one sample.
            gain = editDip.value();
        }
        else
        {
            gain = editDip.next();

            if (dipDownLeft > 0 && editDip.isPending())
                --dipDownLeft;
        }

        for (int ch = 0; ch < numChannels; ++ch)
            channels[ch][i] *= gain;
    }
}

void RackProcessor::processBlockBypassed (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    // Not JUCE's pass-through, which handed the input back undelayed -- early
    // by the latency the host is compensating for, 80 samples with BMO EQ and
    // BMO Saturator at 2x -- and cleared the right channel on mono in / stereo
    // out. The bypass is the processed path with the chain taken out: the
    // input widened exactly as processBlock widens it, then delayed by the
    // latency the host was last told (BypassDelay.h).
    const auto numSamples = buffer.getNumSamples();
    const auto numOut     = juce::jmin (buffer.getNumChannels(), getTotalNumOutputChannels());

    buses::spreadInputAcrossOutputs (buffer, getTotalNumInputChannels(), numOut);
    bypassDelay.delay (buffer.getArrayOfWritePointers(), numOut, numSamples,
                       juce::jmax (0, reportedLatency.load (std::memory_order_relaxed)));

    // The host's own bad samples come back out of the delay a latency later;
    // scrubbed here, on the way out, as they always were.
    finite::scrub (buffer.getArrayOfWritePointers(), numOut, numSamples);
}

juce::AudioProcessorEditor* RackProcessor::createEditor()
{
    return new RackEditor (*this);
}

} // namespace bmo
