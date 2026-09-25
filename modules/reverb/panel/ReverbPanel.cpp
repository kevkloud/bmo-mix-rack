#include "ReverbPanel.h"
// The screen's ramp width and the right-hand edge of its TAIL axis are the
// engine's own constants, not copies of them -- so the display and the DSP
// cannot come to disagree about where a tap arrives or how long a tail is
// allowed to be.
#include "modules/reverb/dsp/DspCore.h"
#include "modules/reverb/params.h"

#include <algorithm>
#include <cmath>

namespace bmo::reverb
{

namespace
{
    //== The grid ==============================================================

    /** Three columns, and everything but the strip is laid out against them:
        the segment row centred in them, and the cluster's two rows of three.
        The cell is `(380 - 2 * kPad) / 3 = 120` px. */
    constexpr int kCols = 3;

    /** The strip is the one row that is not three across, because it holds
        four things: ER, REVERB, MIX and the TYPE / DECAY column.

        **And its four cells are not equal.** Four equal cells at 380 are 90 px
        and a `ChoiceBox` insets its box by 6 a side, which leaves 78 for a list
        whose longest item, "Ambience", needs 90 -- it overflowed by 11.7 px,
        measured. So the fourth column keeps **a whole grid cell**, 120 px, and
        the three faders divide the 240 that are left. A fader is 27.6 px of cap
        and its captions are ER, REVERB and MIX, so 80 px apiece is comfortable. */
    constexpr int kFootLevels = 3;

    //== The bezel and its screen ==============================================

    /** How much bezel there is around the screen on every side. The recess is
        noticeably larger than what it holds, which is most of what makes it
        read as a bezel rather than as a border. */
    constexpr int kBezelPad     = 12;

    /** **105 until 2026-09-22, and 258 now.**

        The screen was a letterbox because the EQ page was twelve controls in
        four reserved rows, and four rows on every page is 264 px of cluster on
        a face that only has 680. One set of FREQ / GAIN / Q repointed by a node
        selector takes that page to six, which is the two rows every other page
        needs -- and the two rows that fall free, plus the 36 px page-key row,
        plus its 16 px PAGE rule, plus the gaps that went with them, are what
        this grew into.

        A reverb's screen is the part of the panel doing the explaining: it
        carries the tap scatter, the three decay curves and a spectrum. This is
        the first version of this face where it is bigger than the controls, and
        that is the right way round. */
    constexpr int kScreenHeight = 258;
    constexpr int kScreenToLine = 4;

    /** The line of small printed text under the screen. It carries a reading
        for the page that is showing -- see `LingerScreen::readout`. */
    constexpr int   kReadoutRow  = 16;
    constexpr float kReadoutSize = ReverbPanel::kReadoutSize;

    constexpr int kBezelHeight = kBezelPad * 2 + kScreenHeight + kScreenToLine + kReadoutRow;

    //== The rows ==============================================================

    /** The segmented sub-selection row, at the suite's switch height.

        **Reserved on every page, filled on two.** EARLY puts ER MODE here and
        EQ puts LOW / MID / HIGH; TAIL has nothing three-way on it and the row
        stays empty, which is the design rather than an omission -- segments
        appearing is what says there is a sub-selection on this page. Reserving
        it either way is what keeps the two knob rows at one y, so turning a
        page does not move the controls under it. */
    constexpr int kSegmentRow = ui::Tokens::switchHeight;

    /** How wide the segmented row is, centred in the three columns.

        **Deliberately not 360 and so deliberately not the column grid.** Three
        segments at 120 would sit exactly over the three knobs below them, and
        on the EQ page that reads as column headings -- LOW over FREQ, MID over
        GAIN, HIGH over Q -- which is the opposite of what the row says. At 270
        the segments straddle the columns and cannot be mistaken for them. */
    constexpr int kSegmentsWidth = 270;

    /** Everything else on the face is `ui::Fader` or a knob with its caption
        under it, and **there is one caption size now**. It was 12 pt in the
        persistent row and 10 in the cluster, because the cluster carried
        "EQ HIGH FREQ" at twelve characters; the node selector took those
        captions down to FREQ, GAIN and Q, and the longest caption anywhere on
        the panel is now nine characters in a 120 px cell. Two caption sizes on
        one face was a cost worth paying for a twelve-character caption and is
        not worth paying for nothing. */
    constexpr float kCaptionSize = 12.0f;

    /** The cluster's two rows, and the strip. Both named on `ReverbPanel`,
        which carries the arithmetic -- 79 is the FILTER ring's box and 134 is
        the fader plus its two lines. */
    constexpr int kClusterRow  = ReverbPanel::kClusterRow;
    constexpr int kClusterRows = 2;
    constexpr int kStripRow    = ReverbPanel::kStripRow;

    /** The TYPE / DECAY column, split. TYPE is its caption row over a
        `ChoiceBox::kBoxHeight` box; DECAY is a `kKnobSide` knob over its
        caption and its reading. 44 + 82 is 126 of the strip's 134, and the 8
        that are left fall between them -- which is the gap that stops the two
        reading as one control. */
    constexpr int kTypeBlock   = 18 + ui::ChoiceBox::kBoxHeight;
    constexpr int kDecayBlock  = ReverbPanel::kKnobSide + 18 + 14;
    constexpr int kTypeToDecay = kStripRow - kTypeBlock - kDecayBlock;

    /** Everything above, added up, so the gap between the blocks is whatever is
        left over divided evenly rather than a number somebody has to redo when
        a block's height changes.

        302 + 26 + 158 + 16 + 134 = 636, against the 680 a panel's content area
        has, so the four gaps are 11 px each and the sum is exact.
        `tests/ui/LayoutTests.cpp` is what notices if it stops being exact.

        **`kFaceHeight` and not `kContentHeight`, which is what it was called
        until 2026-09-22 and was a name collision that silently ate the
        arithmetic.** `ui::ModulePanel::kContentHeight` is 688 -- the whole
        slot -- and it is a member of the base class, so inside `resized` the
        unqualified name resolved to *that* and not to the constant three lines
        up: class scope beats namespace scope. `(680 - 688) / 4` is negative, so
        the `jmax` floor won and every gap on this panel was a `switchGap`
        whatever the budget said. The old face's class comment claimed "the five
        gaps are `Tokens::switchGap` exactly", and it was right by accident. */
    constexpr int kFaceHeight = kBezelHeight + kSegmentRow
                              + kClusterRow * kClusterRows
                              + ui::ModulePanel::kRuleRow + kStripRow;

    static_assert (kFaceHeight == 636, "the height budget has moved");
    static_assert (kTypeToDecay >= ui::Tokens::switchGap,
                   "TYPE and DECAY need a gap between them or the column reads as one control");

    //== The screen's own arithmetic ===========================================

    /** How many infill pulses the density bridge has to spend, over and above
        the 21 core taps. 48 taps at the top of the knob, 10 section 3. */
    constexpr int kMaxInfill = 48 - kNumReferenceTaps;

    /** `DspCore::kRampWidth`, read through the DSP's own constant so the
        picture's ramp and the engine's cannot disagree about where a tap
        arrives. */
    constexpr float kRampWidth = DspCore::kRampWidth;

    /** Infill tap `i`'s activation threshold, theta_k.

        Spread over the *open* interval (0, 1) rather than over (0, 1]: with the
        last threshold at exactly 1 its weight is zero at the top of the knob,
        so the forty-eighth tap would never arrive at all. The 21 core taps keep
        theta = 0 and are unaffected: they never switch off, which is what keeps
        the renormalising denominator bounded away from zero and the whole sweep
        continuous (10 section 3). */
    constexpr float infillThreshold (int i) noexcept
    {
        return (float) (i + 1) / (float) (kMaxInfill + 1);
    }

    /** The tail onset a type's ATTACK constant selects, in milliseconds.
        Linear, because the contour is CALIBRATE (10 section 8) and a curve here
        would be a guess drawn as a fact.

        **This is the only place the figure is printed now.** ATTACK lost its
        knob and its value string in the 2026-09-21 trim, so the readout line
        under the TAIL picture is where a user finds out what the selected type
        does to the onset.

        **The readout says ONSET and said BLOOM until 2026-09-21.** Owner
        approved: BMO Dimension ships a control captioned BLOOM
        (`modules/dim/params.h`, `shuffle`), which is Gerzon's bass shuffler and
        has nothing to do with a reverb's tail. **Do not tidy it back.** */
    constexpr float onsetMs (float percent) noexcept { return percent * 1.2f; }

    /** A decade mark on the TAIL axis as a tick label: "10 MS", "1 S". ASCII
        and as short as the number allows.

        **This is what pays for the axis following the tail.** With a fixed
        1 ms - 30 s axis a reader could learn where a second was and never look
        again; with a window that moves, an unlabelled decade line says only
        "a decade happened here". */
    juce::String decadeText (float ms)
    {
        return ms >= 1000.0f ? juce::String (juce::roundToInt (ms / 1000.0f)) + " S"
                             : juce::String (juce::roundToInt (ms)) + " MS";
    }

    /** A frequency for the readout line, ASCII and short: "200 HZ",
        "1.60 KHZ". Three of them have to fit one line under the screen. */
    juce::String hzText (float hz)
    {
        return hz >= 1000.0f ? juce::String (hz / 1000.0f, 2) + " KHZ"
                             : juce::String (juce::roundToInt (hz)) + " HZ";
    }

    /** A round step for a linear time axis: the first of 1, 2, 5, 10 ... that
        leaves no more than eight divisions across `span`. Chosen rather than
        fixed, because the EARLY window scales with SIZE over a 160:1 range and
        a fixed step would be one mark at the small end and four hundred at the
        large one. */
    float niceStepMs (float span) noexcept
    {
        static constexpr float steps[] { 1.0f, 2.0f, 5.0f, 10.0f, 20.0f, 50.0f,
                                         100.0f, 200.0f, 500.0f, 1000.0f, 2000.0f };
        auto step = steps[0];

        for (const auto s : steps)
        {
            step = s;

            if (span / s <= 8.0f)
                break;
        }

        return step;
    }

