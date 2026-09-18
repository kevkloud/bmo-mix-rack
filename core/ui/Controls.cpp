#include "Controls.h"
#include "ModulePanel.h"

namespace bmo::ui
{

juce::String compactFrequency (const juce::String& text)
{
    if (text.equalsIgnoreCase ("off"))
        return "OFF";

    if (text.containsIgnoreCase ("kHz"))
    {
        const auto number = text.upToFirstOccurrenceOf (" ", false, true).trim();

        if (number.contains ("."))
            return number.upToFirstOccurrenceOf (".", false, false) + "k"
                 + number.fromFirstOccurrenceOf (".", false, false);

        return number + "k";
    }

    if (text.containsIgnoreCase ("Hz"))
        return text.upToFirstOccurrenceOf (" ", false, true).trim();

    return text;
}

//==============================================================================
PlainKnob::PlainKnob (juce::RangedAudioParameter& param, const juce::String& captionText,
                      Knob::Style style, float faceScale, juce::Colour accent, juce::Colour captionColourIn)
    : caption (captionText), captionColour (captionColourIn), accentColour (accent), parameter (param)
{
    // A component name, so a layout test can find this knob by the caption a
    // reader sees. JUCE hands it to the accessibility layer as well.
    setName (captionText);

    knob.setStyle (style);
    knob.setAccent (accent);
    knob.setFaceScale (faceScale);
    addAndMakeVisible (knob);

    attachment = std::make_unique<juce::SliderParameterAttachment> (parameter, knob);

    // Only a knob that prints its value needs to hear it move; the attachment
    // keeps its own listener, so this does not take anything from it.
    knob.onValueChange = [this] { if (showsValue) repaint (valueBox()); };
}

juce::Rectangle<int> PlainKnob::captionBox() const
{
    return { 0, knob.getBottom() - captionLift, getWidth(),
             captionRow() - 4 - (showsValue ? valueRow() : 0) };
}

juce::Rectangle<int> PlainKnob::valueBox() const
{
    if (! showsValue)
        return {};

    const auto name = captionBox();
    return { 0, name.getBottom(), getWidth(), valueRow() };
}

float PlainKnob::captionOverflow() const
{
    auto overflow = juce::GlyphArrangement::getStringWidth (captionFont (captionSize), caption)
                      - (float) captionBox().getWidth();

    // The value too, at the widest it can be rather than at whatever it
    // happens to read now: both ends of the range and the default, which
    // between them are the long strings ("20.0 kHz", "-24.0 dB").
    if (showsValue)
        for (const auto n : { 0.0f, 1.0f, parameter.getDefaultValue() })
            overflow = juce::jmax (overflow, juce::GlyphArrangement::getStringWidth (captionFont (kValueSize), valueText (parameter.getText (n, 0)))
                                               - (float) valueBox().getWidth());

    return overflow;
}

void PlainKnob::setShowsValue (bool shouldShow)
{
    showsValue = shouldShow;
    resized();
    repaint();
}

void PlainKnob::setValueFormat (std::function<juce::String (const juce::String&)> format)
{
    valueFormat = std::move (format);
    repaint();
}

juce::String PlainKnob::valueText (const juce::String& hostText) const
{
    return valueFormat ? valueFormat (hostText) : hostText;
}

void PlainKnob::paint (juce::Graphics& g)
{
    // The name sits under the knob, in the caption face. Derived here rather
    // than cached in the constructor so that editing the theme file recolours
    // an open panel -- the editors repaint on a theme change but do not
    // rebuild their controls.
    //
    // A caption is a legible step of whichever colour system its knob belongs
    // to, not of the module's accent regardless. A character knob -- drive,
    // tone, a band's gain -- is drawn in the module's colour and its caption
    // follows it. A utility knob is deliberately the same pale blue in every
    // module, with a blue track and blue plus and minus, so INPUT and OUTPUT
    // read as the same control wherever they are; setting those captions in
    // the accent put pink text on BMO EQ's blue cap and orange on the
    // Saturator's.
    // panelAccentFor: a product line may supply its own ink in place of the
    // module's accent. LTV does, because its caps are fixed and the accent no
    // longer reaches them, so the caption is the only thing left carrying the
    // knob's colour. Nothing changes for a BMO panel.
    const auto system = knob.getStyle() == Knob::Style::character
                            ? panelAccentFor (knob, accentColour)
                            : (knob.getUtilityTint().isTransparent() ? tokens().track
                                                                     : knob.getUtilityTint());

    // The colour system as it stands, not stepped for contrast. A caption is
    // the larger of a panel's two labels -- 15 pt against a section legend's
    // 13 -- and it names a knob you are already looking at, where the legend
    // is what you navigate by. So the raw colour goes here and the legible
    // step goes on the legend; see ModulePanel::drawRuleLegend.
    //
    // The two swapped in 0.2.3 and the swap costs contrast here: on the pale
    // plate a caption goes from 4.57-4.69:1 to 1.72-2.00:1, and on the dark
    // one from 9.07 to 5.87. Frosty's call, taken on a render with those
    // numbers in front of him. Do not "fix" it.
    const auto ink = captionColour.isTransparent() ? system : captionColour;


    // Hung off the knob's own bottom edge, not the component's. The two are
    // the same thing for a knob that fills its cell, which every knob in the
    // suite did until gain knobs were capped at one shared size -- a capped
    // knob centres in a taller area, and a caption pinned to the foot of the
    // cell would drift away from it by half the difference, and drift further
    // every time the type got smaller.
    const auto box = captionBox();

    drawLabel (g, caption, box.toFloat(),
               juce::Justification::centred, captionFont (captionSize),
               knob.isEnabled() ? ink : ink.withAlpha (0.4f));

    // The value in the caption face, a step down and in the secondary ink: it
    // is read after the name, and it should not compete with it.
    if (showsValue)
        drawLabel (g, valueText (parameter.getCurrentValueAsText()), valueBox().toFloat(),
                   juce::Justification::centredTop, captionFont (kValueSize),
                   knob.isEnabled() ? tokens().text2 : tokens().text2.withAlpha (0.4f));
}

void PlainKnob::resized()
{
    // Square and centred, capped at knobSide. jmin(width, height) is what the
    // rotary's radius comes from, so squaring an already-narrower-than-tall
    // area leaves the drawn knob exactly where it was -- what it buys is the
    // freedom to make the component wider than the knob, so a long caption
    // has somewhere to go. See setKnobSide().
    const auto area = getLocalBounds().withTrimmedBottom (captionRow());
    const auto side = juce::jmin (area.getWidth(), area.getHeight(), knobSide);

    knob.setBounds (area.withSizeKeepingCentre (side, side));
}

void PlainKnob::setKnobSide (int maxSide)
{
    knobSide = maxSide;
    resized();
}

void PlainKnob::setCaptionSize (float points)
{
    captionSize = points;
    resized();
    repaint();
}

void PlainKnob::setCaptionLift (int pixels)
{
    captionLift = pixels;
    repaint();
}

void PlainKnob::setKnobEnabled (bool shouldBeEnabled)
{
    knob.setEnabled (shouldBeEnabled);
    repaint();
}

void PlainKnob::setAccent (juce::Colour accent)
{
    accentColour = accent;
    knob.setAccent (accent);
    repaint();
}

void PlainKnob::setUtilityTint (juce::Colour tint)
{
    knob.setUtilityTint (tint);
    repaint();
}

void PlainKnob::setRestMark (bool b)
{
    knob.setRestMark (b);
    repaint();
}

void PlainKnob::setEndMarks (Knob::EndMarks m)
{
    knob.setEndMarks (m);
    repaint();
}

//==============================================================================
ConcentricBand::ConcentricBand (juce::RangedAudioParameter& selector, const ParamSpec& selectorSpec,
                                juce::RangedAudioParameter* gain, juce::Colour accent,
                                bool outsetFan)
    : accentColour (accent), hasCentre (gain != nullptr)
{
    ring.setDetents (selectorSpec.numChoices());
    ring.setSliderSnapsToMousePosition (false);

    // The legend follows the ring's sweep -- paint() puts label i at the angle
    // the pointer takes for value i -- so the sweep is what decides where the
    // legend sits, and the two kinds of control want different sweeps.
    {
        const auto r = ring.getRotaryParameters();

        if (gain != nullptr)
        {
            // A band's frequencies occupy the left half of the dial and its
            // gain the right, so the two controls on it are told apart by
            // which side of the knob they are on.
            //
            // Before 0.2.3 they shared the whole circle at different radii,
            // and the collisions that came of it were fixed one at a time:
            // the gain's rest dot and the selected frequency both want to
            // point straight up, and on a band with an odd number of
            // positions they landed a pixel and a half apart and read as one
            // mark. BMO EQ's high shelf is three positions with 12 kHz in the
            // middle, and 12 kHz is the default, so that was the panel's
            // opening state.
            //
            // The middle of the available frequencies sits at 9 o'clock.
            // Higher values radiate clockwise from it, toward 12; lower ones
            // counter-clockwise, toward 6. An even count straddles 9 o'clock
            // rather than landing on it, which is what "the middle of the
            // frequencies" means when there is no middle frequency.
            //
            // The ends stop short of 12 and 6, or run just past them, and the
            // direction alternates down the panel. Left to itself every band
            // puts a label at dead-centre top and dead-centre bottom, so the
            // high shelf's lowest and the mid bell's highest would sit one
            // above the other on the same x with only a rule between them --
            // a column of numbers down the middle of the panel. Alternating
            // the nudge breaks it.
            const auto pi = juce::MathConstants<float>::pi;
            const auto nudge = outsetFan ? -kFanNudge : kFanNudge;

            ring.setRotaryParameters (pi + nudge, pi * 2.0f - nudge, r.stopAtEnd);
        }
        else
        {
            // A filter. Its positions sit on a step sized so that exactly one
            // is left over: five frequencies plus one gap, at sixty degrees
            // each. Nothing is drawn in the empty one.
            //
            // The fan is centred on 12 o'clock, so the middle position sits
            // straight up and the rest radiate from it -- lower counter-
            // clockwise, higher clockwise. BMO EQ's low cut is Off, 45, 70,
            // 160, 360, which puts 70 at the top with a frequency either side
            // of it and Off at the far anti-clockwise end.
            //
            // It used to start from the sweep's own start angle and run
            // clockwise from there, which left the whole fan rotated off
            // vertical for no reason anyone could state. Centred, the empty
            // slot falls symmetrically about 6 o'clock, and the double gap
            // there still says which way the control sweeps.
            //
            // An even number of positions has no middle one; the fan is still
            // centred, so two positions straddle 12 o'clock instead.
            const auto step = juce::MathConstants<float>::twoPi
                                / (float) (selectorSpec.numChoices() + 1);
            const auto halfSpan = step * (float) (selectorSpec.numChoices() - 1) * 0.5f;

            ring.setRotaryParameters (-halfSpan, halfSpan, r.stopAtEnd);
        }
    }

    ring.setStyle (hasCentre ? Knob::Style::ring : Knob::Style::filter);
    ring.setFaceScale (hasCentre ? 0.529f : 0.35f);

    // The legend is drawn here, not by the slider, so a change of position has
    // to repaint the parent or the marked position goes stale.
    ring.onValueChange = [this] { repaint(); };

    addAndMakeVisible (ring);
    ringAttachment = std::make_unique<juce::SliderParameterAttachment> (selector, ring);

    if (hasCentre)
    {
        // Added second, so it sits above the ring and takes the mouse first.
        centre.setStyle (Knob::Style::character);
        centre.setAccent (accent);

        // The face is small, but the component spans the whole cell: its gain
        // track and its plus and minus are drawn well outside the face, and a
        // component only tight around the face would clip them away entirely.
        centre.setFaceScale (0.286f);
        centre.setCircularHitTest (true);

        // The gain keeps the suite's own sweep untouched: rest dot straight
        // up, minus at about 7:25, plus at about 4:35, pointer vertical at
        // 0 dB like every other knob in the suite. Only the frequency fan
        // moved, and it moved to the half of the dial the gain was not using.
        //
        // Two other arrangements were built and thrown away on the way here.
        // Turning the gain a quarter clockwise, to put its rest dot opposite
        // the fan at 3 o'clock, works on paper and reads as a knob turned hard
        // right at zero -- the rest dot is where the pointer rests, so moving
        // one moves the other. Flipping and shrinking the sweep to a 120
        // degree arc on the right, minus at 5 and plus at 1, keeps the two
        // controls on separate sides but makes gain rise anti-clockwise: with
        // zero at 3 o'clock and the sweep symmetric about it, the end that
        // carries the minus is the end that fixes the direction.
        addAndMakeVisible (centre);

        centreAttachment = std::make_unique<juce::SliderParameterAttachment> (*gain, centre);
    }

    for (int i = 0; i < selectorSpec.numChoices(); ++i)
        legend.add (compactFrequency (selectorSpec.choices[(size_t) i]));
}

void ConcentricBand::setRingEnabled (bool shouldBeEnabled)
{
    ringEnabled = shouldBeEnabled;
    ring.setEnabled (shouldBeEnabled);
    repaint();
}

void ConcentricBand::setLegend (const juce::StringArray& labels)
{
    jassert (labels.size() == legend.size());   // one label a position
    legend = labels;

    // A filter's dial is nudged by its legend's ink (geometry()), so new
    // words can move it.
    resized();
    repaint();
}

float ConcentricBand::legendOverflow() const
{
    auto overflow = -kLegendBoxWidth;

    for (const auto& label : legend)
        overflow = juce::jmax (overflow, juce::GlyphArrangement::getStringWidth (
                                             kPointUsesCaption ? captionFont (kPointSize) : labelFont (kPointSize, true), label)
                                           - kLegendBoxWidth);

    return overflow;
}

float ConcentricBand::filterLabelRadius (float angle, const juce::String& text,
                                         float ringRadius, float maxRadius) const
{
    // The selected size in both states: a label that grew when you switched
    // onto it would also move outwards, and the fan would breathe.
    const auto font = kPointUsesCaption ? captionFont (kPointSize)
                                        : labelFont (kPointSize, true);

    // The **ink**, not the box it is drawn in. kLegendBoxHeight is 15 and
    // these digits are 6 tall, so measuring the box would hand a near-vertical
    // ray four and a half pixels of empty air and push that label out by it:
    // 70 cleared 14.5 px where 45 and 160 cleared 10, which is exactly the
    // unevenness this function exists to remove.
    juce::GlyphArrangement arrangement;
    arrangement.addLineOfText (font, text, 0.0f, 0.0f);

    // The glyph outlines, not getBoundingBox: that returns the line's box,
    // which carries the font's ascent and descent whether the string uses them
    // or not, and left 70 two pixels proud of the rest.
    juce::Path outline;
    arrangement.createPath (outline);

    const auto ink = outline.getBounds();
    const auto halfWidth  = ink.getWidth()  * 0.5f;
    const auto halfHeight = ink.getHeight() * 0.5f;

    // Which of the box's own edges this ray crosses first, and how far out
    // that is from the label's centre.
    const auto s = std::abs (std::sin (angle));
    const auto c = std::abs (std::cos (angle));

    const auto reach = juce::jmin (s > 1.0e-3f ? halfWidth  / s : maxRadius,
                                   c > 1.0e-3f ? halfHeight / c : maxRadius);

    return juce::jmin (ringRadius + Tokens::filterLegendGap + reach, maxRadius);
}

ConcentricBand::Geometry ConcentricBand::geometry() const
{
    const auto area = getLocalBounds().toFloat();
    const auto ringRadius = juce::jmin (area.getWidth(), area.getHeight()) * 0.5f * ring.getFaceScale();

    // Knob edge, gap, dotted track, the same gap again, then the legend --
    // never so far out that a label runs off the top of the cell and is
    // clipped by whatever is above it.
    //
    // A band keeps a 9 px margin for that. A filter needs less of one: its
    // lowest position is the blank, so nothing is pushing down, and the row
    // above it is a rule with clear space under the line. At 9 the clamp was
    // biting -- the legend wanted 33.3 and got 29 -- and that was the whole of
    // why the circle looked cramped.
    const auto margin = hasCentre ? 9.0f : 4.0f;

    // The gain's dotted track runs between the selector ring and the frequency
    // legend, and the legend is the outermost thing on the dial. Swapping the
    // two -- legend tight to the ring, track outside it -- was built and works,
    // but it is not needed once the two controls are on opposite halves, and
    // it costs the track the clearance the cell's height gives the legend.
    const auto maxRadius = area.getHeight() * 0.5f - margin;

    if (hasCentre)
        return { ringRadius,
                 juce::jmin (centre.getTrackRadius() + Tokens::legendGap, maxRadius),
                 maxRadius, 0 };

    // Measure how far the ink actually reaches above and below the dial, and
    // nudge the assembly by half the difference. Every position but the blank
    // carries a label, and the blank is at the foot, so the answer is always a
    // shift downwards -- but it is measured rather than assumed, so it follows
    // a change in the number of positions or the radius on its own.
    const auto start = ring.getRotaryParameters().startAngleRadians;
    const auto end   = ring.getRotaryParameters().endAngleRadians;

    auto top = -ringRadius, bottom = ringRadius;

    for (int i = 0; i < legend.size(); ++i)
    {
        const auto f = legend.size() > 1 ? (float) i / (float) (legend.size() - 1) : 0.0f;
        const auto a = start + f * (end - start);
        const auto y = -std::cos (a) * filterLabelRadius (a, legend[i], ringRadius, maxRadius);

        top    = juce::jmin (top,    y - kLegendBoxHeight * 0.5f);
        bottom = juce::jmax (bottom, y + kLegendBoxHeight * 0.5f);
    }

    return { ringRadius, 0.0f, maxRadius, juce::roundToInt (-(top + bottom) * 0.5f) };
}

int ConcentricBand::inkHalfWidth() const
{
    return juce::roundToInt (geometry().maxRadius + kLegendBoxWidth * 0.5f);
}

void ConcentricBand::setDialOffset (int dx)
{
    if (dialOffset == dx)
        return;

    dialOffset = dx;
    resized();
    repaint();
}

void ConcentricBand::paint (juce::Graphics& g)
{
    if (legend.isEmpty())
        return;

    const auto& t = tokens();
    const auto area = getLocalBounds().toFloat();
    const auto geo = geometry();
    const auto centrePoint = area.getCentre().translated ((float) dialOffset, (float) geo.shift);

    const auto startAngle = ring.getRotaryParameters().startAngleRadians;
    const auto endAngle   = ring.getRotaryParameters().endAngleRadians;
    const auto selected   = juce::roundToInt (ring.getValue());

    for (int i = 0; i < legend.size(); ++i)
    {
        const auto f = legend.size() > 1 ? (float) i / (float) (legend.size() - 1) : 0.0f;
        const auto a = startAngle + f * (endAngle - startAngle);

        // A band's legend is one radius for all of it; a filter's is per label,
        // so that each clears the dial by the same gap. See filterLabelRadius.
        const auto textRadius = hasCentre
            ? geo.textRadius
            : filterLabelRadius (a, legend[i], geo.ringRadius, geo.maxRadius);

        const juce::Point<float> at { centrePoint.x + textRadius * std::sin (a),
                                      centrePoint.y - textRadius * std::cos (a) };

        const auto isSelected = (i == selected);

        // The module's own colour for the position you are on, neutral for one
        // you could switch to. Until 0.2.2 selected was the shared azure at
        // 2.22:1 and unselected was text2 at 2.45:1 -- so the *unselected*
        // legends had more contrast than the selected one, on the control this
        // module is mostly used through. Moving unselected up to text1 fixed
        // that on the pale plate: 4.53:1 selected against 4.37, near enough
        // equal, with hue doing the work.
        //
        // On the dark plate the same pair measures 8.98:1 selected against
        // 10.87, so the inversion is still there -- the frequency you did not
        // pick is seven points of L* brighter than the one you did. That is
        // deliberate and it is not fixable from this line. text1 sits at 10.87
        // of a 13.53 ceiling on this plate, and a *coloured* ink cannot pass it:
        // taking the accent past that luminance means mixing it so far toward
        // white it stops reading as the accent. The only lever is to dim the
        // unselected legends instead, and that was built, rendered and thrown
        // away -- at 6.00:1 it works, and it dims the numbers you read to
        // decide where to go next in order to emphasise the one you already
        // know.
        //
        // What carries selection here is the band marker, which since 0.2.3 is
        // the accent at full strength pointing straight at the chosen position.
        // It did not used to be: when this was first measured the marker was
        // near-black on a middle-grey ring, so nothing on the dial said which
        // frequency was live. Fixing the ring fixed the premise, and the
        // labels were left to hue. Frosty's call, on a side-by-side.
        auto fill = isSelected ? accentInk (accentColour) : t.text1;

        if (! ringEnabled)
            fill = fill.withAlpha (0.35f);

        drawLabel (g, legend[i], juce::Rectangle<float> (kLegendBoxWidth, kLegendBoxHeight).withCentre (at),
                   juce::Justification::centred,
                   kPointUsesCaption ? captionFont (isSelected ? kPointSize : kPointSizeIdle)
                                     : labelFont   (isSelected ? kPointSize : kPointSizeIdle, true),
                   fill);
    }
}

void ConcentricBand::resized()
{
    // The dial moves with its legend, so a filter is nudged down by the same
    // amount paint() nudges the labels -- see geometry().
    const auto bounds = getLocalBounds().translated (dialOffset, geometry().shift);

    ring.setBounds (bounds);

    if (! hasCentre)
        return;

    centre.setBounds (bounds);

    // The gain track has to clear the selector ring drawn around it, and the
    // gain control cannot work that out from its own face.
    const auto area = getLocalBounds().toFloat();
    const auto ringRadius = juce::jmin (area.getWidth(), area.getHeight()) * 0.5f * ring.getFaceScale();

    centre.setTrackRadius (ringRadius + Tokens::concentricTrackGap);
}

//==============================================================================
SwitchButton::SwitchButton (juce::RangedAudioParameter& parameter, const juce::String& text,
                            juce::Colour tint)
{
    setName (text);
    button.setButtonText (text);
    button.setColour (juce::ToggleButton::tickColourId, tint);
    addAndMakeVisible (button);

    attachment = std::make_unique<juce::ButtonParameterAttachment> (parameter, button);
}

void SwitchButton::resized()                 { button.setBounds (getLocalBounds()); }
void SwitchButton::setSwitchEnabled (bool e) { button.setEnabled (e); }

void SwitchButton::setLockedOn (bool shouldBeLocked)
{
    locked = shouldBeLocked;

    // Clicks off rather than enabled off: enabled off is what dims it, and a
    // switch the DSP is holding on is not a dimmed switch, it is an engaged
    // one you cannot turn off from here.
    button.setInterceptsMouseClicks (! locked, ! locked);

    if (locked)
    {
        // dontSendNotification, so the attachment does not hear it and the
        // parameter keeps the value the user set. The caller re-asserts this
        // while the lock holds -- a parameter change would otherwise push the
        // stored value back into the button underneath us -- and hands the
        // switch its real state back when the lock lifts.
        button.setToggleState (true, juce::dontSendNotification);
        button.repaint();
    }
}

void SwitchButton::setToggleStateSilently (bool shouldBeOn)
{
    button.setToggleState (shouldBeOn, juce::dontSendNotification);
    button.repaint();
}

void SwitchButton::setTint (juce::Colour tint)
{
    button.setColour (juce::ToggleButton::tickColourId, tint);
    button.repaint();
}

void SwitchButton::setLabelSize (float points)
{
    button.getProperties().set (BmoLookAndFeel::kSwitchLabelSize, points);
    button.repaint();
}

void SwitchButton::setActiveInkFrom (juce::Colour accent)
{
    // Derived against the fill the ink will sit on rather than against the
    // plate. On the pale plate the two land within a step of each other, so
    // the label matches the module's captions; on a dark one, deriving
    // against the plate would *lighten* the accent and put pale green on a
    // white switch.
    const auto fill = button.findColour (juce::ToggleButton::tickColourId);

    button.setColour (juce::ToggleButton::textColourId, accentTextOn (accent, fill));
    button.repaint();
}

//==============================================================================
OutputMeter::OutputMeter (std::function<float()> peakSource, std::function<float()> rmsSource)
    : peak (std::move (peakSource)), rms (std::move (rmsSource))
{
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
    startTimerHz (30);
}

void OutputMeter::mouseUp (const juce::MouseEvent&)
{
    vuMode = ! vuMode;
    displayed = 0.0f;
    repaint();
}

void OutputMeter::timerCallback()
{
    const auto level = vuMode ? (rms ? rms() : 0.0f) : (peak ? peak() : 0.0f);

    // A VU meter integrates; a peak meter jumps and falls back slowly.
    const auto rate = vuMode ? 0.28f : (level > displayed ? 1.0f : 0.16f);

    displayed += rate * (level - displayed);
    repaint();
}

void OutputMeter::paint (juce::Graphics& g)
{
    const auto& t = tokens();

    auto bounds = getLocalBounds();
    const auto labelArea = bounds.removeFromBottom (12);
    const auto well = bounds.withSizeKeepingCentre (kBarWidth, bounds.getHeight()).toFloat();

    g.setColour (t.well);
    g.fillRoundedRectangle (well, 2.0f);

    const auto db = juce::Decibels::gainToDecibels (displayed, -70.0f);
    const auto lo = vuMode ? -20.0f : -60.0f;
    const auto hi = vuMode ? 3.0f : 0.0f;
    const auto reading = vuMode ? db - kVuReference : db;
    const auto norm = juce::jlimit (0.0f, 1.0f, (reading - lo) / (hi - lo));

    if (norm > 0.002f)
    {
        auto bar = well.reduced (1.5f);
        bar = bar.removeFromBottom (bar.getHeight() * norm);

        const auto hot  = vuMode ? reading > 0.0f  : reading > -1.0f;
        const auto warm = vuMode ? reading > -3.0f : reading > -9.0f;

        g.setColour (hot ? t.meterClip : warm ? t.meterHigh : t.meterLow);
        g.fillRoundedRectangle (bar, 1.5f);
    }

    g.setColour (t.outline.withAlpha (0.6f));
    g.drawRoundedRectangle (well.reduced (0.5f), 2.0f, 1.0f);

    drawLabel (g, vuMode ? "VU" : "dBFS", labelArea.toFloat(), juce::Justification::centred,
               labelFont (9.0f), t.text2);
}

//==============================================================================
DynamicsMeter::DynamicsMeter (std::function<float()> inputRmsSource,
                              std::function<float()> outputRmsSource,
                              std::function<float()> gainReductionDbSource,
                              Mode initialMode, juce::Colour accent, juce::Colour hot)
    : inputRms (std::move (inputRmsSource)), outputRms (std::move (outputRmsSource)),
      gainReductionDb (std::move (gainReductionDbSource)), mode (initialMode),
      accentColour (accent), hotColour (hot)
{
    startTimerHz (30);
}

void DynamicsMeter::setMode (Mode newMode) noexcept
{
    if (newMode == mode)
        return;

    mode = newMode;
    // The two VU sources are linear RMS, the reduction source is already in
    // dB -- `displayed` is whichever unit the current mode reads in, so a
    // stale value from the old mode would paint a nonsense deflection for
    // one frame if it weren't reset here.
    displayed = 0.0f;
    repaint();
}

void DynamicsMeter::setColours (juce::Colour accent, juce::Colour hot) noexcept
{
    accentColour = accent;
    hotColour = hot;
    repaint();
}

void DynamicsMeter::timerCallback()
{
    float level = 0.0f;

    switch (mode)
    {
        case Mode::input:     level = inputRms         ? inputRms()         : 0.0f; break;
        case Mode::output:    level = outputRms        ? outputRms()        : 0.0f; break;
        case Mode::reduction: level = gainReductionDb  ? gainReductionDb()  : 0.0f; break;
    }

    // Same integration on every mode: VU levels and a dB reduction figure
    // both read as "how much is happening right now", so one rate serves all
    // three rather than needing a peak/VU distinction of its own.
    displayed += 0.28f * (level - displayed);
    repaint();
}

float DynamicsMeter::fractionFor (float value, const std::vector<ScalePoint>& scale) const noexcept
{
    if (value <= scale.front().value)  return scale.front().fraction;
    if (value >= scale.back().value)   return scale.back().fraction;

    for (size_t i = 1; i < scale.size(); ++i)
    {
        const auto& a = scale[i - 1];
        const auto& b = scale[i];

        if (value <= b.value)
        {
            const auto t = (value - a.value) / (b.value - a.value);
            return a.fraction + t * (b.fraction - a.fraction);
        }
    }

    return scale.back().fraction;
}

const std::vector<DynamicsMeter::ScalePoint>& DynamicsMeter::vuScale()
{
    // The reduction scale below, mirrored. GR is read near its start, where a
    // few dB is a decision; a VU is read near 0, so the same arc turned round
    // puts the fine ticks at the top: a tick every dB from -2 to +3, the gaps
    // widening toward +3 the way a real VU spreads there, then a tick every
    // 2 dB down to -24. Frosty's call, 2026-09-17, on renders, so that the two
    // faces of this meter are one design and switching IN/GR/OUT changes the
    // figures rather than the instrument.
    //
    //   dB   -24  -22  -20  ...  -6   -4   -2   -1    0   +1   +2   +3
    //   at  .000 .055 .109  ... .491 .545 .600 .665 .735 .810 .900 1.00
    //   ink   y    .    .        y    .    .    .    y    .    .    y
    //
    // The top five dB are GR's 0..5 exactly (1 - its fractions). An exact
    // mirror of all of GR spans 24 dB and would run -21..+3, printing -21,
    // -15, -9 and -3; that was rendered and not taken for the figures. Starting
    // at -24 costs the 2 dB gaps a little -- .055 of the sweep against GR's
    // .06 -- and buys figures a VU is read against: 3, 0, -6, -12, -18, -24.
    //
    // It replaced a hand-placed table whose gaps ran .10, .17, .14, .09, .08,
    // .09, then .05, .06, .07 up to 0 and .05 after it -- widening toward 0
    // and snapping narrow above it. Also rendered and not taken: a true VU law
    // (0 VU at .71; the low end crowds, the top goes sparse) and an even 1 dB
    // ruler from -7 up.
    //
    // The first point is printed now and is where the needle parks in
    // silence, the same as GR's 0. That is safe only because the figures sit
    // outside the arc -- see paint(). Silence parks at -24 VU, which is
    // -42 dBFS at kVuReference; anything quieter reads as rest.
    constexpr float kStep = 0.60f / 11.0f;   ///< one 2 dB gap, -24 to -2

    static const std::vector<ScalePoint> scale {
        { -24.0f, 0.0f },            { -22.0f, 1.0f * kStep, false }, { -20.0f, 2.0f * kStep, false },
        { -18.0f, 3.0f * kStep },    { -16.0f, 4.0f * kStep, false }, { -14.0f, 5.0f * kStep, false },
        { -12.0f, 6.0f * kStep },    { -10.0f, 7.0f * kStep, false }, {  -8.0f, 8.0f * kStep, false },
        {  -6.0f, 9.0f * kStep },    {  -4.0f, 10.0f * kStep, false },
        {  -2.0f, 1.0f - 0.400f, false }, { -1.0f, 1.0f - 0.335f, false },
        {   0.0f, 1.0f - 0.265f },   {   1.0f, 1.0f - 0.190f, false }, {   2.0f, 1.0f - 0.100f, false },
        {   3.0f, 1.0f },
    };

    return scale;
}

const std::vector<DynamicsMeter::ScalePoint>& DynamicsMeter::reductionScale()
{
    // Reduction is read where it is small: two or three dB is a decision, and
    // twenty is a fact you already knew. So this scale is fine at the bottom
    // and coarse at the top, in two ways at once -- the inked figures step 3
    // to 6 and then widen, and the sweep itself gives the low end far more
    // room than a linear map would. Frosty's calls throughout, taken on
    // rendered ladders.
    //
    // **The fractions are hand-placed, and that is deliberate.** They were a
    // power law until the shape was pinned down, and no single exponent can
    // produce it: pushing 6 outward drags 3 out with it, so 3..6 keeps the
    // same share of the sweep however the exponent is tuned. Going from 0.7 to
    // a square root moved 6 exactly where it was wanted and left 3..6 at 14.6%
    // either way, while the first dB of reduction swelled to a fifth of the
    // whole dial. The table below gives 3..6 19.5% and holds the first dB at
    // 10.0%, which an exponent cannot do at the same time.
    //
    // The cost is that a value added here has to be placed by hand and its
    // neighbours re-measured. There is no formula to evaluate. That is the
    // honest price of the shape, and it is cheaper than a formula that quietly
    // does not do what the comment claims.
    //
    // 0 and 24 are fixed points. Everything between is set against them.
    //
    //   dB    0    1    2    3    4    5    6    8   10   12   14   16   18   20   22   24
    //   at  .000 .100 .190 .265 .335 .400 .460 .520 .580 .640 .700 .760 .820 .880 .940 1.00
    //   ink  y    .    .    y    .    .    y    .    .    y    .    .    y    .    .    y
    //
    // Two regions. Through 0..6 a tick every dB, each gap a little narrower
    // than the one before -- Frosty's placement, untouched. Above 6 a tick
    // every 2 dB at an even .06 of the sweep, which is exactly the 5-to-6 gap,
    // so the arc runs on past 6 at the spacing it arrived with instead of
    // changing gear there. Every inked figure above 6 is even and lands on a
    // tick. Frosty's call, 2026-09-17, on renders.
    //
    // It replaced ticks at 9, 15 and 21 whose gaps ran .095, .110, .080,
    // .075, .092, .088 -- hand-placed and never smoothed, widest at 9..12,
    // which read as a wobble through 3..12. Rendered and not taken: log curves
    // above 6 (a hole after 6, 18 and 24 crowded), even 3 dB gaps (clean, but
    // the tick rhythm halves at 6), and 1 dB ticks above 6 (a comb denser than
    // the 0..6 it sits beside -- each dB there gets half the 5-to-6 arc).
    //
    // 9 was once inked and is not even struck now. At six figures this reads
    // as an instrument, at nine as a chart. Inked figures above 6 sit .18 of
    // the sweep apart, wider than the .155 that 12..18 had when it was the
    // tightest pair.
    //
    // The needle is non-linear in dB as a result -- it moves further per dB at
    // small reductions, which is the point of all of this, and which VU
    // already does. Display only: no DSP, no parameter, no spec.
    static const std::vector<ScalePoint> scale {
        { 0.0f,  0.000f },        { 1.0f,  0.100f, false }, { 2.0f,  0.190f, false },
        { 3.0f,  0.265f },        { 4.0f,  0.335f, false }, { 5.0f,  0.400f, false },
        { 6.0f,  0.460f },        { 8.0f,  0.520f, false }, { 10.0f, 0.580f, false },
        { 12.0f, 0.640f },        { 14.0f, 0.700f, false }, { 16.0f, 0.760f, false },
        { 18.0f, 0.820f },        { 20.0f, 0.880f, false }, { 22.0f, 0.940f, false },
        { kGrRangeDb, 1.000f },
    };

    return scale;
}

void DynamicsMeter::paint (juce::Graphics& g)
{
    const auto& t = tokens();

    const auto isReduction = mode == Mode::reduction;

    // The whole box is face. A caption naming the current mode used to take 14
    // px off the bottom of it, and it was saying what the IN/GR/OUT row under
    // the meter already says -- louder, in the same place, and without the 9 pt
    // and 2.45:1 on the pale plate that the caption was read at. The 14 px went
    // back to the panel. With it went the suite's only non-ASCII source glyph.
    auto bounds = getLocalBounds().toFloat();

    // Sweep geometry: needle pivots at bottom-centre, arcs upward. 100
    // degrees total, split evenly either side of straight up.
    // Half the width of the hub the needle turns on.
    constexpr float kHubRadius = 3.5f;

    // A 124 degree sweep is limited by width, never by height: the arc ends
    // reach sin(62) = 0.88 of the radius sideways but only 0.47 of it
    // downwards. Taking the radius from jmin(width/2, height) therefore sized
    // the arc to the wrong dimension whenever the face was taller than half
    // its width, which is every face this has been given -- 0.2.1 shipped with
    // roughly 40% of the meter empty above the needle.
    // The numbers sit **outside** the arc, where the needle cannot reach them.
    //
    // They used to be drawn at radius - 19, inside the tick ring, while the
    // needle runs out to radius - 4 -- so the needle crossed every label
    // position on the dial. The VU scale got away with it only because its
    // first point is struck and never printed, and that is where the needle
    // parks; see the vuScale comment above. The GR scale has no such point, so
    // at zero reduction -- the state that meter is in whenever the module is
    // not working, and the first thing anyone sees -- the needle lay straight
    // across its own 0.
    //
    // Moving the ring outside settles it for both scales, and for any scale
    // added later, rather than by giving each one its own park point.
    constexpr float kLabelRing  = 12.0f;                     ///< arc to label centre
    constexpr float kLabelHalf  = 7.5f;                      ///< half a label box
    constexpr float kLabelReach = kLabelRing + kLabelHalf;   ///< arc to the top of the ink

    // Both dimensions, not width alone. A bare arc is limited by width -- its
    // ends reach sin(62) = 0.88 of the radius sideways against 0.47 of it
    // downwards -- but a ring of numbers above the arc is not: that reaches a
    // full radius plus the ring, straight up.
    //
    // This is the fault the comment above describes, one dimension over.
    // Sizing from width alone put the top number 1.2 px *off* the top of the
    // face, and it read as the arc being too high.
    const auto byWidth  = bounds.getWidth() * 0.5f - 8.0f - kLabelReach;
    const auto byHeight = bounds.getHeight() - kHubRadius - kLabelReach - 6.0f;
    const auto radius   = juce::jmin (byWidth, byHeight);

    // What actually gets drawn runs from the apex, one radius above the pivot,
    // down to the hub. Centre that block in whatever face the panel hands over
    // rather than pinning the pivot to the bottom edge, so a taller box can
    // never bring the dead band back. A box shorter than the block puts the
    // pivot below the face, which is where the hardware hides it anyway.
    //
    // The block is the numbers, the arc and the hub -- not the arc alone.
    // Centring a height that left the label ring out of the sum is exactly
    // what pushed the ring off the top edge.
    //
    // Then 6 px lower again. Frosty's call, taken on a rendered ladder: it
    // gives the numbers 32 px of face above them rather than 20, and spends
    // the hub, which now meets the bottom bezel instead of sitting clear of
    // it. Hardware hides the pivot under the faceplate entirely; this leans
    // that way without going all the way. Do not "centre" it back.
    constexpr float kDrop = 6.0f;

    const auto drawnHeight = kLabelReach + radius + kHubRadius;
    const auto pivot = juce::Point<float> (bounds.getCentreX(),
                                           bounds.getY() + (bounds.getHeight() + drawnHeight) * 0.5f
                                             + kDrop);
    // 124 rather than 100 degrees: the numbers are set larger now, and the
    // extra arc is what keeps them apart at the crowded top of the scale.
    const auto sweep     = juce::degreesToRadians (124.0f);
    const auto startAngle = -sweep * 0.5f;
    const auto angleFor  = [&] (float fraction) { return startAngle + fraction * sweep; };

    // Face plate: dark, so the light ink on it reads. The bezel stays the
    // module's accent -- semantic colour on the frame, luminance contrast on
    // everything that has to be read.
    g.setColour (t.meterFace);
    g.fillRoundedRectangle (bounds, 4.0f);
    g.setColour (accentColour.withAlpha (0.7f));
    g.drawRoundedRectangle (bounds.reduced (0.75f), 4.0f, 1.5f);

    // Scale ticks and numbers. Split into two paths so the 0 VU and above
    // zone -- hotColour, a classic VU meter's red printed in the module's
    // own colour instead -- strokes separately from the rest of the scale.
    const auto& scale = isReduction ? reductionScale() : vuScale();
    juce::Path ticks, hotTicks;

    for (const auto& p : scale)
    {
        const auto angle = angleFor (p.fraction);
        const auto inner = pivot.getPointOnCircumference (radius - 7.0f, angle);
        const auto outer = pivot.getPointOnCircumference (radius, angle);
        const auto hot = ! isReduction && p.value >= 0.0f;

        auto& path = hot ? hotTicks : ticks;
        path.startNewSubPath (inner);
        path.lineTo (outer);

        if (! p.numbered)
            continue;

        // The scale is printed in white, the hot zone in the module's own
        // colour. Colour marks the zone; contrast does the reading.
        const auto labelCentre = pivot.getPointOnCircumference (radius + kLabelRing, angle);
        drawLabel (g, juce::String ((int) p.value),
                   juce::Rectangle<float> (28.0f, 15.0f).withCentre (labelCentre),
                   juce::Justification::centred, labelFont (11.5f),
                   hot ? hotColour : t.meterInk);
    }

    g.setColour (t.meterInk);
    g.strokePath (ticks, juce::PathStrokeType (1.4f));
    g.setColour (hotColour);
    g.strokePath (hotTicks, juce::PathStrokeType (1.4f));

    // Needle.
    const auto valueForNeedle = isReduction ? juce::jlimit (0.0f, kGrRangeDb, displayed)
                                              : juce::Decibels::gainToDecibels (displayed, -70.0f) - kVuReference;
    const auto needleAngle = angleFor (fractionFor (valueForNeedle, scale));
    const auto tip = pivot.getPointOnCircumference (radius - 4.0f, needleAngle);

    // White in every mode. The needle is the one thing on this panel that has
    // to be legible before you look at it, so it gets the maximum contrast
    // against the face rather than a colour that says which mode is up --
    // the button row underneath already says that.
    g.setColour (t.meterInk);
    g.drawLine (juce::Line<float> (pivot, tip), 2.4f);
    g.fillEllipse (juce::Rectangle<float> (kHubRadius * 2.0f, kHubRadius * 2.0f).withCentre (pivot));
}

} // namespace bmo::ui
