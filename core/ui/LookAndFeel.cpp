#include "LookAndFeel.h"
#include "ModulePanel.h"

namespace bmo::ui
{

const juce::String& BmoLookAndFeel::phaseGlyph()
{
    static const juce::String glyph (juce::CharPointer_UTF8 ("\xc3\x98"));
    return glyph;
}

void BmoLookAndFeel::refreshColours()
{
    const auto& t = tokens();

    setColour (juce::ResizableWindow::backgroundColourId, t.plate);
    setColour (juce::Label::textColourId,                 t.text1);

    setColour (juce::ComboBox::backgroundColourId,        t.plate);
    setColour (juce::ComboBox::textColourId,              t.text1);
    setColour (juce::ComboBox::outlineColourId,           t.outline);
    setColour (juce::ComboBox::arrowColourId,             t.track);

    setColour (juce::PopupMenu::backgroundColourId,       t.plateEdge);
    setColour (juce::PopupMenu::textColourId,             t.text1);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, t.trackFill);
    setColour (juce::PopupMenu::highlightedTextColourId,  onAccentOf (t.trackFill));

    // The preset strip is built from TextButtons, which otherwise come out in
    // JUCE's default blue and fight the scheme.
    setColour (juce::TextButton::buttonColourId,   t.plate);
    setColour (juce::TextButton::buttonOnColourId, t.trackFill);
    setColour (juce::TextButton::textColourOffId,  t.text1);
    setColour (juce::TextButton::textColourOnId,   onAccentOf (t.trackFill));

    // Left transparent so that "unset" is the common case: drawToggleButton
    // derives an engaged switch's label from its own fill unless the switch
    // names one. JUCE's own default here is opaque, which would have made
    // every switch look like it had asked for something.
    setColour (juce::ToggleButton::textColourId, juce::Colours::transparentBlack);

    setColour (juce::AlertWindow::backgroundColourId, t.plateEdge);
    setColour (juce::AlertWindow::textColourId,       t.text1);
    setColour (juce::AlertWindow::outlineColourId,    t.outline);
    setColour (juce::TextEditor::backgroundColourId,  t.plate);
    setColour (juce::TextEditor::textColourId,        t.text1);
    setColour (juce::TextEditor::outlineColourId,     t.outline);
    setColour (juce::TextEditor::highlightColourId,   t.trackFill);
}

juce::Font BmoLookAndFeel::getLabelFont (juce::Label& label)
{
    return labelFont (label.getHeight() > 0 ? juce::jmin (12.0f, (float) label.getHeight()) : 11.0f);
}

juce::Font BmoLookAndFeel::getTextButtonFont (juce::TextButton&, int buttonHeight)
{
    return labelFont (juce::jmin (13.0f, (float) buttonHeight * 0.55f));
}

//==============================================================================
void BmoLookAndFeel::drawDottedArc (juce::Graphics& g, juce::Point<float> centre, float radius,
                                    float startAngle, float endAngle, juce::Colour colour,
                                    float dotSize)
{
    // std::abs, because a sweep may run backwards -- a control whose value
    // rises anti-clockwise hands this a negative span, and the dot count came
    // out negative and clamped to the minimum eight.
    const auto span = endAngle - startAngle;
    const auto count = juce::jlimit (8, 96, juce::roundToInt (radius * std::abs (span) * 0.16f));

    g.setColour (colour);

    for (int i = 0; i <= count; ++i)
    {
        const auto a = startAngle + span * (float) i / (float) count;
        const juce::Point<float> at { centre.x + radius * std::sin (a),
                                      centre.y - radius * std::cos (a) };

        g.fillEllipse (juce::Rectangle<float> (dotSize, dotSize).withCentre (at));
    }
}

