#pragma once

#include "core/product/ModuleDef.h"
#include "modules/reverb/dsp/EqNodes.h"
#include "modules/reverb/dsp/ErTable.h"
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

    **The keys are inside the screen as of 2026-09-22.** They were three round
    buttons on the plate under the bezel, with a PAGE rule under them; they are
    a menu band drawn in the display's own ink now, which is what a handheld
    with a screen actually does. See `LingerScreen::kMenuBand`. */
enum class Page { early = 0, tail, eq };

//==============================================================================
/** The screen: a dark display inside the bezel, carrying its own page menu and
    drawing one of three pictures under it.

    **The top 26 px is the menu, and it is part of what the display is
    showing.** Three divided segments -- EARLY / TAIL / EQ -- in the screen's
    own ink, the selected one as inverted video, a hairline between each pair
    and a rule under the band. It is not a row of buttons on the bezel: a
    handheld's page keys live on its screen, and the plate under the bezel is
    worth more to the controls than to three keys that were duplicating what
    the picture already says. The 232 px under the band is the drawing area,
    and `plotBounds` is inside that rather than inside the whole component.

    The band takes clicks -- `onPageChosen` -- which is why this component
    intercepts them at all. Everything below the band ignores them.

    **EARLY and TAIL are static: no tap, no FFT, no timer, no audio path.**
    They redraw when one of the parameters they draw from moves, and when the
    page changes, and at no other time.

    **The EQ page is not.** The owner asked for a spectrum analyser behind its
    response curve, so the screen owns a `Spectrum` and a timer, and **the
    timer runs only while the EQ page is showing**: turning to EARLY or TAIL
    stops it, so those two pages cost exactly what they cost before.
    `core/dsp/AnalyserTap.h` is why the tap itself cannot change the sound or
    the latency.

    **What the spectrum is showing is the dry input**, because the tap is at
    the point the Reverb EQ acts on -- pre both generators, which is where 10
    section 2 puts the EQ -- and until M3 builds the input stage and the EQ,
    that point is the module's input. The early reflections exist since M2;
    they are after the tap, so they are not in it. The wiring is already
    right and only the EQ is missing. It is honest rather than broken, and it is marked at the tap
    site, here, and in AGENTS.md so that nobody "fixes" a working analyser.

    **It draws in the module's accent, not in LCD green.** A second hue on one
    module is the failure the accent audit was run to find. The face underneath
    is `meterFace`, the token a needle meter's scale is printed on: a value
    rather than a hue, dark in both appearances, which is what a screen is.

    It draws the taps from `erTableFor (type)` at the current VARIATION, through
    the Size law the engine plays by (`erSizeScale`) -- **the same table the
    engine plays**, since 2026-09-24; it drew `TapTables.h`'s placeholder until
    then. 11 section 5 names sketch/DSP drift as the display's one real
    risk and asks for exactly this: one source of truth, plus a layout test
    asserting the sketch's first tap time equals the table's. That assertion is
    what `firstTapTimeMs` is public for, and every other accessor below is
    public for the same reason -- they are numbers the picture is built from
    rather than pixels it happens to land on, so a test can read them without
    rendering anything.

    ## The three pages, and the picture each one needed

    **EARLY -- a time x pan scatter.** x is arrival, linear over the real ER
    window; y is bearing, hard left at the top and hard right at the bottom;
    and **the radius of the dot is the tap's gain**. What this replaced was a
    set of mirrored stems with a bearing dash on each, and before that a
    symmetric envelope. The scatter is the third and the right one, and the
    argument is VARIATION: it is a lateral-spread control, so the picture it
    belongs in is one where lateral spread is an axis. Turning it fans the
    cluster open and shut vertically, which is a thing a reader sees without
    being told what to look at; on stems it moved twenty-one short dashes a few
    pixels each.

    **The three marks are three different claims.** A filled dot is a core tap:
    a real time, a real gain and a real bearing, all three off the same `Tap`
    row the engine will play. The direct sound is that dot with a ring around
    it, at t = 0 on the centre line -- it is not a reflection and it is what
    every arrival here is measured from. And **an infill tap is a faint
    full-height line**, at its real time off the table and as bright as the
    density ramp has made it. It was drawn as a line because its bearing was
    invented while the tables were a placeholder; the real infill has real
    bearings now, and drawing them as dots would change what the page shows,
    which nobody has asked for, so the marks keep their meanings. The scatter
    draws the VARIATION's **left** set: the two channels are one image field
    heard at two ears, the same taps but for a few split in time.

    **TAIL -- three decay curves, low, mid and high.** The page's controls are
    LOW x, HIGH x, MOD DEPTH and MOD RATE, and the single envelope this
    replaced showed none of them: LOW x could be swept end to end with nothing
    on the screen moving, because the envelope was drawn at the mid decay and
    the multipliers only ever moved the shaded band behind it. Three curves
    make the two multipliers the subject. The mid curve is the heaviest -- it
    is what DECAY says -- and the outer two are lighter.

    The axis is unchanged and so is the reason: logarithmic, 1 ms to a window
    that follows the tail. 1 ms is where a reflection stops fusing with the
    direct sound (05 section 1.1), and the end **follows `tailEndSeconds`** so
    the curves fill the box at every setting instead of finishing 60 % of the
    way across. The decade marks are labelled because a window that moves makes
    an unlabelled decade line say only "a decade happened here".

    **EQ -- logarithmic frequency, 20 Hz to 20 kHz,** which is the only axis a
    frequency response has. Level is linear in dB over +/-`kEqRangeDb`.

    ## What the EQ curve is, and it is the real filters

    **Three marked nodes over one summed curve, and a curtain that is not a
    node.** The three are the Reverb EQ's -- node 1 a low shelf, node 2 a bell,
    node 3 a high shelf, or a low cut and a high cut with FILTER on -- and they
    are designed by `EqNodes::design`, which is `dsp::designMatched`, which is
    **the code the engine will run**. IN HI-CUT is in the curve, because it is
    in the chain, but it is marked as a region rather than as a fourth node.

    ### The four node states, and they compose

    A marker says two independent things and says them with two independent
    strokes, which is what lets it say all four combinations:

    - **A ring means the knobs edit this node.** FREQ, GAIN and Q are one set
      repointed by the LOW / MID / HIGH segments, so at any moment two of the
      three nodes on the curve are not the one the knobs are holding. Without
      the ring a reader turning FREQ has to look away from the picture to find
      out which corner is about to move.
    - **A fill means this node is shaping the sound.** Gain off zero, or a
      shape that removes without a gain at all -- a cut is always doing
      something. An unfilled marker is a node sitting at unity.

    So: ringed and filled is the node you are editing and it is doing
    something; ringed and hollow is the node you are editing and it is flat;
    filled alone is a node working that the knobs are not on; hollow alone is a
    node at rest. `nodeMark` is the one place that is decided and a layout test
    reads it there. */
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

        **Twenty of these come from a parameter and `attack` does not.** It is
        `TypeConstants::attack` for whichever type is selected, since the
        2026-09-21 control-set trim took its knob away -- so TYPE is one of the
        parameters the panel redraws the screen on, and the TAIL page's onset
        follows a type change with no knob having moved.

        The EQ block is an `EqSettings` and not ten loose fields, because that
        is the struct `EqNodes::design` takes and the same struct the engine
        builds through `DspCore::eqSettingsFor`. Two transcriptions of the same
        ten numbers is exactly how the picture and the sound come apart. */
    struct State
    {
        // EARLY.
        int   type         = 0;       ///< the TYPE detent, whose ER table the scatter draws
        float sizeM        = 12.0f;
        float preDelayMs   = 0.0f;
        float erDensity    = 50.0f;   ///< per cent
        float erLevelDb    = -6.0f;
        float variation    = 2.0f;    ///< 0-6, the lateral-spread step

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
        Null, or returning 0, means "not prepared yet" and leaves the curve on
        `kEqDesignRate`. */
    void setHostRate (std::function<double()>);

    /** The rate the curve was last designed at. For a test that wants to know
        the fallback held. */
    double designedAt() const noexcept { return drawnAt; }

    /** Repaints only when the page has actually changed. */
    void setPage (Page);
    Page getPage() const noexcept { return page; }

    /** Which node's marker wears the ring: the one the FREQ / GAIN / Q knobs
        are pointed at. UI state, handed down by the panel. */
    void setSelectedNode (EqNode);
    EqNode getSelectedNode() const noexcept { return selectedNode; }

    /** A click on the menu band asked for a page. Null does nothing, which is
        what a screen rendered headlessly wants. */
    std::function<void (Page)> onPageChosen;

    void paint (juce::Graphics&) override;
    void mouseUp (const juce::MouseEvent&) override;

    //== The menu band ========================================================

    /** The band across the top of the display, in this component's pixels. */
    juce::Rectangle<float> menuBandBounds() const noexcept;

    /** One page's segment of the band, dividers included. Public so a layout
        test can assert the three tile the band and that each one's word fits
        inside its own segment. */
    juce::Rectangle<float> menuSegment (Page) const noexcept;

    /** How much wider a page's name is than the room it has inside its
        segment; zero or less fits, and the negative of it is the margin.

        Here rather than in the test, for `ui::PlainKnob::captionOverflow`'s
        reason: it uses the same box and the same face `paint` does, so it
        cannot agree with the bug it is looking for. MAKEUP -> MAKEU is the
        failure, and a menu is the place it hides best -- nothing else on a
        panel walks text drawn inside a picture. */
    float menuLabelOverflow (Page) const;

    /** The word printed in a page's segment. */
    static juce::String menuLabel (Page) noexcept;

    //== Arithmetic, public so a test can assert it without rendering ==========

    /** When the first reflection arrives, in milliseconds, at the current
        SIZE. **This is the sketch/DSP drift assertion**: it must equal the
        selected type's table, first tap of the VARIATION's set, times
        `erSizeScale` -- the time the engine plays it at. */
    float firstTapTimeMs() const noexcept;

    /** When the last reflection can sound -- `t_ER,max` in the tail formula,
        `erSpanMsAt (table, size)`, the per-type span inside its clamp. */
    float lastTapTimeMs() const noexcept;

    /** How many core taps (threshold 0) the scatter's set has: the dots
        `tapDot` indexes, 21 in every shipped table. */
    int coreTapCount() const noexcept;

    /** The right-hand edge of the EARLY page's own time axis: the last tap
        with a tenth of the window left after it, so the last dot is a dot and
        not the border. */
    float erWindowMs() const noexcept;

    /** When the tail's first sample arrives: the pre-delay, which is tail-only
        and can never be negative. */
    float tailStartMs() const noexcept { return state.preDelayMs; }

    /** Where the drawn tail reaches -60 dB, in seconds, on its slowest band --
        `decay * max(1, dampLo, dampHi)`, with the pre-delay in front of it. */
    float tailEndSeconds() const noexcept;

    /** The three decay times the TAIL page draws, in seconds: low, mid, high.

        Mid is DECAY itself and the outer two are DECAY times their own
        multiplier, which is what makes the two outer curves the picture of
        LOW x and HIGH x. Public because it is what the three curves *are*, and
        a test that re-derived them its own way could agree with a picture
        drawing one curve three times. */
    std::array<float, 3> decayTimesSeconds() const noexcept;

    /** The right-hand end of the TAIL page's time axis, in seconds: far enough
        past the tail that is set to leave `kAxisAir` of the width clear after
        it, floored at `kMinWindowS` so the shortest decay still has two
        decades to draw in, and clamped at `kMaxSeconds`. */
    float tailWindowSeconds() const noexcept;

    /** A bearing as a y inside the plot, for the EARLY page: -1 is hard left
        and draws at the top, +1 is hard right and draws at the bottom, 0 is
        the centre axis. `kPanReach` is why +/-1 does not land *on* the frame.

        **The bearing handed in is the table's, and the scatter draws it
        through `lateralSpread`** -- see that function for what VARIATION does
        to it and for the marking that says how much of it is real. */
    float panY (float pan) const noexcept;

    /** How much of a tap's tabulated bearing VARIATION lets through, 0 to 1.

        **Still a stand-in, and marked as one.** The tables are real since
        2026-09-24 and the scatter draws their times, gains and bearings, but a
        tap's bearing is its image's, which VARIATION does not move: what
        VARIATION changes is which taps each channel carries. So this scalar is
        what makes the knob visible on the picture, and it is not read off the
        tables. What is real is the direction: more variation is more lateral
        spread, the bottom of the travel is not zero because the first
        reflections stay near centre at every setting anyway, and position 6 is
        mono null, all side, which this does not attempt to draw. Replacing it
        with something the tables carry is an owner decision.

        Public so the layout test can assert the fan opens with the knob
        without reading pixels. */
    float lateralSpread() const noexcept;

    /** The plotting area the page is drawn inside -- **under the menu band**,
        so a test can ask whether something drawn is within it. */
    juce::Rectangle<float> plotBounds() const noexcept { return plotArea(); }

    /** Where a core tap's dot is drawn and how big it is, on the EARLY page.

        The centre is (arrival, bearing) and **the radius is the gain**, which
        is the third axis the scatter has and the stems did not. Empty radius
        means the tap is below the floor and is not drawn at all. */
    struct TapDot
    {
        juce::Point<float> centre;
        float radius = 0.0f;
    };

    /** Core tap `index`'s dot, 0-based into the scatter set's core taps in
        time order. */
    TapDot tapDot (int index) const noexcept;

    /** The direct sound's dot: t = 0, dead centre, full gain, pushed in by its
        own radius so no part of it is drawn on the frame. */
    TapDot directDot() const noexcept;

    /** IN HI-CUT's curtain on the EQ page: the washed region from its corner
        to the right-hand edge of the axis, **clamped so its bright edge is
        inside the plot at 20 kHz** rather than half-drawn on the frame. Empty
        on the other two pages.

        The rectangle is the whole curtain, edge included; the edge itself is
        its left-hand `kCurtainEdge` pixels. */
    juce::Rectangle<float> inputCutRegion() const noexcept;

    /** How one EQ node's marker is drawn: where, how big, and the two
        independent strokes. See the class comment for what the two mean and
        why they have to compose. */
    struct NodeMark
    {
        juce::Point<float> centre;
        float radius = 0.0f;
        bool ringed = false;   ///< the FREQ / GAIN / Q knobs are pointed here
        bool filled = false;   ///< this node is shaping the sound
    };

    NodeMark nodeMark (EqNode) const noexcept;

    /** Whether a node is shaping the sound: a gain off zero, or a shape that
        removes without having a gain at all.

        **A cut counts whatever its GAIN knob reads**, which is the whole
        reason this is a function rather than a comparison at the call site:
        `eqNodeHasGain` is false for a cut, the knob is greyed, and the node is
        very much doing something. Reading the gain alone would draw a 24 dB
        low cut as a node at rest. */
    bool nodeIsActive (EqNode) const noexcept;

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

    /** How many of the table's taps DENSITY has switched on: the core taps,
        which never switch off, plus every infill tap whose ramp weight is
        above zero at this density -- the table's own thresholds and the
        engine's own ramp width, so the picture thickens exactly as the ER do. */
    int activeTapCount() const noexcept;

    /** The whole EQ chain at `hz`, in dB: the three designed nodes plus the
        input high-cut, summed. This is what the curve is drawn from and what a
        test should assert against. */
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

    /** Which of the two outer nodes are cuts. The same mode the GAIN knob
        greys out on, read back through the screen so a test can check that the
        picture and the controls agree about it. */
    EqFilter filterMode() const noexcept { return state.eq.filter; }

    /** Whether *either* outer node is a cut. */
    bool isFilterMode() const noexcept { return state.eq.filter != EqFilter::off; }

    /** The line of small printed text under the screen, for whichever page is
        showing.

        **ASCII only**, like every other string this module prints: the two
        display faces are licensed individually and live outside this
        repository, so a glyph outside ASCII is one this suite cannot promise
        it can draw. Public because the panel paints it -- it belongs to the
        bezel rather than to the screen -- and because a test should be able to
        read what a page says it is showing. */
    juce::String readout() const;

    /** The menu band's height, inside the display. The suite's switch height,
        because a divided menu segment and a switch are the same kind of object
        -- a small rectangle with a word in it -- and the band is the display's
        own version of the row of segments below the bezel. */
    static constexpr int kMenuBand = ui::Tokens::switchHeight;

    /** The hairline between two menu segments, and the rule under the band. */
    static constexpr float kMenuDivider = 1.0f;

    /** The menu's point size. Smaller than a switch's label, because it is set
        inside a picture rather than on the plate, and `menuLabelOverflow` is
        what says it still fits. */
    static constexpr float kMenuSize = 9.5f;

    /** The TAIL page's window. `kMaxSeconds` is `bmo::kMaxTailSeconds`, the
        clamp on the tail this module and the rack it sits in will ever report,
        so the axis can never be asked to draw a tail longer than the host is
        told about. `tailWindowSeconds` is where the axis actually ends. */
    static constexpr float kMinMs      = 1.0f;
    static constexpr float kMaxSeconds = 30.0f;

    /** How much of the box is left blank after the drawn tail reaches the
        floor, **as a fraction of the width and not as a multiple of the
        time**. On a logarithmic axis those are not the same thing. */
    static constexpr float kAxisAir = 0.08f;

    /** The shortest window the axis will draw, in seconds. */
    static constexpr float kMinWindowS = 0.05f;

    /** How far from the centre axis a hard-panned tap draws on EARLY, as a
        fraction of the half-height. Not 1.0: the plot's own edge is where the
        frame is, and a dot centred on the frame reads as clipped rather than
        as hard panned. */
    static constexpr float kPanReach = 0.88f;

    /** The scatter's dot radii, in pixels: what a tap at the floor draws at
        and what a tap at 0 dB draws at.

        The small end is not zero. A tap at the bottom of the range is still an
        arrival at a bearing, and a dot that shrank to nothing would delete the
        quietest reflections rather than showing them as quiet -- which is the
        opposite of what the gain axis is for. */
    static constexpr float kDotMinRadius = 1.6f;
    static constexpr float kDotMaxRadius = 5.4f;

    /** The bright edge on IN HI-CUT's curtain, in pixels. */
    static constexpr float kCurtainEdge = 2.0f;

    /** The EQ node markers: the filled disc's radius, and how far outside it
        the selection ring is drawn. The ring has to be a ring at a glance
        rather than a thicker dot, which is what the gap buys. */
    static constexpr float kNodeRadius   = 3.6f;
    static constexpr float kNodeRingGap  = 2.6f;

    /** The tick labels' point size. Small, and smaller than the readout's 11:
        these sit *inside* the picture and a tick that competed with the curve
        would be a second thing to read rather than a scale for the first. */
    static constexpr float kTickSize = 8.0f;

    /** The bottom of the TAIL page's level axis, in dB. -72 rather than -60,
        so the tail's own -60 point lands inside the box with room under it
        instead of on the floor itself. */
    static constexpr float kFloorDb = -72.0f;

    /** The bottom of the EARLY page's gain scale, in dB, and **it is the ER
        fader's own bottom rather than the tail's floor**. -40 is where the ER
        fader stops and reads "Off", so a dot at the smallest radius means the
        same thing on the screen as the fader at the bottom of its travel. */
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
        wash, the node markers **and the spectrum**. A spectrum that built its
        own log axis would drift from the one it is drawn against. */
    float eqXFor (double hz) const noexcept;
    float eqYFor (double db) const noexcept;

    /** The plotting area: the component less the menu band, inset. */
    juce::Rectangle<float> plotArea() const noexcept;

    /** Milliseconds to x on the EARLY page's linear window, and on the TAIL
        page's logarithmic one. Members and not lambdas inside the painters,
        because the labels in `axisLabels` and the dots in `tapDot` have to
        land on the same axes the pictures are drawn against. */
    float erXFor (float ms) const noexcept;
    float tailXFor (float ms) const noexcept;

    /** A tap's gain in dB as a dot radius, against `kTapFloorDb`. */
    float dotRadiusFor (float db) const noexcept;

    /** The set the EARLY scatter draws: the selected type's table, the
        current VARIATION, the left channel. */
    const ErChannel& scatterSet() const noexcept;

    /** The Size factor the engine plays the selected type at. */
    float sizeFactor() const noexcept;

    /** Core tap `index` of `scatterSet`, or null. */
    const ErTap* coreTap (int index) const noexcept;

    void paintMenu  (juce::Graphics&, juce::Colour ink) const;
    void paintEarly (juce::Graphics&, juce::Rectangle<float> plot, juce::Colour ink) const;
    void paintTail  (juce::Graphics&, juce::Rectangle<float> plot, juce::Colour ink) const;
    void paintEq    (juce::Graphics&, juce::Rectangle<float> plot, juce::Colour ink) const;

    juce::Colour accent;
    State state;
    Page page = Page::early;
    EqNode selectedNode = EqNode::low;

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
/** A row of rectangular segments: one of N, and which one is lit.

    **One component with two bindings, and that was the decision.** BMO Linger
    uses it twice and the two uses are not the same kind of thing: on EARLY the
    segments are `ermode`, a real three-position choice parameter a host can
    automate, and on EQ they are `ui.node`, which is not a parameter and must
    never become one -- `specs()` is thirty with two lanes spare and which node
    a panel is pointed at does not belong in a session.

    Two nearly identical components was the alternative and it was rejected on
    what the difference actually is. Nothing about the *control* differs: same
    rectangles, same height, same inverted-video selection, same hit test, same
    keyboard-less radio behaviour. What differs is where the chosen index is
    kept, and a control should not know that -- `PageButton` made the same
    argument before it was replaced ("a click asks for a page rather than
    flipping a state"), and BMO Opto's meter row makes it too. So this holds an
    index and an `onSelect`, the panel owns the binding, and the two uses
    differ by four lines in `ReverbPanel`'s constructor rather than by a class.

    **Rectangular, at `ui::Tokens::switchHeight`.** Frosty rejected round keys
    here on 2026-09-22: three circles want a 44 px row, the row would then be
    the third tallest thing on the panel, and a sub-selection is not worth
    that. A rectangle with a word in it is what every switch in the suite is,
    and a segmented row is a line of them that happen to be exclusive.

    The fill is the suite's: the module's accent for the lit segment,
    `switchOff` -- the raised grey every unlit switch is filled with -- for the
    others, and `ui::onAccentOf` for the ink so a label on the accent is
    legible against it rather than against the plate. */
class Segments final : public juce::Component
{
public:
    Segments (juce::StringArray labels, juce::Colour accent);

    void paint (juce::Graphics&) override;
    void mouseUp (const juce::MouseEvent&) override;

    /** Lights segment `index` and nothing else. Silent: it does not call
        `onSelect`, because it is what a binding calls *after* the value has
        already moved. */
    void setSelected (int index);
    int getSelected() const noexcept { return selected; }

    /** A click picked a segment. The panel decides what that means -- a
        parameter gesture on EARLY, a repoint on EQ. */
    std::function<void (int)> onSelect;

    /** Re-colours the row. See `ui::PlainKnob::setAccent`. */
    void setAccent (juce::Colour);

    int numSegments() const noexcept { return labels.size(); }

    /** Segment `index`'s rectangle inside this component, dividers included.
        Public so a layout test can assert the segments tile the row. */
    juce::Rectangle<int> segmentBounds (int index) const;

    /** How much wider segment `index`'s word is than the room it has, in px;
        zero or less fits, and the negative of it is the margin.
        `ui::PlainKnob::captionOverflow`'s argument and its discipline. */
    float labelOverflow (int index) const;

    /** The label's point size. `ui::SwitchButton` derives its own from the box
        height at 62 %, which on a 26 px segment is 16 pt -- calibrated for a
        word running the length of a 70 px box, and these are 90 px boxes with
        four-letter words in them. Pinned instead, as BMO CEQ's square HI-Q
        switch pins its own. */
    static constexpr float kLabelSize = 11.0f;

    /** Air either side of a segment's word, taken off the measurement rather
        than off the drawing, so the margin `labelOverflow` reports is room a
        reader can see. */
    static constexpr float kLabelInset = 5.0f;

private:
    juce::StringArray labels;
    juce::Colour accentColour;
    int selected = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Segments)
};

