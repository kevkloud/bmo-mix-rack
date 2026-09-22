#pragma once

#include "core/product/ModuleDef.h"
#include "modules/reverb/dsp/EqNodes.h"
#include "modules/reverb/dsp/TapTables.h"
#include "modules/reverb/panel/Spectrum.h"

#include <array>
#include <functional>
#include <vector>

namespace bmo::reverb
{

//==============================================================================
/** Which page the handheld is showing.

    **UI state, not a parameter**, and the distinction is the whole design of
    it. `specs()` is thirty with two spare host lanes; which page somebody is
    looking at is not something a session should carry, not something a host
    should be able to automate, and **least of all worth a lane now that there
    are two left**. BMO Opto's meter mode and BMO DEQ's selected band are the
    same kind of thing and reach their panels the same way -- through
    `ModulePanel::setUiState`, which is also what makes every page renderable
    headlessly. See `ReverbPanel::setUiState`.

    **The third page is EQ and was TONE.** Frosty's call, 2026-09-21, when the
    two shelves became a three-node parametric: TONE named a vague direction
    and the page is now an equaliser. The `ui.page` key moved with the label --
    `ui.page=early|tail|eq` -- because a render key that said `tone` for a page
    captioned EQ is the kind of drift a reader has to hold in their head. */
enum class Page { early = 0, tail, eq };

//==============================================================================
/** The screen: a small dark display inside the bezel, drawing one of three
    pictures depending on the page the handheld is on.

    **EARLY and TAIL are static: no tap, no FFT, no timer, no audio path.**
    They redraw when one of the parameters they draw from moves, and when the
    page changes, and at no other time.

    **The EQ page is not, as of 2026-09-21.** The owner asked for a spectrum
    analyser behind its response curve, which overrides what this comment said
    -- "the display is parameter-driven only" -- for that one page. So the
    screen owns a `Spectrum` and a timer, and **the timer runs only while the
    EQ page is showing**: turning to EARLY or TAIL stops it, so those two pages
    cost exactly what they cost before. `core/dsp/AnalyserTap.h` is why the
    tap itself cannot change the sound or the latency.

    It still reads none of `ModuleContext`'s meter callbacks, and that part is
    unchanged: a reverb has no gain reduction to report.

    **What the spectrum is showing is the dry input**, because
    `dsp/DspCore.h` is a marked pass-through and there is no reverb under it
    yet. The tap is at the point the Reverb EQ acts on -- pre both generators,
    which is where 10 section 2 puts the EQ -- so the wiring is already right
    and only the signal is missing. It is honest rather than broken, and it is
    marked at the tap site, here, and in AGENTS.md so that nobody "fixes" a
    working analyser.

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

    **EARLY -- linear time, 0 to the last tap plus a tenth of the window, and
    the stems are mirrored about a centre axis by their bearing.** The
    image-source taps as discrete stems, at their times, their gains and
    **their pans**: left above the axis, right below it, length still gain.
    What this replaced was a symmetric envelope mirrored about a centre line
    that bloomed and closed to a point; Frosty rejected it, and why it was
    rejected is why the axis changed with it. **Mirroring the envelope and
    mirroring the taps are not the same thing** -- the first drew a lens with
    no event in it, and this draws one stem per reflection with its bearing in
    the sign. The ER window is 7-79 ms at the reference size and scales with
    SIZE, so it is a little over one decade wherever it is set -- and a linear
    axis scaled to the window itself shows the *spacing* of the reflections,
    which is the one thing about a tap set worth looking at. A log axis here
    would crowd the late taps into the last fifth, and a fixed window would
    leave most of the box empty at every size but one.

    **The sign is the bearing's side and the dash on each stem is its
    magnitude.** A sign alone cannot tell 0.05 from 0.72, and 10 section 3's
    lateral distribution -- target early lateral fraction 0.10-0.35, first
    reflections near centre so the phantom centre holds -- is a question about
    magnitudes. So every core tap also carries a short dash at `panY (pan)`,
    which is the same arithmetic the stem's side comes from. The dash sits on
    its own stem while the tap is loud and out past the tip while it is not,
    and the run of dashes is the lateral scatter the section is about. `L` and
    `R` are printed at the left-hand edge, because a mirrored picture with no
    hand on it is a picture a reader has to guess at.

    **TAIL -- logarithmic time, 1 ms to a window that follows the tail.**
    The axis end **follows `tailEndSeconds`**, far enough past it to leave
    `kAxisAir` of the box clear after the curve, and is clamped to
    `kMaxSeconds`,
    so the curve fills the box at every setting instead of finishing 60 % of
    the way across and ruling a flat line over the rest. The old axis ran to
    30 s at all times -- `kMaxSeconds`, which is `bmo::kMaxTailSeconds`, the
    clamp on the tail this module and the rack report -- and 30 s is the
    ceiling rather than a setting anybody uses: at the 1.8 s default it spent
    40 % of a 336 px box drawing nothing.

    **What that costs is comparability, and the readout is what pays it
    back.** With the axis following the tail, turning DECAY no longer walks the
    curve across the box -- the shape stays and the *scale under it* moves --
    so two settings cannot be compared by eye alone. The bezel line under the
    screen carries "DECAY 1.80 S" and "TAIL 2.16 S" in absolute seconds, and
    the decade marks inside the box are labelled 10 MS / 100 MS / 1 S / 10 S,
    so the length is readable in two places and neither of them is the width of
    the drawing. The trade was taken deliberately: a shape that fills the box
    answers "is this tail too long for the track" and a flat line does not.

    The left-hand end does not move: 1 ms is where a reflection stops fusing
    with the direct sound (05 section 1.1). Logarithmic, for the reason it
    always was -- a linear window wide enough for the tail puts the whole
    0-120 ms onset inside the first two pixels, and 11 section 5's proposed
    fixed 0-500 ms window cannot show a 20 s decay at all.

    **EQ -- logarithmic frequency, 20 Hz to 20 kHz,** which is the only axis a
    frequency response has. Level is linear in dB over +/-`kEqRangeDb`. It was
    captioned TONE until 2026-09-21; see `Page`.

    ## What the EQ curve is, and it is now the real filters

    **Three marked nodes over one summed curve, and a curtain that is not a
    node.** The three are the Reverb EQ's -- node 1 a low shelf, node 2 a bell,
    node 3 a high shelf, or a low cut and a high cut with FILTER on -- and they
    are designed by `EqNodes::design`, which is `dsp::designMatched`, which is
    **the code the engine will run**. IN HI-CUT is in the curve, because it is
    in the chain, but it is marked as a region rather than as a fourth node;
    see below.

    That closes what this comment used to own up to. The shelves were drawn
    first-order and the cut one-pole, marked as "not claimed to be the shipped
    filter" because `dsp/DspCore.h` was a marked placeholder and there was no
    shipped filter to be wrong about. Reproducing the matched-Z design into
    `core/dsp` gave the module one, so the sketch became a measurement. 11
    section 5 names sketch/DSP drift as the display's one real risk and asks
    for one source of truth; `TapTables.h` is that answer for the ER picture
    and `EqNodes.h` is now that answer for this one.

    ## The two high cuts, and how the picture tells them apart

    This module has **two** and they are different controls:

    - **IN HI-CUT** (`inhicut`), the input high-cut, ahead of the EQ and ahead
      of both generators, on top of a fixed 20 Hz high-pass. No Q, no gain, one
      pole. It darkens *what the room is given*.
    - **EQ HIGH with FILTER on**, node 3 of the Reverb EQ, second-order with a
      Q. It darkens *the room*.

    The captions carry the distinction -- "IN HI-CUT" against "EQ HIGH FREQ",
    "EQ HIGH" and "EQ HIGH Q" -- and **the picture now draws it as a different
    kind of thing entirely**: the three EQ nodes are filled circles on the
    curve, and IN HI-CUT is a **curtain** -- a washed region from its corner to
    the right-hand edge of the axis, with a bright edge at the corner and a tab
    along the top of it. It was an open circle until 2026-09-22 and that was
    wrong twice over. A marker one stroke different from three filled ones
    reads as a fourth node of the same EQ, which it is not; and at its default
    of 20 kHz it sat *on* the frame and was drawn half outside the box. A
    region is not a node at any glance, it cannot be confused for one, and
    `inputCutRegion` is clamped inside the plot so nothing of it is ever cut
    off by the frame. Its one-pole roll-off is still arithmetic in this file
    rather than an `EqNodes` node, and that is deliberate: giving it a `Biquad`
    would imply an order nobody has chosen for it.

    ## How FILTER reads as cuts

    Three things at once, so it cannot be mistaken for a shelf at a lot of
    gain. The nodes really are `Shape::lowCut` and `Shape::highCut`, so the
    curve dives off the bottom of the +/-24 dB axis at each end instead of
    levelling onto a shelf. The area between the curve and the 0 dB line is
    washed in, which is a lens either side of a shelf and a pair of wedges
    running to the floor for a cut. And the readout line prints "LO CUT" and
    "HI CUT" where it printed "LOW" and "HIGH". */
class LingerScreen final : public juce::Component,
                           private juce::Timer
{
public:
    explicit LingerScreen (juce::Colour accentColour);
    ~LingerScreen() override;

    /** Everything the three pictures are drawn from, in the units `specs()`
        uses. One struct rather than twenty arguments, because the panel
        refreshes all of it at once and a partial update is not a state this
        screen can be in.

        **Nineteen of these come from a parameter and `attack` does not.** It
        is `TypeConstants::attack` for whichever type is selected, since the
        2026-09-21 control-set trim took its knob away -- so TYPE is one of the
        parameters the panel redraws the screen on, and the TAIL page's onset
        follows a type change with no knob having moved.

        `linkEr` was here and went with `prelink`: ER travel with dry, fixed,
        so the pictures no longer have two cases to draw.

        The EQ block is an `EqSettings` and not ten loose fields, because that
        is the struct `EqNodes::design` takes and the same struct the engine
        builds through `DspCore::eqSettingsFor`. Two transcriptions of the same
        ten numbers is exactly how the picture and the sound come apart. */
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
        float attack       = 30.0f;   ///< per cent of the 0-120 ms onset, off the type's row
        float verbLevelDb  = -6.0f;

        // EQ: the three nodes and the mode, plus the input high-cut, which is
        // in series with them and is not one of them.
        EqSettings eq {};
        float inHiCutHz    = 20000.0f;
    };

