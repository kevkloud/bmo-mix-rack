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
    it. `specs()` is frozen at thirty with two spare host lanes; which page
    somebody is looking at is not something a session should carry, not
    something a host should be able to automate, and not worth one of the two
    lanes left. BMO Opto's meter mode and BMO DEQ's selected band are the same
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
    module is the failure the accent audit was run to find -- the twenty-three
    group knobs drew suite azure under a violet face, and the module read as
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
        uses. One struct rather than fifteen arguments, because the panel
        refreshes all of it at once from the parameters and a partial update is
        not a state this screen can be in. */
    struct State
    {
        // EARLY.
        float sizeM        = 12.0f;
        float preDelayMs   = 0.0f;
        bool  linkEr       = false;
        float erDensity    = 50.0f;   ///< per cent
        float erLevelDb    = -6.0f;

        // TAIL.
        float decaySeconds = 1.8f;
        float dampLo       = 1.20f;
        float dampHi       = 0.40f;
        float attack       = 30.0f;   ///< per cent, over the 0-120 ms bloom
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
    grille at the foot is the one raked thing on this panel.

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

    A bezelled screen with a line of printed text under it, three page keys,
    a persistent row that is there whatever page you are on, a cluster that
    changes with the page, a strip of three levels along the foot and a grille
    beside them. Frosty approved the shape on 2026-09-21, and it replaces the
    compact/expanded two-width split BMO DEQ's precedent had given this module.

    ## One width, and `expandedWidth` is gone

    The old arrangement was 300 compact and 700 expanded, with the same face
    down the left of both and three groups of knobs appearing in the extra
    400 px. **Paging removes the reason for it.** Eight, eight and seven
    controls never need to be on screen at once; what they need is to be
    reachable, and a key under the screen reaches them in one click where the
    expand switch reached them in one click and 400 px. So
    `ModuleDef::expandedWidth` is 0 here, `isExpandable()` is false, the
    standalone header and the rack's slot bar stop offering a switch with
    nothing to switch, and the module is the same panel everywhere -- which is
    the arrangement every module but BMO DEQ already had.

    It costs the width the groups used to take. **500**, because that is what
    the four-column grid needs for its longest caption and its widest dropdown
    item, and it is a multiple of 20 like every other panel in the suite. A
    rack slot is 200 px wider than it was, and 200 narrower than the expanded
    one a rack could already be asked for.

    ## The grid, and why four columns

    Four columns of the content width, and every row is laid out against them:
    the persistent row is four cells, the cluster is two rows of four, the page
    keys take three of the four centred, and the level strip takes three with
    the grille in the fourth.

    Three columns was the first cut and it fails on TONE. Seven controls over
    three is 3 + 2 + 2, so all three pages grow to three rows, the cluster
    block goes from 148 px to 222, and the screen has to lose 74 px to pay for
    it. Over four, EARLY and TAIL are 4 + 4 and TONE is 4 + 3 -- two rows every
    time, so the block under the keys does not change height when the page
    does, which is the one thing that would make paging feel like switching
    panels rather than turning a page.

    **No row anywhere holds one control.** That is the rule MIX broke on the
    old face -- a lone centred knob with two empty quarters beside it reads as
    a control whose partner has gone missing -- and TONE's second row is three
    centred in the four for the same reason the old EARLY group's last row was
    two centred in three.

    ## The cluster's controls are added and removed, not hidden

    A hidden component still has bounds, and `tests/ui/LayoutTests` walks every
    child of a panel whether it is visible or not -- so a hidden control with a
    stale or zeroed rectangle either escapes the panel, overlaps something, or
    reports a caption overflowing a box of width zero. Unparenting is the one
    state in which a control is genuinely not part of this layout. All
    twenty-three keep their parameter attachments throughout, so nothing is
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
    bezel, the screen, the grille -- takes `Tokens::corner`, 3 px, uniformly. */
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

    /** Everything on the panel whatever the page: the four persistent controls
        and the three levels. */
    std::vector<juce::Component*> alwaysOnControls() const;

    const PageButton& getPageButton (Page p) const noexcept
    {
        return *pageButtons[(size_t) p];
    }

    //== Where the painted furniture landed ====================================
    //
    // A bezel, a readout line, a rule and a grille are all *painted*, so unlike
    // every control on the panel they have no bounds anyone can read. These are
    // public for the reason `ui::ModulePanel::getRules` and
    // `ui::DynamicsMeter::vuScale` are: it is the only way a test can see them.

    /** The recess the screen sits in, noticeably larger than the screen: it
        carries the readout line as well, which is what the extra height is. */
    juce::Rectangle<int> getBezelBox() const noexcept   { return bezelBox; }
    juce::Rectangle<int> getScreenBox() const noexcept  { return screenBox; }
    juce::Rectangle<int> getReadoutBox() const noexcept { return readoutBox; }

    /** The raked speaker grille at the foot, which is texture and nothing
        else: no control, no state, nothing to click. */
    juce::Rectangle<int> getGrilleBox() const noexcept  { return grilleBox; }

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

    /** All twenty-three cluster controls at once, for the unparenting walk. */
    std::vector<juce::Component*> allPageControls() const;

    Page page = Page::early;

    LingerScreen screen;

    std::array<std::unique_ptr<PageButton>, 3> pageButtons;

    // The persistent row: what the room is, when the tail arrives and how long
    // it rings. On screen at every page, because the other three pages are all
    // adjustments to these four.
    /** **TYPE is a dropdown, not a knob.** A knob says less and more, and a
        room type says neither -- Chamber is not more than Room. Frosty's call,
        2026-09-21: "Room type makes no sense as a knob". `ui::ChoiceBox`
        carries the rest of the argument, including why VARIATION stays a knob
        and BMO DEQ's SHAPE stays a legend ring. */
    ui::ChoiceBox typeBox;
    ui::PlainKnob sizeKnob, preDelayKnob, decayKnob;

    // EARLY. ER MODE is this panel's other list of names and its other
    // dropdown; everything else in the cluster is an amount and stays a knob.
    ui::ChoiceBox erModeBox;
    ui::PlainKnob densityKnob, erShapeKnob, erSpreadKnob,
                  erHiCutKnob, variationKnob, feedKnob;
    ui::SwitchButton linkErSwitch;

    // TAIL.
    ui::PlainKnob attackKnob, decayShapeKnob,
                  dampLoFreqKnob, dampLoKnob, dampHiFreqKnob, dampHiKnob,
                  modDepthKnob, modRateKnob;

    // TONE.
    ui::PlainKnob eqLoFreqKnob, eqLoKnob, eqHiFreqKnob, eqHiKnob,
                  inHiCutKnob, widthKnob, outputKnob;

    // The strip at the foot: the two absolute trims, which are the thesis of
    // the module and have to be reachable from every page, and how much of the
    // whole thing.
    ui::PlainKnob erLevelKnob, verbLevelKnob, mixKnob;

    /** One per parameter the screen is drawn from. Fifteen of them, and the
        list in the constructor is exactly `LingerScreen::State`'s fields --
        which is the thing a reader wants to be able to check at a glance. */
    std::array<std::unique_ptr<juce::ParameterAttachment>, 15> screenAttachments;

    juce::Rectangle<int> bezelBox, screenBox, readoutBox, grilleBox, clusterBox;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ReverbPanel)
};

} // namespace bmo::reverb
