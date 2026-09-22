// Layout assertions on the real panels.
//
// Nine suites existed before this one and not one of them touched the UI.
// Everything in testing-notes/ui-editor-handoff.md §6 was checked by hand,
// which is why two layout faults shipped in 0.2.1 and why 0.2.3 left three
// hand-matched alignments holding the rack together with nothing watching
// them.
//
// The house rule, from tests/dsp/OptoDspTests.cpp: **assert absolutes, not
// comparisons.** A release test passed for a whole release while both modes
// were broken because it only compared them to each other. Every number below
// is a fixed pixel row named in ui::ModulePanel, not "the same as last time"
// and not "inside the panel somewhere".
//
// No rendering happens here. A panel is laid out by its editor's constructor
// at design size regardless of what the editor is later scaled to, so
// constructing the editor is enough to ask where everything landed.

#include "products/deq/Product.h"
#include "modules/deq/panel/ResponseView.h"
#include "modules/deq/panel/Widgets.h"
#include "modules/vcomp/panel/LevelBars.h"
#include "modules/vcomp/params.h"
#include "products/dim/Product.h"
#include "products/eq/Product.h"
#include "products/opto/Product.h"
#include "products/reverb/Product.h"
#include "modules/reverb/panel/ReverbPanel.h"
#include "modules/reverb/dsp/DspCore.h"
#include "modules/reverb/dsp/TapTables.h"
#include "modules/reverb/params.h"
#include "products/vcomp/Product.h"
#include "products/sat/Product.h"
#include "products/util/Product.h"
#include "products/rack/Product.h"

#include "core/ui/ModulePanel.h"

#include <juce_gui_basics/juce_gui_basics.h>
#include <algorithm>
#include <iostream>
#include <vector>

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

void checkEquals (int actual, int expected, const juce::String& what)
{
    if (actual != expected)
    {
        std::cerr << "FAIL: " << what << " -- expected " << expected
                  << ", got " << actual << '\n';
        ++failures;
    }
}

/** The same, for a figure a panel computes rather than places.
    Everything here is pixels, which are integers, except a drawn picture's own
    arithmetic -- BMO Linger's ER/tail display is the first of those. */
void checkNear (double actual, double expected, double tolerance, const juce::String& what)
{
    if (! (std::abs (actual - expected) <= tolerance))
    {
        std::cerr << "FAIL: " << what << " -- expected " << expected
                  << " +/- " << tolerance << ", got " << actual << '\n';
        ++failures;
    }
}

//== Getting at the panels =====================================================

/** Every ModulePanel under `root`, at any depth.
    A product editor has one; the rack editor has one per slot. */
void collectPanels (juce::Component& root, std::vector<bmo::ui::ModulePanel*>& out)
{
    for (auto* child : root.getChildren())
    {
        if (auto* panel = dynamic_cast<bmo::ui::ModulePanel*> (child))
            out.push_back (panel);

        collectPanels (*child, out);
    }
}

/** A named descendant, or null. Controls name themselves after the caption a
    reader sees -- see PlainKnob's constructor -- so "OUTPUT" here is the same
    OUTPUT that is printed on the panel. */
juce::Component* findNamed (juce::Component& root, const juce::String& name)
{
    for (auto* child : root.getChildren())
    {
        if (child->getName() == name)
            return child;

        if (auto* found = findNamed (*child, name))
            return found;
    }

    return nullptr;
}

/** Every PlainKnob under `root`. */
void collectKnobs (juce::Component& root, std::vector<bmo::ui::PlainKnob*>& out)
{
    for (auto* child : root.getChildren())
    {
        if (auto* knob = dynamic_cast<bmo::ui::PlainKnob*> (child))
            out.push_back (knob);

        collectKnobs (*child, out);
    }
}

/** The `ui::Knob` a PlainKnob draws with, or null.

    A PlainKnob owns its rotary as a private child, so walking to it is the
    only way to ask what style it is drawing in -- and until BMO Linger shipped
    twenty-three group knobs in `utility`, nothing in this file had ever asked
    any panel that question. */
const bmo::ui::Knob* knobFace (const bmo::ui::PlainKnob& knob)
{
    for (const auto* child : knob.getChildren())
        if (const auto* face = dynamic_cast<const bmo::ui::Knob*> (child))
            return face;

    return nullptr;
}

/** Every ChoiceBox under `root`. BMO Linger's TYPE and ER MODE are the only
    two in the suite; the walk is generic so the next one is covered the day it
    is added rather than the day someone remembers. */
void collectChoiceBoxes (juce::Component& root, std::vector<bmo::ui::ChoiceBox*>& out)
{
    for (auto* child : root.getChildren())
    {
        if (auto* choice = dynamic_cast<bmo::ui::ChoiceBox*> (child))
            out.push_back (choice);

        collectChoiceBoxes (*child, out);
    }
}

/** Every SwitchButton under `root`. */
void collectSwitches (juce::Component& root, std::vector<bmo::ui::SwitchButton*>& out)
{
    for (auto* child : root.getChildren())
    {
        if (auto* sw = dynamic_cast<bmo::ui::SwitchButton*> (child))
            out.push_back (sw);

        collectSwitches (*child, out);
    }
}

//== The numbers ===============================================================
//
// ui::ModulePanel's own constants, written out as the literals its header
// comment states. Deriving them from the constants would make this test agree
// with any value they took, including a wrong one; the point is to pin the
// panels to the documented rows.

constexpr int kInputKnobTop    = 4;
constexpr int kInputKnobBottom = 82;    ///< exclusive: knob occupies 4..81
constexpr int kInputRuleCentre = 90;

constexpr int kOutputRuleCentre = 566;
constexpr int kSwitchRowTop     = 574;
constexpr int kSwitchRowBottom  = 602;  ///< exclusive: switches occupy 574..601
constexpr int kOutputKnobTop    = 602;
constexpr int kOutputKnobBottom = 680;  ///< exclusive: knob occupies 602..679

/** Where a panel's rules landed, as centre rows, in the order laid out. */
std::vector<int> ruleCentres (const bmo::ui::ModulePanel& panel)
{
    std::vector<int> out;

    for (const auto& r : panel.getRules())
        out.push_back (r.row.getCentreY());

    return out;
}

//== The assertions ============================================================

/** Asserts a rule landed on `centre`, and says where they all are if not. */
void checkHasRuleAt (const bmo::ui::ModulePanel& panel, int centre, const juce::String& who)
{
    const auto rules = ruleCentres (panel);

    if (std::find (rules.begin(), rules.end(), centre) != rules.end())
        return;

    juce::String where;

    for (auto r : rules)
        where << (where.isEmpty() ? "" : ", ") << r;

    check (false, who + " has no rule centred on row " + juce::String (centre)
                      + " -- its rules are at " + (where.isEmpty() ? "no rows at all" : where));
}

/** A panel that takes the input section puts its trim knob on rows 4..81 and
    its rule's centre on row 90. */
void checkInputSection (bmo::ui::ModulePanel& panel, const juce::String& who)
{
    auto* input = findNamed (panel, "INPUT");

    if (input == nullptr)
    {
        check (false, who + " has no INPUT knob");
        return;
    }

    checkEquals (input->getY(), kInputKnobTop, who + " INPUT knob top");
    checkEquals (input->getBottom(), kInputKnobBottom, who + " INPUT knob bottom");

    checkHasRuleAt (panel, kInputRuleCentre, who);
}

/** Every panel that takes *or reserves* the output section puts a rule's
    centre on row 566.

    Util is the one that proves the reservation works: it has no output knob
    and no switch row down there, and its lower rule still has to land on the
    same line as EQ's and the Saturator's. That alignment is one of the three
    hand-matched ones 0.2.3 left holding the rack together. */
void checkOutputRule (bmo::ui::ModulePanel& panel, const juce::String& who)
{
    checkHasRuleAt (panel, kOutputRuleCentre, who);
}

/** A panel that adopts the output section puts its trim knob on rows 602..679
    and its switches on 574..601. */
void checkOutputSection (bmo::ui::ModulePanel& panel, const juce::String& who)
{
    auto* output = findNamed (panel, "OUTPUT");

    if (output == nullptr)
    {
        check (false, who + " has no OUTPUT knob");
        return;
    }

    checkEquals (output->getY(), kOutputKnobTop, who + " OUTPUT knob top");
    checkEquals (output->getBottom(), kOutputKnobBottom, who + " OUTPUT knob bottom");
}

/** An oversampling row: three switches side by side over one choice parameter,
    and Off is the position none of them lights. The Saturator took one on
    2026-09-17 and BMO CEQ the same day.

    Both halves matter. The geometry, because these are the suite's switch size
    and gap, and a row that drifted off them would be the only one in the rack
    that had. The behaviour, because a click here does not toggle a button: it
    sets a parameter, and the parameter lights the switches. If that loop breaks
    nothing lights at all, and on the Saturator a render of the default state
    cannot tell -- Off is the state with nothing lit either way.

    `litAtInit` is the mask the row opens on: 0 for the Saturator, which starts
    Off, and 1 for CEQ, which starts on 2x. */
void checkOversamplingRow (bmo::ui::ModulePanel& panel, const juce::String& who, int litAtInit)
{
    juce::Button* row[3] {};
    const char* names[3] { "2x", "4x", "8x" };

    for (int i = 0; i < 3; ++i)
    {
        row[i] = dynamic_cast<juce::Button*> (findNamed (panel, names[i]));

        if (row[i] == nullptr)
        {
            check (false, who + " has no " + names[i] + " switch");
            return;
        }
    }

    for (int i = 0; i < 3; ++i)
    {
        checkEquals (row[i]->getWidth(),  bmo::ui::Tokens::switchWidth,
                     who + " " + names[i] + " width");
        checkEquals (row[i]->getHeight(), bmo::ui::Tokens::switchHeight,
                     who + " " + names[i] + " height");
        checkEquals (row[i]->getY(), row[0]->getY(),
                     who + " " + names[i] + " top, against 2x's");
    }

    for (int i = 1; i < 3; ++i)
        checkEquals (row[i]->getX() - row[i - 1]->getRight(), bmo::ui::Tokens::switchGap,
                     who + " gap before " + names[i]);

    check (row[0]->getBottom() < kSwitchRowTop,
           who + " oversampling row should sit above the output switches, is at "
               + juce::String (row[0]->getY()));

    const auto lit = [&row] { return (row[0]->getToggleState() ? 1 : 0)
                                   + (row[1]->getToggleState() ? 2 : 0)
                                   + (row[2]->getToggleState() ? 4 : 0); };

    checkEquals (lit(), litAtInit, who + " oversampling at Init");

    // Back to Off before the walk below, which starts from nothing lit. On CEQ
    // that is itself the assertion that clicking the lit switch is the way to
    // reach Off, since Off is the one position with no switch of its own.
    for (int i = 0; i < 3; ++i)
        if (litAtInit == (1 << i) && row[i]->onClick != nullptr)
            row[i]->onClick();

    checkEquals (lit(), 0, who + " clicking the lit switch reaches Off");

    for (int i = 0; i < 3; ++i)
    {
        if (row[i]->onClick != nullptr)
            row[i]->onClick();

        checkEquals (lit(), 1 << i, who + " " + names[i] + " lit alone after a click");

        if (row[i]->onClick != nullptr)
            row[i]->onClick();

        checkEquals (lit(), 0, who + " " + names[i] + " clicked again is Off");
    }
}

/** The switch named `name` sits in the output section's switch row. */
void checkOutputSwitch (bmo::ui::ModulePanel& panel, const juce::String& name,
                        const juce::String& who)
{
    auto* sw = findNamed (panel, name);

    if (sw == nullptr)
    {
        check (false, who + " has no " + name + " switch");
        return;
    }

    check (sw->getY() >= kSwitchRowTop && sw->getBottom() <= kSwitchRowBottom,
           who + " " + name + " should sit within rows " + juce::String (kSwitchRowTop)
               + ".." + juce::String (kSwitchRowBottom - 1) + ", is "
               + juce::String (sw->getY()) + ".." + juce::String (sw->getBottom() - 1));
}

/** BMO EQ's band column, pinned to absolute rows.

    This is the assertion the suite mainly exists for, and the one that would
    have caught the regression the repository is most exposed to.

    The three bands and the low cut are taken off the top in sequence, between
    the two shared sections. Change kBandRow by one pixel and every row below
    it moves, the column comes up short of the output rule, and *nothing else
    in this file notices*: the input knob, the output knob, the switch row and
    both shared rules are all still exactly where they were, because the
    sections are taken off the two ends before the bands get what is left.

    So the rows are written out. `ui_layout_tests --dump` prints them.

    The flush check at the end is the one that carries the most: BMO EQ has no
    vertical slack anywhere -- no empty band over 16 px on the whole panel --
    so its column ending exactly on the output rule is a real property of this
    layout rather than a coincidence worth asserting loosely. */
