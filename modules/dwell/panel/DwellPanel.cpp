#include "DwellPanel.h"
#include "core/ui/Fonts.h"
#include "modules/dwell/params.h"

#include <cmath>
#include <utility>

namespace bmo::dwell
{

//==============================================================================
/** A row or grid of switches over one choice parameter.

    The click sets the parameter and the parameter lights the switches, so a
    click and host automation cannot disagree -- BMO Saturator's oversampling
    row and BMO DEQ's placement row are the same shape. There is no Off
    position in any of the choices here, so clicking the lit one does nothing.

    `columns` is how many cells stand side by side before the next line starts,
    which is 3 for every row on this panel now. A short last line is centred
    rather than left-hung, which three across three never needs and the old
    seven-cell stack did.

    `namePrefix` renames the *components* without touching what is drawn on
    them, and the lane's FX cells are what it is for. They repeat the main
    delay's words on purpose -- DIFFUSE, PAN and CRUSH under a LANE rule read
    as the same stage on the other engine, where LN-DIFFUSE would read as a bin
    of leftovers -- but `findNamed` in tests/ui/LayoutTests.cpp walks children
    by name, and two cells with one name resolve by child order, which changes
    every time the reveal opens.

    Plain `juce::ToggleButton`s rather than `ui::SwitchButton`s because a
    SwitchButton attaches itself to a *bool*; the look and feel draws both the
    same way, and `tests/ui/LayoutTests.cpp` measures both labels. */
class ChoiceRow final : public juce::Component
{
public:
    ChoiceRow (juce::RangedAudioParameter& parameter, juce::StringArray labels,
               juce::Colour tint, float labelPoints, int columnCount, int gapPx,
               const juce::String& namePrefix = {})
        : columns (juce::jmax (1, columnCount)), gap (gapPx), points (labelPoints),
          attachment (parameter, [this] (float v) { show ((int) std::lround (v)); })
    {
        for (int i = 0; i < labels.size(); ++i)
        {
            auto b = std::make_unique<juce::ToggleButton> (labels[i]);
            b->setColour (juce::ToggleButton::tickColourId, tint);
            b->setClickingTogglesState (false);
            b->getProperties().set (ui::BmoLookAndFeel::kSwitchLabelSize, points);
            b->onClick = [this, i] { attachment.setValueAsCompleteGesture ((float) i); };

            // The drawn label is the button's `text`, set by the constructor
            // above; this moves only the component's name. See the class
            // comment.
            if (namePrefix.isNotEmpty())
                b->setName (namePrefix + "." + labels[i]);

            addAndMakeVisible (*b);
            buttons.push_back (std::move (b));
        }

        // Lights whatever the parameter already says, so a render and a
        // reopened editor both come up in the state they were left in.
        attachment.sendInitialUpdate();
    }

    void resized() override
    {
        const auto n = (int) buttons.size();

        if (n == 0)
            return;

        auto area = getLocalBounds();
        const auto lines = (n + columns - 1) / columns;
        const auto lineHeight = (area.getHeight() - gap * (lines - 1)) / juce::jmax (1, lines);
        const auto cellWidth = (area.getWidth() - gap * (columns - 1)) / columns;

        for (int line = 0; line < lines; ++line)
        {
            auto row = area.removeFromTop (lineHeight);
            area.removeFromTop (gap);

            const auto first = line * columns;
            const auto inLine = juce::jmin (columns, n - first);

            // A short last line is centred, for whatever count does not divide
            // by its columns: one cell hung on the left edge reads as a mistake.
            auto band = row.withSizeKeepingCentre (inLine * cellWidth + (inLine - 1) * gap,
                                                   row.getHeight());

            for (int i = 0; i < inLine; ++i)
            {
                buttons[(size_t) (first + i)]->setBounds (band.removeFromLeft (cellWidth));
                band.removeFromLeft (gap);
            }
        }
    }

    void setRowEnabled (bool shouldBe)
    {
        for (auto& b : buttons)
            b->setEnabled (shouldBe);
    }

    /** What the lit cell draws in.

        For the lane's FX cells, which step back toward the hairline while
        FX LINK is holding them to the main delay's -- see
        DwellPanel::refreshFxLinkFollowing. It is a *colour* change and
        deliberately not `setRowEnabled (false)`: the dim a disabled control
        takes means "this stage is off" everywhere else in the suite, and these
        are not off. */
    void setRowTint (juce::Colour tint)
    {
        for (auto& b : buttons)
            b->setColour (juce::ToggleButton::tickColourId, tint);

        repaint();
    }

private:
    void show (int index)
    {
        for (int i = 0; i < (int) buttons.size(); ++i)
            buttons[(size_t) i]->setToggleState (i == index, juce::dontSendNotification);
    }

    int columns;
    int gap;
    float points;
    std::vector<std::unique_ptr<juce::ToggleButton>> buttons;
    juce::ParameterAttachment attachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ChoiceRow)
};

//==============================================================================
/** The DUCK meter: a horizontal bar reading `currentGainReductionDb`, sitting
    beside the knob that sets it.

    `docs/delay/13` §5 asks for `ui::DynamicsMeter` in its reduction mode, and
    that control cannot be had at this width: it is a needle VU whose radius is
    `width/2 - 8 - 19.5`, so a 130 px cell computes a **negative** radius, and
    the narrowest box it draws anything readable in is about half the panel. A
    bar is what the width affords. §7 puts "meter scale and rate" among the
    things that are free after ship, so this is a layout choice rather than a
    schema one.

    15 Hz, as `modules/opto`, `modules/util` and `modules/tune` run, rather
    than the 30 in `core/ui/Controls.cpp`: half the repaints, ample for a
    180 ms release. */
class DwellPanel::DuckMeter final : public juce::Component,
                                    private juce::Timer
{
public:
    DuckMeter (std::function<float()> source, juce::Colour moduleAccent)
        : reduction (std::move (source)), accent (moduleAccent)
    {
        setName ("GR");
        setInterceptsMouseClicks (false, false);
        startTimerHz (15);
    }

    ~DuckMeter() override { stopTimer(); }

    void paint (juce::Graphics& g) override
    {
        const auto t = ui::panelTokensFor (*this);
        const auto area = getLocalBounds().toFloat();

        g.setColour (t.well);
        g.fillRoundedRectangle (area, ui::Tokens::corner);

        // Reduction grows from the left, the way a horizontal meter reads.
        const auto fraction = juce::jlimit (0.0f, 1.0f, shown / kRangeDb);

        if (fraction > 0.0f)
        {
            g.setColour (ui::accentInk (accent, t.well));
            g.fillRoundedRectangle (area.withWidth (std::max (2.0f, area.getWidth() * fraction)),
                                    ui::Tokens::corner);
        }

        // Quarter marks, so the bar carries a scale without printing numbers
        // beside a knob whose own readout is already in dB.
        g.setColour (t.hairline);

        for (int i = 1; i < 4; ++i)
        {
            const auto x = area.getX() + area.getWidth() * (float) i * 0.25f;
            g.fillRect (juce::Rectangle<float> (x, area.getY() + 2.0f,
                                                ui::Tokens::hairlineWeight, area.getHeight() - 4.0f));
        }

        g.drawRoundedRectangle (area.reduced (0.5f), ui::Tokens::corner, ui::Tokens::hairlineWeight);
    }

private:
    void timerCallback() override
    {
        // Clamped rather than trusted: ModuleContext::gainReductionDb is
        // signed, and whether a negative can arrive depends on the module
        // under it rather than on the declaration.
        const auto now = reduction ? std::max (0.0f, reduction()) : 0.0f;

        if (std::abs (now - shown) > 0.05f)
        {
            shown = now;
            repaint();
        }
    }

    /** The full length of the bar, in dB, matching DUCK's own range. */
    static constexpr float kRangeDb = 24.0f;

    std::function<float()> reduction;
    juce::Colour accent;
    float shown = 0.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DuckMeter)
};

//==============================================================================
namespace
{
    constexpr int kRule = ui::ModulePanel::kRuleRow;   // 16