    /** Repaints only when something has actually moved, and redesigns the
        three EQ nodes when one of the ten values behind them has. */
    void setState (const State&);

    /** The tap the spectrum is drawn from, or null for none. Handing one over
        is what starts the audio thread writing; the destructor hands null
        back. `ReverbPanel` passes `ModuleContext::analyser` straight through,
        which is null for a product that has no tap. */
    void setAnalyserTap (AnalyserTap*);

    /** Where the screen gets the rate to design its filters at.

        Polled rather than read once: a matched-Z design is rate-dependent by
        construction, and a host can re-prepare a plugin with its editor open.
        BMO DEQ drew the 48 kHz design at every rate until 2026-09-15, which
        was up to a dB out in the top octave at 44.1 and 96 k. Null, or
        returning 0, means "not prepared yet" and leaves the curve on
        `kEqDesignRate`. */
    void setHostRate (std::function<double()>);

    /** The rate the curve was last designed at. For a test that wants to know
        the fallback held. */
    double designedAt() const noexcept { return drawnAt; }

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

    /** The right-hand end of the TAIL page's time axis, in seconds: far enough
        past the tail that is set to leave `kAxisAir` of the width clear after
        it, floored at `kMinWindowS` so the shortest decay still has two
        decades to draw in, and clamped at `kMaxSeconds`.

        **This is what stopped 40 % of the box being a flat line.** It is a
        window rather than a constant, so a test asserting on it has to assert
        a relation -- the drawn tail ends inside the axis, and not far inside
        it -- which is what `tests/ui/LayoutTests.cpp` does. */
    float tailWindowSeconds() const noexcept;