void checkEqBandColumn (bmo::ui::ModulePanel& panel)
{
    struct Row { const char* name; int top, height; };

    // 98 is the first row under the input section's rule; 558 is the top of
    // the output section's.
    //
    // The bands were 112 until 2026-09-17, when the oversampling section went
    // in below LO-CUT and they paid for it: a rule, a switch row and the plate
    // under it, 50 px, taken evenly off the three. The column no longer runs
    // to the output rule on its own -- LO-CUT's foot plus that section does.
    constexpr Row rows[] = {
        { "HIGH",   98, 95 },
        { "MID",   209, 95 },
        { "LOW",   320, 95 },
        { "LO-CUT", 431, 77 },
    };

    constexpr int kOutputRuleTop = 558;
    constexpr int kOversamplingSection = 50;

    for (const auto& row : rows)
    {
        auto* band = findNamed (panel, row.name);

        if (band == nullptr)
        {
            check (false, juce::String ("eq has no ") + row.name + " band");
            continue;
        }

        checkEquals (band->getY(), row.top,
                     juce::String ("eq ") + row.name + " band top");
        checkEquals (band->getHeight(), row.height,
                     juce::String ("eq ") + row.name + " band height");
    }

    if (auto* lowCut = findNamed (panel, "LO-CUT"))
        checkEquals (lowCut->getBottom() + kOversamplingSection, kOutputRuleTop,
                     "eq band column plus the oversampling section should end flush against"
                     " the output rule, and its foot");

    // HI-Q went from the output switch row back to the mid band on 2026-09-17,
    // where the control it affects is. It is laid over the band's own cell, so
    // this pins the thing a reader would notice if it drifted: that it is
    // beside the mid band and nowhere near the switch row.
    auto* hiQ = findNamed (panel, "HI-Q");
    auto* midBand = findNamed (panel, "MID");

    if (hiQ == nullptr || midBand == nullptr)
    {
        check (false, "eq should have both a HI-Q switch and a MID band");
        return;
    }

    checkEquals (hiQ->getBounds().getCentreY(), midBand->getBounds().getCentreY(),
                 "eq HI-Q centres on the mid band");
    check (hiQ->getY() >= midBand->getY() && hiQ->getBottom() <= midBand->getBottom(),
           "eq HI-Q should sit within the mid band's own rows, is "
               + juce::String (hiQ->getY()) + ".." + juce::String (hiQ->getBottom() - 1));
    check (hiQ->getBottom() < kSwitchRowTop,
           "eq HI-Q should no longer be on the output switch row");
}

/** Every trim knob is the shared trim height, wherever it appears.

    **This exists because a panel that runs out of room fails silently.**
    juce::Rectangle::removeFromTop *clamps* when the rectangle is shorter than
    the amount asked for: it hands back what is left and leaves the rest empty.
    So a layout whose rows no longer fit does not overflow, it squashes -- and
    every other assertion in this file still passes, because nothing has
    escaped the panel and nothing overlaps.

    LTV Comp did exactly that on 2026-09-15. Its meter block grew a printed
    scale and a tag row for the gate flag, the content stopped fitting in 688,
    and LOW and HIGH came out **40 px tall against the 78 a trim knob is**.
    The suite was green and the render was obviously wrong.

    A trim knob is the right thing to pin because it is the one control with a
    size the suite fixes rather than the panel: ModulePanel::styleTrimKnob sets
    it, kTrimKnobRow is the number, and any panel that hands one less than that
    has run out of room somewhere above it. */
void checkTrimKnobHeights (bmo::ui::ModulePanel& panel, const juce::String& who,
                           const juce::StringArray& captions)
{
    for (const auto& caption : captions)
    {
        auto* knob = findNamed (panel, caption);

        if (knob == nullptr)
        {
            check (false, who + " has no " + caption + " knob");
            continue;
        }

        checkEquals (knob->getHeight(), bmo::ui::ModulePanel::kTrimKnobRow,
                     who + " " + caption + " is a trim knob and should be the trim height"
                         + " -- a short one means the panel ran out of room above it");
    }
}

/** The gate's flag and the name over it move as one control.

    **The class of bug this catches is invisible to everything else here.**
    Both are *painted*, so neither has bounds for the walkers above to read --
    the same blind spot ModulePanel::getRules and DynamicsMeter::vuScale were
    each made public to close. It shipped in #9 and Frosty found it by using
    the plugin: the label was clamped into the well while the flag was not, so
    over the last 17 px of leftward travel the marker went on without its name.

    Asserted at **both ends of the parameter's travel**, because the middle was
    always right -- the clamp only engaged near the rail, which is exactly where
    the gate rests by default.

    It calls the same two functions paint calls. A check that re-derived either
    position its own way could agree with the bug it exists to catch, which is
    the discipline PlainKnob::captionOverflow was written under. */
void checkGateMarkerCarriesItsName (bmo::ui::ModulePanel& panel)
{
    auto* found = findNamed (panel, "IN");
    auto* bar = dynamic_cast<bmo::vcomp::LevelBar*> (found);

    if (bar == nullptr)
    {
        check (false, "ltvcomp has no IN bar to carry the gate");
        return;
    }

    // The ends of kGate's own range, from modules/vcomp/params.h.
    for (const auto db : { bmo::vcomp::kGateOffDb, -10.0f })
    {
        const auto marker = bar->gateMarkerXFor (db);
        const auto label  = bar->gateLabelBoundsFor (db);
        const auto name   = juce::String (db, 1) + " dB";

        checkEquals (juce::roundToInt (label.getCentreX()), juce::roundToInt (marker),
                     "ltvcomp gate name should be centred on its flag at " + name
                         + " -- they are one control");

        check (label.getX() >= 0.0f && label.getRight() <= (float) bar->getWidth(),
               "ltvcomp gate name should stay inside the meter component at " + name
                   + ", is " + juce::String (label.getX(), 1) + ".."
                   + juce::String (label.getRight(), 1) + " of "
                   + juce::String (bar->getWidth()));
    }
}

/** Util reserves the output section without adopting it: the rule is on the
    shared line and there is nothing below it that belongs to an output stage. */
void checkReservesWithoutAdopting (bmo::ui::ModulePanel& panel, const juce::String& who)
{
    check (findNamed (panel, "OUTPUT") == nullptr,
           who + " should have no OUTPUT knob -- it reserves the section, it does not take it");
}

/** No control escapes its panel. */
void checkWithinPanel (bmo::ui::ModulePanel& panel, const juce::String& who)
{
    const auto bounds = panel.getLocalBounds();

    for (auto* child : panel.getChildren())
        check (bounds.contains (child->getBounds()),
               who + " control '" + child->getName() + "' at " + child->getBounds().toString()
                   + " is outside the panel " + bounds.toString());
}

/** No two of a panel's controls overlap.

    They are laid out in a single column in every module, so an overlap is
    always a mistake rather than a design. */
void checkNoOverlap (bmo::ui::ModulePanel& panel, const juce::String& who)
{
    const auto& children = panel.getChildren();

    for (int i = 0; i < children.size(); ++i)
        for (int j = i + 1; j < children.size(); ++j)
        {
            const auto a = children[i]->getBounds();
            const auto b = children[j]->getBounds();

            check (! a.intersects (b),
                   who + " controls '" + children[i]->getName() + "' " + a.toString()
                       + " and '" + children[j]->getName() + "' " + b.toString() + " overlap");
        }
}

/** Every knob caption fits the box it is drawn in.

    This is the MAKEUP -> MAKEU bug, which was a five-character overflow that
    survived a full release because nobody measured it. The measurement lives
    on PlainKnob so it uses the same font and the same box paint does. */
void checkCaptionsFit (bmo::ui::ModulePanel& panel, const juce::String& who)
{
    std::vector<bmo::ui::PlainKnob*> knobs;
    collectKnobs (panel, knobs);

    check (! knobs.empty(), who + " has no knobs, which cannot be right");

    for (auto* knob : knobs)
    {
        const auto overflow = knob->captionOverflow();

        check (overflow <= 0.0f,
               who + " caption '" + knob->getName() + "' overflows its box by "
                   + juce::String (overflow, 1) + " px");
    }

    // A dropdown has two ways to clip and only one of them is a knob's: its
    // caption, measured like the above, and **the widest item in its list**,
    // which is the one nothing else here could see. A type list that fits at
    // "Room" and clips at "Chamber" renders perfectly until somebody opens it.
    // `ChoiceBox::captionOverflow` returns the worse of the two.
    {
        std::vector<bmo::ui::ChoiceBox*> boxes;
        collectChoiceBoxes (panel, boxes);

        for (auto* box : boxes)
        {
            const auto overflow = box->captionOverflow();

            check (overflow <= 0.0f,
                   who + " dropdown '" + box->getName() + "' overflows its box by "
                       + juce::String (overflow, 1) + " px -- caption or widest item");
        }
    }

    // A stepped dial's legend too. BMO EQ's are frequencies ("1k6"); BMO DEQ's
    // shape dial is the first to carry words (setLegend), and words are what
    // outgrow a 38 px box.
    std::function<void (juce::Component&)> walk = [&] (juce::Component& c)
    {
        for (auto* child : c.getChildren())
        {
            if (auto* band = dynamic_cast<bmo::ui::ConcentricBand*> (child))
                check (band->legendOverflow() <= 0.0f,
                       who + " a legend on '" + c.getName() + "' overflows its box by "
                           + juce::String (band->legendOverflow(), 1) + " px");

            walk (*child);
        }
    };
    walk (panel);
}

/** BMO DEQ's own: the band's knobs print their values, the shape is the
    stepped dial with its name fitting under it, and AUTO shares the output
    row with DEQ. Frosty's three calls on the first build (2026-09-11), so a
    later layout pass cannot quietly undo one. */
void checkDeqPanel (bmo::ui::ModulePanel& panel, const juce::String& who)
{
    std::vector<bmo::ui::PlainKnob*> knobs;
    collectKnobs (panel, knobs);

    auto band = 0;
    for (auto* knob : knobs)
        if (knob->getName() != "OUTPUT")
        {
            ++band;
            check (knob->isShowingValue(), who + " knob '" + knob->getName() + "' shows no value");
        }

    checkEquals (band, 8, who + " band knobs (FREQ GAIN Q THRESH RANGE RATIO ATTACK RELEASE)");

    auto* shape = dynamic_cast<bmo::deq::ShapeDial*> (findNamed (panel, "SHAPE"));
    check (shape != nullptr, who + " has no SHAPE dial");

    if (shape != nullptr)
        check (shape->captionOverflow() <= 0.0f,
               who + " SHAPE or its legend overflows by " + juce::String (shape->captionOverflow(), 1) + " px");

    // The gain-reduction bar's words. It is a Component that paints its own
    // caption and readout, so neither checkCaptionFits nor checkSwitchLabelsFit
    // has ever looked at it -- and the readout was clipping to "-12." on the
    // full panel from the module's first build. Same class as MAKEUP -> MAKEU,
    // one container over: the text that gets measured is the text somebody
    // remembered to measure.
    bmo::deq::GainReductionBar* gr = nullptr;

    for (auto* child : panel.getChildren())
        if (auto* bar = dynamic_cast<bmo::deq::GainReductionBar*> (child))
            gr = bar;

    check (gr != nullptr, who + " has no gain-reduction bar");

    if (gr != nullptr)
        check (gr->valueOverflow() <= 0.0f,
               who + " GR bar's widest word (\"" + bmo::deq::GainReductionBar::widestValue()
                   + "\") overflows its " + juce::String (gr->getWidth()) + " px cell by "
                   + juce::String (gr->valueOverflow(), 1) + " px");

    checkOutputSwitch (panel, "AUTO", who);
}

/** A mouse event good enough to drive a component's own handler.

    The suite has never needed one: `ui_layout` asserts bounds, and everything
    else a panel does was reachable through a parameter. BMO DEQ's band on/off
    is the first control that is a **gesture and nothing else** -- no switch, no
    affordance -- so it is the first one where "it compiles" is not evidence
    that it works. */
juce::MouseEvent clickAt (juce::Component& c, juce::Point<float> p, int clicks)
{
    const auto now = juce::Time::getCurrentTime();

    return { juce::Desktop::getInstance().getMainMouseSource(), p, juce::ModifierKeys(),
             juce::MouseInputSource::defaultPressure, juce::MouseInputSource::defaultOrientation,
             juce::MouseInputSource::defaultRotation, juce::MouseInputSource::defaultTiltX,
             juce::MouseInputSource::defaultTiltY, &c, &c, now, p, now, clicks, false };
}

/** A mouse event with a chosen set of buttons held. */
juce::MouseEvent buttonsAt (juce::Component& c, juce::Point<float> p, juce::ModifierKeys mods)
{
    const auto now = juce::Time::getCurrentTime();

    return { juce::Desktop::getInstance().getMainMouseSource(), p, mods,
             juce::MouseInputSource::defaultPressure, juce::MouseInputSource::defaultOrientation,
             juce::MouseInputSource::defaultRotation, juce::MouseInputSource::defaultTiltX,
             juce::MouseInputSource::defaultTiltY, &c, &c, now, p, now, 1, false };
}