    //== The three columns =====================================================
    //
    /** 280 less `kPad` each side is a 260 px column, and this panel is laid
        out in whole columns: the face, the main delay's depth, and the lane.

        **All three are 260 from 2026-09-22.** The lane took 400 while it
        mirrored the main delay -- four voicing knobs across a row, and an FX
        band laid out as a 172 px stack of type cells beside a 220 px AMOUNT.
        With the voicing gone its widest row is an `AMOUNT (SMEAR)` caption
        that measures 209 px at 15 pt, which a 260 px column carries with room
        to spare, and its three type cells go across a row at 84 px a cell --
        the same shape, and the same cell, the depth column already uses.

        260 + 20 + 260 + 20 + 260 + 2 * kPad = 840, which is
        `ModuleDef::expandedWidth`. See Module.cpp for why not two columns. */
    constexpr int kColumn = 260;
    constexpr int kGutter = 20;

    /** The arithmetic, asserted rather than described. `resized` never names
        the lane's width -- it takes the two fixed columns off the left and
        gives the lane what is left -- so this is the only thing that would
        catch `expandedWidth` and these drifting apart, and it catches it at
        compile time. */
    static_assert (2 * ui::ModulePanel::kPad + kColumn + kGutter + kColumn
                       + kGutter + kColumn == 840,
                   "the three columns and their gutters are ModuleDef::expandedWidth");

    constexpr int kSwitchHeight = ui::Tokens::switchHeight;   // 26
    constexpr int kSwitchWidth  = ui::Tokens::switchWidth;    // 70
    constexpr int kSwitchGap    = ui::Tokens::switchGap;      // 8

    /** **15 pt -- the suite standard, and what a nine-control face buys.**

        `ui::PlainKnob`'s own default is 15 (core/ui/Controls.h), and a module
        goes below it only when it is cramped: BMO DEQ's squeezed shape dial
        runs at 11. Every earlier attempt at this panel was cramped, and two of
        them dropped to 12 to fit a three-knob primary row into a 260 px
        column -- 86 px a cell, where "FEEDBACK" needs 125 at 15.

        Nothing on this panel is laid out three knobs across a 260 px column
        any more. The face runs in pairs at 130 px a cell; the depth column
        runs a single and a pair; the lane runs a single and a pair too, now
        that its four voicing knobs are gone. No word was shortened to buy the
        size back.

        MOD RATE and MOD DEPTH are still captioned RATE and DEPTH, LOW and HIGH
        CUT LO CUT and HI CUT, and LANE GAIN is TAIL -- a reading choice rather
        than a fit, since a panel's words are not its schema (WORKFLOWS.md's
        control audit).

        **Switch labels are not captions** and keep their own sizes, which the
        suite sets to the cell rather than to a standard: 13 pt for CHARACTER's
        three wide cells, 12 for STEREO's and the FX grids', 14 on the gates
        and on FX, 12 on the two switches that sit on rules, 11 on SYNC.
        Section legends stay at `ui::ModulePanel::kLegendSize`. */
    constexpr float kCaption = 15.0f;

    /** The small print: the two numbered strips under FEEDBACK and MIX, and
        the lane's THROW / FREEZE / BUILD legend. A step under the 13 pt
        section legends. */
    constexpr float kSmallSize = 9.5f;

    /** How much height a PlainKnob spends under its knob, at `kCaption`. The
        same arithmetic `PlainKnob::captionRow` does -- round(points * 1.2) + 4,
        plus round(11 * 1.2) + 1 for a knob that prints its value -- written out
        here because a row's height has to be known before a knob is in it. */
    constexpr int kCaptionRow = 22;
    constexpr int kValueRow   = 14;

    //== Knob sides. Two, and the hierarchy is the point. ======================
    //
    // TIME is the hero of the face and the lane's TAIL is the hero of the
    // lane -- the one control in each column that is reached for first. Every
    // other knob on the panel is the same step down, including the depth
    // column's, which ran smaller again while it was a narrow secondary strip
    // and has no reason to now that it is a column like the others. BMO
    // Saturator's proportions -- a ~120 px DRIVE over a ~90 px TONE and MIX --
    // are what this is set against.
    constexpr int kHeroKnob = 116;
    constexpr int kPairKnob = 86;

    constexpr float kHeroFace = 0.60f;
    constexpr float kPairFace = 0.58f;

    //== Row heights ===========================================================
    constexpr int kHeroRow   = kHeroKnob + kCaptionRow + kValueRow;   // 152
    constexpr int kValuePair = kPairKnob + kCaptionRow + kValueRow;   // 122
    constexpr int kPairRow   = kPairKnob + kCaptionRow;               // 108

    /** The secondary knobs: the cuts, the loop, DUCK and both AMOUNTs. They print
        their values (13 §4: "you cannot tell 6 kHz from 18 kHz on HI CUT
        without dragging it"), and they do it inside the same 108 px row they
        had without one, by drawing the knob a step smaller. The 14 px could
        not come from anywhere else: the depth column's three rows fill 340 of
        the 383 px between the DELAY and TONE rules, and those two lines are
        the grid all three columns are cut on. So the panel has three sizes,
        hero, primary and secondary, rather than two. */
    constexpr int kValueKnob = kPairRow - kCaptionRow - kValueRow;   // 72
    constexpr int kStripRow  = 20;
    constexpr int kSwitchRow = 28;

    /** The lane's three gates, and the face's FX. The controls a hand reaches
        for while something is playing, so they are taller than a switch row
        and lettered a size up. */
    constexpr int kGateRow = 40;

    /** Under the lane's tail knob: THROW, FREEZE, BUILD.

        The band is wider than the knob's own box -- 200 px of a 260 px column
        against the 130 the knob takes -- because it carries three words and
        the knob carries one. 200 gives each word a 66 px cell where "FREEZE"
        measures about 33 at 9.5 pt; the knob's 130 would give 43, which fits
        and looks like it only just does. */
    constexpr int kRegionRow   = 16;
    constexpr int kRegionWidth = 200;

    constexpr int kFootRow   = 28;
    constexpr int kArrowSide = 16;

    /** The face's FX gate: half the column, so it reads as a gate rather than
        as the one thing left on a row. */
    constexpr int kFxSwitchWidth = 126;

    /** Between the FX cells. */
    constexpr int kFxCellGap = ui::Tokens::switchGap;                   // 8

    //== Three FX types, and one shape for them now =============================
    //
    // **Sweep is cut** (DECIDED, Frosty 2026-09-22; modules/dwell/params.h):
    // it was specified as sweeping VOICE's resonant centre, and VOICE was
    // deleted the day before. Three types do not tile as the 2x2 that four
    // did, so both FX stages lay them **across a row**: an 84 px cell in a
    // 260 px column, which is the cell the face's STEREO trio already uses,
    // and a band 28 px tall rather than the 2x2's 60.
    //
    // Either stage's AMOUNT then takes a row of its own and spans the whole
    // column, which is what its caption needs: "AMOUNT (SMEAR)" measures
    // 209 px at 15 pt.
    //
    // **The lane stacked its three down a 172 px slot until 2026-09-22**,
    // beside its AMOUNT, because a 400 px column carrying a voicing block had
    // no height to spare and three across that slot would have cut the cell to
    // 52 px against a "DIFFUSE" that measures about 46. The voicing is gone,
    // the column is 260, and the two stages are the same shape -- which is the
    // truer reading anyway, since they are the same stage on two engines.
    constexpr int kFxRow = kSwitchRow;   // 28

    /** The DUCK bar, on the knob's own centre line. */
    constexpr int kDuckBarHeight = 18;

    /** The arrow's state dot: a filled disc beside the chevrons, drawn only
        while the panel is compact and something behind them has been moved.
        Six pixels, the smallest mark that survives a rack screenshot and still
        smaller than anything on the panel that can be clicked. */
    constexpr int kStateDot    = 6;
    constexpr int kStateDotGap = 8;

    //== The lane gain knob's catch ============================================
    //
    /** How wide the catch at the centre is, as a fraction of the travel either
        side of it.

        3 % of the sweep is 6 units of `lane_gain`, about 1.7 degrees of arc
        either side of centre and a hair under two pixels at the rim of a
        116 px knob. Wide enough that a hand lands on unity without aiming;
        narrow enough that the sliver of the range a *drag* can no longer stop
        in is smaller than the mark drawn over it -- and the wheel, the arrow
        keys, typed entry and automation can all still reach every value in it,
        because the catch is on the drag alone (ui::Knob::setCatch). */
    constexpr double kLaneGainCatch = 0.03;