    /** The three pages in menu order, which is also `Page`'s own order. */
    constexpr Page kPages[] { Page::early, Page::tail, Page::eq };

    /** The three EQ nodes in selector order, low to high, which is `EqNode`'s
        own order and the order the curve crosses them. */
    constexpr EqNode kNodes[] { EqNode::low, EqNode::mid, EqNode::high };
}

//==============================================================================
LingerScreen::LingerScreen (juce::Colour accentColour) : accent (accentColour)
{
    setName ("DISPLAY");

    // **The screen takes clicks now, and the menu band is why.** It took none
    // at all while the page keys were three buttons on the plate. Clicks only:
    // there is nothing here to type into, and a screen that took focus would
    // take it off the control a user was arrowing through.
    setInterceptsMouseClicks (true, false);
}

LingerScreen::~LingerScreen()
{
    // Before `spectrum` is destroyed, so no timer tick can read a
    // half-destroyed analyser. `Spectrum`'s own destructor is what tells the
    // audio thread to stop writing.
    stopTimer();
}

void LingerScreen::setState (const State& s)
{
    const auto& a = s.eq;
    const auto& b = state.eq;

    const auto sameEq = a.filter == b.filter
                     && juce::approximatelyEqual (a.loFreqHz,  b.loFreqHz)
                     && juce::approximatelyEqual (a.loDb,      b.loDb)
                     && juce::approximatelyEqual (a.loQ,       b.loQ)
                     && juce::approximatelyEqual (a.midFreqHz, b.midFreqHz)
                     && juce::approximatelyEqual (a.midDb,     b.midDb)
                     && juce::approximatelyEqual (a.midQ,      b.midQ)
                     && juce::approximatelyEqual (a.hiFreqHz,  b.hiFreqHz)
                     && juce::approximatelyEqual (a.hiDb,      b.hiDb)
                     && juce::approximatelyEqual (a.hiQ,       b.hiQ);

    const auto same = juce::approximatelyEqual (s.sizeM, state.sizeM)
                   && juce::approximatelyEqual (s.preDelayMs, state.preDelayMs)
                   && juce::approximatelyEqual (s.erDensity, state.erDensity)
                   && juce::approximatelyEqual (s.erLevelDb, state.erLevelDb)
                   && juce::approximatelyEqual (s.variation, state.variation)
                   && juce::approximatelyEqual (s.decaySeconds, state.decaySeconds)
                   && juce::approximatelyEqual (s.dampLo, state.dampLo)
                   && juce::approximatelyEqual (s.dampHi, state.dampHi)
                   && juce::approximatelyEqual (s.attack, state.attack)
                   && juce::approximatelyEqual (s.verbLevelDb, state.verbLevelDb)
                   && sameEq
                   && juce::approximatelyEqual (s.inHiCutHz, state.inHiCutHz);

    if (same)
        return;

    state = s;

    // Designing three biquads is a few hundred flops and happens on a knob
    // move, not per pixel and not per frame. `paintEq` reads `nodes`.
    if (! sameEq)
        redesign();

    repaint();
}

void LingerScreen::setAnalyserTap (AnalyserTap* tap)
{
    spectrum.setTap (tap);
}

void LingerScreen::setHostRate (std::function<double()> f)
{
    hostRate = std::move (f);
}

void LingerScreen::redesign()
{
    nodes = EqNodes::design (state.eq, drawnAt);
}

void LingerScreen::setPage (Page p)
{
    if (p == page)
        return;

    page = p;

    // **The timer is the EQ page's and nobody else's.** EARLY and TAIL are
    // parameter-driven, so on those two pages this component is exactly as
    // static as it was before the analyser arrived -- no ticks, no FFT, no
    // repaints between knob moves. The tap itself is left enabled either way:
    // it is the panel's lifetime that owns that, and a page turn is not a panel
    // going away.
    if (page == Page::eq)
        startTimerHz (kFrameHz);
    else
        stopTimer();

    repaint();
}

void LingerScreen::setSelectedNode (EqNode n)
{
    if (n == selectedNode)
        return;

    selectedNode = n;

    // Only the EQ page draws a ring, but the state is kept whatever page is
    // showing: turning away from EQ and back must not lose which node the
    // knobs are on.
    if (page == Page::eq)
        repaint();
}

void LingerScreen::timerCallback()
{
    // The rate first, because the three nodes are designed at it and a host can
    // re-prepare a plugin with its editor open. 0 means "not prepared yet" and
    // leaves the curve on kEqDesignRate.
    if (hostRate != nullptr)
        if (const auto rate = hostRate(); rate > 0.0 && ! juce::approximatelyEqual (rate, drawnAt))
        {
            drawnAt = rate;
            redesign();
            repaint();
        }

    // The spectrum moves on its own, independently of whether a knob has. A
    // closed editor never gets here, and a tap that is null or has not been
    // filled costs a read and a comparison and no repaint.
    if (spectrum.update (drawnAt))
    {
        rebuildSpectrum();
        repaint();
    }
}

void LingerScreen::rebuildSpectrum()
{
    spectrum.buildPath (spectrumPath, plotArea(),
                        [this] (double hz) { return eqXFor (hz); });
}

//==============================================================================
juce::String LingerScreen::menuLabel (Page p) noexcept
{
    switch (p)
    {
        case Page::early: return "EARLY";
        case Page::tail:  return "TAIL";
        case Page::eq:    return "EQ";
    }

    return {};
}

juce::Rectangle<float> LingerScreen::menuBandBounds() const noexcept
{
    return getLocalBounds().toFloat().withHeight ((float) kMenuBand);
}

juce::Rectangle<float> LingerScreen::menuSegment (Page p) const noexcept
{
    const auto band = menuBandBounds();

    // Divided by thirds of the *band* rather than by a fixed segment width, so
    // the three tile it exactly at any screen width and the dividers land on
    // the joins. A test asserts the tiling rather than trusting this.
    const auto x0 = band.getX() + band.getWidth() * (float) ((int) p)     / 3.0f;
    const auto x1 = band.getX() + band.getWidth() * (float) ((int) p + 1) / 3.0f;

    return { x0, band.getY(), x1 - x0, band.getHeight() };
}

float LingerScreen::menuLabelOverflow (Page p) const
{
    // The same face and the same box `paintMenu` draws with, which is the whole
    // of why this lives here. See the declaration.
    const auto room = menuSegment (p).getWidth() - 2.0f * kMenuDivider - 8.0f;

    return juce::GlyphArrangement::getStringWidth (ui::labelFont (kMenuSize, true), menuLabel (p))
             - room;
}

void LingerScreen::mouseUp (const juce::MouseEvent& e)
{
    if (onPageChosen == nullptr || ! menuBandBounds().contains (e.position))
        return;

    for (const auto p : kPages)
        if (menuSegment (p).contains (e.position))
        {
            onPageChosen (p);
            return;
        }
}

//==============================================================================
float LingerScreen::firstTapTimeMs() const noexcept
{
    // Through the table's own function, not through a copy of its arithmetic: a
    // check that re-derived this its own way could agree with the bug it exists
    // to catch.
    //
    // **No pre-delay term.** `kPreLinkFixed` is false, so the ER always travel
    // with dry and the taps never move with PRE-DELAY. The static_assert is what
    // keeps that true: flip the constant and this stops compiling rather than
    // quietly drawing the wrong picture.
    static_assert (! kPreLinkFixed, "the ER scatter is drawn unshifted by PRE-DELAY");

    return tapTimeMsAt (kReferenceTaps[0], state.sizeM);
}

float LingerScreen::lastTapTimeMs() const noexcept
{
    return erSpanMsAt (state.sizeM);
}

float LingerScreen::erWindowMs() const noexcept
{
    return juce::jmax (10.0f, lastTapTimeMs() * 1.1f);
}

float LingerScreen::tailEndSeconds() const noexcept
{
    const auto slowest = std::max (1.0f, std::max (state.dampLo, state.dampHi));
    return state.preDelayMs * 0.001f + state.decaySeconds * slowest;
}

std::array<float, 3> LingerScreen::decayTimesSeconds() const noexcept
{
    // Low, mid, high, in that order and always: the mid is DECAY itself and the
    // two outer ones are DECAY times their own multiplier, which is exactly
    // what LOW x and HIGH x are. A picture that drew one curve three times
    // would pass any "it drew three paths" check and fail this one.
    return { state.decaySeconds * state.dampLo,
             state.decaySeconds,
             state.decaySeconds * state.dampHi };
}

float LingerScreen::tailWindowSeconds() const noexcept
{
    // **The axis follows the tail.** `kMaxSeconds` is the clamp on what this
    // module may report and not a setting anybody uses, so drawing to it at all
    // times left the default 1.8 s tail finishing 60 % across with a flat line
    // over the rest.
    //
    // The air is taken in *width* and not in time -- see `kAxisAir`. Widening
    // the axis by a fraction f of its own span means stretching the decades the
    // tail occupies to 1 / (1 - f) of themselves, which is the exponent below
    // and is why this is not a multiplication.
    const auto endMs   = juce::jmax (kMinMs * 2.0f, tailEndSeconds() * 1000.0f);
    const auto decades = std::log (endMs / kMinMs) / (1.0f - kAxisAir);

    return juce::jlimit (kMinWindowS, kMaxSeconds, kMinMs * std::exp (decades) * 0.001f);
}

float LingerScreen::erXFor (float ms) const noexcept
{
    const auto plot = plotArea();
    const auto window = erWindowMs();

    return plot.getX() + plot.getWidth() * juce::jlimit (0.0f, 1.0f, ms / window);
}

float LingerScreen::tailXFor (float ms) const noexcept
{
    const auto plot     = plotArea();
    const auto windowMs = tailWindowSeconds() * 1000.0f;
    const auto n = std::log (juce::jmax (kMinMs, ms) / kMinMs) / std::log (windowMs / kMinMs);

    return plot.getX() + plot.getWidth() * juce::jlimit (0.0f, 1.0f, (float) n);
}

float LingerScreen::panY (float pan) const noexcept
{
    // -1 is hard left and draws above the axis, +1 is hard right and draws
    // below it. `kPanReach` is why the extremes stop short of the frame.
    const auto plot = plotArea();
    return plot.getCentreY()
             + plot.getHeight() * 0.5f * kPanReach * juce::jlimit (-1.0f, 1.0f, pan);
}

float LingerScreen::lateralSpread() const noexcept
{
    // See the declaration for what is real here and what is a stand-in. The
    // floor is not zero: the phantom centre has to hold at every setting, so
    // the first reflections are near centre anyway, and a variation of 0 is
    // "least decorrelation" rather than "mono".
    const auto n = juce::jlimit (0.0f, 1.0f, state.variation / 6.0f);
    return 0.3f + 0.7f * n;
}

float LingerScreen::dotRadiusFor (float db) const noexcept
{
    const auto n = juce::jlimit (0.0f, 1.0f, (db - kTapFloorDb) / (0.0f - kTapFloorDb));
    return kDotMinRadius + (kDotMaxRadius - kDotMinRadius) * n;
}

LingerScreen::TapDot LingerScreen::tapDot (int index) const noexcept
{
    if (index < 0 || index >= kNumReferenceTaps)
        return {};

    // ER at "Off" draws no taps at all, because "Off" is silence and not
    // -40 dB.
    if (state.erLevelDb <= -39.95f)
        return {};

    const auto& tap = kReferenceTaps[(size_t) index];
    const auto gain = tapGainAt (tap, state.sizeM);

    if (gain <= 0.0f)
        return {};

    const auto db = 20.0f * std::log10 (gain) + state.erLevelDb;

    if (db <= kTapFloorDb)
        return {};

    // **The bearing comes off the same `Tap` row the time and the gain do**, so
    // there is no second table to drift: the engine will play these bearings.
    // `lateralSpread` is the one thing between the table and the picture, and
    // it is marked.
    return { { erXFor (tapTimeMsAt (tap, state.sizeM)),
               panY (tap.pan * lateralSpread()) },
             dotRadiusFor (db) };
}

LingerScreen::TapDot LingerScreen::directDot() const noexcept
{
    const auto plot = plotArea();

    // Gain 1.0 and bearing 0: the direct sound is the reference every arrival
    // on this page is measured from, in time and in bearing both.
    //
    // **Pushed in by its own radius and its ring.** t = 0 is the left frame
    // exactly, and a dot centred there is a dot half outside the box -- which
    // is the clipping `inputCutRegion` was rewritten to stop on the other page.
    // Its x still means zero, and the ring is what says this one is not a
    // reflection.
    const auto radius = dotRadiusFor (0.0f);

    return { { plot.getX() + radius + kNodeRingGap, plot.getCentreY() }, radius };
}

int LingerScreen::activeTapCount() const noexcept
{
    // **The 21 core taps never switch off.** That is not a simplification: it is
    // what keeps the renormalising denominator bounded away from zero, and so
    // what makes the whole density sweep continuous and click-free (10
    // section 3). DENSITY spends infill on top of them.
    const auto d = juce::jlimit (0.0f, 1.0f, state.erDensity * 0.01f);

    int infill = 0;

    for (int i = 0; i < kMaxInfill; ++i)
        if ((d - infillThreshold (i)) / kRampWidth > 0.0f)
            ++infill;

    return kNumReferenceTaps + infill;
}

namespace
{
    /** IN HI-CUT, and it is **not** one of the three EQ nodes -- it is the input
        high-cut in series ahead of them. One pole, no Q, no gain, so it has no
        `EqNodes` entry: giving it a `Biquad` would claim an order that nobody
        has chosen for it (10 section 2 does not say), and an unmarked guess
        about a filter is the same fault as an unmarked CALIBRATE number. */
    float inputHiCutDbAt (float hz, float cornerHz) noexcept
    {
        const auto r = juce::jmax (1.0f, hz) / juce::jmax (1.0f, cornerHz);
        return -10.0f * std::log10 (1.0f + r * r);
    }
}

float LingerScreen::nodeDbAt (EqNode node, float hz) const noexcept
{
    return (float) nodes.nodeDbAt (node, (double) juce::jmax (1.0f, hz), drawnAt);
}

float LingerScreen::responseDbAt (float hz) const noexcept
{
    // The three Reverb EQ nodes, **as the engine's own matched-Z designs**, plus
    // the input high-cut's one pole. Serial, so the dB add.
    return (float) nodes.magnitudeDbAt ((double) juce::jmax (1.0f, hz), drawnAt)
             + inputHiCutDbAt (hz, state.inHiCutHz);
}

std::array<float, 4> LingerScreen::nodeFrequencies() const noexcept
{
    return { state.eq.loFreqHz, state.eq.midFreqHz, state.eq.hiFreqHz, state.inHiCutHz };
}

bool LingerScreen::nodeIsActive (EqNode node) const noexcept
{
    // A cut is always doing something and its GAIN knob is greyed, so reading
    // the gain would draw a 24 dB low cut as a node at rest. See the
    // declaration.
    if (! eqNodeHasGain (node, state.eq.filter))
        return true;

    const auto db = node == EqNode::low ? state.eq.loDb
                  : node == EqNode::mid ? state.eq.midDb
                                        : state.eq.hiDb;

    // Half the parameter's own 0.1 dB step, so the threshold cannot fall
    // between two reachable values and leave a node flickering.
    return std::abs (db) > 0.05f;
}

LingerScreen::NodeMark LingerScreen::nodeMark (EqNode node) const noexcept
{
    const auto hz = node == EqNode::low ? state.eq.loFreqHz
                  : node == EqNode::mid ? state.eq.midFreqHz
                                        : state.eq.hiFreqHz;

    // **On the summed curve, not on the node's own contribution.** What a
    // serial EQ's marker says is "this control's corner is here, and here is
    // what the chain is doing at that corner".
    return { { eqXFor ((double) hz), eqYFor ((double) responseDbAt (hz)) },
             kNodeRadius,
             node == selectedNode,
             nodeIsActive (node) };
}

juce::Rectangle<float> LingerScreen::plotArea() const noexcept
{
    // **Under the menu band.** The band is part of what the display is showing,
    // so the picture starts below it -- and every accessor on this class is in
    // these coordinates, which is what stops a curve being drawn through the
    // menu.
    return getLocalBounds().toFloat().withTrimmedTop ((float) kMenuBand).reduced (5.0f);
}

juce::Rectangle<float> LingerScreen::inputCutRegion() const noexcept
{
    if (page != Page::eq)
        return {};

    const auto plot = plotArea();

    // **Clamped, and that is the whole point of the accessor.** IN HI-CUT's
    // default is 20 kHz, which is the right-hand end of the axis exactly: the
    // open circle this replaced was centred there and drawn half outside the
    // frame.
    const auto x = juce::jmin (eqXFor ((double) state.inHiCutHz),
                               plot.getRight() - kCurtainEdge);

    return { x, plot.getY(), plot.getRight() - x, plot.getHeight() };
}

std::vector<LingerScreen::AxisLabel> LingerScreen::axisLabels() const
{
    const auto plot = plotArea();
    const auto font = ui::labelFont (kTickSize, true);
    const auto row  = kTickSize + 2.0f;

    std::vector<AxisLabel> out;

    switch (page)
    {
        case Page::early:
        {
            // **Which way is up.** The scatter's whole vertical axis is bearing,
            // and a picture with no hand printed on it is one a reader has to
            // guess at -- the guess being fifty-fifty, and wrong half the time
            // on a page whose subject is lateral placement.
            //
            // **At the right-hand end, not the left.** The left edge carries the
            // direct sound's ringed dot, and a letter set over it is a letter
            // nobody can read; the window keeps a tenth of itself clear after
            // the last tap, and that tenth is where these go.
            const auto w = juce::GlyphArrangement::getStringWidth (font, "R") + 2.0f;
            const auto x = plot.getRight() - w - 1.0f;

            out.push_back ({ "L", { x, plot.getY() + 1.0f, w, row } });
            out.push_back ({ "R", { x, plot.getBottom() - row - 1.0f, w, row } });
            break;
        }

        case Page::tail:
        {
            // The decades, labelled, because the axis end moves with the tail --
            // see `decadeText`. Along the foot: the curves only reach the floor
            // at the far right of the window, where the last label has already
            // been dropped for want of room.
            const auto windowMs = tailWindowSeconds() * 1000.0f;

            for (auto ms = 10.0f; ms < windowMs; ms *= 10.0f)
            {
                const auto text = decadeText (ms);
                const auto w    = juce::GlyphArrangement::getStringWidth (font, text) + 2.0f;
                const auto box  = juce::Rectangle<float> (tailXFor (ms) + 2.0f,
                                                          plot.getBottom() - row - 1.0f, w, row);

                // A tick that would run into the frame is not set at all. It is
                // the last decade before the end of the window, its line is
                // still drawn, and half a number is worse than none: this is the
                // MAKEUP failure mode and it is refused here rather than
                // measured after the fact.
                if (box.getRight() <= plot.getRight())
                    out.push_back ({ text, box });
            }

            break;
        }

        case Page::eq:
            // Nothing: the curve has its own captions under the knobs and the
            // readout line carries the three corners. A frequency axis is the
            // one axis a reader already knows the shape of.
            break;
    }

    return out;
}

float LingerScreen::eqXFor (double hz) const noexcept
{
    const auto plot = plotArea();
    const auto n = std::log (juce::jlimit ((double) kMinHz, (double) kMaxHz, hz) / (double) kMinHz)
                     / std::log ((double) kMaxHz / (double) kMinHz);

    return plot.getX() + plot.getWidth() * (float) n;
}

float LingerScreen::eqYFor (double db) const noexcept
{
    const auto plot = plotArea();
    const auto n = juce::jlimit (-1.0, 1.0, db / (double) kEqRangeDb);

    return plot.getCentreY() - plot.getHeight() * 0.5f * (float) n;
}

juce::String LingerScreen::readout() const
{
    switch (page)
    {
        case Page::early:
            // The tap count and the ER window, which are the two numbers the
            // EARLY picture is of and neither of which is on a knob: DENSITY is
            // a per cent and SIZE is metres.
            return juce::String (activeTapCount()) + " TAPS   "
                     + juce::String (firstTapTimeMs(), 1) + "-"
                     + juce::String (lastTapTimeMs(), 1) + " MS";

        case Page::tail:
        {
            // **The two outer curves, in seconds, and the onset.**
            //
            // It printed DECAY and TAIL until 2026-09-22. DECAY is on a knob in
            // the strip and is the mid curve, so printing it here was the one
            // figure on this line a user could already read; TAIL was the
            // slowest of the three, which is a number with no control under it.
            // LOW and HIGH are what LOW x and HIGH x *do* -- the two curves
            // either side of the mid one -- so the line now names the picture.
            //
            // **ONSET stays, and it is the one field here that is not a
            // control.** `attack` was cut into the per-type table in the
            // 2026-09-21 trim, so this number moves when TYPE moves and never
            // when a knob does. It is also the only place the figure appears at
            // all. `onsetMs` carries why it is not called BLOOM.
            const auto t = decayTimesSeconds();

            return "ONSET " + juce::String (juce::roundToInt (onsetMs (state.attack)))
                     + " MS   LOW " + juce::String (t[0], 2)
                     + " S   HIGH " + juce::String (t[2], 2) + " S";
        }

        case Page::eq:
            // The three nodes' corners, in the order the curve crosses them,
            // **and the mode in the words rather than as a fourth field**: the
            // two outer nodes read LOW and HIGH as shelves and LO CUT and HI CUT
            // as filters, so the line says what the picture says.
            //
            // IN HI-CUT is deliberately not on this line: it has its own
            // caption, it is a curtain rather than a node, and it is not part of
            // the Reverb EQ. The three that are, are here.
            //
            // **Per node, not per mode**: with four positions the two outer
            // nodes can be in different shapes at once, and a line that named
            // the mode would have to invent a word for "one of them".
            // `eqCutsLow` and `eqCutsHigh` are the same two predicates
            // `eqShapeOf` branches on, so the words and the shapes cannot come
            // apart.
            return juce::String (eqCutsLow (state.eq.filter) ? "LO CUT " : "LOW ") + hzText (state.eq.loFreqHz)
                     + "   MID " + hzText (state.eq.midFreqHz)
                     + (eqCutsHigh (state.eq.filter) ? "   HI CUT " : "   HIGH ") + hzText (state.eq.hiFreqHz);
    }

    return {};
}

//==============================================================================
void LingerScreen::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();
    const auto plot = plotArea();

