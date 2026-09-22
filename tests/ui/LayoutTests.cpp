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
#include "products/dwell/Product.h"
#include "modules/dwell/params.h"
#include "products/eq/Product.h"
#include "products/opto/Product.h"
#include "products/vcomp/Product.h"
#include "products/sat/Product.h"
#include "products/util/Product.h"
#include "products/rack/Product.h"

#include "core/ui/ExpandButton.h"
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

//== BMO Dwell =================================================================
//
// **The nine-control face and the three-even-column reveal**, which is the settled
// panel from 2026-09-22. `docs/delay/13` §2 draws an earlier one literally,
// and it was built and rejected, as were two more after it -- "too busy and
// not intuitive", Frosty 2026-09-21. `13` is not rewritten until the rest of
// the module is settled, so this file and that document disagree on purpose.
//
// Absolutes, not comparisons.
//
// **The face is nine rows and a foot** -- 28, 16, 152, 122, 20, 28, 16, 108,
// 40, and the 28 px foot taken off the bottom -- which is 558 px of the 676 px
// the content area is once a 4 px foot margin is reserved. Neither the input
// nor the output section is taken, so the 118 px over is laid on as one 9 px
// unit of air above every row and a **double** unit at each of the three
// section breaks, the odd 10 px going to the first of them. That puts the
// DELAY rule at 77 and the TONE rule at 460.
//
// **Those are the same two numbers the eleven-control face had**, and that is
// the point of the swap that made this face nine. SEND and HOLD came off it --
// "send does nothing if no hold is applied, and hold is a complex control",
// Frosty 2026-09-21 -- and their 40 px band was taken over by FX, which had
// been a 56 px switch sharing the foot with the arrow. Two controls left, one
// moved, and not one of the panel's lines moved with them, so the depth column
// that is cut on those lines did not have to be re-laid.
//
// Four of those row heights carry the **15 pt captions** -- the suite standard
// that ui::PlainKnob defaults to and that every earlier attempt at this panel
// had to give up. A caption row is round(15 * 1.2) + 4 = 22 px rather than the
// 18 that 12 pt cost. Laying the face out in pairs is what makes it
// affordable: a caption gets a 130 px cell instead of the 86 a three-knob row
// would cut, and "FEEDBACK" needs 125 at 15 pt.
//
// **The wide view is three even columns**: 260 + 20 + 260 + 20 + 260, which
// with `kPad` either side is the 840 in modules/dwell/Module.cpp.
//
// **All three strike both of their rules on the same two lines** -- DELAY,
// LOOP and LANE on row 77, then TONE and the two FX rules on row 460 -- and
// those are the assertions that catch a column drifting off the others by a
// few pixels. The depth column and the lane are each laid out in two segments
// cut on the face's TONE line rather than as one run and hoped over, so the
// alignment is exact by construction.
//
// **It was 980, and the lane had its own rules at 300 and 547.** The lane was
// a second delay then -- nine bands including a mirrored voicing block -- and
// nothing it carried would sit on the shared line, so it was given a rhythm
// deliberately *clear* of it, because a rule struck a dozen pixels off a face
// rule reads as a failed alignment rather than as two sections. The voicing
// was cut on 2026-09-22, the five bands left do sit on the line, and the lane
// is a 260 px column like the others.
//
// What is left bare above the depth column is the CHARACTER band, and that is
// deliberate: the trio voices both engines and what sits under it is the main
// delay's own depth. The lane's band up there is not bare -- it carries the
// three gates, above the LANE rule for the same reason CHARACTER sits above
// the DELAY rule.

constexpr int kDwellDelayRule = 77;    ///< and the LOOP and LANE rules beside it
constexpr int kDwellToneRule  = 460;   ///< and both FX rules beside it

/** The face, at either width. The wide view keeps its bands exactly where the
    compact one has them and adds two columns beside them, so every number
    here is the same in both. */