/** Right-clicking a node solos it, and **right-clicking one that is already
    being dragged with the left button solos it without ending the drag.**

    The second half is why this test exists. JUCE never delivers a second
    mouseDown while a button is held -- `MouseInputSourceImpl::setButtons`
    returns early, "ignore secondary clicks when there's already a button
    down" -- so the gesture is only visible in `mouseDrag`'s modifiers. A
    reasonable implementation in `mouseDown` would compile, pass review, and
    silently never fire. */
void checkDeqNodeSolo (bmo::ui::ModulePanel& panel, const juce::String& who, std::vector<int>& soloCalls)
{
    bmo::deq::ResponseView* curve = nullptr;

    for (auto* child : panel.getChildren())
        if (auto* v = dynamic_cast<bmo::deq::ResponseView*> (child))
            curve = v;

    check (curve != nullptr, who + " has no response view");

    if (curve == nullptr)
        return;

    auto& params = panel.getContext().params;

    // Band 1 at its default 30 Hz, switched on, as a bell so it has a gain and
    // its node sits on the zero line rather than being pinned there.
    const auto band = 0;
    params.setReal (bmo::deq::indexOf (band, bmo::deq::Control::shape), 0.0f);
    params.setReal (bmo::deq::indexOf (band, bmo::deq::Control::gain), 0.0f);
    params.setReal (bmo::deq::indexOf (band, bmo::deq::Control::on), 1.0f);

    // Where that node is, from the view's own geometry rather than a repeat of
    // its log mapping: 30 Hz of a 20..20000 sweep, and 0 dB is the centre.
    const auto r = curve->plot();
    const auto x = r.getX() + r.getWidth() * (float) (std::log (30.0 / 20.0) / std::log (20000.0 / 20.0));
    const juce::Point<float> node { x, r.getCentreY() };

    const auto before = soloCalls.size();

    // 1. A plain right-click on the node.
    curve->mouseDown (buttonsAt (*curve, node, juce::ModifierKeys (juce::ModifierKeys::rightButtonModifier)));
    check (soloCalls.size() == before + 1 && soloCalls.back() == band,
           who + " right-clicking band 1's node should solo it");

    curve->mouseUp (buttonsAt (*curve, node, juce::ModifierKeys()));
    check (soloCalls.size() == before + 2 && soloCalls.back() == -1,
           who + " releasing the node should clear the solo");

    // 2. Left-drag the node, then add the right button mid-drag.
    curve->mouseDown (buttonsAt (*curve, node, juce::ModifierKeys (juce::ModifierKeys::leftButtonModifier)));
    curve->mouseDrag (buttonsAt (*curve, node, juce::ModifierKeys (juce::ModifierKeys::leftButtonModifier)));

    check (soloCalls.size() == before + 2,
           who + " dragging a node alone should not solo anything");

    const auto both = juce::ModifierKeys (juce::ModifierKeys::leftButtonModifier | juce::ModifierKeys::rightButtonModifier);
    curve->mouseDrag (buttonsAt (*curve, node, both));

    check (soloCalls.size() == before + 3 && soloCalls.back() == band,
           who + " right-clicking during a drag should solo the band being dragged");

    // Still dragging: the freq parameter must keep following the mouse.
    const auto movedTo = juce::Point<float> (r.getCentreX(), r.getCentreY());
    curve->mouseDrag (buttonsAt (*curve, movedTo, both));
    check (params.getReal (bmo::deq::indexOf (band, bmo::deq::Control::freq)) > 100.0f,
           who + " the drag should continue while soloed, but band 1 stayed at "
               + juce::String (params.getReal (bmo::deq::indexOf (band, bmo::deq::Control::freq)), 1) + " Hz");

    // Letting go of the right button alone ends the solo, not the drag.
    curve->mouseDrag (buttonsAt (*curve, movedTo, juce::ModifierKeys (juce::ModifierKeys::leftButtonModifier)));
    check (soloCalls.size() == before + 4 && soloCalls.back() == -1,
           who + " releasing the right button mid-drag should clear the solo");

    curve->mouseUp (buttonsAt (*curve, movedTo, juce::ModifierKeys()));
}

/** Double-clicking a band's tab switches that band on, and again switches it
    off.

    This is the whole of band on/off since the ON switch was dropped on
    2026-09-15, so if it breaks there is no other way to reach the parameter
    from the panel and nothing else would notice. */
void checkDeqBandToggle (bmo::ui::ModulePanel& panel, const juce::String& who)
{
    bmo::deq::BandTabs* tabs = nullptr;

    for (auto* child : panel.getChildren())
        if (auto* t = dynamic_cast<bmo::deq::BandTabs*> (child))
            tabs = t;

    check (tabs != nullptr, who + " has no band tabs");

    if (tabs == nullptr)
        return;

    auto& params = panel.getContext().params;

    // Band 5, so a failure cannot be the selected band or band 1 by accident.
    const auto band = 4;
    const auto onIndex = bmo::deq::indexOf (band, bmo::deq::Control::on);
    const auto centre = tabs->tabBounds (band).getCentre().toFloat();

    check (params.getReal (onIndex) < 0.5f, who + " band 5 should start off");

    tabs->mouseDoubleClick (clickAt (*tabs, centre, 2));
    check (params.getReal (onIndex) > 0.5f,
           who + " double-clicking band 5's tab should switch it on");

    tabs->mouseDoubleClick (clickAt (*tabs, centre, 2));
    check (params.getReal (onIndex) < 0.5f,
           who + " double-clicking band 5's tab again should switch it off");

    // Right-click held is solo, released is not. Solo is the suite's first
    // non-parameter path from editor to engine, so there is no value to read
    // back: the call itself is the whole of the behaviour, and intercepting it
    // is the only way to assert on it.
    std::vector<int> soloCalls;
    auto& ctx = const_cast<bmo::ui::ModuleContext&> (panel.getContext());
    const auto realSolo = ctx.setSolo;

    ctx.setSolo = [&soloCalls, realSolo] (int b)
    {
        soloCalls.push_back (b);
        if (realSolo) realSolo (b);
    };

    const auto rightClick = juce::MouseEvent (juce::Desktop::getInstance().getMainMouseSource(), centre,
                                              juce::ModifierKeys (juce::ModifierKeys::rightButtonModifier),
                                              juce::MouseInputSource::defaultPressure, juce::MouseInputSource::defaultOrientation,
                                              juce::MouseInputSource::defaultRotation, juce::MouseInputSource::defaultTiltX,
                                              juce::MouseInputSource::defaultTiltY, tabs, tabs,
                                              juce::Time::getCurrentTime(), centre, juce::Time::getCurrentTime(), 1, false);

    tabs->mouseDown (rightClick);
    check (soloCalls.size() == 1 && soloCalls.back() == band,
           who + " right-clicking band 5's tab should solo band 5, got "
               + (soloCalls.empty() ? juce::String ("no call") : juce::String (soloCalls.back())));

    tabs->mouseUp (rightClick);
    check (soloCalls.size() == 2 && soloCalls.back() == -1,
           who + " releasing should clear the solo with -1");

    checkDeqNodeSolo (panel, who, soloCalls);

    ctx.setSolo = realSolo;
}

/** Every switch label fits its switch.

    Switches are one size across the whole suite -- Tokens::switchWidth -- so a
    label is only ever as wide as the word someone chose. Nothing measured
    that until now; the knob captions were covered and the switches were not,
    which is half the text on a panel. */
void checkSwitchLabelsFit (bmo::ui::ModulePanel& panel, const juce::String& who)
{
    std::vector<bmo::ui::SwitchButton*> switches;
    collectSwitches (panel, switches);

    for (auto* sw : switches)
    {
        const auto overflow = sw->labelOverflow();

        check (overflow <= 0.0f,
               who + " switch '" + sw->getName() + "' label overflows its box by "
                   + juce::String (overflow, 1) + " px");
    }

    // And every toggle that is *not* inside a SwitchButton: BMO DEQ's rows of
    // switches for a choice (STEREO / MID / SIDE, the five shapes) are plain
    // ToggleButtons drawn by the same look and feel. They clipped to "BEL" and
    // "LO CU" on DEQ's first render while this check still only looked for
    // SwitchButtons -- the same blind spot MAKEUP fell through, one class over.
    std::function<void (juce::Component&)> walk = [&] (juce::Component& c)
    {
        for (auto* child : c.getChildren())
        {
            if (dynamic_cast<bmo::ui::SwitchButton*> (child) != nullptr)
                continue;

            if (auto* toggle = dynamic_cast<juce::ToggleButton*> (child))
            {
                const auto overflow = bmo::ui::BmoLookAndFeel::toggleLabelOverflow (*toggle);

                check (overflow <= 0.0f,
                       who + " switch '" + toggle->getButtonText() + "' label overflows its box by "
                           + juce::String (overflow, 1) + " px");
            }

            walk (*child);
        }
    };
    walk (panel);
}

//== The meter scales ==========================================================

/** A printed scale is well formed.

    These are the cheap invariants — no rendering, no component, just the
    table. They exist because the reduction scale's fractions stopped being
    computed on 8 Sep and became thirteen numbers typed by hand, and a
    transposed pair in that list is invisible: the needle would simply run
    backwards over a stretch of the dial and every other test would pass.

    What is *not* checked here is crowding, which is the thing that actually
    decides whether a figure can be added. That is a pixel question — it
    depends on the label ring radius and on how many digits each figure has —
    so `kMinInkedGap` below is a floor to catch someone jamming figures in, not
    the real limit. Measure a new figure with `tools/inspect` before trusting
    it; `testing-notes/ui-editor-handoff.md` carries the numbers. */
void checkScale (const std::vector<bmo::ui::DynamicsMeter::ScalePoint>& scale,
                 const juce::String& who, float lastValue)
{
    if (scale.size() < 2)
    {
        check (false, who + " scale needs at least two points");
        return;
    }

    // The ends are the sweep. A scale that did not start at 0 or reach 1 would
    // leave part of the dial unreachable by the needle.
    check (scale.front().fraction == 0.0f,
           who + " scale should start at fraction 0, starts at "
               + juce::String (scale.front().fraction, 3));
    check (scale.back().fraction == 1.0f,
           who + " scale should end at fraction 1, ends at "
               + juce::String (scale.back().fraction, 3));
    check (scale.back().value == lastValue,
           who + " scale should end at " + juce::String (lastValue, 1)
               + ", ends at " + juce::String (scale.back().value, 1));

    // Both axes strictly increasing. This is the one that catches a typo.
    for (size_t i = 1; i < scale.size(); ++i)
    {
        check (scale[i].value > scale[i - 1].value,
               who + " scale values must increase: " + juce::String (scale[i - 1].value, 1)
                   + " then " + juce::String (scale[i].value, 1));

        check (scale[i].fraction > scale[i - 1].fraction,
               who + " scale fractions must increase: " + juce::String (scale[i - 1].fraction, 3)
                   + " then " + juce::String (scale[i].fraction, 3)
                   + " (at " + juce::String (scale[i].value, 1) + ")");
    }

    // A floor, not the real limit. See the note above.
    constexpr float kMinInkedGap = 0.10f;

    const bmo::ui::DynamicsMeter::ScalePoint* previousInked = nullptr;

    for (const auto& p : scale)
    {
        if (! p.numbered)
            continue;

        if (previousInked != nullptr)
            check (p.fraction - previousInked->fraction >= kMinInkedGap,
                   who + " printed figures " + juce::String (previousInked->value, 1)
                       + " and " + juce::String (p.value, 1) + " are only "
                       + juce::String (p.fraction - previousInked->fraction, 3)
                       + " of the sweep apart, under the " + juce::String (kMinInkedGap, 2)
                       + " floor -- measure it before printing both");

        previousInked = &p;
    }
}

/** BMO Linger's panel: the paged handheld.

    One width, three pages, and a screen that draws a different picture on each
    of them. The panel is walked once per page, because **a page that is not
    showing is not laid out**: its controls are unparented, so every generic
    check in this file -- nothing escapes, nothing overlaps, every caption fits
    -- sees only a third of the module unless it is run three times. That is
    the whole reason the page is reachable from a test at all.

    **The screen is what nothing else here can see.** It has no parameter of
    its own and paints itself, so if it stopped following the knobs every other
    check on this panel would still pass -- the blind spot BMO DEQ's
    gain-reduction bar and BMO Defang's band sketch both fell through. Its
    numbers are arithmetic rather than drawing, so this needs no render.

    The bezel, the readout line and the grille are **painted**, so they have no
    bounds a walker can read either; `ReverbPanel` exposes them for the reason
    `ui::ModulePanel::getRules` is public. */