    // A dark face, in `meterFace` -- the token a needle meter's scale is printed
    // on. A value rather than a hue, and the same one in both appearances,
    // because a screen that went pale in the light theme would stop reading as a
    // screen.
    g.setColour (ui::tokens().meterFace);
    g.fillRoundedRectangle (bounds, ui::Tokens::corner);

    const auto ink = ui::accentInk (accent, ui::tokens().meterFace);

    // **The spectrum goes in first, under everything**, which is BMO DEQ's order
    // and its reason: it is a filled shape at low alpha that the grid and the
    // curve are read *against*. EARLY and TAIL never have one -- the path is
    // only ever built by the EQ page's own timer.
    if (page == Page::eq && spectrum.isEnabled() && ! spectrumPath.isEmpty())
    {
        g.setColour (Spectrum::colour().withAlpha (0.28f));
        g.fillPath (spectrumPath);
    }

    // The dot matrix: a faint regular grid, which is what makes the box read as
    // a display rather than as a hole in the plate. Drawn from the plot's own
    // origin so the dots do not crawl when the panel is laid out again.
    {
        g.setColour (ui::tokens().hairline.withMultipliedAlpha (0.22f));

        for (auto y = plot.getY() + 3.0f; y < plot.getBottom(); y += 8.0f)
            for (auto x = plot.getX() + 3.0f; x < plot.getRight(); x += 8.0f)
                g.fillRect (juce::Rectangle<float> (x, y, 1.0f, 1.0f));
    }