//==============================================================================
void BmoLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height,
                                       float sliderPos, float startAngle, float endAngle,
                                       juce::Slider& slider)
{
    const auto& t = tokens();
    auto* knob = dynamic_cast<Knob*> (&slider);
    const auto style = knob != nullptr ? knob->getStyle() : Knob::Style::utility;
    const auto moduleAccent = knob != nullptr ? knob->getAccent() : t.accent;

    const auto bounds = juce::Rectangle<int> (x, y, width, height).toFloat();
    const auto centre = bounds.getCentre();
    const auto scale  = knob != nullptr ? knob->getFaceScale() : 1.0f;
    const auto radius = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f * scale;
    const auto angle  = startAngle + sliderPos * (endAngle - startAngle);
    const auto enabled = slider.isEnabled();

    const auto dim = [enabled] (juce::Colour c) { return enabled ? c : c.withAlpha (0.35f); };

    const auto at = [centre] (float a, float r)
    {
        return juce::Point<float> { centre.x + r * std::sin (a), centre.y - r * std::cos (a) };
    };

    // The selector ring of a band: a white annulus with the selected position
    // marked on it, drawn behind the gain control that sits inside it.
    if (style == Knob::Style::ring)
    {
        const auto thickness = radius * 0.30f;
        const auto mid = radius - thickness * 0.5f;

        juce::Path ring;
        ring.addCentredArc (centre.x, centre.y, mid, mid, 0.0f, 0.0f,
                            juce::MathConstants<float>::twoPi, true);

        g.setColour (dim (t.ringFace));
        g.strokePath (ring, juce::PathStrokeType (thickness));

        g.setColour (dim (t.knobEdge));
        g.drawEllipse (juce::Rectangle<float> (radius * 2.0f, radius * 2.0f).withCentre (centre), 1.6f);
        g.drawEllipse (juce::Rectangle<float> ((radius - thickness) * 2.0f,
                                               (radius - thickness) * 2.0f).withCentre (centre), 1.6f);

        // Where the switch is set.
        juce::Path marker;
        marker.addCentredArc (centre.x, centre.y, mid, mid, 0.0f,
                              angle - 0.10f, angle + 0.10f, true);

        // Which position the band is switched to. Derived against the ring it
        // is drawn on, not the plate: as the shared knobFace azure it measured
        // 1.49:1 on the white annulus, and this marker is the only thing on a
        // band that says what frequency is selected.
        g.setColour (dim (accentTextOn (moduleAccent, t.ringFace)));
        g.strokePath (marker, juce::PathStrokeType (thickness));
        return;
    }

    const auto character = style == Knob::Style::character;

    // A line may fix its caps rather than derive them from each module's
    // accent -- LTV is black knobs on a silver panel. When it does, the
    // pointer comes off the cap instead of off the appearance: `pointer` is
    // near-black in the dark set because a dark cap is normally the accent at
    // full strength and therefore light, and a fixed black cap would swallow
    // it. onAccentOf hands back white on either of these.
    // Every knob on the line, not only the character ones -- Frosty,
    // 2026-09-15. LTV Comp's drawer knobs take the line's cap so they match
    // AMOUNT and MAKEUP, while their dotted track, rest dot and caption stay
    // the drawer's red. The cap says which instrument this is; the ink says
    // which group of controls you are looking at.
    const auto lineCap = capFor (panelLineFor (slider));

    const auto tinted = knob != nullptr && ! knob->getUtilityTint().isTransparent();

    const auto face       = lineCap ? *lineCap
                                    : (character ? faceOf (moduleAccent)
                                                 : (tinted ? knob->getUtilityTint() : t.knobFace));
    const auto pointerInk = lineCap ? onAccentOf (*lineCap) : t.pointer;
    // The dotted track, its plus and minus, and the rest dot. Routed through
    // the line as well as the cap and the pointer: these are the knob's own
    // marks, and leaving them on the module accent is what kept a periwinkle
    // ring around a black LTV knob after the cap and the caption had moved.
    // A utility knob may be tinted -- see Knob::setUtilityTint. Transparent
    // means the suite azure, which is what every trim knob outside LTV Comp's
    // drawer still draws in.
    const auto utilityInk = knob != nullptr && ! knob->getUtilityTint().isTransparent()
                                ? knob->getUtilityTint()
                                : t.track;

    // Routed through the line for utility knobs as well as character ones --
    // Frosty, 2026-09-15. A tinted drawer keeps its colour on the *caption*
    // and gives the marks back to the line, so LTV Comp's five drawer knobs
    // carry the same track, rest dot and plus-minus as AMOUNT and MAKEUP and
    // differ only in their lettering. Nothing changes on a BMO panel: with no
    // line ink, panelAccentFor hands the fallback straight back.
    const auto accent    = panelAccentFor (slider, character ? moduleAccent : utilityInk);
    const auto faceBox   = juce::Rectangle<float> (radius * 2.0f, radius * 2.0f).withCentre (centre);

    // Gain controls carry a dotted track, with the rest position marked on it
    // and a plus -- and, where the control cuts as well, a minus -- at its ends.
    if (style == Knob::Style::utility || character)
    {
        const auto given = knob != nullptr ? knob->getTrackRadius() : 0.0f;
        const auto track = given > 0.0f ? given : radius + Tokens::trackGap;

        // Signed with the sweep. The inset pulls the symbols in from the ends;
        // on a sweep that runs backwards, adding it to the start and taking it
        // off the end pushes them out past the ends instead.
        const auto symbolInset = endAngle >= startAngle ? 0.11f : -0.11f;

        // The dotted ring stops short of the sweep's ends, and the plus and
        // minus are placed on those two terminal dots rather than beyond them.
        // They then read as the two ends of the ring itself, in its own
        // rhythm, instead of as a pair of marks parked just outside it -- at
        // this radius the old gap was about four pixels of nothing.
        const auto minusAngle = startAngle + symbolInset;
        const auto plusAngle  = endAngle   - symbolInset;

        drawDottedArc (g, centre, track, minusAngle, plusAngle,
                       dim (accent.withAlpha (enabled ? 0.55f : 0.2f)), 1.6f);

        // The heavy dot marks where the control rests -- its default, which is
        // where double-clicking it already puts it back.
        //
        // That value is not ours to set: `juce::SliderParameterAttachment`
        // calls `setDoubleClickReturnValue` with the parameter's own default
        // when it attaches, so every attached knob in the suite already
        // carries it. Reading it back here rather than deriving the default a
        // second time is the point -- the mark and the gesture are then one
        // fact, and cannot drift apart. A slider with no attachment keeps the
        // old behaviour rather than losing its dot.
        //
        // It used to mark *zero*, clamped into range. On a control that cuts
        // and boosts those are the same point, which is why this went unseen
        // for so long: the comment in ConcentricBand already says the dot is
        // "where the pointer rests", and on BMO EQ's bipolar band gain it was.
        // On a control that only goes up they are not the same point at all.
        // The Saturator's TONE and BMO EQ's MIX both default to their
        // *maximum*, so the dot sat at the far end of the dial from anywhere
        // the control had ever been, and every panel opened with its pointers
        // apparently parked away from their own marked rest positions.
        //
        // valueToProportionOfLength rather than arithmetic across the range:
        // it is the same mapping the pointer goes through, so a skewed control
        // would keep the two together. Nothing in the suite is skewed today,
        // which is exactly why it is worth spending the call now.
        const auto range = slider.getRange();
        const auto rest  = slider.isDoubleClickReturnEnabled()
                             ? slider.getDoubleClickReturnValue()
                             : juce::jlimit (range.getStart(), range.getEnd(), 0.0);
        const auto restPos = range.getLength() > 0.0
                               ? (float) juce::jlimit (0.0, 1.0, slider.valueToProportionOfLength (rest))
                               : 0.5f;

        // A control whose default *is* one of its ends puts the dot on top of
        // the symbol already marking that end -- the Saturator's TONE and MIX
        // both rest at maximum, and rendered, the dot and the plus fused into
        // one malformed glyph. The end symbol wins that argument: it says
        // which way the control increases, which is what you need before you
        // turn it, and a knob resting at an end already shows that by where
        // its pointer sits when the panel opens.
        //
        // Measured as a pixel clearance converted to an angle at this knob's
        // own track radius, because the arc a given gap subtends depends on
        // the radius and these knobs run from 36 px to 53. Seven pixels is the
        // 5 px dot and the 8.4 px plus just clearing each other.
        const auto restAngle = startAngle + restPos * (endAngle - startAngle);
        const auto clearArc  = 7.0f / juce::jmax (track, 1.0f);

        const auto collides = std::abs (restAngle - plusAngle)  < clearArc
                           || std::abs (restAngle - minusAngle) < clearArc;

        if (! collides && (knob == nullptr || knob->hasRestMark()))
        {
            g.setColour (dim (accent));
            g.fillEllipse (juce::Rectangle<float> (5.0f, 5.0f).withCentre (at (restAngle, track)));
        }

        // Drawn rather than set. Neither panel face has a minus sign that
        // matches its plus, and two strokes and a bar are the one case where
        // drawing beats setting: they match each other exactly, at any size,
        // on any machine.
        {
            // A gain sitting inside a selector ring has far less room for these
            // than a knob with a bare face: its track runs in the gap between
            // the ring's outer edge and the frequency legend, which is about
            // eight and a half pixels. At the bare-face size the symbols need
            // fourteen and are drawn straight over the ring. A knob that was
            // given its track radius is one of those; one that works its own
            // out is not.
            const auto concentric = knob != nullptr && knob->getTrackRadius() > 0.0f;

            const auto arm    = concentric ? 2.8f : 4.2f;
            const auto weight = concentric ? 1.8f : 2.3f;

            g.setColour (dim (accent));

            // Both ends, on every tracked knob. The minus used to appear only
            // where the control's range went below zero, which read the pair
            // as "negative and positive" -- so a knob that only goes up got a
            // plus and a bare arc end.
            //
            // They mean **less and more**, which is how a hardware faceplate
            // marks a knob and is true of every control here: the Saturator's
            // DRIVE runs from less drive to more, and nothing about that
            // claims it cuts. Frosty's call, taken once the rest dot stopped
            // sitting on the bottom of the sweep and stopped anchoring that
            // end by accident.
            const auto minusAt = at (minusAngle, track);
            const auto plusAt  = at (plusAngle, track);

            if (knob != nullptr && knob->getEndMarks() == Knob::EndMarks::leftRight)
            {
                // Letters, so these are set rather than drawn -- two letters are
                // not balanced by construction the way two bars are, which is
                // what the ink-centring below is for.
                //
                // Blender at 12, Frosty's call on 2026-09-16 from a rendered
                // ladder of both faces at 9-13 pt. Minerva is the caption face
                // and was the obvious candidate, but at this size its L and R
                // are narrow enough to read as marks rather than letters;
                // Blender's are rounder and read at a glance. At 12 pt the ink
                // is 8.5 px tall, level with the plus it stands in for, and
                // clears the caption below by 8 px.
                const auto font = captionFont (12.0f);

                // Centred on the letter's ink, not its advance box: L and R
                // have different widths and sidebearings, and centring the
                // boxes would put the two marks at different distances from
                // the ends they name.
                const auto mark = [&] (const juce::String& letter, juce::Point<float> where)
                {
                    juce::GlyphArrangement ga;
                    ga.addLineOfText (font, letter, 0.0f, 0.0f);
                    const auto ink = ga.getBoundingBox (0, -1, false);

                    juce::Path p;
                    ga.createPath (p);
                    p.applyTransform (juce::AffineTransform::translation (where - ink.getCentre()));
                    g.fillPath (p);
                };

                mark ("L", minusAt);
                mark ("R", plusAt);
            }
            else
            {
                g.fillRect (juce::Rectangle<float> (arm * 2.0f, weight).withCentre (minusAt));
                g.fillRect (juce::Rectangle<float> (arm * 2.0f, weight).withCentre (plusAt));
                g.fillRect (juce::Rectangle<float> (weight, arm * 2.0f).withCentre (plusAt));
            }
        }
    }

    g.setColour (dim (face));
    g.fillEllipse (faceBox);
    g.setColour (dim (character ? t.outline : t.knobEdge));
    g.drawEllipse (faceBox.reduced (0.8f), character ? 1.6f : Tokens::knobStroke);

    // Pointer.
    {
        const auto tip  = radius - 3.0f;
        const auto tail = radius * 0.05f;

        g.setColour (dim (pointerInk));
        g.drawLine ({ at (angle, tail), at (angle, tip) }, 2.6f);
    }
}

