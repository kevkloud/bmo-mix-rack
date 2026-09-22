#pragma once

#include "core/product/ModuleDef.h"
#include "modules/reverb/dsp/TapTables.h"

#include <array>
#include <vector>

namespace bmo::reverb
{

//==============================================================================
/** Which page the handheld is showing.

    **UI state, not a parameter**, and the distinction is the whole design of
    it. `specs()` is twenty-four with eight spare host lanes; which page
    somebody is looking at is not something a session should carry, not
    something a host should be able to automate, and **not worth a lane even
    now that there are eight of them** -- the 2026-09-21 control-set trim
    bought room for controls, not for UI state. BMO Opto's meter mode and BMO DEQ's selected band are the same
    kind of thing and reach their panels the same way -- through
    `ModulePanel::setUiState`, which is also what makes every page renderable
    headlessly. See `ReverbPanel::setUiState`. */
enum class Page { early = 0, tail, tone };

//==============================================================================
/** The screen: a small dark display inside the bezel, drawing one of three
    pictures depending on the page the handheld is on.

    **Static: no tap, no FFT, no timer, no audio path.** It redraws when one of
    the parameters it draws from moves, and when the page changes, and at no
    other time. It reads neither `AnalyserTap` nor any of `ModuleContext`'s
    meter callbacks, and that is a decision rather than a gap: neither
    `docs/reverb/10-dsp-spec.md` nor `11-integration-and-test-plan.md` asks
    this module for metering at all, and a reverb has no gain reduction to
    report.

    **It draws in the module's accent, not in LCD green.** A second hue on one
    module is the failure the accent audit was run to find -- the cluster's
    knobs drew suite azure under a violet face, and the module read as
    two plugins sharing a slot. The face underneath is `meterFace`, the token a
    needle meter's scale is printed on: a value rather than a hue, dark in both
    appearances, which is what a screen is.

    It draws the taps from `TapTables.h`, which is **the same table the engine
    will play**. 11 section 5 names sketch/DSP drift as the display's one real
    risk and asks for exactly this: one source of truth, plus a layout test
    asserting the sketch's first tap time equals the table's. That assertion is
    what `firstTapTimeMs` is public for, and every other accessor below is
    public for the same reason -- they are numbers the picture is built from
    rather than pixels it happens to land on, so a test can read them without
    rendering anything.

    ## The three pages, and the axis each one needed

    **EARLY -- linear time, 0 to the last tap plus a tenth of the window.** The
    image-source taps as discrete stems from a baseline, at their times and
    their gains. What this replaced was a symmetric envelope mirrored about a
    centre line that bloomed and closed to a point; Frosty rejected it, and why
    it was rejected is why the axis changed with it. The ER window is 7-79 ms
    at the reference size and scales with SIZE, so it is a little over one
    decade wherever it is set -- and a linear axis scaled to the window itself
    shows the *spacing* of the reflections, which is the one thing about a tap
    set worth looking at. A log axis here would crowd the late taps into the
    last fifth, and a fixed window would leave most of the box empty at every
    size but one.

    **TAIL -- logarithmic time, 1 ms to 30 s.** This is the axis argued for
    when the display was one picture, and the argument survives the split
    intact: a fixed 0-500 ms window -- 11 section 5's proposal -- shows the
    bloom beautifully and cannot show a 20 s decay at all, and DECAY reaches
    20 s with a 2.0x damping multiplier over it. The old version did exactly
    that, and most of the box was dead. A linear window wide enough for the
    tail puts the whole 0-120 ms attack bloom inside the first two pixels. On a
    log axis 1-100 ms keeps 45 % of the width, so the bloom and a 20 s tail are
    both legible in one picture. The ends are chosen rather than round: 1 ms is
    where a reflection stops fusing with the direct sound (05 section 1.1), and
    30 s is `bmo::kMaxTailSeconds`, the ceiling on the tail this module -- and
    now the rack it sits in -- will ever report, so the right-hand edge is the
    same number the host is told rather than a second opinion about it.

    **TONE -- logarithmic frequency, 20 Hz to 20 kHz,** which is the only axis
    a frequency response has. Level is linear in dB over +/-`kToneRangeDb`.

    ## What the TONE curve is, and what it is not

    Three nodes in series -- the Reverb EQ's low shelf, its high shelf and the
    input high-cut -- summed in dB and drawn as one curve with a marker on it
    per node, which is how a serial EQ reads.

    **The shelves are drawn first-order and the cut one-pole, and neither is
    claimed to be the shipped filter.** `dsp/DspCore.h` is a marked placeholder
    and 10 section 2 gives the shelves no order, so there is nothing to agree
    with yet; a second-order curve drawn here would be a guess presented as a
    measurement, which is the same fault as an unmarked CALIBRATE number. What
    the picture does promise is the part that is settled: which three controls
    are in series, where their corners sit, and which way each one leans. When
    the filters land, `responseDbAt` is the one place this changes. */
class LingerScreen final : public juce::Component
{
public:
    explicit LingerScreen (juce::Colour accentColour);

