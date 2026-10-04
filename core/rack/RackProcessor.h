#pragma once

#include "SlotOverflow.h"
#include "SlotParameter.h"
#include "core/product/BypassDelay.h"
#include "core/dsp/SwitchFade.h"
#include "core/product/ModuleEngine.h"
#include "core/product/ProductInfo.h"
#include <array>
#include <atomic>
#include <cstdint>

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

    An edit keeps the engine of every module it does not remove or replace,
    moved or not, so its DSP state, its tail and its settings carry on through
    it; only a module arriving in the chain gets a new one. The audio thread
    hears the edit through a short dip to silence (`kEditDipMs` down, the swap,
    the same up) and is never blocked by it -- `rebuild` says how.

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

        /** The chain is about to change: drop anything pointing at its
            engines, panels included. An engine the edit keeps is the same
            object afterwards, possibly in another slot; one it removes or
            replaces is destroyed some time after this call, never before it.
            So a listener that drops everything here is always safe. */
        virtual void rackChainWillChange() = 0;
        virtual void rackChainChanged() = 0;
    };

    /** How long the output takes to fade out ahead of a chain edit, and to
        fade back in after it. A straight line each way (core/dsp/SwitchFade.h,
        `Dip`), so the whole edit is twice this plus the one sample at the
        bottom, wherever in a block it lands. */
    static constexpr double kEditDipMs = 5.0;

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

    /** A rack preset is a whole chain, so loading one is one rebuild. */
    bool factoryPresetsAreWhole() const override { return true; }

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
    void setSlotExpanded (int slot, bool shouldBe);

    const ProductInfo& getInfo() const noexcept   { return info; }
    PresetManager& getPresets() noexcept          { return presets; }

    //== AudioProcessor ========================================================
    void prepareToPlay (double sampleRate, int maximumExpectedSamplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    void processBlockBypassed (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override                          { return true; }

    const juce::String getName() const override              { return info.name; }
    bool acceptsMidi() const override                        { return false; }
    bool producesMidi() const override                       { return false; }
    bool isMidiEffect() const override                       { return false; }

    /** The chain's tail: the **sum** over the occupied slots, cached. See
        `totalTail()` for why a sum and not a maximum. Lock-free, because a
        host polls it from wherever it likes. */
    double getTailLengthSeconds() const override             { return reportedTail.load (std::memory_order_relaxed); }

    int getNumPrograms() override                            { return 1; }
    int getCurrentProgram() override                         { return 0; }
    void setCurrentProgram (int) override                    {}
    const juce::String getProgramName (int) override         { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

private:
    /** A slot as the message thread sees it, which is the chain as it is now:
        what `getEngineAt` hands out and what a state is captured from. */
    struct Slot
    {
        const ModuleDef* def = nullptr;

        // Before the engine, so it outlives it: the engine's ParamSet points
        // into it.
        std::unique_ptr<SlotOverflow> overflow;
        std::unique_ptr<ModuleEngine> engine;

        bool expanded = false;
    };

    /** The chain as the audio thread runs it: the engines in order, by
        pointer, and the edit that made it. Built by `rebuild` and never
        changed after it is published. */
    struct Chain
    {
        std::array<ModuleEngine*, kSlots> engines {};
        int size = 0;
        std::uint64_t generation = 0;
    };

    /** An engine an edit took out of the chain -- or, alone, the overflow a
        moved engine left behind -- kept alive until the audio thread has
        finished a block on a chain without it. `generation` is the first
        chain that does not hold it. */
    struct Retired
    {
        std::unique_ptr<SlotOverflow> overflow;
        std::unique_ptr<ModuleEngine> engine;
        std::uint64_t generation = 0;
    };

    /** One module of the chain an edit asks for: either the engine now in
        slot `from`, carried, or a new one for `def` restored from `state`
        (null for the module's defaults). */
    struct Entry
    {
        const ModuleDef* def = nullptr;
        int from = -1;
        std::unique_ptr<juce::XmlElement> state;
    };

    void parameterValueChanged (int, float) override;
    void parameterGestureChanged (int, bool) override {}
    void handleAsyncUpdate() override;

    /** Makes `chain` the chain, keeping every engine it carries, and tells
        the listeners either side. Message thread, or a thread holding the
        message manager's lock. See the definition for the threading. */
    void rebuild (std::vector<Entry> chain);

    /** The chain as it is, every engine carried: what an edit starts from. */
    std::vector<Entry> currentChain() const;

    /** Holds the audio thread off every lane and ParamSet until the chain
        this edit publishes is in place, and returns that chain's generation.
        Waits at most for the block in progress to finish. */
    std::uint64_t holdAudioReads();

    /** True if the host has handed the rack a block recently enough that an
        edit can leave the swap to the audio thread. */
    bool audioIsRunning() const;

    /** Puts the newest chain in place from this thread, with the audio thread
        locked out: for prepare, release, and an edit made while no audio is
        running. Needs `editLock`. */
    void completeSwap();

    /** Destroys what the audio thread has finished with. Needs `editLock`. */
    void collectRetired();

    /** The edit dip, applied to the chain's output; nothing while idle. */
    void applyDip (float* const* channels, int numChannels, int numSamples) noexcept;

    int totalLatency() const;

    /** The tail of the whole chain: every occupied slot's, **added together**.

        Not the maximum, which is the tempting answer and the wrong one. The
        slots are in series, so a four-second reverb feeding a two-second one
        is still being fed after four seconds and rings for six; taking the
        larger would cut the last two off. Erring the other way only costs a
        host some idle pulling, so the sum is the safe direction as well as
        the correct one (docs/reverb/11-integration-and-test-plan.md 2a).

        **Then clamped at `bmo::kMaxTailSeconds`, the same ceiling a module
        clamps its own figure at**, so the whole product has one rule: no BMO
        Mix Rack instance ever reports more than thirty seconds. `addModule`
        checks the slot count and not for duplicates, so eight reverbs is a
        legal chain and the honest sum of eight maxed ones is four minutes --
        free at transport stop, where over-reporting only idles the host, and
        not free for an offline bounce, where the figure is rendered onto the
        end of every export. Frosty approved it on 2026-09-21.

        The clamp is a ceiling and not an answer: a chain under it, including
        the two-reverb 8.6223 s sum the tail suite pins, is reported in full. */
    double totalTail() const;

    std::vector<FactoryEntry> factoryEntries (std::vector<RackPreset>);
    void applyPreset (const RackPreset&);

    std::vector<const ModuleDef*> registry;
    ProductInfo info;

    std::array<std::array<SlotParameter*, kParamsPerSlot>, kSlots> params {};
    std::array<Slot, kSlots> slots;

    // Engines and overflows waiting for the audio thread to let go, and every
    // published chain not yet known to be finished with. Under `editLock`.
    std::vector<Retired> retired;
    std::vector<std::unique_ptr<Chain>> chains;

    // The newest published chain, and the one the audio thread is running.
    // `live` belongs to the audio thread, and to whoever holds `chainLock`.
    std::atomic<Chain*> pending { nullptr };
    Chain* live = nullptr;

    // Bumped at the start of every edit. A block that sees it ahead of its
    // chain's generation runs that chain on held values (`processHeld`); one
    // that sees it equal to the pending chain's may swap to that chain.
    std::atomic<std::uint64_t> editGeneration { 0 };

    // The generation of the chain the audio thread ran its last block on, set
    // at the end of the block: what `collectRetired` may destroy up to.
    std::atomic<std::uint64_t> audioGeneration { 0 };

    // True while the audio thread is inside a block. With `editGeneration`
    // it is the handshake in `holdAudioReads`.
    std::atomic<bool> inBlock { false };

    // When the audio thread last finished a block, in milliseconds; 0 for
    // never since the last prepare or release.
    std::atomic<juce::uint32> lastBlockMs { 0 };

    // The audio thread's, and `chainLock`'s holder's.
    dsp::Dip editDip;
    int dipLength = 1, dipDownLeft = 0;

    // Serialises everything that changes or reads the chain off the audio
    // thread: edits, restores, prepare and release, state captures. The audio
    // thread never takes it.
    juce::CriticalSection editLock;

    // Held by the audio thread for each block, with a try-lock it never waits
    // on, and by anything that must change the audio thread's own state:
    // prepare, release, and an edit made while no audio is running. A block
    // that finds it taken goes out silent.
    juce::CriticalSection chainLock;

    double currentRate = 0.0;
    int currentBlock = 0, currentChannels = 0;
    bool prepared = false;

    juce::ListenerList<Listener> listeners;
    PresetManager presets;

    std::atomic<int> reportedLatency { -1 };

    // The host's bypass, delayed by the reported latency and fed on every
    // processed block so that switching to it stays in step (BypassDelay.h).
    BypassDelay bypassDelay;

    // The summed tail, cached for the getter. Refreshed wherever the latency
    // is -- on a parameter change and in prepareToPlay -- and a chain edit
    // reaches both through the triggerAsyncUpdate() at the end of rebuild().
    std::atomic<double> reportedTail { 0.0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RackProcessor)
};

} // namespace bmo