//==============================================================================
/** BMO Linger's panel: a paged handheld.

    A bezelled screen carrying its own page menu and a line of printed text
    under it; a segmented sub-selection row; two rows of six controls that
    change with the page; and a strip at the foot holding three faders, a rule
    that names them, and a fourth column with TYPE over DECAY.

    ## The height budget

    `ModulePanel::kContentHeight` is 688 suite-wide and `RackEditor` sets every
    panel to it, so **the total cannot move**. What follows is the whole of the
    680 of content area, and it is a reallocation rather than a growth:

        bezel     302   24 of well, a 258 px screen, 4, a 16 px readout row
        cluster   184   a 26 px segment row and two knob rows of 79
        rule       16
        strip     134   fader, caption, value, each on its own line
                  ---
                  636   leaving 44, which is four gaps of 11

    **The screen paid for itself and then some.** It was 105 px and a
    letterbox, because the EQ page was twelve controls in four reserved rows.
    Repointing FREQ / GAIN / Q by a node selector takes that page to six, which
    is the same two rows every other page needs, and the two rows that fall
    free plus the page-key row plus the PAGE rule are what the screen grew
    into. A reverb's display is the part of the panel doing the explaining, and
    258 px is the first version of this face where it is bigger than the
    controls.

    ### Where the odd numbers come from

    **79 is the FILTER ring's box, and the knob row took it.** FILTER is a
    `ui::ConcentricBand` with a null gain -- a legend ring -- and its cap is
    `jmin (width, height) * 0.35`. The cap has to be **27.6 px**, which is what
    `ui::Fader::kCapWidth` is and what every knob on this panel draws, so the
    box has to be 78.86 and a component's bounds are integers: 79, which is
    27.65. Sized by its cell instead, on the old 66 px cluster row, it came out
    at 23 px beside 26.7 px knobs and the row visibly stepped when the page
    turned. `ConcentricBand::capDiameter` exists so that is asserted rather
    than assumed.

    **The knobs are 50 px and not 46.** A 27.6 px cap is 46 px at the suite's
    0.6 face scale, which is where 46 comes from -- but the dotted track sits
    `Tokens::trackGap` = 10 px outside the cap, so it wants a radius of 23.8
    inside a 23 px half-box and the topmost track dot is drawn on the
    component's own edge. It has been all along, at 46 and 0.58. 50 px is the
    smallest box that holds the cap, the gap and the dot, and the cap is still
    27.6 because the face scale comes off the cap rather than the other way
    round.

    ## The screen carries the pages, and the row below carries the node

    The three page keys and their PAGE rule are gone from the plate. The menu
    is drawn inside the display -- see `LingerScreen` -- which is 42 px of
    faceplate back and, more to the point, is where a handheld's page menu
    belongs.

    **The segmented row under the bezel is a sub-selection and says so by being
    there.** On EARLY it is ER MODE, a real parameter, replacing the dropdown
    that control used. On EQ it is LOW / MID / HIGH, choosing which of the
    three EQ nodes the one set of FREQ / GAIN / Q edits -- `ui.node`, UI state,
    refused rather than defaulted on an unknown value. **On TAIL there are no
    segments at all**, and the empty row is the point: nothing on that page is
    three-way, and segments appearing means there is a sub-selection here.

    The row is reserved on every page whether it is filled or not, so the two
    knob rows sit at the same y wherever you are and turning a page does not
    move the controls under it.

    ## One set of FREQ / GAIN / Q, and it re-ranges

    BMO DEQ's band-selector pattern, and the same mechanism: the three knobs
    are rebuilt against the selected node's parameters when the node changes,
    the way `DeqPanel::bindBand` rebuilds eight. **All nine EQ parameters still
    exist and still automate** -- the panel shows three of them at a time.

    **They are not a uniform control and the panel does not pretend they are.**
    FREQ runs 16-1600 Hz on the low shelf, 20 Hz-20 kHz on the bell and
    1 k-20 kHz on the high shelf; Q stops at `kShelfMaxQ` = 2.0 on the two
    shelves and runs to 40 on the bell. So the same knob at the same angle
    means three different frequencies depending on the segment above it. The
    readout line prints real values, which is what keeps it honest, and
    `AGENTS.md` records it because it is the one thing about this arrangement a
    reader would not guess.

    ## FILTER is a legend ring

    `ui::ConcentricBand` with a null gain parameter, which its own comment
    describes as "a filter: a single knob with the same legend around it" and
    which sets `Knob::Style::filter`. The legend is `kEqFilterLegend` --
    OFF / L / H / B -- while the parameter's value strings stay "Off",
    "Lo Cut", "Hi Cut" and "Bandpass" for the host's lane and for the readout.
    BMO CEQ's LO-CUT is the precedent and carries the argument: a 38 px legend
    box cannot set "Bandpass" and an automation lane should not say "B".

    It replaces a `ui::SwitchButton`, which was marked provisional in the code
    that added it: a switch on a four-position choice could only ever reach two
    of them.

    ## The strip, and the fourth column

    Three faders -- `ui::Fader`, ER, REVERB and MIX -- in 134 px: the fader,
    then its caption, then its reading under the caption, which is the suite's
    arrangement and what BMO Dimension does with BELOW over 700 Hz. `kStripRow`
    is a named constant because Frosty has already noted the faders could be
    taller later, and that should be one line.

    **The fourth column is TYPE over DECAY, and both labels sit outside.**
    TYPE's caption is above its dropdown and DECAY's is below its knob. With
    both underneath, the upper label fell between the two controls, and a label
    between two controls binds downward: it read as a second caption for DECAY
    and the column stopped being two controls. `ui::ChoiceBox::setCaptionAbove`
    is the half of that which is not this file's.

    **The LEVEL rule spans the three faders and stops.** It named a fourth
    column it does not describe for one release, and the panel owned that in a
    comment as a wrinkle rather than drawing the truth; `ModulePanel::Rule` now
    carries a span, so it is the same rule, the same hairline and the same
    knocked-out legend, just ending where its subject does.

    ## The page's controls are added and removed, not hidden

    A hidden component still has bounds, and `tests/ui/LayoutTests` walks every
    child of a panel whether it is visible or not -- so a hidden control with a
    stale or zeroed rectangle either escapes the panel, overlaps something, or
    reports a caption overflowing a box of width zero. Unparenting is the one
    state in which a control is genuinely not part of this layout.

    ## Square corners on the body, 3 px on everything cut into it

    `ModulePanel::paint` fills the plate with `fillAll`, so the body is square
    by construction and a slot tiles flush against its neighbours. Everything
    cut *into* the plate -- the bezel and the screen -- takes `Tokens::corner`,
    3 px, uniformly. */
