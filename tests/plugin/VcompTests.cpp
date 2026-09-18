/*
    BMO Vcomp as a host sees it: schema, presets, level. See EqTests.cpp for
    why the schema table is written out in full.
*/

#include "TestUtil.h"
#include "modules/vcomp/presets/FactoryPresets.h"
#include "products/vcomp/Product.h"

using namespace test;
namespace P = bmo::vcomp;

namespace
{
    // Permanent and append-only. The six detector parameters' defaults are
    // the standard-mode figures in params.h, and they have to stay that way:
    // it is what makes turning COMPLEX on with untouched knobs silent. If one
    // of these defaults moves, the matching kStandard* constant moves with it
    // or VcompDspTests::testComplexIsSilentAtTheDefaults fails.
    const Expected kSchema[]
    {
        { P::kAmount,    "Amount",      0.0f, 100.0f,     0.0f, 0 },
        { P::kGate,      "Gate",      -60.0f, -10.0f,   -60.0f, 0 },
        { P::kOutput,    "Output",    -24.0f,  24.0f,     0.0f, 0 },
        { P::kComplex,   "Complex",     0.0f,   1.0f,     0.0f, 2 },
        { P::kAttack,    "Attack",      0.1f, 100.0f,     5.0f, 0 },
        { P::kRelease,   "Release",    20.0f,1000.0f,   200.0f, 0 },
        { P::kArc,       "Arc",         0.0f,   1.0f,     1.0f, 2 },
        { P::kSidechain, "Sidechain",  20.0f, 500.0f,    90.0f, 0 },
        { P::kLowThru,   "Low Thru",   20.0f, 500.0f,    20.0f, 0 },
        { P::kHighThru,  "High Thru",2000.0f,20000.0f,20000.0f, 0 },
    };
}

