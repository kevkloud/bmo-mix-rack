#include "EqPanel.h"
#include "modules/eq/params.h"

namespace bmo::eq
{

namespace
{
    // The column's budget, top to bottom, inside the padding: it adds up to
    // the common content height with a few pixels over. Every module shares
    // the height, so the EQ, the tallest of them, sets the pace.
    // The input and output sections are ui::ModulePanel's now -- the knob rows,
    // the rules and the switch row all come from there. What is left here is
    // what this module actually is.
    constexpr int kBandRow   = 112;   // knob, gap, dotted track, gap, legend
    constexpr int kFilterRow = 76;

    constexpr int kSwitchWidth  = ui::Tokens::switchWidth;
    constexpr int kSwitchHeight = ui::Tokens::switchHeight;

    /** The plate between the oversampling switches and the output rule under
        them, so the row does not sit on the line. The Saturator's measures the
        same 8 px. */
    constexpr int kOsFoot = 8;

    /** The plate between the MID dial and HI-Q beside it. */
    constexpr int kHiQGap = 8;

    /** HI-Q is square rather than the suite's 70 x 26 switch: it sits in the
        margin beside the mid bell, where a full-width switch would have pushed
        the dial off the panel's centre line. Frosty's size, 2026-09-17, from a
        ladder of 40, 46 and 54.

        A square switch only works because its label is pinned. The look and
        feel sets a switch's text at 62% of its *height*, so a square one grows
        the label faster than the width it has to fit in, and "HI-Q" overflowed
        at every square size there is -- 11.3 px at 40, 16.0 at 54, both caught
        by checkSwitchLabelsFit. See kSwitchLabelSize. */
    constexpr int kHiQSide = 40;

    /** How far HI-Q sits in from the panel's right margin, so that the plate
        either side of it reads equal -- Frosty, 2026-09-17.

        Measured rather than derived: the dial's ink is not symmetric about its
        centre, because the frequencies occupy the left half of the ring and the
        gain the right, so the widget's own half-width (the legend's limit) is
        58 px on the left and the ink reaches only 30 on the right. Rendered at
        the 95 px band row, the mid dial's ink ends at design x 170 and the
        content edge is 270, so a 40 px switch centred in what is left sits 30
        from each. Re-measure if the band row changes. */
    constexpr int kHiQInset = 30;

    /** The point size a switch on the suite's own 26 px row sets its label at:
        62% of the box inside its 3 px inset. HI-Q keeps it while being square. */