    switch (page)
    {
        case Page::early: paintEarly (g, plot, ink); break;
        case Page::tail:  paintTail  (g, plot, ink); break;
        case Page::eq:    paintEq    (g, plot, ink); break;
    }

    // **The tick labels come from `axisLabels` and are not built here.** One
    // list, drawn by the painter and measured by the test, for the reason the
    // taps come from one table: a check that laid the labels out its own way
    // could pass while the drawn one clipped. They go on last so nothing can be
    // drawn over a number.
    {
        const auto font = ui::labelFont (kTickSize, true);

        for (const auto& label : axisLabels())
        {
            // The face is punched out under a tick for the reason it is punched
            // out under an EQ node: a 12 s tail runs its curve along the foot of
            // the box, straight through where "10 S" is set.
            g.setColour (ui::tokens().meterFace.withAlpha (0.65f));
            g.fillRect (label.box);

            ui::drawLabel (g, label.text, label.box, juce::Justification::centredLeft, font,
                           ink.withAlpha (0.6f));
        }
    }

    // The menu last of all, so nothing a page draws can reach it.
    paintMenu (g, ink);

    g.setColour (ui::tokens().outline);
    g.drawRoundedRectangle (bounds.reduced (0.5f), ui::Tokens::corner, ui::Tokens::hairlineWeight);
}

//==============================================================================
void LingerScreen::paintMenu (juce::Graphics& g, juce::Colour ink) const
{
    const auto band = menuBandBounds();

    // The band's own ground, so a picture that reached the top of the plot
    // cannot show through the menu. `meterFace` is what the screen is.
    g.setColour (ui::tokens().meterFace);
    g.fillRect (band);

    for (const auto p : kPages)
    {
        const auto seg = menuSegment (p);
        const auto on  = (p == page);

        // **Inverted video for the page you are on.** A block of the screen's
        // own ink with the ground punched out of it in letters, which is what a
        // display does and is not what a button does: there is no bevel, no
        // glow and no second colour anywhere in this band.
        if (on)
        {
            g.setColour (ink);
            g.fillRect (seg.reduced (kMenuDivider, kMenuDivider));
        }

        ui::drawLabel (g, menuLabel (p), seg, juce::Justification::centred,
                       ui::labelFont (kMenuSize, true),
                       on ? ui::tokens().meterFace : ink.withAlpha (0.55f));

        // The divider before each segment but the first: a hairline in the
        // screen's ink at low strength, which is what divides a menu rather than
        // a border round each item.
        if (p != Page::early)
        {
            g.setColour (ink.withAlpha (0.35f));
            g.fillRect (juce::Rectangle<float> (seg.getX(), band.getY() + 3.0f,
                                                kMenuDivider, band.getHeight() - 6.0f));
        }
    }

    // And the rule under the band, which is what says the menu is a header for
    // the picture rather than the top of it.
    g.setColour (ink.withAlpha (0.45f));
    g.fillRect (juce::Rectangle<float> (band.getX(), band.getBottom() - kMenuDivider,
                                        band.getWidth(), kMenuDivider));
}

//==============================================================================
void LingerScreen::paintEarly (juce::Graphics& g, juce::Rectangle<float> plot,
                               juce::Colour ink) const
{
    // **A time x pan scatter, with gain in the radius.** See the class comment
    // for why this replaced mirrored stems, and for what each of the three marks
    // claims. The short version is that VARIATION is a lateral-spread control
    // and this is the picture that has lateral spread as an axis.
    const auto axis   = plot.getCentreY();
    const auto window = erWindowMs();

    // The time marks, at a round step chosen for the window rather than fixed:
    // SIZE moves the window over 160:1 and no single step survives that.
    {
        g.setColour (ui::tokens().hairline.withMultipliedAlpha (0.5f));

        for (auto ms = niceStepMs (window); ms < window; ms += niceStepMs (window))
            g.fillRect (juce::Rectangle<float> (erXFor (ms), plot.getY(),
                                                ui::Tokens::hairlineWeight, plot.getHeight()));
    }

    // The centre axis the bearings are read against: the phantom centre, and the
    // line a tap panned dead centre would sit on.
    g.setColour (ui::tokens().hairline.withMultipliedAlpha (0.85f));
    g.fillRect (juce::Rectangle<float> (plot.getX(), axis, plot.getWidth(),
                                        ui::Tokens::hairlineWeight));

    // ER at "Off" draws no taps at all, because "Off" is silence and not -40 dB.
    // The direct sound is not an ER and stays.
    const bool erAudible = state.erLevelDb > -39.95f;

    // **The infill DENSITY spends, as faint full-height lines and not as dots.**
    // Their bearings are invented -- the 21 core bearings come off `TapTables.h`
    // and these stand in for a master sequence that does not exist yet (10
    // section 3) -- and a dot on this page is a claim about bearing. A line at a
    // time claims only the thing that is true: a tap arrives here. Times are one
    // per equal window with a deterministic nudge, which 10 section 3 says
    // sounds smoother than fully random placement at the same density.
    if (erAudible)
    {
        const auto d     = juce::jlimit (0.0f, 1.0f, state.erDensity * 0.01f);
        const auto first = tapTimeMsAt (kReferenceTaps[0], state.sizeM);
        const auto last  = erSpanMsAt (state.sizeM);
        const auto span  = juce::jmax (1.0f, last - first);

        for (int i = 0; i < kMaxInfill; ++i)
        {
            const auto w = juce::jlimit (0.0f, 1.0f, (d - infillThreshold (i)) / kRampWidth);

            if (w <= 0.0f)
                continue;

            // A fixed integer hash for the jitter, so the picture is the same
            // every time it is drawn and the same on both machines.
            const auto jitter = 1.0f + 0.03f * (float) (((i * 37) % 7) - 3) / 3.0f;
            const auto ms = first + span * ((float) i + 0.5f) / (float) kMaxInfill * jitter;

            g.setColour (ink.withAlpha (0.10f + 0.16f * w));
            g.fillRect (juce::Rectangle<float> (erXFor (ms), plot.getY(),
                                                ui::Tokens::hairlineWeight, plot.getHeight()));
        }
    }

    // The core taps: a filled dot each, at its arrival, at its bearing, sized by
    // its gain. All three numbers off one `Tap` row.
    if (erAudible)
    {
        g.setColour (ink.withAlpha (0.92f));

        for (int i = 0; i < kNumReferenceTaps; ++i)
        {
            const auto dot = tapDot (i);

            if (dot.radius <= 0.0f)
                continue;

            g.fillEllipse (juce::Rectangle<float> (dot.radius * 2.0f, dot.radius * 2.0f)
                               .withCentre (dot.centre));
        }
    }

    // The direct sound: the same dot with a ring around it, which is the mark
    // that says "not a reflection". Leaving it out would make the first
    // reflection look like the beginning of the sound.
    {
        const auto dot = directDot();
        const auto disc = juce::Rectangle<float> (dot.radius * 2.0f, dot.radius * 2.0f)
                              .withCentre (dot.centre);

        g.setColour (ink);
        g.fillEllipse (disc);
        g.drawEllipse (disc.expanded (kNodeRingGap), ui::Tokens::hairlineWeight);
    }
}

