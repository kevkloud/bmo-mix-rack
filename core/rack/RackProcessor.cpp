#include "RackProcessor.h"
#include "RackEditor.h"

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

void RackProcessor::setSlotExpanded (int slot, bool shouldBe) noexcept
{
    if (slot >= 0 && slot < kSlots)
    {
        auto& s = slots[(size_t) slot];
        s.expanded = shouldBe && s.def != nullptr && s.def->isExpandable();
    }
}

//==============================================================================
std::vector<std::pair<const ModuleDef*, std::unique_ptr<juce::XmlElement>>> RackProcessor::snapshot()
{
    std::vector<std::pair<const ModuleDef*, std::unique_ptr<juce::XmlElement>>> chain;

    // The view rides on the module's own state, so a chain edit that moves the
    // module takes its view with it, the same way it takes its settings.
    for (auto& s : slots)
        if (s.def != nullptr && s.engine != nullptr)
        {
            auto state = s.engine->params().toXml (s.def->schemaVersion);

            if (s.expanded)
                state->setAttribute (kViewAttribute, kViewExpanded);

            chain.emplace_back (s.def, std::move (state));
        }

    return chain;
}

void RackProcessor::rebuild (std::vector<std::pair<const ModuleDef*, std::unique_ptr<juce::XmlElement>>> chain)
{
    jassert (juce::MessageManager::getInstance()->isThisTheMessageThread());
    jassert ((int) chain.size() <= kSlots);

    listeners.call ([] (Listener& l) { l.rackChainWillChange(); });

    {
        const juce::ScopedLock lock (chainLock);

        for (int s = 0; s < kSlots; ++s)
        {
            auto& slot = slots[(size_t) s];
            slot.engine.reset();
            slot.overflow.reset();
            slot.def = s < (int) chain.size() ? chain[(size_t) s].first : nullptr;

            // Compact unless the module's state says otherwise: a module newly
            // added, a preset's chain and an old session all arrive compact.
            const auto* carried = s < (int) chain.size() ? chain[(size_t) s].second.get() : nullptr;
            slot.expanded = slot.def != nullptr && slot.def->isExpandable() && carried != nullptr
                         && carried->getStringAttribute (kViewAttribute) == kViewExpanded;

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

            slot.engine = std::make_unique<ModuleEngine> (*slot.def, ParamSet (slot.def->specs, assigned));

            if (auto* state = chain[(size_t) s].second.get())
                slot.engine->params().applyXml (*state);

            if (prepared)
                slot.engine->prepare (currentRate, currentBlock, currentChannels);
        }
    }

    // Names, ranges and steps of up to 256 parameters just changed.
    updateHostDisplay (ChangeDetails().withParameterInfoChanged (true));
    triggerAsyncUpdate();

    listeners.call ([] (Listener& l) { l.rackChainChanged(); });
}

bool RackProcessor::addModule (const ModuleDef& def)
{
    if (getNumModules() >= kSlots)
        return false;

    auto chain = snapshot();
    chain.emplace_back (&def, nullptr);
    rebuild (std::move (chain));
    presets.noteChange();
    return true;
}

void RackProcessor::setModule (int slot, const ModuleDef& def)
{
    auto chain = snapshot();

    if (slot >= 0 && slot < (int) chain.size())
        chain[(size_t) slot] = { &def, nullptr };
    else if ((int) chain.size() < kSlots)
        chain.emplace_back (&def, nullptr);
    else
        return;

    rebuild (std::move (chain));
    presets.noteChange();
}

void RackProcessor::removeModule (int slot)
{
    auto chain = snapshot();

    if (slot < 0 || slot >= (int) chain.size())
        return;

    chain.erase (chain.begin() + slot);
    rebuild (std::move (chain));
    presets.noteChange();
}

void RackProcessor::moveModule (int from, int to)
{
    auto chain = snapshot();
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

    std::vector<std::pair<const ModuleDef*, std::unique_ptr<juce::XmlElement>>> chain;

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

        chain.emplace_back (def, std::move (state));
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
    std::vector<std::pair<const ModuleDef*, std::unique_ptr<juce::XmlElement>>> chain;

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

        chain.emplace_back (def, std::move (state));
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

void RackProcessor::handleAsyncUpdate()
{
    const auto latency = totalLatency();

    if (reportedLatency.exchange (latency, std::memory_order_relaxed) != latency)
        setLatencySamples (latency);
}

//==============================================================================
void RackProcessor::prepareToPlay (double sampleRate, int maximumExpectedSamplesPerBlock)
{
    const juce::ScopedLock lock (chainLock);

    currentRate     = sampleRate;
    currentBlock    = maximumExpectedSamplesPerBlock;
    currentChannels = getTotalNumOutputChannels();
    prepared        = true;

    for (auto& s : slots)
        if (s.engine != nullptr)
            s.engine->prepare (currentRate, currentBlock, currentChannels);

    const auto latency = totalLatency();
    reportedLatency.store (latency, std::memory_order_relaxed);
    setLatencySamples (latency);
}

void RackProcessor::releaseResources()
{
    const juce::ScopedLock lock (chainLock);

    for (auto& s : slots)
        if (s.engine != nullptr)
            s.engine->reset();
}

bool RackProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& out = layouts.getMainOutputChannelSet();

    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;

    return layouts.getMainInputChannelSet() == out;
}

void RackProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const auto numSamples = buffer.getNumSamples();
    const auto numIn      = getTotalNumInputChannels();
    const auto numOut     = getTotalNumOutputChannels();

    for (int ch = numIn; ch < numOut; ++ch)
        buffer.clear (ch, 0, numSamples);

    const juce::ScopedTryLock lock (chainLock);

    if (! lock.isLocked())
        return;

    for (auto& s : slots)
        if (s.engine != nullptr)
            s.engine->process (buffer.getArrayOfWritePointers(), numOut, numSamples);
}

juce::AudioProcessorEditor* RackProcessor::createEditor()
{
    return new RackEditor (*this);
}

} // namespace bmo