    /** What the percent moves, per FX type, `docs/delay/13` §6a. Indexed by
        `kFxTypeNames`, so the two lists move together -- and `fxType` is the
        one list in this schema that is still free to change before ship. */
    juce::String amountCaptionFor (int type)
    {
        switch (type)
        {
            case 0:  return "AMOUNT (SMEAR)";    // Diffuse
            case 1:  return "AMOUNT (DEPTH)";    // Pan/Tremolo
            case 2:  return "AMOUNT (BITS)";     // Crush
            default: return "AMOUNT";
        }
    }

    /** Short forms for the three cells. A panel's button labels are not its
        schema (WORKFLOWS.md's control audit), so these can be re-worded
        without touching `kFxTypeNames` -- but the *count* is the schema, and
        it is three from 2026-09-22, with Sweep cut. */
    juce::StringArray fxTypeLabels()
    {
        return { "DIFFUSE", "PAN", "CRUSH" };
    }

    /** Every parameter the reveal carries, which is exactly what the state dot
        reads: **sixteen**, which is everything that is not one of the ten the
        face draws. Six in the depth column, ten in the lane.

        A dot that did not read them would be saying "nothing behind here has
        moved" about a panel whose preset had set the lane to build. Every
        index in this list is drawn behind the arrow and nowhere else, and the
        two facts have to stay one fact: a parameter added to the reveal and
        left out of here is a silent dot. */
    std::vector<int> revealedIndices()
    {
        return { Index::drive, Index::modRate, Index::modDepth, Index::duck,
                 Index::fxType, Index::fxAmount,
                 Index::send, Index::laneGain, Index::hold, Index::chop,
                 Index::laneLevel, Index::laneTime,
                 Index::laneFx, Index::laneFxType, Index::laneFxAmount,
                 Index::fxLink };
    }

    //== The air between the rows ==============================================
    //
    /** Spends whatever a column has left over its rows as air above each of
        them, with a double helping wherever a section changes.

        Pooled into bands the panel read as islands with nothing between them;
        laid on a unit at a time it breathes and the one band that is meant to
        say "new section" is the only large one. The odd pixels go to the
        *first* section break rather than to the foot, because the foot is the
        band nobody is looking at.

        `units` counts every helping the caller is going to ask for -- one per
        `air()`, two per `breakAir()`. Miscount it and the column stops short
        of the foot or runs past it, which is visible in a render; the failure
        this shape is built to avoid is `removeFromTop` silently *clamping*,
        which squashes a row to nothing and passes every assertion there is. */
    struct Column
    {
        juce::Rectangle<int> area;
        int unit = 0;
        int spare = 0;

        void spend (int fixedRows, int units)
        {
            const auto slack = juce::jmax (0, area.getHeight() - fixedRows);

            unit = slack / juce::jmax (1, units);
            spare = slack - unit * units;
        }

        void air (int units = 1) { area.removeFromTop (unit * units); }
        void breakAir()          { area.removeFromTop (unit * 2 + std::exchange (spare, 0)); }

        juce::Rectangle<int> take (int h)     { return area.removeFromTop (h); }
        juce::Rectangle<int> takeFoot (int h) { return area.removeFromBottom (h); }
    };

    //== What each composition adds up to =======================================
    //
    // Written out so a layout's two numbers -- its rows and its helpings of
    // air -- sit beside each other and can be checked by counting the calls.

    /** The face, foot excluded: CHARACTER, the DELAY rule, TIME, the value
        pair, the strips, STEREO, the TONE rule, the tone pair and the FX gate.

        **These are the same two numbers the eleven-control face had**, which
        is not luck and is the reason the DELAY rule is still on row 77 and the
        TONE rule still on 460. SEND and HOLD left a 40 px band and three
        helpings of air behind them, and FX moved off the foot into exactly
        that band -- so the face lost two controls without one of its lines
        moving, and the depth column, which is cut on those lines, did not have
        to be re-laid to follow. */
    constexpr int kFaceRows = kSwitchRow + kRule + kHeroRow + kValuePair + kStripRow
                            + kSwitchRow + kRule + kPairRow + kGateRow;
    constexpr int kFaceAir  = 12;   // 6 single + 3 double

    /** The depth column, in the two segments the face's rules cut it into.

        Above the TONE line: the LOOP rule, DRIVE alone, the MOD pair, and the
        DUCK band. Four bands rather than the trio-and-a-band the two-column
        panel ran, because this column lost the lane's tail to the lane and
        would otherwise have spent a third of its height on air. DRIVE alone on
        a row is also the truer reading: it is the loop's own colour, where the
        two modulation knobs are a pair that means nothing apart.

        Below it: the FX rule and the type grid, with FX AMOUNT anchored to the
        foot so all three columns end on one line. */
    constexpr int kDepthTopRows = kRule + kPairRow + kPairRow + kPairRow;
    constexpr int kDepthTopAir  = 4;   // 2 single + 1 double

    constexpr int kDepthBotRows = kRule + kFxRow;

    // **Two even units, not a single and a double.** Three types across one
    // row spend 32 px less than the 2x2 did, and all 32 of them land in this
    // segment as air. Where they land is the only choice left: AMOUNT is
    // anchored to the foot so the columns end on one line, so whatever is not
    // spent above the cells falls into the gap below them. A single and a
    // double put 25 px under the rule and 51 above the knob, which reads as a
    // hole; two even units put 38 either side of the cells, which reads as a
    // band with air around it.
    constexpr int kDepthBotAir  = 2;   // 2 single, evenly

    /** The lane, in the two segments the face's rules cut it into, exactly as
        the depth column is.

        Above the TONE line: the LANE rule, the tail alone on its row, its
        region legend, and the lane's own TIME and LEVEL as a pair.

        Below it: the FX rule carrying the lane's gate and FX LINK, the three
        type cells, and AMOUNT anchored to the foot so all three columns end on
        one line.

        **Two segments, not one run, from 2026-09-22.** The lane ran as one run
        while it was a second engine: nine bands under its own rule, on interior
        lines at 300 and 547 that were deliberately *clear* of the 460 the other
        two columns share, because a rule struck a dozen pixels off a face rule
        reads as a failed alignment rather than as two sections. Cutting the
        voicing left five bands, which land where the grid wants them, so the
        lane shares the line instead of avoiding it -- and shares it exactly,
        by construction, rather than by arithmetic that has to be redone every
        time a row's height changes.

        **The bottom segment is the depth column's, row for row**: the same
        16 px rule, the same 28 px band of type cells and the same
        foot-anchored AMOUNT, so the two FX stages are laid out on the same
        three lines rather than three pixels apart. The lane's rule carries two
        switches and the depth column's carries none, and that is the only
        difference -- a 26 px switch centred on a 16 px rule row overhangs it
        by five pixels either way, which the air above and below absorbs.
        Giving the lane a taller band instead put its cells 3 px below the
        depth column's, which is exactly the near-miss this grid exists to
        avoid. */
    constexpr int kLaneTopRows = kRule + kHeroRow + kRegionRow + kValuePair;
    constexpr int kLaneTopAir  = 3;   // 3 single

    constexpr int kLaneBotRows = kDepthBotRows;
    constexpr int kLaneBotAir  = kDepthBotAir;
}

