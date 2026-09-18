#include "DimPanel.h"
#include "modules/dim/params.h"

namespace bmo::dim
{

namespace
{
    // DIMENSION, the hero. 148 px against the pairs' 64 -- Frosty, 2026-09-17,
    // from a ladder of 92 (Opto's size, what it was), 132, 148 and 156. 156
    // began to crowd the WIDTH legend above it.
    //
    // Its box is the knob plus 30 px, where it had been the knob plus 58: the
    // caption takes about 22, and the other 36 of the old allowance was bare
    // plate under it. Growing the knob alone left that band where it was.
    constexpr int kBigKnobSide   = 148;
    constexpr int kBigKnobHeight = kBigKnobSide + 30;

    // Between GENERATE and the pair under it. The suite's switch gap rather
    // than the panel's rhythm, so SOURCE reads as one block -- Frosty,
    // 2026-09-17, who asked for that section to be tighter.
    constexpr int kSourceGap = ui::Tokens::switchGap;

    // Everything else is paired two to a row. 64 is between BMO EQ's 56 and
    // Opto's 92, and it is what the column can afford. Sized when there were
    // ten controls here; there are eight since RATE and DEPTH lost theirs, so
    // the pairs could grow -- deliberately not done, because 64 is the size
    // the rest of the suite's paired knobs use and matching them across
    // modules is worth more than the spare pixels.
    constexpr int kPairKnobSide   = 64;
    constexpr int kPairKnobHeight = 104;

    // The line a knob prints its value on: PlainKnob::valueRow, 11 pt at 1.2
    // plus one. The BLOOM/BELOW row is this much taller, because BELOW prints
    // its frequency and BLOOM keeps the same line blank to stay level with it.
    constexpr int kValueRow = 14;

    constexpr int kSwitchWidth  = ui::Tokens::switchWidth;
    constexpr int kSwitchHeight = ui::Tokens::switchHeight;
    constexpr int kSwitchGap    = ui::Tokens::switchGap;

    // Captions are single short words on purpose. PlainKnob draws its caption
    // inside its own width, and half of a 200 px row is 100 px: at 15 pt
    // "MODULATE" is 126 px, and a clipped caption is BMO Opto's MAKEUP bug
    // over again. ui_layout_tests asserts the overflow rather than trusting
    // this comment.

    // The bracket under each pair: how far above the pair's foot its line
    // sits, how tall its turned-up ends are, how far in from the row's sides
    // it starts, and its stroke.
    constexpr float kBracketRise   = 6.0f;
    constexpr float kBracketEnd    = 7.0f;
    constexpr float kBracketInset  = 8.0f;
    constexpr float kBracketWeight = 2.0f;
}

DimPanel::DimPanel (ui::ModuleContext ctx)
    : ModulePanel (std::move (ctx)),
      // Captions and host names agree, so an automation lane is called what
      // the panel calls the knob -- Frosty, 2026-09-17. The IDs underneath
      // (`shuffle`, `diffuse`, `detune_on`, ...) are the permanent part and do
      // not follow the words.
      //
      // The pairs alliterate by Frosty's choice: DETUNE and DRIFT, BLOOM and
      // BELOW, TURN and TILT. The hero knob is DIMENSION, after the product.
      width       (context.params.param (Index::width),       "DIMENSION",
                   ui::Knob::Style::character, 0.62f, context.def.accent),
      shuffle     (context.params.param (Index::shuffle),     "BLOOM",
                   ui::Knob::Style::character, 0.62f, context.def.accent),
      shuffleFreq (context.params.param (Index::shuffleFreq), "BELOW",
                   ui::Knob::Style::character, 0.62f, context.def.accent),
      cents       (context.params.param (Index::detune),      "DETUNE",
                   ui::Knob::Style::character, 0.62f, context.def.accent),
      diffuse     (context.params.param (Index::diffuse),     "DRIFT",
                   ui::Knob::Style::character, 0.62f, context.def.accent),
      rotation    (context.params.param (Index::rotation),    "TURN",
                   ui::Knob::Style::character, 0.62f, context.def.accent),
      asymmetry   (context.params.param (Index::asymmetry),   "TILT",
                   ui::Knob::Style::character, 0.62f, context.def.accent),

