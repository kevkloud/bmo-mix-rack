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
    position in any of the four choices here, so clicking the lit one does
    nothing.

    `columns` is how many cells stand side by side before the next line starts:
    3 everywhere on this panel, which is `docs/delay/13` §1's row maximum
    honoured rather than waived. A short last line is centred rather than
    left-hung, so the seven FX types read as a block.

    `namePrefix` renames the *components* without touching what is drawn on
    them. THROW MODE's middle cell says THROW and so does the performance
    switch on the face, and `findNamed` in tests/ui/LayoutTests.cpp walks
    children by name: without the prefix a test asking for the button would
    get whichever of the two happened to be earlier in the child list, and
    that order changes every time the revealed section opens.

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

            // A short last line is centred: the FX grid is three, three and one,
            // and one cell hung on the left edge reads as a mistake.
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
    `width/2 - 8 - 19.5`, so the lane §2 draws computes a **negative** radius,
    and the narrowest box it draws anything readable in is about half the
    panel -- for a control whose default reduction is 4 dB. A bar is what the
    width affords. §7 puts "meter scale and rate" among the things that are
    free after ship, so this is a layout choice rather than a schema one.

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
    //== DUCK's home ===========================================================
    //
    // Where DUCK is drawn. **Behind the arrow, and that is now honest.**
    //
    // The objection to hiding it was that it was the one control here whose
    // default was not its inert end: 4 dB of reduction on a fresh instance
    // meant a hidden DUCK was doing something nobody had asked for and could
    // not see. **Its default is 0 dB from 2026-09-21** (DECIDED, Frosty;
    // params.h id 12, docs/delay/10 §6), so it ships inert like DRIVE, VOICE
    // and MOD DEPTH beside it, and hiding it costs a fresh instance nothing.
    //
    // Where it finally lives is still a listening question rather than a
    // layout one, so this enum and the one line under it stay the whole
    // decision. Nothing else in this file names a position for DUCK:
    // `showRevealed`, both layouts and the state dot's parameter list all read
    // `kDuckHome`. And the dot is the other half of the answer -- move DUCK
    // off 0 with the section closed and the arrow says so.

    enum class DuckHome
    {
        revealed,   ///< behind the arrow, where it sits today
        face,       ///< promoted into the tone band, beside LO CUT and HI CUT
        none        ///< off the panel entirely; the parameter stays live
    };

    constexpr DuckHome kDuckHome = DuckHome::revealed;   // <-- the one line

    //==========================================================================
    constexpr int kRule = ui::ModulePanel::kRuleRow;   // 16

    /** A column's content width, and the gutter between two of them. 280 less
        `kPad` each side is 260; 560 is two of those and a 20 px gutter, which
        is what `ModuleDef::expandedWidth` is set to. */
    constexpr int kColumn = 260;
    constexpr int kGutter = 20;

    constexpr int kSwitchHeight = ui::Tokens::switchHeight;   // 26
    constexpr int kSwitchWidth  = ui::Tokens::switchWidth;    // 70
    constexpr int kSwitchGap    = ui::Tokens::switchGap;      // 8

    /** **15 pt -- the suite standard, and what the eleven-control face buys.**

        `ui::PlainKnob`'s own default is 15 (core/ui/Controls.h), and a module
        goes below it only when it is cramped: BMO DEQ's squeezed shape dial
        runs at 11. Every earlier attempt at this panel was cramped, and the
        two before this one dropped to 12 to fit a three-knob primary row into
        a 260 px column -- 86 px a cell, where "FEEDBACK" needs 125 at 15.

        Eleven controls fix that rather than argue with it. There is no primary
        trio any more: TIME has a band of its own and everything else is laid
        out in **pairs**, so a caption gets 130 px instead of 86. "FEEDBACK"
        measures about 125 px at 15 pt and "AMOUNT (SMEAR)" about 209 in a
        column of 260 -- so no word had to be shortened to buy the size back.

        MOD RATE and MOD DEPTH are still captioned RATE and DEPTH, and LOW and
        HIGH CUT LO CUT and HI CUT, but that is now a reading choice rather
        than a fit: a panel's words are not its schema (WORKFLOWS.md's control
        audit), and the short forms are what a pair of filters reads best at.

        **Switch labels are not captions** and keep their own sizes, which the
        suite sets to the cell rather than to a standard: 13 pt for CHARACTER's
        three wide cells, 12 for STEREO's, THROW MODE's and the FX grid's, 14
        on THROW and FREEZE, 11 on SYNC and FX. Section legends stay at
        `ui::ModulePanel::kLegendSize`. */
    constexpr float kCaption = 15.0f;

    /** The small print: the two numbered strips under FEEDBACK and MIX. A step
        under the 13 pt section legends. */
    constexpr float kSmallSize = 9.5f;

    /** How much height a PlainKnob spends under its knob, at `kCaption`. The
        same arithmetic `PlainKnob::captionRow` does -- round(points * 1.2) + 4,
        plus round(11 * 1.2) + 1 for a knob that prints its value -- written out
        here because a row's height has to be known before a knob is in it.

        22 rather than 18 since the captions went to 15 pt, which is four
        pixels on every knob row on the panel and is where `kRevealKnob`'s
        four went. */
    constexpr int kCaptionRow = 22;
    constexpr int kValueRow   = 14;

    //== Knob sides. Three, and the hierarchy is the point. ====================
    //
    // TIME is the hero and the other four on the face are its equals in pairs;
    // the revealed section runs a step smaller because it is secondary by
    // definition. BMO Saturator's proportions -- a ~120 px DRIVE over a ~90 px
    // TONE and MIX -- are what this is set against, and they are the reason
    // this panel reads calm where three attempts at 78 and 64 across eighteen
    // controls did not.

    // The revealed section pays for the 15 pt captions out of its knobs rather
    // than out of its air: its three knob rows are the only band on the panel
    // that is boxed in on both sides, between the face's DELAY rule above and
    // its TONE rule below, so four more pixels of caption on each of them came
    // straight off the air between them. 66 rather than 70 hands that back.
    // The face's two sizes are untouched.
    constexpr int kHeroKnob   = 116;
    constexpr int kPairKnob   = 86;
    constexpr int kRevealKnob = 66;

    constexpr float kHeroFace   = 0.60f;
    constexpr float kPairFace   = 0.58f;
    constexpr float kRevealFace = 0.54f;

    //== Row heights ===========================================================
    constexpr int kHeroRow    = kHeroKnob   + kCaptionRow + kValueRow;   // 152
    constexpr int kValuePair  = kPairKnob   + kCaptionRow + kValueRow;   // 122
    constexpr int kPairRow    = kPairKnob   + kCaptionRow;               // 108
    constexpr int kRevealRow  = kRevealKnob + kCaptionRow;               //  88
    constexpr int kStripRow   = 20;
    constexpr int kSwitchRow  = 28;
    constexpr int kPerformRow = 40;
    constexpr int kFootRow    = 28;

    constexpr int kArrowSide = 16;

    /** The FX switch at the foot. Narrower than the suite's 70 because it is
        two letters and it shares the foot with the arrow. */
    constexpr int kFxSwitchWidth = 56;

    /** Down or across the FX cells. Tighter than the suite's 8 because seven
        cells is more than a row was built for. */
    constexpr int kFxCellGap = 6;
    constexpr int kFxGridRow = 3 * kSwitchHeight + 2 * kFxCellGap;   // 90

    /** The DUCK bar, on the knob's own centre line. */
    constexpr int kDuckBarHeight = 18;

    /** The arrow's state dot: a filled disc beside the chevrons, drawn only
        while the panel is compact and something behind them has been moved.
        Six pixels, the smallest mark that survives a rack screenshot and still
        smaller than anything on the panel that can be clicked. */
    constexpr int kStateDot    = 6;
    constexpr int kStateDotGap = 8;

    /** What the percent moves, per FX type, `docs/delay/13` §6a. Indexed by
        `kFxTypeNames`, so the two lists move together -- and `fxType` is the
        one list in this schema that is still free to change before ship. */
    juce::String amountCaptionFor (int type)
    {
        switch (type)
        {
            case 0:  return "AMOUNT (SMEAR)";    // Diffuse
            case 1:  return "AMOUNT (SWEEP)";    // Sweep
            case 2:  return "AMOUNT (DEPTH)";    // Pan/Tremolo
            case 3:
            case 4:  return "AMOUNT (BLEND)";    // either octave
            case 5:  return "AMOUNT (SEAM)";     // Reverse
            case 6:  return "AMOUNT (BITS)";     // Crush
            default: return "AMOUNT";
        }
    }

    /** Short forms for the seven cells. A panel's button labels are not its
        schema (WORKFLOWS.md's control audit), so these can be re-worded
        without touching `kFxTypeNames`. */
    juce::StringArray fxTypeLabels()
    {
        return { "DIFFUSE", "SWEEP", "PAN", "OCT UP", "OCT DN", "REVERSE", "CRUSH" };
    }

    /** Every parameter the revealed section carries, which is exactly what the
        state dot reads. DUCK is in the list only while it is hidden -- a
        control the face already shows cannot be something the dot is warning
        about. */
    std::vector<int> revealedIndices()
    {
        std::vector<int> out { Index::voice, Index::drive, Index::modRate, Index::modDepth,
                               Index::throwMode, Index::fxType, Index::fxAmount };

        if constexpr (kDuckHome == DuckHome::revealed)
            out.push_back (Index::duck);

        return out;
    }

    //== The air between the rows ==============================================
    //
    /** Spends whatever a column has left over its rows as air above each of
        them, with a double helping wherever a section changes.

        Pooled into three bands the panel read as islands with nothing between
        them; laid on a unit at a time it breathes and the one band that is
        meant to say "new section" is the only large one. The odd pixels go to
        the *first* section break rather than to the foot, because the foot is
        the band nobody is looking at.

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
        pair, the strips, STEREO, the TONE rule, the tone pair and the
        performance pair. */
    constexpr int kFaceRows = kSwitchRow + kRule + kHeroRow + kValuePair + kStripRow
                            + kSwitchRow + kRule + kPairRow + kPerformRow;
    constexpr int kFaceAir  = 12;   // 6 single + 3 double

    /** The revealed column, in the two segments the face's rules cut it into.

        Above the TONE line: the LOOP rule, two or three knob rows depending on
        where DUCK lives, the THROW rule and the mode trio. Promoting DUCK
        takes its row and its helping of air out of the column rather than
        leaving a hole where it was, which is the other half of `kDuckHome`
        being one line.

        Below it: the FX rule and the type grid, with FX AMOUNT anchored to the
        foot so the two columns end on one line. */
    constexpr int kRevealDuck = (kDuckHome == DuckHome::revealed) ? kRevealRow : 0;

    constexpr int kRevealTopRows = kRule + kRevealRow * 2 + kRevealDuck + kRule + kSwitchRow;
    constexpr int kRevealTopAir  = (kDuckHome == DuckHome::revealed) ? 8 : 7;   // singles + 2 double

    constexpr int kRevealBotRows = kRule + kFxGridRow;
    constexpr int kRevealBotAir  = 3;   // 1 single + 1 double
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
      sync      (context.params.param (Index::sync),      "SYNC",   ui::tokens().switchAlt),
      throwHeld (context.params.param (Index::throwHeld), "THROW",  context.def.accent),
      freeze    (context.params.param (Index::freeze),    "FREEZE", context.def.accent),
      fx        (context.params.param (Index::fx),        "FX",     context.def.accent),
      arrow ([this] { return isShowingExpanded(); }),
      // VOICE is an ordinary knob rather than a ConcentricBand ring around
      // HI CUT. The ring was the panel's third control idiom, it needed two
      // panel-drawn names where every other knob names itself, and it put the
      // visually heaviest object on the panel on a secondary control.
      voice    (context.params.param (Index::voice),    "VOICE",  ui::Knob::Style::character, kRevealFace, context.def.accent),
      drive    (context.params.param (Index::drive),    "DRIVE",  ui::Knob::Style::character, kRevealFace, context.def.accent),
      modRate  (context.params.param (Index::modRate),  "RATE",   ui::Knob::Style::character, kRevealFace, context.def.accent),
      modDepth (context.params.param (Index::modDepth), "DEPTH",  ui::Knob::Style::character, kRevealFace, context.def.accent),
      duck     (context.params.param (Index::duck),     "DUCK",   ui::Knob::Style::character, kRevealFace, context.def.accent)
{
    character = std::make_unique<ChoiceRow> (context.params.param (Index::character),
                                             juce::StringArray { "CLEAN", "TAPE", "BUCKET" },
                                             ui::tokens().switchAlt, 13.0f, 3, kSwitchGap);

    stereo = std::make_unique<ChoiceRow> (context.params.param (Index::stereo),
                                          juce::StringArray { "STEREO", "PING-PONG", "DUAL" },
                                          ui::tokens().switchAlt, 12.0f, 3, kSwitchGap);

    // SEND / THROW / BUILD, prefixed so the middle cell and the performance
    // switch on the face do not answer to the same name. See ChoiceRow.
    throwMode = std::make_unique<ChoiceRow> (context.params.param (Index::throwMode),
                                             juce::StringArray { "SEND", "THROW", "BUILD" },
                                             ui::tokens().switchAlt, 12.0f, 3, kSwitchGap,
                                             "mode");

    fxType = std::make_unique<ChoiceRow> (context.params.param (Index::fxType),
                                          fxTypeLabels(),
                                          ui::tokens().switchAlt, 12.0f, 3, kFxCellGap);

    duckMeter = std::make_unique<DuckMeter> (context.gainReductionDb, context.def.accent);

    for (auto* c : std::initializer_list<juce::Component*> {
             &feedback, &mix, &lowCut, &highCut, &sync, &throwHeld, &freeze, &fx, &arrow,
             character.get(), stereo.get() })
        addAndMakeVisible (c);

    // DUCK promoted onto the face belongs to the face, so it is added once
    // here and never taken away with the revealed section. See kDuckHome.
    if constexpr (kDuckHome == DuckHome::face)
    {
        addAndMakeVisible (duck);
        addAndMakeVisible (*duckMeter);
    }

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

    for (auto* k : { &lowCut, &highCut })
    {
        k->setKnobSide (kPairKnob);
        k->setCaptionSize (kCaption);
    }

    for (auto* k : { &voice, &drive, &modRate, &modDepth, &duck })
    {
        k->setKnobSide (kRevealKnob);
        k->setCaptionSize (kCaption);
    }

    // SYNC ships disabled: its slot and NOTE's order are permanent from this
    // release, but no host tempo reaches a ModuleDsp until docs/delay/12's
    // plumbing lands, and that is its own workflow.
    sync.setSwitchEnabled (dwell::kSyncIsEnabled);
    sync.setLabelSize (11.0f);

    // THROW and FREEZE are lit from across the room, so their labels are set
    // to the row rather than to the suite's 26 px switch -- and they light in
    // the module's accent rather than switchAlt, with a glow painted behind
    // them by paintPanel, so a held performance button cannot read as one more
    // CHARACTER or STEREO selection (§3).
    for (auto* s : { &throwHeld, &freeze })
        s->setLabelSize (14.0f);

    fx.setLabelSize (11.0f);

    arrow.onClick = [this]
    {
        requestExpanded (! isShowingExpanded());
        arrow.refresh();
    };

    lastSyncWasOn = context.params.getReal (Index::sync) > 0.5f;
    showNote (lastSyncWasOn);

    lastFxWasOn = context.params.getReal (Index::fx) > 0.5f;
    lastMovedWasSet = revealedSectionIsMoved();
    buildFxAmount (juce::roundToInt (context.params.getReal (Index::fxType)));

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
    // Clicking `fx` on while compact opens the section once, as a convenience
    // (docs/delay/13 §6a). It is done from the *click* and not from a parameter
    // callback on purpose: automation, preset load and session recall all move
    // the same parameter, and none of them may resize the module. Turning `fx`
    // off never closes it, and the arrow closes it without touching it.
    if (e.eventComponent != nullptr
        && (e.eventComponent == &fx || e.eventComponent->findParentComponentOfClass<ui::SwitchButton>() == &fx)
        && ! isShowingExpanded()
        && context.params.getReal (Index::fx) > 0.5f)
    {
        requestExpanded (true);
    }
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

    std::vector<juce::Component*> group { &voice, &drive, &modRate, &modDepth,
                                          throwMode.get(), fxType.get() };

    if constexpr (kDuckHome == DuckHome::revealed)
    {
        group.push_back (&duck);
        group.push_back (duckMeter.get());
    }

    if (fxAmount != nullptr)
        group.push_back (fxAmount.get());

    for (auto* c : group)
    {
        if (shown)
            addAndMakeVisible (c);
        else
            removeChildComponent (c);
    }
}