//==============================================================================
DwellPanel::DwellPanel (ui::ModuleContext ctx)
    : ModulePanel (std::move (ctx)),
      time     (context.params.param (Index::time),     "TIME",     ui::Knob::Style::character, kHeroFace, context.def.accent),
      note     (context.params.param (Index::note),     "NOTE",     ui::Knob::Style::character, kHeroFace, context.def.accent),
      feedback (context.params.param (Index::feedback), "FEEDBACK", ui::Knob::Style::character, kPairFace, context.def.accent),
      mix      (context.params.param (Index::mix),      "MIX",      ui::Knob::Style::character, kPairFace, context.def.accent),
      lowCut   (context.params.param (Index::lowCut),   "LO CUT",   ui::Knob::Style::character, kPairFace, context.def.accent),
      highCut  (context.params.param (Index::highCut),  "HI CUT",   ui::Knob::Style::character, kPairFace, context.def.accent),
      sync     (context.params.param (Index::sync),     "SYNC", ui::tokens().switchAlt),
      fx       (context.params.param (Index::fx),       "FX",   context.def.accent),
      arrow ([this] { return isShowingExpanded(); }),
      drive    (context.params.param (Index::drive),    "DRIVE", ui::Knob::Style::character, kPairFace, context.def.accent),
      modRate  (context.params.param (Index::modRate),  "RATE",  ui::Knob::Style::character, kPairFace, context.def.accent),
      modDepth (context.params.param (Index::modDepth), "DEPTH", ui::Knob::Style::character, kPairFace, context.def.accent),
      duck     (context.params.param (Index::duck),     "DUCK",  ui::Knob::Style::character, kPairFace, context.def.accent),
      sendHeld (context.params.param (Index::send),     "SEND", context.def.accent),
      hold     (context.params.param (Index::hold),     "HOLD", context.def.accent),
      chop     (context.params.param (Index::chop),     "CHOP", context.def.accent),
      // **ON, not FX.** It sits at the right-hand end of a rule whose legend
      // already says FX, and "FX ---- FX" is the one place on this panel a
      // word would have been printed twice on one line. The face's own FX gate
      // stands in a band with no rule over it, so that one has to say FX; this
      // one does not. Its component name is LANE FX either way.
      laneFx   (context.params.param (Index::laneFx),   "ON",   context.def.accent),
      // **The only link left**, and the only place the lane is still allowed
      // to differ from the main delay. The voicing LINK that stood beside it
      // went with the six parameters it tied (params.h, 2026-09-22); this one
      // stayed because a thrown word crushed against a clean main delay is a
      // sound somebody asks for, where a thrown word with its own low cut is a
      // setting. On the rule of the section it governs, rightmost.
      fxLink   (context.params.param (Index::fxLink),   "LINK", context.def.accent),
      // The lane's tail. One bipolar knob: below centre the caught word
      // decays, at centre it holds at exact unity and above it builds -- three
      // regions of one loop gain rather than three modes, which is why the
      // parameter is a float and this is a knob. The catch and the region
      // legend are what make the middle one findable; see `setCatch` below and
      // `paintPanel`.
      laneGain     (context.params.param (Index::laneGain),     "TAIL",   ui::Knob::Style::character, kHeroFace, context.def.accent),
      laneTime     (context.params.param (Index::laneTime),     "TIME",   ui::Knob::Style::character, kPairFace, context.def.accent),
      laneLevel    (context.params.param (Index::laneLevel),    "LEVEL",  ui::Knob::Style::character, kPairFace, context.def.accent)
{
    character = std::make_unique<ChoiceRow> (context.params.param (Index::character),
                                             juce::StringArray { "CLEAN", "TAPE", "BUCKET" },
                                             ui::tokens().switchAlt, 13.0f, 3, kSwitchGap);

    stereo = std::make_unique<ChoiceRow> (context.params.param (Index::stereo),
                                          juce::StringArray { "STEREO", "PING-PONG", "DUAL" },
                                          ui::tokens().switchAlt, 12.0f, 3, kSwitchGap);

    // **Three across, on one line, with nothing left over to centre**, and the
    // lane's stage is laid out identically -- see kFxRow, and params.h's
    // kFxTypeNames for why there are three of them.
    fxType = std::make_unique<ChoiceRow> (context.params.param (Index::fxType),
                                          fxTypeLabels(),
                                          ui::tokens().switchAlt, 12.0f, 3, kFxCellGap);

    // The lane's cells: the same words off the same list, with prefixed
    // component names. See ChoiceRow's class comment.
    laneFxType = std::make_unique<ChoiceRow> (context.params.param (Index::laneFxType),
                                              fxTypeLabels(),
                                              ui::tokens().switchAlt, 12.0f, 3, kFxCellGap, "LANE");

    duckMeter = std::make_unique<DuckMeter> (context.gainReductionDb, context.def.accent);

    // A component name apiece for everything in the lane that repeats a word
    // the main delay already uses. The *drawn* caption is untouched, and that
    // is the whole point: the lane says TIME because it is a delay time, and
    // the test suite says LANE TIME because `findNamed` cannot resolve two of
    // anything.
    laneGain.setName     ("LANE TAIL");
    laneTime.setName     ("LANE TIME");
    laneLevel.setName    ("LANE LEVEL");
    laneFx.setName       ("LANE FX");
    fxLink.setName       ("FX LINK");

    for (auto* c : std::initializer_list<juce::Component*> {
             &feedback, &mix, &lowCut, &highCut, &sync, &fx, &arrow,
             character.get(), stereo.get() })
        addAndMakeVisible (c);

    for (auto* k : { &time, &note })
    {
        k->setKnobSide (kHeroKnob);
        k->setCaptionSize (kCaption);
        k->setShowsValue (true);
    }

    for (auto* k : { &feedback, &mix })
    {
        k->setKnobSide (kPairKnob);
        k->setCaptionSize (kCaption);

        // Two of the three readouts §4 asks for, and the reason for the strips
        // under them: FEEDBACK and MIX both have a marked point on their
        // travel -- unity near 97 and the dry hinge at 50 -- and a knob you
        // can only set by eye cannot be set to either.
        k->setShowsValue (true);
    }

    for (auto* k : { &lowCut, &highCut, &drive, &modRate, &modDepth, &duck,
                     &laneTime, &laneLevel })
    {
        k->setKnobSide (kPairKnob);
        k->setCaptionSize (kCaption);
    }

    // The lane's own time and level print their numbers, for the same reason
    // the face's FEEDBACK and MIX do: a thrown word's time is set against the
    // phrase it came out of and its level against the main delay's wet, and
    // neither of those is a thing anybody finds by eye.
    laneTime.setShowsValue (true);
    laneLevel.setShowsValue (true);

    // And the secondary knobs, which print theirs for the reason 13 §4 gave:
    // every one of them is in Hertz, decibels or a percent that means
    // something, and none of those is found by eye. See kValueKnob for where
    // the 14 px came from.
    for (auto* k : { &lowCut, &highCut, &drive, &modRate, &modDepth, &duck })
    {
        k->setKnobSide (kValueKnob);
        k->setShowsValue (true);
    }

    //== The lane gain knob ====================================================
    //
    // **The catch at unity.** `lane_gain` is the one control on this panel
    // with a value that has to be hit exactly rather than approached: 0 is the
    // lane holding at unity, and a knob you can only set by eye cannot be set
    // to it. `ui::Knob::setCatch` is new for this and is opt-in, so nothing
    // else in the suite changes.
    //
    // The parameter's own default is -40, an ordinary short decay, so the rest
    // dot and the catch mark sit 40 % of the travel apart and are drawn as two
    // different marks: a dot for where a double-click returns to, a stroke
    // across the track for where a drag stops.
    laneGain.setKnobSide (kHeroKnob);
    laneGain.setCaptionSize (kCaption);
    laneGain.setShowsValue (true);
    laneGain.setCatch (0.0, kLaneGainCatch);

    // SYNC ships disabled: its slot and NOTE's order are permanent from this
    // release, but no host tempo reaches a ModuleDsp until docs/delay/12's
    // plumbing lands, and that is its own workflow.
    sync.setSwitchEnabled (dwell::kSyncIsEnabled);
    sync.setLabelSize (11.0f);

    // The gates and the face's FX are lit from across the room, so their
    // labels are set to the row rather than to the suite's 26 px switch -- and
    // they light in the module's accent rather than switchAlt, with a glow
    // painted behind them by paintPanel, so an engaged gate cannot read as one
    // more CHARACTER or STEREO selection (§3).
    for (auto* s : { &sendHeld, &hold, &chop, &fx })
        s->setLabelSize (14.0f);

    // The lane's FX gate and FX LINK sit on a rule rather than in bands of
    // their own, so they keep the suite's 26 px switch and are lettered to it.
    for (auto* s : { &laneFx, &fxLink })
        s->setLabelSize (12.0f);

    arrow.onClick = [this]
    {
        requestExpanded (! isShowingExpanded());
        arrow.refresh();
    };

    lastSyncWasOn = context.params.getReal (Index::sync) > 0.5f;
    showNote (lastSyncWasOn);

    lastFxWasOn     = context.params.getReal (Index::fx) > 0.5f;
    lastLaneFxWasOn = context.params.getReal (Index::laneFx) > 0.5f;
    lastFxLinkWasOn = context.params.getReal (Index::fxLink) > 0.5f;
    lastMovedWasSet = revealedSectionIsMoved();
    lastLaneRegion  = laneRegion();

    buildFxAmount (false, juce::roundToInt (context.params.getReal (Index::fxType)));
    buildFxAmount (true,  juce::roundToInt (context.params.getReal (Index::laneFxType)));

    refreshFxLinkFollowing();

    // Every child's mouse-ups, so the FX switch's own click can be told apart
    // from the parameter arriving from a host. See mouseUp.
    addMouseListener (this, true);

    startTimerHz (15);
}

