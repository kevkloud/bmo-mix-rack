#include "VcompPanel.h"
#include "modules/vcomp/params.h"

namespace bmo::vcomp
{

namespace
{
    // The knob draws at kKnobSide; the control is laid out kKnobWidth wide so
    // the caption underneath has room -- see PlainKnob::setKnobSide for the
    // BMO Opto case that forced the distinction.
    constexpr int kKnobSide   = 92;
    constexpr int kKnobWidth  = 136;
    // 133, not the 150 it was until 2026-09-15. The panel ran out of room --
    // see the note in resized() -- and this is where the 34 px came from: the
    // caption needs 19 of the 41 px under a 92 px knob face, so the two
    // headline knobs give up air rather than size.
    constexpr int kKnobHeight = 133;

    constexpr int kSwitchWidth  = ui::Tokens::switchWidth;
    constexpr int kSwitchHeight = ui::Tokens::switchHeight;
    constexpr int kSwitchGap    = ui::Tokens::switchGap;

    constexpr int kBarRow   = 22 + LevelBar::kScaleRow;   ///< the bar, its air, and its printed scale
    constexpr int kBarGap   = 2;

    // The IN bar is kTagRow taller than the other two, because it is the one
    // carrying the gate and its flag needs somewhere to stand. It absorbs the
    // difference inside itself -- see LevelBar::wellBounds -- so all three
    // wells stay evenly spaced and the block still reads as one instrument.
    constexpr int kMeterBlock = kBarRow * 3 + kBarGap * 2 + LevelBar::kTagRow;

    // The five detector knobs are trim knobs -- ui::ModulePanel::styleTrimKnob
    // sizes and captions them -- in two rows of three and two.
    constexpr int kDetectorRow = ui::ModulePanel::kTrimKnobRow;
    /** How far below a trim knob's own top edge its first track dot falls.

        Derived, then checked on a render: the component is kTrimKnobRow tall,
        the caption row comes off the bottom, and a gainKnobSide square centres
        in what is left -- so the face centre sits about 28 px down, the radius
        is 14 at the default face scale, and the dotted track stands trackGap
        beyond it at 24. 28 - 24 = 4.

        It exists so COMPLEX can be centred on what a reader sees rather than
        on a component edge. */
    constexpr int kTrackTop = 4;

    /** The rule-and-legend row bracketing LOW and HIGH. */
    constexpr int kLegendRow = 16;

    constexpr int kDetectorBlock = kDetectorRow * 2 + kSwitchGap + kLegendRow;

    /** dBFS from a linear peak, floored at the meters' own bottom so a silent
        input parks the bar at the left rather than at minus infinity. */
    float meterDb (float linear)
    {
        return juce::Decibels::gainToDecibels (linear, kGateOffDb);
    }

    /** The GR bar's full-scale reading. 24 dB, which is BMO Opto's
        DynamicsMeter scale -- the two dynamics modules in the suite should not
        disagree about what "a lot of reduction" looks like, and at the top of
        AMOUNT this module reaches about 26. */
    constexpr float kMaxReductionDb = 24.0f;