    /** Everything the three pictures are drawn from, in the units `specs()`
        uses. One struct rather than fourteen arguments, because the panel
        refreshes all of it at once and a partial update is not a state this
        screen can be in.

        **Thirteen of these come from a parameter and `attack` does not.** It
        is `TypeConstants::attack` for whichever type is selected, since the
        2026-09-21 control-set trim took its knob away -- so TYPE is one of the
        parameters the panel redraws the screen on, and the TAIL page's bloom
        follows a type change with no knob having moved.

        `linkEr` was here and went with `prelink`: ER travel with dry, fixed,
        so the pictures no longer have two cases to draw. */
    struct State
    {
        // EARLY.
        float sizeM        = 12.0f;
        float preDelayMs   = 0.0f;
        float erDensity    = 50.0f;   ///< per cent
        float erLevelDb    = -6.0f;

        // TAIL.
        float decaySeconds = 1.8f;
        float dampLo       = 1.20f;
        float dampHi       = 0.40f;
        float attack       = 30.0f;   ///< per cent of the 0-120 ms bloom, off the type's row
        float verbLevelDb  = -6.0f;

        // TONE.
        float eqLoFreqHz   = 200.0f;
        float eqLoDb       = 0.0f;
        float eqHiFreqHz   = 1600.0f;
        float eqHiDb       = 0.0f;
        float inHiCutHz    = 20000.0f;
    };

    /** Repaints only when something has actually moved. */
    void setState (const State&);

    /** Repaints only when the page has actually changed. */
    void setPage (Page);
    Page getPage() const noexcept { return page; }

    void paint (juce::Graphics&) override;

    //== Arithmetic, public so a test can assert it without rendering ==========

    /** When the first reflection arrives, in milliseconds, at the current
        SIZE. **This is the sketch/DSP drift assertion**: it must equal
        `tapTimeMsAt (kReferenceTaps[0], size)` and it does so by calling it. */
    float firstTapTimeMs() const noexcept;

    /** When the last reflection arrives -- `t_ER,max` in the tail formula. */
    float lastTapTimeMs() const noexcept;

    /** The right-hand edge of the EARLY page's own time axis: the last tap
        with a tenth of the window left after it, so the last stem is a stem
        and not the border. */
    float erWindowMs() const noexcept;

    /** When the tail's first sample arrives: the pre-delay, which is tail-only
        and can never be negative. */
    float tailStartMs() const noexcept { return state.preDelayMs; }

    /** Where the drawn tail reaches -60 dB, in seconds, on its slowest band --
        `decay * max(1, dampLo, dampHi)`, with the pre-delay in front of it. */
    float tailEndSeconds() const noexcept;

    /** How many of the table's taps DENSITY has switched on. The real bridge
        is a continuous ramp over 48 taps with a master sequence that does not
        exist yet (10 section 3); this is the 21 core taps plus the infill the
        knob has paid for, so the picture thickens with the control. */
    int activeTapCount() const noexcept;

    /** The summed response of the three TONE nodes at `hz`, in dB. See the
        class comment for what order of filter this is and why. */
    float responseDbAt (float hz) const noexcept;