void checkReverbPanel (bmo::ui::ModulePanel& panel, const juce::String& who)
{
    namespace R = bmo::reverb;

    auto* reverbPanel = dynamic_cast<R::ReverbPanel*> (&panel);

    check (reverbPanel != nullptr, who + " is not a ReverbPanel");

    if (reverbPanel == nullptr)
        return;

    auto& params = panel.getContext().params;

    //== The captions, page by page, written out ==============================
    //
    // Written out here rather than read off `ReverbPanel::pageControls`: a
    // test that took the list from the same place the panel does would agree
    // with any list, including one that had quietly lost a control. The counts
    // are 11 section 4's split and they add up to the schema.

    // TYPE is last rather than first: it is the corner control at the foot
    // now, not the head of the persistent row.
    const char* const alwaysOn[] { "SIZE", "PRE-DELAY", "DECAY",
                                   "ER", "REVERB", "MIX", "TYPE" };

    const char* const earlyPage[] { "ER MODE", "DENSITY", "ER SPREAD",
                                    "ER HI-CUT", "VARIATION", "SOURCE" };

    const char* const tailPage[] { "LOW x", "HIGH x", "MOD DEPTH", "MOD RATE", "WIDTH" };

    // **Twelve, in four rows of three, and the rows are the nodes.** The page
    // was TONE with six until 2026-09-21; the Reverb EQ's six new parameters
    // brought six new controls and the page was relabelled EQ.
    //
    // "EQ HIGH FREQ" and "IN HI-CUT" appear in this list a few entries apart
    // on purpose: the module has **two** high cuts -- node 3 with FILTER on,
    // and the input's, ahead of the EQ -- and the captions are what tell them
    // apart. If either is ever renamed to something carrying neither "EQ" nor
    // "IN", this list is where a reviewer sees the two words together.
    const char* const eqPage[] { "EQ LOW FREQ", "EQ LOW", "EQ LOW Q",
                                 "EQ MID FREQ", "EQ MID", "EQ MID Q",
                                 "EQ HIGH FREQ", "EQ HIGH", "EQ HIGH Q",
                                 "FILTER", "IN HI-CUT", "OUTPUT" };

    // Seven persistent plus six plus five plus twelve is the whole schema, and
    // the panel is where that sum is checked: a parameter with no control
    // anywhere is the Saturator's oversampling row, which sat on the schema
    // and nowhere on the panel for three releases.
    //
    // **This is also where both of 2026-09-21's schema changes land on the
    // face.** The control-set trim took six controls out with six parameters
    // and the Reverb EQ put six back; this sum is what stops one being deleted
    // from the schema and left on the panel, or added to the schema and never
    // given a control.
    check ((int) (std::size (alwaysOn) + std::size (earlyPage)
                    + std::size (tailPage) + std::size (eqPage)) == (int) R::Index::count,
           "every parameter has a control on some page of the panel");

    const auto captionsFor = [&] (R::Page p) -> const char* const*
    {
        return p == R::Page::early ? earlyPage : (p == R::Page::tail ? tailPage : eqPage);
    };

    const auto countFor = [&] (R::Page p)
    {
        return p == R::Page::early ? (int) std::size (earlyPage)
             : p == R::Page::tail  ? (int) std::size (tailPage)
                                   : (int) std::size (eqPage);
    };

    const auto nameFor = [] (R::Page p)
    {
        return p == R::Page::early ? "EARLY" : (p == R::Page::tail ? "TAIL" : "EQ");
    };

    static constexpr R::Page kPages[] { R::Page::early, R::Page::tail, R::Page::eq };

    //== The bezel, the screen inside it and the line under it ================
    {
        const auto bezel  = reverbPanel->getBezelBox();
        const auto glass  = reverbPanel->getScreenBox();
        const auto line   = reverbPanel->getReadoutBox();

        check (panel.getLocalBounds().contains (bezel), who + " the bezel escapes the panel");
        check (bezel.contains (glass), who + " the screen is not inside its bezel");
        check (bezel.contains (line), who + " the readout line is not inside the bezel");

        check (line.getY() >= glass.getBottom(),
               who + " the reading must be printed UNDER the screen, not over it");

        // **Noticeably larger**, which is the word the brief uses and the thing
        // that makes a recess read as a bezel rather than as a border. 24 px of
        // well across and the readout's own row down.
        check (bezel.getWidth() - glass.getWidth() >= 20,
               who + " the bezel is only " + juce::String (bezel.getWidth() - glass.getWidth())
                   + " px wider than the screen");
        check (bezel.getHeight() - glass.getHeight() >= 30,
               who + " the bezel is only " + juce::String (bezel.getHeight() - glass.getHeight())
                   + " px taller than the screen");

        if (auto* display = findNamed (panel, "DISPLAY"))
            check (display->getBounds() == glass,
                   who + " the screen component is at " + display->getBounds().toString()
                       + " and the bezel put it at " + glass.toString());
    }

    //== The three page keys, and the names inside them ========================
    //
    // Round, and **level rather than raked** -- Frosty's explicit call. A rack
    // is scanned in rows against the slot beside it, so a sloped key block
    // would be the one thing in the window that did not line up.
    //
    // **The name is inside the key as of 2026-09-22**, which is why the two
    // label assertions below exist at all. `checkCaptionsFit` further down
    // walks captions *under* controls -- knobs, dropdowns -- and a word set in
    // the middle of a circle is invisible to it, so a key whose name had
    // outgrown its circle would have walked straight through the generic pass.
    // That is the MAKEUP -> MAKEU fault with a round bound, and a circle is the
    // less forgiving shape: the room runs out fastest at the tops of the
    // letters, which is exactly where the reader is looking.
    {
        const auto& first = reverbPanel->getPageButton (R::Page::early);

        for (const auto p : kPages)
        {
            const auto& key = reverbPanel->getPageButton (p);
            const auto dot = key.dotBounds();

            check (key.getName() == nameFor (p),
                   juce::String (who) + " the " + nameFor (p) + " key is named '" + key.getName() + "'");

            checkEquals (dot.getWidth(), dot.getHeight(),
                         juce::String (who) + " the " + nameFor (p) + " key should be round");
            checkEquals (dot.getWidth(), R::PageButton::kDotSide,
                         juce::String (who) + " the " + nameFor (p) + " key's diameter");

            // The name fits the room it is given...
            check (key.labelOverflow() <= 0.0f,
                   juce::String (who) + " page key '" + key.getName() + "' overflows its box by "
                       + juce::String (key.labelOverflow(), 1) + " px");

            // ...and the room it is given is inside the circle. Two halves of
            // one claim, and neither is worth much alone: a label box that had
            // drifted outside the key would pass the overflow check by being
            // roomy, and an overflow check alone says nothing about where the
            // box it measured against actually is.
            check (dot.contains (key.labelBox()),
                   juce::String (who) + " page key '" + key.getName() + "' sets its name in "
                       + key.labelBox().toString() + ", which is not inside the key "
                       + dot.toString());

            checkEquals (key.getY(), first.getY(),
                         juce::String (who) + " the " + nameFor (p)
                             + " key's top -- the keys are level, not raked");
            checkEquals (key.getBottom(), first.getBottom(),
                         juce::String (who) + " the " + nameFor (p) + " key's foot");
        }
    }

    //== The two rules: PAGE over the keys, LEVEL over the strip ==============
    //
    // Both are `ModulePanel::addRule`, which is the suite's one section device
    // -- a hairline with the legend knocked out of the middle. There is no
    // second kind of rule here, which is the thing worth asserting: PAGE
    // arrived on 2026-09-22 and the easy wrong answer would have been a
    // hand-painted legend that looked the same and drifted later.
    //
    // They are asserted **in order** and by index, because the order is the
    // layout: PAGE is up at the keys and LEVEL is down at the foot, and a
    // panel that had added them the other way round would be a panel whose
    // legends had swapped ends.
    {
        const auto& rules = panel.getRules();

        check (rules.size() == 2,
               who + " should carry exactly two rules, has " + juce::String ((int) rules.size()));

        if (rules.size() == 2)
        {
            check (rules[0].text == "PAGE",
                   who + " the first rule should be legended PAGE, reads '" + rules[0].text + "'");
            check (rules[1].text == "LEVEL",
                   who + " the second rule should be legended LEVEL, reads '" + rules[1].text + "'");

            // **PAGE points up, and proximity is the only thing that says so.**
            // It sits hard under the keys -- no gap at all -- and a full
            // `switchGap` above the persistent row beneath it, which is LEVEL's
            // arrangement upside down. If that ever inverts, the legend starts
            // reading as a heading for SIZE / PRE-DELAY / DECAY.
            const auto& early = reverbPanel->getPageButton (R::Page::early);

            checkEquals (rules[0].row.getY(), early.getBottom(),
                         who + " the PAGE rule should sit hard under the keys");

            if (auto* size = findNamed (panel, "SIZE"))
                check (size->getY() - rules[0].row.getBottom() >= bmo::ui::Tokens::switchGap,
                       who + " the PAGE rule is "
                           + juce::String (size->getY() - rules[0].row.getBottom())
                           + " px above the persistent row and should be at least a switchGap --"
                             " a legend nearer the block below it than the block above reads as"
                             " heading the wrong one");

            // TYPE is under LEVEL too, and deliberately: the rule says LEVEL
            // and a type is not a level, but a rule that stopped one cell
            // short would be a second kind of rule in the suite. The wrinkle is
            // owned in `ReverbPanel`'s class comment, so it is asserted here
            // rather than left to look like an accident.
            for (const auto* caption : { "ER", "REVERB", "MIX", "TYPE" })
                if (auto* c = findNamed (panel, caption))
                    check (c->getY() >= rules[1].row.getBottom(),
                           who + " " + caption + " should sit under the LEVEL rule");
        }
    }

    //== TYPE has the bottom-right corner, and the grille is gone =============
    //
    // The grille was painted texture in the fourth column of the level strip
    // and was cut on 2026-09-21; TYPE took the corner, which is Frosty's call
    // and two arguments at once -- a dropdown is not knob-shaped, and the foot
    // is where two of the nine parameters a type change stamps already are.
    //
    // `getGrilleBox` went with it, so there is nothing to assert the absence
    // of directly. What is asserted is the thing that replaced it: TYPE is a
    // real component, it is in the bottom-right quadrant, and it is to the
    // right of MIX on MIX's own row.
    {
        auto* type = findNamed (panel, "TYPE");
        auto* mix  = findNamed (panel, "MIX");

        check (type != nullptr, who + " has no TYPE control");

        if (type != nullptr && mix != nullptr)
        {
            check (type->getBounds().getCentreX() > panel.getWidth() / 2
                     && type->getBounds().getCentreY() > panel.getHeight() / 2,
                   who + " TYPE should sit in the bottom-right, is at "
                       + type->getBounds().toString());

            check (type->getX() >= mix->getRight(),
                   who + " TYPE should sit beside the strip, right of MIX -- TYPE is at "
                       + type->getBounds().toString() + " and MIX ends at "
                       + juce::String (mix->getRight()));

            checkEquals (type->getBounds().getCentreY(), mix->getBounds().getCentreY(),
                         who + " TYPE shares the strip's row");

            // And it is the corner: nothing on the panel reaches further right
            // or further down than it does.
            for (auto* child : panel.getChildren())
            {
                if (child == type)
                    continue;

                check (child->getRight() <= type->getRight() && child->getBottom() <= type->getBottom(),
                       who + " '" + child->getName() + "' at " + child->getBounds().toString()
                           + " reaches past TYPE's corner " + type->getBounds().toString());
            }
        }
    }

    //== Every page, laid out and walked ======================================
    for (const auto p : kPages)
    {
        const juce::String where { juce::String (who) + " " + nameFor (p) };

        reverbPanel->setPage (p);

        check (reverbPanel->getPage() == p, where + " did not take the page");
        check (reverbPanel->getScreen().getPage() == p,
               where + " the screen is drawing a different page from the panel");

        // One key lit, and it is this one.
        for (const auto q : kPages)
            check (reverbPanel->getPageButton (q).getToggleState() == (q == p),
                   where + ": the " + nameFor (q) + " key should be "
                         + (q == p ? "lit" : "unlit"));

        // The persistent controls are on screen whatever page this is -- that
        // is the whole claim being made about them.
        for (const auto* caption : alwaysOn)
            check (findNamed (panel, caption) != nullptr,
                   where + " has lost the persistent control " + caption);

        // This page's cluster is present and inside the cluster block; the
        // other two pages' controls are **not children at all**. A hidden
        // component still has bounds, and every walker in this file reads them
        // -- so a hidden control with a stale rectangle either escapes the
        // panel, overlaps something, or reports a caption overflowing a box of
        // width zero. Unparenting is the one state in which a control is
        // genuinely not part of the layout.
        {
            const auto cluster = reverbPanel->getClusterBox();
            check (! cluster.isEmpty(), where + " has no cluster block");

            const auto* const* mine = captionsFor (p);

            for (int i = 0; i < countFor (p); ++i)
            {
                auto* found = findNamed (panel, mine[i]);

                check (found != nullptr, where + " has no " + mine[i] + " control");

                if (found != nullptr)
                    check (cluster.contains (found->getBounds()),
                           where + " " + mine[i] + " at " + found->getBounds().toString()
                                 + " is outside the cluster block " + cluster.toString());
            }

            for (const auto q : kPages)
            {
                if (q == p)
                    continue;

                const auto* const* theirs = captionsFor (q);

                for (int i = 0; i < countFor (q); ++i)
                    check (findNamed (panel, theirs[i]) == nullptr,
                           where + " still carries " + theirs[i] + ", which belongs to "
                                 + nameFor (q) + " -- a page that is not showing must be"
                                   " unparented, not hidden");
            }
        }

        // Everything the generic walk does, once per page, because a page that
        // is not showing is not laid out and would otherwise never be looked
        // at. **This is where the MAKEUP -> MAKEU class of fault would show
        // up** on two thirds of this module.
        checkWithinPanel     (panel, where);
        checkNoOverlap       (panel, where);
        checkCaptionsFit     (panel, where);
        checkSwitchLabelsFit (panel, where);

        // **One module, one colour.** `ui::Knob::Style` says what a knob is,
        // not where it sits: `utility` is the pale blue of input, output and
        // gain, `character` is the module's own accent worn by anything that
        // shapes the sound. Twenty-three of these went out `utility` as a
        // block once, and the module rendered violet at the top and suite
        // azure below as though it were two plugins sharing a slot -- while
        // every check in this file passed, because none of them had ever read
        // a style. Asserted by name against a single exception rather than by
        // counting, so a control added later in the wrong style has to fail.
        {
            std::vector<bmo::ui::PlainKnob*> knobs;
            collectKnobs (panel, knobs);

            for (auto* knob : knobs)
            {
                const auto* face = knobFace (*knob);

                if (face == nullptr)
                {
                    check (false, where + " " + knob->getName() + " has no rotary under it");
                    continue;
                }

                const auto utility = knob->getName() == "OUTPUT";

                check (face->getStyle() == (utility ? bmo::ui::Knob::Style::utility
                                                    : bmo::ui::Knob::Style::character),
                       where + " " + knob->getName() + " should draw in "
                             + (utility ? "utility" : "character")
                             + " -- utility is input, output and gain, and OUTPUT is the"
                               " only one of those on this panel");
            }
        }

        // **Nothing stands alone in a row.** That is the rule MIX broke on the
        // old face: a lone centred knob with two empty quarters beside it
        // reads as a control whose partner has gone missing. The screen is the
        // one thing here that is allowed a row of its own, because it is the
        // row.
        //
        // A row is a shared centre line rather than a shared top: LINK ER is a
        // 26 px switch centred in a 74 px cell beside three knobs, and asking
        // a switch to share a knob's top edge would mean stretching it to a
        // knob's height, which reads as a heading for the cell next to it.
        // Two *knobs* in one row still have to share a top and a foot, and
        // that is asserted on top.
        {
            for (auto* child : panel.getChildren())
            {
                if (child->getName() == "DISPLAY")
                    continue;

                const auto mid = child->getBounds().getCentreY();
                int alongside = 0;

                for (auto* other : panel.getChildren())
                {
                    if (other == child || other->getName() == "DISPLAY")
                        continue;

                    if (std::abs (other->getBounds().getCentreY() - mid) > 1)
                        continue;

                    ++alongside;

                    const auto bothAreDials =
                        dynamic_cast<bmo::ui::SwitchButton*> (child) == nullptr
                          && dynamic_cast<bmo::ui::SwitchButton*> (other) == nullptr;

                    if (bothAreDials)
                        check (other->getY() == child->getY()
                                 && other->getBottom() == child->getBottom(),
                               where + " '" + child->getName() + "' and '" + other->getName()
                                     + "' share a row and should share a top and a foot");
                }

                check (alongside > 0,
                       where + " '" + child->getName() + "' is the only control in its row");
            }
        }

        // The reading under the screen says what page it is of, and it is
        // **ASCII**: the two display faces are licensed individually and live
        // outside this repository, so a glyph outside ASCII is one this suite
        // cannot promise it can draw.
        {
            const auto text = reverbPanel->getScreen().readout();

            check (text.isNotEmpty(), where + " prints no reading under the screen");

            for (const auto* c = text.toRawUTF8(); *c != 0; ++c)
                if ((unsigned char) *c < 32 || (unsigned char) *c > 126)
                {
                    check (false, where + " the reading '" + text + "' is not ASCII");
                    break;
                }
        }

        //-- Nothing drawn inside the screen is cut off by the screen ---------
        //
        // **This is the IN HI-CUT failure with a walk round it.** That marker
        // was drawn centred on 20 kHz, which is the right-hand end of the axis
        // exactly, so half of it fell outside the plot: the panel shipped a
        // clipped mark that every render showed and no test could see. The
        // screen now hands out every label box and the curtain's rectangle,
        // and both are checked against the plot on every page.
        {
            const auto& screen = reverbPanel->getScreen();
            const auto  plot   = screen.plotBounds();

            check (! plot.isEmpty(), where + " the screen has no plotting area");

            for (const auto& label : screen.axisLabels())
            {
                check (label.text.isNotEmpty(),
                       where + " the screen sets an empty tick label");

                check (plot.contains (label.box),
                       where + " the tick '" + label.text + "' is set in "
                             + label.box.toString() + ", which is not inside the plot "
                             + plot.toString());

                for (const auto* c = label.text.toRawUTF8(); *c != 0; ++c)
                    if ((unsigned char) *c < 32 || (unsigned char) *c > 126)
                    {
                        check (false, where + " the tick '" + label.text + "' is not ASCII");
                        break;
                    }
            }

            // The curtain is the EQ page's and nobody else's, and on that page
            // it is inside the plot **at the top of IN HI-CUT's travel**,
            // which is the setting that used to clip.
            const auto curtain = screen.inputCutRegion();

            if (p == R::Page::eq)
            {
                check (! curtain.isEmpty() && plot.contains (curtain),
                       where + " IN HI-CUT's curtain is " + curtain.toString()
                             + ", which is not inside the plot " + plot.toString());
                check (curtain.getWidth() >= R::LingerScreen::kCurtainEdge,
                       where + " IN HI-CUT's curtain is too narrow to draw its own edge");
            }
            else
            {
                check (curtain.isEmpty(),
                       where + " draws IN HI-CUT's curtain on a page that has no EQ on it");
            }

            // A hard-panned tap draws inside the box at both ends of the
            // bearing, and dead centre is the axis itself.
            check (plot.contains (juce::Point<float> (plot.getCentreX(), screen.panY (-1.0f)))
                     && plot.contains (juce::Point<float> (plot.getCentreX(), screen.panY (1.0f))),
                   where + " a hard-panned tap draws outside the plot: L at "
                         + juce::String (screen.panY (-1.0f), 1) + ", R at "
                         + juce::String (screen.panY (1.0f), 1) + ", plot " + plot.toString());
            check (screen.panY (-1.0f) < screen.panY (0.0f)
                     && screen.panY (0.0f) < screen.panY (1.0f),
                   where + " left should draw above the centre axis and right below it");
            checkNear (screen.panY (0.0f), (double) plot.getCentreY(), 0.5,
                       where + " a tap panned dead centre should sit on the centre axis");
        }
    }

    //== The page is UI state, and an unknown value is refused ================
    //
    // `ui.page=...`, the hook BMO Opto's meter mode and BMO DEQ's band use.
    // **Refused rather than defaulted**, and DEQ's comment is the argument: a
    // render labelled TONE that shows EARLY is worse than no render, and
    // nothing downstream could tell the two apart. `tools/snapshot` treats a
    // false here as fatal for exactly that reason.
    {
        check (panel.setUiState ("page", "tail"), who + " should take ui.page=tail");
        check (reverbPanel->getPage() == R::Page::tail, who + " ui.page=tail did not turn the page");

        check (panel.setUiState ("page", "EQ"), who + " should take ui.page=EQ, case and all");
        check (reverbPanel->getPage() == R::Page::eq, who + " ui.page=EQ did not turn the page");

        // **"tone" is refused like any other unknown value, and it is in this
        // list rather than accepted as a synonym.** It was the third page's
        // key until 2026-09-21. A render script that still passes it should
        // stop with an error rather than quietly produce an EARLY page
        // labelled TONE, which is the same argument the refusal itself rests
        // on -- and accepting it would hide the scripts that need updating.
        for (const auto* bad : { "tone", "middle", "1", "", "earl", "early tail", " early" })
            check (! panel.setUiState ("page", bad),
                   who + " should refuse ui.page=" + bad + " rather than falling back");

        check (reverbPanel->getPage() == R::Page::eq,
               who + " a refused page must leave the panel on the one it was showing");

        for (const auto* key : { "band", "meter", "view", "" })
            check (! panel.setUiState (key, "early"),
                   who + " should refuse the key ui." + key + ", which is not its state");
    }

    //== The screen's own arithmetic ==========================================
    //
    // **This is the sketch/DSP drift assertion** that 11 section 5 asks for by
    // name, and it is the whole reason `dsp/TapTables.h` is JUCE-free: the
    // panel and the engine read one table, so the picture cannot quietly stop
    // describing the sound.
    {
        reverbPanel->setPage (R::Page::early);

        const auto& screen = reverbPanel->getScreen();

        params.setReal (R::Index::size, R::roomDefaults::kSizeM);
        params.setReal (R::Index::predelay, 0.0f);

        checkNear (screen.firstTapTimeMs(),
                   (double) R::tapTimeMsAt (R::kReferenceTaps[0], R::roomDefaults::kSizeM), 1.0e-4,
                   who + " the screen's first tap is the table's first tap");
        checkNear (screen.lastTapTimeMs(),
                   (double) R::erSpanMsAt (R::roomDefaults::kSizeM), 1.0e-4,
                   who + " the screen's last tap is the table's ER span");

        // **The EARLY axis spans the real ER window**, which is the change the
        // stems were drawn for: the last tap has to land inside the box with a
        // little air after it rather than on the border or halfway across.
        check (screen.erWindowMs() > screen.lastTapTimeMs(),
               who + " the last tap must fall inside the EARLY window, not on its edge");
        check (screen.erWindowMs() < screen.lastTapTimeMs() * 1.5f,
               who + " the EARLY window is " + juce::String (screen.erWindowMs(), 1)
                   + " ms for a " + juce::String (screen.lastTapTimeMs(), 1)
                   + " ms cluster, which leaves most of the box empty");

        // SIZE scales the picture and the window with it, which is the Size
        // law made visible.
        params.setReal (R::Index::size, R::roomDefaults::kSizeM * 2.0f);
        checkNear (screen.firstTapTimeMs(),
                   (double) R::tapTimeMsAt (R::kReferenceTaps[0], R::roomDefaults::kSizeM) * 2.0, 1.0e-3,
                   who + " twice the size is twice the first tap's time");
        check (screen.erWindowMs() > screen.lastTapTimeMs(),
               who + " the EARLY window follows SIZE");
        params.setReal (R::Index::size, R::roomDefaults::kSizeM);

        // **Pre-delay is tail-only, at every setting, permanently.** It used
        // to depend on LINK ER, and LINK ER was cut on 2026-09-21: off -- ER
        // with dry -- is the reference behaviour and is now `kPreLinkFixed`.
        // So there is no longer a case where the taps move with PRE-DELAY, and
        // the assertion that used to prove the other branch is replaced by one
        // that proves there is no other branch: the whole travel of PRE-DELAY
        // leaves the cluster where it was.
        const auto restingFirst = screen.firstTapTimeMs();
        const auto restingLast  = screen.lastTapTimeMs();

        for (const auto pre : { 12.0f, 120.0f, 250.0f })
        {
            params.setReal (R::Index::predelay, pre);

            checkNear (screen.firstTapTimeMs(), (double) restingFirst, 1.0e-4,
                       who + " pre-delay " + juce::String (pre, 0)
                           + " must not move the ER -- LINK ER is gone and off is fixed");
            checkNear (screen.lastTapTimeMs(), (double) restingLast, 1.0e-4,
                       who + " pre-delay must not move the last tap either");
            checkNear (screen.tailStartMs(), (double) pre, 1.0e-4,
                       who + " the tail starts at the pre-delay");
        }

        params.setReal (R::Index::predelay, 0.0f);

        // The tail's reach follows DECAY and the *largest* damping multiplier,
        // never a smaller one: a dark tail is not a short one.
        params.setReal (R::Index::decay, 2.0f);
        params.setReal (R::Index::damplo, 1.5f);
        params.setReal (R::Index::damphi, 0.4f);
        checkNear (screen.tailEndSeconds(), 3.0, 1.0e-3,
                   who + " the drawn tail reaches decay x the slowest multiplier");

        params.setReal (R::Index::damplo, 0.5f);
        checkNear (screen.tailEndSeconds(), 2.0, 1.0e-3,
                   who + " both multipliers under unity leaves the mid decay as the reach");

        // **The 21 core taps never switch off.** That is what keeps the
        // renormalising denominator bounded away from zero and so what makes
        // the density sweep continuous and click-free (10 section 3) -- so the
        // picture must not lose stems at the bottom of the knob.
        params.setReal (R::Index::erdensity, 0.0f);
        checkEquals (screen.activeTapCount(), R::kNumReferenceTaps,
                     who + " the core taps are all on at DENSITY zero");

        params.setReal (R::Index::erdensity, 100.0f);
        checkEquals (screen.activeTapCount(), 48,
                     who + " DENSITY at the top reaches the 48-tap master sequence");

        // And the reading under the screen is of the picture above it rather
        // than of something near it: 48 taps on the knob is 48 taps in print.
        check (reverbPanel->getScreen().readout().startsWith ("48 TAPS"),
               who + " the EARLY reading should lead with the tap count, reads '"
                   + reverbPanel->getScreen().readout() + "'");

        // The TAIL page's window is the one the class comment argues for,
        // which is what lets a 20 s tail and a 7 ms bloom share a picture, and
        // **its right-hand end follows the tail** as of 2026-09-22.
        checkNear (R::LingerScreen::kMinMs, 1.0, 1.0e-6, who + " the TAIL axis starts at 1 ms");
        checkNear (R::LingerScreen::kMaxSeconds, bmo::kMaxTailSeconds, 1.0e-6,
                   who + " the TAIL axis is clamped where the reported tail is clamped");

        //-- The axis follows the tail, and the box stops being 40 % empty ----
        //
        // **The defect this replaced**: a fixed 1 ms - 30 s axis put the
        // 1.8 s default tail 60 % of the way across and ruled a flat line over
        // the rest, and 30 s is the clamp ceiling rather than a setting
        // anybody uses. What is asserted is the relation rather than a number,
        // because the window is a function of the tail: the curve ends inside
        // the axis at every setting, and not far inside it.
        {
            for (const auto seconds : { 0.1f, 0.5f, 1.8f, 6.0f, 20.0f })
            {
                params.setReal (R::Index::decay, seconds);

                const auto end    = screen.tailEndSeconds();
                const auto window = screen.tailWindowSeconds();

                check (window > end,
                       who + " a " + juce::String (seconds, 2) + " s decay ends at "
                           + juce::String (end, 3) + " s and the axis stops at "
                           + juce::String (window, 3) + " s -- the curve runs off the box");

                // The air after the curve is taken in width, so on a log axis
                // it is a fraction of the decades and not of the seconds.
                // Three times the tail is the loosest this may ever be.
                check (window < end * 3.0f,
                       who + " the TAIL axis runs to " + juce::String (window, 2)
                           + " s for a " + juce::String (end, 2)
                           + " s tail, which is the dead box this was meant to fix");

                check (window <= R::LingerScreen::kMaxSeconds,
                       who + " the TAIL axis must never draw past the tail clamp");
            }

            // Longer decay, longer window: the axis is a scale that moves, not
            // a second control that sticks.
            params.setReal (R::Index::decay, 1.0f);
            const auto shortWindow = screen.tailWindowSeconds();

            params.setReal (R::Index::decay, 10.0f);
            check (screen.tailWindowSeconds() > shortWindow * 2.0f,
                   who + " the TAIL axis does not follow DECAY");

            // And at the top of both controls it stops at the clamp rather
            // than drawing a tail longer than the one the host is told about.
            params.setReal (R::Index::decay, 20.0f);
            params.setReal (R::Index::damplo, 2.0f);
            checkNear (screen.tailWindowSeconds(), (double) R::LingerScreen::kMaxSeconds, 1.0e-3,
                       who + " a 40 s tail should draw against the 30 s clamp");

            params.setReal (R::Index::damplo, R::specs()[(size_t) R::Index::damplo].def);
            params.setReal (R::Index::decay, R::specs()[(size_t) R::Index::decay].def);
        }

        // Put it back, so the TONE block below reads a panel at its defaults.
        for (const auto i : { R::Index::decay, R::Index::damplo, R::Index::damphi,
                              R::Index::erdensity })
            params.setReal (i, R::specs()[(size_t) i].def);
    }

    //== The bloom follows TYPE, with no knob anywhere =========================
    //
    // **ATTACK has no parameter since the 2026-09-21 control-set trim**, so
    // the only thing that moves the TAIL page's bloom is a type change -- and
    // the only place the number appears at all is the readout line under the
    // picture, because the knob and its value string both went. If the panel
    // had kept reading a parameter that no longer exists, or had frozen on
    // Room's constant, every other check in this file would still pass.
    //
    // Absolute figures against `TypeConstants::attack`, not "it changed": the
    // printed number is the constant times the 1.2 ms-per-cent ramp, rounded.
    {
        reverbPanel->setPage (R::Page::tail);

        const auto onsetFor = [&] (int detent)
        {
            params.setReal (R::Index::type, (float) detent);
            return reverbPanel->getScreen().readout();
        };

        for (const auto detent : { (int) R::room, (int) R::plate, (int) R::cavern,
                                   (int) R::ambience })
        {
            // **ONSET and not BLOOM**, owner-approved 2026-09-21. BMO
            // Dimension already ships a control captioned BLOOM -- Gerzon's
            // bass shuffler, `dim::kShuffle`, nothing to do with a reverb's
            // tail -- and two modules in one rack showing one word for two
            // unrelated things is the collision this avoids. ONSET is also
            // what 10-dsp-spec.md calls the behaviour. Pinned here so a tidy
            // back to BLOOM fails rather than passing quietly.
            const auto expected = "ONSET " + juce::String (juce::roundToInt (
                                      R::constantsFor (detent).attack * 1.2f)) + " MS";

            check (onsetFor (detent).contains (expected),
                   who + " on " + R::kTypeNames[detent] + " the TAIL reading should carry '"
                       + expected + "', reads '" + onsetFor (detent) + "'");

            check (! onsetFor (detent).contains ("BLOOM"),
                   who + " the TAIL reading still says BLOOM, which is BMO Dimension's word");
        }

        // Non-vacuous: Plate's tail is immediate and Cavern's is not, so the
        // two readings cannot be the same string.
        check (onsetFor (R::plate) != onsetFor (R::cavern),
               who + " Plate and Cavern print the same onset, so the readout is not"
                     " following the type");

        params.setReal (R::Index::type, (float) R::room);
        reverbPanel->setPage (R::Page::early);
    }

    //== EQ: three designed nodes, plus the input cut, in series ==============
    //
    // Absolute figures rather than "the curve moved", which is the house rule
    // from tests/dsp/OptoDspTests.cpp -- a relative test there passed for a
    // whole release while both of the things it compared were broken.
    //
    // **And the figures are now the engine's own.** The three Reverb EQ nodes
    // are `dsp::designMatched`, so at their defaults they are that function's
    // exact unity case and contribute exactly nothing. The hand-rolled
    // first-order sketch this replaced could only ever be checked against
    // itself. The one approximation left is IN HI-CUT's single pole, which is
    // the screen's own arithmetic and marked as such.
    //
    // What is asserted *here* rather than in tests/dsp is the wiring: that the
    // panel's screen reads the parameters a host is holding and hands them to
    // the same design the engine uses. The filters themselves are
    // tests/dsp/ReverbDspTests.cpp's, JUCE-free.
    {
        reverbPanel->setPage (R::Page::eq);

        const auto& screen = reverbPanel->getScreen();

        const auto resetEq = [&]
        {
            for (const auto i : { R::Index::eqfilter,
                                  R::Index::eqlofreq, R::Index::eqlo, R::Index::eqloq,
                                  R::Index::eqmidfreq, R::Index::eqmid, R::Index::eqmidq,
                                  R::Index::eqhifreq, R::Index::eqhi, R::Index::eqhiq,
                                  R::Index::inhicut })
                params.setReal (i, R::specs()[(size_t) i].def);
        };

        resetEq();

        // **Flat at the defaults, and flat is the input cut's number alone.**
        // Every node is a unity biquad, so the whole reading at 1 kHz is
        // IN HI-CUT's one pole at 20 kHz: -10*log10(1 + (1000/20000)^2), which
        // is -0.01086 dB. Not "roughly zero" -- the exact figure, because an
        // EQ that had quietly acquired half a dB somewhere would still read as
        // roughly zero.
        checkNear (screen.responseDbAt (1000.0f), -0.010857, 1.0e-4,
                   who + " a flat EQ page draws a flat curve");

        for (const auto hz : { 30.0f, 200.0f, 1000.0f, 1600.0f, 8000.0f })
            for (const auto n : { R::EqNode::low, R::EqNode::mid, R::EqNode::high })
                checkNear (screen.nodeDbAt (n, hz), 0.0, 1.0e-9,
                           who + " every EQ node is exactly flat at its default");

        // The one-pole input cut is -3.01 dB at its own corner, which is what
        // makes it a corner.
        params.setReal (R::Index::inhicut, 2000.0f);
        checkNear (screen.responseDbAt (2000.0f), -3.0103, 1.0e-3,
                   who + " the input high-cut is 3 dB down at its corner");
        resetEq();

        //-- Each node is the control it says it is, and only that control ----
        //
        // Node by node, on the node's own contribution rather than on the sum,
        // so a wire that read node 2's gain onto node 3 fails here rather than
        // in a listening pass.
        {
            params.setReal (R::Index::eqlo, 6.0f);
            checkNear (screen.nodeDbAt (R::EqNode::low, 20.0f), 6.0, 0.25,
                       who + " EQ LOW's gain reaches node 1 below its corner");
            checkNear (screen.nodeDbAt (R::EqNode::mid, 20.0f), 0.0, 1.0e-9,
                       who + " EQ LOW must not move node 2");
            resetEq();

            params.setReal (R::Index::eqmidfreq, 1000.0f);
            params.setReal (R::Index::eqmid, 9.0f);
            params.setReal (R::Index::eqmidq, 4.0f);
            checkNear (screen.nodeDbAt (R::EqNode::mid, 1000.0f), 9.0, 0.05,
                       who + " EQ MID peaks at its own centre at its own gain");
            checkNear (screen.nodeDbAt (R::EqNode::low, 1000.0f), 0.0, 1.0e-9,
                       who + " EQ MID must not move node 1");
            resetEq();

            params.setReal (R::Index::eqhi, -6.0f);
            checkNear (screen.nodeDbAt (R::EqNode::high, 19000.0f), -6.0, 0.35,
                       who + " EQ HIGH's gain reaches node 3 above its corner");
            resetEq();

            // Q is a control and not decoration: a wider bell at the same gain
            // and centre reaches further out. Absolutes at both settings, and
            // the comparison on top of them.
            params.setReal (R::Index::eqmidfreq, 1000.0f);
            params.setReal (R::Index::eqmid, 12.0f);

            params.setReal (R::Index::eqmidq, 8.0f);
            const auto narrow = screen.nodeDbAt (R::EqNode::mid, 2000.0f);

            params.setReal (R::Index::eqmidq, 0.5f);
            const auto wide = screen.nodeDbAt (R::EqNode::mid, 2000.0f);

            checkNear (screen.nodeDbAt (R::EqNode::mid, 1000.0f), 12.0, 0.05,
                       who + " a bell is at its gain at its centre whatever its Q");
            check (wide > narrow + 3.0f,
                   who + " EQ MID Q does not widen the bell -- an octave out reads "
                       + juce::String (wide, 2) + " dB wide and " + juce::String (narrow, 2)
                       + " dB narrow");
            resetEq();
        }

        //-- FILTER: nodes 1 and 3 become cuts, node 2 does not ---------------
        //
        // **The claim the mode makes, as three separate assertions.** "The
        // curve changed" is also true of a change that broke the bell, so node
        // 2 is checked for exact equality across the mode and the outer two
        // for the sign of what they do.
        {
            params.setReal (R::Index::eqlofreq, 200.0f);
            params.setReal (R::Index::eqhifreq, 1600.0f);
            params.setReal (R::Index::eqmidfreq, 900.0f);
            params.setReal (R::Index::eqmid, -5.0f);
            params.setReal (R::Index::eqmidq, 2.0f);
            params.setReal (R::Index::eqlo, 6.0f);
            params.setReal (R::Index::eqhi, 6.0f);

            check (! screen.isFilterMode(), who + " the screen thinks FILTER is on at the default");

            const auto shelfLow  = screen.nodeDbAt (R::EqNode::low, 20.0f);
            const auto shelfHigh = screen.nodeDbAt (R::EqNode::high, 16000.0f);

            float midAsShelf[3] {};
            const float probes[3] { 300.0f, 900.0f, 4000.0f };

            for (int i = 0; i < 3; ++i)
                midAsShelf[i] = screen.nodeDbAt (R::EqNode::mid, probes[i]);

            // **Bandpass, which is index 3.** FILTER became a four-position
            // choice on 2026-09-22 and 1 is Lo Cut now; the block below is the
            // both-cuts case, so it names the position it means.
            params.setReal (R::Index::eqfilter, (float) R::eqFilterBandpass);

            check (screen.isFilterMode(), who + " the screen did not take FILTER");
            check (screen.filterMode() == R::EqFilter::bandpass,
                   who + " the screen took FILTER as some other position than Bandpass");

            // Node 2, bit for bit. A bell is a bell in all four positions.
            for (int i = 0; i < 3; ++i)
                check (screen.nodeDbAt (R::EqNode::mid, probes[i]) == midAsShelf[i],
                       who + " FILTER moved node 2 at " + juce::String (probes[i], 0)
                           + " Hz, and a bell is a bell in both modes");

            // Nodes 1 and 3: a boost became a removal, which is what a cut is.
            check (shelfLow > 5.0f && screen.nodeDbAt (R::EqNode::low, 20.0f) < -18.0f,
                   who + " FILTER should turn node 1 from a " + juce::String (shelfLow, 1)
                       + " dB lift at 20 Hz into a cut, reads "
                       + juce::String (screen.nodeDbAt (R::EqNode::low, 20.0f), 1) + " dB");
            check (shelfHigh > 5.0f && screen.nodeDbAt (R::EqNode::high, 16000.0f) < -18.0f,
                   who + " FILTER should turn node 3 from a " + juce::String (shelfHigh, 1)
                       + " dB lift at 16 kHz into a cut, reads "
                       + juce::String (screen.nodeDbAt (R::EqNode::high, 16000.0f), 1) + " dB");

            // **The GAIN knobs grey out and the parameters keep their values.**
            // Both halves, because a mode that reset a knob to make the
            // greying "true" would also pass a check on the greying alone.
            // **Read through `knobFace`, not off the PlainKnob.**
            // `setKnobEnabled` disables the rotary *inside* the component --
            // that is what the look and feel draws dimmed -- while the wrapper
            // stays enabled so its caption still lays out. A check on the
            // wrapper would pass whatever the knob was actually doing, which
            // is the MAKEUP-shaped blind spot one class over.
            const auto live = [&] (const char* caption) -> int
            {
                auto* found = dynamic_cast<bmo::ui::PlainKnob*> (findNamed (panel, caption));

                if (found == nullptr)
                {
                    check (false, who + " the EQ page has no " + caption + " knob");
                    return -1;
                }

                const auto* face = knobFace (*found);

                if (face == nullptr)
                {
                    check (false, juce::String (who) + " " + caption + " has no rotary under it");
                    return -1;
                }

                return face->isEnabled() ? 1 : 0;
            };

            check (live ("EQ LOW") == 0 && live ("EQ HIGH") == 0,
                   who + " Bandpass should grey out both shelf GAIN knobs -- a cut has no gain");

            // FREQ and Q carry over into filter mode, so they stay live; so
            // does the bell's gain, which FILTER does not touch.
            check (live ("EQ LOW FREQ") == 1 && live ("EQ LOW Q") == 1
                     && live ("EQ HIGH FREQ") == 1 && live ("EQ HIGH Q") == 1
                     && live ("EQ MID") == 1,
                   who + " FILTER should leave FREQ, Q and the bell's GAIN alone");

            checkNear (params.getReal (R::Index::eqlo), 6.0, 1.0e-4,
                       who + " FILTER must not write the shelf gain it is ignoring");

            // **One knob at a time**, which is what the four positions buy the
            // panel: the greying follows `eqNodeHasGain` per node, so Lo Cut
            // takes EQ LOW and leaves EQ HIGH live, and Hi Cut the other way
            // about. A panel that had kept the bool's "either cut greys both"
            // would pass every check above and fail these two.
            params.setReal (R::Index::eqfilter, (float) R::eqFilterLoCut);

            check (live ("EQ LOW") == 0 && live ("EQ HIGH") == 1,
                   who + " Lo Cut should grey EQ LOW alone -- node 3 is still a shelf");
            check (screen.readout().contains ("LO CUT") && screen.readout().contains ("HIGH "),
                   who + " the Lo Cut reading should say LO CUT and HIGH, reads '"
                       + screen.readout() + "'");

            params.setReal (R::Index::eqfilter, (float) R::eqFilterHiCut);

            check (live ("EQ LOW") == 1 && live ("EQ HIGH") == 0,
                   who + " Hi Cut should grey EQ HIGH alone -- node 1 is still a shelf");
            check (screen.readout().contains ("LOW ") && screen.readout().contains ("HI CUT"),
                   who + " the Hi Cut reading should say LOW and HI CUT, reads '"
                       + screen.readout() + "'");

            params.setReal (R::Index::eqfilter, (float) R::eqFilterOff);

            check (live ("EQ LOW") == 1 && live ("EQ HIGH") == 1,
                   who + " switching FILTER off should give the shelf GAIN knobs back");

            checkNear (screen.nodeDbAt (R::EqNode::low, 20.0f), (double) shelfLow, 1.0e-4,
                       who + " switching FILTER off should restore the shelf exactly");

            resetEq();
        }

        //-- Three marked nodes and a curtain, for the four controls ----------
        //
        // `nodeFrequencies` still reports four corners and the fourth is still
        // IN HI-CUT's -- what changed on 2026-09-22 is how the picture marks
        // it. It was an open circle among three filled ones, which read as a
        // fourth node of the same EQ and sat half outside the frame at its own
        // default of 20 kHz; it is `inputCutRegion`'s curtain now, and the
        // per-page walk above is what checks it is inside the box.
        {
            params.setReal (R::Index::eqlofreq, 120.0f);
            params.setReal (R::Index::eqmidfreq, 1500.0f);
            params.setReal (R::Index::eqhifreq, 1800.0f);
            params.setReal (R::Index::inhicut, 9000.0f);

            const auto nodes = screen.nodeFrequencies();

            check (nodes.size() == 4,
                   who + " the EQ page should mark four corners: three EQ nodes and the"
                         " input cut's curtain");
            checkNear (nodes[0], 120.0, 0.5, who + " node 0 is EQ LOW's corner");
            checkNear (nodes[1], 1500.0, 0.5, who + " node 1 is EQ MID's centre");
            checkNear (nodes[2], 1800.0, 0.5, who + " node 2 is EQ HIGH's corner");
            checkNear (nodes[3], 9000.0, 0.5, who + " node 3 is IN HI-CUT's corner");

            // The reading is of the three EQ nodes, and **it names the mode**:
            // LOW / HIGH as shelves, LO CUT / HI CUT as filters. That is the
            // third of the three ways the picture says it is drawing cuts, and
            // the only one a test can read without rendering.
            const auto shelfLine = screen.readout();

            check (shelfLine.contains ("LOW ") && shelfLine.contains ("HIGH ")
                     && shelfLine.contains ("MID 1.50 KHZ"),
                   who + " the EQ reading should carry the three node corners, reads '"
                       + shelfLine + "'");
            check (! shelfLine.contains ("CUT"),
                   who + " the EQ reading says CUT with FILTER off, reads '" + shelfLine + "'");

            params.setReal (R::Index::eqfilter, (float) R::eqFilterBandpass);
            const auto cutLine = screen.readout();

            check (cutLine.contains ("LO CUT") && cutLine.contains ("HI CUT"),
                   who + " in Bandpass the EQ reading should say LO CUT and HI CUT, reads '"
                       + cutLine + "'");
            check (cutLine.contains ("MID 1.50 KHZ"),
                   who + " FILTER must not change what the reading says about the bell");
            check (cutLine != shelfLine,
                   who + " the EQ reading is the same with FILTER on and off");

            resetEq();
        }

        reverbPanel->setPage (R::Page::early);
    }
}