//==============================================================================
void LingerScreen::paintTail (juce::Graphics& g, juce::Rectangle<float> plot,
                              juce::Colour ink) const
{
    // **Three curves, and they are LOW x, DECAY and HIGH x.** The single
    // envelope this replaced was drawn at the mid decay with the multipliers
    // shading a band behind it, so LOW x could be swept end to end and the line
    // a reader was looking at never moved. See the class comment.
    const auto windowMs = tailWindowSeconds() * 1000.0f;

    const auto xFor = [this] (float ms) { return tailXFor (ms); };

    const auto yFor = [&] (float db)
    {
        const auto n = juce::jlimit (0.0f, 1.0f, (db - kFloorDb) / (0.0f - kFloorDb));
        return plot.getBottom() - plot.getHeight() * n;
    };

    // Decade marks -- 10, 100 ms, 1 s, 10 s -- because a logarithmic axis with
    // nothing on it reads as a linear one that has gone wrong, and **labelled**
    // because the axis end moves: the labels are drawn from `axisLabels` in
    // `paint`, after the curves, so nothing is drawn over a number.
    {
        g.setColour (ui::tokens().hairline.withMultipliedAlpha (0.5f));

        for (auto ms = 10.0f; ms < windowMs; ms *= 10.0f)
            g.fillRect (juce::Rectangle<float> (xFor (ms), plot.getY(),
                                                ui::Tokens::hairlineWeight, plot.getHeight()));
    }

    // -60 dB, the line the decay time is quoted against, and the floor. Both
    // hairlines, because one of them is where the answer is and the other is
    // where the box stops.
    g.setColour (ui::tokens().hairline.withMultipliedAlpha (0.65f));
    g.fillRect (juce::Rectangle<float> (plot.getX(), yFor (-60.0f), plot.getWidth(),
                                        ui::Tokens::hairlineWeight));
    g.fillRect (juce::Rectangle<float> (plot.getX(), plot.getBottom(), plot.getWidth(),
                                        ui::Tokens::hairlineWeight));

    // REVERB at "Off" draws no tail at all. The pre-delay gap is drawn by its
    // absence: nothing is between the left edge and `tailStartMs`, which is what
    // a gap is.
    if (state.verbLevelDb <= -39.95f)
        return;

    const auto onset = onsetMs (state.attack);

    /** The envelope at `ms`, for a 60 dB time of `t60` seconds. Below the onset
        it rises; after it, it decays. */
    const auto envelopeDb = [&] (float ms, float t60)
    {
        const auto since = ms - state.preDelayMs;

        if (since <= 0.0f)
            return kFloorDb;

        const auto rise = onset > 0.0f && since < onset
                            ? 20.0f * std::log10 (juce::jmax (1.0e-3f, since / onset))
                            : 0.0f;

        const auto decayFrom = onset > 0.0f ? juce::jmax (0.0f, since - onset) : since;

        return state.verbLevelDb + rise - 60.0f * (decayFrom * 0.001f) / juce::jmax (0.01f, t60);
    };

    const auto x0    = xFor (juce::jmax (kMinMs, state.preDelayMs));
    const auto steps = juce::jmax (8, (int) plot.getWidth());

    const auto xAt = [&] (int i)
    {
        return x0 + (plot.getRight() - x0) * (float) i / (float) steps;
    };

    const auto curve = [&] (float t60)
    {
        juce::Path p;

        for (int i = 0; i <= steps; ++i)
        {
            // Back out of the axis to the time this pixel stands for, over the
            // window the tail is actually drawn in.
            const auto n  = (xAt (i) - plot.getX()) / juce::jmax (1.0f, plot.getWidth());
            const auto ms = kMinMs * std::pow (windowMs / kMinMs, n);
            const auto y  = yFor (envelopeDb (ms, t60));

            if (i == 0) p.startNewSubPath (xAt (i), y);
            else        p.lineTo (xAt (i), y);
        }

        return p;
    };

    const auto t = decayTimesSeconds();

    // **The outer two first and lighter, the mid last and heaviest.** The mid is
    // what DECAY says and is the one a reader is meant to follow; the other two
    // are what the damping multipliers do to it, and they are read as a spread
    // about it rather than as three equal claims. Drawing them under the mid
    // curve is also what stops a crossing looking like a break in it.
    g.setColour (ink.withAlpha (0.42f));
    g.strokePath (curve (t[0]), juce::PathStrokeType (1.0f));
    g.strokePath (curve (t[2]), juce::PathStrokeType (1.0f));

    g.setColour (ink);
    g.strokePath (curve (t[1]), juce::PathStrokeType (1.8f));
}

//==============================================================================
void LingerScreen::paintEq (juce::Graphics& g, juce::Rectangle<float> plot,
                            juce::Colour ink) const
{
    // `eqXFor` and `eqYFor` and not two lambdas, because the spectrum behind
    // this is built on the same `eqXFor` -- see `rebuildSpectrum`. A shape that
    // laid out its own log axis would drift from the one it is drawn against.
    const auto xFor = [this] (float hz) { return eqXFor ((double) hz); };
    const auto yFor = [this] (float db) { return eqYFor ((double) db); };

    // The decades, and the line a flat response sits on.
    {
        g.setColour (ui::tokens().hairline.withMultipliedAlpha (0.5f));

        for (auto hz = 100.0f; hz < kMaxHz; hz *= 10.0f)
            g.fillRect (juce::Rectangle<float> (xFor (hz), plot.getY(),
                                                ui::Tokens::hairlineWeight, plot.getHeight()));

        g.setColour (ui::tokens().hairline.withMultipliedAlpha (0.8f));
        g.fillRect (juce::Rectangle<float> (plot.getX(), yFor (0.0f), plot.getWidth(),
                                            ui::Tokens::hairlineWeight));
    }

    // The summed curve, one sample a pixel, computed once and used twice -- for
    // the wash and for the line -- so the two cannot disagree about where the
    // response is.
    const auto steps = juce::jmax (8, (int) plot.getWidth());

    const auto xAt = [&] (int i)
    {
        return plot.getX() + plot.getWidth() * (float) i / (float) steps;
    };

    std::vector<float> ys ((size_t) steps + 1);

    for (int i = 0; i <= steps; ++i)
    {
        const auto n = (float) i / (float) steps;
        ys[(size_t) i] = yFor (responseDbAt (kMinHz * std::pow (kMaxHz / kMinHz, n)));
    }

    juce::Path curve;
    curve.startNewSubPath (xAt (0), ys[0]);

    for (int i = 1; i <= steps; ++i)
        curve.lineTo (xAt (i), ys[(size_t) i]);

    // **The wash between the curve and 0 dB, and it is how FILTER reads as
    // cuts.** Closing the curve onto the zero line rather than onto the floor is
    // what makes the shape mean something: a shelf gives a lens that flattens
    // out past its corner, and a cut gives a wedge that runs off the end of the
    // axis and keeps going. It is the same drawing in both modes -- no branch,
    // no second style -- so nothing has to be kept in step.
    {
        auto region = curve;
        region.lineTo (xAt (steps), yFor (0.0f));
        region.lineTo (xAt (0), yFor (0.0f));
        region.closeSubPath();

        g.setColour (ink.withAlpha (0.14f));
        g.fillPath (region);
    }

    g.setColour (ink);
    g.strokePath (curve, juce::PathStrokeType (1.6f));

    // **IN HI-CUT is a curtain and not a node.** A washed region from the corner
    // to the end of the axis, with a bright edge at the corner and a tab along
    // the top of it, is a different kind of mark at any glance -- and
    // `inputCutRegion` clamps it so that at 20 kHz the edge is inside the plot
    // rather than half-drawn on the frame. What it says is the true thing:
    // everything to the right of here arrives at the room already darkened.
    {
        const auto curtain = inputCutRegion();

        g.setColour (ink.withAlpha (0.10f));
        g.fillRect (curtain);

        // The edge, and a tab running right along the top of the curtain -- both
        // inside the region, so neither can be clipped by the frame.
        g.setColour (ink.withAlpha (0.55f));
        g.fillRect (curtain.withWidth (kCurtainEdge));
        g.fillRect (curtain.withHeight (1.5f));
    }

    // **Three markers, four states, two strokes.** A ring says the FREQ / GAIN /
    // Q knobs are pointed at this node; a fill says the node is shaping the
    // sound. They are independent and they compose -- see the class comment for
    // why both are needed and why neither can stand in for the other.
    for (const auto n : kNodes)
    {
        const auto mark = nodeMark (n);
        const auto disc = juce::Rectangle<float> (mark.radius * 2.0f, mark.radius * 2.0f)
                              .withCentre (mark.centre);

        // The face is punched out under every marker so it reads against the
        // curve and the spectrum both.
        g.setColour (ui::tokens().meterFace);
        g.fillEllipse (disc.expanded (1.6f));

        g.setColour (ink);

        if (mark.filled)
            g.fillEllipse (disc);
        else
            g.drawEllipse (disc.reduced (0.5f), 1.4f);

        if (mark.ringed)
            g.drawEllipse (disc.expanded (kNodeRingGap), ui::Tokens::hairlineWeight);
    }
}