class ReverbPanel final : public ui::ModulePanel
{
public:
    explicit ReverbPanel (ui::ModuleContext);

    void resized() override;

    /** `ui.page=early|tail|eq` and `ui.node=low|mid|high`. Anything else is
        refused -- see the implementation for why refusing beats defaulting. */
    bool setUiState (const juce::String& key, const juce::String& value) override;

    Page getPage() const noexcept { return page; }

    /** Turns to a page: the screen's drawing, its menu band, the segment row
        and the cluster's controls, all at once. There is no other way. */
    void setPage (Page);

    /** Points FREQ / GAIN / Q at a node and moves the ring on the screen's
        marker with them. UI state; see the class comment. */
    void setNode (EqNode);
    EqNode getNode() const noexcept { return node; }

    const LingerScreen& getScreen() const noexcept { return screen; }

    /** The FILTER legend ring, so a layout test can read the cap it drew
        rather than the box it was given. See the class comment for the 79. */
    const ui::ConcentricBand& getFilterRing() const noexcept { return filterRing; }

    /** The segmented row for a page, or null where a page has none -- which is
        TAIL, and the absence is the design rather than an omission. */
    const Segments* segmentsFor (Page) const noexcept;

    /** The cluster controls belonging to `p`, in layout order. Public so a
        layout test can walk a page that is not showing and assert that those
        controls are **not** children of the panel. */
    std::vector<juce::Component*> pageControls (Page p) const;

