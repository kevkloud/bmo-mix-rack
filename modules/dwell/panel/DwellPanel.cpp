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

    `columns` is how many cells stand side by side before the next line starts:
    3 for the trios, 2 for an FX grid. A short last line is centred rather than
    left-hung, which a 2x2 grid never needs and the old seven-cell stack did.

    `namePrefix` renames the *components* without touching what is drawn on
    them, and the lane is what it is for. The lane's voicing repeats the main
    delay's words on purpose -- CLEAN, TAPE and BUCKET under a LANE rule read
    as a second delay, where LN-CLEAN would read as a bin of leftovers -- but
    `findNamed` in tests/ui/LayoutTests.cpp walks children by name, and two
    cells with one name resolve by child order, which changes every time the
    reveal opens.

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

        For the lane's two voicing rows, which step back toward the hairline
        while LINK is holding them to the main delay's -- see
        DwellPanel::refreshLinkFollowing. It is a *colour* change and
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
        out in whole columns. The face keeps 260; the main delay's depth takes
        another 260; the lane takes **400**, because it is a second delay and
        has to carry sixteen controls including two trios, a 2x2 grid and an
        `AMOUNT (SMEAR)` caption that measures 209 px at 15 pt.

        260 + 20 + 260 + 20 + 400 + 2 * kPad = 980, which is
        `ModuleDef::expandedWidth`. See Module.cpp for why not 1120. */
    constexpr int kColumn     = 260;
    constexpr int kLaneColumn = 400;
    constexpr int kGutter     = 20;

    /** The arithmetic, asserted rather than described. `resized` never names
        the lane's width -- it takes the two fixed columns off the left and
        gives the lane what is left -- so this is the only thing that would
        catch `expandedWidth` and these three drifting apart, and it catches it
        at compile time. */
    static_assert (2 * ui::ModulePanel::kPad + kColumn + kGutter + kColumn
                       + kGutter + kLaneColumn == 980,
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
        runs a single and a pair; the lane's widest knob row is four across a
        **400** px column, which is 100 px a cell where "LO CUT" measures about
        94. No word was shortened to buy the size back.

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
    constexpr int kStripRow  = 20;
    constexpr int kSwitchRow = 28;

    /** A band with a switch at the right-hand end of its rule. Tall enough for
        the switch rather than for the hairline. */
    constexpr int kRuledSwitchRow = 28;

    /** The lane's three gates, and the face's FX. The controls a hand reaches
        for while something is playing, so they are taller than a switch row
        and lettered a size up. */
    constexpr int kGateRow = 40;

    /** Under the lane's tail knob: THROW, FREEZE, BUILD. */
    constexpr int kRegionRow = 16;

    constexpr int kFootRow   = 28;
    constexpr int kArrowSide = 16;

    /** The face's FX gate: half the column, so it reads as a gate rather than
        as the one thing left on a row. */
    constexpr int kFxSwitchWidth = 126;

    /** Between the FX cells, and how tall a stack of three of them is. */
    constexpr int kFxCellGap = ui::Tokens::switchGap;                   // 8
    constexpr int kFxListRow = 3 * kSwitchHeight + 2 * kFxCellGap;      // 94

    //== Three FX types, and two different shapes for them =====================
    //
    // **Sweep is cut** (DECIDED, Frosty 2026-09-22; modules/dwell/params.h):
    // it was specified as sweeping VOICE's resonant centre, and VOICE was
    // deleted the day before. Three types do not tile as the 2x2 that four
    // did, and the two FX stages on this panel get two different answers,
    // because **the shape follows the slot each one has** -- the same rule the
    // rest of this layout runs on.
    //
    //   - **The depth column: three across, one row.** Its AMOUNT knob has to
    //     span the full 260 -- "AMOUNT (SMEAR)" measures 209 px at 15 pt and a
    //     260 px column has nowhere else to put it -- so the types are a band
    //     above it, and three across 260 is an 84 px cell. That is the cell the
    //     face's STEREO trio already uses, and what it buys is a band 28 px
    //     tall instead of the 2x2's 60, which goes back to the column's air.
    //
    //   - **The lane: three down, beside its AMOUNT.** The lane puts its knob
    //     *next to* its types, which is what lets one column carry the gates,
    //     the tail, the whole voicing and an FX stage. That leaves the types a
    //     tall narrow 172 px slot: three across it is a 52 px cell, and
    //     "DIFFUSE" measures about 46 before any padding -- the kind of fit
    //     that clips the first time somebody re-words a label. Three down gives
    //     each of them the whole 172 and costs no height at all: 94 of the
    //     row's 108.
    //
    // Neither is a 2x2 and neither leaves a short line to centre, which is what
    // four types needed and three do not.
    constexpr int kDepthFxRow        = kSwitchRow;   // 28
    constexpr int kLaneFxListWidth   = 172;
    constexpr int kLaneFxAmountWidth = 220;          // 172 + 8 + 220 = 400

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
        reads: **twenty-three**, which is everything that is not one of the ten
        the face draws.

        It read seven while the lane was undrawn, and the difference is the
        whole reason for extending it. CHOP, LINK and the lane's twelve were
        live, automatable and drawn nowhere at all; now they are behind the
        arrow, and a dot that did not read them would be saying "nothing behind
        here has moved" about a panel whose preset had set the lane to build. */
    std::vector<int> revealedIndices()
    {
        return { Index::drive, Index::modRate, Index::modDepth, Index::duck,
                 Index::fxType, Index::fxAmount,
                 Index::send, Index::laneGain, Index::hold, Index::chop,
                 Index::link, Index::laneLevel, Index::laneTime,
                 Index::laneCharacter, Index::laneStereo,
                 Index::laneLowCut, Index::laneHighCut,
                 Index::laneModRate, Index::laneModDepth,
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

    constexpr int kDepthBotRows = kRule + kDepthFxRow;

    // **Two even units, not a single and a double.** Three types across one
    // row spend 32 px less than the 2x2 did, and all 32 of them land in this
    // segment as air. Where they land is the only choice left: AMOUNT is
    // anchored to the foot so the columns end on one line, so whatever is not
    // spent above the cells falls into the gap below them. A single and a
    // double put 25 px under the rule and 51 above the knob, which reads as a
    // hole; two even units put 38 either side of the cells, which reads as a
    // band with air around it.
    constexpr int kDepthBotAir  = 2;   // 2 single, evenly

    /** The lane, under its own rule and in one run.

        The LANE rule, the tail band and its region legend, the VOICE rule with
        LINK on it, the two voicing trios, the four voicing knobs, the FX rule
        with the lane's own gate on it, and the FX band.

        **One run, not two segments.** The depth column is cut on the face's
        TONE line so that its FX rule is exact by construction; the lane is
        not, because there is nothing for it to be exact *to*. Its two interior
        rules land at 300 and 547 -- 160 px and 87 px clear of the 460 the
        other two columns share, which is far enough to read as its own rhythm
        rather than as an alignment that missed by a dozen pixels. The one line
        all three columns do share is the first, at 77. */
    constexpr int kLaneRows = kRule + kHeroRow + kRegionRow
                            + kRuledSwitchRow + kSwitchRow + kSwitchRow + kPairRow
                            + kRuledSwitchRow + kPairRow;
    constexpr int kLaneAir  = 9;    // 5 single + 2 double
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
      link     (context.params.param (Index::link),     "LINK", context.def.accent),
      // **ON, not FX.** It sits at the right-hand end of a rule whose legend
      // already says FX, and "FX ---- FX" is the one place on this panel the
      // mirror would have printed a word twice on one line. The face's own FX
      // gate stands in a band with no rule over it, so that one has to say FX;
      // this one does not. Its component name is LANE FX either way.
      laneFx   (context.params.param (Index::laneFx),   "ON",   context.def.accent),
      // **A second link, over the lane's FX rather than over its voicing.**
      // LINK ties the six voicing rows; this ties the FX trio. Two switches
      // because independent FX is what the lane's own FX stage is *for* -- a
      // thrown word crushed against a clean main delay -- and folding it into
      // LINK would have made that cost six parameters of divergence to buy one
      // (params.h, id 32; DECIDED, Frosty 2026-09-22). It is drawn exactly the
      // way LINK is drawn: on the rule of the section it governs, with the
      // same word on it, told apart by its component name and by which rule it
      // is standing on.
      fxLink   (context.params.param (Index::fxLink),   "LINK", context.def.accent),
      // The lane's tail. One bipolar knob: below centre the caught word
      // decays, at centre it holds at exact unity and above it builds -- three
      // regions of one loop gain rather than three modes, which is why the
      // parameter is a float and this is a knob. The catch and the region
      // legend are what make the middle one findable; see `setCatch` below and
      // `paintPanel`.
      laneGain     (context.params.param (Index::laneGain),     "TAIL",   ui::Knob::Style::character, kHeroFace, context.def.accent),
      laneTime     (context.params.param (Index::laneTime),     "TIME",   ui::Knob::Style::character, kPairFace, context.def.accent),
      laneLevel    (context.params.param (Index::laneLevel),    "LEVEL",  ui::Knob::Style::character, kPairFace, context.def.accent),
      laneLowCut   (context.params.param (Index::laneLowCut),   "LO CUT", ui::Knob::Style::character, kPairFace, context.def.accent),
      laneHighCut  (context.params.param (Index::laneHighCut),  "HI CUT", ui::Knob::Style::character, kPairFace, context.def.accent),
      laneModRate  (context.params.param (Index::laneModRate),  "RATE",   ui::Knob::Style::character, kPairFace, context.def.accent),
      laneModDepth (context.params.param (Index::laneModDepth), "DEPTH",  ui::Knob::Style::character, kPairFace, context.def.accent)
{
    character = std::make_unique<ChoiceRow> (context.params.param (Index::character),
                                             juce::StringArray { "CLEAN", "TAPE", "BUCKET" },
                                             ui::tokens().switchAlt, 13.0f, 3, kSwitchGap);

    stereo = std::make_unique<ChoiceRow> (context.params.param (Index::stereo),
                                          juce::StringArray { "STEREO", "PING-PONG", "DUAL" },
                                          ui::tokens().switchAlt, 12.0f, 3, kSwitchGap);

    // **Three across, on one line, with nothing left over to centre.** See
    // kDepthFxRow for why this stage takes a row where the lane's takes a
    // column, and params.h's kFxTypeNames for why there are three of them.
    fxType = std::make_unique<ChoiceRow> (context.params.param (Index::fxType),
                                          fxTypeLabels(),
                                          ui::tokens().switchAlt, 12.0f, 3, kFxCellGap);

    // The lane's three rows of cells: the same words off the same lists, with
    // prefixed component names. See ChoiceRow's class comment.
    laneCharacter = std::make_unique<ChoiceRow> (context.params.param (Index::laneCharacter),
                                                 juce::StringArray { "CLEAN", "TAPE", "BUCKET" },
                                                 ui::tokens().switchAlt, 13.0f, 3, kSwitchGap, "LANE");

    laneStereo = std::make_unique<ChoiceRow> (context.params.param (Index::laneStereo),
                                              juce::StringArray { "STEREO", "PING-PONG", "DUAL" },
                                              ui::tokens().switchAlt, 12.0f, 3, kSwitchGap, "LANE");

    // **One column, three down.** The lane's types stand beside its AMOUNT
    // knob rather than above it, so their slot is 172 px wide and the full
    // width of it goes to each cell. See kLaneFxListWidth.
    laneFxType = std::make_unique<ChoiceRow> (context.params.param (Index::laneFxType),
                                              fxTypeLabels(),
                                              ui::tokens().switchAlt, 12.0f, 1, kFxCellGap, "LANE");

    duckMeter = std::make_unique<DuckMeter> (context.gainReductionDb, context.def.accent);

    // A component name apiece for everything in the lane that repeats a word
    // the main delay already uses. The *drawn* caption is untouched, and that
    // is the whole point: the lane says TIME because it is a delay time, and
    // the test suite says LANE TIME because `findNamed` cannot resolve two of
    // anything.
    laneGain.setName     ("LANE TAIL");
    laneTime.setName     ("LANE TIME");
    laneLevel.setName    ("LANE LEVEL");
    laneLowCut.setName   ("LANE LO CUT");
    laneHighCut.setName  ("LANE HI CUT");
    laneModRate.setName  ("LANE RATE");
    laneModDepth.setName ("LANE DEPTH");
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
                     &laneTime, &laneLevel, &laneLowCut, &laneHighCut,
                     &laneModRate, &laneModDepth })
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

    // LINK and the lane's FX gate sit on rules rather than in bands of their
    // own, so they keep the suite's 26 px switch and are lettered to it.
    for (auto* s : { &link, &laneFx, &fxLink })
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
    lastLinkWasOn   = context.params.getReal (Index::link) > 0.5f;
    lastFxLinkWasOn = context.params.getReal (Index::fxLink) > 0.5f;
    lastMovedWasSet = revealedSectionIsMoved();
    lastLaneRegion  = laneRegion();

    buildFxAmount (false, juce::roundToInt (context.params.getReal (Index::fxType)));
    buildFxAmount (true,  juce::roundToInt (context.params.getReal (Index::laneFxType)));

    refreshLinkFollowing();

    // Every child's mouse-ups, so the FX switch's own click can be told apart
    // from the parameter arriving from a host, and LINK's own click from
    // everything that is not a click at all. See mouseUp.
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

void DwellPanel::seedLaneOnUnlink()
{
    // **Deliberately empty.** See the declaration in DwellPanel.h: what has to
    // happen here is settled -- the lane's six voicing parameters take the
    // main delay's current values, so unlinking changes nothing audible until
    // something is turned -- and *when* it may happen is what the spec pass is
    // defining. Writing six parameters from a guess would be exactly the side
    // effect params.h spends a paragraph forbidding.
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

    // The unlink gesture, and the only caller of the hook. A click is the one
    // event here that is unambiguously a user's; a preset arriving with LINK
    // off is not, and never reaches this.
    if (cameFrom (link) && context.params.getReal (Index::link) < 0.5f)
        seedLaneOnUnlink();
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
        &sendHeld, &hold, &chop, &laneGain, &laneTime, &laneLevel, &link,
        laneCharacter.get(), laneStereo.get(),
        &laneLowCut, &laneHighCut, &laneModRate, &laneModDepth,
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
    slot->setKnobSide (kPairKnob);
    slot->setCaptionSize (kCaption);
    held = type;

    if (lane)
        slot->setName ("LANE " + amountCaptionFor (type));

    if (wasPresent)
        addAndMakeVisible (*slot);

    refreshFxEnablement();

    // A rebuilt knob is a new object with the module accent on it, so the
    // FOLLOWS MAIN colour has to be put back or changing the lane's FX type
    // would quietly un-follow the knob beside the cells.
    refreshLinkFollowing();
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

void DwellPanel::refreshLinkFollowing()
{
    const auto t = panelTokens();

    // **Stepped back, not switched off.** The disabled alpha in this suite
    // means "this stage is not running", which is exactly what a followed
    // control is not -- it is running, on the main delay's numbers. So the
    // colour moves and nothing else does: the knobs keep their size, their
    // captions and their clicks, the rule above them says in words what has
    // happened to them, and paintPanel brackets them and points the bracket
    // back at the column they are following.
    const auto followed = context.def.accent.interpolatedWith (t.hairline, 0.55f);
    const auto own      = context.def.accent;

    // **Two links, two blocks, and the same treatment on each.** LINK governs
    // the six voicing rows and FX LINK governs the FX trio, and they are
    // separate because independent FX is the sound the lane's own FX stage
    // exists to make (params.h, id 32). Drawing both the same way is what
    // stops that split needing to be explained: whatever a rule says
    // FOLLOWS MAIN, the controls under it are in the quieter colour.
    const auto voicing = context.params.getReal (Index::link)   > 0.5f ? followed : own;
    const auto laneFxInk = context.params.getReal (Index::fxLink) > 0.5f ? followed : own;

    for (auto* k : { &laneLowCut, &laneHighCut, &laneModRate, &laneModDepth })
        k->setAccent (voicing);

    for (auto* row : { laneCharacter.get(), laneStereo.get() })
        if (row != nullptr)
            row->setRowTint (voicing == own ? ui::tokens().switchAlt : voicing);

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

    // Either link changes what its controls look like *and* what its rule
    // says, so it costs a re-layout rather than only a repaint: a rule's text
    // is recorded where the rule is laid out.
    const auto linkOn = context.params.getReal (Index::link) > 0.5f;
    const auto fxLinkOn = context.params.getReal (Index::fxLink) > 0.5f;

    if (linkOn != lastLinkWasOn || fxLinkOn != lastFxLinkWasOn)
    {
        lastLinkWasOn = linkOn;
        lastFxLinkWasOn = fxLinkOn;
        refreshLinkFollowing();
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

    // The state dot. Polled rather than listened for: it reads twenty-three
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

void DwellPanel::placeQuad (ui::PlainKnob& a, ui::PlainKnob& b, ui::PlainKnob& c,
                            ui::PlainKnob& d, juce::Rectangle<int> row, int knobSide)
{
    // Four across a 400 px column is a 100 px cell, where the longest caption
    // of the four -- "LO CUT" -- measures about 94 at 15 pt. Four across one of
    // the 260 px columns would be 65 and would have had to shorten a word,
    // which is the thing this whole redesign exists to stop doing.
    for (auto* k : { &a, &b, &c, &d })
    {
        k->setKnobSide (knobSide);
        k->setCaptionSize (kCaption);
    }

    const auto cell = row.getWidth() / 4;

    a.setBounds (row.removeFromLeft (cell));
    b.setBounds (row.removeFromLeft (cell));
    c.setBounds (row.removeFromLeft (cell));
    d.setBounds (row);
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
    // it spends 28 px of column saying one word. On the rule it reads as what
    // it is.
    //
    // **The link is always the last thing on a rule.** The lane's FX rule
    // carries two -- its own gate and FX LINK -- and the gate goes to the
    // *inside*, so that the rightmost switch on the VOICE rule and the
    // rightmost switch on the FX rule are the same kind of control and the
    // eye can read a column of them.
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

void DwellPanel::placeLaneTail (juce::Rectangle<int> row)
{
    // TIME, TAIL, LEVEL in thirds -- and the boxes are cut by hand so that all
    // three captions land on one line. See the declaration for why a shared
    // trio could not have done it.
    const auto cell = row.getWidth() / 3;
    const auto captionLine = row.getY() + kHeroKnob;   // where TAIL's name starts

    const auto box = [&] (juce::Rectangle<int> c, int side)
    {
        return juce::Rectangle<int> (c.getX(), captionLine - side,
                                     c.getWidth(), side + kCaptionRow + kValueRow);
    };

    auto left = row.removeFromLeft (cell);
    auto mid  = row.removeFromLeft (cell);

    laneTime.setBounds  (box (left, kPairKnob));
    laneGain.setBounds  (box (mid,  kHeroKnob));
    laneLevel.setBounds (box (row,  kPairKnob));
}

void DwellPanel::placeDuckBand (juce::Rectangle<int> row)
{
    duck.setKnobSide (kPairKnob);
    duck.setCaptionSize (kCaption);

    // Where a knob's face stops and its name starts, worked out the way
    // PlainKnob::resized does it, so the bar and its name land on the same two
    // lines the knob beside them uses.
    const auto knobTop    = row.getY() + (row.getHeight() - kCaptionRow - kPairKnob) / 2;
    const auto captionTop = knobTop + kPairKnob;

    duck.setBounds (row.removeFromLeft (row.getWidth() / 2));

    auto bar = row.reduced (kSwitchGap, 0);

    duckMeter->setBounds (bar.getX(), knobTop + (kPairKnob - kDuckBarHeight) / 2,
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
    linkTieBand = {};
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
    // something to put there: its three gates. So it is handed both.
    layOutLane (area.withTop (delayRuleTop), area.withBottom (delayRuleTop));
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
    placePair (lowCut, highCut, face.take (kPairRow), kPairKnob);

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
    placeSingle (drive, top.take (kPairRow), kPairKnob);
    top.air();
    placePair (modRate, modDepth, top.take (kPairRow), kPairKnob);

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
    placeSwitchRow (*fxType, bottom.take (kDepthFxRow));
    // No second helping: what is left of the segment *is* the gap above the
    // foot-anchored AMOUNT. See kDepthBotAir.

    if (fxAmount != nullptr)
        fxAmount->setBounds (amountRow);
}

/** The lane: a second delay, composed as one.

    Read top to bottom it says the same things the main delay says, in the same
    order and in the same words -- what it is doing right now, then how long
    the tail is and how loud, then what it sounds like, then what is done to
    it. That is the whole argument for the column: sixteen controls stacked in
    whatever order they came out of `params.h` would be a bin of leftovers, and
    the same sixteen in the shape of a delay are a delay. */
void DwellPanel::layOutLane (juce::Rectangle<int> column, juce::Rectangle<int> topBand)
{
    // The gates go in the band above the first rule, which is the band the
    // CHARACTER trio takes over the face -- the same argument, one column
    // over. See placeGates.
    placeGates (topBand.withSizeKeepingCentre (topBand.getWidth(), kGateRow));

    Column lane { column };
    lane.spend (kLaneRows, kLaneAir);

    addRule (lane.take (kRule), "LANE");
    lane.air();

    placeLaneTail (lane.take (kHeroRow));

    // No air before the region legend: it belongs to the knob above it, the
    // same way the face's two numbered strips belong to theirs. Centred on the
    // column and narrower than it, so it sits under TAIL rather than under all
    // three of them.
    laneRegionBand = lane.take (kRegionRow)
                         .withSizeKeepingCentre (kLaneFxAmountWidth, kRegionRow);

    lane.breakAir();

    // **The voicing, and LINK on its rule.** The six controls under this line
    // are exactly the ones LINK governs, so LINK is drawn on the line rather
    // than among them; see refreshLinkFollowing for what they look like while
    // it is on, and `linkTieBand` for the bracket that ties them to it.
    const auto voiceTop = lane.area.getY();

    placeRuledSwitch (lane.take (kRuledSwitchRow),
                      context.params.getReal (Index::link) > 0.5f ? "VOICE - FOLLOWS MAIN"
                                                                  : "VOICE",
                      link);

    lane.air();
    placeSwitchRow (*laneCharacter, lane.take (kSwitchRow));
    lane.air();
    placeSwitchRow (*laneStereo, lane.take (kSwitchRow));
    lane.air();
    placeQuad (laneLowCut, laneHighCut, laneModRate, laneModDepth,
               lane.take (kPairRow), kPairKnob);

    linkTieBand = column.withTop (voiceTop).withBottom (lane.area.getY());

    lane.breakAir();

    // **The FX section, its gate and its own link.** FX LINK ties this trio
    // and LINK above ties the voicing, and they are separate because
    // independent FX is the sound the lane's FX stage exists to make
    // (params.h, id 32). Drawn identically: the same word, on the rule of the
    // section it governs, with the same FOLLOWS MAIN legend and the same
    // bracket -- so the split needs no explaining, it just reads.
    //
    // The gate sits *inside* the link, so the rightmost switch on this rule
    // and the rightmost on the VOICE rule are the same kind of control.
    const auto fxTop = lane.area.getY();

    placeRuledSwitch (lane.take (kRuledSwitchRow),
                      context.params.getReal (Index::fxLink) > 0.5f ? "FX - FOLLOWS MAIN"
                                                                    : "FX",
                      fxLink, &laneFx);
    lane.air();

    // The lane's FX band: the three type cells **beside** its AMOUNT rather
    // than above it, stacked because their slot is tall and narrow. See
    // kLaneFxListWidth for why this column can afford an arrangement the
    // 260 px one cannot, and why the two stages' cells are different shapes.
    auto fxRow = lane.take (kPairRow);
    auto list = fxRow.removeFromLeft (kLaneFxListWidth);
    fxRow.removeFromLeft (fxRow.getWidth() - kLaneFxAmountWidth);

    // Centred on the row rather than on the knob's face beside it: the stack
    // is 94 px of a 108 px row, so there is nowhere else for it to go, and
    // centring it keeps the 7 px of plate above and below it equal.
    // Top-aligned with the row rather than centred in it: the knob beside it
    // draws its face in the row's top 86 px and hangs its caption under that,
    // so centring the 94 px stack in the full 108 put it eleven pixels below
    // the dial and the two read as separate bands. Sharing a top line puts
    // their centres 4 px apart and they read as one.
    laneFxType->setBounds (list.getX(), list.getY(), kLaneFxListWidth, kFxListRow);

    if (laneFxAmount != nullptr)
        laneFxAmount->setBounds (fxRow);

    fxLinkTieBand = column.withTop (fxTop).withBottom (lane.area.getY());
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

        **Still a pending decision** -- it is left exactly as it was, and the
        three switches that have arrived since are drawn by this same lambda
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
    glow (link,     context.params.getReal (Index::link)   > 0.5f);
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

    /** **The link brackets, one per link.** While a link is on, the controls
        under its rule are following the main delay's -- see
        refreshLinkFollowing for the colour half of that, which is what stops
        them reading as dead. This is the other half: a hairline down the
        band's left edge with a spur running off into the gutter toward the
        column they are following, so the tie has a direction and is not only a
        word on a rule.

        Two of them since 2026-09-22, drawn by one lambda rather than two
        copies: LINK brackets the six voicing rows and FX LINK brackets the FX
        trio. A reader who has worked out what one bracket means has worked out
        what the other one means, which is the whole reason the two links are
        drawn the same way. */
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

    bracket (linkTieBand,   context.params.getReal (Index::link)   > 0.5f);
    bracket (fxLinkTieBand, context.params.getReal (Index::fxLink) > 0.5f);

    /** **The state dot**: the arrow says whether anything behind it has been
        moved.

        A closed reveal whose twenty-three controls are all at their defaults and
        one whose DRIVE is at 80 % are the same picture, and that is the
        measured cost of hiding anything at all. A filled disc in the module's
        accent, beside the chevrons, is the cheapest thing that answers it: it
        is where the eye already is when it asks the question, it costs no row,
        and it is drawn rather than clicked, so it cannot be mistaken for a
        control.

        It is **not a parameter** -- it is a reading of twenty-three that already
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