void checkDwellFace (bmo::ui::ModulePanel& panel, const juce::String& who, bool expanded)
{
    // Two rules on the face; the depth column adds LOOP and FX, and the lane
    // adds LANE and its own FX. Six wide, and it was seven while the lane
    // carried a VOICE rule of its own.
    checkEquals ((int) ruleCentres (panel).size(), expanded ? 6 : 2, who + " rule count");
    checkHasRuleAt (panel, kDwellDelayRule, who);
    checkHasRuleAt (panel, kDwellToneRule, who);

    // **Nine controls, and these are they.** The count is the redesign: three
    // attempts carried eighteen down a 280 px strip and all three read as
    // dense, eleven fixed that, and nine is what is left once the lane's two
    // gates went to the lane. LO CUT, HI CUT and the rest are what the panel
    // prints -- a caption is not schema (WORKFLOWS.md's control audit).
    for (const auto* name : { "TIME", "FEEDBACK", "MIX", "LO CUT", "HI CUT",
                              "SYNC", "FX" })
        check (findNamed (panel, name) != nullptr,
               who + " has no " + juce::String (name));

    // The two trios, by a cell each, because a ChoiceRow's buttons are laid out
    // in the row's own coordinates and only the row knows where it sits.
    for (const auto* cell : { "CLEAN", "TAPE", "BUCKET", "STEREO", "PING-PONG", "DUAL" })
        check (findNamed (panel, cell) != nullptr,
               who + " has no " + juce::String (cell) + " cell");

    // TIME and NOTE share one position, so only one of them is ever there.
    // SYNC ships disabled (params.h kSyncIsEnabled), so it is TIME.
    check (findNamed (panel, "NOTE") == nullptr,
           who + " shows NOTE and TIME at once -- they share one position");

    // No control is labelled DWELL. The module is; nothing on it is.
    check (findNamed (panel, "DWELL") == nullptr, who + " has a control labelled DWELL");

    auto* time = findNamed (panel, "TIME");
    auto* sync = findNamed (panel, "SYNC");
    auto* mix  = findNamed (panel, "MIX");
    auto* hiCut = findNamed (panel, "HI CUT");
    auto* fx = findNamed (panel, "FX");

    // The CHARACTER trio is above the DELAY rule; the two cuts are below the
    // TONE rule; FX is below both.
    if (auto* clean = findNamed (panel, "CLEAN"); clean != nullptr)
        if (auto* row = clean->getParentComponent(); row != nullptr)
            check (row->getBottom() <= kDwellDelayRule,
                   who + " the CHARACTER trio is not above the DELAY rule");

    if (time != nullptr && sync != nullptr && mix != nullptr
        && hiCut != nullptr && fx != nullptr)
    {
        check (time->getY() >= kDwellDelayRule, who + " TIME is above the DELAY rule");
        check (time->getBottom() <= mix->getY(), who + " TIME and MIX are out of order");
        check (mix->getBottom() <= kDwellToneRule, who + " MIX runs past the TONE rule");
        check (hiCut->getY() >= kDwellToneRule, who + " HI CUT is above the TONE rule");
        check (fx->getY() >= hiCut->getBottom(), who + " FX is not below the tone pair");

        // **TIME is the hero and it is on the column's centre line.** SYNC
        // sits beside it rather than under it, which is what buys the band;
        // the knob is still dead centre, because its box is the middle of
        // three cells rather than the whole column.
        checkEquals (time->getBounds().getCentreX(), bmo::ui::ModulePanel::kPad + 130,
                     who + " TIME is not centred on its column");
        check (sync->getX() >= time->getRight(), who + " SYNC is not beside TIME");
        check (sync->getBounds().getCentreY() < time->getBounds().getCentreY(),
               who + " SYNC is level with TIME's caption rather than its face");

        // **FX is centred on its column** and has a band of its own now, which
        // is what it bought by SEND and HOLD leaving. The left column's centre
        // is the same number at both widths -- kPad + 260/2 -- because the
        // wide view only ever adds columns to the right of the first.
        checkEquals (fx->getBounds().getCentreX(), bmo::ui::ModulePanel::kPad + 130,
                     who + " FX is not centred on its column");
    }

    // The expand arrow has the foot to itself. It moves no parameter and is
    // the same view affordance as the host bar's own, which sits at an edge
    // throughout the suite -- so it sits at one, under everything else on the
    // face rather than beside the last control on it.
    if (auto* expand = findNamed (panel, "expand"); expand != nullptr && fx != nullptr)
    {
        check (expand->getY() >= fx->getBottom(),
               who + " the expand arrow is not below the FX band");
        check (expand->getRight() >= 280 - bmo::ui::ModulePanel::kPad - 1,
               who + " the expand arrow is not at the right edge of the face's foot");
    }
}