//==============================================================================
Segments::Segments (juce::StringArray segmentLabels, juce::Colour accent)
    : labels (std::move (segmentLabels)), accentColour (accent)
{
    jassert (labels.size() > 1);   // a row of one is not a selection
}

juce::Rectangle<int> Segments::segmentBounds (int index) const
{
    const auto n = juce::jmax (1, labels.size());

    // Divided by exact thirds of the row rather than by a fixed width, so the
    // segments tile the component with nothing left over at either end -- which
    // a fixed width leaves whenever the width is not a multiple of n.
    const auto x0 = getWidth() * juce::jlimit (0, n, index)     / n;
    const auto x1 = getWidth() * juce::jlimit (0, n, index + 1) / n;

    return { x0, 0, x1 - x0, getHeight() };
}

float Segments::labelOverflow (int index) const
{
    if (! juce::isPositiveAndBelow (index, labels.size()))
        return 0.0f;

    // The same face and the same box `paint` draws with. See the declaration.
    return juce::GlyphArrangement::getStringWidth (ui::labelFont (kLabelSize, true), labels[index])
             - ((float) segmentBounds (index).getWidth() - 2.0f * kLabelInset);
}

void Segments::setSelected (int index)
{
    const auto clamped = juce::jlimit (0, juce::jmax (0, labels.size() - 1), index);

    if (clamped == selected)
        return;

    selected = clamped;
    repaint();
}

void Segments::setAccent (juce::Colour colour)
{
    accentColour = colour;
    repaint();
}

void Segments::mouseUp (const juce::MouseEvent& e)
{
    if (onSelect == nullptr)
        return;

    for (int i = 0; i < labels.size(); ++i)
        if (segmentBounds (i).contains (e.getPosition()))
        {
            onSelect (i);
            return;
        }
}

void Segments::paint (juce::Graphics& g)
{
    for (int i = 0; i < labels.size(); ++i)
    {
        const auto seg = segmentBounds (i).toFloat();
        const auto on  = (i == selected);

        // The suite's own switch colours, one shape over: the module's accent
        // for the segment you are on and `switchOff` -- the raised grey every
        // unlit switch in the suite is filled with -- for the rest. Flat, like
        // every other control here.
        const auto fill = on ? accentColour : ui::tokens().switchOff;

        g.setColour (fill);
        g.fillRect (seg);

        // One outline per segment, so the divisions are the outlines meeting
        // rather than a second kind of line drawn between them.
        g.setColour (ui::tokens().outline);
        g.drawRect (seg, ui::Tokens::hairlineWeight);

        // **Ink derived against the fill, not against the plate.** The word sits
        // on the segment, so the ground it has to be legible against is the
        // fill -- which for the lit one *is* the accent, and deriving against
        // the plate would put the accent on top of itself. `ui::onAccentOf` is
        // the call a switch's label makes, and carries the 4.5:1 target.
        ui::drawLabel (g, labels[i], seg.reduced (kLabelInset, 0.0f),
                       juce::Justification::centred,
                       ui::labelFont (kLabelSize, true), ui::onAccentOf (fill));
    }
}

//==============================================================================
ReverbPanel::ReverbPanel (ui::ModuleContext ctx)
    : ModulePanel (std::move (ctx)),
      screen (context.def.accent),

      // **One component, two bindings.** `Segments` carries the argument; the
      // binding is four lines below and in `setNode`.
      erModeSegments ({ kErModeNames[taps], kErModeNames[energy], kErModeNames[blend] },
                      context.def.accent),
      nodeSegments ({ "LOW", "MID", "HIGH" }, context.def.accent),

      // **One colour for the whole module.** `ui::Knob::Style` says what the two
      // faces mean, and the meaning is not "this row" against "that row":
      // `utility` is the pale blue of *input, output, gain*, `character` is the
      // module's accent worn by anything that shapes the sound. So every knob
      // here is `character` but `output`, the one control in this schema that is
      // literally an output trim. The cluster's knobs were `utility` as a block
      // once, and the module rendered violet at the top and suite azure below as
      // though it were two plugins sharing a slot.
      //
      // IN HI-CUT is the near miss and stays `character`: it is a tone control
      // on the way in rather than a level. WIDTH is the other, and stays
      // `character` too -- it sets how wide the tail is made, which is BMO
      // Dimension's DIMENSION and not anybody's gain.
      densityKnob   (context.params.param (Index::erdensity),   "DENSITY",   ui::Knob::Style::character, kFaceScale, context.def.accent),
      erSpreadKnob  (context.params.param (Index::erspread),    "ER SPREAD", ui::Knob::Style::character, kFaceScale, context.def.accent),
      erHiCutKnob   (context.params.param (Index::erhicut),     "ER HI-CUT", ui::Knob::Style::character, kFaceScale, context.def.accent),
      variationKnob (context.params.param (Index::ervariation), "VARIATION", ui::Knob::Style::character, kFaceScale, context.def.accent),
      feedKnob      (context.params.param (Index::feed),        "SOURCE",    ui::Knob::Style::character, kFaceScale, context.def.accent),
      sizeKnob      (context.params.param (Index::size),        "SIZE",      ui::Knob::Style::character, kFaceScale, context.def.accent),

      // TAIL. "LOW x" and not "LOW x" with a multiplication sign: the two
      // display faces are licensed individually and live outside this
      // repository, so a glyph outside ASCII is one this suite cannot promise it
      // can draw. The value strings follow, printing "1.20x".
      preDelayKnob  (context.params.param (Index::predelay),    "PRE-DELAY", ui::Knob::Style::character, kFaceScale, context.def.accent),
      widthKnob     (context.params.param (Index::width),       "WIDTH",     ui::Knob::Style::character, kFaceScale, context.def.accent),
      modRateKnob   (context.params.param (Index::modrate),     "MOD RATE",  ui::Knob::Style::character, kFaceScale, context.def.accent),
      dampLoKnob    (context.params.param (Index::damplo),      "LOW x",     ui::Knob::Style::character, kFaceScale, context.def.accent),
      dampHiKnob    (context.params.param (Index::damphi),      "HIGH x",    ui::Knob::Style::character, kFaceScale, context.def.accent),
      modDepthKnob  (context.params.param (Index::moddepth),    "MOD DEPTH", ui::Knob::Style::character, kFaceScale, context.def.accent),

      // **FILTER is a legend ring and was a switch.** The switch was marked
      // provisional in the code that added it, for the reason that is now fixed:
      // `eqfilter` is a four-position choice and a `ButtonParameterAttachment`
      // could only ever reach two of them. A `ConcentricBand` with a null gain
      // is a filter dial -- `Knob::Style::filter`, no track, the legend around
      // it -- which is how BMO CEQ draws its own LO-CUT.
      filterRing    (context.params.param (Index::eqfilter),
                     context.params.spec  (Index::eqfilter), nullptr, context.def.accent),

      // OUTPUT is the one `utility` knob on this panel: a trim on the way out is
      // exactly what the style is for, and it is the same knob BMO DEQ leaves
      // blue at the foot of its own accent-coloured face.
      //
      // **"IN HI-CUT" is a caption doing real work**: this module has two high
      // cuts. Node 3 with FILTER on is a cut on the reverb path and lives under
      // the node selector; this one is the input's, ahead of the EQ and ahead of
      // both generators. The screen says it a second way, by drawing this one as
      // a curtain and the three EQ nodes as markers.
      inHiCutKnob   (context.params.param (Index::inhicut),     "IN HI-CUT", ui::Knob::Style::character, kFaceScale, context.def.accent),
      outputKnob    (context.params.param (Index::output),      "OUTPUT",    ui::Knob::Style::utility,   kFaceScale, context.def.accent),

      // The strip. **Faders and not knobs**, because what a user judges here is
      // not either number but the balance between them: two caps side by side
      // put that on one line the eye reads without arithmetic, where two
      // pointers at two angles do not. `ui::Fader` carries the rest.
      erLevelFader  (context.params.param (Index::erlevel),   "ER",     context.def.accent),
      verbLevelFader(context.params.param (Index::verblevel), "REVERB", context.def.accent),
      mixFader      (context.params.param (Index::mix),       "MIX",    context.def.accent),

      // TYPE is a dropdown and not a knob: a knob says less and more, and a room
      // type says neither -- Chamber is not more than Room. Frosty's call,
      // 2026-09-21.
      typeBox       (context.params.param (Index::type),
                     context.params.spec  (Index::type),       "TYPE",    context.def.accent),

      decayKnob     (context.params.param (Index::decay),       "DECAY",   ui::Knob::Style::character, kFaceScale, context.def.accent)
{
    addAndMakeVisible (screen);

    // **The spectrum behind the EQ page's curve.** Handing the tap over is what
    // starts the audio thread writing at all, and `LingerScreen`'s destructor
    // hands null back -- the contract `AnalyserTap` is emphatic about.
    screen.setAnalyserTap (context.analyser);

    // Polled and not read once: a matched-Z design is rate-dependent, and a host
    // can re-prepare a plugin with its editor open.
    screen.setHostRate (context.sampleRate);

    // The menu is inside the display, so the display is what a click on a page
    // key reaches. `setPage` is the only way the page moves, from here, from
    // `setUiState` and from nowhere else.
    screen.onPageChosen = [this] (Page p) { setPage (p); };

    //== The two segmented rows, and their two bindings ========================
    //
    // **ER MODE is a parameter and goes through a host gesture**, exactly as
    // `DeqPanel::toggleBand` does: a click here has to automate and undo the way
    // turning a knob does, and the attachment underneath is what lets a host, a
    // lane or a preset recall move the row without a click.
    erModeSegments.setName ("ER MODE");
    erModeSegments.onSelect = [this] (int i)
    {
        auto& p = context.params.param (Index::ermode);

        p.beginChangeGesture();
        context.params.setReal (Index::ermode, (float) i);
        p.endChangeGesture();
    };

    erModeAttachment = std::make_unique<juce::ParameterAttachment> (
        context.params.param (Index::ermode),
        [this] (float) { erModeSegments.setSelected ((int) std::lround (context.params.getReal (Index::ermode))); });

    // **The node selector is UI state and goes nowhere near a parameter.**
    // `specs()` is thirty with two lanes spare, and which node a panel is
    // pointed at is not something a session should carry or a host should
    // automate -- BMO Opto's meter mode and BMO DEQ's selected band are the same
    // kind of thing and reach their panels the same way.
    nodeSegments.setName ("EQ NODE");
    nodeSegments.onSelect = [this] (int i) { setNode (kNodes[(size_t) juce::jlimit (0, 2, i)]); };

    //== The controls =========================================================

    // Input, output and volume are one-piece in the Textured surface whatever
    // their size: they set a level rather than voice the module. Frosty,
    // 2026-09-25. Everything else follows the size rule in texturedFormFor.
    outputKnob.setTexturedForm (ui::Knob::TexturedForm::onePiece);

    for (auto* k : { &densityKnob, &erSpreadKnob, &erHiCutKnob, &variationKnob, &feedKnob, &sizeKnob,
                     &preDelayKnob, &widthKnob, &modRateKnob, &dampLoKnob, &dampHiKnob, &modDepthKnob,
                     &inHiCutKnob, &outputKnob, &decayKnob })
    {
        k->setKnobSide (kKnobSide);
        k->setCaptionSize (kCaptionSize);
    }

    // **The terse legend on the ring and the full names on the lane.** A legend
    // label sits in a 38 x 15 px box and "Bandpass" does not fit in one; a DAW's
    // automation lane, which has room, should not say "B". `setLegend` is paint
    // only and is the shared method that takes one without touching the other.
    filterRing.setName ("FILTER");
    filterRing.setLegend ({ kEqFilterLegend[eqFilterOff],   kEqFilterLegend[eqFilterLoCut],
                            kEqFilterLegend[eqFilterHiCut], kEqFilterLegend[eqFilterBandpass] });

    for (auto* f : { &erLevelFader, &verbLevelFader, &mixFader })
    {
        f->setCaptionSize (kCaptionSize);
        addAndMakeVisible (f);
    }

    // **TYPE's name is above its box and DECAY's is below its knob**, so the two
    // labels of a stacked column both sit outside the pair pointing inward. With
    // both underneath, the upper one fell between the two controls and bound
    // downward -- it read as a second caption for DECAY.
    typeBox.setCaptionSize (kCaptionSize);
    typeBox.setCaptionAbove (true);
    addAndMakeVisible (typeBox);

    // **DECAY is the one control on this panel that prints its value**, and it
    // is the fourth column's own reason: the three faders beside it each carry a
    // reading under their caption (`ui::Fader` has it on by default, because a
    // fader's ticks are deliberately mute), and a knob in that row with no
    // number would read as the one control whose value the panel would not tell
    // you. Every knob in the cluster stays silent -- they say less and more, and
    // the numbers a page cannot show are on the readout line under the screen.
    decayKnob.setShowsValue (true);
    addAndMakeVisible (decayKnob);

    // Every parameter the screen is drawn from redraws it, and the list is
    // exactly `LingerScreen::State`'s fields -- which is the thing a reader
    // wants to be able to check at a glance.
    {
        // **TYPE is in the list and is not itself drawn.** The TAIL picture's
        // onset is `TypeConstants::attack`, which has no parameter since the
        // 2026-09-21 trim, so a type change is the only thing that moves it --
        // and without this the onset would only redraw when some *other* knob
        // happened to move.
        //
        // **`ervariation` joined on 2026-09-22** with the scatter: it is the
        // control the new EARLY picture was chosen for, and it moves no time and
        // no gain, so without this entry the fan would only open when something
        // else was turned.
        const int drawn[] { Index::type,
                            Index::size, Index::predelay,
                            Index::erdensity, Index::erlevel, Index::ervariation,
                            Index::decay, Index::damplo, Index::damphi,
                            Index::verblevel,
                            // **`eqfilter` is the one that would be easiest to
                            // leave out**: it moves no frequency and no gain, it
                            // changes two of the three nodes' *shapes*, and
                            // without this the curve would only redraw when some
                            // other knob happened to move.
                            Index::eqfilter,
                            Index::eqlofreq, Index::eqlo, Index::eqloq,
                            Index::eqmidfreq, Index::eqmid, Index::eqmidq,
                            Index::eqhifreq, Index::eqhi, Index::eqhiq,
                            Index::inhicut };

        static_assert (sizeof (drawn) / sizeof (int) == 21, "one attachment per drawn parameter");

        for (size_t i = 0; i < screenAttachments.size(); ++i)
            screenAttachments[i] = std::make_unique<juce::ParameterAttachment> (
                context.params.param (drawn[i]),
                [this] (float) { refreshScreen(); refreshFilterMode(); });
    }

    // FREQ, GAIN and Q exist from here on; the node they are bound to is what
    // changes. `DeqPanel::bindBand` is the pattern.
    bindNode();

    screen.setPage (page);
    screen.setSelectedNode (node);
    erModeSegments.setSelected ((int) std::lround (context.params.getReal (Index::ermode)));
    nodeSegments.setSelected ((int) node);

    // Draw whatever the parameters already say, which is how a render and a
    // reopened editor come up in the state they were left in.
    refreshScreen();
    refreshFilterMode();
}