//== Driving it ================================================================

struct Product
{
    const char* who;
    std::unique_ptr<juce::AudioProcessor> (*create)();
};

/** Runs `body` against the panel of a single-module product. */
template <typename Fn>
void withPanel (const Product& product, Fn&& body)
{
    auto processor = product.create();
    processor->prepareToPlay (48000.0, 512);

    std::unique_ptr<juce::AudioProcessorEditor> editor (processor->createEditorAndMakeActive());

    if (editor == nullptr)
    {
        check (false, juce::String (product.who) + " has no editor");
        return;
    }

    std::vector<bmo::ui::ModulePanel*> panels;
    collectPanels (*editor, panels);

    if (panels.size() != 1)
        check (false, juce::String (product.who) + " should have exactly one panel, has "
                          + juce::String ((int) panels.size()));
    else
        body (*panels.front());

    // The editor goes before its processor.
    processor->editorBeingDeleted (editor.get());
    editor.reset();
}

} // namespace

//== The rack ==================================================================

/** The rack is why the shared rows matter at all.

    Each panel being internally correct is not the same as a rack reading as
    one surface: put one slot a few pixels lower and every shared rule
    misaligns while every per-panel assertion above still passes. So this
    asserts the two things that turn panel-local rows into rack-wide
    alignment -- that the slots sit on one baseline, and that the rules which
    are supposed to line up actually land on the same absolute row.

    testing-notes/ui-editor-handoff.md calls these hand-matched alignments
    load-bearing for how a rack reads, and until now nothing watched them. */