    /** Everything on the panel whatever the page: the three faders, TYPE and
        DECAY. */
    std::vector<juce::Component*> alwaysOnControls() const;

    //== Where the painted furniture landed ====================================
    //
    // A bezel, a readout line and a rule are all *painted*, so unlike every
    // control on the panel they have no bounds anyone can read. These are
    // public for the reason `ui::ModulePanel::getRules` is: it is the only way
    // a test can see them.

    /** The recess the screen sits in, larger than the screen: it carries the
        readout line as well, which is what the extra height is. */
    juce::Rectangle<int> getBezelBox() const noexcept    { return bezelBox; }
    juce::Rectangle<int> getScreenBox() const noexcept   { return screenBox; }
    juce::Rectangle<int> getReadoutBox() const noexcept  { return readoutBox; }

    /** The row the segments are laid out in, reserved on every page. */
    juce::Rectangle<int> getSegmentBox() const noexcept  { return segmentBox; }

    /** The two knob rows the page's six controls are laid out in. */
    juce::Rectangle<int> getClusterBox() const noexcept  { return clusterBox; }

    /** The strip at the foot: three faders and the TYPE / DECAY column. */
    juce::Rectangle<int> getStripBox() const noexcept    { return stripBox; }

    //== The numbers the budget is made of =====================================