DwellPanel::~DwellPanel()
{
    stopTimer();
    removeMouseListener (this);
}

bool DwellPanel::isShowingExpanded() const noexcept
{
    return context.def.isExpandable() && getWidth() >= context.def.expandedWidth;
}

void DwellPanel::requestExpanded (bool expanded)
{
    if (context.setExpanded)
        context.setExpanded (expanded);
}

void DwellPanel::mouseUp (const juce::MouseEvent& e)
{
    const auto cameFrom = [&e] (const ui::SwitchButton& sw)
    {
        return e.eventComponent != nullptr
            && (e.eventComponent == &sw
                || e.eventComponent->findParentComponentOfClass<ui::SwitchButton>() == &sw);
    };

    // Clicking `fx` on while compact opens the reveal once, as a convenience
    // (docs/delay/13 §6a). It is done from the *click* and not from a parameter
    // callback on purpose: automation, preset load and session recall all move
    // the same parameter, and none of them may resize the module. Turning `fx`
    // off never closes it, and the arrow closes it without touching it.
    if (cameFrom (fx)
        && ! isShowingExpanded()
        && context.params.getReal (Index::fx) > 0.5f)
    {
        requestExpanded (true);
    }

    // **Nothing else listens here, and the unlink gesture is why that is worth
    // a line.** The voicing LINK used to call a seeding hook from its own
    // click: unlinking six controls that had been following the main delay had
    // to leave them where the main delay had them, and doing that from the
    // parameter's change rather than from a click would have rewritten six
    // parameters on every automation pass. Both the link and the hook went
    // with the voicing on 2026-09-22. FX LINK is a plain tie with nothing to
    // seed, so it needs no gesture at all.
}

//==============================================================================
void DwellPanel::showNote (bool syncOn)
{
    // One box, one caption, two parameters. The dead one is removed rather than
    // hidden: a knob left parented with no bounds still gets measured by the
    // caption-fit assertion, and it would measure a zero-width box.
    auto& live = syncOn ? note : time;
    auto& dead = syncOn ? time : note;

    removeChildComponent (&dead);
    addAndMakeVisible (live);

    live.setKnobEnabled (dwell::kSyncIsEnabled || ! syncOn);
}

void DwellPanel::showRevealed (bool shown)
{
    if (shown == revealedShown)
        return;

    revealedShown = shown;

    std::vector<juce::Component*> group {
        // The depth column.
        &drive, &modRate, &modDepth, &duck, duckMeter.get(), fxType.get(),
        // The lane.
        &sendHeld, &hold, &chop, &laneGain, &laneTime, &laneLevel,
        &laneFx, &fxLink, laneFxType.get() };

    if (fxAmount != nullptr)
        group.push_back (fxAmount.get());

    if (laneFxAmount != nullptr)
        group.push_back (laneFxAmount.get());

    for (auto* c : group)
    {
        if (shown)
            addAndMakeVisible (c);
        else
            removeChildComponent (c);
    }
}

void DwellPanel::buildFxAmount (bool lane, int type)
{
    auto& slot = lane ? laneFxAmount : fxAmount;
    auto& held = lane ? laneFxAmountType : fxAmountType;

    if (slot != nullptr && held == type)
        return;

    const auto wasPresent = slot != nullptr && slot->getParentComponent() == this;

    slot = std::make_unique<ui::PlainKnob> (context.params.param (lane ? Index::laneFxAmount
                                                                      : Index::fxAmount),
                                            amountCaptionFor (type),
                                            ui::Knob::Style::character,
                                            kPairFace, context.def.accent);
    slot->setKnobSide (kValueKnob);
    slot->setCaptionSize (kCaption);
    slot->setShowsValue (true);
    held = type;

    if (lane)
        slot->setName ("LANE " + amountCaptionFor (type));

    if (wasPresent)
        addAndMakeVisible (*slot);

    refreshFxEnablement();

    // A rebuilt knob is a new object with the module accent on it, so the
    // FOLLOWS MAIN colour has to be put back or changing the lane's FX type
    // would quietly un-follow the knob under the cells.
    refreshFxLinkFollowing();
}

void DwellPanel::refreshFxEnablement()
{
    // With a stage's gate off its two controls grey rather than vanishing, so
    // the width never changes underneath a user (§6a). The DSP skips the stage
    // regardless -- and that is a property of the gate, not of the reveal
    // being open. The two stages answer to their own gates, which is the whole
    // reason the lane has one of its own.
    const auto mainOn = context.params.getReal (Index::fx) > 0.5f;
    const auto laneOn = context.params.getReal (Index::laneFx) > 0.5f;

    fxType->setRowEnabled (mainOn);
    laneFxType->setRowEnabled (laneOn);

    if (fxAmount != nullptr)
        fxAmount->setKnobEnabled (mainOn);

    if (laneFxAmount != nullptr)
        laneFxAmount->setKnobEnabled (laneOn);
}

void DwellPanel::refreshFxLinkFollowing()
{
    const auto t = panelTokens();

    // **Stepped back, not switched off.** The disabled alpha in this suite
    // means "this stage is not running", which is exactly what a followed
    // control is not -- it is running, on the main delay's numbers. So the
    // colour moves and nothing else does: the cells and the knob keep their
    // size, their captions and their clicks, the rule above them says in words
    // what has happened to them, and paintPanel brackets them and points the
    // bracket back at the column they are following.
    const auto followed = context.def.accent.interpolatedWith (t.hairline, 0.55f);
    const auto own      = context.def.accent;

    // **One block now.** This treatment was worked out for two -- the voicing
    // LINK and this one -- and was written as one lambda over both so that a
    // reader who had understood one had understood the other. The voicing went
    // on 2026-09-22 and the treatment stayed exactly as it was, because it was
    // never about there being two of them.
    const auto laneFxInk = context.params.getReal (Index::fxLink) > 0.5f ? followed : own;

    if (laneFxType != nullptr)
        laneFxType->setRowTint (laneFxInk == own ? ui::tokens().switchAlt : laneFxInk);

    if (laneFxAmount != nullptr)
        laneFxAmount->setAccent (laneFxInk);
}

bool DwellPanel::revealedSectionIsMoved() const
{
    for (const auto i : revealedIndices())
    {
        const auto& spec = context.params.spec (i);

        // A step's worth of slack for a continuous control, so a lane that
        // lands a fraction off its default after a round trip through a host's
        // normalised float does not light the dot on an untouched instance. A
        // bool or a choice has a step of 1 and is compared exactly enough.
        const auto slack = juce::jmax (1.0e-4f, spec.step * 0.5f);

        if (std::abs (context.params.getReal (i) - spec.def) > slack)
            return true;
    }

    return false;
}

int DwellPanel::laneRegion() const
{
    // Half a step either side of centre, which is the same slack the state dot
    // allows: `lane_gain` steps in 0.1, so a drag caught at the detent lands
    // exactly on 0 and a value that came back off a host's normalised lane
    // lands well within 0.05.
    const auto slack = context.params.spec (Index::laneGain).step * 0.5f;
    const auto v = context.params.getReal (Index::laneGain);

    return v < -slack ? -1 : (v > slack ? 1 : 0);
}

void DwellPanel::timerCallback()
{
    const auto syncOn = context.params.getReal (Index::sync) > 0.5f;

    if (syncOn != lastSyncWasOn)
    {
        lastSyncWasOn = syncOn;
        showNote (syncOn);
        resized();
        repaint();
    }

    const auto fxOn = context.params.getReal (Index::fx) > 0.5f;
    const auto laneFxOn = context.params.getReal (Index::laneFx) > 0.5f;

    if (fxOn != lastFxWasOn || laneFxOn != lastLaneFxWasOn)
    {
        lastFxWasOn = fxOn;
        lastLaneFxWasOn = laneFxOn;
        refreshFxEnablement();
        repaint();                 // the glow follows both gates
    }

    // FX LINK changes what its controls look like *and* what its rule says, so
    // it costs a re-layout rather than only a repaint: a rule's text is
    // recorded where the rule is laid out.
    const auto fxLinkOn = context.params.getReal (Index::fxLink) > 0.5f;

    if (fxLinkOn != lastFxLinkWasOn)
    {
        lastFxLinkWasOn = fxLinkOn;
        refreshFxLinkFollowing();
        resized();
        repaint();
    }

    // The region legend under the lane's tail. Polled with everything else:
    // the knob, a lane and a preset can all move it, and all three have to
    // relight the word.
    const auto region = laneRegion();

    if (region != lastLaneRegion)
    {
        lastLaneRegion = region;
        repaint (laneRegionBand.expanded (2));
    }

    // The state dot. Polled rather than listened for: it reads sixteen
    // parameters, any of them can be moved by a knob, a lane or a preset, and
    // all three have to light it.
    const auto moved = revealedSectionIsMoved();

    if (moved != lastMovedWasSet)
    {
        lastMovedWasSet = moved;
        repaint();
    }

    // The captions name what each percent moves, so they follow their own
    // types. Only the captions: nothing here asks the host for a width.
    buildFxAmount (false, juce::roundToInt (context.params.getReal (Index::fxType)));
    buildFxAmount (true,  juce::roundToInt (context.params.getReal (Index::laneFxType)));

    const auto needsBounds = [this] (const std::unique_ptr<ui::PlainKnob>& k)
    {
        return k != nullptr && k->getParentComponent() == this && k->getBounds().isEmpty();
    };

    if (needsBounds (fxAmount) || needsBounds (laneFxAmount))
        resized();
}

