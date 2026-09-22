#pragma once

#include "core/product/ModuleDef.h"
#include "core/ui/ExpandButton.h"

namespace bmo::dwell
{

class ChoiceRow;

/** BMO Dwell's panel: a nine-control face, and a three-column reveal that
    carries a complete second delay beside it.

    ## The face -- nine controls, and nothing else

    CHARACTER above the DELAY rule; TIME with SYNC beside it; FEEDBACK and MIX;
    STEREO; LO CUT and HI CUT under the TONE rule; and FX at the foot.

    **SEND and HOLD came off it** (Frosty, 2026-09-21): "send does nothing if
    no hold is applied, and hold is a complex control". They are the lane's
    gates, they only mean anything once there is a lane, and a pair of
    accent-lit buttons on the face made the plain delay look like a
    performance instrument. The face is the plain delay now: put it in, set a
    time, set a feedback, set a mix. Nine controls in 280 px is 3.2 per 100 px,
    a shade calmer than the eleven-control face's 3.9 and than BMO Saturator's
    3.5, and the row the pair used to take goes to **FX**, which is a band of
    its own now rather than a switch hung off the foot beside the arrow.

    ## The reveal -- three columns, 980 px

    280 opens to 980: the 260 px face column, a 260 px column for the main
    delay's depth, and a **400 px column carrying the lane**, with 20 px
    gutters. `modules/dwell/Module.cpp` carries the arithmetic and the argument
    against the 1120 four-column fallback.

    - **The depth column** is the main delay below the surface: DRIVE, then MOD
      RATE and MOD DEPTH, then DUCK with its GR bar, then the FX stage -- the
      2x2 type grid and AMOUNT, whose caption names what the percent moves.
    - **The lane column** is a second delay and is composed as one: its gates
      (SEND, HOLD, CHOP), its tail with its own time and level, then its own
      voicing under a VOICE rule -- CHARACTER, STEREO, LO CUT, HI CUT, RATE,
      DEPTH -- then its own FX stage. The words are deliberately the *same*
      words the main delay uses. A mirror that renamed everything would read as
      a bin of leftovers; a mirror that repeats the names reads as what it is,
      which is a second engine.

    **Two reveals are impossible**, whatever a composition might prefer:
    `ModuleDef` carries one `expandedWidth` and the session flag behind it is
    one bool. The depth and the lane open together or not at all.

    ## What the rules do across three columns

    All three columns strike their first rule on **one line** -- DELAY, LOOP
    and LANE at row 77 -- which is what says they are three parts of one
    instrument rather than three panels that happen to be adjacent. Below that
    they diverge on purpose: the face and the depth column share their second
    line (TONE and FX at 460, the cut the depth column is laid out in two
    segments to make exact), and the lane runs its own rhythm under its own
    rules, because a lane rule struck a dozen pixels off a face rule reads as a
    failed alignment rather than as two sections. The lane's VOICE and FX rules
    land clear of both shared lines rather than near them.

    ## The lane gain knob

    `lane_gain` is bipolar, -100..+100, and **0 is exact unity**: below it the
    caught word decays, at it the word holds, above it the word builds. One
    loop gain with three regions rather than three modes, which is why the
    parameter is a float and this is a knob.

    It carries two things nothing else on the panel has:

    - **A catch at the centre** (`ui::Knob::setCatch`), so unity is findable by
      hand. Dragging only, and opt-in -- typed entry, the mouse wheel, the
      arrow keys, automation and preset recall are all untouched, and so is
      every other control in the suite.
    - **A region caption** under it, naming THROW, FREEZE and BUILD with the
      one the knob is in lit and the other two dim. It says where the control
      *is*, and -- before you touch it -- that there are three places to be.

    ## LINK, and what it looks like when it is on

    LINK ties the lane's voicing to the main delay's, and it **defaults on**: a
    fresh instance is one delay with one set of controls. It is drawn at the
    right-hand end of the lane's VOICE rule, which is the row the six controls
    it governs hang under.

    While it is on, those six are drawn **following rather than dead**: they
    keep their positions, their captions and their full size, and what changes
    is the colour -- the module accent stepped back toward the hairline, so
    they read as quieter than the lane's own controls without taking the
    disabled dim, which in this suite means "this stage is off" and would be a
    lie. The VOICE legend reads **VOICE - FOLLOWS MAIN**, and an accent
    hairline brackets the band and runs left toward the column they are
    following. Nothing is disabled and nothing is hidden.

    **Seeding on unlink is deliberately not implemented here.** See
    `seedLaneOnUnlink`.

    ## The closed affordance says whether anything is hidden

    The arrow carries a **state dot**: a filled disc in the module's accent,
    drawn beside the chevrons while the panel is compact and any parameter the
    reveal carries is away from its default. It reads **all twenty-three** of
    them now -- the depth column's six and the lane's sixteen -- rather than
    the seven it read while the lane was undrawn. It is painted by
    `paintPanel`, it is not a parameter, it is not saved and it is not
    automatable.

    ## What is not touched here

    The accent is `0xfff094e6` in `Module.cpp`; the lit glow behind SEND, HOLD,
    CHOP, LINK and the two FX gates is what it was. Captions are 15 pt, the
    suite default (`core/ui/Controls.h`). Nothing in this file names a colour.

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
        child: the FX switch's own click is what opens the columns the first
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

    /** Adds or removes both revealed columns as a whole, for the same reason:
        a control left parented with no bounds passes every overlap check and
        fails no caption check while being invisible. */
    void showRevealed (bool);