    /** The strip's height. **A named constant because it is the next thing
        that will change**: Frosty has noted the faders could be taller, and
        the whole of that change should be this line and the gap arithmetic
        following it. 134 is the fader body, its caption at 12 pt and its value
        line -- 102 + 18 + 14 -- which leaves 90 px of cap travel. */
    static constexpr int kStripRow = 134;

    /** The cluster's two rows, each. **79 is the FILTER ring's box**: see the
        class comment, which does the arithmetic from the 27.6 px cap
        backwards. Every other control in the row fills its cell, so nothing
        here is special-cased at layout time. */
    static constexpr int kClusterRow = 79;

    /** The knob box in the cluster and in the strip, and the cap it draws.

        The cap comes first and the box is derived: `ui::Fader::kCapWidth` is
        the suite's 27.6, so a fader's cap and a knob's cap are one number
        rather than two that happen to agree. The box is then the smallest that
        holds the cap, the `Tokens::trackGap` outside it and the track's own
        dot -- see the class comment. */
    static constexpr int   kKnobSide  = 50;
    static constexpr float kCapWidth  = ui::Fader::kCapWidth;
    static constexpr float kFaceScale = kCapWidth / (float) kKnobSide;

    /** The readout line's point size, and its row.

        Public because the line is **painted** -- it has no component, so the
        only way a test can ask whether it fits `getReadoutBox` is to measure it
        with the size and the face the paint uses. `PlainKnob::captionOverflow`
        is the discipline; this is the same claim for the one piece of text on
        this panel that is not a control's. */
    static constexpr float kReadoutSize = 11.0f;

private:
    void paintPanel (juce::Graphics&) override;