    /** A bearing as a y inside the plot, for the EARLY page: -1 is hard left
        and draws at the top, +1 is hard right and draws at the bottom, 0 is
        the centre axis.

        Public for the reason every other number here is: it is what the
        picture is built from, and a test can check that a hard-panned tap
        lands inside the box without rendering anything. `kPanReach` is why
        +/-1 does not land *on* the frame. */
    float panY (float pan) const noexcept;

    /** The plotting area the page is drawn inside, so a test can ask whether
        something drawn is within it. */
    juce::Rectangle<float> plotBounds() const noexcept { return plotArea(); }

    /** IN HI-CUT's curtain on the EQ page: the washed region from its corner
        to the right-hand edge of the axis, **clamped so its bright edge is
        inside the plot at 20 kHz** rather than half-drawn on the frame, which
        is what the open circle it replaced was. Empty on the other two pages.

        The rectangle is the whole curtain, edge included; the edge itself is
        its left-hand `kCurtainEdge` pixels. */
    juce::Rectangle<float> inputCutRegion() const noexcept;

    /** A tick label the screen prints inside the plot, and the box it is set
        in. `text` is ASCII, like every other string this module prints. */
    struct AxisLabel
    {
        juce::String text;
        juce::Rectangle<float> box;
    };

    /** Every label the showing page prints inside the screen: the L and R
        hands on EARLY, the decade ticks on TAIL, none on EQ.

        **The painter draws this list rather than building its own**, which is
        `TapTables.h`'s discipline applied to text: a test that measured the
        labels its own way could agree with a label that clips. */
    std::vector<AxisLabel> axisLabels() const;

