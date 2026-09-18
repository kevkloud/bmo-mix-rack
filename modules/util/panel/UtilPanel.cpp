#include "UtilPanel.h"
#include "modules/util/params.h"

namespace bmo::util
{

namespace
{
    constexpr int kSwitchRow = 34;
    constexpr int kSwitchGap = ui::Tokens::switchGap;

    /** One row for all three knobs.

        VOLUME had 126 against the other two's 150 until 2026-09-17, so the
        control the module is named for was the smallest thing on the panel.
        The 24 px it takes came out of the plate under MONO, which was this
        panel's largest bare band at 46 px and is 22 now. Rendered at 150, 160
        and 170: 160 leaves 14 px under MONO and 170 puts MONO on the rule, and
        both were passed over -- a knob here cannot exceed 140 px anyway,
        because the panel is 160 wide with 10 px of padding, so the two larger
        rows buy a few pixels and spend the whole foot. Frosty's call. */
    constexpr int kKnobRow = 150;

    /** How far each caption comes up into the air under its knob's face.

        Matched to the two compressors, Frosty 2026-09-17: BMO Opto's MAKEUP
        sits 38 render px from face to caption ink and LTV Comp's 39, where
        this panel sat at 61. At 11 it measures 39 -- LTV's exactly, and half a
        design pixel off BMO Opto's. Rendered at 6, 11, 12 and 18 panel and
        rack; by 18 the name is up against PAN's L and R marks. */
    constexpr int kCaptionLift = 11;

    /** Air above and below the image section, and nothing more than that.

        These two were 62 and 0 until 0.2.3, and the 62 was an alignment
        constant: it existed to push this panel's lower rule down onto the line
        BMO EQ draws its own at, worked out by summing EQ's entire column. This
        panel now takes `ui::ModulePanel`'s output-section reservation like the
        other two, so the rule lands there structurally and these are free to be
        what they look like -- the same gap either side of the section between
        them. */
    constexpr int kGainToRule = 31;
    constexpr int kMonoToRule = 31;