//==============================================================================
juce::Rectangle<float> BmoLookAndFeel::toggleLabelBox (const juce::ToggleButton& button)
{
    return button.getLocalBounds().toFloat().reduced (3.0f);
}

juce::Font BmoLookAndFeel::toggleLabelFont (const juce::ToggleButton& button)
{
    // 62% of the box, unless the switch pins a size. Deriving it from the
    // height is right for a row of switches that are all the suite's own
    // height, and wrong for one that is taller than it is wide: the text grows
    // with the height, so it outgrows the width faster than the width grows.
    // "HI-Q" overflows a square switch at *every* size because of it -- 11.3 px
    // at 40, 16.0 at 54. BMO CEQ's HI-Q pins the size a 26 px switch would have
    // used, so a square switch sets its label the same as the row does.
    const auto pinned = (float) button.getProperties().getWithDefault (kSwitchLabelSize, 0.0);

    return labelFont (pinned > 0.0f ? pinned : toggleLabelBox (button).getHeight() * 0.62f, true);
}

float BmoLookAndFeel::toggleLabelOverflow (const juce::ToggleButton& button)
{
    const auto box  = toggleLabelBox (button);
    const auto font = toggleLabelFont (button);
    const auto text = button.getButtonText();

    if (! text.contains (phaseGlyph()))
        return juce::GlyphArrangement::getStringWidth (font, text) - box.getWidth();

    // Drawn, not set: the circle is a path of radius height * 0.30, and
    // anything left over -- " L", " R" -- is set beside it with 4 px of air.
    const auto rest = text.replace (phaseGlyph(), "").trim();
    const auto restWidth = rest.isEmpty() ? 0.0f
                         : juce::GlyphArrangement::getStringWidth (font, rest) + 4.0f;

    return box.getHeight() * 0.60f + restWidth - box.getWidth();
}