    /** The three nodes' corner frequencies, in the order they are drawn:
        EQ LOW, EQ HIGH, IN HI-CUT. */
    std::array<float, 3> nodeFrequencies() const noexcept;

    /** The line of small printed text under the screen, for whichever page is
        showing: the tap count and the ER window, the decay time and where the
        tail ends, or the three crossover points.

        **ASCII only**, like every other string this module prints: the two
        display faces are licensed individually and live outside this
        repository, so a glyph outside ASCII is one this suite cannot promise
        it can draw. Public because the panel paints it -- it belongs to the
        bezel rather than to the screen -- and because a test should be able to
        read what a page says it is showing. */
    juce::String readout() const;

    /** The TAIL page's window, as the class comment argues it. */
    static constexpr float kMinMs      = 1.0f;
    static constexpr float kMaxSeconds = 30.0f;

    /** The bottom of the TAIL page's level axis, in dB. -72 rather than -60,
        so the tail's own -60 point lands inside the box with room under it
        instead of on the floor itself. */
    static constexpr float kFloorDb = -72.0f;

    /** The bottom of the EARLY page's stem axis, in dB, and **it is the ER
        fader's own bottom rather than the tail's floor**.

        The taps span 15 dB between them -- the table runs 0.501 to 0.087 -- so
        on the tail's -72 dB floor the first stem is 83 % of the box and the
        last is 62 %, and a cluster that visibly decays draws as a comb of
        near-equal lines. Against -40 the same set runs 70 % to 33 %, which is
        the 1/d law made visible and is the thing the picture is of.

        -40 rather than a number picked to look right: it is where the ER fader
        stops and reads "Off", so a stem reaching the floor means the same
        thing on the screen as the fader at the bottom of its travel does. */
    static constexpr float kTapFloorDb = -40.0f;

    /** The TONE page's axes. */
    static constexpr float kMinHz       = 20.0f;
    static constexpr float kMaxHz       = 20000.0f;
    static constexpr float kToneRangeDb = 24.0f;

private:
    void paintEarly (juce::Graphics&, juce::Rectangle<float> plot, juce::Colour ink) const;
    void paintTail  (juce::Graphics&, juce::Rectangle<float> plot, juce::Colour ink) const;
    void paintTone  (juce::Graphics&, juce::Rectangle<float> plot, juce::Colour ink) const;

    juce::Colour accent;
    State state;
    Page page = Page::early;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LingerScreen)
};

//==============================================================================
/** One of the three page keys under the screen.

    **Round, and level rather than raked.** Frosty's call, 2026-09-21: a
    handheld is held at an angle and can afford a raked key block, and a mix
    panel is scanned in rows against its neighbours, so a row that sloped would
    be the only thing in the window not lining up with the slot beside it. The
    raked grille at the foot used to be the one sloped thing here; it was cut
    later the same day and nothing on this panel slopes now.

    The circle carries no text and the name sits under it, which is what every
    other control here does -- a knob and its caption, a dropdown and its
    caption. A word set inside a 32 px circle has about 24 px of chord to live
    in, and "EARLY" does not fit that at any size worth reading; the
    alternatives were a much larger key or a much smaller word. The fill is the
    module's accent when its page is selected and `switchOff` -- the raised
    grey every unlit switch in the suite is filled with -- when it is not.

    A `juce::Button` rather than a `ui::SwitchButton`, because it is not a
    switch: there is no parameter under it, the three are a radio set rather
    than three independent toggles, and the panel owns which one is lit. */
class PageButton final : public juce::Button
{
public:
    PageButton (const juce::String& name, juce::Colour accent);

    void paintButton (juce::Graphics&, bool shouldDrawHighlighted, bool shouldDrawDown) override;

    /** Re-colours the key. See `ui::PlainKnob::setAccent`. */
    void setAccent (juce::Colour);

