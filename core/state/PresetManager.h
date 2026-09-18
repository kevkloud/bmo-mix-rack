#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <atomic>
#include <functional>

namespace bmo
{

/** What a preset is taken from and applied to. A single module's parameter
    set, or the rack's whole chain. */
class PresetTarget
{
public:
    virtual ~PresetTarget() = default;

    /** The current state, in the same XML the plugin hands the host. */
    virtual std::unique_ptr<juce::XmlElement> captureState() = 0;

    /** Defaults first, then the element. False if it is not ours. */
    virtual bool restoreState (const juce::XmlElement&) = 0;

    virtual void resetToDefaults() = 0;
};

struct FactoryEntry
{
    juce::String name;
    std::function<void()> apply;    ///< runs after resetToDefaults()
};

/** Left in a preset folder once the copy from an older name has run. Not a
    preset: it does not carry the product's extension, so nothing lists it. */
inline constexpr auto kMigrationMarker = ".migrated";

/** A folder and extension a product used to write, before it was renamed. */
struct LegacyPreset
{
    juce::String folderName;        ///< "BMO EQ": under LT3 Audio/
    juce::String extension;         ///< ".bmoeq"
};

/** Where a product keeps its presets. */
struct PresetInfo
{
    juce::String folderName;        ///< "BMO CEQ": under LT3 Audio/
    juce::String extension;         ///< ".bmoceq"

    /** A product that was renamed keeps reading what it used to write. On
        first run the old folders' files are copied across under the new
        extension, and a marker left in the new folder stops the copy ever
        running again -- so a preset the user deletes afterwards stays deleted.

        **Newest first.** A product renamed twice can have the same preset name
        sitting in two old folders, and the copy never overwrites, so whichever
        folder is listed first wins. That has to be the most recent one: the
        older file is the same preset from before the user's later edits. */
    std::vector<LegacyPreset> legacy {};
};

/** Presets, both the ones that ship and the user's own.

    A preset is the same XML the plugin hands the host when it saves state, so
    a preset file and a saved session hold the same thing and stay compatible
    through the same version tag. User presets are plain files in a folder the
    user can open, copy from and back up; sharing one is sending a file.

    Loading always resets everything to its default first. Without that a
    preset silently inherits whatever the previous one left set, which is the
    usual way preset systems come to be quietly wrong.
*/
class PresetManager
{
public:
    PresetManager (PresetTarget&, PresetInfo, std::vector<FactoryEntry>);

    juce::File directory() const;
    juce::String extension() const { return info.extension; }

    /** Redirects every preset folder. Tests use it so they never write into
        the user's own presets; nothing else should. */
    static void setDirectoryForTesting (const juce::File&);

    //== Listing ==============================================================
    const std::vector<FactoryEntry>& getFactory() const noexcept { return factoryPresets; }
    juce::StringArray getUserNames() const;

    //== Loading ==============================================================
    void loadFactory (int index);
    void loadUser (const juce::String& name);
    bool loadFile (const juce::File&);

    /** Steps through the factory presets and then the user's, so the arrows
        walk the same list the menu shows. */
    void step (int delta);

    //== Storing ==============================================================
    bool saveUser (const juce::String& name);
    bool exportTo (juce::File destination);
    bool importFrom (const juce::File& source);
    bool deleteUser (const juce::String& name);

    //== What is loaded =======================================================
    juce::String getCurrentName() const { return currentName; }

    /** True once any control has moved since the preset was loaded. */
    bool isEdited() const noexcept { return edited.load (std::memory_order_relaxed); }

    /** The owner calls this from wherever it hears a parameter move. Safe from
        any thread: an atomic store and nothing else. */
    void noteChange() noexcept
    {
        if (! loading.load (std::memory_order_relaxed))
            edited.store (true, std::memory_order_relaxed);
    }

private:
    void migrateLegacy();

    /** A product folder by name, through the test redirection if one is set.
        `directory()` is this for `info.folderName`; migration needs it for the
        old names too, which is what used to make the copy untestable. */
    juce::File folderFor (const juce::String& name) const;

    PresetTarget& target;
    PresetInfo info;
    std::vector<FactoryEntry> factoryPresets;

    juce::String currentName { "Init" };
    std::atomic<bool> edited { false };
    std::atomic<bool> loading { false };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PresetManager)
};

/** Root of the suite's preset tree: ~/Library/Audio/Presets/LT3 Audio on
    macOS, <AppData>/LT3 Audio elsewhere. Themes live beside the products. */
juce::File suitePresetRoot();

} // namespace bmo