int main()
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    using bmo::products::createVcomp;

    //== The golden schema =====================================================
    {
        auto proc = createVcomp();
        checkSchema (*proc, kSchema);
        check (P::specs().size() == (size_t) P::Index::count, "the Index enum matches specs()");
    }

    //== The standard-mode figures really are the defaults =====================
    // Stated here as well as in the schema table because it is a claim about
    // two files agreeing, and the schema table alone would let someone change
    // params.h's kStandard* constants without anything failing.
    {
        auto proc = createVcomp();

        checkClose (getValue (*proc, P::kAttack), P::kStandardAttackMs, 0.001,
                    "ATTACK's default is the standard-mode attack");
        checkClose (getValue (*proc, P::kRelease), P::kStandardReleaseMs, 0.001,
                    "RELEASE's default is the standard-mode release");
        checkClose (getValue (*proc, P::kSidechain), P::kStandardSidechainHz, 0.001,
                    "SIDECHAIN's default is the standard-mode sidechain");
        check ((getValue (*proc, P::kArc) > 0.5f) == P::kStandardArc,
               "ARC's default is the standard-mode ARC");
        checkClose (getValue (*proc, P::kLowThru), P::kStandardLowThruHz, 0.001,
                    "LOW THRU's default is the standard-mode figure");
        checkClose (getValue (*proc, P::kHighThru), P::kStandardHighThruHz, 0.001,
                    "HIGH THRU's default is the standard-mode figure");
    }

    //== The rails that mean "off" are where the ranges end ====================
    // Three parameters are inert at one end of their travel instead of having
    // a switch, and each one's rail has to *be* the end of its range or the
    // control has dead travel past "off". The gate's rail is also the IN
    // meter's floor, because the handle at the far left of that meter is the
    // gate switched off -- move one and the other has to move with it.
    {
        auto proc = createVcomp();

        const auto range = [&proc] (const char* id)
        {
            return param (*proc, id).getNormalisableRange();
        };

        checkClose (range (P::kGate).start, P::kGateOffDb, 0.001,
                    "the gate's off rail is the bottom of its range");
        checkClose (range (P::kLowThru).start, P::kLowThruOffHz, 0.001,
                    "LOW THRU's off rail is the bottom of its range");
        checkClose (range (P::kHighThru).end, P::kHighThruOffHz, 0.001,
                    "HIGH THRU's off rail is the top of its range");
    }

    //== Displayed values ======================================================
    {
        auto proc = createVcomp();

        setValue (*proc, P::kAmount, 50.0f);
        check (param (*proc, P::kAmount).getCurrentValueAsText() == "50 %",
               "Amount should read '50 %', got '" + param (*proc, P::kAmount).getCurrentValueAsText() + "'");

        setValue (*proc, P::kOutput, -3.0f);
        check (param (*proc, P::kOutput).getCurrentValueAsText() == "-3.0 dB",
               "Output should read '-3.0 dB', got '" + param (*proc, P::kOutput).getCurrentValueAsText() + "'");
    }

    //== Latency: always zero, whatever Amount is set to ========================
    {
        auto proc = createVcomp();
        proc->setPlayConfigDetails (2, 2, 48000.0, 512);

        setValue (*proc, P::kAmount, 0.0f);
        proc->prepareToPlay (48000.0, 512);
        check (proc->getLatencySamples() == 0, "Amount 0 reports zero latency");

        setValue (*proc, P::kAmount, 100.0f);
        setValue (*proc, P::kComplex, 1.0f);
        setValue (*proc, P::kAttack, 0.1f);
        proc->prepareToPlay (48000.0, 512);
        check (proc->getLatencySamples() == 0, "the fastest complex setting also reports zero latency");
    }

    //== State round-trip ======================================================
    {
        juce::MemoryBlock state;

        const bmo::Setting settings[] {
            { P::kAmount, 72.5f },
            { P::kOutput, -4.5f },
            { P::kComplex, 1.0f },
            { P::kAttack, 12.0f },
            { P::kRelease, 350.0f },
            { P::kSidechain, 140.0f },
        };

        {
            auto proc = createVcomp();
            for (const auto& s : settings) setValue (*proc, s.id, s.value);
            proc->getStateInformation (state);
        }

        auto restored = createVcomp();
        restored->setStateInformation (state.getData(), (int) state.getSize());

        for (const auto& s : settings)
            checkClose (getValue (*restored, s.id), s.value, 0.01,
                        juce::String ("state round-trip of '") + s.id + "'");

        auto xml = juce::AudioProcessor::getXmlFromBinary (state.getData(), (int) state.getSize());
        check (xml != nullptr && xml->hasTagName ("PARAMS") && xml->hasAttribute ("stateVersion"),
               "saved state is <PARAMS stateVersion=..>");
    }

    //== Presets ==============================================================
    {
        const auto sandbox = juce::File::getSpecialLocation (juce::File::tempDirectory)
                                 .getChildFile ("bmo-vcomp-preset-tests");
        sandbox.deleteRecursively();
        bmo::PresetManager::setDirectoryForTesting (sandbox);

        auto proc = createVcomp();
        auto& presets = proc->getPresets();

        check (presets.getFactory().size() == P::factory().size(), "every factory preset is listed");
        check (presets.getCurrentName() == "Init", "the plugin starts on Init");
        // Renamed with the product, 2026-09-14: BMO Vcomp became LTV Comp, the
        // first product on the LTV line. The legacy pair that still reads the
        // old `.bmovcomp` folder is set in products/vcomp/Product.h and is not
        // reachable from here -- PresetManager exposes extension() and none of
        // the rest of its PresetInfo -- so this pins the new extension only.
        check (presets.extension() == ".ltvcomp", "presets are .ltvcomp files");

        for (int i = 0; i < (int) P::factory().size(); ++i)
        {
            const auto& preset = P::factory()[(size_t) i];
            presets.loadFactory (i);
            check (presets.getCurrentName() == preset.name, "loading names the preset");

            for (const auto& s : preset.settings)
                checkClose (getValue (*proc, s.id), s.value, 0.01,
                            juce::String ("preset \"") + preset.name + "\" sets " + s.id);
        }

        // A preset that sets a detector control must set COMPLEX too, or it
        // stores a number the DSP will not read and shows a panel with no knob
        // on it -- see the note in modules/vcomp/presets/FactoryPresets.h.
        for (const auto& preset : P::factory())
        {
            auto touchesDetector = false, setsComplex = false;

            for (const auto& s : preset.settings)
            {
                touchesDetector = touchesDetector
                                      || juce::String (s.id) == P::kAttack
                                      || juce::String (s.id) == P::kRelease
                                      || juce::String (s.id) == P::kArc
                                      || juce::String (s.id) == P::kSidechain
                                      || juce::String (s.id) == P::kLowThru
                                      || juce::String (s.id) == P::kHighThru;

                setsComplex = setsComplex || (juce::String (s.id) == P::kComplex && s.value > 0.5f);
            }

            check (! touchesDetector || setsComplex,
                   juce::String ("preset \"") + preset.name + "\" sets COMPLEX if it sets a detector control");
        }

        presets.loadFactory (0);
        setValue (*proc, P::kAmount, 88.0f);
        check (presets.isEdited(), "moving a control marks the preset edited");
        check (presets.saveUser ("Round Trip"), "a user preset saves");
        presets.loadFactory (0);
        presets.loadUser ("Round Trip");
        checkClose (getValue (*proc, P::kAmount), 88.0, 0.01, "user preset restores amount");
        check (presets.deleteUser ("Round Trip"), "a user preset deletes");

        sandbox.deleteRecursively();
        bmo::PresetManager::setDirectoryForTesting ({});
    }

    //== Every preset comes out near the level it went in ======================
    // Unlike every other module's, this is not a check on hand-picked makeup
    // figures -- no preset here sets OUTPUT at all. It is a check on the
    // automatic makeup itself (autoMakeupDb, Detector.h), which is what makes
    // AMOUNT buy density rather than level. If this drifts, the auto makeup
    // has drifted, not a preset.
    {
        auto proc = createVcomp();
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

            checkClose (outDb - sourceDb, 0.0, 3.0,
                        juce::String ("preset '") + factory[(size_t) index].name + "' comes out near the level it went in");
        }
    }

    return finish ("LTV Comp");
}
