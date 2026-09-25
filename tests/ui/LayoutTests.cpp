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
#include "core/ui/LevelBars.h"
#include "modules/vcomp/params.h"
#include "products/deesser/Product.h"
#include "modules/deesser/panel/DeesserPanel.h"
#include "modules/deesser/params.h"
#include "products/dim/Product.h"
#include "products/eq/Product.h"
#include "products/fetcomp/Product.h"
#include "modules/fetcomp/params.h"
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

#include "core/ui/LookAndFeel.h"
#include "core/ui/ModulePanel.h"

#include <juce_gui_basics/juce_gui_basics.h>
#include <algorithm>
#include <iostream>
#include <map>
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
    arithmetic -- BMO Defang's band sketch was the first of those and BMO
    Linger's ER/tail display is the second. */
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

/** Every knob drawn as a knob under `root`, with the caption of the nearest
    named control above it -- a PlainKnob's, or a ConcentricBand's for the
    gain inside a band. A selector ring is a ring in both surfaces, so it is
    left out. */
void collectTexturedKnobs (juce::Component& root, std::vector<std::pair<juce::String, bmo::ui::Knob*>>& out)
{
    for (auto* child : root.getChildren())
    {
        if (auto* knob = dynamic_cast<bmo::ui::Knob*> (child);
            knob != nullptr && knob->getStyle() != bmo::ui::Knob::Style::ring)
        {
            juce::String name;
            for (auto* p = child->getParentComponent(); p != nullptr && name.isEmpty(); p = p->getParentComponent())
                name = p->getName();

            out.emplace_back (name, knob);
        }

        collectTexturedKnobs (*child, out);
    }
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

/** BMO FET's switch blocks.

    Three rows of switches over three choice parameters -- the ratio buttons,
    the voicing pair and the oversampling pair -- and none of them toggles: a
    click sets the parameter and the parameter lights the buttons. That loop is
    invisible to a render at Init, which is exactly how the Saturator's
    oversampling row could have been dead for a release without anyone seeing
    it, so it is walked here.

    The geometry is pinned to the suite's own switch size and gap rather than
    to this panel's numbers. This panel is 260 wide, which is what lets its
    IN/GR/OUT row use the full `switchWidth` and drop the 220-px exception
    modules/AGENTS.md records for BMO Opto -- so that row is held to the full
    width here, and a drift back to a narrower switch fails. */
void checkFetcompSwitches (bmo::ui::ModulePanel& panel, const juce::String& who)
{
    auto& params = panel.getContext().params;

    const auto button = [&panel, &who] (const char* name) -> juce::Button*
    {
        auto* b = dynamic_cast<juce::Button*> (findNamed (panel, name));

        if (b == nullptr)
            check (false, who + " has no " + name + " switch");

        return b;
    };

    //== The ratio block: five buttons, one lit, and every one reachable =======
    {
        const char* names[5] { "4:1", "8:1", "12:1", "20:1", "ALL" };
        juce::Button* row[5] {};

        for (int i = 0; i < 5; ++i)
        {
            row[i] = button (names[i]);

            if (row[i] == nullptr)
                return;
        }

        for (int i = 0; i < 5; ++i)
        {
            checkEquals (row[i]->getWidth(),  bmo::ui::Tokens::switchWidth,
                         who + " " + names[i] + " width");
            checkEquals (row[i]->getHeight(), bmo::ui::Tokens::switchHeight,
                         who + " " + names[i] + " height");
        }

        // One column beside INPUT and OUTPUT: the four ratios run down it in
        // order, and ALL sits apart at the foot because it is a different
        // curve rather than a steeper one.
        //
        // This was a 2 x 2 block with ALL underneath until 2026-09-20, when the
        // strip moved into the column beside the stacked drive knobs. What is
        // pinned here is unchanged in substance -- the four are evenly spaced,
        // and the fifth is set further off than they are from each other -- so
        // that the separation cannot quietly be lost to a tidy-up.
        for (int i = 0; i < 4; ++i)
            checkEquals (row[i]->getX(), row[0]->getX(),
                         who + " " + names[i] + " shares the ratio column");

        for (int i = 1; i < 4; ++i)
            checkEquals (row[i]->getY() - row[i - 1]->getBottom(), bmo::ui::Tokens::switchGap,
                         who + " gap above " + names[i]);

        check (row[4]->getY() > row[3]->getBottom(), who + " ALL sits under the four ratios");
        check (row[4]->getY() - row[3]->getBottom() > row[1]->getY() - row[0]->getBottom(),
               who + " ALL is set further apart than the ratios are from each other");

        const auto lit = [&row]
        {
            auto count = 0, which = -1;

            for (int i = 0; i < 5; ++i)
                if (row[i]->getToggleState()) { ++count; which = i; }

            return count == 1 ? which : -1;
        };

        checkEquals (lit(), 0, who + " ratio at Init is 4:1");

        // Every position, including the one the default already is: clicking a
        // lit ratio is a no-op, not a way out of it, because unlike
        // oversampling there is no Off here.
        for (int i = 0; i < 5; ++i)
        {
            if (row[i]->onClick != nullptr)
                row[i]->onClick();

            checkEquals (lit(), i, who + " " + names[i] + " lit alone after a click");
            checkEquals (juce::roundToInt (params.getReal (bmo::fetcomp::Index::ratio)), i,
                         who + " clicking " + names[i] + " sets the ratio parameter");
        }

        params.setReal (bmo::fetcomp::Index::ratio, 0.0f);
        checkEquals (lit(), 0, who + " the parameter lights the ratio buttons, not the click");
    }

    //== The voicing pair: both states named, both reachable ==================
    {
        auto* blue  = button ("BLUE");
        auto* black = button ("BLACK");

        if (blue == nullptr || black == nullptr)
            return;

        // BLUE over BLACK in the switch column, under the ratio strip. The
        // pair was abreast at the foot of the panel until 2026-09-20; what is
        // pinned is that they are a pair -- same column, one switch gap apart,
        // in that order -- rather than where on the panel the pair sits.
        checkEquals (blue->getX(), black->getX(), who + " BLACK shares BLUE's column");
        checkEquals (black->getY() - blue->getBottom(), bmo::ui::Tokens::switchGap,
                     who + " gap between BLUE and BLACK");

        // Black is the default, and it is permanent.
        check (! blue->getToggleState() && black->getToggleState(),
               who + " voicing at Init is Black");

        if (blue->onClick != nullptr)
            blue->onClick();

        check (blue->getToggleState() && ! black->getToggleState(),
               who + " clicking BLUE selects it");
        checkEquals (juce::roundToInt (params.getReal (bmo::fetcomp::Index::voicing)),
                     (int) bmo::fetcomp::blue, who + " BLUE sets the voicing parameter");

        params.setReal (bmo::fetcomp::Index::voicing, (float) bmo::fetcomp::black);
        check (black->getToggleState(), who + " the parameter lights the voicing pair");
    }

    //== Oversampling: Off is the position with no switch =====================
    {
        auto* os2 = button ("2x");
        auto* os4 = button ("4x");

        if (os2 == nullptr || os4 == nullptr)
            return;

        checkEquals (os2->getY(), os4->getY(), who + " 4x sits beside 2x");
        check (! os2->getToggleState() && ! os4->getToggleState(),
               who + " oversampling at Init is Off, so neither switch is lit");

        if (os2->onClick != nullptr) os2->onClick();
        check (os2->getToggleState() && ! os4->getToggleState(), who + " 2x lit alone after a click");

        if (os2->onClick != nullptr) os2->onClick();
        check (! os2->getToggleState() && ! os4->getToggleState(),
               who + " clicking the lit switch reaches Off");

        if (os4->onClick != nullptr) os4->onClick();
        check (! os2->getToggleState() && os4->getToggleState(), who + " 4x lit alone after a click");

        params.setReal (bmo::fetcomp::Index::oversampling, (float) bmo::fetcomp::osOff);
    }

    //== The meter's own row, at the full switch width ========================
    {
        auto* in  = button ("IN");
        auto* gr  = button ("GR");
        auto* out = button ("OUT");

        if (in == nullptr || gr == nullptr || out == nullptr)
            return;

        for (auto* b : { in, gr, out })
            checkEquals (b->getWidth(), bmo::ui::Tokens::switchWidth,
                         who + " " + b->getButtonText() + " is a full-width switch on a 260 px panel");

        // GR in the middle, IN and OUT reading left to right as signal flow
        // either side of it -- and GR is where the meter opens, because it is
        // the reading this module is for.
        check (in->getX() < gr->getX() && gr->getX() < out->getX(),
               who + " the meter row reads IN, GR, OUT");
        check (gr->getToggleState(), who + " the meter opens on GR");
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
    auto* bar = dynamic_cast<bmo::ui::LevelBar*> (found);

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
    //
    // **And a fader's caption and its reading**, which nothing here could see
    // until BMO Linger put three of them in an 80 px column. `ui::Fader` is not
    // a `PlainKnob`, so the walk above never looked at it, and its value line is
    // on by *default* -- it is the one control in the suite that always prints a
    // number under its name, and so the one most likely to outgrow its box.
    // `Fader::captionOverflow` measures the widest reading the parameter can
    // produce rather than whatever it happens to read now.
    std::function<void (juce::Component&)> walk = [&] (juce::Component& c)
    {
        for (auto* child : c.getChildren())
        {
            if (auto* band = dynamic_cast<bmo::ui::ConcentricBand*> (child))
                check (band->legendOverflow() <= 0.0f,
                       who + " a legend on '" + c.getName() + "' overflows its box by "
                           + juce::String (band->legendOverflow(), 1) + " px");

            if (auto* fader = dynamic_cast<bmo::ui::Fader*> (child))
                check (fader->captionOverflow() <= 0.0f,
                       who + " fader '" + fader->getName() + "' overflows its box by "
                           + juce::String (fader->captionOverflow(), 1)
                           + " px -- caption or widest reading");

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

/** BMO Defang's panel: the shape pair, the meter row, the band sketch and the
    momentary LISTEN switch.

    Two of these are invisible to a render at Init and so invisible to every
    other check here. The shape pair is a radio over a choice parameter and
    nothing toggles -- the Saturator's oversampling row could have been dead
    for a release the same way. **LISTEN is worse**: it has no parameter at
    all, so there is no value to read back afterwards, and the call to the
    engine is the whole of the behaviour. Intercepting it is the only way to
    assert on it, which is the same shape `checkDeqBandToggle` uses for the
    band solo this module's listen path is modelled on. */
void checkDeesserPanel (bmo::ui::ModulePanel& panel, const juce::String& who)
{
    auto& params = panel.getContext().params;

    const auto button = [&panel, &who] (const char* name) -> juce::Button*
    {
        auto* b = dynamic_cast<juce::Button*> (findNamed (panel, name));

        if (b == nullptr)
            check (false, who + " has no " + name + " switch");

        return b;
    };

    //== The shape pair: both states named, both reachable ====================
    {
        auto* bell  = button ("BELL");
        auto* shelf = button ("SHELF");

        if (bell == nullptr || shelf == nullptr)
            return;

        checkEquals (bell->getY(), shelf->getY(), who + " SHELF sits beside BELL");
        checkEquals (shelf->getX() - bell->getRight(), bmo::ui::Tokens::switchGap,
                     who + " gap between BELL and SHELF");

        for (auto* b : { bell, shelf })
        {
            checkEquals (b->getWidth(),  bmo::ui::Tokens::switchWidth,
                         who + " " + b->getButtonText() + " width");
            checkEquals (b->getHeight(), bmo::ui::Tokens::switchHeight,
                         who + " " + b->getButtonText() + " height");
        }

        // Bell is the default, and the index order is permanent.
        check (bell->getToggleState() && ! shelf->getToggleState(),
               who + " shape at Init is Bell");

        if (shelf->onClick != nullptr)
            shelf->onClick();

        check (shelf->getToggleState() && ! bell->getToggleState(),
               who + " clicking SHELF selects it");
        checkEquals (juce::roundToInt (params.getReal (bmo::deesser::Index::shape)),
                     (int) bmo::deesser::highShelf, who + " SHELF sets the shape parameter");

        params.setReal (bmo::deesser::Index::shape, (float) bmo::deesser::bell);
        check (bell->getToggleState(), who + " the parameter lights the shape pair, not the click");
    }

    //== One GR bar, scaled to what the module can actually do ================
    //
    // The needle and its IN/GR/OUT row went on 2026-09-20 (Frosty). Two of
    // those three modes showed the same reading twice on this module, because
    // a band cut takes under a dB of broadband energy -- so IN and OUT were
    // one number wearing two captions. What is checked here is what replaced
    // them, and the part worth protecting is the *scale*: a drift back to the
    // shared 24 dB would give the bar a top quarter no setting can reach.
    {
        bmo::ui::LevelBar* bar = nullptr;

        for (auto* c : panel.getChildren())
            if (auto* b = dynamic_cast<bmo::ui::LevelBar*> (c))
                bar = b;

        check (bar != nullptr, who + " has no GR bar");

        if (bar == nullptr)
            return;

        // The old row's three switches and two gaps, kept as the bar's width
        // so its well lines up with the shape pair above and LISTEN below.
        checkEquals (bar->getWidth(),
                     bmo::ui::Tokens::switchWidth * 3 + bmo::ui::Tokens::switchGap * 2,
                     who + " the GR bar is as wide as the switch rows it sits between");

        // 18 dB is RANGE's ceiling in params.h and therefore the deepest cut
        // the module can make. If that parameter's top ever moves, this fails
        // -- which is the point of asserting it here rather than in the panel.
        const auto top = bmo::deesser::specs()[bmo::deesser::Index::range].max;
        checkNear (top, 18.0, 0.0f, who + " RANGE's ceiling, which the GR bar is scaled to");

        // Nothing switches modes any more, so the render key that did must be
        // refused rather than quietly accepted -- a render of the wrong thing
        // is worse than a render that failed.
        check (! panel.setUiState ("meter", "GR"),
               who + " should refuse ui.meter now that the bar has no modes");
    }

    //== LISTEN is momentary, and it is not a parameter =======================
    {
        auto* listen = dynamic_cast<bmo::deesser::HoldButton*> (findNamed (panel, "LISTEN"));

        check (listen != nullptr, who + " has no LISTEN switch");

        if (listen == nullptr)
            return;

        check (bmo::indexOfParam (bmo::deesser::specs(), "listen") < 0,
               who + " listen must not be a parameter");
        check (! listen->getClickingTogglesState(),
               who + " LISTEN must not latch -- it is held, not toggled");
        check (listen->onHeld != nullptr, who + " LISTEN is not wired to anything");

        std::vector<int> soloCalls;
        auto& ctx = const_cast<bmo::ui::ModuleContext&> (panel.getContext());
        const auto realSolo = ctx.setSolo;

        ctx.setSolo = [&soloCalls, realSolo] (int b)
        {
            soloCalls.push_back (b);
            if (realSolo) realSolo (b);
        };

        // Pressing and releasing, through the same callback a mouse drives.
        listen->onHeld (true);
        check (soloCalls.size() == 1 && soloCalls.back() == 0,
               who + " holding LISTEN should solo the band, got "
                   + (soloCalls.empty() ? juce::String ("no call") : juce::String (soloCalls.back())));
        check (listen->getToggleState(), who + " LISTEN should light while held");

        listen->onHeld (false);
        check (soloCalls.size() == 2 && soloCalls.back() == -1,
               who + " releasing LISTEN should clear the solo with -1");
        check (! listen->getToggleState(), who + " LISTEN should go dark on release");

        // And the render path, which is the only way tools/snapshot can reach
        // it: same code, so a rendered LISTEN and a held one cannot diverge.
        check (panel.setUiState ("listen", "on"), who + " should accept ui.listen=on");
        check (soloCalls.size() == 3 && soloCalls.back() == 0,
               who + " ui.listen=on should solo the band");
        check (panel.setUiState ("listen", "off"), who + " should accept ui.listen=off");
        check (soloCalls.size() == 4 && soloCalls.back() == -1,
               who + " ui.listen=off should clear the solo");

        check (! panel.setUiState ("listen", "maybe"),
               who + " should refuse a listen value it does not understand");

        ctx.setSolo = realSolo;
    }

    //== The band sketch draws the band it is given ===========================
    //
    // It has no parameter of its own and paints itself, so nothing else here
    // would notice if it stopped following the knobs -- the same blind spot
    // BMO DEQ's gain-reduction bar fell through. `responseDbAt` is arithmetic,
    // not drawing, so this needs no render.
    {
        bmo::deesser::BandSketch* sketch = nullptr;

        for (auto* child : panel.getChildren())
            if (auto* s = dynamic_cast<bmo::deesser::BandSketch*> (child))
                sketch = s;

        check (sketch != nullptr, who + " has no band sketch");

        if (sketch == nullptr)
            return;

        params.setReal (bmo::deesser::Index::shape, (float) bmo::deesser::bell);
        params.setReal (bmo::deesser::Index::freq, 6500.0f);
        params.setReal (bmo::deesser::Index::q, 2.5f);
        params.setReal (bmo::deesser::Index::range, 8.0f);

        // A bell is its full depth at the centre and unity well away from it.
        checkNear (sketch->responseDbAt (6500.0f), -8.0, 0.1,
                    who + " the sketch's bell is RANGE deep at FREQ");
        checkNear (sketch->responseDbAt (200.0f), 0.0, 0.3,
                    who + " the sketch's bell is unity far below the band");
        checkNear (sketch->responseDbAt (19000.0f), 0.0, 0.5,
                    who + " the sketch's bell is unity far above the band");

        // RANGE is the depth, and moving it moves the picture.
        params.setReal (bmo::deesser::Index::range, 16.0f);
        checkNear (sketch->responseDbAt (6500.0f), -16.0, 0.1,
                    who + " the sketch follows RANGE");

        // FREQ is where it sits.
        params.setReal (bmo::deesser::Index::freq, 3000.0f);
        checkNear (sketch->responseDbAt (3000.0f), -16.0, 0.1,
                    who + " the sketch follows FREQ");
        check (sketch->responseDbAt (6500.0f) > -8.0f,
               who + " the sketch's old centre should be shallower once FREQ has moved");

        // A shelf is unity below its corner and its full depth above it, which
        // is the one shape that takes everything above the corner down.
        params.setReal (bmo::deesser::Index::shape, (float) bmo::deesser::highShelf);
        params.setReal (bmo::deesser::Index::freq, 6500.0f);
        params.setReal (bmo::deesser::Index::range, 8.0f);

        checkNear (sketch->responseDbAt (1000.0f), 0.0, 0.5,
                    who + " the sketch's shelf is unity below its corner");
        checkNear (sketch->responseDbAt (20000.0f), -8.0, 1.0,
                    who + " the sketch's shelf reaches RANGE above its corner");

        params.setReal (bmo::deesser::Index::shape, (float) bmo::deesser::bell);
        params.setReal (bmo::deesser::Index::range, 8.0f);
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
    // Written out here rather than read off `ReverbPanel::pageControls`: a test
    // that took the list from the same place the panel does would agree with
    // any list, including one that had quietly lost a control.

    // The persistent five: the three faders, and the TYPE / DECAY column. The
    // old persistent row of SIZE, PRE-DELAY and DECAY was dissolved on
    // 2026-09-22 -- SIZE scales every tap time so it went to EARLY, PRE-DELAY
    // is tail-only so it went to TAIL, and DECAY joined the strip.
    const char* const alwaysOn[] { "ER", "REVERB", "MIX", "TYPE", "DECAY" };

    const char* const earlyPage[] { "DENSITY", "ER SPREAD", "ER HI-CUT",
                                    "VARIATION", "SOURCE", "SIZE" };

    const char* const tailPage[] { "PRE-DELAY", "WIDTH", "MOD RATE",
                                   "LOW x", "HIGH x", "MOD DEPTH" };

    // **Six, and nine parameters behind three of them.** FREQ, GAIN and Q are
    // one set repointed by the LOW / MID / HIGH segments -- BMO DEQ's band
    // selector, the same mechanism -- so the page shows one node's worth at a
    // time and the other six lanes are reached through the row above.
    const char* const eqPage[] { "FREQ", "GAIN", "Q", "FILTER", "IN HI-CUT", "OUTPUT" };

    // **Every parameter still has a control, and this is the sum that says so.**
    // Twenty-three controls, one of which is a segmented row bound to `ermode`,
    // and three of which stand for nine lanes rather than three. A parameter
    // with no control anywhere is the Saturator's oversampling row, which sat on
    // the schema and nowhere on the panel for three releases; a panel that lost
    // the node selector would fail here rather than in a listening pass.
    {
        constexpr int kSegmentParams = 1;   ///< ER MODE, on EARLY
        constexpr int kExtraEqNodes  = 6;   ///< the two nodes FREQ / GAIN / Q are not pointed at

        check ((int) (std::size (alwaysOn) + std::size (earlyPage)
                        + std::size (tailPage) + std::size (eqPage))
                   + kSegmentParams + kExtraEqNodes == (int) R::Index::count,
               "every parameter has a control on some page of the panel");
    }

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
    static constexpr R::EqNode kNodes[] { R::EqNode::low, R::EqNode::mid, R::EqNode::high };

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
        // that makes a recess read as a bezel rather than as a border.
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

        // **The screen is the biggest thing on the face, and that is the point
        // of the rebuild.** It was 105 px and a letterbox while the EQ page
        // needed four reserved cluster rows; the node selector took that page to
        // two and the screen is what the freed rows went into. If a later pass
        // ever shrinks it again this is where the argument is.
        check (glass.getHeight() > reverbPanel->getClusterBox().getHeight(),
               who + " the screen is " + juce::String (glass.getHeight())
                   + " px and the cluster is "
                   + juce::String (reverbPanel->getClusterBox().getHeight())
                   + " -- the display should be the biggest block on this face");
    }

    //== The blocks, in order, with nothing overlapping and nothing off the end =
    //
    // The height budget as a relation rather than as five numbers copied out of
    // the panel: each block sits under the one before it, the last one ends
    // inside the panel, and the gaps between them are at least a `switchGap`.
    // A budget that stopped adding up would show here as a negative gap.
    {
        const auto bezel   = reverbPanel->getBezelBox();
        const auto segment = reverbPanel->getSegmentBox();
        const auto cluster = reverbPanel->getClusterBox();
        const auto strip   = reverbPanel->getStripBox();

        check (segment.getY() - bezel.getBottom() >= bmo::ui::Tokens::switchGap,
               who + " the segment row is " + juce::String (segment.getY() - bezel.getBottom())
                   + " px under the bezel and wants at least a switchGap");

        checkEquals (cluster.getY(), segment.getBottom(),
                     who + " the cluster should sit hard under the segment row -- the row is"
                           " part of the page, not a block of its own");

        check (strip.getY() > cluster.getBottom(),
               who + " the strip should sit under the cluster");

        check (strip.getBottom() <= panel.getHeight(),
               who + " the strip ends at " + juce::String (strip.getBottom())
                   + " on a panel " + juce::String (panel.getHeight()) + " tall");

        // The strip's height is the one number Frosty has already said will
        // change -- taller faders -- so it is named on the panel and asserted
        // here, which is what makes that a one-line change rather than a hunt.
        checkEquals (strip.getHeight(), R::ReverbPanel::kStripRow,
                     who + " the strip's height should be the named constant");
        checkEquals (cluster.getHeight(), R::ReverbPanel::kClusterRow * 2,
                     who + " the cluster is two rows of the named row height");
    }

    //== The page menu, inside the display ====================================
    //
    // **The page keys are not components any more.** They were three round
    // buttons on the plate with a PAGE rule under them; the menu is drawn in the
    // screen's own ink now, which means nothing in this file's generic walk can
    // see it -- no bounds, no caption, no child. So everything that walk used to
    // do for the keys has to be done here explicitly, and the MAKEUP -> MAKEU
    // fault hides better inside a picture than anywhere else on a panel.
    {
        const auto& screen = reverbPanel->getScreen();
        const auto band = screen.menuBandBounds();

        check (! band.isEmpty(), who + " the display draws no menu band");
        checkNear (band.getHeight(), (double) R::LingerScreen::kMenuBand, 0.01,
                   who + " the menu band's height");
        checkNear (band.getY(), 0.0, 0.01,
                   who + " the menu band should be the top of the display");

        // The band is above the picture and the picture is under it: a curve
        // drawn through the menu is the whole failure this separation prevents.
        check (screen.plotBounds().getY() >= band.getBottom(),
               who + " the plot starts at " + juce::String (screen.plotBounds().getY(), 1)
                   + " and the menu band ends at " + juce::String (band.getBottom(), 1));

        // Three segments tiling the band exactly, in page order.
        auto previous = band.getX();

        for (const auto p : kPages)
        {
            const auto seg = screen.menuSegment (p);

            checkNear (seg.getX(), (double) previous, 0.51,
                       juce::String (who) + " the " + nameFor (p)
                           + " menu segment does not start where the last one ended");
            checkNear (seg.getY(), (double) band.getY(), 0.01,
                       juce::String (who) + " the " + nameFor (p) + " segment's top");
            checkNear (seg.getHeight(), (double) band.getHeight(), 0.01,
                       juce::String (who) + " the " + nameFor (p) + " segment's height");

            previous = seg.getRight();

            check (screen.menuLabelOverflow (p) <= 0.0f,
                   juce::String (who) + " the menu word '" + R::LingerScreen::menuLabel (p)
                       + "' overflows its segment by "
                       + juce::String (screen.menuLabelOverflow (p), 1) + " px");

            // The word in a local, not walked straight off the call: `menuLabel`
            // returns a `juce::String` by value, and `toRawUTF8()` on the
            // temporary hands back a pointer into storage that is gone by the
            // time the loop reads it. It failed loudly here, which is the good
            // case; it is the same shape as a bug that would not.
            const auto word = R::LingerScreen::menuLabel (p);

            for (const auto* c = word.toRawUTF8(); *c != 0; ++c)
                if ((unsigned char) *c < 32 || (unsigned char) *c > 126)
                {
                    check (false, juce::String (who) + " the menu word for " + nameFor (p)
                                    + " is not ASCII");
                    break;
                }
        }

        checkNear (previous, (double) band.getRight(), 0.51,
                   who + " the three menu segments do not fill the band");

        // **And a click on one turns the page.** The band is painted, so "it
        // compiles" is not evidence that it is reachable: without this the menu
        // could be a picture of a menu and every other check here would pass.
        if (auto* display = findNamed (panel, "DISPLAY"))
        {
            for (const auto p : { R::Page::eq, R::Page::tail, R::Page::early })
            {
                const auto at = screen.menuSegment (p).getCentre();

                display->mouseUp (clickAt (*display, at, 1));

                check (reverbPanel->getPage() == p,
                       juce::String (who) + " clicking the " + nameFor (p)
                           + " menu segment did not turn the page");
            }

            // A click below the band is a click on the picture and does
            // nothing -- which is what stops a drag across a curve from paging.
            reverbPanel->setPage (R::Page::tail);
            display->mouseUp (clickAt (*display, screen.plotBounds().getCentre(), 1));

            check (reverbPanel->getPage() == R::Page::tail,
                   who + " a click inside the picture turned the page");
        }
    }

    //== One rule, legended LEVEL, spanning only the faders ===================
    //
    // **The PAGE rule went with the page keys.** What is left is LEVEL, and the
    // thing worth asserting about it is the span: it named a fourth column it
    // does not describe for a release, and the panel owned that in a comment as
    // a wrinkle rather than drawing the truth.
    {
        const auto& rules = panel.getRules();

        check (rules.size() == 1,
               who + " should carry exactly one rule, has " + juce::String ((int) rules.size()));

        if (rules.size() == 1)
        {
            check (rules[0].text == "LEVEL",
                   who + " the rule should be legended LEVEL, reads '" + rules[0].text + "'");

            check (! rules[0].span.isEmpty(),
                   who + " the LEVEL rule runs edge to edge and should stop at the faders");

            auto* mix  = findNamed (panel, "MIX");
            auto* type = findNamed (panel, "TYPE");

            if (mix != nullptr && type != nullptr)
            {
                check (rules[0].span.getEnd() >= mix->getRight()
                         && rules[0].span.getEnd() <= type->getX() + 1,
                       who + " the LEVEL rule ends at " + juce::String (rules[0].span.getEnd())
                           + ", and should end between MIX's right edge "
                           + juce::String (mix->getRight()) + " and TYPE's left edge "
                           + juce::String (type->getX()));

                // The three it names are under it; the column it does not name
                // is under it too, because they share a strip -- the rule's span
                // is what says which of them it is about.
                for (const auto* caption : { "ER", "REVERB", "MIX" })
                    if (auto* c = findNamed (panel, caption))
                        check (c->getY() >= rules[0].row.getBottom(),
                               who + " " + caption + " should sit under the LEVEL rule");
            }
        }
    }

    //== The fourth column: TYPE above, DECAY below ===========================
    //
    // Frosty's call, 2026-09-22. With both names underneath, the upper label
    // fell between the two controls and bound downward -- it read as DECAY's
    // second caption and the column stopped being two controls.
    {
        auto* type  = findNamed (panel, "TYPE");
        auto* decay = findNamed (panel, "DECAY");
        auto* mix   = findNamed (panel, "MIX");

        check (type != nullptr,  who + " has no TYPE control");
        check (decay != nullptr, who + " has no DECAY control");

        if (type != nullptr && decay != nullptr && mix != nullptr)
        {
            check (type->getBottom() <= decay->getY(),
                   who + " TYPE is at " + type->getBounds().toString()
                       + " and DECAY at " + decay->getBounds().toString()
                       + " -- TYPE goes above DECAY");

            check (decay->getY() - type->getBottom() >= bmo::ui::Tokens::switchGap,
                   who + " TYPE and DECAY are "
                       + juce::String (decay->getY() - type->getBottom())
                       + " px apart and want at least a switchGap, or the column reads as"
                         " one control with two captions");

            checkEquals (type->getX(), decay->getX(),
                         who + " TYPE and DECAY should share a column");
            checkEquals (type->getRight(), decay->getRight(),
                         who + " TYPE and DECAY should share a column's width");

            check (type->getX() >= mix->getRight(),
                   who + " the TYPE / DECAY column should sit right of MIX");

            // **TYPE's caption is above its box**, which is the half of this a
            // rectangle alone cannot show. Read off the control, so the check
            // uses the same arrangement the paint does.
            if (auto* box = dynamic_cast<bmo::ui::ChoiceBox*> (type))
                check (box->isCaptionAbove(),
                       who + " TYPE's caption should be set above its box");

            // And the column is the corner: nothing reaches further right or
            // further down than it does.
            for (auto* child : panel.getChildren())
                check (child->getRight() <= type->getRight()
                         && child->getBottom() <= decay->getBottom(),
                       who + " '" + child->getName() + "' at " + child->getBounds().toString()
                           + " reaches past the TYPE / DECAY column");
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

        // The persistent controls are on screen whatever page this is -- that is
        // the whole claim being made about them.
        for (const auto* caption : alwaysOn)
            check (findNamed (panel, caption) != nullptr,
                   where + " has lost the persistent control " + caption);

        // This page's cluster is present and inside the cluster block; the other
        // two pages' controls are **not children at all**. A hidden component
        // still has bounds, and every walker in this file reads them -- so a
        // hidden control with a stale rectangle either escapes the panel,
        // overlaps something, or reports a caption overflowing a box of width
        // zero. Unparenting is the one state in which a control is genuinely not
        // part of the layout.
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
                {
                    // FREQ, GAIN and Q are the EQ page's and are rebuilt per
                    // node, so they are not on the other two pages by name --
                    // but neither is anything else, so the rule is the same one.
                    check (findNamed (panel, theirs[i]) == nullptr,
                           where + " still carries " + theirs[i] + ", which belongs to "
                                 + nameFor (q) + " -- a page that is not showing must be"
                                   " unparented, not hidden");
                }
            }
        }

        //-- The segmented row, and its absence on TAIL ----------------------
        //
        // **The row is reserved on every page and filled on two.** That is what
        // keeps the two knob rows at one y; and TAIL's empty row is the design
        // rather than an omission, because a row of segments appearing is what
        // says there is a sub-selection here.
        {
            const auto box = reverbPanel->getSegmentBox();

            check (! box.isEmpty(), where + " reserves no segment row");
            checkEquals (box.getHeight(), bmo::ui::Tokens::switchHeight,
                         where + " the segment row's height -- it is a row of switches");

            const auto* row = reverbPanel->segmentsFor (p);

            if (p == R::Page::tail)
            {
                check (row == nullptr,
                       where + " has a segmented row, and nothing on TAIL is three-way");

                // And it is not merely null: neither row is a child.
                check (findNamed (panel, "ER MODE") == nullptr
                         && findNamed (panel, "EQ NODE") == nullptr,
                       where + " still carries a segmented row as a child");
            }
            else
            {
                check (row != nullptr, where + " should carry a segmented row");

                if (row != nullptr)
                {
                    check (box.contains (row->getBounds()),
                           where + " the segmented row at " + row->getBounds().toString()
                                 + " is outside the reserved row " + box.toString());

                    checkEquals (row->numSegments(), 3,
                                 where + " the segmented row should have three segments");

                    // The segments tile the row, and every word fits its own
                    // segment. A segmented row is the other place on this panel
                    // where a word is set inside a shape rather than under one.
                    auto previous = 0;

                    for (int i = 0; i < row->numSegments(); ++i)
                    {
                        const auto seg = row->segmentBounds (i);

                        checkEquals (seg.getX(), previous,
                                     where + " segment " + juce::String (i)
                                         + " does not start where the last one ended");
                        checkEquals (seg.getHeight(), row->getHeight(),
                                     where + " segment " + juce::String (i) + "'s height");
                        previous = seg.getRight();

                        check (row->labelOverflow (i) <= 0.0f,
                               where + " segment " + juce::String (i)
                                   + "'s word overflows its box by "
                                   + juce::String (row->labelOverflow (i), 1) + " px");
                    }

                    checkEquals (previous, row->getWidth(),
                                 where + " the segments do not fill the row");

                    // **Not the column grid**, deliberately: three segments at
                    // the cell width would sit exactly over the three knobs
                    // below and read as column headings, which on EQ is the
                    // opposite of what the row says.
                    check (row->getWidth() < reverbPanel->getClusterBox().getWidth(),
                           where + " the segmented row is as wide as the knob columns, so its"
                                   " segments sit over them and read as column headings");
                }
            }
        }

        // Everything the generic walk does, once per page, because a page that
        // is not showing is not laid out and would otherwise never be looked at.
        // **This is where the MAKEUP -> MAKEU class of fault would show up** on
        // two thirds of this module.
        checkWithinPanel     (panel, where);
        checkNoOverlap       (panel, where);
        checkCaptionsFit     (panel, where);
        checkSwitchLabelsFit (panel, where);

        // **The readout line fits the box it is printed in.** It is painted, so
        // it has no component and the caption walk cannot see it -- and it is
        // the one line on this panel that changes length with the parameters,
        // which is exactly the shape of thing that clips after a later edit.
        {
            const auto text = reverbPanel->getScreen().readout();
            const auto width = juce::GlyphArrangement::getStringWidth (
                                   bmo::ui::labelFont (R::ReverbPanel::kReadoutSize, true), text);

            check (width <= (float) reverbPanel->getReadoutBox().getWidth(),
                   where + " the reading '" + text + "' is " + juce::String (width, 1)
                         + " px in a " + juce::String (reverbPanel->getReadoutBox().getWidth())
                         + " px box");
        }

        // **One module, one colour.** `ui::Knob::Style` says what a knob is, not
        // where it sits: `utility` is the pale blue of input, output and gain,
        // `character` is the module's own accent worn by anything that shapes
        // the sound. Twenty-three of these went out `utility` as a block once,
        // and the module rendered violet at the top and suite azure below as
        // though it were two plugins sharing a slot -- while every check in this
        // file passed, because none of them had ever read a style.
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

                // **And every cap on this face is the same 27.6 px**, which is
                // `ui::Fader::kCapWidth` -- so a knob, a fader and the FILTER
                // ring in one window are one size of control rather than three.
                // The face scale is derived from the cap rather than the other
                // way round, and this is what says the derivation held.
                checkNear ((double) face->getFaceScale() * juce::jmin (face->getWidth(),
                                                                       face->getHeight()),
                           (double) bmo::ui::Fader::kCapWidth, 0.6,
                           where + " " + knob->getName() + "'s cap");
            }
        }

        // **The FILTER ring's cap, read off the ring rather than off its box.**
        // This is the failure the accessor was added for: sized by its 66 px
        // cluster cell the cap came out at 23 px beside 26.7 px knobs and the
        // row visibly stepped when the page turned. The 79 px row is derived
        // backwards from this number, so if the row ever changes this fails
        // first.
        if (p == R::Page::eq)
        {
            const auto& ring = reverbPanel->getFilterRing();

            checkNear ((double) ring.capDiameter(), (double) bmo::ui::Fader::kCapWidth, 0.1,
                       who + " the FILTER ring's cap, in a box of "
                           + ring.getBounds().toString());

            check (ring.legendOverflow() <= 0.0f,
                   who + " the FILTER ring's legend overflows by "
                       + juce::String (ring.legendOverflow(), 1) + " px");
        }

        // **Nothing stands alone in a row, and the one stack is a stack.**
        //
        // The rule exists because a lone centred knob with two empty quarters
        // beside it reads as a control whose partner has gone missing -- which
        // is what MIX did on the old face. The TYPE / DECAY column is the one
        // exception and it is an exception by construction rather than by
        // accident: the two share a column, which is visibly a pair, and the
        // pair as a whole shares the strip with the three faders. So a control
        // passes if something shares its centre line **or** something shares its
        // column. A lone knob in the middle of a row still fails both.
        //
        // A row is a shared centre line rather than a shared top: a
        // `SwitchButton` is 26 px among 79 px cells, and asking it to share a
        // knob's top edge would mean stretching it to a knob's height. Two
        // *dials* in one row still have to share a top and a foot, and that is
        // asserted on top.
        {
            for (auto* child : panel.getChildren())
            {
                // The display is the row, and so is a segmented row: both span
                // their own block and have nothing to stand beside.
                if (child->getName() == "DISPLAY"
                      || dynamic_cast<R::Segments*> (child) != nullptr)
                    continue;

                const auto mine = child->getBounds();
                int alongside = 0, stacked = 0;

                for (auto* other : panel.getChildren())
                {
                    if (other == child || other->getName() == "DISPLAY"
                          || dynamic_cast<R::Segments*> (other) != nullptr)
                        continue;

                    const auto theirs = other->getBounds();

                    if (std::abs (theirs.getCentreY() - mine.getCentreY()) <= 1)
                    {
                        ++alongside;

                        const auto bothAreDials =
                            dynamic_cast<bmo::ui::SwitchButton*> (child) == nullptr
                              && dynamic_cast<bmo::ui::SwitchButton*> (other) == nullptr;

                        if (bothAreDials)
                            check (theirs.getY() == mine.getY()
                                     && theirs.getBottom() == mine.getBottom(),
                                   where + " '" + child->getName() + "' and '" + other->getName()
                                         + "' share a row and should share a top and a foot");
                    }
                    else if (theirs.getX() == mine.getX() && theirs.getRight() == mine.getRight())
                    {
                        ++stacked;
                    }
                }

                check (alongside > 0 || stacked > 0,
                       where + " '" + child->getName()
                             + "' is alone in its row and in its column");
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
        // clipped mark that every render showed and no test could see.
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

            // The curtain is the EQ page's and nobody else's, and on that page it
            // is inside the plot **at the top of IN HI-CUT's travel**, which is
            // the setting that used to clip.
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

    //== The page and the node are UI state, and unknown values are refused ====
    //
    // `ui.page=...` and `ui.node=...`, the hook BMO Opto's meter mode and BMO
    // DEQ's band use. **Refused rather than defaulted**, and DEQ's comment is
    // the argument: a render labelled EQ that shows EARLY is worse than no
    // render, and nothing downstream could tell the two apart.
    {
        check (panel.setUiState ("page", "tail"), who + " should take ui.page=tail");
        check (reverbPanel->getPage() == R::Page::tail, who + " ui.page=tail did not turn the page");

        check (panel.setUiState ("page", "EQ"), who + " should take ui.page=EQ, case and all");
        check (reverbPanel->getPage() == R::Page::eq, who + " ui.page=EQ did not turn the page");

        // **"tone" is refused like any other unknown value**, and it is in this
        // list rather than accepted as a synonym. It was the third page's key
        // until 2026-09-21, and a render script that still passes it should stop
        // with an error rather than quietly produce an EARLY page labelled TONE.
        for (const auto* bad : { "tone", "middle", "1", "", "earl", "early tail", " early" })
            check (! panel.setUiState ("page", bad),
                   who + " should refuse ui.page=" + bad + " rather than falling back");

        check (reverbPanel->getPage() == R::Page::eq,
               who + " a refused page must leave the panel on the one it was showing");

        // The node selector is the second key, and it is the same contract.
        check (panel.setUiState ("node", "mid"), who + " should take ui.node=mid");
        check (reverbPanel->getNode() == R::EqNode::mid, who + " ui.node=mid did not repoint");

        check (panel.setUiState ("node", "HIGH"), who + " should take ui.node=HIGH, case and all");
        check (reverbPanel->getNode() == R::EqNode::high, who + " ui.node=HIGH did not repoint");

        for (const auto* bad : { "middle", "0", "", "lo", "low mid", " low", "bell" })
            check (! panel.setUiState ("node", bad),
                   who + " should refuse ui.node=" + juce::String (bad) + " rather than falling back");

        check (reverbPanel->getNode() == R::EqNode::high,
               who + " a refused node must leave the panel on the one it was on");

        for (const auto* key : { "band", "meter", "view", "" })
            check (! panel.setUiState (key, "early"),
                   who + " should refuse the key ui." + key + ", which is not its state");

        panel.setUiState ("node", "low");
    }

    //== The screen's own arithmetic ==========================================
    //
    // **This is the sketch/DSP drift assertion** that 11 section 5 asks for by
    // name, and it is the whole reason `dsp/TapTables.h` is JUCE-free: the panel
    // and the engine read one table, so the picture cannot quietly stop
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

        // **The EARLY axis spans the real ER window**: the last tap has to land
        // inside the box with a little air after it rather than on the border or
        // halfway across.
        check (screen.erWindowMs() > screen.lastTapTimeMs(),
               who + " the last tap must fall inside the EARLY window, not on its edge");
        check (screen.erWindowMs() < screen.lastTapTimeMs() * 1.5f,
               who + " the EARLY window is " + juce::String (screen.erWindowMs(), 1)
                   + " ms for a " + juce::String (screen.lastTapTimeMs(), 1)
                   + " ms cluster, which leaves most of the box empty");

        // SIZE scales the picture and the window with it, which is the Size law
        // made visible.
        params.setReal (R::Index::size, R::roomDefaults::kSizeM * 2.0f);
        checkNear (screen.firstTapTimeMs(),
                   (double) R::tapTimeMsAt (R::kReferenceTaps[0], R::roomDefaults::kSizeM) * 2.0, 1.0e-3,
                   who + " twice the size is twice the first tap's time");
        check (screen.erWindowMs() > screen.lastTapTimeMs(),
               who + " the EARLY window follows SIZE");
        params.setReal (R::Index::size, R::roomDefaults::kSizeM);

        //-- The scatter: three axes, and only the real ones are drawn --------
        //
        // **This is what replaced the mirrored stems.** x is arrival, y is
        // bearing and the radius is gain, so each of the three is asserted as a
        // claim of its own -- a picture that had dropped one of them would still
        // draw twenty-one marks in roughly the right place.
        {
            params.setReal (R::Index::erlevel, 0.0f);

            const auto plot = screen.plotBounds();

            for (int i = 0; i < R::kNumReferenceTaps; ++i)
            {
                const auto dot = screen.tapDot (i);

                check (dot.radius > 0.0f,
                       who + " tap " + juce::String (i) + " draws no dot at ER level 0 dB");

                // Inside the box, dot and all. The EARLY window keeps a tenth of
                // itself clear after the last tap and `kPanReach` keeps a hard
                // pan off the frame, so nothing here should be within its own
                // radius of an edge.
                check (plot.contains (juce::Rectangle<float> (dot.radius * 2.0f, dot.radius * 2.0f)
                                          .withCentre (dot.centre)),
                       who + " tap " + juce::String (i) + "'s dot at "
                           + dot.centre.toString() + " r " + juce::String (dot.radius, 1)
                           + " is not inside the plot " + plot.toString());
            }

            // **x is time**: the taps are tabulated in ascending order, so the
            // dots run left to right and the first one is where the table says.
            for (int i = 1; i < R::kNumReferenceTaps; ++i)
                check (screen.tapDot (i).centre.x > screen.tapDot (i - 1).centre.x,
                       who + " tap " + juce::String (i)
                           + " does not draw to the right of the one before it");

            // **The radius is gain**: the table decays from 0.501 to 0.087, so
            // the last dot is visibly smaller than the first, and none of them
            // has collapsed to nothing.
            check (screen.tapDot (0).radius > screen.tapDot (R::kNumReferenceTaps - 1).radius + 1.0f,
                   who + " the first tap's dot is "
                       + juce::String (screen.tapDot (0).radius, 2) + " and the last is "
                       + juce::String (screen.tapDot (R::kNumReferenceTaps - 1).radius, 2)
                       + " -- the radius is supposed to be the gain");
            check (screen.tapDot (R::kNumReferenceTaps - 1).radius >= R::LingerScreen::kDotMinRadius,
                   who + " the quietest tap's dot has shrunk below the floor radius");

            // **y is bearing, and VARIATION fans it.** The table's third tap is
            // panned +0.34 and its fourth -0.41, so one draws below the axis and
            // the other above it -- and turning VARIATION up moves both further
            // from the centre. This is the whole reason the scatter replaced the
            // stems.
            params.setReal (R::Index::ervariation, 6.0f);
            const auto wideSpread = screen.lateralSpread();
            const auto wideThird  = screen.tapDot (2).centre.y;

            params.setReal (R::Index::ervariation, 0.0f);
            const auto narrowSpread = screen.lateralSpread();
            const auto narrowThird  = screen.tapDot (2).centre.y;

            check (wideSpread > narrowSpread,
                   who + " VARIATION does not open the lateral spread: "
                       + juce::String (narrowSpread, 3) + " at the bottom and "
                       + juce::String (wideSpread, 3) + " at the top");
            check (wideThird > narrowThird + 4.0f,
                   who + " VARIATION does not fan the scatter -- tap 3 sits at "
                       + juce::String (narrowThird, 1) + " narrow and "
                       + juce::String (wideThird, 1) + " wide");

            params.setReal (R::Index::ervariation,
                            R::specs()[(size_t) R::Index::ervariation].def);

            const auto third  = screen.tapDot (2).centre.y;
            const auto fourth = screen.tapDot (3).centre.y;

            check (third > plot.getCentreY() && fourth < plot.getCentreY(),
                   who + " tap 3 is panned right and tap 4 left, so one draws below the"
                         " centre axis and the other above it");

            // The direct sound is the ringed dot at t = 0 on the centre line,
            // **inside the box**: centred on the frame it would be half drawn,
            // which is the fault IN HI-CUT's marker had on the other page.
            const auto direct = screen.directDot();

            checkNear (direct.centre.y, (double) plot.getCentreY(), 0.5,
                       who + " the direct sound should sit on the centre axis");
            check (direct.centre.x - direct.radius >= plot.getX(),
                   who + " the direct sound's dot is drawn on the left frame");
            check (direct.centre.x < screen.tapDot (0).centre.x,
                   who + " the direct sound should arrive before the first reflection");

            // ER at "Off" is silence and not -40 dB, so the reflections go
            // entirely -- and the direct sound, which is not a reflection, does
            // not.
            params.setReal (R::Index::erlevel, R::specs()[(size_t) R::Index::erlevel].min);

            check (screen.tapDot (0).radius <= 0.0f,
                   who + " ER at Off still draws taps");
            check (screen.directDot().radius > 0.0f,
                   who + " ER at Off took the direct sound with it, and the direct sound is"
                         " not an early reflection");

            params.setReal (R::Index::erlevel, R::specs()[(size_t) R::Index::erlevel].def);
        }

        // **Pre-delay is tail-only, at every setting, permanently.** It used to
        // depend on LINK ER, and LINK ER was cut on 2026-09-21: off -- ER with
        // dry -- is the reference behaviour and is now `kPreLinkFixed`.
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

        //-- Three curves, and they are LOW x, DECAY and HIGH x --------------
        //
        // **The defect this replaced**: one envelope at the mid decay, with the
        // two multipliers shading a band behind it. LOW x could be swept end to
        // end and the line a reader was looking at never moved. The three times
        // are asserted as absolutes rather than as "they differ", because a
        // picture drawing one curve three times differs from nothing.
        {
            const auto t = screen.decayTimesSeconds();

            checkNear (t[0], 3.0, 1.0e-3, who + " the low curve is DECAY x LOW x");
            checkNear (t[1], 2.0, 1.0e-3, who + " the mid curve is DECAY itself");
            checkNear (t[2], 0.8, 1.0e-3, who + " the high curve is DECAY x HIGH x");

            // And each multiplier moves its own curve and neither of the others.
            params.setReal (R::Index::damplo, 0.5f);
            const auto moved = screen.decayTimesSeconds();

            checkNear (moved[0], 1.0, 1.0e-3, who + " LOW x did not move the low curve");
            check (moved[1] == t[1] && moved[2] == t[2],
                   who + " LOW x moved a curve that is not the low one");
        }

        params.setReal (R::Index::damplo, 0.5f);
        checkNear (screen.tailEndSeconds(), 2.0, 1.0e-3,
                   who + " both multipliers under unity leaves the mid decay as the reach");

        // **The 21 core taps never switch off.** That is what keeps the
        // renormalising denominator bounded away from zero and so what makes the
        // density sweep continuous and click-free (10 section 3).
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

        // The TAIL page's window is the one the class comment argues for, and
        // **its right-hand end follows the tail**.
        checkNear (R::LingerScreen::kMinMs, 1.0, 1.0e-6, who + " the TAIL axis starts at 1 ms");
        checkNear (R::LingerScreen::kMaxSeconds, bmo::kMaxTailSeconds, 1.0e-6,
                   who + " the TAIL axis is clamped where the reported tail is clamped");

        //-- The axis follows the tail, and the box stops being 40 % empty ----
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

                // The air after the curve is taken in width, so on a log axis it
                // is a fraction of the decades and not of the seconds. Three
                // times the tail is the loosest this may ever be.
                check (window < end * 3.0f,
                       who + " the TAIL axis runs to " + juce::String (window, 2)
                           + " s for a " + juce::String (end, 2)
                           + " s tail, which is the dead box this was meant to fix");

                check (window <= R::LingerScreen::kMaxSeconds,
                       who + " the TAIL axis must never draw past the tail clamp");
            }

            // Longer decay, longer window: the axis is a scale that moves, not a
            // second control that sticks.
            params.setReal (R::Index::decay, 1.0f);
            const auto shortWindow = screen.tailWindowSeconds();

            params.setReal (R::Index::decay, 10.0f);
            check (screen.tailWindowSeconds() > shortWindow * 2.0f,
                   who + " the TAIL axis does not follow DECAY");

            // And at the top of both controls it stops at the clamp rather than
            // drawing a tail longer than the one the host is told about.
            params.setReal (R::Index::decay, 20.0f);
            params.setReal (R::Index::damplo, 2.0f);
            checkNear (screen.tailWindowSeconds(), (double) R::LingerScreen::kMaxSeconds, 1.0e-3,
                       who + " a 40 s tail should draw against the 30 s clamp");

            params.setReal (R::Index::damplo, R::specs()[(size_t) R::Index::damplo].def);
            params.setReal (R::Index::decay, R::specs()[(size_t) R::Index::decay].def);
        }

        // Put it back, so the blocks below read a panel at its defaults.
        for (const auto i : { R::Index::decay, R::Index::damplo, R::Index::damphi,
                              R::Index::erdensity, R::Index::ervariation })
            params.setReal (i, R::specs()[(size_t) i].def);
    }

    //== The onset follows TYPE, with no knob anywhere =========================
    //
    // **ATTACK has no parameter since the 2026-09-21 control-set trim**, so the
    // only thing that moves the TAIL page's onset is a type change -- and the
    // only place the number appears at all is the readout line under the
    // picture. If the panel had kept reading a parameter that no longer exists,
    // or had frozen on Room's constant, every other check in this file would
    // still pass.
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
            // **ONSET and not BLOOM**, owner-approved 2026-09-21: BMO Dimension
            // already ships a control captioned BLOOM -- Gerzon's bass shuffler,
            // nothing to do with a reverb's tail. Pinned here so a tidy back to
            // BLOOM fails rather than passing quietly.
            const auto expected = "ONSET " + juce::String (juce::roundToInt (
                                      R::constantsFor (detent).attack * 1.2f)) + " MS";

            check (onsetFor (detent).contains (expected),
                   who + " on " + R::kTypeNames[detent] + " the TAIL reading should carry '"
                       + expected + "', reads '" + onsetFor (detent) + "'");

            check (! onsetFor (detent).contains ("BLOOM"),
                   who + " the TAIL reading still says BLOOM, which is BMO Dimension's word");
        }

        // Non-vacuous: Plate's tail is immediate and Cavern's is not, so the two
        // readings cannot be the same string.
        check (onsetFor (R::plate) != onsetFor (R::cavern),
               who + " Plate and Cavern print the same onset, so the readout is not"
                     " following the type");

        // **And the line names the two curves the page's multipliers move.** It
        // printed DECAY and TAIL until 2026-09-22 -- DECAY is on a knob in the
        // strip and TAIL is the slowest of the three, which is a number with no
        // control under it. LOW and HIGH are what LOW x and HIGH x do.
        params.setReal (R::Index::type, (float) R::room);
        params.setReal (R::Index::decay, 2.0f);
        params.setReal (R::Index::damplo, 1.5f);
        params.setReal (R::Index::damphi, 0.4f);

        const auto line = reverbPanel->getScreen().readout();

        check (line.contains ("LOW 3.00 S") && line.contains ("HIGH 0.80 S"),
               who + " the TAIL reading should carry the two outer curves' times, reads '"
                   + line + "'");

        for (const auto i : { R::Index::decay, R::Index::damplo, R::Index::damphi })
            params.setReal (i, R::specs()[(size_t) i].def);

        reverbPanel->setPage (R::Page::early);
    }

    //== ER MODE's segments are a parameter; the node's are not ===============
    //
    // **The two bindings, asserted as two different claims**, because they are
    // the whole reason one component was chosen over two. A row that had been
    // wired to the wrong side would draw identically.
    {
        reverbPanel->setPage (R::Page::early);

        auto* row = const_cast<R::Segments*> (reverbPanel->segmentsFor (R::Page::early));

        check (row != nullptr, who + " the EARLY page has no ER MODE row");

        if (row != nullptr)
        {
            // A click writes the parameter, which is what makes it automatable
            // and undoable -- `DeqPanel::toggleBand`'s gesture, same reason.
            row->mouseUp (clickAt (*row, row->segmentBounds (R::energy).toFloat().getCentre(), 1));

            checkNear (params.getReal (R::Index::ermode), (double) R::energy, 1.0e-4,
                       who + " clicking the Energy segment did not write `ermode`");
            checkEquals (row->getSelected(), (int) R::energy,
                         who + " the ER MODE row did not light the segment that was clicked");

            // And the parameter moves the row, which is the half a click cannot
            // show: a host, an automation lane and a preset recall all arrive
            // this way.
            params.setReal (R::Index::ermode, (float) R::taps);
            checkEquals (row->getSelected(), (int) R::taps,
                         who + " `ermode` moved and the ER MODE row did not follow it");
        }

        // **The node row writes no parameter at all**, which is the claim that
        // matters on the other side: `specs()` is thirty with two lanes spare
        // and which node a panel is pointed at must never take one.
        reverbPanel->setPage (R::Page::eq);
        reverbPanel->setNode (R::EqNode::low);

        std::vector<float> before ((size_t) R::Index::count);

        for (int i = 0; i < (int) R::Index::count; ++i)
            before[(size_t) i] = params.getReal (i);

        auto* nodeRow = const_cast<R::Segments*> (reverbPanel->segmentsFor (R::Page::eq));

        check (nodeRow != nullptr, who + " the EQ page has no node row");

        if (nodeRow != nullptr)
        {
            nodeRow->mouseUp (clickAt (*nodeRow,
                                       nodeRow->segmentBounds (2).toFloat().getCentre(), 1));

            check (reverbPanel->getNode() == R::EqNode::high,
                   who + " clicking the HIGH segment did not repoint the knobs");

            for (int i = 0; i < (int) R::Index::count; ++i)
                check (juce::approximatelyEqual (params.getReal (i), before[(size_t) i]),
                       who + " repointing the node wrote parameter " + juce::String (i)
                           + ", and the node selector is UI state");
        }

        reverbPanel->setNode (R::EqNode::low);
    }

    //== EQ: three designed nodes, plus the input cut, in series ==============
    //
    // Absolute figures rather than "the curve moved", which is the house rule
    // from tests/dsp/OptoDspTests.cpp -- a relative test there passed for a
    // whole release while both of the things it compared were broken.
    //
    // **And the figures are the engine's own.** The three Reverb EQ nodes are
    // `dsp::designMatched`, so at their defaults they are that function's exact
    // unity case and contribute exactly nothing. What is asserted *here* rather
    // than in tests/dsp is the wiring: that the panel's screen reads the
    // parameters a host is holding and hands them to the same design the engine
    // uses.
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
        // is -0.01086 dB. Not "roughly zero" -- the exact figure, because an EQ
        // that had quietly acquired half a dB somewhere would still read as
        // roughly zero.
        checkNear (screen.responseDbAt (1000.0f), -0.010857, 1.0e-4,
                   who + " a flat EQ page draws a flat curve");

        // **1e-4 dB rather than 1e-9, and the difference is a platform fact
        // rather than slack.** A gain set to exactly 0 does not arrive as
        // exactly 0: it round-trips through the host's 32-bit normalised float,
        // and these three gains sit at 0 dB on a -24..+12 range whose
        // normalised position is 2/3, which is not exactly representable.
        // Windows happened to land back on zero; macOS landed 3.6e-07 dB away,
        // and this was the only EQ assertion that failed there. It is the same
        // round-trip that made the dB formatter print "+0.0 dB" on macOS alone.
        //
        // 1e-4 matches the summed-response check above, is five thousand times
        // tighter than the "half a dB acquired somewhere" the comment there is
        // guarding against, and sits far below anything audible. What the test
        // claims is unchanged; only the arithmetic it demands of a 32-bit float
        // round-trip is.
        for (const auto hz : { 30.0f, 200.0f, 1000.0f, 1600.0f, 8000.0f })
            for (const auto n : kNodes)
                checkNear (screen.nodeDbAt (n, hz), 0.0, 1.0e-4,
                           who + " every EQ node is flat at its default");

        // The one-pole input cut is -3.01 dB at its own corner, which is what
        // makes it a corner.
        params.setReal (R::Index::inhicut, 2000.0f);
        checkNear (screen.responseDbAt (2000.0f), -3.0103, 1.0e-3,
                   who + " the input high-cut is 3 dB down at its corner");
        resetEq();

        //-- One set of knobs over nine parameters, and they re-range --------
        //
        // **This is the new mechanism and the thing about it a reader would not
        // guess.** FREQ is 16-1600 Hz on the low shelf, 20 Hz-20 kHz on the bell
        // and 1 k-20 kHz on the high shelf; Q stops at `kShelfMaxQ` on the two
        // shelves and runs to a bell's 40 on node 2. So the same knob at the
        // same angle means three different frequencies depending on the segment
        // above it.
        //
        // The ranges are read off the rotary the attachment configured -- not
        // off `specs()` -- so a knob bound to the wrong node fails here rather
        // than looking right and editing something else.
        {
            struct NodeRange
            {
                R::EqNode node;
                const char* name;
                double freqLow, freqHigh, qHigh;
                int freqIndex, gainIndex;
            };

            const NodeRange ranges[] {
                { R::EqNode::low,  "LOW",  16.0,   1600.0,  R::kShelfMaxQ, R::Index::eqlofreq,  R::Index::eqlo  },
                { R::EqNode::mid,  "MID",  20.0,   20000.0, 40.0,          R::Index::eqmidfreq, R::Index::eqmid },
                { R::EqNode::high, "HIGH", 1000.0, 20000.0, R::kShelfMaxQ, R::Index::eqhifreq,  R::Index::eqhi  },
            };

            const auto rotary = [&] (const char* caption) -> const bmo::ui::Knob*
            {
                auto* found = dynamic_cast<bmo::ui::PlainKnob*> (findNamed (panel, caption));

                if (found == nullptr)
                {
                    check (false, who + " the EQ page has no " + caption + " knob");
                    return nullptr;
                }

                return knobFace (*found);
            };

            for (const auto& r : ranges)
            {
                reverbPanel->setNode (r.node);

                check (reverbPanel->getNode() == r.node,
                       juce::String (who) + " the panel did not take node " + r.name);
                check (reverbPanel->getScreen().getSelectedNode() == r.node,
                       juce::String (who) + " the screen did not take node " + r.name);

                if (const auto* freq = rotary ("FREQ"))
                {
                    checkNear (freq->getRange().getStart(), r.freqLow, 0.5,
                               juce::String (who) + " FREQ's bottom on node " + r.name);
                    checkNear (freq->getRange().getEnd(), r.freqHigh, 0.5,
                               juce::String (who) + " FREQ's top on node " + r.name);
                }

                if (const auto* q = rotary ("Q"))
                    checkNear (q->getRange().getEnd(), r.qHigh, 0.01,
                               juce::String (who) + " Q's top on node " + r.name);

                // And the knob is actually holding that node's parameter: move
                // the parameter, and the knob under the caption follows it.
                params.setReal (r.freqIndex, r.node == R::EqNode::high ? 4000.0f : 400.0f);

                if (const auto* freq = rotary ("FREQ"))
                    checkNear (freq->getValue(), r.node == R::EqNode::high ? 4000.0 : 400.0, 1.0,
                               juce::String (who) + " FREQ is not bound to node " + r.name);

                params.setReal (r.gainIndex, -7.5f);

                if (const auto* gain = rotary ("GAIN"))
                    checkNear (gain->getValue(), -7.5, 0.05,
                               juce::String (who) + " GAIN is not bound to node " + r.name);

                resetEq();
            }

            reverbPanel->setNode (R::EqNode::low);
        }

        //-- Each node is the control it says it is, and only that control ----
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
            // and centre reaches further out.
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
        // **The claim the mode makes, as three separate assertions.** "The curve
        // changed" is also true of a change that broke the bell, so node 2 is
        // checked for exact equality across the mode and the outer two for the
        // sign of what they do.
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

            // **GAIN greys out and the parameter keeps its value.** Both halves,
            // because a mode that reset a knob to make the greying "true" would
            // also pass a check on the greying alone. **Read through
            // `knobFace`**: `setKnobEnabled` disables the rotary *inside* the
            // component -- that is what the look and feel draws dimmed -- while
            // the wrapper stays enabled so its caption still lays out.
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

            // **The greying follows the node the knobs are on, one node at a
            // time** -- which is what the node selector changed about it. There
            // was one GAIN knob per shelf and both greyed independently; there
            // is one GAIN now, so the question is only ever about the node it is
            // showing.
            reverbPanel->setNode (R::EqNode::low);
            check (live ("GAIN") == 0,
                   who + " Bandpass should grey GAIN on node 1 -- a cut has no gain");

            reverbPanel->setNode (R::EqNode::mid);
            check (live ("GAIN") == 1,
                   who + " Bandpass should leave node 2's GAIN alone -- a bell is a bell");

            reverbPanel->setNode (R::EqNode::high);
            check (live ("GAIN") == 0,
                   who + " Bandpass should grey GAIN on node 3 too");

            // FREQ and Q carry over into filter mode, so they stay live: a cut
            // has a corner and a resonance and the same two knobs set them.
            check (live ("FREQ") == 1 && live ("Q") == 1,
                   who + " FILTER should leave FREQ and Q alone");

            checkNear (params.getReal (R::Index::eqlo), 6.0, 1.0e-4,
                       who + " FILTER must not write the shelf gain it is ignoring");

            // **One position at a time**, which is what the four positions buy
            // the panel: Lo Cut greys node 1 and leaves node 3 live, and Hi Cut
            // the other way about.
            params.setReal (R::Index::eqfilter, (float) R::eqFilterLoCut);

            reverbPanel->setNode (R::EqNode::low);
            check (live ("GAIN") == 0, who + " Lo Cut should grey node 1's GAIN");
            reverbPanel->setNode (R::EqNode::high);
            check (live ("GAIN") == 1, who + " Lo Cut should leave node 3 a shelf");

            check (screen.readout().contains ("LO CUT") && screen.readout().contains ("HIGH "),
                   who + " the Lo Cut reading should say LO CUT and HIGH, reads '"
                       + screen.readout() + "'");

            params.setReal (R::Index::eqfilter, (float) R::eqFilterHiCut);

            check (live ("GAIN") == 0, who + " Hi Cut should grey node 3's GAIN");
            reverbPanel->setNode (R::EqNode::low);
            check (live ("GAIN") == 1, who + " Hi Cut should leave node 1 a shelf");

            check (screen.readout().contains ("LOW ") && screen.readout().contains ("HI CUT"),
                   who + " the Hi Cut reading should say LOW and HI CUT, reads '"
                       + screen.readout() + "'");

            params.setReal (R::Index::eqfilter, (float) R::eqFilterOff);

            check (live ("GAIN") == 1,
                   who + " switching FILTER off should give the GAIN knob back");

            checkNear (screen.nodeDbAt (R::EqNode::low, 20.0f), (double) shelfLow, 1.0e-4,
                       who + " switching FILTER off should restore the shelf exactly");

            resetEq();
        }

        //-- Four node states, two strokes, and they compose ------------------
        //
        // **A ring says the knobs edit this node; a fill says the node is
        // shaping the sound.** Both were asked for and they are independent, so
        // all four combinations are asserted -- a marker that had folded one
        // into the other would pass any check on either alone.
        {
            resetEq();
            reverbPanel->setNode (R::EqNode::low);

            // Flat everywhere: the selected node is ringed and hollow, the other
            // two are neither.
            for (const auto n : kNodes)
            {
                const auto mark = screen.nodeMark (n);

                checkEquals (mark.ringed ? 1 : 0, n == R::EqNode::low ? 1 : 0,
                             juce::String (who) + " the ring should be on the selected node");
                check (! mark.filled,
                       juce::String (who) + " a node at 0 dB should not be filled");
            }

            // Fill node 3 and leave the ring on node 1: filled without a ring,
            // and ringed without a fill, at the same time.
            params.setReal (R::Index::eqhi, 4.0f);

            check (screen.nodeMark (R::EqNode::low).ringed
                     && ! screen.nodeMark (R::EqNode::low).filled,
                   who + " node 1 should be ringed and hollow -- edited, and doing nothing");
            check (screen.nodeMark (R::EqNode::high).filled
                     && ! screen.nodeMark (R::EqNode::high).ringed,
                   who + " node 3 should be filled and unringed -- working, and not edited");

            // Move the ring onto it and both strokes are on the one marker.
            reverbPanel->setNode (R::EqNode::high);
            check (screen.nodeMark (R::EqNode::high).ringed
                     && screen.nodeMark (R::EqNode::high).filled,
                   who + " node 3 should be ringed and filled once the knobs are on it");

            // **A cut is active whatever its greyed GAIN reads**, which is the
            // one case a gain comparison gets wrong: the knob is dimmed, the
            // parameter may be sitting at 0, and a 24 dB low cut is very much
            // doing something.
            resetEq();
            params.setReal (R::Index::eqfilter, (float) R::eqFilterLoCut);

            checkNear (params.getReal (R::Index::eqlo), 0.0, 1.0e-4,
                       who + " this case needs node 1's gain at zero to mean anything");
            check (screen.nodeIsActive (R::EqNode::low),
                   who + " a low cut at 0 dB of gain reads as a node at rest, and a cut has"
                         " no gain to be at rest at");
            check (! screen.nodeIsActive (R::EqNode::mid),
                   who + " FILTER made the bell active, and FILTER does not touch the bell");

            resetEq();
            reverbPanel->setNode (R::EqNode::low);
        }

        //-- Three marked nodes and a curtain, for the four controls ----------
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

            // The three markers land on the curve in frequency order, and inside
            // the plot with their rings on.
            const auto plot = screen.plotBounds();
            auto previousX = plot.getX() - 1.0f;

            for (const auto n : kNodes)
            {
                const auto mark = screen.nodeMark (n);
                const auto reach = mark.radius + R::LingerScreen::kNodeRingGap;

                check (mark.centre.x > previousX,
                       who + " the EQ markers are not in frequency order across the plot");
                previousX = mark.centre.x;

                check (plot.expanded (reach).contains (mark.centre),
                       who + " an EQ marker at " + mark.centre.toString()
                           + " is outside the plot " + plot.toString());
            }

            // The reading is of the three EQ nodes, and **it names the mode**:
            // LOW / HIGH as shelves, LO CUT / HI CUT as filters.
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

        if (auto* f = dynamic_cast<bmo::ui::Fader*> (c))
            return "   margin " + juce::String (-f->captionOverflow(), 1);

        // BMO Linger's segmented rows. The room is a segment and not a cell,
        // and the words are the widest thing in either row -- this is the print
        // the segment width was chosen from.
        if (auto* s = dynamic_cast<bmo::reverb::Segments*> (c))
        {
            juce::String out;

            for (int i = 0; i < s->numSegments(); ++i)
                out += "   margin " + juce::String (-s->labelOverflow (i), 1);

            return out;
        }

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
        box ("segment", paged->getSegmentBox());
        box ("cluster", paged->getClusterBox());
        box ("strip  ", paged->getStripBox());

        // The FILTER ring's cap, which is the number the 79 px cluster row was
        // derived from and the one that came out at 23 when the band was sized
        // by its cell. `ConcentricBand::capDiameter` reads the face that is
        // actually drawn, so this print cannot flatter it.
        std::cout << "   filter cap  " << juce::String (paged->getFilterRing().capDiameter(), 2)
                  << "   box " << paged->getFilterRing().getBounds().toString() << '\n';

        // The menu inside the display: three segments and the room each word
        // has in its own one.
        for (const auto p : { bmo::reverb::Page::early, bmo::reverb::Page::tail,
                              bmo::reverb::Page::eq })
            std::cout << "   menu " << bmo::reverb::LingerScreen::menuLabel (p)
                      << "   " << paged->getScreen().menuSegment (p).toString()
                      << "   margin "
                      << juce::String (-paged->getScreen().menuLabelOverflow (p), 1) << '\n';
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
        { "deesser", +[] () -> std::unique_ptr<juce::AudioProcessor> { return createDeesser(); } },
        { "fetcomp", +[] () -> std::unique_ptr<juce::AudioProcessor> { return createFetcomp(); } },

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

    // BMO Defang: a shape pair and a meter row over choice state, a band
    // sketch that no other check can see, and LISTEN -- momentary, with no
    // parameter behind it. Its own panel, because this one moves parameters.
    withPanel (named ("deesser"), [] (bmo::ui::ModulePanel& panel)
    {
        checkEquals (panel.getWidth(), 260, "deesser panel width");
        checkDeesserPanel (panel, "deesser");

        // It takes neither shared section -- one de-esser, one section, so no
        // rules, the same argument BMO Opto and LTV Comp make. It also has no
        // trim knob to put in one: a band cut takes under a dB of broadband
        // energy, so there is no makeup to give back.
        check (panel.getRules().empty(),
               "deesser should have no section rules, has "
                   + juce::String ((int) panel.getRules().size()));
    });

    // BMO FET: three rows of switches over three choice parameters, none of
    // which toggles, plus the meter row at the full switch width. Its own
    // panel, because this one moves parameters.
    withPanel (named ("fetcomp"), [] (bmo::ui::ModulePanel& panel)
    {
        checkFetcompSwitches (panel, "fetcomp");

        // It takes neither shared section -- one compressor, one section, so
        // no rules, the same argument BMO Opto and LTV Comp make.
        check (panel.getRules().empty(),
               "fetcomp should have no section rules, has "
                   + juce::String ((int) panel.getRules().size()));
        check (findNamed (panel, "ATTACK") != nullptr && findNamed (panel, "RELEASE") != nullptr,
               "fetcomp has its ATTACK and RELEASE knobs");
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

    //== Textured knob forms ================================================
    //
    // Frosty, 2026-09-25: input, output and volume are one-piece; every other
    // knob is one-piece at BMO FET's ATTACK and RELEASE size or smaller and
    // ringed above it. The size rule decides without a tag, so these hold it
    // to the three things that could go wrong without anyone noticing: a
    // trim that lost its tag, a knob that changes form between a module's two
    // widths, and a knob sitting so close to the line that a pixel of relayout
    // would flip it.
    {
        using Form = bmo::ui::Knob::TexturedForm;
        const auto formName = [] (Form f) { return f == Form::ringed ? "ringed" : "one-piece"; };

        std::map<juce::String, std::map<juce::String, Form>> formsByProduct;

        for (const auto& product : all)
            withPanel (product, [&] (bmo::ui::ModulePanel& panel)
            {
                std::vector<std::pair<juce::String, bmo::ui::Knob*>> knobs;
                collectTexturedKnobs (panel, knobs);

                for (const auto& [name, knob] : knobs)
                {
                    const auto who = juce::String (product.who) + " " + name;
                    const auto form = bmo::ui::texturedFormFor (*knob);
                    formsByProduct[product.who][name] = form;

                    if (name == "INPUT" || name == "OUTPUT" || name == "VOLUME")
                        check (form == Form::onePiece, who + " is one-piece: input, output and volume always are");

                    if (knob->getTexturedForm() == Form::automatic)
                    {
                        const auto r = bmo::ui::capRadiusOf (*knob);
                        check (std::abs (r - bmo::ui::Tokens::onePieceMaxRadius) >= 0.25f,
                               who + "'s cap is " + juce::String (r, 2) + " px, within a quarter pixel of the "
                                   + juce::String (bmo::ui::Tokens::onePieceMaxRadius, 1)
                                   + " px one-piece line -- move it, or tag it");
                    }
                }
            });

        for (const auto* name : { "ATTACK", "RELEASE" })
            check (formsByProduct["fetcomp"][name] == Form::onePiece,
                   juce::String ("fetcomp ") + name + " is one-piece: it is the size the rule is written against");

        const auto& full    = formsByProduct["deq"];
        const auto& compact = formsByProduct["deq compact"];

        check (! full.empty() && full.size() == compact.size(), "deq shows the same knobs at both widths");

        for (const auto& [name, form] : full)
            if (const auto other = compact.find (name); other != compact.end())
                check (other->second == form,
                       "deq " + name + " is " + formName (form) + " expanded and "
                           + formName (other->second) + " compact -- a knob keeps its form across widths");
    }

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
