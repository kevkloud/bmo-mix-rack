/*
    BMO Defang as a host sees it: schema, value strings, latency, state, presets.
    See EqTests.cpp for why the schema table is written out in full.

    **The DSP under this is still the placeholder** in
    modules/deesser/dsp/DspCore.h, so what is asserted here is everything that
    does not depend on there being a de-esser yet -- which is the whole of the
    module's contract with a host. What is missing is named where it would
    otherwise sit, rather than left to be noticed: anything about gain
    reduction.
*/

#include "TestUtil.h"
#include "modules/deesser/presets/FactoryPresets.h"
#include "products/deesser/Product.h"

using namespace test;
namespace P = bmo::deesser;

namespace
{
    // Permanent and append-only from the first release. The table is
    // docs/deesser/11-integration-and-test-plan.md section 3, which is the
    // single authoritative copy of the schema -- 10-dsp-spec.md section 9
    // points here rather than restating it, because restating it is how the
    // two documents drifted apart the first time.
    //
    // If any of these five ever needs to change, this is the first thing that
    // fails, and 11 section 3 is where the argument has to be made.
    const Expected kSchema[]
    {
        { P::kFreq,   "Freq",       2000.0f, 10000.0f, 6500.0f, 0 },
        { P::kQ,      "Q",             0.7f,     6.0f,    2.5f, 0 },
        { P::kThresh, "Threshold",   -24.0f,    24.0f,    0.0f, 0 },
        { P::kRange,  "Range",         1.0f,    18.0f,    8.0f, 0 },
        { P::kShape,  "Shape",         0.0f,     1.0f,    0.0f, 2 },
    };
}