    /** How many of the table's taps DENSITY has switched on. The real bridge
        is a continuous ramp over 48 taps with a master sequence that does not
        exist yet (10 section 3); this is the 21 core taps plus the infill the
        knob has paid for, so the picture thickens with the control. */
    int activeTapCount() const noexcept;

    /** The whole EQ chain at `hz`, in dB: the three designed nodes plus the
        input high-cut, summed. This is what the curve is drawn from and what a
        test should assert against.

        The three nodes are `dsp::designMatched`, the engine's own design, so
        at the defaults every one of them is that function's exact unity case
        and the three contribute **0.0 dB and not nearly zero**. The only
        approximation left in the number is IN HI-CUT's one pole, which is a
        third of a dB at 20 kHz with the knob wide open. */
    float responseDbAt (float hz) const noexcept;

    /** One Reverb EQ node's own contribution at `hz`, in dB -- without its two
        neighbours and without the input high-cut. This is how a test says
        "FILTER changed node 1 and node 3 and left node 2 alone" as three
        separate claims rather than as one about a sum. */
    float nodeDbAt (EqNode node, float hz) const noexcept;

    /** The four corner frequencies the picture marks, in the order it marks
        them: EQ LOW, EQ MID and EQ HIGH as nodes on the curve, then IN HI-CUT,
        which is `inputCutRegion`'s curtain rather than a fourth node. */
    std::array<float, 4> nodeFrequencies() const noexcept;

    /** Which of the two outer nodes are cuts. The same mode the GAIN knobs
        grey out on, read back through the screen so a test can check that the
        picture and the controls agree about it.

        The mode itself and not "is it on", since 2026-09-22: with four
        positions a bool could not tell Lo Cut from Hi Cut, and those two are
        the pair a test most needs to be able to tell apart. */
    EqFilter filterMode() const noexcept { return state.eq.filter; }

