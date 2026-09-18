/*
    BMO Saturator as a host sees it: schema, presets, level. See EqTests.cpp
    for why the schema table is written out in full. The table is what
    BMO Saturator 0.2 shipped with; "tone" is last because it arrived last.
*/

#include "TestUtil.h"
#include "modules/sat/presets/FactoryPresets.h"
#include "products/sat/Product.h"

using namespace test;
namespace P = bmo::sat;

namespace
{
    const Expected kSchema[]
    {
        { P::kInputGain,    "Input",        -24.0f,  24.0f,   0.0f, 0 },
        { P::kDrive,        "Drive",          0.0f, 100.0f,  40.0f, 0 },
        { P::kMix,          "Mix",            0.0f, 100.0f, 100.0f, 0 },
        { P::kOutputLevel,  "Output",       -24.0f,  24.0f,   0.0f, 0 },
        { P::kSatIn,        "Sat In",         0.0f,   1.0f,   1.0f, 2 },
        { P::kPhase,        "Phase",          0.0f,   1.0f,   0.0f, 2 },
        { P::kAutoGain,     "Auto Gain",      0.0f,   1.0f,   0.0f, 2 },
        { P::kOversampling, "Oversampling",   0.0f,   3.0f,   0.0f, 4 },
        { P::kTone,         "Tone",           0.0f, 100.0f, 100.0f, 0 },
    };
}