    /** How much wider the name is than the room it has, in pixels; zero or
        less fits.

        Here rather than in the test that asserts on it, because it has to use
        the same box and the same face `paintButton` does.
        `ui::PlainKnob::captionOverflow` is the precedent and carries the
        argument: MAKEUP drew as MAKEU for a full release, and a test that
        measured it its own way could have agreed with the bug. */
    float labelOverflow() const;

    /** The circle itself, inside the component. For a layout test that wants
        to know the key is round rather than merely present. */
    juce::Rectangle<int> dotBounds() const;

    static constexpr int   kDotSide     = 32;
    static constexpr float kCaptionSize = 11.0f;

    /** Room under the circle for its name -- `ui::PlainKnob::captionRow`
        exactly, 1.2 x the point size plus four.

        `constexpr` and public because the panel's own page-row height is the
        circle plus the air plus this, and a second transcription of it there
        is how a key ends up with its name clipped off the bottom of its row. */
    static constexpr int kCaptionRow = (int) (kCaptionSize * 1.2f + 0.5f) + 4;

private:
    juce::Rectangle<int> captionBox() const;

    juce::Colour accentColour;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PageButton)
};

//==============================================================================
/** BMO Linger's panel: a paged handheld.

    A bezelled screen with a line of printed text under it, three page keys, a
    persistent row that is there whatever page you are on, a cluster that
    changes with the page, and a foot holding the two generator levels, MIX and
    the TYPE dropdown. Frosty approved the shape on 2026-09-21, and it replaces
    the compact/expanded two-width split BMO DEQ's precedent had given this
    module.

    ## The speaker grille is gone, and TYPE has its corner

    The grille was texture in the fourth column of the level strip, raked, the
    one sloped thing on a panel whose keys are deliberately level. **It lost
    its job when the corners went square** and it rendered as a flat swatch
    rather than as texture, so it was cut on 2026-09-21 rather than retried.

    **TYPE took the corner it left.** Frosty's call, and it is two arguments at
    once: a dropdown is not knob-shaped, so it never belonged in a grid of
    knobs, and the foot is where two of the nine parameters a type change
    stamps already are. TYPE sits at the right-hand end of the ER / REVERB /
    MIX row, under the LEVEL rule, which is the only wrinkle in the
    arrangement -- TYPE is not a level, and the rule is legended as though it
    were. The alternative was a rule that stopped short of one cell, which
    `ModulePanel::addRule` does not draw and which would have been a second
    kind of rule in the suite for one corner's sake.

    ## One width, and `expandedWidth` is gone

    The old arrangement was 300 compact and 700 expanded, with the same face
    down the left of both and three groups of knobs appearing in the extra
    400 px. **Paging removes the reason for it.** Six, five and six
    controls never need to be on screen at once; what they need is to be
    reachable, and a key under the screen reaches them in one click where the
    expand switch reached them in one click and 400 px. So
    `ModuleDef::expandedWidth` is 0 here, `isExpandable()` is false, the
    standalone header and the rack's slot bar stop offering a switch with
    nothing to switch, and the module is the same panel everywhere -- which is
    the arrangement every module but BMO DEQ already had.

    It costs the width the groups used to take. It was **500** at four columns
    and is **380** at three, since the 2026-09-21 control-set trim took the
    schema from thirty parameters to twenty-four.

    ## The grid, and why three columns at 380

    Three columns of the content width for the knob grid: the persistent row is
    three cells, the cluster is two rows of three, and the page keys take all
    three.

    **380 is not "roughly right", it is the width at which nothing that fitted
    stops fitting.** A panel insets its content by `kPad` = 10 a side, so the
    four-column cell at 500 was (500 - 20) / 4 = 120 px, and the three-column
    cell at 380 is (380 - 20) / 3 = 120 px -- the same number. Every caption on
    this panel was measured against a 120 px cell and still is, "EQ HIGH FREQ"
    at 10 pt included, so the width came down by 120 px with no caption pass
    at all. It is also a multiple of 20, like every other panel in the suite.
    Anything narrower is a caption argument; anything wider is unearned.

    The foot is the exception and holds **four** cells at 90 px, because it
    holds four controls: ER, REVERB, MIX and TYPE. Three there would orphan
    TYPE in a row of its own, which is the one thing this panel does not do.

    ## Two rows on every page, and what that cost

    EARLY is six, TAIL five and TONE six, so the cluster is 3 + 3, 3 + 2 and
    3 + 3 -- **two rows whatever page you are on**, so the block under the keys
    does not change height when the page does, which is the one thing that
    would make paging feel like switching panels rather than turning a page.
    That property is why the old face had four columns at all.

    **WIDTH moved from TONE to TAIL to buy it, and the move is right on its own
    terms.** After the trim TONE held seven -- the four EQ rows, IN HI-CUT,
    WIDTH and OUTPUT -- and seven over three is 3 + 2 + 2, a third row on one
    page only. A fixed three-row block would have cost the screen 74 px of its
    170. WIDTH is M/S gain **on the tail only** (`kWidth`), the TONE page draws
    a frequency response of exactly three nodes and WIDTH is not one of them,
    and TAIL had four. So TONE is six and TAIL is five, both pages read better
    for it, and the screen keeps its height.

    OUTPUT stays on TONE rather than joining the foot, which was the other way
    to land it: the foot already has four, and a fifth cell there would put a
    68 px knob and the TYPE dropdown in 72 px cells.

    **No row anywhere holds one control.** That is the rule MIX broke on the
    old face -- a lone centred knob with two empty quarters beside it reads as
    a control whose partner has gone missing -- and TAIL's second row is two
    centred in the three for the same reason.

    ## The cluster's controls are added and removed, not hidden

    A hidden component still has bounds, and `tests/ui/LayoutTests` walks every
    child of a panel whether it is visible or not -- so a hidden control with a
    stale or zeroed rectangle either escapes the panel, overlaps something, or
    reports a caption overflowing a box of width zero. Unparenting is the one
    state in which a control is genuinely not part of this layout. All
    seventeen keep their parameter attachments throughout, so nothing is
    rebound and nothing is rebuilt when the page turns. This is the same
    arrangement the expanded groups used, and it is the reason turning a page
    costs a `resized` and nothing else.

    ## The page is UI state, and an unknown value is refused

    `ui.page=early|tail|tone`, through `ModulePanel::setUiState`. See `Page`
    for why it is not a parameter, and `setUiState` for why an unknown value
    comes back false rather than falling back to EARLY.

    ## Square corners on the body, 3 px on everything cut into it

    `ModulePanel::paint` fills the plate with `fillAll`, so the body is square
    by construction and a slot tiles flush against its neighbours -- a rack is
    a rectangle, and a module that rounded its own corners would show four
    slivers of whatever is behind it. Everything cut *into* the plate -- the
    bezel and the screen -- takes `Tokens::corner`, 3 px, uniformly. */