    /** Whether *either* outer node is a cut -- what the old bool said. Kept
        because "the screen took the mode at all" is still a claim worth
        making on its own. */
    bool isFilterMode() const noexcept { return state.eq.filter != EqFilter::off; }

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

    /** The TAIL page's window, as the class comment argues it.

        `kMaxSeconds` is **the ceiling the window is clamped to and no longer
        the axis end itself**: it is `bmo::kMaxTailSeconds`, the clamp on the
        tail this module and the rack it sits in will ever report, so the axis
        can never be asked to draw a tail longer than the host is told about.
        `tailWindowSeconds` is where the axis actually ends. */
    static constexpr float kMinMs      = 1.0f;
    static constexpr float kMaxSeconds = 30.0f;

    /** How much of the box is left blank after the drawn tail reaches the
        floor, **as a fraction of the width and not as a multiple of the
        time**.

        On a logarithmic axis those are not the same thing and the difference
        is the whole of why this constant is written this way: 15 % more time
        after a 2.16 s tail is 1.8 % of the width, which rendered on AURORA as
        a curve running into the right-hand frame. Eight per cent of the width
        is eight per cent of the width at every setting, which is what
        `erWindowMs`'s tenth already gives the EARLY page. */
    static constexpr float kAxisAir = 0.08f;

    /** The shortest window the axis will draw, in seconds. DECAY bottoms out
        at 0.1 s, which with the headroom is 115 ms and two decades of axis;
        this floor is what stops a future shorter decay from drawing one. */
    static constexpr float kMinWindowS = 0.05f;

    /** How far from the centre axis a hard-panned tap draws on EARLY, as a
        fraction of the half-height. Not 1.0: the plot's own edge is where the
        frame is, and a bearing dash sitting on the frame reads as a clipped
        dash rather than as a hard pan. */
    static constexpr float kPanReach = 0.92f;

    /** The bright edge on IN HI-CUT's curtain, in pixels. The curtain's left
        edge is inset by this much at 20 kHz so the whole of it stays inside
        the plot -- which is the clipping the open circle was guilty of. */
    static constexpr float kCurtainEdge = 2.0f;

    /** The tick labels' point size. Small, and smaller than the readout's 11:
        these sit *inside* the picture and a tick that competed with the curve
        would be a second thing to read rather than a scale for the first. */
    static constexpr float kTickSize = 8.0f;

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

    /** The EQ page's axes. */
    static constexpr float kMinHz     = 20.0f;
    static constexpr float kMaxHz     = 20000.0f;
    static constexpr float kEqRangeDb = 24.0f;

    /** The spectrum's own frame rate, and the rate the EQ page redraws at.
        Thirty, which is every meter in the suite's. */
    static constexpr int kFrameHz = 30;

private:
    void timerCallback() override;

    /** Rebuilds the three nodes at `drawnAt`. Called when the ten EQ values
        move and when the host's rate does, never per pixel. */
    void redesign();

    void rebuildSpectrum();

    /** Hz to x and dB to y on the EQ page, shared by the grid, the curve, the
        wash, the node markers **and the spectrum** -- which is why they are
        members rather than lambdas inside `paintEq`. A spectrum that built its
        own log axis would drift from the one it is drawn against. */
    float eqXFor (double hz) const noexcept;
    float eqYFor (double db) const noexcept;

    /** The plotting area, inset from the component. */
    juce::Rectangle<float> plotArea() const noexcept;

    /** Milliseconds to x on the TAIL page, over a window that follows the
        tail. A member and not a lambda inside `paintTail`, because the decade
        ticks in `axisLabels` have to land on the same axis the envelope is
        drawn against -- the same reason `eqXFor` is one. */
    float tailXFor (float ms) const noexcept;

