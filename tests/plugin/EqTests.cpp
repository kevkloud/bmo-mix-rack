/*
    BMO CEQ as a host sees it: the parameter schema, presets, and level.

    The schema block is the important one and the least interesting to read.
    A parameter's ID, its position in the list, its range and its default are
    all load-bearing forever: automation lanes and saved state are keyed by
    them, and stored automation is normalised, so widening a range silently
    rescales every automation point a user ever wrote. Nothing errors, nothing
    warns, and the session simply comes back wrong months later.

    So the schema is written down here in full, and any edit to it turns into
    a failing build. If a change is genuinely wanted, this file is where the
    decision gets recorded. The table is what FrostyEQ 0.1 shipped with.
*/

#include "TestUtil.h"
#include "modules/eq/presets/FactoryPresets.h"
#include "products/eq/Product.h"
#include "modules/eq/dsp/ModelTables.h"

using namespace test;
namespace P = bmo::eq;

namespace
{
    const Expected kSchema[]
    {
        { P::kHfFreq,       "HF Freq",   0.0f,   2.0f,   1.0f, 3 },
        { P::kHfGain,       "HF Gain",      -16.0f,  16.0f,   0.0f, 0 },
        { P::kMidFreq,      "Mid Freq",  0.0f,   5.0f,   2.0f, 6 },
        { P::kMidGain,      "Mid Gain",     -18.0f,  18.0f,   0.0f, 0 },
        { P::kMidHiQ,       "Mid Hi-Q",       0.0f,   1.0f,   0.0f, 2 },
        { P::kLfFreq,       "LF Freq",   0.0f,   3.0f,   1.0f, 4 },
        { P::kLfGain,       "LF Gain",      -16.0f,  16.0f,   0.0f, 0 },
        { P::kHpfFreq,      "Low Cut",        0.0f,   4.0f,   0.0f, 5 },
        { P::kLpfFreq,      "High Cut",       0.0f,   5.0f,   0.0f, 6 },
        { P::kInputGain,    "Input",        -24.0f,  24.0f,   0.0f, 0 },
        { P::kOutputLevel,  "Output",       -24.0f,  24.0f,   0.0f, 0 },
        { P::kEqIn,         "EQ In",          0.0f,   1.0f,   1.0f, 2 },
        { P::kPhase,        "Phase",          0.0f,   1.0f,   0.0f, 2 },
        { P::kMix,          "Mix",            0.0f, 100.0f, 100.0f, 0 },
        { P::kAutoGain,     "Auto Gain",      0.0f,   1.0f,   0.0f, 2 },
        { P::kOversampling, "Oversampling",   0.0f,   3.0f,   1.0f, 4 },
    };
}