void DwellPanel::buildFxAmount (int type)
{
    if (fxAmount != nullptr && fxAmountType == type)
        return;

    const auto wasPresent = fxAmount != nullptr && fxAmount->getParentComponent() == this;

    fxAmount = std::make_unique<ui::PlainKnob> (context.params.param (Index::fxAmount),
                                                amountCaptionFor (type),
                                                ui::Knob::Style::character,
                                                kRevealFace, context.def.accent);
    fxAmount->setKnobSide (kRevealKnob);
    fxAmount->setCaptionSize (kCaption);
    fxAmountType = type;

    if (wasPresent)
        addAndMakeVisible (*fxAmount);

    refreshFxEnablement();
}

void DwellPanel::refreshFxEnablement()
{
    // With FX off the two FX controls grey rather than vanishing, so the width
    // never changes underneath a user (§6a). The DSP skips the stage regardless
    // -- and that is a property of `fx`, not of the section being open.
    const auto on = context.params.getReal (Index::fx) > 0.5f;

    fxType->setRowEnabled (on);

    if (fxAmount != nullptr)
        fxAmount->setKnobEnabled (on);
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

    if (fxOn != lastFxWasOn)
    {
        lastFxWasOn = fxOn;
        refreshFxEnablement();
        repaint();                 // the foot's glow follows FX as well as THROW
    }

    // The state dot. Polled with everything else rather than listened for: it
    // reads eight parameters, any of them can be moved by a knob, a lane or a
    // preset, and all three have to light it.
    const auto moved = revealedSectionIsMoved();

    if (moved != lastMovedWasSet)
    {
        lastMovedWasSet = moved;
        repaint();
    }

    // The caption names what the percent moves, so it follows the type. Only
    // the caption: nothing here asks the host for a width.
    buildFxAmount (juce::roundToInt (context.params.getReal (Index::fxType)));

    if (fxAmount != nullptr && fxAmount->getParentComponent() == this && fxAmount->getBounds().isEmpty())
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
    // one is left bare, and bare plate is what the symmetry costs. The
    // alternative, a row of its own for a 26 px switch, cost 28 px of height
    // and a whole band -- and bands are what this panel has too many of.
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

void DwellPanel::placePerform (juce::Rectangle<int> row)
{
    const auto half = (row.getWidth() - kSwitchGap) / 2;

    throwHeld.setBounds (row.removeFromLeft (half));
    row.removeFromLeft (kSwitchGap);
    freeze.setBounds (row);
}

void DwellPanel::placeSwitchRow (ChoiceRow& switches, juce::Rectangle<int> row)
{
    switches.setBounds (row.withSizeKeepingCentre (row.getWidth(), kSwitchHeight));
}

void DwellPanel::placeDuckBand (juce::Rectangle<int> row)
{
    duck.setKnobSide (kRevealKnob);
    duck.setCaptionSize (kCaption);

    // Where a knob's face stops and its name starts, worked out the way
    // PlainKnob::resized does it, so the bar and its name land on the same two
    // lines the knob beside them uses.
    const auto knobTop    = row.getY() + (row.getHeight() - kCaptionRow - kRevealKnob) / 2;
    const auto captionTop = knobTop + kRevealKnob;

    duck.setBounds (row.removeFromLeft (row.getWidth() / 2));

    auto bar = row.reduced (kSwitchGap, 0);

    duckMeter->setBounds (bar.getX(), knobTop + (kRevealKnob - kDuckBarHeight) / 2,
                          bar.getWidth(), kDuckBarHeight);
    duckMeterCaption = { bar.getX(), captionTop, bar.getWidth(), kCaptionRow - 4 };
}

void DwellPanel::placeFoot (juce::Rectangle<int> row)
{
    // **FX on the column's centre line, the arrow still at the edge.**
    //
    // FX is a control -- the one switch at the foot -- and it is centred on the
    // whole row, measured before the arrow's cell is taken off, so it lands on
    // the column's centre and not on the centre of what the arrow left over.
    //
    // The arrow is not a control: it moves no parameter and it is the same
    // view affordance the host's own ui::ExpandButton is, which sits at an
    // edge everywhere in the suite. Centring the two as a pair was the
    // alternative and it fails the instruction it was meant to serve -- a
    // 56 px switch and a 16 px arrow centred together put FX 12 px left of
    // centre, which is the off-to-one-side look this removes.
    fx.setBounds (row.withSizeKeepingCentre (kFxSwitchWidth, kSwitchHeight));

    arrow.setBounds (row.removeFromRight (kArrowSide)
                        .withSizeKeepingCentre (kArrowSide, kArrowSide));

    // The state dot's place, to the left of the chevrons and on their centre
    // line. Worked out here because `paintPanel` has no other way to know
    // where the arrow ended up; whether it is *drawn* is decided there.
    stateDotSpot = juce::Rectangle<int> (kStateDot, kStateDot)
                       .withCentre ({ arrow.getX() - kStateDotGap, arrow.getBounds().getCentreY() });
}

void DwellPanel::placeFxGrid (juce::Rectangle<int> row)
{
    // Three across, which is docs/delay/13 §1's row maximum honoured rather
    // than waived: a 260 px column cuts three 82 px cells and "REVERSE" fits
    // one at 12 pt. The seven land three, three and one, the short line
    // centred.
    fxType->setBounds (row);
}

//==============================================================================
void DwellPanel::resized()
{
    clearRules();

    duckMeterCaption = {};
    stateDotSpot = {};
    mixNoteBand = {};
    feedbackNoteBand = {};

    auto area = getLocalBounds().reduced (kPad, 4);
    const auto expanded = isShowingExpanded();

    // A little plate under the foot of both columns. Without it the FX
    // AMOUNT knob's caption -- the lowest text on the panel -- sits six
    // pixels off the bottom edge and reads as having fallen out of the
    // window; the suite's own output section reserves the same kind of margin
    // (ui::ModulePanel::kFootMargin).
    area.removeFromBottom (kFootMargin);

    showRevealed (expanded);

    // A rule belongs to its column: a hairline drawn the full width of a
    // two-column panel runs each column's rule straight through the other
    // column's contents. See ui::ModulePanel::setColumnScopedRules.
    setColumnScopedRules (true);

    auto faceColumn = expanded ? area.removeFromLeft (kColumn) : area;

    int delayRuleTop = 0, toneRuleTop = 0;
    layOutFace (faceColumn, delayRuleTop, toneRuleTop);

    if (! expanded)
        return;

    area.removeFromLeft (kGutter);

    // Struck on the same line as the DELAY rule, so what is bare above the
    // second column is the CHARACTER band -- and that reads right: the trio
    // sits over both columns rather than over one of them.
    layOutRevealed (area.withTop (delayRuleTop), toneRuleTop);
}

/** The face: eleven controls, two ruled sections and a foot.

    CHARACTER over everything; DELAY carrying TIME, SYNC, FEEDBACK, MIX and
    STEREO; TONE carrying the two cuts; then the performance pair and the foot,
    which take no rule of their own -- two 40 px buttons lit in the accent are
    not going to be mistaken for tone controls, and a third hairline was the
    one thing this composition could afford to lose. */
void DwellPanel::layOutFace (juce::Rectangle<int> column, int& delayRuleTop, int& toneRuleTop)
{
    // The foot first, off the bottom, so FX and the arrow sit on the panel's
    // own foot line and the two columns end on a common baseline whatever the
    // air above them works out to.
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

    // DUCK promoted onto the face makes this a trio rather than a pair; that
    // is the whole cost of the move. See kDuckHome.
    if constexpr (kDuckHome == DuckHome::face)
    {
        auto row = face.take (kPairRow);
        const auto third = row.getWidth() / 3;

        lowCut.setKnobSide (kPairKnob);
        highCut.setKnobSide (kPairKnob);
        lowCut.setBounds (row.removeFromLeft (third));
        highCut.setBounds (row.removeFromLeft (third));
        placeDuckBand (row);
    }
    else
    {
        placePair (lowCut, highCut, face.take (kPairRow), kPairKnob);
    }

    face.breakAir();
    placePerform (face.take (kPerformRow));
    face.air();

    placeFoot (foot);
}

/** The revealed column: LOOP, THROW and FX, read top to bottom in the order
    the face reads in.

    Every knob here is a step smaller than the face's, which is the hierarchy
    saying what the column is for: nothing in it is reached for first. */
void DwellPanel::layOutRevealed (juce::Rectangle<int> column, int toneRuleTop)
{
    // **Two segments, cut on the line the face's TONE rule is struck on.**
    //
    // Laid out as one run of rows the column's second rule landed eleven
    // pixels off the face's second rule -- near enough to read as a failed
    // alignment rather than as two sections, which is the one thing a
    // two-column panel must not do. Cutting the column at that line makes it
    // exact by construction instead of by arithmetic that has to be redone
    // every time a row's height changes.
    Column top { column.withBottom (toneRuleTop) };
    top.spend (kRevealTopRows, kRevealTopAir);

    addRule (top.take (kRule), "LOOP");
    top.air();
    placePair (voice, drive, top.take (kRevealRow), kRevealKnob);
    top.air();
    placePair (modRate, modDepth, top.take (kRevealRow), kRevealKnob);

    // === DUCK: still under investigation. See kDuckHome. =====================
    if constexpr (kDuckHome == DuckHome::revealed) { top.air(); placeDuckBand (top.take (kRevealRow)); }
    // =========================================================================

    top.breakAir();
    addRule (top.take (kRule), "THROW");
    top.air();
    placeSwitchRow (*throwMode, top.take (kSwitchRow));
    top.breakAir();

    Column bottom { column.withTop (toneRuleTop) };

    // FX AMOUNT off the foot, so both columns end on one line.
    auto amountRow = bottom.takeFoot (kRevealRow);

    bottom.spend (kRevealBotRows, kRevealBotAir);

    addRule (bottom.take (kRule), "FX");
    bottom.air();
    placeFxGrid (bottom.take (kFxGridRow));
    bottom.breakAir();

    if (fxAmount != nullptr)
        fxAmount->setBounds (amountRow);
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

        **Still a pending decision** -- it is left exactly as it was. */
    const auto glow = [&] (const juce::Component& c, bool lit)
    {
        if (! lit || c.getBounds().isEmpty() || c.getParentComponent() != this)
            return;

        const juce::DropShadow bloom { accent.withAlpha (0.55f), 14, {} };
        bloom.drawForRectangle (g, c.getBounds());
    };

    glow (throwHeld, context.params.getReal (Index::throwHeld) > 0.5f);
    glow (freeze,    context.params.getReal (Index::freeze)    > 0.5f);
    glow (fx,        context.params.getReal (Index::fx)        > 0.5f);

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

    // **The mono-sum line is gone.** PING-PONG summed its input to mono before
    // this change and still does (docs/delay/10 §8): the behaviour did not
    // move, the sentence did. Frosty struck it 2026-09-21.

    /** **The state dot**: the arrow says whether anything behind it has been
        moved.

        A closed section whose eight controls are all at their defaults and one
        whose DRIVE is at 80 % are the same picture, and that is the measured
        cost of hiding anything at all. A filled disc in the module's accent,
        beside the chevrons, is the cheapest thing that answers it: it is where
        the eye already is when it asks the question, it costs no row, and it
        is drawn rather than clicked, so it cannot be mistaken for a control.

        It is **not a parameter** -- it is a reading of eight that already
        exist, refreshed by the panel's own timer, so a lane, a preset and a
        knob all light it and none of them stores it. And it is drawn only
        while the panel is compact: with the column open nothing is hidden, and
        a dot would then be saying something about controls the user can see. */
    if (! stateDotSpot.isEmpty() && ! isShowingExpanded() && revealedSectionIsMoved())
    {
        g.setColour (ui::accentInk (accent, t.plate));
        g.fillEllipse (stateDotSpot.toFloat());
    }
}

} // namespace bmo::dwell