    /** Re-reads every parameter the screen is drawn from and hands them over
        as one state. */
    void refreshScreen();

    /** Builds FREQ / GAIN / Q against the selected node's three parameters.
        `DeqPanel::bindBand` is the pattern and the precedent. */
    void bindNode();

    /** Greys out GAIN while the selected node is a cut, because a cut has no
        gain. `setKnobEnabled` and **not** `setLockedOn`'s equivalent: the
        parameter still holds whatever the user set, it is never written, and
        switching FILTER off gives the gain back. A mode must not eat an edit.
        `eqGainReachingDesign` is the same decision one folder over, in the
        DSP, and it is the reason the two cannot disagree. */
    void refreshFilterMode();

    /** All of the cluster's controls at once, for the unparenting walk. */
    std::vector<juce::Component*> allPageControls() const;

    Page page = Page::early;
    EqNode node = EqNode::low;

    LingerScreen screen;

    /** The two segmented rows. One class, two bindings -- see `Segments`. */
    Segments erModeSegments, nodeSegments;

    /** ER MODE's segments are a parameter, so the parameter has to be able to
        move them: a host, an automation lane and a preset recall all reach the
        panel this way and none of them goes through a click. */
    std::unique_ptr<juce::ParameterAttachment> erModeAttachment;

    // EARLY. ER SHAPE was here and is a per-type constant now; LINK ER went
    // with `prelink`; ER MODE's dropdown became the segment row above.
    ui::PlainKnob densityKnob, erSpreadKnob, erHiCutKnob, variationKnob, feedKnob, sizeKnob;