      // Not a bypass, not mono, not polarity -- so `switchAlt`, per the table
      // in modules/AGENTS.md. GENERATE rather than DETUNE: the switch turns on
      // the one stage that makes width from nothing, and a mono source needs
      // it before anything below does anything, which "detune" did not say.
      detuneOn (context.params.param (Index::detuneOn), "GENERATE", ui::tokens().switchAlt)
{
    width.setKnobSide (kBigKnobSide);

    for (auto* k : { &shuffle, &shuffleFreq, &cents, &diffuse,
                     &rotation, &asymmetry })
        k->setKnobSide (kPairKnobSide);

    // BELOW prints its frequency, the way BMO DEQ's knobs print theirs --
    // Frosty, 2026-09-17. The suite's knobs otherwise say less and more rather
    // than how much, but this one is a crossover, and "below what" is the
    // question its own caption raises.
    shuffleFreq.setShowsValue (true);

    // BLOOM reserves the same line and prints nothing on it. A value line
    // lifts a knob and its caption by its own height, so without this BELOW sat
    // 14 px above BLOOM and the pair stopped reading as one row.
    shuffle.setShowsValue (true);
    shuffle.setValueFormat ([] (const juce::String&) { return juce::String(); });

    // The bottom pair marks its ends L and R, not minus and plus: each moves
    // the image one way or the other, and neither end is more than the other.
    // Both, so the row reads as one pair -- Frosty, 2026-09-16.
    //
    // The R has to be where the image goes, and DimDspTests asserts that for
    // both. ROTATE's + was confirmed rightward by ear on bc89293. ASYM's +
    // leaned *left* until 2026-09-16, and its DSP sign was flipped rather than
    // printing the letters backwards -- see asymCoeff.
    for (auto* k : { &rotation, &asymmetry })
        k->setEndMarks (ui::Knob::EndMarks::leftRight);

    for (auto* c : std::initializer_list<juce::Component*> {
             &detuneOn, &cents, &diffuse, &width,
             &shuffle, &shuffleFreq, &rotation, &asymmetry })
        addAndMakeVisible (c);
}

void DimPanel::resized()
{
    auto area = getLocalBounds().reduced (kPad, 4);
    clearRules();

    // Two legends, set in the gaps the rhythm below already leaves, so no
    // control moves for them. SOURCE over what makes width from a mono
    // source, WIDTH over what shapes the width that exists -- Frosty,
    // 2026-09-17. MONO and STEREO were rendered first and read well, but DRIFT
    // works on a stereo source too, and a MONO legend beside Util's MONO switch
    // reads as a mode.
    //
    // This panel had no rules at all until then, on the argument that it is
    // one idea. It is two, once the switch says so: make width, then shape it.
    const auto legendIn = [this] (juce::Rectangle<int> gapRow, const char* text)
    {
        addRule (gapRow.withSizeKeepingCentre (gapRow.getWidth(), kRuleRow), text);
    };

    // Two knobs abreast, each centred in its half. The bracket painted under
    // each pair is what says they belong together.
    const auto pair = [] (juce::Rectangle<int> row, ui::PlainKnob& a, ui::PlainKnob& b)
    {
        a.setBounds (row.removeFromLeft (row.getWidth() / 2));
        b.setBounds (row);
    };

    // Opto's rhythm: every block placed from the top on one derived gap, with a
    // margin above the first and below the last, so the spacing stays even if a
    // block's height changes later. Five blocks and five derived gaps -- the
    // sixth, inside SOURCE, is the fixed kSourceGap.
    const auto content = kSwitchHeight + kSourceGap + kPairKnobHeight * 3 + kValueRow + kBigKnobHeight;
    const auto gap     = juce::jmax (kSwitchGap, (area.getHeight() - content) / 5);

    legendIn (area.removeFromTop (gap), "SOURCE");

    // -- Source ----------------------------------------------------------
    // The switch first, centred over its pair -- Frosty, 2026-09-17, after a
    // render with it stacked on DETUNE alone.
    detuneOn.setBounds (area.removeFromTop (kSwitchHeight)
                            .withSizeKeepingCentre (kSwitchWidth, kSwitchHeight));
    area.removeFromTop (kSourceGap);

    pairBoxes[0] = area.removeFromTop (kPairKnobHeight);
    pair (pairBoxes[0], cents, diffuse);
    legendIn (area.removeFromTop (gap), "WIDTH");

    // -- Width -----------------------------------------------------------
    width.setBounds (area.removeFromTop (kBigKnobHeight));
    area.removeFromTop (gap);

    pairBoxes[1] = area.removeFromTop (kPairKnobHeight + kValueRow);
    pair (pairBoxes[1], shuffle, shuffleFreq);
    area.removeFromTop (gap);

    pairBoxes[2] = area.removeFromTop (kPairKnobHeight);
    pair (pairBoxes[2], rotation, asymmetry);
}

void DimPanel::paintPanel (juce::Graphics& g)
{
    // A bracket under each pair: a line with its ends turned up. Frosty,
    // 2026-09-17, from renders of no grouping, a well, a bracket and both. The
    // well read best on the dark plate and cost the most on the pale one --
    // lavender on the pale well is 1.37:1 against 1.73 on the plate -- and a
    // bracket sits under no caption at all.
    //
    // The raw accent at the 0.55 the dotted tracks use, so the bracket is the
    // knobs' own mark rather than a rule: a rule divides, and this joins.
    g.setColour (context.def.accent.withAlpha (0.55f));

    for (const auto& box : pairBoxes)
    {
        if (box.isEmpty())
            continue;

        const auto y  = (float) box.getBottom() - kBracketRise;
        const auto x0 = (float) box.getX() + kBracketInset;
        const auto x1 = (float) box.getRight() - kBracketInset;

        g.fillRect (juce::Rectangle<float> (x0, y - kBracketWeight * 0.5f, x1 - x0, kBracketWeight));
        g.fillRect (juce::Rectangle<float> (x0, y - kBracketEnd, kBracketWeight, kBracketEnd));
        g.fillRect (juce::Rectangle<float> (x1 - kBracketWeight, y - kBracketEnd, kBracketWeight, kBracketEnd));
    }
}

} // namespace bmo::dim