int main()
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    using bmo::products::createSat;

    //== The golden schema =====================================================
    {
        auto proc = createSat();
        checkSchema (*proc, kSchema);
        check (P::specs().size() == (size_t) P::Index::count, "the Index enum matches specs()");
    }

    //== Displayed values ======================================================
    {
        auto proc = createSat();

        setValue (*proc, P::kDrive, 40.0f);
        check (param (*proc, P::kDrive).getCurrentValueAsText() == "40 %",
               "Drive should read '40 %', got '" + param (*proc, P::kDrive).getCurrentValueAsText() + "'");

        setValue (*proc, P::kOutputLevel, -3.0f);
        check (param (*proc, P::kOutputLevel).getCurrentValueAsText() == "-3.0 dB",
               "Output should read '-3.0 dB', got '" + param (*proc, P::kOutputLevel).getCurrentValueAsText() + "'");

        setValue (*proc, P::kOversampling, 3.0f);
        check (param (*proc, P::kOversampling).getCurrentValueAsText() == "8x",
               "Oversampling 3 should read '8x', got '" + param (*proc, P::kOversampling).getCurrentValueAsText() + "'");
    }

    //== Latency ==============================================================
    {
        auto proc = createSat();
        proc->setPlayConfigDetails (2, 2, 48000.0, 512);

        setValue (*proc, P::kOversampling, 0.0f);
        proc->prepareToPlay (48000.0, 512);
        check (proc->getLatencySamples() == 0, "oversampling off reports zero latency");

        setValue (*proc, P::kOversampling, 1.0f);
        proc->prepareToPlay (48000.0, 512);
        const auto at2x = proc->getLatencySamples();
        check (at2x > 0, "2x oversampling reports its latency");

        setValue (*proc, P::kOversampling, 3.0f);
        proc->prepareToPlay (48000.0, 512);
        check (proc->getLatencySamples() > at2x, "8x reports more latency than 2x");
    }

    //== State round-trip ======================================================
    {
        juce::MemoryBlock state;

        const bmo::Setting settings[] {
            { P::kInputGain,    6.0f },
            { P::kDrive,       72.5f },
            { P::kMix,         33.0f },
            { P::kOutputLevel, -4.5f },
            { P::kSatIn,        0.0f },
            { P::kPhase,        1.0f },
            { P::kAutoGain,     1.0f },
            { P::kOversampling, 2.0f },
            { P::kTone,        25.0f },
        };

        {
            auto proc = createSat();
            for (const auto& s : settings) setValue (*proc, s.id, s.value);
            proc->getStateInformation (state);
        }

        auto restored = createSat();
        restored->setStateInformation (state.getData(), (int) state.getSize());

        for (const auto& s : settings)
            checkClose (getValue (*restored, s.id), s.value, 0.01,
                        juce::String ("state round-trip of '") + s.id + "'");

        auto xml = juce::AudioProcessor::getXmlFromBinary (state.getData(), (int) state.getSize());
        check (xml != nullptr && xml->hasTagName ("PARAMS") && xml->hasAttribute ("stateVersion"),
               "saved state is <PARAMS stateVersion=..>");

        // A 0.1 state has no "tone": it must come back at the default so old
        // sessions sound as they did.
        {
            juce::XmlElement legacy ("PARAMS");
            legacy.setAttribute ("stateVersion", 1);
            auto* e = legacy.createNewChildElement ("PARAM");
            e->setAttribute ("id", P::kDrive);
            e->setAttribute ("value", 61.0);

            juce::MemoryBlock block;
            juce::AudioProcessor::copyXmlToBinary (legacy, block);

            auto proc = createSat();
            setValue (*proc, P::kTone, 10.0f);
            proc->setStateInformation (block.getData(), (int) block.getSize());
            checkClose (getValue (*proc, P::kDrive), 61.0, 0.01, "an old state loads");
            checkClose (getValue (*proc, P::kTone), 100.0, 0.01, "a missing parameter comes back at its default");
        }
    }

    //== Presets ==============================================================
    {
        const auto sandbox = juce::File::getSpecialLocation (juce::File::tempDirectory)
                                 .getChildFile ("bmo-sat-preset-tests");
        sandbox.deleteRecursively();
        bmo::PresetManager::setDirectoryForTesting (sandbox);

        auto proc = createSat();
        auto& presets = proc->getPresets();

        check (presets.getFactory().size() == P::factory().size(), "every factory preset is listed");
        check (presets.getCurrentName() == "Init", "the plugin starts on Init");
        check (presets.extension() == ".bmosat", "presets are .bmosat files");

        for (int i = 0; i < (int) P::factory().size(); ++i)
        {
            const auto& preset = P::factory()[(size_t) i];
            presets.loadFactory (i);
            check (presets.getCurrentName() == preset.name, "loading names the preset");

            for (const auto& s : preset.settings)
                checkClose (getValue (*proc, s.id), s.value, 0.01,
                            juce::String ("preset \"") + preset.name + "\" sets " + s.id);
        }

        presets.loadFactory (0);
        setValue (*proc, P::kDrive, 88.0f);
        check (presets.isEdited(), "moving a control marks the preset edited");
        check (presets.saveUser ("Round Trip"), "a user preset saves");
        presets.loadFactory (0);
        presets.loadUser ("Round Trip");
        checkClose (getValue (*proc, P::kDrive), 88.0, 0.01, "user preset restores drive");
        check (presets.deleteUser ("Round Trip"), "a user preset deletes");

        sandbox.deleteRecursively();
        bmo::PresetManager::setDirectoryForTesting ({});
    }

    //== A preset must not change how loud the track is ======================
    // Saturation adds harmonics, so the tolerance is looser than the EQ's,
    // but a preset that is 3 dB louder is a preset that sounds "better" for
    // the wrong reason.
    {
        auto proc = createSat();
        proc->setPlayConfigDetails (2, 2, 48000.0, 512);
        proc->prepareToPlay (48000.0, 512);

        const auto source = voice (512 * 300);
        const auto sourceDb = rmsDb (source);
        const auto& factory = proc->getPresets().getFactory();
        const bool print = std::getenv ("BMO_PRINT_PRESET_LEVELS") != nullptr;

        for (int index = 1; index < (int) factory.size(); ++index)
        {
            proc->getPresets().loadFactory (index);
            proc->reset();

            const auto outDb = outputDb (*proc, source, 512, 20);

            if (print)
                std::cout << factory[(size_t) index].name << ": " << (outDb - sourceDb) << " dB\n";

            checkClose (outDb - sourceDb, 0.0, 2.5,
                        juce::String ("preset '") + factory[(size_t) index].name + "' comes out near the level it went in");
        }
    }

    return finish ("BMO Saturator");
}