    /** An FX AMOUNT knob is rebuilt when its type changes, because its caption
        names what the percent moves -- `AMOUNT (SMEAR)`, `(BITS)`, `(SEAM)` --
        and a PlainKnob's caption is fixed at construction. BMO DEQ rebuilds a
        band's knobs the same way. There are two FX stages now, so this takes
        which one it is building for. */
    void buildFxAmount (bool lane, int type);

    void refreshFxEnablement();

    /** Draws the lane's six voicing controls as following the main delay, or
        as the lane's own. See the class comment; called from the timer when
        LINK moves, and once from the constructor. */
    void refreshLinkFollowing();

    /** **HOOK -- deliberately empty.**

        Unlinking should seed the lane's voicing from the main delay's current
        values, so that switching LINK off changes nothing audible until
        something is turned (docs/delay/15). That seeding is a **UI gesture and
        not a side effect of the parameter**: `link` is automatable, and doing
        it on the parameter's own change would rewrite six parameters on every
        automation pass and fight the user's own lanes.

        Which leaves the question of what a *gesture* is here -- a click on the
        switch is one, a preset arriving with LINK off is not, a host flipping
        it from a generic view is somewhere in between -- and that is the spec
        pass's call rather than this one's. So this is the one place the answer
        will go, and it is empty on purpose. It is called from `mouseUp` on the
        LINK switch's own click, which is the narrowest touch point there is;
        nothing else calls it. */
    void seedLaneOnUnlink();

    /** Whether any parameter the reveal carries is away from its spec default.
        What the arrow's state dot reads; see the class comment. It reads
        parameters and holds none of its own. */
    bool revealedSectionIsMoved() const;

    /** Which of THROW / FREEZE / BUILD the lane gain knob is in: -1, 0 or +1.
        One definition, read by the paint that letters the region caption and
        by the timer that decides whether it has to be redrawn. */
    int laneRegion() const;

    /** Asks the host for the other width. Never called from a parameter
        callback -- see core/ui/ModulePanel.h's view rule. */
    void requestExpanded (bool expanded);

    //== The three compositions, one each =====================================
    //
    // The face reports back the two lines its rules are struck on. The depth
    // column is laid out *between* them -- LOOP shares the first, FX shares
    // the second -- and the lane column shares only the first and then runs
    // its own rhythm. See the class comment on why those are two different
    // answers rather than an inconsistency.
    void layOutFace  (juce::Rectangle<int> column, int& delayRuleTop, int& toneRuleTop);
    void layOutDepth (juce::Rectangle<int> column, int toneRuleTop);
    void layOutLane  (juce::Rectangle<int> column, juce::Rectangle<int> topBand);