//==============================================================================
// The bands. Each takes the row it is to fill and nothing else.

void DwellPanel::placeHero (juce::Rectangle<int> row)
{
    // **The hero is centred on the column; SYNC sits beside it.**
    //
    // TIME's box is the middle of three cells -- a switch's width either side
    // of it -- so the knob, which PlainKnob draws square and centred, lands on
    // the column's own centre line. SYNC takes the right-hand cell; the left
    // one is left bare, and bare plate is what the symmetry costs.
    //
    // The cell is taken off rather than drawn over: `checkNoOverlap` in
    // tests/ui/LayoutTests.cpp compares the panel's children, and a hero box
    // spanning the whole column would overlap the switch standing in it.
    row.removeFromLeft (kSwitchWidth);
    auto right = row.removeFromRight (kSwitchWidth);

    (lastSyncWasOn ? note : time).setBounds (row);

    // Level with the knob's face rather than with its caption: SYNC decides
    // what the dial *is*, so it reads against the dial.
    const auto knobTop = row.getY() + (row.getHeight() - kCaptionRow - kValueRow - kHeroKnob) / 2;

    sync.setBounds (right.withSizeKeepingCentre (kSwitchWidth, kSwitchHeight)
                         .withY (knobTop + (kHeroKnob - kSwitchHeight) / 2));
}

void DwellPanel::placePair (ui::PlainKnob& leftKnob, ui::PlainKnob& rightKnob,
                            juce::Rectangle<int> row, int knobSide)
{
    for (auto* k : { &leftKnob, &rightKnob })
    {
        k->setKnobSide (knobSide);
        k->setCaptionSize (kCaption);
    }

    leftKnob.setBounds (row.removeFromLeft (row.getWidth() / 2));
    rightKnob.setBounds (row);
}

void DwellPanel::placeSingle (ui::PlainKnob& knob, juce::Rectangle<int> row, int knobSide)
{
    knob.setKnobSide (knobSide);
    knob.setCaptionSize (kCaption);

    // Half the column, centred, which is the cell a pair would have given it:
    // a lone knob spanning the full width draws in the same place and gives
    // its caption more room than any other knob on the panel has, which reads
    // as a different size of control rather than as the same one alone.
    knob.setBounds (row.withSizeKeepingCentre (row.getWidth() / 2, row.getHeight()));
}

void DwellPanel::placeGates (juce::Rectangle<int> row)
{
    // SEND, HOLD and CHOP: the lane's three gates, in the band above its rule.
    //
    // **Three, and they are a set.** SEND gates the lane's input a word at a
    // time, HOLD gates its life and clears it when it goes off, CHOP gates its
    // output for a stutter. They are what a hand goes to while something is
    // playing, so they are the one knob-free row on this panel sized for a
    // hand rather than for the grid -- and they sit over the lane's own column
    // and above its rule for the same reason CHARACTER sits above the DELAY
    // rule: they govern the whole of what is under them.
    const auto cell = (row.getWidth() - kSwitchGap * 2) / 3;

    sendHeld.setBounds (row.removeFromLeft (cell));
    row.removeFromLeft (kSwitchGap);
    hold.setBounds (row.removeFromLeft (cell));
    row.removeFromLeft (kSwitchGap);
    chop.setBounds (row);
}

void DwellPanel::placeSwitchRow (ChoiceRow& switches, juce::Rectangle<int> row)
{
    switches.setBounds (row.withSizeKeepingCentre (row.getWidth(), kSwitchHeight));
}

void DwellPanel::placeRuledSwitch (juce::Rectangle<int> band, const juce::String& legend,
                                   ui::SwitchButton& rightmost, ui::SwitchButton* inner)
{
    // The switches at the right-hand end, the rule struck across what is left.
    //
    // A section's own switch belongs to the section, and the two other places
    // it could go both say something wrong: in the first row under the rule it
    // reads as a sibling of the controls it governs, and in a band of its own
    // it spends 40 px of column saying one word. On the rule it reads as what
    // it is.
    //
    // **`band` is the rule's own 16 px row and the switches overhang it**, by
    // five pixels either way, which the air above and below absorbs. A taller
    // band would move the hairline off the line the other columns strike
    // theirs on, which is the whole grid.
    //
    // **The link is the last thing on the rule**, with the gate inside it, so
    // the row reads left to right as "this section, this stage, tied to the
    // other one". Two rules used this while the lane had a voicing link as
    // well, and the rule that the rightmost switch is always the same kind of
    // control is what let the eye read a column of them; one rule keeps the
    // order anyway, because it is the order the words go in.
    const auto cell = [] (juce::Rectangle<int>& b)
    {
        auto c = b.removeFromRight (kSwitchWidth);
        b.removeFromRight (kSwitchGap);
        return c.withSizeKeepingCentre (kSwitchWidth, kSwitchHeight);
    };

    rightmost.setBounds (cell (band));

    if (inner != nullptr)
        inner->setBounds (cell (band));

    addRule (band.withSizeKeepingCentre (band.getWidth(), kRule), legend);
}

void DwellPanel::placeDuckBand (juce::Rectangle<int> row)
{
    duck.setKnobSide (kValueKnob);
    duck.setCaptionSize (kCaption);

    // Where a knob's face stops and its name starts, worked out the way
    // PlainKnob::resized does it, so the bar and its name land on the same two
    // lines the knob beside them uses.
    const auto knobTop    = row.getY() + (row.getHeight() - kCaptionRow - kValueRow - kValueKnob) / 2;
    const auto captionTop = knobTop + kValueKnob;

    duck.setBounds (row.removeFromLeft (row.getWidth() / 2));

    auto bar = row.reduced (kSwitchGap, 0);

    duckMeter->setBounds (bar.getX(), knobTop + (kValueKnob - kDuckBarHeight) / 2,
                          bar.getWidth(), kDuckBarHeight);
    duckMeterCaption = { bar.getX(), captionTop, bar.getWidth(), kCaptionRow - 4 };
}

void DwellPanel::placeFxBand (juce::Rectangle<int> row)
{
    // **FX on the column's centre line**, in the band SEND and HOLD used to
    // take. It is the main delay's own gate, and it is what opens the reveal
    // the first time it is switched on, so it is lettered and sized like the
    // lane's gates rather than like the 56 px switch it was when it shared the
    // foot with the arrow -- which was the one control on the face hung off to
    // one side.
    fx.setBounds (row.withSizeKeepingCentre (kFxSwitchWidth, row.getHeight()));
}

void DwellPanel::placeFoot (juce::Rectangle<int> row)
{
    // The arrow, alone. It is not a control -- it moves no parameter, and it
    // is the same view affordance the host's own ui::ExpandButton is, which
    // sits at an edge everywhere in the suite.
    arrow.setBounds (row.removeFromRight (kArrowSide)
                        .withSizeKeepingCentre (kArrowSide, kArrowSide));

    // The state dot's place, to the left of the chevrons and on their centre
    // line. Worked out here because `paintPanel` has no other way to know
    // where the arrow ended up; whether it is *drawn* is decided there.
    stateDotSpot = juce::Rectangle<int> (kStateDot, kStateDot)
                       .withCentre ({ arrow.getX() - kStateDotGap, arrow.getBounds().getCentreY() });
}

