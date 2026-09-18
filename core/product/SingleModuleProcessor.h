#pragma once

#include "ModuleDef.h"
#include "ModuleEngine.h"
#include "ProductInfo.h"
#include "core/state/Parameters.h"
#include <juce_audio_processors/juce_audio_processors.h>

namespace bmo
{

/** One module as its own plugin.

    The module's specs become real host parameters in an APVTS; the engine
    reads them; presets and state are the APVTS's own XML with a stateVersion
    tag. Every standalone product in the suite is one of these with a
    different ModuleDef and ProductInfo -- see products/.
*/
class SingleModuleProcessor final : public juce::AudioProcessor,
                                    public PresetTarget,
                                    private juce::AudioProcessorValueTreeState::Listener,
                                    private juce::AsyncUpdater
{
public:
    SingleModuleProcessor (const ModuleDef&, ProductInfo);
    ~SingleModuleProcessor() override;

    //== AudioProcessor ========================================================
    void prepareToPlay (double sampleRate, int maximumExpectedSamplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override                          { return true; }

    const juce::String getName() const override              { return info.name; }
    bool acceptsMidi() const override                        { return false; }
    bool producesMidi() const override                       { return false; }
    bool isMidiEffect() const override                       { return false; }
    double getTailLengthSeconds() const override             { return 0.0; }

    int getNumPrograms() override                            { return 1; }
    int getCurrentProgram() override                         { return 0; }
    void setCurrentProgram (int) override                    {}
    const juce::String getProgramName (int) override         { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    //== PresetTarget =========================================================
    std::unique_ptr<juce::XmlElement> captureState() override;
    bool restoreState (const juce::XmlElement&) override;
    void resetToDefaults() override;

    //== Ours ==================================================================
    const ModuleDef& getModule() const noexcept              { return def; }
    const ProductInfo& getInfo() const noexcept              { return info; }
    juce::AudioProcessorValueTreeState& getApvts() noexcept  { return apvts; }
    ModuleEngine& getEngine() noexcept                       { return engine; }
    PresetManager& getPresets() noexcept                     { return presets; }

    ui::ModuleContext makeContext();

    /** Wide or compact, for an expandable module; always false otherwise.
        Standalone opens expanded (ModuleDef::expandedWidth). Message thread;
        kept with the session, not with presets. */
    bool isExpanded() const noexcept   { return expanded.load (std::memory_order_relaxed); }
    void setExpanded (bool shouldBe)   { expanded.store (shouldBe && def.isExpandable(), std::memory_order_relaxed); }

private:
    void parameterChanged (const juce::String&, float) override;
    void handleAsyncUpdate() override;

    static std::vector<FactoryEntry> factoryEntries (const ModuleDef&, ParamSet&);

    const ModuleDef& def;
    ProductInfo info;

    juce::AudioProcessorValueTreeState apvts;
    ModuleEngine engine;
    PresetManager presets;

    // Latency is only pushed to the host when it actually changes. Automating
    // the oversampling otherwise floods the host with setLatencySamples on
    // every move, which is enough to destabilise it.
    std::atomic<int> reportedLatency { -1 };

    std::atomic<bool> expanded { def.isExpandable() };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SingleModuleProcessor)
};

} // namespace bmo