    constexpr int kSwitchWidth  = ui::Tokens::switchWidth;
    constexpr int kSwitchHeight = ui::Tokens::switchHeight;
}

UtilPanel::UtilPanel (ui::ModuleContext ctx)
    : ModulePanel (std::move (ctx)),
      // Character, not utility. Utility is INPUT and OUTPUT -- the trim pair
      // every module carries, drawn the same pale blue everywhere so they read
      // as the same control wherever you meet them. This one has their
      // behaviour and a different job, so it takes its own colour.
      gain  (context.params.param (Index::gain),  "VOLUME", ui::Knob::Style::character, 0.5f,
             ui::tokens().utilGain),
      pan   (context.params.param (Index::pan),   "PAN",   ui::Knob::Style::character, 0.5f, context.def.accent),
      width (context.params.param (Index::width), "WIDTH", ui::Knob::Style::character, 0.5f, context.def.accent),
      phaseL (context.params.param (Index::phaseL), ui::BmoLookAndFeel::phaseGlyph() + " L", ui::tokens().polarity),
      phaseR (context.params.param (Index::phaseR), ui::BmoLookAndFeel::phaseGlyph() + " R", ui::tokens().polarity),
      mono   (context.params.param (Index::mono),   "MONO", context.def.accent)
{
    for (auto* c : std::initializer_list<juce::Component*> {
             &gain, &pan, &width, &phaseL, &phaseR, &mono })
        addAndMakeVisible (c);

    // No setKnobSide here. Tokens::gainKnobSide caps INPUT and OUTPUT at the
    // one size BMO EQ's column can afford; this knob is not one of those and
    // takes the room its own row gives it.

    // PAN's ends are two directions, not two amounts: hard left is not less
    // than hard right, and the minus and plus this knob wore said it was. The
    // same marks BMO Dimension's TURN and TILT take, for the same reason.
    pan.setEndMarks (ui::Knob::EndMarks::leftRight);

    // VOLUME and PAN print what they are set to -- Frosty, 2026-09-17. These
    // two are the ones a number answers: "how much gain" and "how far over",
    // both questions with an amount for an answer that the caption cannot give.
    // PAN's reads C, L 50, R 50.
    gain.setShowsValue (true);
    pan .setShowsValue (true);

    // WIDTH reserves the same line and prints nothing on it, the way BMO
    // Dimension's BLOOM does: a value line lifts a knob and its caption by its
    // own height, so without this WIDTH's name would sit below the other two
    // and the column would stop reading as one. No number, because width is a
    // less-and-more control and its percentage names nothing a listener has a
    // word for.
    width.setShowsValue (true);
    width.setValueFormat ([] (const juce::String&) { return juce::String(); });

    // The names come up off the foot of each knob's box and into the air under
    // its face, so they sit the same distance from it as the two compressors'
    // do. See kCaptionLift.
    for (auto* k : { &gain, &pan, &width })
        k->setCaptionLift (kCaptionLift);

    // WIDTH keeps its minus and plus. Frosty, 2026-09-17: Width runs 0..200 and
    // rests at 100, so it reduces and increases around its rest position, which
    // is what lessMore is for -- and the rest dot on the track is already the
    // mark that says where that is.

    // Polarity is white in every module; its label is what says which module.
    for (auto* p : { &phaseL, &phaseR })
        p->setActiveInkFrom (context.def.accent);

    lastMonoWasOn = context.params.param (Index::mono).getValue() > 0.5f;
    width.setKnobEnabled (! lastMonoWasOn);

    startTimerHz (15);
}

UtilPanel::~UtilPanel() { stopTimer(); }

void UtilPanel::timerCallback()
{
    // MONO sums to (L+R)/2 *before* the mid/side stage, so while it is on the
    // side signal is zero and WIDTH has nothing left to scale: turning it does
    // nothing at all. Dim it, the way PlainKnob::setKnobEnabled was written for
    // and BMO DEQ's gain knob uses on a cut filter.
    //
    // MONO itself is never dimmed. It is the switch that put WIDTH to sleep and
    // it is the way back out, and DEQ's pass settled that dimming the way in
    // reads as a door locked rather than as the door.
    //
    // **The dim is the full shipped one, caption and all**, which on the pale
    // plate takes the caption from 1.72:1 to 1.25:1. A ladder was rendered that
    // held the caption at full strength and dimmed only the face -- it measures
    // better and Frosty did not take it, 2026-09-17: a knob whose name still
    // reads at full strength while its face has gone pale reads as a knob that
    // has broken, and the whole control fading says on purpose. Do not "fix"
    // this to the ratio.
    const auto monoOn = context.params.param (Index::mono).getValue() > 0.5f;

    if (monoOn != lastMonoWasOn)
    {
        lastMonoWasOn = monoOn;
        width.setKnobEnabled (! monoOn);
    }
}

void UtilPanel::resized()
{
    clearRules();
    auto area = getLocalBounds().reduced (kPad, 4);

    const auto rule = [&] (const juce::String& text)
    {
        addRule (area.removeFromTop (kRuleRow), text);
    };

    // Three sections instead of five.
    //
    // PAN and WIDTH each had a rule of their own carrying the same word the
    // knob's caption said ten pixels below it. They are one idea -- what the
    // module does to the stereo image -- so they are one section, and MONO
    // belongs in it: it is the far end of the same control WIDTH is the middle
    // of. POLARITY loses its rule because two switches with a phase glyph on
    // them do not need to be told apart from anything.
    //
    // The meter goes. Its rule stays, and the polarity pair moves into the
    // space under it, which is the one place on this panel that was doing
    // nothing.
    const auto centred = [] (juce::Rectangle<int> row)
    {
        return row.withSizeKeepingCentre (kSwitchWidth, kSwitchHeight);
    };

    // No input section: VOLUME is what this module does, not a trim either side
    // of it. No output section either -- there is no output stage and the meter
    // is gone. But the *reservation* is taken, because that is what puts this
    // panel's lower rule on the same line as the other two, and the polarity
    // pair then centres in the body the section would have used.
    const auto out = takeOutputSection (area);

    // The knob and its name centre in the section as one object, rather than
    // sitting at the top of it with the whole gap underneath. The section is
    // everything above the first rule, so the 31 px splits either side.
    {
        auto section = area.removeFromTop (kKnobRow + kGainToRule);
        gain.setBounds (section.withSizeKeepingCentre (section.getWidth(), kKnobRow));
    }

    // A bare rule. Only BMO EQ names its sections -- see modules/AGENTS.md.
    rule ({});
    pan.setBounds   (area.removeFromTop (kKnobRow));
    width.setBounds (area.removeFromTop (kKnobRow));

    // A knob pins its caption to the foot of its row, so MONO needs a gap put
    // in by hand or it sits against the word WIDTH and reads as a second line
    // of it.
    area.removeFromTop (kSwitchGap * 2);
    mono.setBounds  (centred (area.removeFromTop (kSwitchRow)));

    area.removeFromTop (kMonoToRule);
    addRule (out.rule, {});

    // The pair centres in the body the output section would have used, so the
    // rule above them reads as the top of a section rather than as a line ruled
    // under the one before it.
    auto polarity = out.body.withSizeKeepingCentre (kSwitchWidth, kSwitchRow * 2 + kSwitchGap);

    phaseL.setBounds (centred (polarity.removeFromTop (kSwitchRow)));
    polarity.removeFromTop (kSwitchGap);
    phaseR.setBounds (centred (polarity.removeFromTop (kSwitchRow)));
}

} // namespace bmo::util