    void paintEarly (juce::Graphics&, juce::Rectangle<float> plot, juce::Colour ink) const;
    void paintTail  (juce::Graphics&, juce::Rectangle<float> plot, juce::Colour ink) const;
    void paintEq    (juce::Graphics&, juce::Rectangle<float> plot, juce::Colour ink) const;

    juce::Colour accent;
    State state;
    Page page = Page::early;

    /** The three Reverb EQ nodes as designed filters, and the rate they were
        designed at. `kEqDesignRate` until a host says otherwise. */
    EqNodes nodes = EqNodes::design (EqSettings {}, kEqDesignRate);
    double drawnAt = kEqDesignRate;
    std::function<double()> hostRate;

    Spectrum spectrum;
    juce::Path spectrumPath;

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

    **The name is set inside the key, and was under it until 2026-09-22.**
    Frosty's call, against the mockup. The old arrangement borrowed a knob's --
    a circle with a caption row beneath it -- and that was the *less*
    house-consistent of the two: every other thing in this suite that is a key
    rather than a control puts its word inside itself. `ui::SwitchButton` does
    (FILTER here, MONO in Util, LINK ER before the trim), and so do BMO Opto's
    TELE and ELD. A page key is a key.

    The old comment here claimed "EARLY does not fit at any size worth
    reading", and it was measuring a 32 px circle. It is 36 now, paid for by
    the caption row that went -- see `kPageRow` -- and the word fits with real
    room to spare; `labelOverflow` is the measurement and
    `tests/ui/LayoutTests.cpp` asserts on it per key per page. **Do not take
    the diameter back down without re-reading that number**: this is the
    MAKEUP -> MAKEU failure mode with a circle round it, and a chord is less
    forgiving than a rectangle because the room runs out fastest exactly where
    the letters are.

    The fill is unchanged: the module's accent when its page is selected and
    `switchOff` -- the raised grey every unlit switch in the suite is filled
    with -- when it is not. The *ink* had to change, because it moved off the
    plate and onto the fill: `ui::onAccentOf` is what a switch's label uses for
    the same reason, and deriving against the plate here would have put the
    accent on top of itself.

    **Flat, and staying flat.** No gradient, no bevel, no drop shadow. Every
    control in this suite is flat and Frosty is assessing that separately; a
    key that got a dimensional treatment on its own would decide the question
    by accident.

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

    /** How much wider the name is than the room it has **inside the circle**,
        in pixels; zero or less fits, and the negative of it is the margin.

        Here rather than in the test that asserts on it, because it has to use
        the same box and the same face `paintButton` does.
        `ui::PlainKnob::captionOverflow` is the precedent and carries the
        argument: MAKEUP drew as MAKEU for a full release, and a test that
        measured it its own way could have agreed with the bug. */
    float labelOverflow() const;

    /** The circle itself, inside the component. For a layout test that wants
        to know the key is round rather than merely present. */
    juce::Rectangle<int> dotBounds() const;

    /** The box the name is actually set in, inside the circle.

        Public because a label inside a round bound is not something the
        caption walk in `tests/ui/LayoutTests.cpp` can check -- that walk knows
        about captions under controls -- so the test asserts this box is inside
        `dotBounds` itself. */
    juce::Rectangle<int> labelBox() const;

    /** 32 until the caption row went. The four extra pixels are four of the
        twenty that row freed; the other sixteen are the PAGE legend under the
        row. See `kPageRow` in ReverbPanel.cpp, which adds it up. */
    static constexpr int kDotSide = 36;

