// The shared controls in core/ui, asserted on their own rather than through a
// panel that happens to use one.
//
// `ui/LayoutTests.cpp` is the other UI suite and it is a different claim: it
// builds the real editors and asks where everything landed. Nothing there can
// say what a control *is* -- only where a panel put it -- and `ui::Fader` has
// no call site yet at all. BMO Linger's panel is the pass after this one, and
// a control that arrives untested and then gets laid out is a control whose
// first test measures the panel and the class at once.
//
// The house rule, from tests/dsp/OptoDspTests.cpp: **assert absolutes, not
// comparisons.** Every figure below is a stated pixel -- 6.0, 49.0, 92.0 --
// worked out from the class's own constants and written out here, not "the
// same as it was" and not "further up than before". A cap that moved the wrong
// way and a cap that did not move at all both pass a comparison against a
// previous reading.

#include "core/ui/Controls.h"
#include "core/ui/Line.h"
#include "core/ui/LookAndFeel.h"
#include "core/ui/ModulePanel.h"

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <iostream>

namespace
{

int failures = 0;

void check (bool condition, const juce::String& what)
{
    if (! condition)
    {
        std::cerr << "FAIL: " << what << '\n';
        ++failures;
    }
}

void checkNear (double actual, double expected, double tolerance, const juce::String& what)
{
    if (! (std::abs (actual - expected) <= tolerance))
    {
        std::cerr << "FAIL: " << what << " -- expected " << expected
                  << ", got " << actual << '\n';
        ++failures;
    }
}

void checkText (const juce::String& actual, const juce::String& expected, const juce::String& what)
{
    if (actual != expected)
    {
        std::cerr << "FAIL: " << what << " -- expected '" << expected
                  << "', got '" << actual << "'\n";
        ++failures;
    }
}

/** A level in dB, which is what all three of BMO Linger's faders are.

    A plain `juce::AudioParameterFloat` rather than a module's own parameter:
    the claims below are the control's, and a suite that reached into a module
    for a parameter would fail when that module's range moved.

    Handed back as `RangedAudioParameter`, which is what a `Fader` takes and
    what makes `getDefaultValue` reachable -- `AudioParameterFloat` overrides
    it privately. */
juce::RangedAudioParameter& levelParameter()
{
    static juce::AudioParameterFloat p { juce::ParameterID { "level", 1 }, "Level",
                                         juce::NormalisableRange<float> (-40.0f, 6.0f),
                                         -6.0f,
                                         juce::AudioParameterFloatAttributes()
                                             .withStringFromValueFunction ([] (float v, int)
                                             {
                                                 return juce::String (v, 1) + " dB";
                                             }) };
    return p;
}

/** The cell the mockup's strip row gives a fader, with the value line counted:
    22 px of caption at 15 pt, 14 px of value, and 98 px of body. */
constexpr int kCellWidth  = 100;
constexpr int kCellHeight = 134;

/** What the body and the travel come to in that cell. Stated here and asserted
    below rather than computed from the class, so a change to `kCapHeight` or to
    the caption row fails here instead of quietly moving the fader. */
constexpr float kBody   = 98.0f;
constexpr float kTravel = 86.0f;

} // namespace