void BmoLookAndFeel::drawToggleButton (juce::Graphics& g, juce::ToggleButton& button,
                                       bool shouldDrawHighlighted, bool shouldDrawDown)
{
    const auto& t = tokens();
    const auto bounds = toggleLabelBox (button);
    const auto on = button.getToggleState();

    // The switch's engaged colour is set by whoever made it: the module's
    // accent, the deeper azure, or the suite's pink.
    const auto tint = button.findColour (juce::ToggleButton::tickColourId);

    auto fill = on ? tint : t.switchOff;

    if (shouldDrawDown)             fill = fill.darker (0.12f);
    else if (shouldDrawHighlighted) fill = fill.brighter (0.06f);

    if (! button.isEnabled())
        fill = fill.withAlpha (0.35f);

    // An engaged switch glows: a few rounded rectangles stepping outwards at
    // falling alpha. Kept faint, because the text has to stay first.
    if (on && button.isEnabled())
        for (int i = 3; i >= 1; --i)
        {
            g.setColour (tint.withAlpha (0.10f * (float) i / 3.0f));
            g.fillRoundedRectangle (bounds.expanded ((float) i), Tokens::corner + (float) i);
        }

    g.setColour (fill);
    g.fillRoundedRectangle (bounds, Tokens::corner);

    // Ink derived from the fill it sits on rather than always white: white
    // measured 1.98-2.55:1 on the four accents, and 2.43:1 on the old pale
    // switchOff, so a switch's label was equally hard to read in both states
    // and on/off was carried by hue alone.
    //
    // A switch may name its own engaged ink through textColourId, which
    // refreshColours() clears so that "unset" is transparent and means
    // "derive it". Polarity is why: its fill is white in every module, so
    // deriving gives black in every module, and the label is the one part of
    // that switch left free to say which module it belongs to.
    const auto named = button.findColour (juce::ToggleButton::textColourId);
    const auto ink = ((on && ! named.isTransparent()) ? named : onAccentOf (fill))
                         .withAlpha (button.isEnabled() ? 1.0f : 0.4f);

    // The polarity switch is drawn, not set: typing the slashed O gives back
    // whatever the machine maps it to, which on several faces is a plain O and
    // says nothing.
    const auto text = button.getButtonText();
    const auto font = toggleLabelFont (button);

    if (text.contains (phaseGlyph()))
    {
        // Whatever follows the glyph -- " L", " R" -- is set beside it.
        const auto rest = text.replace (phaseGlyph(), "").trim();
        const auto restWidth = rest.isEmpty() ? 0.0f
                             : juce::GlyphArrangement::getStringWidth (font, rest) + 4.0f;

        const auto r = bounds.getHeight() * 0.30f;
        const auto weight = juce::jmax (1.6f, r * 0.22f);
        const auto centre = bounds.getCentre().translated (-restWidth * 0.5f, 0.0f);

        juce::Path symbol;
        symbol.addEllipse (juce::Rectangle<float> (r * 2.0f, r * 2.0f).withCentre (centre));
        symbol.startNewSubPath (centre.x - r * 0.95f, centre.y + r * 0.95f);
        symbol.lineTo         (centre.x + r * 0.95f, centre.y - r * 0.95f);

        g.setColour (ink);
        g.strokePath (symbol, juce::PathStrokeType (weight, juce::PathStrokeType::curved,
                                                    juce::PathStrokeType::rounded));

        if (rest.isNotEmpty())
            ui::drawLabel (g, rest, bounds.withLeft (centre.x + r + 4.0f), juce::Justification::centredLeft,
                       font, ink);
        return;
    }

    ui::drawLabel (g, text, bounds, juce::Justification::centred, font, ink);
}

} // namespace bmo::ui