void checkRack (juce::Component& editor)
{
    std::vector<bmo::ui::ModulePanel*> panels;
    collectPanels (editor, panels);

    if (panels.size() != 4)
    {
        check (false, "rack should have four panels, has " + juce::String ((int) panels.size()));
        return;
    }

    // One baseline. Every slot's panel is placed at the same y and the same
    // height, which is what makes a panel-local row a rack-wide row.
    const auto top = panels.front()->getY();

    for (auto* panel : panels)
    {
        checkEquals (panel->getY(), top, "rack slot top");
        checkEquals (panel->getHeight(), bmo::ui::ModulePanel::kContentHeight, "rack slot height");
    }

    // The panels tile without a gap or an overlap, so the plate reads as one
    // surface rather than four cards.
    auto ordered = panels;
    std::sort (ordered.begin(), ordered.end(),
               [] (auto* a, auto* b) { return a->getX() < b->getX(); });

    for (size_t i = 1; i < ordered.size(); ++i)
        checkEquals (ordered[i]->getX(), ordered[i - 1]->getRight(),
                     "rack slot " + juce::String ((int) i) + " should start where the one before ends");

    // The shared rules, as absolute rack rows rather than as "the same as each
    // other" -- comparing them to one another would pass on a rack that put
    // every slot equally wrong, which is the trap tests/dsp/OptoDspTests.cpp
    // was written to avoid.
    //
    // A slot's panel starts under the product header and the slot bar:
    // 28 + 24 = 52. So the output line is 52 + 566 and the input line 52 + 90.
    constexpr int kSlotTop = 52;

    checkEquals (top, kSlotTop, "rack slot top, absolutely");

    int withOutputRule = 0, withInputRule = 0;

    for (auto* panel : ordered)
    {
        const auto rules = ruleCentres (*panel);

        for (auto centre : rules)
        {
            if (centre == kOutputRuleCentre)
            {
                ++withOutputRule;
                checkEquals (panel->getY() + centre, kSlotTop + kOutputRuleCentre,
                             "rack output rule row");
            }
            else if (centre == kInputRuleCentre)
            {
                ++withInputRule;
                checkEquals (panel->getY() + centre, kSlotTop + kInputRuleCentre,
                             "rack input rule row");
            }
        }
    }

    // util, eq and sat all put a rule on the output line; opto takes neither
    // section and reserves nothing. eq and sat take the input section, util
    // does not. If a module changes its mind about that, this is where it
    // shows up rather than in a screenshot.
    checkEquals (withOutputRule, 3, "rack panels sharing the output line");
    checkEquals (withInputRule,  2, "rack panels sharing the input line");
}

