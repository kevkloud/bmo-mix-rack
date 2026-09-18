#pragma once

#include "SlotOverflow.h"
#include "SlotParameter.h"
#include "core/product/ModuleEngine.h"
#include "core/product/ProductInfo.h"
#include <array>

namespace bmo
{

/** A rack preset: which modules, in what order, with what settings. */
struct RackPreset
{
    struct Entry
    {
        const char* moduleId;
        std::vector<Setting> settings;
    };

    const char* name;
    std::vector<Entry> chain;
};

/** The rack: up to eight modules in series, each compiled in and driven
    through a fixed grid of generic host parameters.

    Slots are a list, not an array with holes: slot 0 is always the first
    module in the chain. Adding a module appends; removing one closes the gap;
    moving one shuffles the rest. Each shuffle re-assigns the slot parameters,
    which is why a host's automation lanes follow the slot, not the module --
    the plan calls this scheme A, and the rack tests pin the mapping.

    A module is not limited to the 32 lanes a slot has. Its first 32
    parameters take them; any past that are held off the grid in a
    SlotOverflow, where everything but host automation still reaches them.
*/
class RackProcessor final : public juce::AudioProcessor,
                            public PresetTarget,
                            private juce::AudioProcessorParameter::Listener,
                            private juce::AsyncUpdater
{
public:
    static constexpr int kSlots         = 8;

    /** Host lanes per slot -- permanent, since sessions reference them. Not a
        cap on a module's parameter count: see SlotOverflow. */
    static constexpr int kParamsPerSlot = 32;

    static constexpr auto kRootTag = "RACK";
    static constexpr auto kSlotTag = "SLOT";

    /** Anyone drawing the rack. All calls on the message thread. */
    class Listener
    {
    public:
        virtual ~Listener() = default;

        /** The engines are about to be replaced: drop anything pointing at
            them, panels included. */
        virtual void rackChainWillChange() = 0;
        virtual void rackChainChanged() = 0;
    };

    RackProcessor (std::vector<const ModuleDef*> registry, ProductInfo, std::vector<RackPreset>);
    ~RackProcessor() override;

    //== The chain ============================================================
    const std::vector<const ModuleDef*>& getRegistry() const noexcept { return registry; }
    const ModuleDef* findModule (const juce::String& id) const noexcept;

    int getNumModules() const noexcept;
    const ModuleDef* getModuleAt (int slot) const noexcept;
    ModuleEngine* getEngineAt (int slot) noexcept;

    /** Appends. False if the rack is full. */
    bool addModule (const ModuleDef&);

    /** Puts a module in a slot, replacing whatever is there. `slot` may be
        one past the end, which appends. */
    void setModule (int slot, const ModuleDef&);
    void removeModule (int slot);
    void moveModule (int from, int to);
    void clearChain();

    /** Whole-chain state: <RACK stateVersion=..><SLOT index module schema><PARAMS/></SLOT>... */
    std::unique_ptr<juce::XmlElement> captureState() override;
    bool restoreState (const juce::XmlElement&) override;
    void resetToDefaults() override;

    void addRackListener (Listener* l)    { listeners.add (l); }
    void removeRackListener (Listener* l) { listeners.remove (l); }

    SlotParameter& getSlotParameter (int slot, int index) noexcept
    {
        return *params[(size_t) slot][(size_t) index];
    }

    ui::ModuleContext makeContext (int slot);

    /** Wide or compact, for an expandable module in `slot`; false otherwise.
        A module arrives in a rack compact (ModuleDef::expandedWidth). The view
        travels with the module through chain edits and is kept with the
        session, not with rack presets. Message thread. */
    bool isSlotExpanded (int slot) const noexcept;
    void setSlotExpanded (int slot, bool shouldBe) noexcept;

    const ProductInfo& getInfo() const noexcept   { return info; }
    PresetManager& getPresets() noexcept          { return presets; }

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

private:
    struct Slot
    {
        const ModuleDef* def = nullptr;

        // Before the engine, so it outlives it: the engine's ParamSet points
        // into it.
        std::unique_ptr<SlotOverflow> overflow;
        std::unique_ptr<ModuleEngine> engine;

        bool expanded = false;
    };

    void parameterValueChanged (int, float) override;
    void parameterGestureChanged (int, bool) override {}
    void handleAsyncUpdate() override;

    /** Rebuilds every engine from `chain`, under the lock, and tells the
        listeners either side. `chain` is the new list of (module, state). */
    void rebuild (std::vector<std::pair<const ModuleDef*, std::unique_ptr<juce::XmlElement>>>);

    /** The chain as (module, state) pairs, for editing. */
    std::vector<std::pair<const ModuleDef*, std::unique_ptr<juce::XmlElement>>> snapshot();

    int totalLatency() const;

    std::vector<FactoryEntry> factoryEntries (std::vector<RackPreset>);
    void applyPreset (const RackPreset&);

    std::vector<const ModuleDef*> registry;
    ProductInfo info;

    std::array<std::array<SlotParameter*, kParamsPerSlot>, kSlots> params {};
    std::array<Slot, kSlots> slots;

    // Guards the engines. processBlock tries it and passes the audio through
    // untouched if the message thread is mid-swap, which is a few samples of
    // dry signal once per module change rather than a lock on the audio
    // thread.
    juce::CriticalSection chainLock;

    double currentRate = 0.0;
    int currentBlock = 0, currentChannels = 0;
    bool prepared = false;

    juce::ListenerList<Listener> listeners;
    PresetManager presets;

    std::atomic<int> reportedLatency { -1 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RackProcessor)
};

} // namespace bmo