    /** The name's point size, and **it is a measurement, not a preference**.

        `ui::SwitchButton` derives its label from the box height at 62%, which
        on a 36 px key would be 22 pt and absurd: that ratio is calibrated for
        a 26 px switch whose word runs the length of a 70 px box, and a chord
        is not a box. Pinned instead, the way BMO CEQ's square HI-Q switch pins
        its own.

        "EARLY" in `labelFont`, against the chord `labelBox` gives it inside a
        36 px key, measured on AURORA through `ui_layout_tests --dump`:

            8.0 pt   23.7 px in 31.1   margin  7.4
            9.0 pt   26.7 px in 30.9   margin  4.2     <- this
           10.0 pt   29.6 px in 30.6   margin  1.0
           10.5 pt   31.1 px in 30.4   margin -0.7     <- clips

        Ten was the first choice and 1.0 px of margin is not a margin; it is
        the half-pixel case `tests/ui/LayoutTests.cpp`'s own dump comment warns
        about, one type size away from being the next MAKEUP. Nine gives four
        px and reads.

        **The face matters more than the size here.** `captionFont` -- Blender,
        what the name was set in while it was a caption under the key -- is
        *wider* than Minerva Black at the same nominal height, and by a lot:
        "EARLY" is 45.4 px at 9 pt against Minerva's 26.7. That, and not the
        circle, is why the old comment here concluded a word could not be set
        inside a key at any size worth reading. It was measuring the wrong
        face. */
    static constexpr float kLabelSize = 9.0f;

    /** How far inside the circle the name's box stops, each side.

        The chord is where the outline is drawn, so a name measured to the
        chord is a name touching the rim. Two pixels is the hairline plus air,
        and it is taken off the measurement rather than off the drawing, so the
        margin `labelOverflow` reports is room the reader can actually see. */
    static constexpr float kLabelInset = 2.0f;

private:
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
    schema from thirty parameters to twenty-four -- and it **stayed 380** when
    the Reverb EQ took it back to thirty later the same day. Frosty's call:
    the density belongs on the page, which is what paging is for.

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

    ## Four rows reserved on every page, and what that cost

    EARLY is six and TAIL five, which is 3 + 3 and 3 + 2. **EQ is twelve**:
    each of the three nodes as FREQ / GAIN / Q, then FILTER, IN HI-CUT and
    OUTPUT. Twelve over three is four rows, and four rows is what the cluster
    block is reserved at **on every page**, so the block under the keys does
    not change height when the page does -- the one thing that would make
    paging feel like switching panels rather than turning a page.

    ### Where the two spare rows go, and it is not where they went

    **EARLY and TAIL fill two of the four rows, and since 2026-09-22 they are
    packed to the top of the block rather than centred in it.** Frosty's
    observation, against the render: the centred arrangement drew "two large
    voids", one above the cluster and one below it, and a reader has no way to
    tell a deliberate space from a control that failed to appear. Packed, the
    page's own rows sit hard under the persistent row they qualify, and the
    whole of the leftover falls in one piece immediately above the LEVEL rule
    -- where a gap already belongs, because every legended rule in the suite
    has one above it. It is a larger gap than usual and it reads as separation
    rather than as absence.

    **Two alternatives were weighed and rejected.**

    A **taller screen on the short pages** would absorb the rows exactly, and
    it is the one thing that cannot be done: the screen's height is the panel's
    one soft number, and moving it per page makes the bezel -- the biggest
    object on the face -- resize every time a page key is pressed. A panel that
    jumps when you turn a page is worse than any amount of air, and the page
    keys exist to make turning cheap.

    **Spreading the two rows evenly over the four** leaves no void anywhere and
    costs more: 44 px between a page's own two rows is more than the 66 px
    between the cluster and its neighbours *was*, so the rows stop reading as
    one block and start reading as two unrelated pairs. A page's rows belong
    together; the space does not belong between them.

    The panel is **380 x 688 either way**. Neither the height nor the width
    moves, because the rack slot cannot resize.

    **The screen paid for the two extra rows, and it is the only thing that
    could have.** A panel is 688 px of content and every block in it was
    already sized; two more cluster rows are 132 px and there are five gaps of
    8 px between six blocks. The screen came down from **170 to 105**, the
    cluster knob from 54 to 46, and the persistent row and the level strip from
    100 to 80 apiece -- which together is exactly the 132. `kContentHeight` is
    640 against the 680 available, so the five gaps are `Tokens::switchGap` and
    the panel fits to the pixel. **There is no slack left**: anything added to
    a page now comes out of the screen again.

