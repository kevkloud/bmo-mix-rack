#include "SingleModuleProcessor.h"
#include "BusLayouts.h"
#include "ProductEditor.h"

namespace bmo
{

SingleModuleProcessor::SingleModuleProcessor (const ModuleDef& d, ProductInfo i)
    : juce::AudioProcessor (BusesProperties()
          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      def (d), info (std::move (i)),
      apvts (*this, nullptr, "PARAMS", makeLayout (d.specs, i.versionHint)),
      engine (d, ParamSet (d.specs, collect (apvts, d.specs))),
      presets (*this, info.presets, factoryEntries (d, engine.params()))
{
    for (const auto& s : def.specs)
        apvts.addParameterListener (s.id, this);

    // Answer honestly before the first prepareToPlay: a host is entitled to
    // ask an instance it has only just constructed, and for BMO Linger the
    // default schema is already several seconds of tail.
    reportedTail.store (engine.tailSeconds(), std::memory_order_relaxed);
}

// getTailLengthSeconds() may be called from the audio thread, so the cache it
// reads has to be a load and not a lock. Stated rather than assumed.
static_assert (std::atomic<double>::is_always_lock_free,
               "getTailLengthSeconds() is polled from the audio thread");

SingleModuleProcessor::~SingleModuleProcessor()
{
    for (const auto& s : def.specs)
        apvts.removeParameterListener (s.id, this);

    cancelPendingUpdate();
}

std::vector<FactoryEntry> SingleModuleProcessor::factoryEntries (const ModuleDef& d, ParamSet& params)
{
    std::vector<FactoryEntry> out;

    for (const auto& p : d.factoryPresets)
        out.push_back ({ p.name, [&params, &p] { params.apply (p.settings); } });

    return out;
}

//==============================================================================
void SingleModuleProcessor::parameterChanged (const juce::String&, float)
{
    // Fired from whichever thread moved the parameter, so this stays
    // allocation- and lock-free: a flag and an async trigger.
    presets.noteChange();
    triggerAsyncUpdate();
}

void SingleModuleProcessor::handleAsyncUpdate()
{
    const auto latency = engine.latency();

    if (reportedLatency.exchange (latency, std::memory_order_relaxed) != latency)
        setLatencySamples (latency);

    // No change-detection and no updateHostDisplay: the host pulls the tail
    // when it wants it, so refreshing the cache is the whole job.
    reportedTail.store (engine.tailSeconds(), std::memory_order_relaxed);
}

//==============================================================================
void SingleModuleProcessor::prepareToPlay (double sampleRate, int maximumExpectedSamplesPerBlock)
{
    engine.prepare (sampleRate, maximumExpectedSamplesPerBlock, getTotalNumOutputChannels());

    const auto latency = engine.latency();
    reportedLatency.store (latency, std::memory_order_relaxed);
    setLatencySamples (latency);

    reportedTail.store (engine.tailSeconds(), std::memory_order_relaxed);
}

void SingleModuleProcessor::releaseResources()
{
    engine.reset();
}

bool SingleModuleProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    // Mono or stereo, and mono in to stereo out only for a module that opts in
    // (ModuleDef::acceptsMonoInput). See core/product/BusLayouts.h, which the
    // rack answers from as well.
    return buses::isSupported (layouts, def.acceptsMonoInput);
}

void SingleModuleProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    // Denormals in IIR filter tails cost roughly 100x CPU and are a classic
    // source of mystery dropouts. Must be first.
    juce::ScopedNoDenormals noDenormals;

    const auto numSamples = buffer.getNumSamples();
    const auto numIn      = getTotalNumInputChannels();
    const auto numOut     = getTotalNumOutputChannels();

    // Mono in, stereo out: the module is given the input in both channels, not
    // one channel and silence. BusLayouts.h says why at length.
    buses::spreadInputAcrossOutputs (buffer, numIn, numOut);

    engine.process (buffer.getArrayOfWritePointers(), numOut, numSamples);
}

//==============================================================================
juce::AudioProcessorEditor* SingleModuleProcessor::createEditor()
{
    return new ProductEditor (*this);
}

ui::ModuleContext SingleModuleProcessor::makeContext()
{
    return { engine.params(), def,
             [this] { return engine.meter().maxPeak(); },
             [this] { return engine.meter().maxRms(); },
             [this] { return engine.inputMeter().maxPeak(); },
             [this] { return engine.inputMeter().maxRms(); },
             [this] { return engine.gainReduction().get(); },
             [this] { return engine.sampleRate(); },
             [this] (int band) { engine.setSolo (band); },
             engine.analyser() };
}

//==============================================================================
std::unique_ptr<juce::XmlElement> SingleModuleProcessor::captureState()
{
    return engine.params().toXml (info.stateVersion);
}

bool SingleModuleProcessor::restoreState (const juce::XmlElement& xml)
{
    if (! xml.hasTagName (ParamSet::kRootTag))
        return false;

    // Future versions migrate here, keyed off the stored stateVersion.
    engine.restoreState (xml);
    return true;
}

void SingleModuleProcessor::resetToDefaults()
{
    engine.params().resetToDefaults();
}

void SingleModuleProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = captureState())
    {
        // The view goes in the session and nowhere else: captureState is also
        // what a preset file is written from, and a preset is sound, not a
        // window size. Modules with one width write nothing new at all.
        if (def.isExpandable())
            xml->setAttribute (kViewAttribute, isExpanded() ? kViewExpanded : kViewCompact);

        copyXmlToBinary (*xml, destData);
    }
}

void SingleModuleProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
    {
        restoreState (*xml);

        // A session saved before the module could expand has no view, and
        // opens the way a new instance does.
        if (def.isExpandable() && xml->hasAttribute (kViewAttribute))
            setExpanded (xml->getStringAttribute (kViewAttribute) == kViewExpanded);
    }
}

} // namespace bmo
