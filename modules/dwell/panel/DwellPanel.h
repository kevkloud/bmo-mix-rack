#pragma once

#include "core/product/ModuleDef.h"
#include "core/ui/ExpandButton.h"

namespace bmo::dwell
{

class ChoiceRow;

/** BMO Dwell's panel: a nine-control face, and a reveal that carries the main
    delay's depth and the throw lane beside it.

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

    **The face did not change when the lane was cut back**, and that is worth
    saying rather than assuming: CHARACTER, STEREO, LO CUT and HI CUT govern
    both engines from 2026-09-22, so these nine controls are now nine controls
    over a delay *and* a lane. Not one of them moved.

    ## The reveal -- three even columns, 840 px

    280 opens to 840: the 260 px face column, a 260 px column for the main
    delay's depth, and a 260 px column for the lane, with 20 px gutters.
    `modules/dwell/Module.cpp` carries the arithmetic, why the lane no longer
    needs the 400 px it had, and why a two-column reveal cannot hold the
    sixteen controls behind the arrow.

    - **The depth column** is the main delay below the surface: DRIVE, then MOD
      RATE and MOD DEPTH, then DUCK with its GR bar, then the FX stage -- the
      three type cells across a row and AMOUNT, whose caption names what the
      percent moves.
    - **The lane column** is what a throw needs and nothing else: its gates
      (SEND, HOLD, CHOP) above the rule, its tail with the region caption under
      it, its own TIME and LEVEL, then its own FX stage. It is a section now
      rather than a second instrument, because the lane runs the main delay's
      voicing -- and a column with no voicing block in it is a column the width
      of every other one.

    **Two reveals are impossible**, whatever a composition might prefer:
    `ModuleDef` carries one `expandedWidth` and the session flag behind it is
    one bool. The depth and the lane open together or not at all.

    ## What the rules do across three columns

    All three columns strike their first rule on **one line** -- DELAY, LOOP
    and LANE at row 77 -- and their second on **another** -- TONE, the depth
    column's FX and the lane's FX at 460. That is the whole grid, and it is
    exact by construction rather than by arithmetic: the depth column and the
    lane are each laid out in two segments cut on the face's TONE line, so a
    row height changing anywhere cannot move a rule a few pixels off its
    neighbours.

    A few pixels off is the failure worth designing against. The lane ran its
    own rhythm, on lines deliberately *clear* of the shared one, while it was a
    second engine whose nine bands would not sit on that grid; with the voicing
    gone its content does sit on it, and sharing the line is what says the
    three columns are three parts of one instrument.

    ## The lane gain knob

    `lane_gain` is bipolar, -100..+100, and **0 is exact unity**: below it the
    caught word decays, at it the word holds, above it the word builds. One
    loop gain with three regions rather than three modes, which is why the
    parameter is a float and this is a knob.

    It is alone on its row now, with the lane's own TIME and LEVEL paired under
    it. Three across a 260 px column would cut the hero's cell to 86 px and it
    draws 116, and the lane has height to spare -- so the composition says what
    is true, which is that the tail is the lane's one hero and the two knobs
    under it are its supporting pair.

    It carries two things nothing else on the panel has:

    - **A catch at the centre** (`ui::Knob::setCatch`), so unity is findable by
      hand. Dragging only, and opt-in -- typed entry, the mouse wheel, the
      arrow keys, automation and preset recall are all untouched, and so is
      every other control in the suite.
    - **A region caption** under it, naming THROW, FREEZE and BUILD with the
      one the knob is in lit and the other two dim. It says where the control
      *is*, and -- before you touch it -- that there are three places to be.

    ## FX LINK, and what it looks like when it is on

    FX LINK ties the lane's FX trio to the main delay's, and it **defaults on**:
    a fresh instance is one delay with one set of controls. It is the only link
    left -- the voicing LINK went with the six parameters it tied -- and it is
    drawn at the right-hand end of the lane's FX rule, which is the row the
    three controls it governs hang under.

    While it is on, those three are drawn **following rather than dead**: they
    keep their positions, their captions and their full size, and what changes
    is the colour -- the module accent stepped back toward the hairline, so
    they read as quieter than the lane's own controls without taking the
    disabled dim, which in this suite means "this stage is off" and would be a
    lie. The rule's legend says so in words, and an accent hairline brackets
    the band and runs left toward the column it is following. Nothing is
    disabled and nothing is hidden.

    **Nothing is seeded when it is switched off.** The voicing link had a
    `seedLaneOnUnlink` hook, because unlinking six controls that had been
    following had to leave them where the main delay had them or the sound
    would jump. A plain tie over three controls the user is reaching for anyway
    has nothing to seed, so the hook and its mouse-up touch point went with it.

    ## The closed affordance says whether anything is hidden

    The arrow carries a **state dot**: a filled disc in the module's accent,
    drawn beside the chevrons while the panel is compact and any parameter the
    reveal carries is away from its default. It reads **all sixteen** of them --
    the depth column's six and the lane's ten. It is painted by `paintPanel`,
    it is not a parameter, it is not saved and it is not automatable.

    ## What is not touched here

    The accent is `0xfff094e6` in `Module.cpp`; the lit glow behind SEND, HOLD,
    CHOP, FX LINK and the two FX gates is what it was. Captions are 15 pt, the
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
        band's knobs the same way. There are two FX stages, so this takes which
        one it is building for. */
    void buildFxAmount (bool lane, int type);