    /** What COMPLEX and the five controls it reveals are drawn in.

        Red -- Frosty, 2026-09-15, from the drawer rendered in azure, amber and
        red in both appearances. Azure and amber both look better in the dark
        one and both fail in the pale: these are knob *captions* as well as
        faces, and as ink on the silver plate amber measures 1.20:1 and azure
        1.34, against the 1.72-2.00 band a raw caption is allowed. Red is
        1.99:1 -- inside it, and the only one of the three that is.

        It costs the distinction from SAUCE, which is the same red. That is the
        trade: a drawer that opens and a release curve that is running now look
        alike, and the captions read. */
    inline juce::Colour drawerTint() { return ui::tokens().meterClip; }
}

VcompPanel::VcompPanel (ui::ModuleContext ctx)
    : ModulePanel (std::move (ctx)),
      // AMOUNT and MAKEUP are character knobs in the module's accent: they are
      // what this module *is*. The five detector knobs below are utility --
      // the suite's azure, dotted track -- because five more accent knobs
      // would compete with AMOUNT for the eye, and these are controls you set
      // once rather than ride.
      amount (context.params.param (Index::amount), "AMOUNT",
              ui::Knob::Style::character, 0.62f, context.def.accent),
      output (context.params.param (Index::output), "MAKEUP",
              ui::Knob::Style::character, 0.62f, context.def.accent),
      inBar  ("IN",  LevelBar::Grow::rightward, kGateOffDb, 0.0f,
              [this] { return context.inputPeak ? meterDb (context.inputPeak()) : kGateOffDb; }),
      grBar  ("GR",  LevelBar::Grow::leftward, 0.0f, kMaxReductionDb,
              [this] { return context.gainReductionDb ? context.gainReductionDb() : 0.0f; }),
      outBar ("OUT", LevelBar::Grow::rightward, kGateOffDb, 0.0f,
              [this] { return context.peak ? meterDb (context.peak()) : kGateOffDb; }),
      // COMPLEX is neither a bypass, a mono nor a polarity, so it takes
      // switchAlt -- the table in modules/AGENTS.md, not a free choice.
      complexSwitch (context.params.param (Index::complex), "COMPLEX", drawerTint()),
      // SAUCE takes the engaged red rather than switchAlt, which is an
      // exception to the switch table in modules/AGENTS.md and the second one
      // in the suite. BMO Opto is the first, and for the same reason: a panel
      // with no colour of its own can let the one colour on it mean "on".
      // Frosty, 2026-09-14. The red is tokens().meterClip, the same one Opto
      // lights TELE, LINK and COLOR with -- not a new hex, and themable.
      arcSwitch     (context.params.param (Index::arc),     "SAUCE",   ui::tokens().meterClip),
      attackKnob    (context.params.param (Index::attack),    "ATTACK"),
      releaseKnob   (context.params.param (Index::release),   "RELEASE"),
      sidechainKnob (context.params.param (Index::sidechain), "DETECT"),
      lowThruKnob   (context.params.param (Index::lowThru),   "LOW"),
      highThruKnob  (context.params.param (Index::highThru),  "HIGH")
{
    // The printed scales -- Frosty, 2026-09-14. Not evenly spaced, and that is
    // the point: the figures crowd toward 0 because that is the end a reader
    // works at. -60 is a floor you need named once; everything between -24 and
    // 0 is where a vocal actually sits and where the gate gets set.
    //
    // GR's positions are amounts of reduction, 0..24 and positive, because
    // that is what the DSP reports. Its *text* is negative, because what the
    // meter means is gain. ScaleMark keeps the two apart for exactly this.
    // **-18 sits at the halfway point and the scale opens out toward 0** --
    // Frosty, 2026-09-14. The bar is no longer linear in dB: the top 18 take
    // half its length and the bottom 42 take the other half, because the top
    // is where a vocal lives and where the gate gets set, and -60 is a floor
    // you need named once.
    //
    // Hand-placed, and it has to be. Sweeping an exponent cannot hold -18 near
    // the middle *and* keep opening out above it; BMO Opto's GR scale hit the
    // same wall and stopped pretending to be a power law.
    //
    // **-3 joined on 2026-09-15 and cost nothing**, which is the part worth
    // understanding. Adding a seventh figure to a 202 px well should crowd it,
    // and the first attempt at a fix -- dropping -24 to make room -- came back
    // *worse*, 8.5 px at its tightest against the six-figure scale's 10.5, and
    // left a 101 px void between -60 and -18. The room came from easing the
    // whole curve instead: -18 moved 0.50 to 0.47 and everything above it came
    // down with it, which is 10.5 px at the tightest again with seven figures
    // rather than six. Rendered, then scanned; both numbers are off the pixels.
    //
    // Per-dB density across the marks runs 0.0094, 0.022, 0.027, 0.027, 0.033,
    // 0.037 -- monotonic toward 0, which is the property to preserve if one is
    // ever moved.
    const std::vector<LevelBar::ScaleMark> levelScale {
        { -60.0f, 0.00f, "-60" }, { -24.0f, 0.34f, "-24" }, { -18.0f, 0.47f, "-18" },
        { -12.0f, 0.63f, "-12" }, {  -6.0f, 0.79f,  "-6" }, {  -3.0f, 0.89f,  "-3" },
        {   0.0f, 1.00f,   "0" },
    };

    inBar .setScale (levelScale);
    outBar.setScale (levelScale);

    // **The fill is a gradient along the bar, not a colour chosen by the
    // level** -- Frosty, 2026-09-15. Grey through -12, into yellow by -3, into
    // red at the top.
    //
    // The distinction is the whole of it. Until now the bar took one colour
    // from its own peak, so crossing -1 turned the *entire* bar red, including
    // the quiet end that was nowhere near clipping. A gradient means a given
    // dB is always the same colour and the bar says where in its range it is
    // rather than only how far along it has got.
    //
    // Grey rather than the suite's meterLow green: this panel is greyscale,
    // and a green bar on it was the same loose end periwinkle was. meterQuiet
    // is the token, and it is mid so that it reads in an LTV well at either
    // end of the range -- 24.5 L* clear of the silver one, 31.6 of the
    // graphite.
    // The stops are where each colour *arrives*, not where it starts blending,
    // which is the thing to get right. Putting the red at 0 rather than at -3
    // was the first attempt and it never showed red at all: the top 3 dB spent
    // themselves finishing a blend, so a bar reading -2 was still amber. Red
    // reaching -3 gives it the whole -3..0 to be red in.
    const std::vector<LevelBar::ZoneStop> zones {
        { -60.0f, ui::tokens().meterQuiet },
        { -12.0f, ui::tokens().meterQuiet },
        {  -6.0f, ui::tokens().meterHigh  },
        {  -3.0f, ui::tokens().meterClip  },
        {   0.0f, ui::tokens().meterClip  },
    };

    inBar .setZones (zones);
    outBar.setZones (zones);

    // GR gets the same treatment about its own zero, which is the *right* end:
    // its positions are amounts of reduction and its fill grows leftward from
    // none. So the first 3 dB of reduction take a fifth of the bar and the
    // last 12 take two fifths -- the difference between 1 and 3 dB is worth
    // seeing and the difference between 20 and 24 is not.
    //
    // The text is negative where the position is positive. ScaleMark keeps the
    // two apart for exactly this: what the DSP reports is reduction, what the
    // meter means is gain.
    grBar .setScale ({ {  0.0f, 0.00f,   "0" }, {  3.0f, 0.20f,  "-3" },
                       {  6.0f, 0.35f,  "-6" }, { 12.0f, 0.60f, "-12" },
                       { 24.0f, 1.00f, "-24" } });

    for (auto* k : { &amount, &output })
        k->setKnobSide (kKnobSide);

    // The drawer takes the colour of the switch that opens it, so COMPLEX and
    // the five controls it reveals read as one thing rather than as a switch
    // and five knobs that happened to turn up. Frosty, 2026-09-15.
    for (auto* k : { &attackKnob, &releaseKnob, &sidechainKnob, &lowThruKnob, &highThruKnob })
    {
        styleTrimKnob (*k);
        k->setUtilityTint (drawerTint());
    }

    // GR is flat bronze -- Frosty, 2026-09-15 -- and it took two goes.
    //
    // It was flat `meterGr` azure, on the argument that reduction is not a
    // fault and painting 24 dB of it in the clip red would be the meter
    // telling the user off for using the module. True, and it left the last
    // suite colour on a panel that had none, reading as a different
    // instrument between two grey-to-red bars.
    //
    // Red was the next answer and lasted one render. It put "near clipping" at
    // the top of IN and OUT and "working" across the whole of GR -- two
    // meanings in one colour, told apart only by which bar they were in.
    //
    // Bronze is warm without being on the ramp. The level bars run amber at 42
    // degrees to red at 6, so a warm colour *between* those reads as a level;
    // the candidates that did -- deep gold, copper -- were rendered and
    // rejected for exactly that. See tokens().meterGrWarm.
    //
    // Flat rather than a gradient, which is the half of the first argument
    // that survives: a gradient says "further along is worse", and on this bar
    // further along is only further along.
    grBar.setFlatColour (ui::tokens().meterGrWarm);

    // The gate, on the meter that shows the level it acts on. Red rather than
    // the module accent -- Frosty, 2026-09-14 -- which is the same call as
    // SAUCE above and leaves the panel greyscale but for the two things that
    // act: the switch that is on, and the threshold that is cutting.
    inBar.attachThreshold (context.params.param (Index::gate), ui::tokens().meterClip);

    for (auto* c : std::initializer_list<juce::Component*> {
             &amount, &inBar, &grBar, &outBar, &output, &complexSwitch, &arcSwitch,
             &attackKnob, &releaseKnob, &sidechainKnob, &lowThruKnob, &highThruKnob })
        addAndMakeVisible (c);

    lastComplex = context.params.param (Index::complex).getValue() > 0.5f;
    applyComplex (lastComplex);

    startTimerHz (30);
}

VcompPanel::~VcompPanel() { stopTimer(); }

void VcompPanel::applyComplex (bool on)
{
    for (auto* k : std::initializer_list<juce::Component*> {
             &attackKnob, &releaseKnob, &sidechainKnob, &lowThruKnob, &highThruKnob })
        k->setVisible (on);

    // Standard mode runs ARC whatever the parameter says -- DspCore substitutes
    // kStandardArc -- so the switch is held on and made unclickable rather than
    // disabled or hidden. Locked, not disabled, for the reason BMO Opto's COLOR
    // is: the disabled alpha pulls a switch's fill and its ink toward the plate
    // at the same rate, which says it at about 1.3:1.
    arcSwitch.setLockedOn (! on);

    // Coming out of the lock, hand the switch back what the user actually set.
    // The lock never wrote to the parameter, so the value is still there, but
    // nothing else will push it into the button: the attachment only speaks
    // when the parameter changes, and it has not.
    if (on)
        arcSwitch.setToggleStateSilently (context.params.param (Index::arc).getValue() > 0.5f);
}

void VcompPanel::timerCallback()
{
    inBar.refresh();
    grBar.refresh();
    outBar.refresh();

    // COMPLEX is polled rather than listened to, same as the meters and BMO
    // Opto's mode poll -- there is no cross-thread marshaling to get right for
    // a once-in-a-while UI state change, and polling means host automation of
    // COMPLEX moves the panel exactly as a click does.
    const auto on = context.params.param (Index::complex).getValue() > 0.5f;

    if (on != lastComplex)
    {
        lastComplex = on;
        applyComplex (on);
    }
    else if (! on)
    {
        // Re-asserted while the lock holds: host automation of ARC still
        // reaches the attachment in standard mode and would otherwise put the
        // stored value back on screen under a switch the DSP is holding on.
        arcSwitch.setLockedOn (true);
    }
}

void VcompPanel::paintPanel (juce::Graphics& g)
{
    // The IGNORE legend: the word, and a rule reaching out from each side of
    // it to the ends of the pair it brackets.
    //
    // Drawn here rather than through ModulePanel::addRule because a section
    // rule is the panel's and takes the panel's ink, and this one belongs to
    // the drawer -- it appears and disappears with COMPLEX and it carries the
    // drawer's colour. The panel has no section rules of its own; see the
    // class comment for why one compressor gets no dividing lines.
    if (! lastComplex || ignoreRow.isEmpty())
        return;

    const auto row = ignoreRow.toFloat();
    const auto ink = drawerTint();
    const auto font = ui::labelFont (ui::Tokens::gainCaptionSize, true);

    const auto text = juce::String ("PASS THROUGH");
    const auto textWidth = juce::GlyphArrangement::getStringWidth (font, text);
    const auto gap = 8.0f;

    ui::drawLabel (g, text, row, juce::Justification::centred, font, ink);

    g.setColour (ink.withAlpha (0.55f));

    const auto y = row.getCentreY();
    const auto half = textWidth * 0.5f + gap;

    g.drawLine (row.getX(), y, row.getCentreX() - half, y, 1.0f);
    g.drawLine (row.getCentreX() + half, y, row.getRight(), y, 1.0f);
}

void VcompPanel::resized()
{
    auto area = getLocalBounds().reduced (kPad, 4);

    const auto content = kSwitchHeight + kKnobHeight + kMeterBlock + kKnobHeight
                             + kSwitchHeight + kDetectorBlock;

    // Seven divisions -- a margin above the first block and below the last as
    // well as between them -- and the gap is worked out from the *complex-on*
    // content whether or not the detector rows are visible. That is what keeps
    // AMOUNT, the meters, MAKEUP and the switch row at the same pixel in both
    // states; in standard mode the reserved rows and the margin under them are
    // simply empty plate. See the class comment for why that trade was taken.
    const auto gap = juce::jmax (kSwitchGap, (area.getHeight() - content) / 7);

    area.removeFromTop (gap);

    // SAUCE above AMOUNT, centred on it. It is the programme-dependent release
    // and standard mode runs it whatever the parameter says, so it belongs to
    // AMOUNT rather than to the drawer COMPLEX opens -- which is where it sat
    // until 2026-09-14, abreast of COMPLEX, reading as one of the advanced
    // controls it is not.
    arcSwitch.setBounds (area.removeFromTop (kSwitchHeight)
                             .withSizeKeepingCentre (kSwitchWidth, kSwitchHeight));
    area.removeFromTop (gap);

    amount.setBounds (area.removeFromTop (kKnobHeight)
                          .withSizeKeepingCentre (kKnobWidth, kKnobHeight));
    area.removeFromTop (gap);

    // IN, GR, OUT top to bottom: what arrived, what was taken off it, what
    // left. Reading order is signal order.
    {
        auto block = area.removeFromTop (kMeterBlock);

        inBar.setBounds (block.removeFromTop (kBarRow + LevelBar::kTagRow));
        block.removeFromTop (kBarGap);
        grBar.setBounds (block.removeFromTop (kBarRow));
        block.removeFromTop (kBarGap);
        outBar.setBounds (block.removeFromTop (kBarRow));
    }
    area.removeFromTop (gap);

    output.setBounds (area.removeFromTop (kKnobHeight)
                          .withSizeKeepingCentre (kKnobWidth, kKnobHeight));
    area.removeFromTop (gap);

    // COMPLEX alone now, centred: the switch that opens the drawer, directly
    // over what it opens. SAUCE used to sit beside it and is at the head.
    complexSwitch.setBounds (area.removeFromTop (kSwitchHeight)
                                 .withSizeKeepingCentre (kSwitchWidth, kSwitchHeight));
    area.removeFromTop (gap);

    // The reserved rows: the three detector-timing controls above the two that
    // decide which band is being compressed at all. Grouped by what they do
    // rather than packed to fill, so the row break means something.
    {
        auto row = area.removeFromTop (kDetectorRow);
        const auto each = row.getWidth() / 3;

        attackKnob   .setBounds (row.removeFromLeft (each));
        releaseKnob  .setBounds (row.removeFromLeft (each));
        sidechainKnob.setBounds (row.removeFromLeft (each));
    }

    area.removeFromTop (kSwitchGap);

    // The group legend over the pair below -- drawn in paintPanel. It names
    // what those two knobs do, which neither caption can: LOW and HIGH say
    // which band, and nothing on either knob says the band is being left out.
    ignoreRow = area.removeFromTop (kLegendRow);

    {
        auto row = area.removeFromTop (kDetectorRow);

        // **Two knobs across the full width, not two thirds of a three-column
        // grid.** They carried the longest captions in the module until
        // 2026-09-15 -- "LOW THRU" and "HIGH THRU", which in an 80 px column
        // rendered as "LOW THR" and "HIGH TH", because Graphics::drawText
        // curtails what will not fit rather than spilling it, so a caption
        // wider than its own control loses its tail silently. They are LOW and
        // HIGH now and would fit a third of the width, but the pair stays
        // full-width: the IGNORE legend above brackets these two and nothing
        // else, and a two-column row is what says so.
        //
        // ui_layout passed with them clipped, and the reason is worth keeping:
        // not because PlainKnob::captionOverflow was wrong -- it reports 10.7
        // and 13.3 px, correctly -- but because this module had not been added
        // to that test's product list, so its panel was never looked at. That
        // list is one of the shared files modules/AGENTS.md tells a new module
        // to edit, and missing it fails exactly this way: silently, with a
        // green suite.
        const auto each = row.getWidth() / 2;

        lowThruKnob .setBounds (row.removeFromLeft (each));
        highThruKnob.setBounds (row.removeFromLeft (each));
    }

    // COMPLEX last, and centred in the space it actually sits in rather than
    // placed by the run of gaps above it -- Frosty, 2026-09-15. The gap
    // arithmetic distributes evenly over seven divisions, which is right for
    // the blocks and wrong for a lone switch between two unequal neighbours:
    // it had MAKEUP's caption close above it and the drawer's first ring of
    // track dots further below.
    //
    // Measured between what a reader sees, not between component bounds. A
    // trim knob's box starts well above its dotted track, so centring on
    // attackKnob.getY() would centre on nothing -- kTrackTop is how far down
    // that box the first dot actually falls.
    {
        const auto above = output.getBottom();
        const auto below = attackKnob.getY() + kTrackTop;

        complexSwitch.setBounds (juce::Rectangle<int> (kSwitchWidth, kSwitchHeight)
                                     .withCentre ({ getWidth() / 2, (above + below) / 2 }));
    }
}

} // namespace bmo::vcomp
