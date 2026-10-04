/*
    BMO Util as a host sees it. The schema is new in 1.0, and from here on it
    is frozen the same way the others are.
*/

#include "TestUtil.h"
#include "modules/util/presets/FactoryPresets.h"
#include "products/util/Product.h"

using namespace test;
namespace P = bmo::util;

namespace
{
    const Expected kSchema[]
    {
        { P::kGain,   "Gain",   -24.0f,  24.0f,   0.0f, 0 },
        { P::kPan,    "Pan",   -100.0f, 100.0f,   0.0f, 0 },
        { P::kWidth,  "Width",    0.0f, 200.0f, 100.0f, 0 },
        { P::kPhaseL, "Phase L",  0.0f,   1.0f,   0.0f, 2 },
        { P::kPhaseR, "Phase R",  0.0f,   1.0f,   0.0f, 2 },
        { P::kMono,   "Mono",     0.0f,   1.0f,   0.0f, 2 },
    };
}

int main()
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    using bmo::products::createUtil;

    {
        auto proc = createUtil();
        checkSchema (*proc, kSchema);
        check (P::specs().size() == (size_t) P::Index::count, "the Index enum matches specs()");
    }

    //== Displayed values ======================================================
    {
        auto proc = createUtil();

        setValue (*proc, P::kPan, -50.0f);
        check (param (*proc, P::kPan).getCurrentValueAsText() == "L 50",
               "Pan should read 'L 50', got '" + param (*proc, P::kPan).getCurrentValueAsText() + "'");

        setValue (*proc, P::kPan, 0.0f);
        check (param (*proc, P::kPan).getCurrentValueAsText() == "C",
               "centre pan should read 'C', got '" + param (*proc, P::kPan).getCurrentValueAsText() + "'");

        setValue (*proc, P::kWidth, 150.0f);
        check (param (*proc, P::kWidth).getCurrentValueAsText() == "150 %", "Width reads as a percentage");

        setValue (*proc, P::kGain, -3.0f);
        check (param (*proc, P::kGain).getCurrentValueAsText() == "-3.0 dB", "Gain reads in decibels");
    }

    //== Latency ==============================================================
    {
        auto proc = createUtil();
        proc->setPlayConfigDetails (2, 2, 48000.0, 512);
        proc->prepareToPlay (48000.0, 512);
        check (proc->getLatencySamples() == 0, "util has no latency");
    }

    //== State round-trip ======================================================
    {
        juce::MemoryBlock state;

        const bmo::Setting settings[] {
            { P::kGain,   -7.5f },
            { P::kPan,    30.0f },
            { P::kWidth, 140.0f },
            { P::kPhaseL,  1.0f },
            { P::kPhaseR,  0.0f },
            { P::kMono,    1.0f },
        };

        {
            auto proc = createUtil();
            for (const auto& s : settings) setValue (*proc, s.id, s.value);
            proc->getStateInformation (state);
        }

        auto restored = createUtil();
        restored->setStateInformation (state.getData(), (int) state.getSize());

        for (const auto& s : settings)
            checkClose (getValue (*restored, s.id), s.value, 0.01,
                        juce::String ("state round-trip of '") + s.id + "'");
    }

    //== Presets ==============================================================
    {
        const auto sandbox = juce::File::getSpecialLocation (juce::File::tempDirectory)
                                 .getChildFile ("bmo-util-preset-tests");
        sandbox.deleteRecursively();
        bmo::PresetManager::setDirectoryForTesting (sandbox);

        auto proc = createUtil();
        auto& presets = proc->getPresets();

        check (presets.getFactory().size() == P::factory().size(), "every factory preset is listed");
        check (presets.extension() == ".bmoutil", "presets are .bmoutil files");

        for (int i = 0; i < (int) P::factory().size(); ++i)
        {
            const auto& preset = P::factory()[(size_t) i];
            presets.loadFactory (i);

            for (const auto& s : preset.settings)
                checkClose (getValue (*proc, s.id), s.value, 0.01,
                            juce::String ("preset \"") + preset.name + "\" sets " + s.id);
        }

        // The names, in order. A preset is named for what it does: width 200
        // doubles the side and keeps the mid (+3.99 dB RMS on uncorrelated
        // noise), so it is "Wide"; until 2026-10-03 it was "Side Only".
        {
            juce::StringArray names;
            for (const auto& p : P::factory())
                names.add (p.name);

            check (names == juce::StringArray { "Init", "Mono Check", "Flip Polarity", "Wide", "Narrow", "Pad -6" },
                   "the factory presets are Init, Mono Check, Flip Polarity, Wide, Narrow, Pad -6; got "
                       + names.joinIntoString (", "));
        }

        // Nothing saved refers to a factory preset by name: a session and a
        // user preset hold parameter values only, so renaming one cannot
        // break either.
        for (int i = 0; i < (int) P::factory().size(); ++i)
        {
            const juce::String name { P::factory()[(size_t) i].name };
            presets.loadFactory (i);

            juce::MemoryBlock session;
            proc->getStateInformation (session);
            const auto xml = juce::AudioProcessor::getXmlFromBinary (session.getData(), (int) session.getSize());
            check (xml != nullptr && ! xml->toString().contains (name),
                   "a session saved after \"" + name + "\" does not name it");

            check (presets.saveUser ("Named Check"), "a user preset saves after \"" + name + "\"");
            check (! presets.directory().getChildFile ("Named Check" + presets.extension()).loadFileAsString().contains (name),
                   "a user preset saved after \"" + name + "\" does not name it");
            presets.deleteUser ("Named Check");
        }

        presets.loadFactory (0);
        setValue (*proc, P::kWidth, 30.0f);
        check (presets.saveUser ("Round Trip"), "a user preset saves");
        presets.loadFactory (0);
        presets.loadUser ("Round Trip");
        checkClose (getValue (*proc, P::kWidth), 30.0, 0.01, "user preset restores width");
        check (presets.deleteUser ("Round Trip"), "a user preset deletes");

        sandbox.deleteRecursively();
        bmo::PresetManager::setDirectoryForTesting ({});
    }

    //== Through the plugin, the arithmetic still holds ======================
    {
        auto proc = createUtil();
        proc->setPlayConfigDetails (2, 2, 48000.0, 512);
        proc->prepareToPlay (48000.0, 512);

        setValue (*proc, P::kGain, -6.0206f);
        setValue (*proc, P::kPhaseR, 1.0f);

        juce::AudioBuffer<float> buffer (2, 512);
        juce::MidiBuffer midi;

        for (int b = 0; b < 200; ++b)
        {
            for (int ch = 0; ch < 2; ++ch)
                juce::FloatVectorOperations::fill (buffer.getWritePointer (ch), 1.0f, 512);

            proc->processBlock (buffer, midi);
        }

        checkClose (buffer.getSample (0, 511),  0.5, 0.005, "gain -6 dB on the left");
        checkClose (buffer.getSample (1, 511), -0.5, 0.005, "gain -6 dB and polarity on the right");
    }

    return finish ("BMO Util");
}