//==============================================================================
void DwellPanel::resized()
{
    clearRules();

    duckMeterCaption = {};
    laneRegionBand = {};
    fxLinkTieBand = {};
    stateDotSpot = {};
    mixNoteBand = {};
    feedbackNoteBand = {};

    auto area = getLocalBounds().reduced (kPad, 4);
    const auto expanded = isShowingExpanded();

    // A little plate under the foot of every column. Without it the lowest
    // caption on the panel sits six pixels off the bottom edge and reads as
    // having fallen out of the window; the suite's own output section reserves
    // the same kind of margin (ui::ModulePanel::kFootMargin).
    area.removeFromBottom (kFootMargin);

    showRevealed (expanded);

    // A rule belongs to its column: a hairline drawn the full width of a
    // three-column panel runs each column's rule straight through the other
    // two columns' contents. See ui::ModulePanel::setColumnScopedRules.
    setColumnScopedRules (true);

    auto faceColumn = expanded ? area.removeFromLeft (kColumn) : area;

    int delayRuleTop = 0, toneRuleTop = 0;
    layOutFace (faceColumn, delayRuleTop, toneRuleTop);

    if (! expanded)
        return;

    area.removeFromLeft (kGutter);
    auto depthColumn = area.removeFromLeft (kColumn);
    area.removeFromLeft (kGutter);

    // The depth column is struck on the same line as the DELAY rule, so what
    // is bare above it is the CHARACTER band -- and that reads right: the trio
    // voices the whole delay, and what is under it is that delay's depth.
    layOutDepth (depthColumn.withTop (delayRuleTop), toneRuleTop);

    // The lane takes the band above the first rule as well, because it has
    // something to put there: its three gates. So it is handed both, and the
    // TONE line too -- it is cut on that line exactly as the depth column is.
    layOutLane (area.withTop (delayRuleTop), area.withBottom (delayRuleTop), toneRuleTop);
}

/** The face: nine controls, two ruled sections, a gate and a foot.

    CHARACTER over everything; DELAY carrying TIME, SYNC, FEEDBACK, MIX and
    STEREO; TONE carrying the two cuts; then FX and the foot, which take no
    rule of their own -- one accent-lit gate is not going to be mistaken for a
    tone control, and a third hairline is the one thing this composition could
    afford to lose. */
void DwellPanel::layOutFace (juce::Rectangle<int> column, int& delayRuleTop, int& toneRuleTop)
{
    // The foot first, off the bottom, so the arrow sits on the panel's own
    // foot line and every column ends on a common baseline whatever the air
    // above them works out to.
    Column face { column };
    auto foot = face.takeFoot (kFootRow);

    face.spend (kFaceRows, kFaceAir);

    face.air();
    placeSwitchRow (*character, face.take (kSwitchRow));

    // **CHARACTER before the rule, not inside the section.** CLEAN / TAPE /
    // BUCKET voices the whole delay -- the repeats, the loop, and whatever the
    // FX section is doing to them -- and under the DELAY rule it read as one
    // more row of that section, a sibling of TIME and FEEDBACK. Above the
    // first rule it is a sibling of nothing.
    //
    // Frosty, 2026-09-21: "so users know it affects the delay as a whole".
    face.breakAir();

    delayRuleTop = face.area.getY();
    addRule (face.take (kRule), "DELAY");

    face.air();
    placeHero (face.take (kHeroRow));
    face.air();
    placePair (feedback, mix, face.take (kValuePair), kPairKnob);

    // No air before the strips: they belong to the two knobs above them and
    // are drawn against their value lines.
    feedbackNoteBand = face.take (kStripRow);
    mixNoteBand = feedbackNoteBand.removeFromRight (feedbackNoteBand.getWidth() / 2);

    face.air();
    placeSwitchRow (*stereo, face.take (kSwitchRow));

    face.breakAir();
    toneRuleTop = face.area.getY();
    addRule (face.take (kRule), "TONE");
    face.air();
    placePair (lowCut, highCut, face.take (kPairRow), kValueKnob);

    face.breakAir();
    placeFxBand (face.take (kGateRow));
    face.air();

    placeFoot (foot);
}

/** The depth column: the main delay below the surface, in the two segments the
    face's rules cut it into.

    Laid out as one run its FX rule landed a dozen pixels off the face's TONE
    rule -- near enough to read as a failed alignment rather than as two
    sections, which is the one thing a multi-column panel must not do. Cutting
    the column on that line makes it exact by construction instead of by
    arithmetic that has to be redone every time a row's height changes. */
void DwellPanel::layOutDepth (juce::Rectangle<int> column, int toneRuleTop)
{
    Column top { column.withBottom (toneRuleTop) };
    top.spend (kDepthTopRows, kDepthTopAir);

    addRule (top.take (kRule), "LOOP");
    top.air();
    placeSingle (drive, top.take (kPairRow), kValueKnob);
    top.air();
    placePair (modRate, modDepth, top.take (kPairRow), kValueKnob);

    // **DUCK gets the break.** It is not part of the loop's colour: it is the
    // main delay's wet being pushed out of the way of the dry, applied after
    // the loop tap, never inside the feedback path and never touching the lane
    // at all (docs/delay/15). A double helping of air is the cheapest thing
    // that says so without spending a third hairline on it.
    top.breakAir();
    placeDuckBand (top.take (kPairRow));

    Column bottom { column.withTop (toneRuleTop) };

    // FX AMOUNT off the foot, so every column ends on one line.
    auto amountRow = bottom.takeFoot (kPairRow);

    bottom.spend (kDepthBotRows, kDepthBotAir);

    addRule (bottom.take (kRule), "FX");
    bottom.air();
    placeSwitchRow (*fxType, bottom.take (kFxRow));
    // No second helping: what is left of the segment *is* the gap above the
    // foot-anchored AMOUNT. See kDepthBotAir.

    if (fxAmount != nullptr)
        fxAmount->setBounds (amountRow);
}

/** The lane: the throw, composed as one, in the two segments the face's rules
    cut it into.

    Read top to bottom it says what a throw is -- what it is doing right now
    (the gates), then how long the tail is and how loud, then what is done to
    it. That order is the whole argument for the column: ten controls stacked
    in whatever order they came out of `params.h` would be a bin of leftovers.

    **It is a section now, not a second instrument.** Nine bands of mirrored
    voicing needed 400 px and its own rhythm, on interior rules kept clear of
    the line the other two columns share. Five bands sit on that line, so they
    are put on it. */
void DwellPanel::layOutLane (juce::Rectangle<int> column, juce::Rectangle<int> topBand,
                             int toneRuleTop)
{
    // The gates go in the band above the first rule, which is the band the
    // CHARACTER trio takes over the face -- the same argument, one column
    // over. See placeGates.
    placeGates (topBand.withSizeKeepingCentre (topBand.getWidth(), kGateRow));

    Column top { column.withBottom (toneRuleTop) };
    top.spend (kLaneTopRows, kLaneTopAir);

    addRule (top.take (kRule), "LANE");
    top.air();

    // **The tail alone on its row.** It is the lane's hero and it draws 116 px;
    // three across a 260 px column would cut its cell to 86. TIME and LEVEL go
    // under it as an ordinary pair, which is the truer reading anyway -- the
    // tail is what a hand reaches for, and those two are what it is set
    // against.
    placeSingle (laneGain, top.take (kHeroRow), kHeroKnob);

    // No air before the region legend: it belongs to the knob above it, the
    // same way the face's two numbered strips belong to theirs.
    laneRegionBand = top.take (kRegionRow)
                        .withSizeKeepingCentre (kRegionWidth, kRegionRow);

    top.air();
    placePair (laneTime, laneLevel, top.take (kValuePair), kPairKnob);
    top.air();   // the third helping, under the pair rather than against the rule

    Column bottom { column.withTop (toneRuleTop) };

    // AMOUNT off the foot, so every column ends on one line.
    auto amountRow = bottom.takeFoot (kPairRow);

    bottom.spend (kLaneBotRows, kLaneBotAir);

    // **The FX section, its gate and the link.** FX LINK is the only link on
    // the panel now; see refreshFxLinkFollowing for what the trio under it
    // looks like while it is on, and `fxLinkTieBand` for the bracket that ties
    // them to it and points at the column they are following.
    //
    // **TIED, not FOLLOWS MAIN.** The two switches take 156 px of a 260 px
    // column, so the rule has 104 left and a legend is drawn as a plate-filled
    // box of its text plus 14 px. "FX - FOLLOWS MAIN" measures 124 at 13 pt
    // and needs 138: it was rendered on AURORA, clipped its own F on the left
    // and butted against the gate on the right with no hairline showing at
    // all. "FX - TIED" measures 65, needs 79, and leaves 12 px of rule either
    // side -- and it is the word the tie actually is, with the bracket's spur
    // saying what it is tied to.
    const auto fxTop = bottom.area.getY();

    placeRuledSwitch (bottom.take (kRule),
                      context.params.getReal (Index::fxLink) > 0.5f ? "FX - TIED" : "FX",
                      fxLink, &laneFx);
    bottom.air();

    // Three across a row and AMOUNT under them, which is the depth column's FX
    // section exactly. See kFxRow for why both stages are one shape now.
    placeSwitchRow (*laneFxType, bottom.take (kFxRow));
    // No second helping: what is left of the segment *is* the gap above the
    // foot-anchored AMOUNT, as in the depth column.

    if (laneFxAmount != nullptr)
        laneFxAmount->setBounds (amountRow);

    fxLinkTieBand = column.withTop (fxTop).withBottom (amountRow.getBottom());
}