int main()
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    using bmo::products::createDeesser;

    //== The golden schema =====================================================
    {
        auto proc = createDeesser();
        checkSchema (*proc, kSchema);
        check (P::specs().size() == (size_t) P::Index::count, "the Index enum matches specs()");
        check (P::specs().size() == 5, "v1 ships exactly five parameters");
    }

    //== The parameters that were considered and left out =====================
    //
    // Every one of these is a decision in 11 section 3 rather than an
    // oversight, and "we decided not to" and "somebody forgot" look identical
    // in a parameter list -- so they are asserted.
    //
    // ADAPT is the interesting one: 10 section 3's reference blend went into
    // the table as a sixth parameter and came back out, because the owner
    // wants it proven before it earns a control. It ships as one fixed
    // internal constant and can only ever be **appended after `shape`**.
    // Attack, release, mix and a stereo-link switch sit in the same queue.
    {
        auto proc = createDeesser();

        for (const auto* absent : { "adapt", "kappa", "mix", "attack", "release",
                                    "lookahead", "oversampling", "link", "listen",
                                    "sidechain", "width", "mode", "sensitivity" })
            check (bmo::indexOfParam (P::specs(), absent) < 0,
                   juce::String ("there is no '") + absent + "' parameter");

        // Shape is last, so an appended control cannot land between two
        // parameters a saved session already refers to by position.
        check (P::Index::shape == P::Index::count - 1,
               "shape is the last parameter, so anything new appends after it");
    }

    //== THRESHOLD says what it means =========================================
    //
    // This is the assertion that fails if the value string is ever "tidied" to
    // a plain dB figure. The number is **prominence** over the detector's
    // reference, not dBFS: an input-gain change adds the same constant to
    // every term of the detector and cancels exactly (10 section 3), which is
    // why the threshold never needs re-riding across a take. Printed as a bare
    // "+3.0 dB" it reads as a level and invites exactly the threshold-riding
    // this detection style exists to abolish.
    {
        auto proc = createDeesser();

        const auto text = [&proc] (const char* id, float v)
        {
            setValue (*proc, id, v);
            return param (*proc, id).getCurrentValueAsText();
        };

        check (text (P::kThresh, 3.0f) == "+3.0 dB over",
               "threshold at +3 should read '+3.0 dB over', got '" + text (P::kThresh, 3.0f) + "'");
        check (text (P::kThresh, 0.0f) == "0.0 dB over",
               "threshold at 0 should read '0.0 dB over', got '" + text (P::kThresh, 0.0f) + "'");
        check (text (P::kThresh, -6.5f) == "-6.5 dB over",
               "threshold at -6.5 should read '-6.5 dB over', got '" + text (P::kThresh, -6.5f) + "'");
        check (text (P::kThresh, 24.0f) == "+24.0 dB over",
               "threshold at the top should read '+24.0 dB over', got '" + text (P::kThresh, 24.0f) + "'");
    }

    //== The choice list, in order ============================================
    // Names and index order freeze with the ids, and they are BMO DEQ's own
    // labels. `tools/snapshot` sets a choice by name, so these strings are
    // also a command-line interface.
    {
        auto proc = createDeesser();

        const auto choice = [&proc] (const char* id, float v)
        {
            setValue (*proc, id, v);
            return param (*proc, id).getCurrentValueAsText();
        };

        check (choice (P::kShape, 0.0f) == "Bell", "shape 0 is Bell");
        check (choice (P::kShape, 1.0f) == "High Shelf", "shape 1 is High Shelf");
    }

    //== Displayed values, at both ends of every knob =========================
    //
    // **Every string here is ASCII**, and that is a build constraint rather
    // than a preference: the two display faces are licensed individually and
    // live outside this repository (see .bmo-fontdir), so a glyph outside
    // ASCII is one this suite cannot promise it can draw.
    {
        auto proc = createDeesser();

        const auto text = [&proc] (const char* id, float v)
        {
            setValue (*proc, id, v);
            return param (*proc, id).getCurrentValueAsText();
        };

        const auto isAscii = [] (const juce::String& s)
        {
            for (auto c : s)
                if (c < 32 || c > 126)
                    return false;

            return true;
        };

        // Freq never reaches the Hz form: the range starts at 2 kHz, so it is
        // "2.00 kHz" to "10.0 kHz" across the whole travel.
        check (text (P::kFreq, 2000.0f) == "2.00 kHz",
               "Freq at the bottom should read '2.00 kHz', got '" + text (P::kFreq, 2000.0f) + "'");
        check (text (P::kFreq, 6500.0f) == "6.50 kHz",
               "Freq at the default should read '6.50 kHz', got '" + text (P::kFreq, 6500.0f) + "'");
        check (text (P::kFreq, 10000.0f) == "10.0 kHz",
               "Freq at the top should read '10.0 kHz', got '" + text (P::kFreq, 10000.0f) + "'");

        // Q is Plain: a bare number and no unit, because a Q has none --
        // bandwidth in octaves would be a reading of it, not a second control.
        // Plain is also the one format that takes no `withStringFromValue`
        // (core/state/Parameters.h), so what a host prints is JUCE's own
        // two-decimal default rather than ParamSpec::text's "%g".
        check (text (P::kQ, 2.5f) == "2.50",
               "Q should read '2.50', got '" + text (P::kQ, 2.5f) + "'");
        check (text (P::kQ, 0.7f) == "0.70",
               "Q at the bottom should read '0.70', got '" + text (P::kQ, 0.7f) + "'");

        check (text (P::kRange, 1.0f) == "+1.0 dB",
               "Range at the floor should read '+1.0 dB', got '" + text (P::kRange, 1.0f) + "'");
        check (text (P::kRange, 8.0f) == "+8.0 dB",
               "Range at the default should read '+8.0 dB', got '" + text (P::kRange, 8.0f) + "'");
        check (text (P::kRange, 18.0f) == "+18.0 dB",
               "Range at the ceiling should read '+18.0 dB', got '" + text (P::kRange, 18.0f) + "'");

        for (const auto* id : { P::kFreq, P::kQ, P::kThresh, P::kRange, P::kShape })
            for (const auto v : { 0.0f, 0.5f, 1.0f })
                check (isAscii (param (*proc, id).getText (v, 0)),
                       juce::String ("'") + id + "' prints ASCII only at normalised "
                           + juce::String (v));
    }

    //== A shelf's Q is capped behind an unchanged knob =======================
    //
    // Q is one parameter whatever the shape -- 0.7 to 6 on the knob in both,
    // permanently -- and the shelf's own limit is applied behind it, which is
    // BMO DEQ's idiom. The panel's band sketch is what found this: at the
    // default Q of 2.5 the shelf drew a resonant dip below its corner and a
    // climb back above it, which is not a shelf. Asserted here so the cap
    // cannot quietly become a narrower *parameter* range instead.
    {
        check (P::specs()[P::Index::q].max == 6.0f, "the Q knob still reaches 6 in both shapes");
        checkClose (P::effectiveQ (P::bell, 6.0f), 6.0, 1.0e-6, "a bell keeps the Q it is given");
        checkClose (P::effectiveQ (P::highShelf, 6.0f), (double) P::kShelfMaxQ, 1.0e-6,
                    "a shelf is capped at kShelfMaxQ");
        checkClose (P::effectiveQ (P::highShelf, 1.0f), 1.0, 1.0e-6,
                    "a shelf under the cap is left alone");
    }

    //== Latency: zero, at every setting ======================================
    //
    // Not "zero at the default" -- zero everywhere, because there is no
    // lookahead and no oversampling and there is no parameter that could add
    // either. Latency is permanent once shipped, so this is the costly one to
    // get wrong; 10 section 1 and 11 section 5 both pin it.
    {
        auto proc = createDeesser();
        proc->setPlayConfigDetails (2, 2, 48000.0, 512);

        const auto latencyAt = [&proc] (float f, float shapeValue, float rangeValue)
        {
            setValue (*proc, P::kFreq, f);
            setValue (*proc, P::kShape, shapeValue);
            setValue (*proc, P::kRange, rangeValue);
            proc->prepareToPlay (48000.0, 512);
            return proc->getLatencySamples();
        };

        for (const auto shapeValue : { 0.0f, 1.0f })
            for (const auto f : { 2000.0f, 6500.0f, 10000.0f })
                for (const auto r : { 1.0f, 18.0f })
                    check (latencyAt (f, shapeValue, r) == 0,
                           "zero latency at every setting");

        auto fresh = createDeesser();
        fresh->setPlayConfigDetails (2, 2, 44100.0, 64);
        fresh->prepareToPlay (44100.0, 64);
        check (fresh->getLatencySamples() == 0, "a fresh instance reports zero latency");
    }

    //== State round-trip, carrying no listen state ===========================
    {
        juce::MemoryBlock state;

        const bmo::Setting settings[] {
            { P::kFreq, 7250.0f },
            { P::kQ, 3.4f },
            { P::kThresh, -5.5f },
            { P::kRange, 12.5f },
            { P::kShape, 1.0f },
        };

        {
            auto proc = createDeesser();
            for (const auto& s : settings) setValue (*proc, s.id, s.value);
            proc->getStateInformation (state);
        }

        auto restored = createDeesser();
        restored->setStateInformation (state.getData(), (int) state.getSize());

        for (const auto& s : settings)
            checkClose (getValue (*restored, s.id), s.value, 0.01,
                        juce::String ("state round-trip of '") + s.id + "'");

        auto xml = juce::AudioProcessor::getXmlFromBinary (state.getData(), (int) state.getSize());
        check (xml != nullptr && xml->hasTagName ("PARAMS") && xml->hasAttribute ("stateVersion"),
               "saved state is <PARAMS stateVersion=..>");

        // **Nothing about listen is written.** It is momentary panel state on
        // the setSolo hook, and a saved one could be recalled into a session
        // or printed into a bounce -- which is the reason it is not a
        // parameter. Checked against the XML rather than against the parameter
        // list, so a listen attribute smuggled in beside the parameters would
        // fail too.
        if (xml != nullptr)
        {
            check (! xml->hasAttribute ("listen"), "saved state carries no listen attribute");
            check (xml->getChildByAttribute ("id", "listen") == nullptr,
                   "saved state carries no listen element");
            check (xml->getChildByAttribute ("id", "solo") == nullptr,
                   "saved state carries no solo element");
        }
    }

    //== The rack slot fits ====================================================
    // Five parameters against a slot's 32 host lanes, so nothing here is held
    // off the grid -- and there is room for every control 11 section 3 lists
    // as appendable to arrive later and still be automatable in a rack.
    check (P::specs().size() <= 32, "every parameter gets a rack host lane");

    //== Presets ==============================================================
    {
        const auto sandbox = juce::File::getSpecialLocation (juce::File::tempDirectory)
                                 .getChildFile ("bmo-deesser-preset-tests");
        sandbox.deleteRecursively();
        bmo::PresetManager::setDirectoryForTesting (sandbox);

        auto proc = createDeesser();
        auto& presets = proc->getPresets();

        check (presets.getFactory().size() == P::factory().size(), "every factory preset is listed");
        check (presets.getCurrentName() == "Init", "the plugin starts on Init");
        check (presets.extension() == ".bmodeesser", "presets are .bmodeesser files");

        // Init is index 0 and is all defaults -- modules/AGENTS.md step 6.
        check (P::factory()[0].settings.empty(), "Init sets nothing, so it is the defaults");

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
        setValue (*proc, P::kFreq, 7000.0f);
        check (presets.isEdited(), "moving a control marks the preset edited");
        check (presets.saveUser ("Round Trip"), "a user preset saves");
        presets.loadFactory (0);
        presets.loadUser ("Round Trip");
        checkClose (getValue (*proc, P::kFreq), 7000.0, 1.0, "user preset restores freq");
        check (presets.deleteUser ("Round Trip"), "a user preset deletes");

        sandbox.deleteRecursively();
        bmo::PresetManager::setDirectoryForTesting ({});
    }

    //== Not asserted yet, and here is where it goes ==========================
    //
    // **Gain reduction.** currentGainReductionDb() is a flat zero in the
    // placeholder, so there is nothing to assert about it here. The real
    // figure is the **peak band reduction** -- the applied, glided offset, not
    // a wideband-equivalent one -- and it is asserted against the offset in
    // tests/dsp/DeesserDspTests.cpp once there is a detector to produce one.
    //
    // **No preset level check, and there never will be one.** Every other
    // module's suite checks that a preset comes out at the level it went in.
    // This module has no output trim and cannot have one: a band cut takes
    // under a dB of broadband energy, which is the whole reason the meter
    // reports band reduction rather than a wideband figure (10 section 8). The
    // absence is by construction, not deferred.

    return finish ("BMO Defang");
}
