#include "OptoPanel.h"
#include "modules/opto/params.h"

namespace bmo::opto
{

namespace
{
    // The knob draws at kKnobSide; the control is laid out kKnobWidth wide so
    // that the caption underneath has room. "MAKEUP" set at 15 pt is wider
    // than the 92 px the knob itself wants, and clipped to "MAKEU" when the
    // two were the same number.
    constexpr int kKnobSide    = 92;
    constexpr int kKnobWidth   = 136;
    constexpr int kKnobHeight  = 150;
    constexpr int kMeterWidth  = 190;
    constexpr int kMeterButtonGap = 4;   ///< meter face to its IN/GR/OUT row

    constexpr int kSwitchWidth  = ui::Tokens::switchWidth;
    constexpr int kSwitchHeight = ui::Tokens::switchHeight;
    constexpr int kSwitchGap    = ui::Tokens::switchGap;

    // The IN/GR/OUT row is a row of switches, so it is switchHeight tall like
    // every other switch in the suite. It was 20, which is what made it read
    // as a different kind of control from TELE/STRESS and LINK/COLOR six inches
    // above and below it.
    constexpr int kMeterButtonRow = kSwitchHeight;

    // A needle meter is a landscape window: at this width the arc stands about
    // 91 px tall. Handing DynamicsMeter the whole 168 px of the middle third,
    // as 0.2.1 did, only bought empty face -- the meter centres its arc in
    // whatever box it is given, so the height it does not need is better spent
    // by the panel.
    //
    // 116 until 0.2.2, of which the meter spent 14 on a caption naming the
    // current mode. That caption is gone -- the row of buttons under it says
    // the same word -- so the face is the same 102 px it always was and the
    // 14 px is the panel's, which is where the taller button row comes from.
    constexpr int kMeterHeight = 102;

    // 0.2.1 carried #9c71c3 here for the captions and the switches, because
    // the accent was a pale lavender chosen for knob caps and there was no
    // token for "the accent, stepped until it is legible". There is now --
    // ui::accentTextOn -- and this panel's colours all come from tokens, so a
    // theme change reaches this module the way it reaches the others. See
    // accentFor / activeFor for which token does what.

