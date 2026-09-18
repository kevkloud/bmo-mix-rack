#include "SatPanel.h"
#include "modules/sat/params.h"

namespace bmo::sat
{

namespace
{
    /** The middle. Both ends of this panel are ui::ModulePanel's sections.

        This module took both, which left it 34 px more room than it had, spread
        across the two rows in proportion to what they already were -- 210 and
        130 of a 340 px pair, so 18 and 11. Drive keeps the larger share because
        it had it. The knobs grow with their rows: Drive's face goes from 188 to
        206 px and Tone and Mix from 108 to 119.

        Whatever is left after that is *centred* rather than left at the foot.
        It was all at the foot until 0.2.3, which put 5 px between Drive's name
        and the rule under it and 75 between Mix's name and the rule under that
        -- the whole middle jammed against the top of its span with the slack
        pooled underneath. There is no constant for the span: the output section
        comes off the foot of the content area first, so the middle is by
        definition what remains and re-centres itself if either row changes. */
    constexpr int kDriveRow = 228;   // the one control that gets room
    constexpr int kPairRow  = 141;   // Tone and Mix, side by side

    constexpr int kRule      = 20;

    /** DRIVE's caption, in points, against the suite's 15.

        Drive is the plugin and its name was the same size as the two knobs you
        reach for after it. Rendered at 20, 24, 28 and 32; Frosty took 28,
        2026-09-17. At 28 the word is 144 design px of the 240 the panel has, so
        it is nowhere near the width that would shrink it to fit.

        A caption is charged to its knob's row -- captionRow() comes off the top
        of the cell before the knob squares itself in what is left -- so the row
        below grows by what the larger name takes and the face keeps its size.
        Without that, DRIVE's face would have paid for its own caption. */
    constexpr int kDriveCaption = 28;

    /** How far each character knob's caption comes up into the air under its own
        face, in design px, as ui::PlainKnob::setCaptionLift.

        The suite sits at 30 render px from face to caption ink: BMO Util, BMO
        Opto's MAKEUP and LTV Comp all measure exactly 30. This panel was at 74
        on DRIVE and 68 on the pair, the loosest in the rack. INPUT and OUTPUT
        were already at 30 and are not touched.

        The two numbers differ because the lift is applied under captions of two
        different sizes, and a taller caption sits lower in its own box. Both
        land on 30.

        A lift is measured against the box it is applied to, so it does not
        survive a change of row: DRIVE wanted 25 at the full row and wants 20 at
        the trimmed one, where that same 25 measured 20 px rather than 30.
        Re-measure rather than re-derive if a row or a caption size changes. */
    constexpr int kDriveLift = 20;
    constexpr int kPairLift  = 19;

    /** What DRIVE's row gives up so the oversampling section has air, in design
        px. Frosty's call, 2026-09-17, rendered against keeping the row whole.

        The section is paid for out of the middle's spare 55 px and costs no
        control anything. What it cannot buy is *slack*: 7 px left over is not
        enough for a section to centre its ink in, so DRIVE sat 35 px under the
        rule above it against 47 over the one below, and the two switch rows had
        12 px between them. Trimmed, that reads 43 and 47, and the rows are 18
        apart. DRIVE's face pays: 136 design px to 121. */
    constexpr int kDriveTrim = 24;

    /** What each section's content is nudged down by to centre its *ink* rather
        than its boxes, in design px. Frosty's call between the two, 2026-09-17.

        A knob's box carries more air above its face than its lifted caption
        leaves under it, so a section holding centred boxes still reads high:
        centred that way, DRIVE sat 44 px from the rule above and 61 from the one
        below, and the pair 44 and 49. Nudged, they measure 52/53 and 46/47, and
        the panel's largest bare band falls from 66 px to 53.

        Measured constants, so they are only true for the sizes above. */
    constexpr int kDriveNudge = 8;
    constexpr int kPairNudge  = 2;