//==============================================================================
void ReverbPanel::bindNode()
{
    // **One set of knobs over nine parameters**, rebuilt rather than repointed,
    // because a `PlainKnob` holds a reference to its parameter and a
    // `SliderParameterAttachment` is made once. `DeqPanel::bindBand` rebuilds
    // eight the same way for the same reason, and the cost is three small
    // components on a click that is already a relayout.
    //
    // **The three are not a uniform control.** FREQ is 16-1600 Hz here,
    // 20 Hz-20 kHz on the bell and 1 k-20 kHz on node 3; Q stops at 2.0 on the
    // two shelves and runs to 40 on the bell. The same knob at the same angle
    // means three different frequencies depending on the segment above it, and
    // the readout line printing real values is what keeps that honest. AGENTS.md
    // records it, because it is the one thing about this arrangement a reader
    // would not guess.
    const auto freqIndex = node == EqNode::low ? Index::eqlofreq
                         : node == EqNode::mid ? Index::eqmidfreq
                                               : Index::eqhifreq;
    const auto gainIndex = node == EqNode::low ? Index::eqlo
                         : node == EqNode::mid ? Index::eqmid
                                               : Index::eqhi;
    const auto qIndex    = node == EqNode::low ? Index::eqloq
                         : node == EqNode::mid ? Index::eqmidq
                                               : Index::eqhiq;

    const auto make = [this] (int index, const char* caption)
    {
        auto k = std::make_unique<ui::PlainKnob> (context.params.param (index), caption,
                                                  ui::Knob::Style::character, kFaceScale,
                                                  context.def.accent);
        k->setKnobSide (kKnobSide);
        k->setCaptionSize (kCaptionSize);
        return k;
    };

    freqKnob = make (freqIndex, "FREQ");
    gainKnob = make (gainIndex, "GAIN");
    qKnob    = make (qIndex,    "Q");
}

//==============================================================================
std::vector<juce::Component*> ReverbPanel::pageControls (Page p) const
{
    // const_cast because this is a list of the panel's own members and the
    // caller parents them; making it non-const would mean two copies of a list
    // whose whole value is that there is one.
    auto* self = const_cast<ReverbPanel*> (this);

    switch (p)
    {
        case Page::early:
            // Row one is how the cluster is generated; row two is what is done
            // to it, what it is fed from and how big the room is. SIZE arrived
            // when the persistent row was dissolved: it is the ER picture's own
            // axis -- it scales every tap time -- so the page that draws the
            // taps is where it belongs.
            return { &self->densityKnob, &self->erSpreadKnob, &self->erHiCutKnob,
                     &self->variationKnob, &self->feedKnob, &self->sizeKnob };

        case Page::tail:
            // PRE-DELAY arrived here for the same reason: it is the tail's own
            // delay and the ER never move with it (`kPreLinkFixed`), so a
            // control that was persistent because it looked global was persistent
            // by mistake.
            return { &self->preDelayKnob, &self->widthKnob, &self->modRateKnob,
                     &self->dampLoKnob, &self->dampHiKnob, &self->modDepthKnob };

        case Page::eq:
            // **Six, and nine parameters behind three of them.** Row one is the
            // selected node; row two is the mode, the input cut and the output
            // trim. Reading across row one is one band; the segments above are
            // how you read the other two.
            return { self->freqKnob.get(), self->gainKnob.get(), self->qKnob.get(),
                     &self->filterRing, &self->inHiCutKnob, &self->outputKnob };
    }

    return {};
}

std::vector<juce::Component*> ReverbPanel::alwaysOnControls() const
{
    auto* self = const_cast<ReverbPanel*> (this);

    return { &self->erLevelFader, &self->verbLevelFader, &self->mixFader,
             &self->typeBox, &self->decayKnob };
}

std::vector<juce::Component*> ReverbPanel::allPageControls() const
{
    std::vector<juce::Component*> all;

    for (const auto p : kPages)
        for (auto* c : pageControls (p))
            all.push_back (c);

    return all;
}

const Segments* ReverbPanel::segmentsFor (Page p) const noexcept
{
    // **TAIL has none, and the absence is the design.** Nothing on that page is
    // three-way, and a row of segments appearing is what tells a reader there is
    // a sub-selection here. Returning null rather than an empty row is what
    // makes that a fact the layout and a test can both read.
    switch (p)
    {
        case Page::early: return &erModeSegments;
        case Page::tail:  return nullptr;
        case Page::eq:    return &nodeSegments;
    }

    return nullptr;
}

//==============================================================================
void ReverbPanel::setPage (Page p)
{
    page = p;
    screen.setPage (p);

    // `resized` is what parents the new page's controls and unparents the old
    // one's, so turning a page is a layout and nothing else: no control is
    // rebuilt and no parameter attachment is touched. The one exception is the
    // EQ page's three knobs, which are rebuilt when the *node* changes and not
    // when the page does.
    resized();
    repaint();
}

void ReverbPanel::setNode (EqNode n)
{
    if (n == node)
        return;

    node = n;
    nodeSegments.setSelected ((int) n);
    screen.setSelectedNode (n);

    bindNode();
    refreshFilterMode();
    resized();
    repaint();
}