/** The two revealed columns: the main delay's depth, and the lane.

    They are there in the wide view and gone in the compact one -- gone rather
    than hidden, because a control left parented with no bounds passes every
    overlap check and fails no caption check while being invisible.

    **It is a visibility split and nothing else.** Every one of these
    parameters stays live and is read by the DSP whichever width the panel is
    at; there is no gate. Nothing here asserts on the sound, because nothing
    about the sound changes.

    **Every one of the twenty-six is drawn**: ten on the face, six in the depth
    column and ten in the lane. Nothing in this schema is live, automatable and
    on no panel at all, which is what the redesign exists to guarantee. */
void checkDwellRevealed (bmo::ui::ModulePanel& panel, const juce::String& who, bool expanded)
{
    // The depth column, by caption. DUCK's GR bar is named for what it reads.
    const juce::StringArray depth { "DRIVE", "RATE", "DEPTH", "DUCK", "GR",
                                    "AMOUNT (SMEAR)" };

    // The lane, by component name. Its knobs print the *same words* the main
    // delay's print -- TIME, LEVEL -- because renaming them would read as a
    // bin of leftovers, so the lane's components carry a LANE prefix and
    // `findNamed` walks that. See ChoiceRow's namePrefix in
    // modules/dwell/panel/DwellPanel.cpp.
    const juce::StringArray lane { "SEND", "HOLD", "CHOP",
                                   "LANE TAIL", "LANE TIME", "LANE LEVEL",
                                   "LANE FX", "FX LINK", "LANE AMOUNT (SMEAR)" };

    for (const auto& name : { depth, lane })
        for (const auto& control : name)
        {
            const auto* found = findNamed (panel, control);

            if (expanded)
                check (found != nullptr, who + " wide has no " + control);
            else
                check (found == nullptr, who + " compact still carries " + control);
        }

    // **Three cells apiece, and one shape for both**, since Sweep went on
    // 2026-09-22 and three no longer tile as the 2x2 four did. Both stages lay
    // them across a row at 84 px a cell, with AMOUNT on a row of its own
    // spanning the column, because its caption measures 209 px at 15 pt. The
    // lane stacked its three down a 172 px slot beside its AMOUNT while it had
    // a 400 px column and no height to spare; at 260 it is the depth column's
    // shape, which is the truer reading of two engines' worth of one stage.
    // The depth column's cells keep the plain names; the lane's are prefixed,
    // for the same reason its knobs are.
    const auto* fxCell = findNamed (panel, "DIFFUSE");
    const auto* crushCell = findNamed (panel, "CRUSH");
    const auto* laneFxCell = findNamed (panel, "LANE.DIFFUSE");
    const auto* laneCrushCell = findNamed (panel, "LANE.CRUSH");

    // The types that came out. A cell for one of these is a choice list that
    // grew back, which is the schema moving rather than a layout slip. **SWEEP
    // is on this list from 2026-09-22**: it swept VOICE's resonant centre and
    // VOICE was deleted the day before.
    for (const auto* gone : { "OCT UP", "OCT DN", "REVERSE", "SWEEP" })
    {
        check (findNamed (panel, gone) == nullptr,
               who + " still has an " + juce::String (gone) + " cell");
        check (findNamed (panel, juce::String ("LANE.") + gone) == nullptr,
               who + " the lane still has an " + juce::String (gone) + " cell");
    }

    if (expanded)
    {
        check (fxCell != nullptr, who + " wide has no FX type cells");
        check (crushCell != nullptr, who + " wide has no CRUSH cell");
        check (laneFxCell != nullptr, who + " wide has no lane FX type cells");
        check (laneCrushCell != nullptr, who + " wide has no lane CRUSH cell");

        // **The depth column's three are a row**: CRUSH is the third, so it is
        // to the right of DIFFUSE and level with it.
        if (fxCell != nullptr && crushCell != nullptr)
        {
            check (crushCell->getX() > fxCell->getX(),
                   who + " the depth column's FX cells are not laid out across");
            checkEquals (crushCell->getY(), fxCell->getY(),
                         who + " the depth column's FX cells are not on one line");
        }

        // **And so are the lane's**, from 2026-09-22: the same row, the same
        // cell, one column over.
        if (laneFxCell != nullptr && laneCrushCell != nullptr)
        {
            check (laneCrushCell->getX() > laneFxCell->getX(),
                   who + " the lane's FX cells are not laid out across");
            checkEquals (laneCrushCell->getY(), laneFxCell->getY(),
                         who + " the lane's FX cells are not on one line");
            checkEquals (laneCrushCell->getWidth(), laneFxCell->getWidth(),
                         who + " the lane's FX cells are not one width");
        }

        // The two stages are drawn alike, which is worth an assertion rather
        // than a comment: same cell width, same row height, one column apart.
        if (fxCell != nullptr && laneFxCell != nullptr)
        {
            checkEquals (laneFxCell->getWidth(), fxCell->getWidth(),
                         who + " the two FX stages' cells are not the same width");
            checkEquals (laneFxCell->getHeight(), fxCell->getHeight(),
                         who + " the two FX stages' cells are not the same height");
        }

        // **The columns, and which side of the panel each one is on.** The
        // depth column starts where the face ends and the lane starts where
        // the depth column ends, which is what "the module can only grow
        // sideways" means with three of them.
        constexpr int kDepthLeft = 280 - bmo::ui::ModulePanel::kPad;
        constexpr int kLaneLeft  = kDepthLeft + 260 + 20;

        for (const auto& control : depth)
            if (const auto* found = findNamed (panel, control))
            {
                check (found->getX() >= kDepthLeft,
                       who + " " + control + " is not in the column beside the face");
                check (found->getRight() <= kLaneLeft,
                       who + " " + control + " has run into the lane's column");
            }

        for (const auto& control : lane)
            if (const auto* found = findNamed (panel, control))
                check (found->getX() >= kLaneLeft,
                       who + " " + control + " is not in the lane's column");

        if (fxCell != nullptr && fxCell->getParentComponent() != nullptr)
            check (fxCell->getParentComponent()->getX() >= kDepthLeft,
                   who + " the FX cells are not in the depth column");

        if (laneFxCell != nullptr && laneFxCell->getParentComponent() != nullptr)
            check (laneFxCell->getParentComponent()->getX() >= kLaneLeft,
                   who + " the lane's FX cells are not in the lane's column");

        //== The rules, which is where the three columns have to agree ========
        //
        // **All three on both lines**, 77 and 460, which is the whole grid.
        // The lane's own two rules at 300 and 547 went with its voicing on
        // 2026-09-22: five bands sit on the shared line where nine did not,
        // and the lane is cut on that line exactly as the depth column is.
        //
        // The lane's FX rule is inside a 28 px ruled-switch band and the
        // hairline is centred in it, so its segment starts six pixels above
        // the line. Six pixels off is precisely the failure this asserts
        // against, which is why the inset is in the layout rather than hoped
        // for -- see kRuleInset in modules/dwell/panel/DwellPanel.cpp.
        const auto rules = ruleCentres (panel);

        checkEquals ((int) std::count (rules.begin(), rules.end(), kDwellDelayRule),
                     3, who + " the three columns' first rules are not on one line");
        checkEquals ((int) std::count (rules.begin(), rules.end(), kDwellToneRule),
                     3, who + " the three columns' second rules are not on one line");

        //== The lane's tail, which is the control the redesign is about ======
        //
        // **TAIL alone on its row, TIME and LEVEL paired under it.** It is the
        // lane's hero at 116 px and the column is 260, so three across would
        // have cut its cell to 86 -- which is what the 400 px column afforded
        // and this one does not. The pair below it shares a caption line with
        // itself, not with the hero.
        auto* tail = findNamed (panel, "LANE TAIL");
        auto* laneTime = findNamed (panel, "LANE TIME");
        auto* laneLevel = findNamed (panel, "LANE LEVEL");

        if (tail != nullptr && laneTime != nullptr && laneLevel != nullptr)
        {
            checkEquals (laneTime->getBottom(), laneLevel->getBottom(),
                         who + " the lane's TIME and LEVEL do not share a caption line");

            check (laneTime->getY() >= tail->getBottom(),
                   who + " the lane's TIME is not below its TAIL");
            check (laneLevel->getY() >= tail->getBottom(),
                   who + " the lane's LEVEL is not below its TAIL");
            check (laneTime->getRight() <= laneLevel->getX(),
                   who + " the lane's TIME and LEVEL are out of order");

            // The hero is centred on its column, the way the face's TIME is on
            // its own. A lone knob drifting off centre is the failure a single
            // on a row is most likely to have. kLaneLeft is where the *gutter*
            // before the lane starts, so the column's own centre is 20 px of
            // gutter and half a column past it.
            checkEquals (tail->getBounds().getCentreX(), kLaneLeft + 20 + 130,
                         who + " the lane's TAIL is not centred on its column");
        }

        // The gates sit above the LANE rule, the way CHARACTER sits above the
        // DELAY rule. If a later change drops them into the section, this is
        // what should be argued with first.
        for (const auto* gate : { "SEND", "HOLD", "CHOP" })
            if (const auto* found = findNamed (panel, gate))
                check (found->getBottom() <= kDwellDelayRule,
                       who + " the lane's " + juce::String (gate)
                           + " gate is not above the LANE rule");

        //== One link, on the rule of the section it governs ==================
        //
        // FX LINK ties the lane's FX trio, and it is the only link on this
        // panel. A voicing LINK stood on a VOICE rule above it until
        // 2026-09-22 and went with the six parameters it tied; **nothing
        // called LINK may come back without being the lane's FX tie**, which
        // is what the first assertion here is for -- a stray control named
        // LINK would otherwise resolve by child order.
        auto* theFxLink = findNamed (panel, "FX LINK");
        auto* laneFxGate = findNamed (panel, "LANE FX");

        check (findNamed (panel, "LINK") == nullptr,
               who + " still carries a LINK switch -- the voicing link was cut");

        // **The link is the rightmost thing on its rule**, with the gate
        // inside it, so the row reads "this section, this stage, tied to the
        // other one" rather than leaving the eye to work out which switch on a
        // rule is which.
        if (theFxLink != nullptr && laneFxGate != nullptr)
        {
            check (laneFxGate->getRight() <= theFxLink->getX(),
                   who + " the lane's FX gate is outside its link, not inside it");
            checkEquals (theFxLink->getBounds().getCentreY(), laneFxGate->getBounds().getCentreY(),
                         who + " the lane's FX gate and its link are not on one line");
        }
    }
    else
    {
        check (fxCell == nullptr, who + " compact still carries the FX type cells");
        check (crushCell == nullptr, who + " compact still carries the FX type cells");
        check (laneFxCell == nullptr, who + " compact still carries the lane's FX type cells");
        check (laneCrushCell == nullptr, who + " compact still carries the lane's FX type cells");
    }
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
    rather than deriving one from the constants and asserting the derivation. */
void dump (bmo::ui::ModulePanel& panel, const juce::String& who)
{
    std::cout << "== " << who << "  " << panel.getWidth() << "x" << panel.getHeight() << '\n';

    for (auto* child : panel.getChildren())
        std::cout << "   " << child->getBounds().toString()
                  << "   y " << child->getY() << ".." << (child->getBottom() - 1)
                  << "   " << child->getName() << '\n';

    for (const auto& r : panel.getRules())
        std::cout << "   rule  y " << r.row.getY() << ".." << (r.row.getBottom() - 1)
                  << "  centre " << r.row.getCentreY()
                  << (r.text.isEmpty() ? "" : "  \"" + r.text + "\"") << '\n';
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

        // BMO Dwell, the second expandable module, and twice for the same
        // reason: standalone opens the FX column and a rack opens without it.
        { "dwell", +[] () -> std::unique_ptr<juce::AudioProcessor> { return createDwell(); } },
        { "dwell compact", +[] () -> std::unique_ptr<juce::AudioProcessor>
                           {
                               auto p = createDwell();
                               p->setExpanded (false);
                               return std::unique_ptr<juce::AudioProcessor> (p.release());
                           } },
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
            withPanel (product, [&] (bmo::ui::ModulePanel& panel) { dump (panel, product.who); });

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

    // BMO Dwell: the nine-control face at both widths, and the two revealed
    // columns only at the wide one. 840 -- three even 260s with 20 px gutters,
    // for the face, the main delay's depth and the lane. It was 980 while the
    // lane mirrored the main delay and needed 400 for a voicing quad and a
    // stacked FX grid; with the voicing cut its widest row is an
    // `AMOUNT (SMEAR)` caption at 209 px. See modules/dwell/Module.cpp, which
    // also carries the arithmetic for why the reveal cannot be two columns.
    withPanel (named ("dwell"), [] (bmo::ui::ModulePanel& panel)
    {
        checkEquals (panel.getWidth(), 840, "dwell opens wide standalone");
        checkDwellFace     (panel, "dwell", true);
        checkDwellRevealed (panel, "dwell", true);
        check (findNamed (panel, "expand") != nullptr,
               "dwell has no expand arrow on the panel");
    });

    withPanel (named ("dwell compact"), [] (bmo::ui::ModulePanel& panel)
    {
        checkEquals (panel.getWidth(), 280, "dwell compact width");
        checkDwellFace     (panel, "dwell compact", false);
        checkDwellRevealed (panel, "dwell compact", false);
    });

    // **The view is not a parameter.** docs/delay/13 §6a and
    // modules/dwell/AGENTS.md: automation, preset load and session recall move
    // `fx` and must never resize the module, and turning `fx` off must never
    // close the column. Both directions, because both have to hold and only
    // one of them is the obvious one.
    {
        auto proc = createDwell();
        proc->setExpanded (false);
        proc->prepareToPlay (48000.0, 512);

        std::unique_ptr<juce::AudioProcessorEditor> editor (proc->createEditorAndMakeActive());
        std::vector<bmo::ui::ModulePanel*> panels;
        collectPanels (*editor, panels);

        if (panels.size() != 1)
            check (false, "dwell view test has no panel");
        else
        {
            auto& panel = *panels.front();
            auto& params = panel.getContext().params;

            // A host writing the parameter, which is what an automation lane
            // and a preset both are.
            params.setReal (bmo::dwell::Index::fx, 1.0f);
            checkEquals (panel.getWidth(), 280, "dwell stayed compact when fx was automated on");
            check (! proc->isExpanded(), "dwell's view flag moved with a parameter");

            // And the other way: the column stays open with fx off.
            proc->setExpanded (true);
            params.setReal (bmo::dwell::Index::fx, 0.0f);
            check (proc->isExpanded(), "turning fx off closed dwell's FX column");

            // The arrow is the new touch point -- a panel asking its host for
            // the other width through ui::ModuleContext::setExpanded. Nothing
            // else in the suite does it, so nothing else would catch it coming
            // unplugged. Driven through onClick rather than triggerClick,
            // which posts to a message loop this test does not run.
            proc->setExpanded (false);
            editor->resized();

            if (auto* arrow = dynamic_cast<bmo::ui::ExpandButton*> (findNamed (panel, "expand")))
            {
                if (arrow->onClick)
                    arrow->onClick();

                check (proc->isExpanded(), "dwell's arrow did not open the FX column");
                checkEquals (panel.getWidth(), 840, "dwell's width after the arrow opened it");

                // It closes it again, and `fx` never moved either way.
                if (arrow->onClick)
                    arrow->onClick();

                check (! proc->isExpanded(), "dwell's arrow did not close the FX column");
                check (params.getReal (bmo::dwell::Index::fx) < 0.5f,
                       "dwell's arrow moved the fx parameter -- it must touch none");
            }
            else
            {
                check (false, "dwell has no expand arrow to click");
            }
        }

        proc->editorBeingDeleted (editor.get());
        editor.reset();
    }

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

    if (failures == 0)
        std::cout << "All ui layout tests passed.\n";

    return failures == 0 ? 0 : 1;
}