    //== The bands they are built from ========================================
    void placeHero      (juce::Rectangle<int> row);
    void placePair      (ui::PlainKnob& left, ui::PlainKnob& right,
                         juce::Rectangle<int> row, int knobSide);
    void placeQuad      (ui::PlainKnob& a, ui::PlainKnob& b, ui::PlainKnob& c,
                         ui::PlainKnob& d, juce::Rectangle<int> row, int knobSide);
    /** One knob alone on its row, centred on the column. */
    void placeSingle    (ui::PlainKnob& knob, juce::Rectangle<int> row, int knobSide);
    void placeGates     (juce::Rectangle<int> row);
    void placeSwitchRow (ChoiceRow&, juce::Rectangle<int> row);
    void placeDuckBand  (juce::Rectangle<int> row);
    void placeFxBand    (juce::Rectangle<int> row);
    void placeFoot      (juce::Rectangle<int> row);

    /** The lane's tail row: its gain knob between its time and its level.

        Laid out by hand rather than through a shared trio, because the three
        are not the same size and `PlainKnob` centres its knob in its own box:
        left to itself, an 86 px knob beside a 116 px one puts its caption
        fifteen pixels higher than its neighbour's, which reads as a
        misalignment rather than as a hierarchy. Each box is cut so all three
        captions land on one line. */
    void placeLaneTail  (juce::Rectangle<int> row);

    /** A rule with one or two switches at its right-hand end: the lane's VOICE
        rule carrying LINK, and its FX rule carrying the lane's FX gate *and*
        FX LINK. The rule is struck across what is left, so a switch reads as
        belonging to the section rather than to the first row under it.

        **`rightmost` is always the link**, on both rules, so the last thing on
        a rule is always the same kind of thing. `inner` sits to its left. */
    void placeRuledSwitch (juce::Rectangle<int> band, const juce::String& legend,
                           ui::SwitchButton& rightmost,
                           ui::SwitchButton* inner = nullptr);

    // Bands `resized` works out and `paintPanel` draws in. A rule and a knob
    // caption are the only text the shared controls place for themselves;
    // everything else is the panel's own and has to be measured where the
    // layout is, not where the paint is.
    juce::Rectangle<int> mixNoteBand, feedbackNoteBand;
    juce::Rectangle<int> duckMeterCaption;
    juce::Rectangle<int> laneRegionBand;    ///< THROW / FREEZE / BUILD
    juce::Rectangle<int> linkTieBand;       ///< the bracket drawn while LINK is on
    juce::Rectangle<int> fxLinkTieBand;     ///< and the one drawn while FX LINK is
    juce::Rectangle<int> stateDotSpot;      ///< where the arrow's dot goes

    // The face.
    ui::PlainKnob time, note, feedback, mix, lowCut, highCut;
    ui::SwitchButton sync, fx;
    std::unique_ptr<ChoiceRow> character, stereo;
    ui::ExpandButton arrow;

    // The depth column: the main delay below the surface.
    ui::PlainKnob drive, modRate, modDepth, duck;
    std::unique_ptr<ChoiceRow> fxType;
    std::unique_ptr<ui::PlainKnob> fxAmount;

    // The lane column. `laneGain` is captioned TAIL -- the parameter names
    // what it is in a host's list, the caption names what it does under a LANE
    // rule, and a panel's words are not its schema. The rest take the main
    // delay's own words and are told apart by their **component names**, which
    // is what `findNamed` in tests/ui/LayoutTests.cpp walks: two children
    // called TIME would resolve by child order, and child order here changes
    // every time the reveal opens.
    ui::SwitchButton sendHeld, hold, chop, link, laneFx, fxLink;
    ui::PlainKnob laneGain, laneTime, laneLevel, laneLowCut, laneHighCut,
                  laneModRate, laneModDepth;
    std::unique_ptr<ChoiceRow> laneCharacter, laneStereo, laneFxType;
    std::unique_ptr<ui::PlainKnob> laneFxAmount;

    class DuckMeter;
    std::unique_ptr<DuckMeter> duckMeter;

    int  fxAmountType = -1;
    int  laneFxAmountType = -1;
    int  lastLaneRegion = 0;
    bool lastSyncWasOn = false;
    bool lastFxWasOn = false;
    bool lastLaneFxWasOn = false;
    bool lastLinkWasOn = false;
    bool lastFxLinkWasOn = false;
    bool lastMovedWasSet = false;
    bool revealedShown = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DwellPanel)
};

} // namespace bmo::dwell