class ReverbPanel final : public ui::ModulePanel
{
public:
    explicit ReverbPanel (ui::ModuleContext);

    void resized() override;

    /** `ui.page=early|tail|tone`. Anything else is refused. */
    bool setUiState (const juce::String& key, const juce::String& value) override;

    Page getPage() const noexcept { return page; }

    /** Turns to a page: the screen's drawing, the cluster's controls and which
        key is lit, all three at once. There is no other way to change it. */
    void setPage (Page);

    const LingerScreen& getScreen() const noexcept { return screen; }

    /** The cluster controls belonging to `p`, in layout order. Public so a
        layout test can walk a page that is not showing and assert that those
        controls are **not** children of the panel. */
    std::vector<juce::Component*> pageControls (Page p) const;

    /** Everything on the panel whatever the page: the three persistent knobs,
        the three levels and TYPE in the corner. */
    std::vector<juce::Component*> alwaysOnControls() const;

    const PageButton& getPageButton (Page p) const noexcept
    {
        return *pageButtons[(size_t) p];
    }

    //== Where the painted furniture landed ====================================
    //
    // A bezel, a readout line and a rule are all *painted*, so unlike
    // every control on the panel they have no bounds anyone can read. These are
    // public for the reason `ui::ModulePanel::getRules` and
    // `ui::DynamicsMeter::vuScale` are: it is the only way a test can see them.