    A 336 x 105 screen is still a real instrument -- it is wider than BMO DEQ's
    compact curve -- but it is a letterbox, and the EARLY and TAIL pictures are
    the two that felt the loss. Both were checked in render: the ER stems and
    the tail envelope both read, because both are shapes against a baseline
    rather than fine detail.

    **OUTPUT stays on the EQ page rather than joining the foot.** Moving it was
    allowed and would have made EQ eleven -- which is still four rows, 3 + 3 +
    3 + 2, so it would have bought nothing vertically and cost the foot its
    shape: the foot already holds four, and a fifth cell there would put a
    60 px knob and the TYPE dropdown in 72 px cells. Twelve is the number that
    fills four rows exactly, and a full last row is the tidier picture.

    **WIDTH stays on TAIL**, where it moved in the 2026-09-21 trim: it is M/S
    gain on the tail only (`kWidth`), the EQ page draws a frequency response
    and WIDTH is not part of one.

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
    twenty-three keep their parameter attachments throughout, so nothing is
    rebound and nothing is rebuilt when the page turns. This is the same
    arrangement the expanded groups used, and it is the reason turning a page
    costs a `resized` and nothing else.

    ## The page is UI state, and an unknown value is refused

    `ui.page=early|tail|eq`, through `ModulePanel::setUiState`. See `Page`
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

    /** `ui.page=early|tail|eq`. Anything else is refused. */
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

    /** All twenty-three cluster controls at once, for the unparenting walk. */
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

    // EQ. Twelve controls in four rows: each node as FREQ / GAIN / Q, then the
    // mode, the input cut and the output trim.
    //
    // **FILTER is the only switch on this panel.** LINK ER was the last one
    // and went with `prelink` in the 2026-09-21 trim; this is not its
    // replacement, it is a mode over three controls rather than a behaviour
    // toggle. `ui::SwitchButton` and not a `ChoiceBox`, because two states
    // named by one word is a switch, and not a page key, because a page key
    // has no parameter under it and this one does.
    ui::SwitchButton eqFilterSwitch;
    ui::PlainKnob eqLoFreqKnob, eqLoKnob, eqLoQKnob,
                  eqMidFreqKnob, eqMidKnob, eqMidQKnob,
                  eqHiFreqKnob, eqHiKnob, eqHiQKnob,
                  inHiCutKnob, outputKnob;

    // The strip at the foot: the two absolute trims, which are the thesis of
    // the module and have to be reachable from every page, and how much of the
    // whole thing.
    ui::PlainKnob erLevelKnob, verbLevelKnob, mixKnob;

    /** One per parameter the screen is drawn from. Twenty: nineteen of
        `LingerScreen::State`'s values plus TYPE, which is not drawn itself but
        carries `attack` now that the knob is gone. The list in the constructor
        is the thing a reader wants to be able to check at a glance.

        It was fourteen; the Reverb EQ's six new parameters brought it to
        twenty, and `eqfilter` is one of them -- it moves the two outer nodes'
        shapes and so redraws the curve with no frequency or gain having
        changed. */
    std::array<std::unique_ptr<juce::ParameterAttachment>, 20> screenAttachments;

    /** Greys out the two shelf GAIN knobs while FILTER is on, because a cut
        has no gain.

        `setKnobEnabled` and **not** `setLockedOn`'s equivalent: the parameters
        still hold whatever the user set, they are never written, and switching
        FILTER off gives both shelves their gains back. A mode must not eat an
        edit. `eqGainReachingDesign` is the same decision one folder over, in
        the DSP, and it is the reason the two cannot disagree. */
    void refreshFilterMode();

    juce::Rectangle<int> bezelBox, screenBox, readoutBox, clusterBox;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ReverbPanel)
};

} // namespace bmo::reverb