/** Prints a panel's controls and rules. `ui_layout_tests --dump` is how you
    find out what a panel actually does before writing a number down about it,
    rather than deriving one from the constants and asserting the derivation.

    **It prints each caption's margin as well as its box**, which is the number
    `checkCaptionsFit` only reports when it has already gone negative. A
    caption clearing its box by half a pixel passes the suite and is one type
    size away from being the next MAKEUP, and there was no way to find that out
    short of adding a temporary print -- which is how the last two panels were
    sized. Negative is the overflow; positive is the room left. */
void dump (bmo::ui::ModulePanel& panel, const juce::String& who)
{
    std::cout << "== " << who << "  " << panel.getWidth() << "x" << panel.getHeight() << '\n';

    /** The room a control has left, or nothing for one that measures none. */
    const auto margin = [] (juce::Component* c) -> juce::String
    {
        if (auto* k = dynamic_cast<bmo::ui::PlainKnob*> (c))
            return "   margin " + juce::String (-k->captionOverflow(), 1);

        if (auto* b = dynamic_cast<bmo::ui::ChoiceBox*> (c))
            return "   margin " + juce::String (-b->captionOverflow(), 1);

        if (auto* s = dynamic_cast<bmo::ui::SwitchButton*> (c))
            return "   margin " + juce::String (-s->labelOverflow(), 1);

        // BMO Linger's page keys, which set their names inside themselves as
        // of 2026-09-22 and so have the tightest margin on the panel: the room
        // is a chord and not a cell. This is the print the diameter was chosen
        // from, and it is the print to re-read before anyone shrinks it.
        if (auto* k = dynamic_cast<bmo::reverb::PageButton*> (c))
            return "   margin " + juce::String (-k->labelOverflow(), 1)
                     + "   in " + k->labelBox().toString();

        return {};
    };

    for (auto* child : panel.getChildren())
        std::cout << "   " << child->getBounds().toString()
                  << "   y " << child->getY() << ".." << (child->getBottom() - 1)
                  << "   " << child->getName() << margin (child) << '\n';

    for (const auto& r : panel.getRules())
        std::cout << "   rule  y " << r.row.getY() << ".." << (r.row.getBottom() - 1)
                  << "  centre " << r.row.getCentreY()
                  << (r.text.isEmpty() ? "" : "  \"" + r.text + "\"") << '\n';

    // A paged panel's furniture is painted and so has no child to print. These
    // are the rectangles a reviewer asks the size of. The grille was printed
    // here too -- "how big does the grille actually come out?" was the first
    // question asked of this panel, and the answer is why it was cut.
    if (auto* paged = dynamic_cast<bmo::reverb::ReverbPanel*> (&panel))
    {
        const auto box = [] (const char* name, juce::Rectangle<int> r)
        {
            std::cout << "   " << name << "  " << r.toString()
                      << "   " << r.getWidth() << " x " << r.getHeight() << '\n';
        };

        box ("bezel  ", paged->getBezelBox());
        box ("screen ", paged->getScreenBox());
        box ("readout", paged->getReadoutBox());
        box ("cluster", paged->getClusterBox());
    }
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    const auto dumping = argc > 1 && juce::String (argv[1]) == "--dump";

    using namespace bmo::products;

    // Every panel, whatever it opts into: nothing escapes, nothing overlaps,
    // every caption fits.
    const Product all[] = {
        { "eq",   +[] () -> std::unique_ptr<juce::AudioProcessor> { return createEq(); } },
        { "sat",  +[] () -> std::unique_ptr<juce::AudioProcessor> { return createSat(); } },
        { "util", +[] () -> std::unique_ptr<juce::AudioProcessor> { return createUtil(); } },
        { "opto", +[] () -> std::unique_ptr<juce::AudioProcessor> { return createOpto(); } },
        { "dim",  +[] () -> std::unique_ptr<juce::AudioProcessor> { return createDim(); } },
        { "ltvcomp", +[] () -> std::unique_ptr<juce::AudioProcessor> { return createVcomp(); } },

        // BMO DEQ twice, once per width: standalone opens it full, and the
        // compact one is what a rack shows. Both are the same panel laid out
        // from the width it is given, so both are held to everything above.
        { "deq",  +[] () -> std::unique_ptr<juce::AudioProcessor> { return createDeq(); } },
        { "deq compact", +[] () -> std::unique_ptr<juce::AudioProcessor>
                         {
                             auto p = createDeq();
                             p->setExpanded (false);
                             return std::unique_ptr<juce::AudioProcessor> (p.release());
                         } },

        // BMO Linger, **once**. It was here twice while it was expandable, one
        // row per width; the panel is a paged handheld as of 2026-09-21 and
        // has one width, so a second row would be the same 380 px panel under
        // a different name. What replaced it is `checkReverbPanel` walking the
        // panel once per page, which is where the other two thirds of the
        // module now live.
        { "reverb", +[] () -> std::unique_ptr<juce::AudioProcessor> { return createReverb(); } },
    };


    // Addressed by name, not by index. These were all[0], all[1], all[5] and
    // all[6] until BMO Vcomp was added to the list above -- inserting a row
    // anywhere but the end silently re-pointed every one of them, so DEQ's
    // width assertions ran against the new module and failed talking about
    // "deq". The list is one of the shared files every new module is told to
    // edit (modules/AGENTS.md), so it has to survive being edited in the
    // middle.
    const auto named = [&all] (const char* who) -> const Product&
    {
        for (const auto& p : all)
            if (juce::String (p.who) == who)
                return p;

        check (false, juce::String ("no product called ") + who + " in the layout list");
        return all[0];
    };
    if (dumping)
    {
        for (const auto& product : all)
            withPanel (product, [&] (bmo::ui::ModulePanel& panel)
            {
                // A paged panel is dumped once per page: two thirds of BMO
                // Linger is unparented at any moment, so one dump would show a
                // third of it and say nothing about the rest.
                if (auto* paged = dynamic_cast<bmo::reverb::ReverbPanel*> (&panel))
                {
                    for (const auto p : { bmo::reverb::Page::early, bmo::reverb::Page::tail,
                                          bmo::reverb::Page::eq })
                    {
                        paged->setPage (p);
                        dump (panel, juce::String (product.who) + " page "
                                       + paged->getScreen().readout());
                    }

                    return;
                }

                dump (panel, product.who);
            });

        return 0;
    }

    for (const auto& product : all)
        withPanel (product, [&] (bmo::ui::ModulePanel& panel)
        {
            checkWithinPanel     (panel, product.who);
            checkNoOverlap       (panel, product.who);
            checkCaptionsFit     (panel, product.who);
            checkSwitchLabelsFit (panel, product.who);

            checkEquals (panel.getHeight(), bmo::ui::ModulePanel::kContentHeight,
                         juce::String (product.who) + " panel height");
        });

    // BMO EQ and the Saturator take both sections.
    for (const auto& product : { named ("eq"), named ("sat") })
        withPanel (product, [&] (bmo::ui::ModulePanel& panel)
        {
            checkInputSection  (panel, product.who);
            checkOutputRule    (panel, product.who);
            checkOutputSection (panel, product.who);
        });

    // The Saturator's oversampling section, added 2026-09-17: the parameter had
    // been on the panel's schema and nowhere on the panel since 0.2.0.
    withPanel (named ("sat"), [] (bmo::ui::ModulePanel& panel)
    {
        checkOversamplingRow (panel, "sat", 0);
    });

    // BMO CEQ: AUTO took the switch-row place HI-Q left when it went up to the
    // mid band, the oversampling section arrived under LO-CUT, and the band
    // column between the two shared sections is pinned row by row.
    withPanel (named ("eq"), [] (bmo::ui::ModulePanel& panel)
    {
        checkOutputSwitch (panel, "AUTO", "eq");
        checkOversamplingRow (panel, "eq", 1);
        checkEqBandColumn (panel);
    });

    // LTV Comp is the fullest panel in the suite -- two character knobs, three
    // metered bars with printed scales, two switches and a five-knob drawer in
    // 688 px -- so it is the one that runs out of room first, and it has
    // already done so once. See checkTrimKnobHeights.
    withPanel (named ("ltvcomp"), [] (bmo::ui::ModulePanel& panel)
    {
        checkTrimKnobHeights (panel, "ltvcomp",
                              { "ATTACK", "RELEASE", "DETECT", "LOW", "HIGH" });
        checkGateMarkerCarriesItsName (panel);
    });

    // BMO DEQ takes the output section at both widths, so its OUTPUT knob and
    // its DEQ switch sit on the same lines as every other module's in a rack.
    //
    // It is the other content-dense panel: thirteen controls a band, twelve
    // bands, a curve and a meter, and the compact half does it in 320. So it
    // gets checkTrimKnobHeights too -- LTV Comp's silent squash was a layout
    // that no longer fitted and shrank instead of overflowing, and the panel
    // most likely to run out of room next is this one.
    for (const auto& product : { named ("deq"), named ("deq compact") })
        withPanel (product, [&] (bmo::ui::ModulePanel& panel)
        {
            checkTrimKnobHeights (panel, product.who, { "OUTPUT" });
            checkOutputRule    (panel, product.who);
            checkOutputSection (panel, product.who);
            checkOutputSwitch  (panel, "DEQ", product.who);
            checkDeqPanel      (panel, product.who);
        });

    // Its own panel each time: this one moves parameters, and every check above
    // reads a panel that has not been touched.
    for (const auto& product : { named ("deq"), named ("deq compact") })
        withPanel (product, [&] (bmo::ui::ModulePanel& panel) { checkDeqBandToggle (panel, product.who); });

    withPanel (named ("deq"), [] (bmo::ui::ModulePanel& panel) { checkEquals (panel.getWidth(), 600, "deq opens full standalone"); });
    withPanel (named ("deq compact"), [] (bmo::ui::ModulePanel& panel) { checkEquals (panel.getWidth(), 320, "deq compact width"); });

    // BMO Linger: the paged handheld, walked once per page, plus the bezel,
    // the keys, the grille and the screen's own arithmetic -- none of which
    // any other check here can see, because they are painted and have no
    // parameter of their own. Its own panel, because this one moves
    // parameters and every check above reads a panel that has not been
    // touched.
    withPanel (named ("reverb"), [] (bmo::ui::ModulePanel& panel)
    {
        // **One width, and it is the same one in a rack.** The two-width split
        // went with the pages; `ModuleDef::expandedWidth` is 0 and
        // `isExpandable()` is false, which is what stops the standalone header
        // and the slot bar offering a switch with nothing to switch.
        checkEquals (panel.getWidth(), 380, "reverb opens at its one width");
        check (! panel.getContext().def.isExpandable(),
               "a paged module has nothing to expand into");
        checkEquals (panel.getContext().def.expandedWidth, 0,
                     "reverb should declare no second width");

        checkReverbPanel (panel, "reverb");
    });

    // BMO Util reserves the output section and adopts neither half of it. This
    // is the case that proves a reservation is worth anything.
    withPanel (named ("util"), [] (bmo::ui::ModulePanel& panel)
    {
        checkOutputRule             (panel, "util");
        checkReservesWithoutAdopting (panel, "util");
    });

    // The meter scales. No component and no rendering: these are pure tables,
    // and the reduction one is hand-placed, which is why it is worth checking.
    checkScale (bmo::ui::DynamicsMeter::vuScale(), "VU", 3.0f);
    checkScale (bmo::ui::DynamicsMeter::reductionScale(), "reduction", 24.0f);

    // And the rack, which is the reason any of the shared rows exist.
    {
        auto rack = createRack();
        rack->prepareToPlay (48000.0, 512);

        rack->clearChain();

        for (const auto* id : { "util", "eq", "sat", "opto" })
            if (auto* def = rack->findModule (id))
                rack->addModule (*def);
            else
                check (false, juce::String ("rack has no module ") + id);

        std::unique_ptr<juce::AudioProcessorEditor> editor (rack->createEditorAndMakeActive());

        if (editor == nullptr)
            check (false, "rack has no editor");
        else
        {
            checkRack (*editor);

            std::vector<bmo::ui::ModulePanel*> panels;
            collectPanels (*editor, panels);

            for (auto* panel : panels)
            {
                checkWithinPanel     (*panel, "rack slot");
                checkNoOverlap       (*panel, "rack slot");
                checkCaptionsFit     (*panel, "rack slot");
                checkSwitchLabelsFit (*panel, "rack slot");
            }

            rack->editorBeingDeleted (editor.get());
            editor.reset();
        }
    }

    // BMO DEQ in a rack: compact, where a rack opens it, and on the rack-wide
    // output row like everything beside it. Its own block rather than a fifth
    // module in the one above, whose four-panel expectations are written out.
    {
        auto rack = createRack();
        rack->prepareToPlay (48000.0, 512);
        rack->clearChain();
        rack->addModule (*rack->findModule ("util"));
        rack->addModule (*rack->findModule ("deq"));

        std::unique_ptr<juce::AudioProcessorEditor> editor (rack->createEditorAndMakeActive());
        std::vector<bmo::ui::ModulePanel*> panels;
        collectPanels (*editor, panels);

        check (panels.size() == 2, "a util-and-deq rack has two panels");

        for (auto* panel : panels)
        {
            checkWithinPanel     (*panel, "rack deq slot");
            checkNoOverlap       (*panel, "rack deq slot");
            checkCaptionsFit     (*panel, "rack deq slot");
            checkSwitchLabelsFit (*panel, "rack deq slot");
        }

        if (panels.size() == 2)
        {
            auto* deq = panels[0]->getX() > panels[1]->getX() ? panels[0] : panels[1];
            checkEquals (deq->getWidth(), 320, "deq arrives in a rack compact");
            checkOutputRule (*deq, "rack deq");
            checkDeqPanel (*deq, "rack deq");
        }

        rack->editorBeingDeleted (editor.get());
        editor.reset();
    }

    // BMO Linger in a rack, which is now the same panel standalone gives you.
    // Its own block rather than a fifth module in the four-panel one above,
    // and worth having because the *slot* is what changed: a rack used to open
    // it at 300 with an expand switch on its bar, and now opens it at 380 with
    // no switch at all.
    {
        auto rack = createRack();
        rack->prepareToPlay (48000.0, 512);
        rack->clearChain();
        rack->addModule (*rack->findModule ("util"));
        rack->addModule (*rack->findModule ("reverb"));

        std::unique_ptr<juce::AudioProcessorEditor> editor (rack->createEditorAndMakeActive());
        std::vector<bmo::ui::ModulePanel*> panels;
        collectPanels (*editor, panels);

        check (panels.size() == 2, "a util-and-reverb rack has two panels");

        if (panels.size() == 2)
        {
            auto* linger = panels[0]->getX() > panels[1]->getX() ? panels[0] : panels[1];

            checkEquals (linger->getWidth(), 380, "reverb arrives in a rack at its one width");
            check (! rack->isSlotExpanded (1), "there is no expanded view to arrive in");

            checkWithinPanel     (*linger, "rack reverb slot");
            checkNoOverlap       (*linger, "rack reverb slot");
            checkCaptionsFit     (*linger, "rack reverb slot");
            checkSwitchLabelsFit (*linger, "rack reverb slot");

            // And the whole paged walk, on a slot rather than on a product:
            // the parameters underneath are generic `SlotParameter`s here, and
            // the two dropdowns read their names off the module's own spec for
            // exactly that reason.
            checkReverbPanel (*linger, "rack reverb");
        }

        rack->editorBeingDeleted (editor.get());
        editor.reset();
    }

    if (failures == 0)
        std::cout << "All ui layout tests passed.\n";

    return failures == 0 ? 0 : 1;
}