int main()
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    using bmo::products::createEq;

    //== The golden schema =====================================================
    {
        auto proc = createEq();
        checkSchema (*proc, kSchema);
        check (P::specs().size() == (size_t) P::Index::count, "the Index enum matches specs()");
    }

    //== Selector labels ======================================================
    {
        auto proc = createEq();

        setValue (*proc, P::kHpfFreq, 1.0f);
        check (param (*proc, P::kHpfFreq).getCurrentValueAsText() == "45 Hz",
               "low cut detent 1 should read '45 Hz', got '" + param (*proc, P::kHpfFreq).getCurrentValueAsText() + "'");

        setValue (*proc, P::kHfFreq, 2.0f);
        check (param (*proc, P::kHfFreq).getCurrentValueAsText() == "16 kHz",
               "HF detent 2 should read '16 kHz', got '" + param (*proc, P::kHfFreq).getCurrentValueAsText() + "'");

        setValue (*proc, P::kOutputLevel, -3.0f);
        check (param (*proc, P::kOutputLevel).getCurrentValueAsText() == "-3.0 dB",
               "Output should read in decibels, got '" + param (*proc, P::kOutputLevel).getCurrentValueAsText() + "'");

        setValue (*proc, P::kMix, 40.0f);
        check (param (*proc, P::kMix).getCurrentValueAsText() == "40 %",
               "Mix should read as a percentage, got '" + param (*proc, P::kMix).getCurrentValueAsText() + "'");
    }

    //== Panel legends must agree with what the DSP actually does =============
    // The frequencies live in two places: the display strings the selector
    // shows, and the tables EqNetwork tunes its filters from. Nothing in the
    // type system ties them together. Every figure below is from the Neve
    // 1073 & 1084 user manual, issue 5.
    {
        auto proc = createEq();

        const auto parse = [] (const juce::String& label)
        {
            if (label.equalsIgnoreCase ("off"))
                return 0.0f;

            const auto number = label.upToFirstOccurrenceOf (" ", false, true).getFloatValue();
            return label.containsIgnoreCase ("kHz") ? number * 1000.0f : number;
        };

        const auto compare = [&] (const char* id, int position, float expected, const juce::String& what)
        {
            auto& rp = param (*proc, id);
            const auto shown = parse (rp.getText (rp.convertTo0to1 ((float) position), 0));
            checkClose (shown, expected, 0.5, what + ": the panel shows " + juce::String (shown)
                                                + " Hz where the manual says " + juce::String (expected));
        };

        const float mid[6] { 360.0f, 700.0f, 1600.0f, 3200.0f, 4800.0f, 7200.0f };
        const float low[4] { 35.0f, 60.0f, 110.0f, 220.0f };
        const float high[3] { 10000.0f, 12000.0f, 16000.0f };

        for (int i = 0; i < 6; ++i)
        {
            compare (P::kMidFreq, i, mid[i], "mid detent " + juce::String (i));
            checkClose (P::midFreqHz (i), mid[i], 0.5, "the mid filter should tune to " + juce::String (mid[i]));
        }

        for (int i = 0; i < 4; ++i)
        {
            compare (P::kLfFreq, i, low[i], "low shelf detent " + juce::String (i));
            checkClose (P::lowShelfFreqHz (i), low[i], 0.5, "the low shelf should tune to " + juce::String (low[i]));
        }

        for (int i = 0; i < 3; ++i)
        {
            compare (P::kHfFreq, i, high[i], "high shelf detent " + juce::String (i));
            checkClose (P::highShelfFreqHz (i), high[i], 0.5, "the high shelf should tune to " + juce::String (high[i]));
        }

        const float lowCut[4] { 45.0f, 70.0f, 160.0f, 360.0f };
        const float highCut[5] { 6000.0f, 8000.0f, 10000.0f, 14000.0f, 18000.0f };

        for (int i = 0; i < 4; ++i)
        {
            compare (P::kHpfFreq, i + 1, lowCut[i], "low cut detent " + juce::String (i + 1));
            checkClose (P::hpfFreqHz (i + 1), lowCut[i], 0.5, "the low cut should tune to " + juce::String (lowCut[i]));
        }

        for (int i = 0; i < 5; ++i)
        {
            compare (P::kLpfFreq, i + 1, highCut[i], "high cut detent " + juce::String (i + 1));
            checkClose (P::lpfFreqHz (i + 1), highCut[i], 0.5, "the high cut should tune to " + juce::String (highCut[i]));
        }

        check (parse (param (*proc, P::kHpfFreq).getText (0.0f, 0)) == 0.0f, "low cut detent 0 should read Off");
        check (parse (param (*proc, P::kLpfFreq).getText (0.0f, 0)) == 0.0f, "high cut detent 0 should read Off");
    }

    //== Latency is reported, and only when it changes =========================
    {
        auto proc = createEq();
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
    // This is what Ableton does when it saves and reopens a set.
    {
        juce::MemoryBlock state;

        const bmo::Setting settings[] {
            { P::kHfGain,       7.5f },
            { P::kMidFreq,      4.0f },
            { P::kMidGain,    -11.25f },
            { P::kMidHiQ,       1.0f },
            { P::kLfFreq,       3.0f },
            { P::kLfGain,       4.0f },
            { P::kHpfFreq,      2.0f },
            { P::kLpfFreq,      3.0f },
            { P::kInputGain,    6.0f },
            { P::kOutputLevel, -3.0f },
            { P::kPhase,        1.0f },
            { P::kMix,         62.5f },
            { P::kAutoGain,     1.0f },
            { P::kOversampling, 2.0f },
        };

        {
            auto proc = createEq();
            for (const auto& s : settings) setValue (*proc, s.id, s.value);
            proc->getStateInformation (state);
        }

        auto restored = createEq();
        restored->setStateInformation (state.getData(), (int) state.getSize());

        for (const auto& s : settings)
            checkClose (getValue (*restored, s.id), s.value, 0.01,
                        juce::String ("state round-trip of '") + s.id + "'");

        check (param (*restored, P::kHpfFreq).getCurrentValueAsText() == "70 Hz",
               "restored low cut detent 2 should read '70 Hz'");

        auto xml = juce::AudioProcessor::getXmlFromBinary (state.getData(), (int) state.getSize());
        check (xml != nullptr && xml->hasTagName ("PARAMS") && xml->hasAttribute ("stateVersion"),
               "saved state is <PARAMS stateVersion=..>, as FrostyEQ wrote it");

        // What FrostyEQ 0.1 actually wrote: an APVTS ValueTree. It has to
        // still load, or every session with the old plugin in it is lost.
        {
            juce::XmlElement legacy ("PARAMS");
            legacy.setAttribute ("stateVersion", 1);
            auto* e = legacy.createNewChildElement ("PARAM");
            e->setAttribute ("id", P::kMidGain);
            e->setAttribute ("value", -5.5);

            auto proc = createEq();
            juce::MemoryBlock block;
            juce::AudioProcessor::copyXmlToBinary (legacy, block);
            proc->setStateInformation (block.getData(), (int) block.getSize());
            checkClose (getValue (*proc, P::kMidGain), -5.5, 0.01, "a FrostyEQ session state loads");
        }
    }

    //== Audio path is sane ====================================================
    {
        auto proc = createEq();
        proc->setPlayConfigDetails (2, 2, 48000.0, 512);
        proc->prepareToPlay (48000.0, 512);

        juce::AudioBuffer<float> buffer (2, 512);
        juce::MidiBuffer midi;

        for (int ch = 0; ch < 2; ++ch)
            for (int n = 0; n < 512; ++n)
                buffer.getWritePointer (ch)[n] = 0.5f * std::sin (juce::MathConstants<float>::twoPi * 440.0f * (float) n / 48000.0f);

        proc->processBlock (buffer, midi);

        bool finite = true;
        for (int ch = 0; ch < 2; ++ch)
            for (int n = 0; n < 512; ++n)
                finite = finite && std::isfinite (buffer.getReadPointer (ch)[n]);

        check (finite, "the output is finite");
        check (buffer.getMagnitude (0, 0, 512) > 0.01f, "audio passes through at defaults");
    }

    //== Presets ==============================================================
    {
        const auto sandbox = juce::File::getSpecialLocation (juce::File::tempDirectory)
                                 .getChildFile ("bmo-eq-preset-tests");
        sandbox.deleteRecursively();
        bmo::PresetManager::setDirectoryForTesting (sandbox);

        auto proc = createEq();
        auto& presets = proc->getPresets();

        check (presets.getFactory().size() == P::factory().size(), "every factory preset is listed");
        check (presets.getCurrentName() == "Init", "the plugin starts on Init");
        check (presets.extension() == ".bmoceq", "presets are .bmoceq files");
        check (presets.directory().getFileName() == "BMO CEQ", "presets live under BMO CEQ");

        for (int i = 0; i < (int) P::factory().size(); ++i)
        {
            const auto& preset = P::factory()[(size_t) i];
            presets.loadFactory (i);

            check (presets.getCurrentName() == preset.name, "loading names the preset");
            check (! presets.isEdited(), "a freshly loaded preset is not edited");

            for (const auto& s : preset.settings)
                checkClose (getValue (*proc, s.id), s.value, 0.01,
                            juce::String ("preset \"") + preset.name + "\" sets " + s.id);
        }

        setValue (*proc, P::kPhase, 1.0f);
        setValue (*proc, P::kLpfFreq, 4.0f);
        presets.loadFactory (0);

        for (const auto& s : P::specs())
        {
            auto& rp = param (*proc, s.id);
            checkClose (rp.getValue(), rp.getDefaultValue(), 1.0e-5, juce::String ("Init defaults ") + s.id);
        }

        presets.loadFactory (1);
        setValue (*proc, P::kMidGain, 7.0f);
        check (presets.isEdited(), "moving a control marks the preset edited");
        presets.loadFactory (1);
        check (! presets.isEdited(), "reloading clears the edited mark");

        presets.loadFactory (0);
        setValue (*proc, P::kMidGain, -6.5f);
        setValue (*proc, P::kHpfFreq, 3.0f);
        check (presets.saveUser ("Round Trip"), "a user preset saves");
        check (presets.getUserNames().contains ("Round Trip"), "and is listed");
        check (! presets.isEdited(), "saving clears the edited mark");

        presets.loadFactory (0);
        presets.loadUser ("Round Trip");
        checkClose (getValue (*proc, P::kMidGain), -6.5, 0.01, "user preset restores mid gain");
        checkClose (getValue (*proc, P::kHpfFreq),  3.0, 0.01, "user preset restores the filter");

        const auto away = sandbox.getChildFile ("elsewhere/Shared.bmoceq");
        check (presets.exportTo (away), "exporting writes a file");
        presets.loadFactory (0);
        check (presets.importFrom (away), "importing succeeds");
        check (presets.getUserNames().contains ("Shared"), "an imported preset joins the list");
        checkClose (getValue (*proc, P::kMidGain), -6.5, 0.01, "import restores the settings");

        presets.loadFactory (0);
        presets.step (-1);
        check (presets.getCurrentName() != "Init", "stepping back from the first wraps");
        presets.loadFactory (0);
        presets.step (1);
        check (presets.getCurrentName() == presets.getFactory()[1].name, "stepping forward reaches the next");

        check (presets.deleteUser ("Round Trip"), "a user preset deletes");
        check (! presets.getUserNames().contains ("Round Trip"), "and leaves the list");

        sandbox.deleteRecursively();
        bmo::PresetManager::setDirectoryForTesting ({});
    }

    //== The rename chain: FrostyEQ -> BMO EQ -> BMO CEQ ======================
    // This product has been renamed twice, so a user's presets can be sitting
    // in either old folder, or in both. Until 2026-09-17 none of this was
    // testable: migration returned early whenever the sandbox was in use, so
    // the one thing that has to work on a stranger's machine was the one thing
    // the suite never ran. It resolves the old folders through the sandbox now.
    {
        const auto sandbox = juce::File::getSpecialLocation (juce::File::tempDirectory)
                                 .getChildFile ("bmo-ceq-migration-tests");
        const auto current = sandbox.getChildFile ("BMO CEQ");
        const auto middle  = sandbox.getChildFile ("BMO EQ");
        const auto oldest  = sandbox.getChildFile ("FrostyEQ");

        sandbox.deleteRecursively();
        bmo::PresetManager::setDirectoryForTesting (sandbox);

        // Genuine preset files rather than hand-written XML: a preset is
        // whatever the plugin exports, so this cannot drift from the format.
        const auto writePreset = [&] (const juce::File& file, float midGain)
        {
            auto proc = createEq();
            setValue (*proc, P::kMidGain, midGain);
            check (proc->getPresets().exportTo (file), "seeding " + file.getFileName());
        };

        writePreset (middle.getChildFile ("Shared.bmoeq"),     -6.5f);
        writePreset (oldest.getChildFile ("Shared.frostyeq"),   9.0f);
        writePreset (oldest.getChildFile ("Ancient.frostyeq"),  3.0f);
        writePreset (middle.getChildFile ("Recent.bmoeq"),     -2.0f);

        // Seeding ran migration itself, each time; clear what it left so the
        // first run under test is a genuine first run.
        current.deleteRecursively();

        {
            auto proc = createEq();
            auto& presets = proc->getPresets();

            check (presets.getUserNames().contains ("Ancient"), "a FrostyEQ preset survives two renames");
            check (presets.getUserNames().contains ("Recent"),  "a BMO EQ preset comes across");
            check (current.getChildFile ("Ancient.bmoceq").existsAsFile(),
                   "and it arrives under the new extension");

            // The same name in both old folders. Newest wins: the FrostyEQ file
            // is the same preset from before the user's later edits.
            presets.loadUser ("Shared");
            checkClose (getValue (*proc, P::kMidGain), -6.5, 0.01,
                        "a name in both old folders arrives from BMO EQ, not FrostyEQ");

            check (current.getChildFile (bmo::kMigrationMarker).existsAsFile(),
                   "the copy leaves a marker");
        }

        // A preset the user deletes stays deleted. The old code gated on the
        // folder being empty, which answered this by accident and stranded
        // anyone who had saved one preset first; the marker answers it on
        // purpose.
        {
            auto proc = createEq();
            check (proc->getPresets().deleteUser ("Ancient"), "the user deletes a migrated preset");
        }

        {
            auto proc = createEq();
            check (! proc->getPresets().getUserNames().contains ("Ancient"),
                   "and it does not come back on the next launch");
        }

        // The user's own work under the new name is never overwritten, even
        // when an old folder holds the same name.
        {
            auto proc = createEq();
            setValue (*proc, P::kMidGain, 12.0f);
            check (proc->getPresets().saveUser ("Recent"), "the user saves over a migrated name");
        }

        current.getChildFile (bmo::kMigrationMarker).deleteFile();

        {
            auto proc = createEq();
            proc->getPresets().loadUser ("Recent");
            checkClose (getValue (*proc, P::kMidGain), 12.0, 0.01,
                        "a re-run copy does not overwrite the user's own preset");
        }

        sandbox.deleteRecursively();
        bmo::PresetManager::setDirectoryForTesting ({});
    }

    //== A preset must not change how loud the track is ======================
    // Every factory preset drives the input and pulls the output back. If the
    // two do not cancel, the preset is judged on being louder rather than on
    // its tone.
    {
        auto proc = createEq();
        proc->setPlayConfigDetails (2, 2, 48000.0, 512);
        proc->prepareToPlay (48000.0, 512);

        const auto source = pink (512 * 200);
        const auto sourceDb = rmsDb (source);
        const auto& factory = proc->getPresets().getFactory();

        for (int index = 0; index < (int) factory.size(); ++index)
        {
            proc->getPresets().loadFactory (index);
            proc->reset();

            const auto outDb = outputDb (*proc, source, 512, 20);

            checkClose (outDb - sourceDb, 0.0, 0.5,
                        juce::String ("preset '") + factory[(size_t) index].name + "' comes out at the level it went in");
        }
    }

    return finish ("BMO CEQ");
}
