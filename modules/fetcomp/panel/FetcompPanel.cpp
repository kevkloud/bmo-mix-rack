#include "FetcompPanel.h"
#include "modules/fetcomp/params.h"

namespace bmo::fetcomp
{

namespace
{
    /** **The bezel opacity this module ships with, and the one place to flip
        it.**

        `ui::DynamicsMeter` has always stroked its bezel at 0.7, and that alpha
        is the default of the opt-in setter, so leaving this at 0.7 leaves BMO
        Opto's render byte for byte where it was.

        The candidate is full alpha. It matters here and nowhere else in the
        suite, because this bezel carries the voicing: against `meterFace` the
        two states separate by 3.88:1 at 0.7 and by 5.13:1 at 1.0. Which one
        ships is a call taken on renders and it is the owner's
        (docs/1176-comp/11-integration-and-test-plan.md 4d step 6) -- render
        the other one with `ui.bezel=full` rather than by editing this. */
    constexpr float kBezelAlpha = 0.7f;
    constexpr float kFullBezelAlpha = 1.0f;

    // The knob draws at kKnobSide; the cell is wider so the caption underneath
    // has room. BMO Opto's MAKEUP is the case that forced the distinction --
    // at the width the knob wanted, the caption clipped to "MAKEU".
    constexpr int kKnobSide  = 84;
    constexpr int kPairRow   = 120;   ///< a row of two character knobs
    constexpr int kMixRow    = ui::ModulePanel::kTrimKnobRow;

    constexpr int kSwitchWidth  = ui::Tokens::switchWidth;
    constexpr int kSwitchHeight = ui::Tokens::switchHeight;
    constexpr int kSwitchGap    = ui::Tokens::switchGap;

    /** The meter and the row under it are the same width, and it is the width
        three switches and their two gaps want: 226.

        modules/AGENTS.md records BMO Opto's IN/GR/OUT row as the one place a
        switch may be narrower than `switchWidth`, because three of them plus
        two gaps need 226 px and that panel is 220 wide. It also says to use
        the full width and delete the exception on a panel that can afford it.
        This one is 260, so it does. */
    constexpr int kMeterWidth     = kSwitchWidth * 3 + kSwitchGap * 2;
    constexpr int kMeterHeight    = 102;
    constexpr int kMeterButtonGap = 4;      ///< meter face to its IN/GR/OUT row
    constexpr int kMeterBlock     = kMeterHeight + kMeterButtonGap + kSwitchHeight;

    /** Two rows of two, then ALL on its own. See the class comment for why the
        fifth button is set apart rather than made the end of a row. */
    constexpr int kRatioBlock = kSwitchHeight * 3 + kSwitchGap * 2;