    // The face the scale is printed on was #3a3a3a here until 0.2.2 and is
    // now the `meterFace` token, which is what lets a theme move it. It was
    // the last raw hex left in this panel.
}

OptoPanel::OptoPanel (ui::ModuleContext ctx)
    : ModulePanel (std::move (ctx)),
      // Colours here are placeholders that applyModeColours() overwrites at
      // the end of this constructor, and again whenever the mode changes.
      // context.def.accent is deliberately not among them: BMO Opto's
      // lavender identifies the module on its header and in a rack slot bar,
      // but the panel itself is greyscale in both modes.
      crush (context.params.param (Index::crush), "COMP",
             ui::Knob::Style::character, 0.62f, ui::tokens().neutral),
      level (context.params.param (Index::level), "MAKEUP",
             ui::Knob::Style::character, 0.62f, ui::tokens().neutral),
      meter (context.inputRms, context.rms, context.gainReductionDb,
             ui::DynamicsMeter::Mode::output, ui::tokens().neutral,
             ui::tokens().meterClip),
      teleButton ("TELE"), stressButton ("STRESS"),
      meterInButton ("IN"), meterOutButton ("OUT"), meterGrButton ("GR"),
      link  (context.params.param (Index::link),  "LINK",  ui::tokens().meterClip),
      color (context.params.param (Index::color), "COLOR", ui::tokens().meterClip)
{
    // Fill set here only so the buttons exist in a valid state; applyModeColours
    // owns it from the end of this constructor onwards. BmoLookAndFeel derives
    // each label from whatever fill it is drawing, so a lit switch reads dark
    // on colour and an unlit one light on grey without either being stated.
    for (auto* b : { &teleButton, &stressButton,
                     &meterInButton, &meterOutButton, &meterGrButton })
    {
        b->setClickingTogglesState (false);
        b->setColour (juce::ToggleButton::tickColourId, ui::tokens().meterClip);
        addAndMakeVisible (b);
    }

    // A pair in radio behaviour, like the meter's IN/GR/OUT row: clicking sets
    // the mode rather than toggling a button, so host automation and a click
    // land in the same place. timerCallback() is what reads the parameter back
    // into the two states.
    teleButton  .onClick = [this] { setChoice (context.params.param (Index::mode), 0.0f); };
    stressButton.onClick = [this] { setChoice (context.params.param (Index::mode), 1.0f); };

    meterInButton .onClick = [this] { selectMeterMode (ui::DynamicsMeter::Mode::input); };
    meterOutButton.onClick = [this] { selectMeterMode (ui::DynamicsMeter::Mode::output); };
    meterGrButton .onClick = [this] { selectMeterMode (ui::DynamicsMeter::Mode::reduction); };

    selectMeterMode (ui::DynamicsMeter::Mode::output);

    for (auto* k : { &crush, &level })
        k->setKnobSide (kKnobSide);

    for (auto* c : std::initializer_list<juce::Component*> { &crush, &meter, &level, &link, &color })
        addAndMakeVisible (c);

    lastModeWasStressed = context.params.param (Index::mode).getValue() > 0.5f;
    teleButton  .setToggleState (! lastModeWasStressed, juce::dontSendNotification);
    stressButton.setToggleState (  lastModeWasStressed, juce::dontSendNotification);
    color.setLockedOn (! lastModeWasStressed);
    applyModeColours (lastModeWasStressed);

    startTimerHz (15);
}

OptoPanel::~OptoPanel() { stopTimer(); }

void OptoPanel::selectMeterMode (ui::DynamicsMeter::Mode mode)
{
    meter.setMode (mode);

    meterInButton .setToggleState (mode == ui::DynamicsMeter::Mode::input,     juce::dontSendNotification);
    meterOutButton.setToggleState (mode == ui::DynamicsMeter::Mode::output,    juce::dontSendNotification);
    meterGrButton .setToggleState (mode == ui::DynamicsMeter::Mode::reduction, juce::dontSendNotification);
}

bool OptoPanel::setUiState (const juce::String& key, const juce::String& value)
{
    if (key != "meter")
        return false;

    if (value.equalsIgnoreCase ("IN"))  { selectMeterMode (ui::DynamicsMeter::Mode::input);     return true; }
    if (value.equalsIgnoreCase ("OUT")) { selectMeterMode (ui::DynamicsMeter::Mode::output);    return true; }
    if (value.equalsIgnoreCase ("GR"))  { selectMeterMode (ui::DynamicsMeter::Mode::reduction); return true; }

    return false;
}

//==============================================================================
juce::Colour OptoPanel::accentFor (bool) const
{
    // Greyscale in both modes: the faceplate carries no colour, so what colour
    // there is can mean one thing. `neutral` rather than a hex of this panel's
    // own -- see the token for why it is not the accent's lightness in grey.
    return ui::tokens().neutral;
}

juce::Colour OptoPanel::activeFor (bool stressed) const
{
    // The suite's own hot and warm meter colours, not two new hexes. They
    // already mean this, they are already themable, and they measure as well
    // as anything invented for the job: against switchOff, which is what an
    // unlit switch is filled with, amber separates by 2.46:1 and red by
    // 1.48:1, and both take a dark label where an unlit switch takes a light
    // one -- so lit and unlit differ in hue, in lightness and in the polarity
    // of their own text.
    return stressed ? ui::tokens().meterHigh : ui::tokens().meterClip;
}

juce::Colour OptoPanel::hotColourFor (bool stressed) const
{
    // 0 VU and above, in the mode's own colour, stepped off it until it clears
    // 4.5:1 on this dark face rather than being trusted to. Amber already
    // clears at 4.68:1 and comes back untouched; red is lightened to reach it.
    // That difference is why the meter keeps the mode's
    // colour in both modes instead of falling back to red in Stress -- the
    // amber is the more readable of the two, not the less.
    return ui::accentTextOn (activeFor (stressed), ui::tokens().meterFace);
}

void OptoPanel::applyModeColours (bool stressed)
{
    const auto accent = accentFor (stressed);
    const auto active = activeFor (stressed);

    // Knobs, and the meter's bezel, take the neutral: caps, captions, dotted
    // tracks and the plus and minus all derive from that one value. Anything
    // that can be switched on takes the mode's lit colour, and its label is
    // derived from whichever of the two it is currently filled with.
    for (auto* k : { &crush, &level })
        k->setAccent (accent);

    for (auto* s : { &link, &color })
        s->setTint (active);

    for (auto* b : { &teleButton, &stressButton,
                     &meterInButton, &meterOutButton, &meterGrButton })
    {
        b->setColour (juce::ToggleButton::tickColourId, active);
        b->repaint();
    }

    meter.setColours (accent, hotColourFor (stressed));
}

void OptoPanel::setChoice (juce::RangedAudioParameter& param, float normalisedValue)
{
    param.beginChangeGesture();
    param.setValueNotifyingHost (normalisedValue);
    param.endChangeGesture();
}

void OptoPanel::timerCallback()
{
    // Color has no off state in Tele mode: DspCore reads
    // `mode == Mode::OptoUnitA || params.color`, so in Tele it is on whatever the
    // parameter says. The switch is therefore drawn locked on there -- lit,
    // and not clickable.
    //
    // It was drawn *disabled* until 0.2.3, which was wrong twice over. It said
    // colour was off while the DSP had it on, which for the Init preset is the
    // first thing anyone sees on this panel; and the disabled alpha collapses
    // a switch's fill and its ink toward the plate at the same rate, so it
    // said it at 1.27:1 on the pale plate, which is to say invisibly.
    //
    // Not hidden, either: hiding it moved LINK up the panel every time the
    // mode changed, and a control that jumps around is worse than one that
    // stays put.
    //
    // Polled rather than a parameter listener, same as OutputMeter/
    // DynamicsMeter's own timers -- there's no cross-thread marshaling to get
    // right for a once-in-a-while UI state change like this one. The mode
    // button's own label and glow are polled the same way, so host automation
    // of Mode (not just a click) still updates it.
    const auto stressed = context.params.param (Index::mode).getValue() > 0.5f;

    teleButton  .setToggleState (! stressed, juce::dontSendNotification);
    stressButton.setToggleState (  stressed, juce::dontSendNotification);

    if (stressed != lastModeWasStressed)
    {
        lastModeWasStressed = stressed;
        color.setLockedOn (! stressed);
        applyModeColours (stressed);

        // Leaving Tele hands the switch back what the user actually set. The
        // lock never wrote to the parameter, so the value is still there, but
        // nothing else will push it into the button: the attachment only
        // speaks when the parameter changes, and it has not.
        if (stressed)
            color.setToggleStateSilently (context.params.param (Index::color).getValue() > 0.5f);
    }
    else if (! stressed)
    {
        // Re-asserted while the lock holds. Host automation of Color still
        // reaches the attachment in Tele and would otherwise put the stored
        // value back on screen, under a switch the DSP is holding on.
        color.setLockedOn (true);
    }
}

void OptoPanel::resized()
{
    auto area = getLocalBounds().reduced (kPad, 4);

    const auto centredSwitch = [] (juce::Rectangle<int> row)
    {
        return row.withSizeKeepingCentre (kSwitchWidth, kSwitchHeight);
    };

    // One rhythm down the panel, rather than three equal thirds with each
    // block centred inside its own. The blocks are 150, 142 and 150 tall in
    // rows of 190, so the thirds spent 132 px of slack as uneven centring: a
    // 36 px band under TELE, 46 px above the meter, 28 px above LINK -- and
    // nothing at all under COLOR, which sat on the bottom edge of the panel.
    //
    // Every block is placed from the top and the remainder falls below COLOR
    // as the bottom margin, so the spacing stays even if a block's height
    // changes later.
    const auto meterBlock  = kMeterHeight + kMeterButtonGap + kMeterButtonRow;
    const auto stackBlock  = kSwitchHeight * 2 + kSwitchGap;   // TELE/STRESS, and LINK/COLOR
    const auto content     = stackBlock + kKnobHeight + meterBlock + kKnobHeight + stackBlock;

    // Six divisions, not four: a margin above the first block and below the
    // last one as well as between them. A lone mode button used to sit 7 px
    // off the top of the panel with a 58 px band under it.
    const auto gap = juce::jmax (kSwitchGap, (area.getHeight() - content) / 6);

    area.removeFromTop (gap);

    // Mode above everything: it decides what COMP and MAKEUP mean. Stacked
    // rather than abreast, so naming both modes costs height instead of the
    // width this module has none of -- and it mirrors LINK/COLOR at the foot.
    {
        auto head = area.removeFromTop (stackBlock);
        teleButton  .setBounds (centredSwitch (head.removeFromTop (kSwitchHeight)));
        head.removeFromTop (kSwitchGap);
        stressButton.setBounds (centredSwitch (head.removeFromTop (kSwitchHeight)));
    }
    area.removeFromTop (gap);

    crush.setBounds (area.removeFromTop (kKnobHeight)
                         .withSizeKeepingCentre (kKnobWidth, kKnobHeight));
    area.removeFromTop (gap);

    // The VU meter over its IN/GR/OUT row -- GR in the middle because it is
    // the reading this module is actually for, and IN/OUT then read
    // left-to-right as signal flow either side of it. Frosty asked for this
    // order specifically, 2026-09-06.
    {
        auto block = area.removeFromTop (meterBlock);
        const auto meterWidth = juce::jmin (block.getWidth(), kMeterWidth);

        meter.setBounds (block.removeFromTop (kMeterHeight)
                              .withSizeKeepingCentre (meterWidth, kMeterHeight));
        block.removeFromTop (kMeterButtonGap);

        // The suite's switch height and the suite's gap. The width is this
        // row's one exception and the arithmetic is why: three switches at
        // Tokens::switchWidth with two gaps between them wants 226 px, and
        // this panel is 220 wide -- 200 inside its padding. So the row takes
        // the meter's width and splits it three ways, which lands each switch
        // at 58. Height and gap are what carried the visible drift anyway;
        // a switch 6 px short reads as a different control, a switch 12 px
        // narrow reads as a switch.
        auto buttons = block.withSizeKeepingCentre (meterWidth, kSwitchHeight);
        const auto buttonWidth = (meterWidth - kSwitchGap * 2) / 3;

        meterInButton.setBounds (buttons.removeFromLeft (buttonWidth));
        buttons.removeFromLeft (kSwitchGap);
        meterGrButton.setBounds (buttons.removeFromLeft (buttonWidth));
        buttons.removeFromLeft (kSwitchGap);
        meterOutButton.setBounds (buttons.removeFromLeft (buttonWidth));
    }
    area.removeFromTop (gap);

    level.setBounds (area.removeFromTop (kKnobHeight)
                         .withSizeKeepingCentre (kKnobWidth, kKnobHeight));
    area.removeFromTop (gap);

    // LINK and COLOR at the foot, under the MAKEUP caption. Placed from the
    // top like everything else, so what is left over stays underneath them.
    auto footer = area.removeFromTop (stackBlock);
    link.setBounds  (centredSwitch (footer.removeFromTop (kSwitchHeight)));
    footer.removeFromTop (kSwitchGap);
    color.setBounds (centredSwitch (footer.removeFromTop (kSwitchHeight)));
}

} // namespace bmo::opto