    /** The oversampling section: a rule with its name, and the three switches
        under it. 20 + 28 of the 55 px the middle had spare, so no control on
        this panel gives up anything for it.

        Rendered against a dial with its positions printed round it -- BMO EQ's
        LO-CUT pattern -- and against a third knob in the pair row. Frosty took
        the switches, 2026-09-17: the dial cost a 76 px row and the knob cost
        TONE and MIX a third of their faces. */
    constexpr int kOsSwitchRow = ui::Tokens::switchHeight;

    constexpr int kSwitchWidth  = ui::Tokens::switchWidth;
    constexpr int kSwitchHeight = ui::Tokens::switchHeight;
}

SatPanel::SatPanel (ui::ModuleContext ctx)
    : ModulePanel (std::move (ctx)),
      inputGain   (context.params.param (Index::inputGain),   "INPUT"),
      drive       (context.params.param (Index::drive),       "DRIVE", ui::Knob::Style::character, 0.66f, context.def.accent),
      tone        (context.params.param (Index::tone),        "TONE",  ui::Knob::Style::character, 0.46f, context.def.accent),
      mix         (context.params.param (Index::mix),         "MIX",   ui::Knob::Style::character, 0.46f, context.def.accent),
      outputLevel (context.params.param (Index::outputLevel), "OUTPUT"),
      satIn    (context.params.param (Index::satIn),    "SAT", context.def.accent),
      phase    (context.params.param (Index::phase),    ui::BmoLookAndFeel::phaseGlyph(), ui::tokens().polarity),
      autoGain (context.params.param (Index::autoGain), "AUTO", ui::tokens().switchAlt),
      os2x ("2x"), os4x ("4x"), osHq ("8x")

{
    for (auto* c : std::initializer_list<juce::Component*> {
             &inputGain, &drive, &tone, &mix, &satIn, &phase, &autoGain, &outputLevel })
        addAndMakeVisible (c);

    // Oversampling is anything-else by the table in modules/AGENTS.md, so the
    // three light in switchAlt, the same as AUTO under them. The look and feel
    // derives each label from the fill it is drawing.
    for (auto* b : { &os2x, &os4x, &osHq })
    {
        b->setClickingTogglesState (false);
        b->setColour (juce::ToggleButton::tickColourId, ui::tokens().switchAlt);
        addAndMakeVisible (b);
    }

    osAttachment = std::make_unique<juce::ParameterAttachment> (
        context.params.param (Index::oversampling),
        [this] (float value) { showOversampling (juce::roundToInt (value)); });

    // A click sets the parameter and the parameter lights the switches, so a
    // click and host automation cannot disagree. Clicking the lit one is what
    // reaches Off, since Off is the position with no switch of its own.
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

    // Lights whatever the parameter already says, which is how a render or a
    // reopened editor comes up in the state it was left in.
    osAttachment->sendInitialUpdate();

    for (auto* k : { &inputGain, &outputLevel })
        styleTrimKnob (*k);

    // Drive is what this module is, and now its name says so. See kDriveCaption.
    drive.setCaptionSize ((float) kDriveCaption);

    // The names come up off the foot of their knobs' boxes and into the air
    // under each face, so they sit the same distance from it as BMO Util's and
    // the two compressors' do. See kDriveLift.
    drive.setCaptionLift (kDriveLift);

    for (auto* k : { &tone, &mix })
        k->setCaptionLift (kPairLift);

    // Polarity is white in every module; its label is what says which module.
    phase.setActiveInkFrom (context.def.accent);
}

void SatPanel::showOversampling (int choice)
{
    os2x.setToggleState (choice == 1, juce::dontSendNotification);
    os4x.setToggleState (choice == 2, juce::dontSendNotification);
    osHq.setToggleState (choice == 3, juce::dontSendNotification);
}

void SatPanel::resized()
{
    clearRules();
    // 4, not 6: the same content inset BMO EQ and Util use, so all three
    // panels measure from the same origin and the matched rows below actually
    // land where the arithmetic says.
    auto area = getLocalBounds().reduced (kPad, 4);

    const auto rule = [&] (const juce::String& text)
    {
        addRule (area.removeFromTop (kRule), text);
    };

    // Both sections, output first off the foot. The 6 px of air that used to
    // sit between the input knob and its rule is gone with them: the section
    // has none, and the rule row's own half-height is the clearance.
    const auto out = takeOutputSection (area);
    const auto in  = takeInputSection (area);

    inputGain.setBounds (in.knob);
    addRule (in.rule, {});

    // The row carries the taller caption, so the face does not pay for it.
    // See kDriveCaption.
    int driveRow = kDriveRow - kDriveTrim
                       + juce::roundToInt ((float) kDriveCaption * 1.2f)
                       - juce::roundToInt (15.0f * 1.2f);   // the caption this row was drawn for

    // What is left is the middle, and it is two sections with a rule between
    // them. Each centres its own content in its own span rather than the pair
    // of them centring as one block -- Frosty, 2026-09-17. Centred as one, the
    // slack pooled above DRIVE and under MIX and neither section sat in the
    // middle of anything.
    const int slack      = area.getHeight()
                         - (driveRow + kRule + kPairRow + kRule + kOsSwitchRow);
    const int driveSpan  = slack / 2;
    const int pairSpan   = slack - driveSpan;
    // Clamped to the air the section actually has. The nudges were measured
    // when the middle had 55 px spare and the oversampling section spends 48 of
    // them, so an unclamped nudge would push a section past the rule under it.
    const int driveNudge = juce::jlimit (0, driveSpan - driveSpan / 2, kDriveNudge);
    const int pairNudge  = juce::jlimit (0, pairSpan  - pairSpan  / 2, kPairNudge);

    // Each section centres what you can see rather than what the layout holds.
    // See kDriveNudge.
    area.removeFromTop (driveSpan / 2 + driveNudge);
    drive.setBounds (area.removeFromTop (driveRow));
    area.removeFromTop (driveSpan - driveSpan / 2 - driveNudge);

    // Tone and Mix share a row: neither is the reason you reached for this,
    // and side by side they read as the two things you adjust after the fact.
    rule ({});

    area.removeFromTop (pairSpan / 2 + pairNudge);

    {
        auto pair = area.removeFromTop (kPairRow);
        const auto half = pair.getWidth() / 2;
        tone.setBounds (pair.removeFromLeft (half));
        mix.setBounds (pair);
    }

    // The three sit side by side on one row, the same width and gap as the
    // switches under the output rule, so the two rows read as the same kind of
    // control rather than as two inventions.
    rule ("OVERSAMPLING");

    {
        constexpr int gap = ui::Tokens::switchGap;

        auto group = area.removeFromTop (kOsSwitchRow)
                         .withSizeKeepingCentre (kSwitchWidth * 3 + gap * 2, kSwitchHeight);

        os2x.setBounds (group.removeFromLeft (kSwitchWidth));
        group.removeFromLeft (gap);
        os4x.setBounds (group.removeFromLeft (kSwitchWidth));
        group.removeFromLeft (gap);
        osHq.setBounds (group);
    }

    addRule (out.rule, {});

    {
        constexpr int gap = ui::Tokens::switchGap;

        auto group = out.switches.withSizeKeepingCentre (kSwitchWidth * 3 + gap * 2, kSwitchHeight);
        satIn.setBounds (group.removeFromLeft (kSwitchWidth));
        group.removeFromLeft (gap);
        phase.setBounds (group.removeFromLeft (kSwitchWidth));
        group.removeFromLeft (gap);
        autoGain.setBounds (group);
    }

    // The output meter used to share this row, out at the right margin. It is
    // gone, so the knob has the row to itself.
    outputLevel.setBounds (out.knob);

}

} // namespace bmo::sat