    constexpr float kSwitchLabelSize = (float) (ui::Tokens::switchHeight - 6) * 0.62f;
}

EqPanel::EqPanel (ui::ModuleContext ctx)
    : ModulePanel (std::move (ctx)),
      inputGain   (context.params.param (Index::inputGain),   "INPUT"),
      outputLevel (context.params.param (Index::outputLevel), "OUTPUT"),
      // The last argument alternates down the panel: inset, outset, inset. Its
      // job is to stop the three bands lining their end labels up into a
      // column of numbers down the middle -- the high shelf's lowest sits
      // directly above the mid bell's highest, with only a rule between them.
      // A fourth band would carry on alternating, so it takes false.
      high     (context.params.param (Index::hfFreq),  context.params.spec (Index::hfFreq),  &context.params.param (Index::hfGain),  context.def.accent, false),
      mid      (context.params.param (Index::midFreq), context.params.spec (Index::midFreq), &context.params.param (Index::midGain), context.def.accent, true),
      low      (context.params.param (Index::lfFreq),  context.params.spec (Index::lfFreq),  &context.params.param (Index::lfGain),  context.def.accent, false),
      highPass (context.params.param (Index::hpfFreq), context.params.spec (Index::hpfFreq), nullptr, context.def.accent),
      eqIn   (context.params.param (Index::eqIn),   "EQL", context.def.accent),
      phase  (context.params.param (Index::phase),  ui::BmoLookAndFeel::phaseGlyph(), ui::tokens().polarity),
      midHiQ (context.params.param (Index::midHiQ), "HI-Q", ui::tokens().switchAlt),
      autoGain (context.params.param (Index::autoGain), "AUTO", ui::tokens().switchAlt),
      os2x ("2x"), os4x ("4x"), osHq ("8x")
{
    for (auto* c : std::initializer_list<juce::Component*> {
             &inputGain, &high, &mid, &low, &highPass, &eqIn, &phase, &midHiQ,
             &autoGain, &outputLevel })
        addAndMakeVisible (c);

    // Oversampling is anything-else by the table in modules/AGENTS.md, so the
    // three light in switchAlt, the same as AUTO beside them.
    for (auto* b : { &os2x, &os4x, &osHq })
    {
        b->setClickingTogglesState (false);
        b->setColour (juce::ToggleButton::tickColourId, ui::tokens().switchAlt);
        addAndMakeVisible (b);
    }

    osAttachment = std::make_unique<juce::ParameterAttachment> (
        context.params.param (Index::oversampling),
        [this] (float value) { showOversampling (juce::roundToInt (value)); });

    const auto choose = [this] (int choice)
    {
        return [this, choice]
        {
            const auto& p = context.params.param (Index::oversampling);
            const auto current = juce::roundToInt (p.convertFrom0to1 (p.getValue()));

            osAttachment->setValueAsCompleteGesture ((float) (current == choice ? 0 : choice));
        };
    };

    os2x.onClick = choose (1);
    os4x.onClick = choose (2);
    osHq.onClick = choose (3);

    osAttachment->sendInitialUpdate();

    for (auto* k : { &inputGain, &outputLevel })
        styleTrimKnob (*k);

    // A band is the one control here with no caption of its own, so it gets
    // the name of the rule it sits under. tests/ui/LayoutTests.cpp pins this
    // column to absolute rows and finds the bands by these names.
    high.setName     ("HIGH");
    mid.setName      ("MID");
    low.setName      ("LOW");
    highPass.setName ("LO-CUT");

    // Polarity is white in every module; its label is what says which module.
    phase.setActiveInkFrom (context.def.accent);

    // Square, so its label keeps the size the switch row sets rather than one
    // derived from a box that is twice as tall. See SwitchButton::setLabelSize.
    midHiQ.setLabelSize (kSwitchLabelSize);
}


void EqPanel::showOversampling (int choice)
{
    os2x.setToggleState (choice == 1, juce::dontSendNotification);
    os4x.setToggleState (choice == 2, juce::dontSendNotification);
    osHq.setToggleState (choice == 3, juce::dontSendNotification);
}

void EqPanel::resized()
{
    clearRules();

    auto area = getLocalBounds().reduced (kPad, 4);

    const auto rule = [&] (const juce::String& text)
    {
        addRule (area.removeFromTop (kRuleRow), text);
    };

    // Both sections, and the output one comes off the foot first so the bands
    // in between get what is left rather than a number that has to be redone
    // every time one of them changes.
    const auto out = takeOutputSection (area);
    const auto in  = takeInputSection (area);

    inputGain.setBounds (in.knob);

    // What the oversampling section costs the column: its own rule, the switch
    // row, and the plate between that row and the output rule under it.
    constexpr int kOsSection = kRuleRow + kSwitchHeight + kOsFoot;

    // The bands take what is left rather than a constant, so the column ends
    // flush against the output rule whatever sits above it -- the same reason
    // takeOutputSection comes off the foot first. Three rules fall inside this
    // area (MID, LOW, LO-CUT); HIGH's is the input section's.
    //
    // 95 px each as this stands, against the 112 they had before the
    // oversampling section arrived: the dial is drawn from the cell's height,
    // so a band's ring goes 59.5 design px to 50.5. Rendered against paying for
    // the section out of the LO-CUT row instead, and against dropping the rule
    // above each band and naming the sections beside the dials -- which came
    // within 3 px of the old dial, and which Frosty turned down to keep the
    // column straight. 2026-09-17.
    const int bandRow = (area.getHeight() - 3 * kRuleRow - kFilterRow - kOsSection) / 3;

    // The input section's rule is this panel's HIGH rule -- the same row, and
    // the only one in the suite that carries a legend. A module with nothing
    // to name there pushes it with an empty string.
    addRule (in.rule, "HIGH");
    high.setBounds (area.removeFromTop (bandRow));

    rule ("MID");

    {
        // HI-Q belongs to the mid bell and now sits with it, in the width the
        // dial is not using: a band is a circle in a letterbox, so there are
        // 65 px of bare plate either side of it. It spent 0.2.3 onwards down on
        // the switch row because the common height left no *row* for it here --
        // but it never needed a row, only a margin. Frosty, 2026-09-17.
        //
        // On the gain half of the dial, not the frequency half, where it would
        // crowd 3k2 and 1k6; that pair was rendered.
        //
        // The dial stays on the panel's centre line with INPUT, LO-CUT and
        // OUTPUT -- the column reads as a straight stack, which is what it is
        // for. Sliding the dial left to centre it with the switch as a pair was
        // built and rendered and Frosty turned it down as unbalanced,
        // 2026-09-17.
        //
        // So the switch is square instead of the suite's 70 x 26, which is what
        // lets it live in the margin without the dial moving: the cell gives up
        // the same width on both sides, and what the dial loses is width it was
        // not using. Its size is unchanged either way -- a band's radius comes
        // off the cell's height.
        const auto row = area.removeFromTop (bandRow);

        // Centred in the plate between the dial and the panel's margin: the
        // dial's ink stops 30 px short of the switch and the switch stops 30 px
        // short of the edge. Frosty, 2026-09-17, on a ladder of sizes and
        // insets. See kHiQInset for why that number is written down rather
        // than derived.
        const auto square = juce::Rectangle<int> (kHiQSide, kHiQSide)
                                .withCentre ({ row.getRight() - kHiQInset - kHiQSide / 2,
                                               row.getCentreY() });

        // The cell stops short of the switch, because a cell laid over it would
        // take the clicks meant for it -- and the dial is pushed back to the
        // panel's centre line, which the shortened cell would otherwise carry
        // it off. HIGH, LOW, INPUT, LO-CUT and OUTPUT are all on that line.
        const auto cell = row.withTrimmedRight (row.getRight() - square.getX() + kHiQGap);

        mid.setBounds (cell);
        mid.setDialOffset (row.getCentreX() - cell.getCentreX());
        midHiQ.setBounds (square);
    }

    rule ("LOW");
    low.setBounds (area.removeFromTop (bandRow));

    rule ("LO-CUT");
    // Whatever a third of the bands' share rounded away lands here, so the
    // column stays flush against the rule below.
    highPass.setBounds (area.removeFromTop (area.getHeight() - kOsSection));

    // Oversampling: a rule that names it and three switches under it, the same
    // shape the Saturator's took the day before. This module's default is 2x
    // rather than Off, so one of the three is lit at Init -- the first time the
    // panel has said that this module starts with latency.
    //
    // Rendered against stacking the row straight above the switches with no
    // rule, which costs the bands 20 px less. Frosty took the named section,
    // 2026-09-17: this is the one panel in the suite whose rules carry legends,
    // so an unnamed row is the odd thing here.
    {
        addRule (area.removeFromTop (kRuleRow), "OVERSAMPLING");

        auto group = area.removeFromTop (kSwitchHeight)
                         .withSizeKeepingCentre (kSwitchWidth * 3 + ui::Tokens::switchGap * 2,
                                                 kSwitchHeight);
        constexpr int gap = ui::Tokens::switchGap;

        os2x.setBounds (group.removeFromLeft (kSwitchWidth));
        group.removeFromLeft (gap);
        os4x.setBounds (group.removeFromLeft (kSwitchWidth));
        group.removeFromLeft (gap);
        osHq.setBounds (group);
    }

    addRule (out.rule, {});

    {
        // Three switches at the suite's own width. AUTO takes the place HI-Q
        // left when it went up to the mid band, so the row neither grew nor had
        // to narrow to hold a fourth -- which is what it had to do while HI-Q
        // was still here and AUTO arrived: four at 58 px instead of three at 70.
        constexpr int gap = ui::Tokens::switchGap;

        auto group = out.switches.withSizeKeepingCentre (kSwitchWidth * 3 + gap * 2, kSwitchHeight);

        eqIn.setBounds (group.removeFromLeft (kSwitchWidth));
        group.removeFromLeft (gap);
        phase.setBounds (group.removeFromLeft (kSwitchWidth));
        group.removeFromLeft (gap);
        autoGain.setBounds (group);
    }

    // Every knob on the panel shares one centre line, output included. The
    // output meter used to sit out at the right margin of this row; it is
    // gone, so the knob has the row to itself and is centred in it like
    // every other.
    //
    // A MIX knob was rendered here beside it and Frosty took it off on sight,
    // 2026-09-17: Mix is not something this module should offer at all.
    outputLevel.setBounds (out.knob);
}

} // namespace bmo::eq