    void refreshFxEnablement();

    /** Draws the lane's FX trio as following the main delay's, or as the
        lane's own. See the class comment; called from the timer when FX LINK
        moves, and once from the constructor. */
    void refreshFxLinkFollowing();

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
    // The face reports back the two lines its rules are struck on, and the
    // other two columns are laid out *between* them -- LOOP and LANE share the
    // first, both FX rules share the second. Two segments apiece rather than
    // one run, so the grid is exact by construction. See the class comment.
    void layOutFace  (juce::Rectangle<int> column, int& delayRuleTop, int& toneRuleTop);
    void layOutDepth (juce::Rectangle<int> column, int toneRuleTop);
    void layOutLane  (juce::Rectangle<int> column, juce::Rectangle<int> topBand, int toneRuleTop);

    //== The bands they are built from ========================================
    void placeHero      (juce::Rectangle<int> row);
    void placePair      (ui::PlainKnob& left, ui::PlainKnob& right,
                         juce::Rectangle<int> row, int knobSide);
    /** One knob alone on its row, centred on the column. */
    void placeSingle    (ui::PlainKnob& knob, juce::Rectangle<int> row, int knobSide);
    void placeGates     (juce::Rectangle<int> row);
    void placeSwitchRow (ChoiceRow&, juce::Rectangle<int> row);
    void placeDuckBand  (juce::Rectangle<int> row);
    void placeFxBand    (juce::Rectangle<int> row);
    void placeFoot      (juce::Rectangle<int> row);

    /** A rule across its own column only. See the definition.  */
    void addColumnRule  (juce::Rectangle<int> row, const juce::String& legend);

    /** A rule with one or two switches at its right-hand end: the lane's FX
        rule, carrying its own gate *and* FX LINK. The rule is struck across
        what is left, so a switch reads as belonging to the section rather than
        to the first row under it.

        **`rightmost` is the link.** `inner` sits to its left, so the last
        thing on the rule is the tie rather than the gate, and the row reads
        left to right as "this section, this stage, tied to the other one". */
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
    juce::Rectangle<int> fxLinkTieBand;     ///< the bracket drawn while FX LINK is on
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
    // rule, and a panel's words are not its schema. The two knobs under it
    // take the main delay's own words and are told apart by their **component
    // names**, which is what `findNamed` in tests/ui/LayoutTests.cpp walks:
    // two children called TIME would resolve by child order, and child order
    // here changes every time the reveal opens.
    ui::SwitchButton sendHeld, hold, chop, laneFx, fxLink;
    ui::PlainKnob laneGain, laneTime, laneLevel;
    std::unique_ptr<ChoiceRow> laneFxType;
    std::unique_ptr<ui::PlainKnob> laneFxAmount;

    class DuckMeter;
    std::unique_ptr<DuckMeter> duckMeter;

    int  fxAmountType = -1;
    int  laneFxAmountType = -1;
    int  lastLaneRegion = 0;
    bool lastSyncWasOn = false;
    bool lastFxWasOn = false;
    bool lastLaneFxWasOn = false;
    bool lastFxLinkWasOn = false;
    bool lastMovedWasSet = false;
    bool revealedShown = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DwellPanel)
};

} // namespace bmo::dwell