    /** A pair of switches side by side, and the block they make. */
    constexpr int kPairWidth = kSwitchWidth * 2 + kSwitchGap;
}

FetcompPanel::FetcompPanel (ui::ModuleContext ctx)
    : ModulePanel (std::move (ctx)),
      // The four knobs are the module: character style, in the module's own
      // colour. MIX is not -- it is set once and left, so it takes the shared
      // trim size and the utility blue every INPUT and OUTPUT in the suite
      // wears, which is also what keeps it from reading as a fifth headline
      // control.
      inputKnob   (context.params.param (Index::input),   "INPUT",
                   ui::Knob::Style::character, 0.62f, context.def.accent),
      outputKnob  (context.params.param (Index::output),  "OUTPUT",
                   ui::Knob::Style::character, 0.62f, context.def.accent),
      attackKnob  (context.params.param (Index::attack),  "ATTACK",
                   ui::Knob::Style::character, 0.62f, context.def.accent),
      releaseKnob (context.params.param (Index::release), "RELEASE",
                   ui::Knob::Style::character, 0.62f, context.def.accent),
      mixKnob     (context.params.param (Index::mix),     "MIX"),
      meter (context.inputRms, context.rms, context.gainReductionDb,
             ui::DynamicsMeter::Mode::reduction, context.def.accent,
             ui::accentTextOn (context.def.accent, ui::tokens().meterFace)),
      meterInButton ("IN"), meterGrButton ("GR"), meterOutButton ("OUT"),
      ratio4Button ("4:1"), ratio8Button ("8:1"), ratio12Button ("12:1"),
      ratio20Button ("20:1"), ratioAllButton ("ALL"),
      voicingBlueButton ("BLUE"), voicingBlackButton ("BLACK"),
      os2xButton ("2x"), os4xButton ("4x")
{
    for (auto* k : { &inputKnob, &outputKnob, &attackKnob, &releaseKnob })
        k->setKnobSide (kKnobSide);

    // ATTACK and RELEASE print their values, which is the one pair in the
    // suite that has to. The parameter *is* the knob position, 1..7, so the
    // number on a host's automation lane says nothing about the time it
    // selects -- the value line is where "4 (126 us)" is read. See
    // modules/fetcomp/params.h.
    for (auto* k : { &attackKnob, &releaseKnob })
        k->setShowsValue (true);

    styleTrimKnob (mixKnob);

    // The hot zone -- 0 VU and above -- is the accent stepped off the meter's
    // own face until it clears 4.5:1 rather than trusted to, which is
    // OptoPanel::hotColourFor's pattern. It is the same in both voicings: only
    // the bezel carries the voicing.
    meter.setBezelAlpha (kBezelAlpha);

    for (auto* b : { &meterInButton, &meterGrButton, &meterOutButton,
                     &ratio4Button, &ratio8Button, &ratio12Button, &ratio20Button,
                     &ratioAllButton, &voicingBlueButton, &voicingBlackButton,
                     &os2xButton, &os4xButton })
    {
        // Every one of these is a radio over a choice parameter rather than a
        // toggle over a switch: the click sets the parameter and the parameter
        // lights the buttons, so host automation and a click cannot disagree.
        b->setClickingTogglesState (false);
        b->setColour (juce::ToggleButton::tickColourId, context.def.accent);
        addAndMakeVisible (b);
    }

    for (auto* c : std::initializer_list<juce::Component*> {
             &inputKnob, &outputKnob, &attackKnob, &releaseKnob, &mixKnob, &meter })
        addAndMakeVisible (c);

    meterInButton .onClick = [this] { selectMeterMode (ui::DynamicsMeter::Mode::input); };
    meterGrButton .onClick = [this] { selectMeterMode (ui::DynamicsMeter::Mode::reduction); };
    meterOutButton.onClick = [this] { selectMeterMode (ui::DynamicsMeter::Mode::output); };

    // GR is the mode this module is for, so it is where the meter opens and it
    // is the middle button -- IN and OUT then read left to right as signal
    // flow either side of it, the order BMO Opto's row already uses.
    selectMeterMode (ui::DynamicsMeter::Mode::reduction);

    ratioAttachment = std::make_unique<juce::ParameterAttachment> (
        context.params.param (Index::ratio),
        [this] (float value) { showRatio (juce::roundToInt (value)); });

    voicingAttachment = std::make_unique<juce::ParameterAttachment> (
        context.params.param (Index::voicing),
        [this] (float value) { showVoicing (juce::roundToInt (value)); });

    osAttachment = std::make_unique<juce::ParameterAttachment> (
        context.params.param (Index::oversampling),
        [this] (float value) { showOversampling (juce::roundToInt (value)); });

    // Every ratio has a button of its own, so a click always names a position
    // and clicking the lit one is a no-op rather than a way out of it.
    const auto chooseRatio = [this] (int choice)
    {
        return [this, choice] { ratioAttachment->setValueAsCompleteGesture ((float) choice); };
    };

    ratio4Button  .onClick = chooseRatio (ratio4);
    ratio8Button  .onClick = chooseRatio (ratio8);
    ratio12Button .onClick = chooseRatio (ratio12);
    ratio20Button .onClick = chooseRatio (ratio20);
    ratioAllButton.onClick = chooseRatio (ratioAll);

    const auto chooseVoicing = [this] (int choice)
    {
        return [this, choice] { voicingAttachment->setValueAsCompleteGesture ((float) choice); };
    };

    voicingBlueButton .onClick = chooseVoicing (blue);
    voicingBlackButton.onClick = chooseVoicing (black);

    // Off has no switch of its own, so clicking the lit one is the way back to
    // it -- the Saturator's and BMO CEQ's oversampling rows do the same.
    const auto chooseOversampling = [this] (int choice)
    {
        return [this, choice]
        {
            const auto& p = context.params.param (Index::oversampling);
            const auto current = juce::roundToInt (p.convertFrom0to1 (p.getValue()));

            osAttachment->setValueAsCompleteGesture ((float) (current == choice ? osOff : choice));
        };
    };

    os2xButton.onClick = chooseOversampling (os2x);
    os4xButton.onClick = chooseOversampling (os4x);

    // Light whatever the parameters already say, which is how a render and a
    // reopened editor come up in the state they were left in.
    ratioAttachment  ->sendInitialUpdate();
    voicingAttachment->sendInitialUpdate();
    osAttachment     ->sendInitialUpdate();
}

//==============================================================================
void FetcompPanel::selectMeterMode (ui::DynamicsMeter::Mode mode)
{
    meter.setMode (mode);

    meterInButton .setToggleState (mode == ui::DynamicsMeter::Mode::input,     juce::dontSendNotification);
    meterGrButton .setToggleState (mode == ui::DynamicsMeter::Mode::reduction, juce::dontSendNotification);
    meterOutButton.setToggleState (mode == ui::DynamicsMeter::Mode::output,    juce::dontSendNotification);
}

void FetcompPanel::showRatio (int choice)
{
    ratio4Button  .setToggleState (choice == ratio4,   juce::dontSendNotification);
    ratio8Button  .setToggleState (choice == ratio8,   juce::dontSendNotification);
    ratio12Button .setToggleState (choice == ratio12,  juce::dontSendNotification);
    ratio20Button .setToggleState (choice == ratio20,  juce::dontSendNotification);
    ratioAllButton.setToggleState (choice == ratioAll, juce::dontSendNotification);
}

juce::Colour FetcompPanel::bezelFor (int voicingChoice) const
{
    return voicingChoice == blue ? context.def.accent : juce::Colours::black;
}

void FetcompPanel::showVoicing (int choice)
{
    voicingBlueButton .setToggleState (choice == blue,  juce::dontSendNotification);
    voicingBlackButton.setToggleState (choice == black, juce::dontSendNotification);

    if (choice == lastVoicing)
        return;

    lastVoicing = choice;

    // The bezel, and only the bezel. The hot zone keeps the accent in both
    // states, so the meter's warning colour does not change meaning when the
    // voicing does.
    meter.setColours (bezelFor (choice),
                      ui::accentTextOn (context.def.accent, ui::tokens().meterFace));
}

void FetcompPanel::showOversampling (int choice)
{
    os2xButton.setToggleState (choice == os2x, juce::dontSendNotification);
    os4xButton.setToggleState (choice == os4x, juce::dontSendNotification);
}

bool FetcompPanel::setUiState (const juce::String& key, const juce::String& value)
{
    if (key == "meter")
    {
        if (value.equalsIgnoreCase ("IN"))  { selectMeterMode (ui::DynamicsMeter::Mode::input);     return true; }
        if (value.equalsIgnoreCase ("GR"))  { selectMeterMode (ui::DynamicsMeter::Mode::reduction); return true; }
        if (value.equalsIgnoreCase ("OUT")) { selectMeterMode (ui::DynamicsMeter::Mode::output);    return true; }

        return false;
    }

    if (key == "bezel")
    {
        if (value.equalsIgnoreCase ("stock")) { meter.setBezelAlpha (kBezelAlpha);     return true; }
        if (value.equalsIgnoreCase ("full"))  { meter.setBezelAlpha (kFullBezelAlpha); return true; }

        return false;
    }

    return false;
}

//==============================================================================
void FetcompPanel::resized()
{
    // 4, the same content inset every other panel measures from.
    auto area = getLocalBounds().reduced (kPad, 4);

    const auto content = kPairRow + kPairRow + kRatioBlock + kMeterBlock
                           + kSwitchHeight + kMixRow + kSwitchHeight;

    // Eight divisions for seven blocks: a margin above the first and below the
    // last as well as between them, so the spacing stays even if a block's
    // height changes later.
    const auto gap = juce::jmax (kSwitchGap, (area.getHeight() - content) / 8);

    const auto centredRow = [] (juce::Rectangle<int> row, int width)
    {
        return row.withSizeKeepingCentre (width, kSwitchHeight);
    };

    /** A pair of switches centred on one row. */
    const auto layOutPair = [&] (juce::Rectangle<int> row, juce::Button& left, juce::Button& right)
    {
        auto pair = centredRow (row, kPairWidth);
        left .setBounds (pair.removeFromLeft (kSwitchWidth));
        pair.removeFromLeft (kSwitchGap);
        right.setBounds (pair);
    };

    /** Two knobs abreast, each in half the width. */
    const auto layOutKnobs = [] (juce::Rectangle<int> row, ui::PlainKnob& left, ui::PlainKnob& right)
    {
        const auto half = row.getWidth() / 2;
        left .setBounds (row.removeFromLeft (half));
        right.setBounds (row);
    };

    area.removeFromTop (gap);

    // How hard it arrives and how loud it leaves. There is no threshold knob,
    // so these two are the compression control between them.
    layOutKnobs (area.removeFromTop (kPairRow), inputKnob, outputKnob);
    area.removeFromTop (gap);

    // How fast it moves. Both run backwards -- 7 is the fast end -- and both
    // print the time their position means under their names.
    layOutKnobs (area.removeFromTop (kPairRow), attackKnob, releaseKnob);
    area.removeFromTop (gap);

    // The button strip: four ratios in a block, then ALL underneath.
    {
        auto block = area.removeFromTop (kRatioBlock);

        layOutPair (block.removeFromTop (kSwitchHeight), ratio4Button, ratio8Button);
        block.removeFromTop (kSwitchGap);
        layOutPair (block.removeFromTop (kSwitchHeight), ratio12Button, ratio20Button);
        block.removeFromTop (kSwitchGap);
        ratioAllButton.setBounds (centredRow (block.removeFromTop (kSwitchHeight), kSwitchWidth));
    }
    area.removeFromTop (gap);

    // The VU over its IN/GR/OUT row, both the same width. GR in the middle
    // because it is the reading this module is for.
    {
        auto block = area.removeFromTop (kMeterBlock);
        const auto width = juce::jmin (block.getWidth(), kMeterWidth);

        meter.setBounds (block.removeFromTop (kMeterHeight)
                              .withSizeKeepingCentre (width, kMeterHeight));
        block.removeFromTop (kMeterButtonGap);

        auto buttons = block.withSizeKeepingCentre (width, kSwitchHeight);
        const auto buttonWidth = (width - kSwitchGap * 2) / 3;

        meterInButton.setBounds (buttons.removeFromLeft (buttonWidth));
        buttons.removeFromLeft (kSwitchGap);
        meterGrButton.setBounds (buttons.removeFromLeft (buttonWidth));
        buttons.removeFromLeft (kSwitchGap);
        meterOutButton.setBounds (buttons.removeFromLeft (buttonWidth));
    }
    area.removeFromTop (gap);

    // Directly under the meter, because the meter's border is the only thing
    // this pair changes.
    layOutPair (area.removeFromTop (kSwitchHeight), voicingBlueButton, voicingBlackButton);
    area.removeFromTop (gap);

    mixKnob.setBounds (area.removeFromTop (kMixRow));
    area.removeFromTop (gap);

    // Off is the position with no switch of its own: neither lit is Off.
    layOutPair (area.removeFromTop (kSwitchHeight), os2xButton, os4xButton);
}

} // namespace bmo::fetcomp