//==============================================================================
void DwellPanel::paintPanel (juce::Graphics& g)
{
    const auto t = panelTokens();
    const auto accent = context.def.accent;

    const auto small = ui::labelFont (kSmallSize, true);
    const auto caption = ui::captionFont (kCaption);
    const auto ink = ui::accentTextOn (accent, t.plate);

    /** §3's lit state: the accent at full strength with a glow, not the
        ordinary switch tint. The switch already fills in the accent; the glow
        is painted here, under it, because it has to spill past the button's own
        bounds and a SwitchButton draws only inside them.

        **Still a pending decision** -- it is left exactly as it was, and every
        switch that has arrived or left since is drawn by this same lambda
        rather than by a second copy of it. */
    const auto glow = [&] (const juce::Component& c, bool lit)
    {
        if (! lit || c.getBounds().isEmpty() || c.getParentComponent() != this)
            return;

        const juce::DropShadow bloom { accent.withAlpha (0.55f), 14, {} };
        bloom.drawForRectangle (g, c.getBounds());
    };

    glow (sendHeld, context.params.getReal (Index::send)   > 0.5f);
    glow (hold,     context.params.getReal (Index::hold)   > 0.5f);
    glow (chop,     context.params.getReal (Index::chop)   > 0.5f);
    glow (fx,       context.params.getReal (Index::fx)     > 0.5f);
    glow (laneFx,   context.params.getReal (Index::laneFx) > 0.5f);
    glow (fxLink,   context.params.getReal (Index::fxLink) > 0.5f);

    /** A short travel strip with one numbered mark on it: what a knob's own
        track cannot say, drawn under the knob it belongs to. */
    const auto strip = [&] (juce::Rectangle<int> band, float mark, float hotFrom,
                            const juce::String& text)
    {
        if (band.isEmpty())
            return;

        const auto w = juce::jmin (100, band.getWidth() - 6);
        auto line = juce::Rectangle<float> ((float) w, 3.0f)
                        .withCentre ({ (float) band.getCentreX(), (float) band.getY() + 5.0f });

        g.setColour (t.hairline);
        g.fillRect (line);

        // The stretch past the mark in the meter's hot colour, where it means
        // the same thing it does on a meter: past this, something else starts.
        if (hotFrom < 1.0f)
        {
            g.setColour (ui::tokens().meterClip);
            g.fillRect (line.withTrimmedLeft (line.getWidth() * hotFrom));
        }

        g.setColour (t.text1);
        g.fillRect (juce::Rectangle<float> (1.0f, 7.0f)
                        .withCentre ({ line.getX() + line.getWidth() * mark, line.getCentreY() + 1.0f }));

        ui::drawLabel (g, text, band.toFloat().withTrimmedTop (10.0f),
                       juce::Justification::centredTop, small, t.text1);
    };

    // FEEDBACK runs past unity: g = 1 at about 97 %, and the top of the travel
    // self-oscillates on purpose (docs/delay/10 §3). Colour and a tick, not a
    // word -- §4.
    strip (feedbackNoteBand, 0.97f, 0.97f, "97 = UNITY");

    // MIX is not a crossfade. The dry path holds bit-exact unity to 50 % and
    // only fades above it, so the knob would otherwise lie about its own bottom
    // half (docs/delay/10 §9).
    strip (mixNoteBand, 0.5f, 1.0f, "DRY TO 50");

    // The meter's name, on the line the row's captions sit on. It is beside the
    // knob it reads, so nothing else has to explain it.
    if (! duckMeterCaption.isEmpty())
        ui::drawLabel (g, "GR", duckMeterCaption.toFloat(),
                       juce::Justification::centred, caption, ink);

    /** **THROW / FREEZE / BUILD**: the lane gain knob's region, named under it.

        The knob is bipolar and its three regions are three different things
        happening to one loop gain rather than three amounts of one thing --
        below centre the caught word decays, at centre it holds at exact unity,
        above centre it builds. The minus and plus at the ends of the track say
        "less" and "more", which is the wrong sentence, and the value line says
        -40.0 % rather than what -40.0 % *does*.

        All three words are printed rather than only the live one, and that is
        the point: the two that are dim say that a knob resting in THROW has
        two other places to be, which a single caption could not have said. The
        live one is in the module's colour and the other two in the secondary
        ink; the catch mark on the knob's own track is drawn by `ui::Knob` and
        marks the middle word's position on the dial. */
    if (! laneRegionBand.isEmpty())
    {
        const auto region = laneRegion();
        auto band = laneRegionBand;
        const auto cell = band.getWidth() / 3;

        const char* words[] { "THROW", "FREEZE", "BUILD" };

        for (int i = 0; i < 3; ++i)
        {
            const auto box = (i == 2 ? band : band.removeFromLeft (cell));
            const auto lit = (i - 1) == region;

            ui::drawLabel (g, words[i], box.toFloat(), juce::Justification::centred,
                           small, lit ? ui::accentInk (accent, t.plate) : t.text2);
        }
    }

    /** **The link bracket.** While FX LINK is on, the three controls under its
        rule are following the main delay's -- see refreshFxLinkFollowing for
        the colour half of that, which is what stops them reading as dead. This
        is the other half: a hairline down the band's left edge with a spur
        running off into the gutter toward the column they are following, so
        the tie has a direction and is not only a word on a rule.

        It is still a lambda over a band rather than three inline calls: it was
        written for two brackets and one of them went with the voicing link on
        2026-09-22, and the next tie added to this panel should look exactly
        like this one rather than nearly like it. */
    const auto bracket = [&] (juce::Rectangle<int> band, bool following)
    {
        if (band.isEmpty() || ! following)
            return;

        const auto x = (float) band.getX() - 7.0f;

        g.setColour (ui::accentInk (accent, t.plate).withAlpha (0.45f));
        g.fillRect (juce::Rectangle<float> (x, (float) band.getY(),
                                            ui::Tokens::hairlineWeight,
                                            (float) band.getHeight()));

        // The spur, on the section rule's own line, pointing back at the main
        // delay's column.
        g.fillRect (juce::Rectangle<float> (x - (float) kGutter + 4.0f,
                                            (float) band.getY(),
                                            (float) kGutter - 4.0f,
                                            ui::Tokens::hairlineWeight));
    };

    bracket (fxLinkTieBand, context.params.getReal (Index::fxLink) > 0.5f);

    /** **The state dot**: the arrow says whether anything behind it has been
        moved.

        A closed reveal whose sixteen controls are all at their defaults and
        one whose DRIVE is at 80 % are the same picture, and that is the
        measured cost of hiding anything at all. A filled disc in the module's
        accent, beside the chevrons, is the cheapest thing that answers it: it
        is where the eye already is when it asks the question, it costs no row,
        and it is drawn rather than clicked, so it cannot be mistaken for a
        control.

        It is **not a parameter** -- it is a reading of sixteen that already
        exist, refreshed by the panel's own timer, so a lane, a preset and a
        knob all light it and none of them stores it. And it is drawn only
        while the panel is compact: with the columns open nothing is hidden,
        and a dot would then be saying something about controls the user can
        see. */
    if (! stateDotSpot.isEmpty() && ! isShowingExpanded() && revealedSectionIsMoved())
    {
        g.setColour (ui::accentInk (accent, t.plate));
        g.fillEllipse (stateDotSpot.toFloat());
    }
}

} // namespace bmo::dwell