//==============================================================================
int main()
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    auto& param = levelParameter();

    //== The cap matches a knob's cap, not a knob ==============================
    //
    // `PlainKnob` is laid out square and draws its face at `faceScale` of that
    // square, so the 46 px cluster knob at 0.6 has a **27.6 px cap**. A fader
    // beside it that matched the footprint would be 46 across and would read
    // as a different family of control. This has been got wrong before, which
    // is why it is a test and not a comment.
    {
        checkNear ((double) bmo::ui::Fader::kCapWidth, 27.6, 1.0e-4,
                   "the fader cap is the knob cap's 27.6 px, not the knob's 46");
        checkNear ((double) bmo::ui::Fader::kCapWidth, 46.0 * 0.6, 1.0e-4,
                   "27.6 is 46 at faceScale 0.6 -- the cluster knob's own cap");
    }

    //== It reports its parameter's value ======================================
    //
    // The host's own text, so the panel, a rack slot and an automation lane
    // cannot print three different numbers for one position. Absolutes: the
    // exact strings, at the two ends and at the default.
    {
        bmo::ui::Fader fader { param, "REVERB" };
        fader.setBounds (0, 0, kCellWidth, kCellHeight);

        check (fader.isShowingValue(),
               "a fader prints its number by default -- its ticks are unlabelled");

        param.setValueNotifyingHost (0.0f);
        checkText (fader.valueText(), "-40.0 dB", "the fader reads the bottom of its range");

        param.setValueNotifyingHost (1.0f);
        checkText (fader.valueText(), "6.0 dB", "the fader reads the top of its range");

        param.setValueNotifyingHost (param.getDefaultValue());
        checkText (fader.valueText(), "-6.0 dB", "the fader reads its default");

        // And through a format, which is paint only: the host's text is
        // untouched and what is drawn is the rewrite.
        fader.setValueFormat ([] (const juce::String& t) { return t.upToFirstOccurrenceOf (" ", false, false); });
        checkText (fader.valueText(), "-6.0", "a value format rewrites what is drawn");
        checkText (param.getCurrentValueAsText(), "-6.0 dB", "a value format must not touch the host's text");
        fader.setValueFormat ({});
    }

    //== The caption fits its box ==============================================
    //
    // BMO Opto's MAKEUP drew as MAKEU for a whole release because nothing
    // measured it. `captionOverflow` measures the caption *and* the widest
    // value string, in the boxes and faces the paint uses.
    {
        for (const auto* name : { "ER", "REVERB", "MIX" })
        {
            bmo::ui::Fader fader { param, name };
            fader.setBounds (0, 0, kCellWidth, kCellHeight);

            check (fader.captionOverflow() <= 0.0f,
                   juce::String ("'") + name + "' overflows its box by "
                     + juce::String (fader.captionOverflow(), 1) + " px");
        }

        // A caption that cannot fit has to be *reported* as not fitting --
        // otherwise the check above is a check on nothing. A 30 px cell is
        // narrower than "REVERB" sets at 15 pt in any face.
        bmo::ui::Fader narrow { param, "REVERB" };
        narrow.setBounds (0, 0, 30, kCellHeight);

        check (narrow.captionOverflow() > 0.0f,
               "captionOverflow reports nothing in a 30 px cell, so it would report nothing anywhere");
    }

    //== The body, the travel, and what the cell has to be ====================
    //
    // 134 and not 120. The mockup's ~120 px strip row is exactly the caption
    // arithmetic *without* a value line -- 120 - 22 - 12 = 86 -- and the value
    // line costs 14 px more. Both numbers are asserted, so whichever the panel
    // pass picks it gets the travel it was promised.
    {
        bmo::ui::Fader fader { param, "REVERB" };
        fader.setBounds (0, 0, kCellWidth, kCellHeight);

        checkNear ((double) fader.bodyBox().getHeight(), (double) kBody, 1.0e-6,
                   "a 134 px cell leaves 98 px of body");
        checkNear ((double) fader.travel(), (double) kTravel, 1.0e-6,
                   "98 px of body less a 12 px cap is 86 px of travel");

        fader.setShowsValue (false);
        fader.setBounds (0, 0, kCellWidth, 120);

        checkNear ((double) fader.travel(), (double) kTravel, 1.0e-6,
                   "without the value line the mockup's 120 px row gives the same 86 px");
    }

    //== The cap tracks the parameter across the range =========================
    //
    // Stated pixels at five positions. The cap's centre starts half a cap down
    // from the top of the body and runs the travel: 6 at the top of the range,
    // 92 at the bottom, 49 in the middle. A cap that ran the wrong way would
    // still be "somewhere on the fader" and would still move when the
    // parameter moved.
    {
        bmo::ui::Fader fader { param, "REVERB" };
        fader.setBounds (0, 0, kCellWidth, kCellHeight);

        struct Position { float normalised; double centreY; };

        const Position positions[]
        {
            { 1.00f,  6.0 },
            { 0.75f, 27.5 },
            { 0.50f, 49.0 },
            { 0.25f, 70.5 },
            { 0.00f, 92.0 },
        };

        for (const auto& p : positions)
        {
            param.setValueNotifyingHost (p.normalised);

            checkNear ((double) fader.capBounds().getCentreY(), p.centreY, 1.0e-4,
                       "the cap sits at " + juce::String (p.centreY, 1) + " px at "
                         + juce::String (p.normalised, 2) + " of the range");

            // Centred across the body, whatever the position: a cap that
            // wandered sideways would still pass every check above.
            checkNear ((double) fader.capBounds().getCentreX(), 50.0, 1.0e-4,
                       "the cap is centred across the fader");
            checkNear ((double) fader.capBounds().getWidth(), 27.6, 1.0e-4,
                       "the cap is 27.6 px across wherever it is");
        }

        // **A cap cannot leave its slot.** The two ends exactly: the cap's own
        // edges land on the body's, which is what the travel being the body
        // less the cap means.
        param.setValueNotifyingHost (1.0f);
        checkNear ((double) fader.capBounds().getY(), 0.0, 1.0e-4,
                   "at the top of the range the cap's top edge is the body's");

        param.setValueNotifyingHost (0.0f);
        checkNear ((double) fader.capBounds().getBottom(), (double) kBody, 1.0e-4,
                   "at the bottom of the range the cap's bottom edge is the body's");

        param.setValueNotifyingHost (param.getDefaultValue());
    }

    //== The surface: Simple by default, the line's finish, the knob's form ===
    //
    // Frosty, 2026-09-25: the plugins as they are are "Simple" and the
    // default. Textured takes the line's finish -- brushed on every line --
    // unless the user picks one for everything. In Textured, each knob's form
    // comes from its own tag, then its section's, then its drawn size.
    {
        using namespace bmo::ui;

        check (surface() == Surface::simple, "a process that has read no preference is Simple");
        check (! BmoLookAndFeel::textured(), "Simple draws no material");

        overrideSurface (Surface::textured, FinishChoice::house);
        check (finishFor (bmoLine()) == PlateFinish::brushed, "BMO's house finish is brushed");
        check (finishFor (ltvLine()) == PlateFinish::brushed, "the collaborations' house finish is brushed too");

        overrideSurface (Surface::textured, FinishChoice::powder);
        check (finishFor (bmoLine()) == PlateFinish::powder,  "powder everywhere reaches BMO");

        overrideSurface (Surface::textured, FinishChoice::brushed);
        check (finishFor (ltvLine()) == PlateFinish::brushed, "brushed everywhere reaches the collaborations");

        overrideSurface (Surface::simple, FinishChoice::house);

        juce::Component section, inner;
        section.addChildComponent (inner);

        // By size: the cap radius is the knob's shorter side, halved, times
        // its face scale. BMO Dimension's non-hero knobs are 19.84 and are
        // one-piece; the line is Tokens::onePieceMaxRadius, 21.0, and the
        // style plays no part -- a small character knob is one-piece, a large
        // trim ringed.
        checkNear ((double) Tokens::onePieceMaxRadius, 21.0, 1.0e-6, "the one-piece line is 21 px of cap radius");

        Knob character, small, large;
        character.setStyle (Knob::Style::character);
        small.setStyle (Knob::Style::character);
        large.setStyle (Knob::Style::utility);

        character.setSize (100, 100);
        character.setFaceScale (0.62f);            // 31.0, BMO FET's INPUT
        small.setSize (46, 46);
        small.setFaceScale (2.0f / 3.0f);          // 15.33, BMO FET's ATTACK
        large.setSize (46, 46);
        large.setFaceScale (1.0f);                 // 23.0

        checkNear ((double) capRadiusOf (small), 15.333, 1.0e-3, "a 46 px knob at 2/3 has a 15.33 px cap");
        check (texturedFormFor (character) == Knob::TexturedForm::ringed,   "a 31 px cap is ringed");
        check (texturedFormFor (small)     == Knob::TexturedForm::onePiece, "a character knob at FET ATTACK's size is one-piece");
        check (texturedFormFor (large)     == Knob::TexturedForm::ringed,   "a 23 px trim is ringed");

        Knob edge;
        edge.setSize (42, 42);                     // 21.0 exactly
        check (texturedFormFor (edge) == Knob::TexturedForm::onePiece, "exactly on the line is one-piece: 'the same size or smaller'");

        // A section tag reaches a knob however deep it sits in the section.
        inner.addChildComponent (character);
        section.getProperties().set (ModulePanel::kTexturedFormTag, (int) Knob::TexturedForm::onePiece);
        check (texturedFormFor (character) == Knob::TexturedForm::onePiece, "a section tag beats the style");

        // The nearest section wins over one further out.
        inner.getProperties().set (ModulePanel::kTexturedFormTag, (int) Knob::TexturedForm::ringed);
        check (texturedFormFor (character) == Knob::TexturedForm::ringed, "the nearest section tag wins");

        // And the knob's own tag beats every section.
        character.setTexturedForm (Knob::TexturedForm::onePiece);
        check (texturedFormFor (character) == Knob::TexturedForm::onePiece, "a knob's own tag beats its sections");

        // A tool's override beats everything, and `automatic` hands back.
        BmoLookAndFeel::overrideKnobForm (Knob::TexturedForm::ringed);
        check (texturedFormFor (character) == Knob::TexturedForm::ringed, "the override beats a knob's tag");
        BmoLookAndFeel::overrideKnobForm (Knob::TexturedForm::automatic);
        check (texturedFormFor (character) == Knob::TexturedForm::onePiece, "automatic restores the tags");

        inner.removeChildComponent (&character);
    }

    if (failures == 0)
        std::cout << "All ui control tests passed.\n";

    return failures == 0 ? 0 : 1;
}