    // TAIL. ATTACK, DECAY SHAPE and the two damping knees went into the
    // per-type block. PRE-DELAY arrived here when the persistent row was
    // dissolved: it is the tail's own delay and nothing else's.
    ui::PlainKnob preDelayKnob, widthKnob, modRateKnob,
                  dampLoKnob, dampHiKnob, modDepthKnob;

    // EQ. FREQ, GAIN and Q are rebuilt per node -- see `bindNode` -- so they
    // are owned by pointer where every other control here is a member.
    std::unique_ptr<ui::PlainKnob> freqKnob, gainKnob, qKnob;

    /** FILTER, as a legend ring. `ui::ConcentricBand` with no gain, which is
        what makes it a filter rather than a band. */
    ui::ConcentricBand filterRing;

    ui::PlainKnob inHiCutKnob, outputKnob;

    // The strip at the foot: the two absolute levels, which are the thesis of
    // the module, how much of the whole thing, and the TYPE / DECAY column.
    ui::Fader erLevelFader, verbLevelFader, mixFader;
    ui::ChoiceBox typeBox;
    ui::PlainKnob decayKnob;

    /** One per parameter the screen is drawn from: twenty of
        `LingerScreen::State`'s values plus TYPE, which is not drawn itself but
        carries `attack` now that the knob is gone. The list in the constructor
        is the thing a reader wants to be able to check at a glance. */
    std::array<std::unique_ptr<juce::ParameterAttachment>, 21> screenAttachments;

    juce::Rectangle<int> bezelBox, screenBox, readoutBox, segmentBox, clusterBox, stripBox;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ReverbPanel)
};

} // namespace bmo::reverb