bool ReverbPanel::setUiState (const juce::String& key, const juce::String& value)
{
    // **Refused, not defaulted**, for both keys. BMO DEQ's comment is the
    // argument and it holds exactly here: a render labelled EQ that shows EARLY
    // is worse than no render, and nothing downstream could tell the two apart.
    // The snapshot tool treats a false as fatal for the same reason.
    if (key == "page")
    {
        if (value.equalsIgnoreCase ("early")) { setPage (Page::early); return true; }
        if (value.equalsIgnoreCase ("tail"))  { setPage (Page::tail);  return true; }
        if (value.equalsIgnoreCase ("eq"))    { setPage (Page::eq);    return true; }

        // **"tone" is refused like any other unknown value**, and deliberately
        // so: it was the third page's key until 2026-09-21 and a render script
        // that still passes it should stop with an error rather than quietly
        // produce an EARLY page labelled TONE.
        return false;
    }

    if (key == "node")
    {
        if (value.equalsIgnoreCase ("low"))  { setNode (EqNode::low);  return true; }
        if (value.equalsIgnoreCase ("mid"))  { setNode (EqNode::mid);  return true; }
        if (value.equalsIgnoreCase ("high")) { setNode (EqNode::high); return true; }

        return false;
    }

    return false;
}

void ReverbPanel::refreshScreen()
{
    LingerScreen::State s;

    // **ATTACK has no parameter since the 2026-09-21 trim**, so the onset the
    // TAIL page draws comes off the selected type's constants -- through
    // `constantsFor`, which is the same call `ReverbDsp::paramsFrom` makes, so
    // the picture and the engine cannot come to disagree about what a Plate's
    // onset is.
    const auto& voicing = constantsFor ((int) context.params.getReal (Index::type));

    s.sizeM        = context.params.getReal (Index::size);
    s.preDelayMs   = context.params.getReal (Index::predelay);
    s.erDensity    = context.params.getReal (Index::erdensity);
    s.erLevelDb    = context.params.getReal (Index::erlevel);
    s.variation    = context.params.getReal (Index::ervariation);

    s.decaySeconds = context.params.getReal (Index::decay);
    s.dampLo       = context.params.getReal (Index::damplo);
    s.dampHi       = context.params.getReal (Index::damphi);
    s.attack       = voicing.attack;
    s.verbLevelDb  = context.params.getReal (Index::verblevel);

    // The Reverb EQ, as the same `EqSettings` the engine builds in
    // `DspCore::eqSettingsFor` -- one struct, one design, so the curve and the
    // sound cannot be two transcriptions of ten numbers.
    s.eq.filter    = eqFilterFor ((int) context.params.getReal (Index::eqfilter));
    s.eq.loFreqHz  = context.params.getReal (Index::eqlofreq);
    s.eq.loDb      = context.params.getReal (Index::eqlo);
    s.eq.loQ       = context.params.getReal (Index::eqloq);
    s.eq.midFreqHz = context.params.getReal (Index::eqmidfreq);
    s.eq.midDb     = context.params.getReal (Index::eqmid);
    s.eq.midQ      = context.params.getReal (Index::eqmidq);
    s.eq.hiFreqHz  = context.params.getReal (Index::eqhifreq);
    s.eq.hiDb      = context.params.getReal (Index::eqhi);
    s.eq.hiQ       = context.params.getReal (Index::eqhiq);

    s.inHiCutHz    = context.params.getReal (Index::inhicut);

    screen.setState (s);

    // The readout line is painted by the panel and lives in the bezel, so it is
    // the panel that has to redraw it when a number under it moves.
    repaint (readoutBox);
}

void ReverbPanel::refreshFilterMode()
{
    // **A cut has no gain, so GAIN greys out.** The claim the greying makes is
    // exactly `eqGainReachingDesign`'s: `dsp::hasGain` is false for
    // `Shape::lowCut` and `Shape::highCut` and the gain never reaches the
    // design. The panel asks the same function rather than repeating the rule,
    // so there is no way for the look and the sound to disagree about which knob
    // is live.
    //
    // **One knob, because there is one GAIN now.** It was two shelf knobs greyed
    // independently; with the node selector the panel is only ever showing one
    // node's gain, so the question is only ever about the node it is showing.
    // FREQ and Q stay live in every position, because a cut has a corner and a
    // resonance and the same two knobs set them.
    const auto filter = eqFilterFor ((int) context.params.getReal (Index::eqfilter));

    if (gainKnob != nullptr)
        gainKnob->setKnobEnabled (eqNodeHasGain (node, filter));
}

//==============================================================================
void ReverbPanel::paintPanel (juce::Graphics& g)
{
    const auto t = panelTokens();
    const auto ink = ui::accentInk (context.def.accent, t.well);

    // The bezel: a recess in `well`, like every other ground cut into a
    // faceplate, holding the screen and the line of printed text under it.
    // `panelTokens()` rather than `tokens()`, so an LTV plate would move the
    // well with it.
    g.setColour (t.well);
    g.fillRoundedRectangle (bezelBox.toFloat(), ui::Tokens::corner);
    g.setColour (ui::tokens().outline);
    g.drawRoundedRectangle (bezelBox.toFloat().reduced (0.5f), ui::Tokens::corner,
                            ui::Tokens::hairlineWeight);

    // The reading for the page that is showing. Small, printed, and the only
    // number on this panel outside DECAY and the three faders.
    ui::drawLabel (g, screen.readout(), readoutBox.toFloat(), juce::Justification::centred,
                   ui::labelFont (kReadoutSize, true), ink);

    //== And that is everything this panel paints ==============================
    //
    // The PAGE rule went with the page keys on 2026-09-22 -- the menu is inside
    // the display now -- so the LEVEL rule is the only one left, and
    // `ModulePanel` paints it.
}

//==============================================================================
void ReverbPanel::resized()
{
    clearRules();

    // 4, the same content inset every other panel measures from.
    auto area = getLocalBounds().reduced (kPad, 4);

    // Four gaps: above the bezel, between the bezel and the segment row, between
    // the cluster and the LEVEL rule, and the last one left at the foot. The gap
    // is what is left over rather than a number of its own, so a block that
    // changes height does not need anything else changed with it.
    const auto gap = juce::jmax (ui::Tokens::switchGap,
                                 (area.getHeight() - kFaceHeight) / 4);

    // 120 px at the design width.
    const auto cell = area.getWidth() / kCols;

    // 80 px, and only the three faders use it. The TYPE / DECAY column takes a
    // whole `cell` -- see `kFootLevels`.
    const auto levelCell = (area.getWidth() - cell) / kFootLevels;

    area.removeFromTop (gap);

    //== The bezel, the screen and the readout =================================
    {
        bezelBox = area.removeFromTop (kBezelHeight);

        auto inner = bezelBox.reduced (kBezelPad);
        screenBox = inner.removeFromTop (kScreenHeight);
        inner.removeFromTop (kScreenToLine);
        readoutBox = inner.removeFromTop (kReadoutRow);

        screen.setBounds (screenBox);
    }

    //== The segmented sub-selection row =======================================
    //
    // Reserved on every page and filled on two. The one that is not showing is
    // **unparented**, not hidden, for the reason the cluster's controls are: a
    // hidden component still has bounds and every walker in the layout suite
    // reads them.
    {
        area.removeFromTop (gap);
        segmentBox = area.removeFromTop (kSegmentRow);

        const auto* showing = segmentsFor (page);

        for (auto* row : { &erModeSegments, &nodeSegments })
        {
            if (row == showing)
            {
                addAndMakeVisible (row);
                row->setBounds (segmentBox.withSizeKeepingCentre (kSegmentsWidth, kSegmentRow));
            }
            else
            {
                removeChildComponent (row);
            }
        }
    }

    //== The page's two rows ===================================================
    {
        clusterBox = area.removeFromTop (kClusterRow * kClusterRows);

        const auto showing = pageControls (page);

        for (auto* c : allPageControls())
            if (std::find (showing.begin(), showing.end(), c) == showing.end())
                removeChildComponent (c);

        for (auto* c : showing)
            addAndMakeVisible (c);

        // **Every control fills its cell, and there is no branch here.** That is
        // what 79 bought: FILTER is a `ConcentricBand` whose cap is a fraction
        // of its box's shorter side, so the row height is the ring's box and the
        // knobs take the same rectangle. The old face special-cased a switch
        // into a 70 x 26 centred in its cell; nothing here is special-cased, so
        // nothing here can be laid out inconsistently between pages.
        auto block = clusterBox;

        for (size_t first = 0; first < showing.size(); first += (size_t) kCols)
        {
            auto cells = block.removeFromTop (kClusterRow);

            const auto n = (int) juce::jmin ((size_t) kCols, showing.size() - first);

            for (int i = 0; i < n; ++i)
                showing[first + (size_t) i]->setBounds (cells.removeFromLeft (cell));
        }
    }

    //== LEVEL, the three faders, and the TYPE / DECAY column ==================
    {
        area.removeFromTop (gap);

        // **The rule spans the three faders it names and stops.** It ran edge to
        // edge over a fourth column it does not describe for a release, and the
        // panel owned that in a comment as a wrinkle; `ModulePanel::Rule` takes
        // a span now, so it is the same hairline and the same knocked-out legend
        // ending where its subject does.
        addRule (area.removeFromTop (ui::ModulePanel::kRuleRow), "LEVEL",
                 { area.getX(), area.getX() + levelCell * kFootLevels });

        stripBox = area.removeFromTop (kStripRow);

        auto row = stripBox;

        for (auto* f : { &erLevelFader, &verbLevelFader, &mixFader })
            f->setBounds (row.removeFromLeft (levelCell));

        // The fourth column: TYPE over DECAY, with the strip's spare 8 px
        // between them. Both labels sit outside the pair -- see the constructor.
        auto column = row;

        typeBox.setBounds (column.removeFromTop (kTypeBlock));
        column.removeFromTop (kTypeToDecay);
        decayKnob.setBounds (column);
    }
}

} // namespace bmo::reverb