    /** The recess the screen sits in, noticeably larger than the screen: it
        carries the readout line as well, which is what the extra height is. */
    juce::Rectangle<int> getBezelBox() const noexcept   { return bezelBox; }
    juce::Rectangle<int> getScreenBox() const noexcept  { return screenBox; }
    juce::Rectangle<int> getReadoutBox() const noexcept { return readoutBox; }

    // `getGrilleBox` was here. The grille was cut on 2026-09-21 and TYPE has
    // its corner -- see the class comment. There is nothing painted at the
    // foot any more, so there is nothing here for a test to read.

    /** The block the page cluster is laid out in. Two rows at every page. */
    juce::Rectangle<int> getClusterBox() const noexcept { return clusterBox; }

    // The one hairline on this panel -- LEVEL, over the strip at the foot,
    // which is what says the three under it are there whatever page you turn
    // to -- is a `ModulePanel::Rule` and is read through `getRules()`. It has
    // no accessor of its own here: a second copy of one rectangle is the
    // drift this whole section exists to avoid.

private:
    void paintPanel (juce::Graphics&) override;

    /** Re-reads every parameter the screen is drawn from and hands them over
        as one state. */
    void refreshScreen();

    /** All seventeen cluster controls at once, for the unparenting walk. */
    std::vector<juce::Component*> allPageControls() const;

    Page page = Page::early;

    LingerScreen screen;

    std::array<std::unique_ptr<PageButton>, 3> pageButtons;

    // The persistent row: what the room is, when the tail arrives and how long
    // it rings. On screen at every page, because the three pages are all
    // adjustments to these three.
    ui::PlainKnob sizeKnob, preDelayKnob, decayKnob;

    /** **TYPE is a dropdown, not a knob**, and since 2026-09-21 it is not in
        the knob grid either. A knob says less and more, and a room type says
        neither -- Chamber is not more than Room. Frosty's call: "Room type
        makes no sense as a knob", and then that a thing which is not
        knob-shaped should not sit in a row of knobs. It lives in the corner
        the grille vacated, beside the two levels it stamps. `ui::ChoiceBox`
        carries the rest of the argument, including why VARIATION stays a knob
        and BMO DEQ's SHAPE stays a legend ring. */
    ui::ChoiceBox typeBox;

    // EARLY. ER MODE is this panel's other list of names and its other
    // dropdown; everything else in the cluster is an amount and stays a knob.
    // ER SHAPE was here and is a per-type constant now, and LINK ER -- the one
    // switch this panel had -- went with `prelink`.
    ui::ChoiceBox erModeBox;
    ui::PlainKnob densityKnob, erSpreadKnob, erHiCutKnob, variationKnob, feedKnob;

    // TAIL. ATTACK, DECAY SHAPE and the two damping knees went into the
    // per-type block; WIDTH arrived from TONE, because it is M/S gain on the
    // tail and the page it was on draws a frequency response it is not part
    // of.
    ui::PlainKnob dampLoKnob, dampHiKnob, modDepthKnob, modRateKnob, widthKnob;

    // TONE.
    ui::PlainKnob eqLoFreqKnob, eqLoKnob, eqHiFreqKnob, eqHiKnob,
                  inHiCutKnob, outputKnob;

    // The strip at the foot: the two absolute trims, which are the thesis of
    // the module and have to be reachable from every page, and how much of the
    // whole thing.
    ui::PlainKnob erLevelKnob, verbLevelKnob, mixKnob;

    /** One per parameter the screen is drawn from. Fourteen: thirteen of
        `LingerScreen::State`'s fields plus TYPE, which is not drawn itself but
        carries `attack` now that the knob is gone. The list in the constructor
        is the thing a reader wants to be able to check at a glance. */
    std::array<std::unique_ptr<juce::ParameterAttachment>, 14> screenAttachments;

    juce::Rectangle<int> bezelBox, screenBox, readoutBox, clusterBox;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ReverbPanel)
};

} // namespace bmo::reverb
