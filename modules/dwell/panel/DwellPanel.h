#pragma once

#include "core/product/ModuleDef.h"
#include "core/ui/ExpandButton.h"

namespace bmo::dwell
{

class ChoiceRow;

/** BMO Dwell's panel: an eleven-control face, and a second column that opens
    beside it.

    **This is the settled structure** (Frosty, 2026-09-21), after three
    attempts that were rejected for being too busy. `docs/delay/13` §2 still
    draws the first of them and is not rewritten until everything else here is
    settled, so this file and that document disagree on purpose.

    ## What the split is, and what it is not

    It is a **visibility** split and nothing else. Every parameter stays live
    and is read by the DSP at all times, whichever column it is drawn in --
    and that now includes the thirteen this panel does not draw at all. There
    is no gate and no `enabled` bool. A control being out of sight never means
    a stage is switched off, which is the one thing a hidden section must not
    be allowed to imply.

    ## What this panel does not draw yet

    `params.h` carries **thirty-two** parameters from 2026-09-21
    (docs/delay/15): CHOP, LINK and the lane's twelve. They are live, saved,
    automatable and reachable from a host's generic view; the panel that draws
    them is the 980 px redesign, which is its own pass. Until then this file is
    the settled 560 px panel with VOICE removed, the performance pair renamed
    SEND and HOLD, the FX grid at four types, and the lane's TAIL where the old
    three-way THROW MODE stood.

    ## The face -- eleven controls

    CHARACTER, then TIME (with SYNC, and NOTE sharing TIME's position),
    FEEDBACK, MIX, LO CUT, HI CUT, STEREO, SEND, HOLD and FX.

    Eleven, because 280 px is a rack strip rather than a full-width device.
    The field's default-visible counts cluster between fourteen and twenty-five
    with a median near eighteen, and every one of those is a horizontal box
    three or four times this wide; carrying eighteen down a 280 px column is
    what made all three earlier attempts read as dense. Eleven puts this panel
    at BMO Saturator's density rather than at four times it.

    **CHARACTER is the first row, above the DELAY rule.** CLEAN / TAPE /
    BUCKET voices the repeats, the loop and whatever the FX section is doing to
    them. Under the rule it read as one more row of the DELAY section, a
    sibling of TIME; above the first rule it is a sibling of nothing and reads
    as governing the panel. Frosty, 2026-09-21: "so users know it affects the
    delay as a whole".

    **TIME is the hero.** One 116 px knob on the column's centre line, with
    SYNC beside it rather than under it, the way BMO Saturator gives DRIVE a
    band of its own. It is a delay; TIME is the control, and a trio of equals
    said it was one of three.

    ## The revealed section -- a width expansion, not an in-place reveal

    DRIVE, MOD RATE, MOD DEPTH, DUCK, TAIL, FX TYPE and FX AMOUNT, in a second
    260 px column: 280 opens to 560, which is two columns and a 20 px gutter.

    **VOICE is gone** (Frosty, 2026-09-21): LO CUT and HI CUT are already
    continuous sweeps and VOICE only added resonance on top of them. Its half
    of the first knob row is not left bare -- DRIVE, RATE and DEPTH are laid
    out as one trio, which is the same shape the face takes when DUCK is
    promoted into it. **TAIL** is the lane's bipolar tail knob, standing where
    the three-way THROW MODE row stood; that parameter is a float now, so a
    ChoiceRow could not have carried it whatever the layout did.

    The alternative was LTV Comp's in-place reveal -- one width, the drawer's
    controls appearing in space the closed state already reserves. It is the
    cheaper mechanism and it is the wrong one **here**, for one reason: the
    reservation. LTV Comp hides five controls under two, so the plate it keeps
    empty is small and reads as margin. This section is eight controls under
    eleven; reserving their height would either leave a third of a 688 px
    strip visibly blank in the state a user spends all their time in, or shrink
    the face's knobs to pay for room nothing is drawn in. A second column costs
    the face nothing, and the persistence it needs -- `ModuleDef::
    expandedWidth` plus the session-only `view` attribute, reached through
    `ui::ModuleContext::setExpanded` -- already exists for BMO DEQ and is
    already wired to a rack slot and to the standalone window.

    The column is struck level with the DELAY rule, so the CHARACTER row spans
    both columns and keeps meaning what it means.

    ## The closed affordance says whether anything is hidden

    A closed section whose controls are all at their defaults and one whose
    DRIVE is at 80 % look identical, and that is the measured cost of hiding
    anything. So the arrow carries a **state dot**: a filled disc in the
    module's accent, drawn beside the chevrons while the panel is compact and
    any parameter in the revealed section is away from its default. It is
    painted by `paintPanel`, it is not a parameter, it is not saved and it is
    not automatable -- it is a reading of the seven parameters that already exist.

    ## What is still open, and is deliberately cheap to move

    **DUCK's home.** `kDuckHome` in the .cpp is the one line that decides it:
    `revealed` is where it sits today, `face` promotes it into the tone band,
    `none` drops it from the panel. Nothing else in `DwellPanel.cpp` decides
    where DUCK goes.

    Hiding it used to be the objectionable part, because DUCK defaulted to
    4 dB and was the one control here whose default was not its inert end --
    a hidden control doing something nobody had asked for. **Its default is
    0 dB from 2026-09-21** (DECIDED, Frosty; `params.h` id 12,
    `docs/delay/10` §6), so it ships inert like everything beside it and the
    objection is gone. The state dot covers the rest: move DUCK off 0 with the
    section closed and the arrow says so.

    **The accent** is `0xfff288eb` in `Module.cpp` and is a placeholder Frosty
    has not picked yet; one candidate has already been rejected. **The lit
    glow** behind THROW, FREEZE and FX is also pending. Neither is touched
    here. Nothing in this file names a colour: everything coloured comes off
    `context.def.accent` through the `ui::` derivations.

    No control is labelled DWELL, and nothing on the panel names a piece of
    hardware.
*/
class DwellPanel final : public ui::ModulePanel,
                         private juce::Timer
{
public:
    explicit DwellPanel (ui::ModuleContext);
    ~DwellPanel() override;

    void resized() override;

    /** True when the host has given this panel its wide width. The same test
        BMO DEQ's panel makes, and the only signal either needs. */
    bool isShowingExpanded() const noexcept;

    /** A click anywhere on the panel, heard through a mouse listener on every
        child: the FX switch's own click is what opens the column the first
        time, and a mouse event is the one place a *user* action can be told
        apart from a parameter arriving from the host. */
    void mouseUp (const juce::MouseEvent&) override;

private:
    void paintPanel (juce::Graphics&) override;
    void timerCallback() override;

    /** TIME and NOTE share one position, so only one of them is ever a child:
        the other is removed rather than hidden, because a knob with no bounds
        still fails the caption-fit assertion in tests/ui/LayoutTests.cpp. */
    void showNote (bool sync);

    /** Adds or removes the revealed section's controls as a whole, for the
        same reason: a control left parented with no bounds passes every
        overlap check and fails no caption check while being invisible. */
    void showRevealed (bool);

    /** FX AMOUNT is rebuilt when the type changes, because its caption names
        what the percent moves -- `AMOUNT (SMEAR)`, `(BITS)`, `(SEAM)` -- and a
        PlainKnob's caption is fixed at construction. BMO DEQ rebuilds a band's
        knobs the same way. */
    void buildFxAmount (int type);

    void refreshFxEnablement();

    /** Whether any parameter the revealed section carries is away from its
        spec default. What the arrow's state dot reads; see the class comment.
        It reads parameters and holds none of its own. */
    bool revealedSectionIsMoved() const;

    /** Asks the host for the other width. Never called from a parameter
        callback -- see core/ui/ModulePanel.h's view rule. */
    void requestExpanded (bool expanded);

    //== The two compositions, one each ========================================
    //
    // The face reports back the two lines its rules are struck on, and the
    // revealed column is laid out between them: DELAY and LOOP share the
    // first, TONE and FX share the second. Two columns whose rules miss each
    // other by a dozen pixels read as a mistake rather than as two sections.
    void layOutFace     (juce::Rectangle<int> column, int& delayRuleTop, int& toneRuleTop);
    void layOutRevealed (juce::Rectangle<int> column, int toneRuleTop);

    //== The bands they are built from ========================================
    void placeHero      (juce::Rectangle<int> row);
    void placePair      (ui::PlainKnob& left, ui::PlainKnob& right,
                         juce::Rectangle<int> row, int knobSide);
    void placeTrio      (ui::PlainKnob& left, ui::PlainKnob& middle, ui::PlainKnob& right,
                         juce::Rectangle<int> row, int knobSide);
    /** One knob alone on its row, centred on the column: the lane's TAIL, which
        has no sibling now that THROW MODE's three cells are one float. */
    void placeSingle    (ui::PlainKnob& knob, juce::Rectangle<int> row, int knobSide);
    void placePerform   (juce::Rectangle<int> row);
    void placeSwitchRow (ChoiceRow&, juce::Rectangle<int> row);
    void placeDuckBand  (juce::Rectangle<int> row);
    void placeFoot      (juce::Rectangle<int> row);
    void placeFxGrid    (juce::Rectangle<int> row);

    // Bands `resized` works out and `paintPanel` draws in. A rule and a knob
    // caption are the only text the shared controls place for themselves;
    // everything else is the panel's own and has to be measured where the
    // layout is, not where the paint is.
    juce::Rectangle<int> mixNoteBand, feedbackNoteBand;
    juce::Rectangle<int> duckMeterCaption;
    juce::Rectangle<int> stateDotSpot;      ///< where the arrow's dot goes

    // The face.
    ui::PlainKnob time, note, feedback, mix, lowCut, highCut;
    ui::SwitchButton sync, sendHeld, hold, fx;
    std::unique_ptr<ChoiceRow> character, stereo;
    ui::ExpandButton arrow;

    // The revealed section. `laneGain` is captioned TAIL: the parameter names
    // what it is in a host's list, the caption names what it does under a
    // LANE rule (a panel's words are not its schema).
    ui::PlainKnob drive, modRate, modDepth, duck, laneGain;
    std::unique_ptr<ChoiceRow> fxType;
    std::unique_ptr<ui::PlainKnob> fxAmount;

    class DuckMeter;
    std::unique_ptr<DuckMeter> duckMeter;

    int  fxAmountType = -1;
    bool lastSyncWasOn = false;
    bool lastFxWasOn = false;
    bool lastMovedWasSet = false;
    bool revealedShown = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DwellPanel)
};

} // namespace bmo::dwell
