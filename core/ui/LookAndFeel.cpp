#include "LookAndFeel.h"
#include "ModulePanel.h"
#include <map>

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
namespace
{
    /** The face a dropdown's closed text is set in, from its height alone, so
        `getComboBoxFont` and the static overflow measurement -- which has no
        non-const ComboBox to hand -- cannot come to different answers. */
    juce::Font comboFontFor (int boxHeight)
    {
        return labelFont (juce::jmin (13.0f, (float) boxHeight * 0.55f));
    }
}

juce::Font BmoLookAndFeel::getComboBoxFont (juce::ComboBox& box)
{
    return comboFontFor (box.getHeight());
}

juce::Rectangle<int> BmoLookAndFeel::comboTextBox (const juce::ComboBox& box)
{
    // LookAndFeel_V4's own geometry, written out rather than inherited: its
    // arrow is drawn into `Rectangle (width - 30, 0, 20, height)`, so 30 px off
    // the right is the room the text actually has. Here so that the label's
    // bounds and the fit measurement are one number rather than two.
    return { 1, 1, box.getWidth() - 30, box.getHeight() - 2 };
}

void BmoLookAndFeel::positionComboBoxText (juce::ComboBox& box, juce::Label& label)
{
    label.setBounds (comboTextBox (box));
    label.setFont (getComboBoxFont (box));
}

float BmoLookAndFeel::comboTextOverflow (const juce::ComboBox& box)
{
    const auto font = comboFontFor (box.getHeight());
    auto widest = 0.0f;

    for (int i = 0; i < box.getNumItems(); ++i)
        widest = juce::jmax (widest,
                             juce::GlyphArrangement::getStringWidth (font, box.getItemText (i)));

    return widest - (float) comboTextBox (box).getWidth();
}


//==============================================================================
// PROTOTYPE -- the material pass. Off unless BMO_MATERIAL is set in the
// environment, so a snapshot can render both looks from one binary. See
// docs/ui-material-proposal.md for what it is, what it costs and the rules it
// keeps. Every value here is a shading of a token, never a colour of its own.
namespace material
{
    bool enabled()
    {
        static const bool on = juce::SystemStats::getEnvironmentVariable ("BMO_MATERIAL", {}).isNotEmpty();
        return on;
    }

    /** The variants under review, named in BMO_MATERIAL: "brushed" for the
        brushed plate rather than powder, "cap" for the one-piece knob rather
        than skirt and cap. e.g. BMO_MATERIAL=brushed,cap. */
    bool has (const char* word)
    {
        return juce::SystemStats::getEnvironmentVariable ("BMO_MATERIAL", {}).contains (word);
    }

    bool brushed()  { static const bool b = has ("brushed"); return b; }
    bool capKnob()  { static const bool b = has ("cap");     return b; }

    /** A 128 px tile of fine, non-directional grain -- a powder-coat, not a
        photograph. Signed around zero and drawn at a few percent, so it moves
        the plate's luminance by about +/-1.5 % and no ink ratio by more than
        a rounding step. One fixed seed, built once, so every render of it is
        the same render. */
    /** A 256 x 128 tile of brushed grain: streaks along x, each row its own
        run of smoothed noise, wrapped so the tile repeats without a seam.
        About the same amplitude as the powder, all of it in one direction. */
    const juce::Image& brushedTile()
    {
        static const juce::Image tile = []
        {
            constexpr int w = 256, h = 128, run = 40;
            juce::Image img (juce::Image::ARGB, w, h, true);
            juce::Random rng (0x42727573);
            juce::Image::BitmapData bd (img, juce::Image::BitmapData::writeOnly);

            std::vector<float> raw ((size_t) w), row ((size_t) w);

            for (int y = 0; y < h; ++y)
            {
                for (auto& v : raw)
                    v = rng.nextFloat() * 2.0f - 1.0f;

                // A wrapped box blur along the row: long streaks, not dots.
                for (int x = 0; x < w; ++x)
                {
                    auto sum = 0.0f;
                    for (int k = -run / 2; k < run / 2; ++k)
                        sum += raw[(size_t) ((x + k + w) % w)];
                    row[(size_t) x] = sum / std::sqrt ((float) run);
                }

                const auto rowTone = (rng.nextFloat() * 2.0f - 1.0f) * 0.6f;

                for (int x = 0; x < w; ++x)
                {
                    const auto v = juce::jlimit (-1.0f, 1.0f, row[(size_t) x] * 0.55f + rowTone
                                                             + (rng.nextFloat() - 0.5f) * 0.25f);
                    const auto a = (juce::uint8) juce::roundToInt (std::abs (v) * 255.0f * 0.045f);
                    bd.setPixelColour (x, y, v > 0.0f ? juce::Colour (255, 255, 255).withAlpha (a)
                                                      : juce::Colour (0, 0, 0).withAlpha (a));
                }
            }

            return img;
        }();

        return tile;
    }

    const juce::Image& grainTile()
    {
        static const juce::Image tile = []
        {
            constexpr int n = 128;
            juce::Image img (juce::Image::ARGB, n, n, true);
            juce::Random rng (0x424d4f);
            juce::Image::BitmapData bd (img, juce::Image::BitmapData::writeOnly);

            for (int y = 0; y < n; ++y)
                for (int x = 0; x < n; ++x)
                {
                    const auto v = rng.nextFloat() * 2.0f - 1.0f;
                    const auto a = (juce::uint8) juce::roundToInt (std::abs (v) * 255.0f * 0.035f);
                    bd.setPixelColour (x, y, v > 0.0f ? juce::Colour (255, 255, 255).withAlpha (a)
                                                      : juce::Colour (0, 0, 0).withAlpha (a));
                }

            return img;
        }();

        return tile;
    }
}

bool BmoLookAndFeel::materialEnabled() { return material::enabled(); }

void BmoLookAndFeel::paintPlateMaterial (juce::Graphics& g, juce::Rectangle<int> area)
{
    if (! material::enabled())
        return;

    const auto r = area.toFloat();

    // Light from above: the plate is a hair lighter at the top than at the
    // bottom. 4 % either way, which is under a third of the step between
    // plate and plateEdge.
    g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (0.05f), r.getX(), r.getY(),
                                             juce::Colours::black.withAlpha (0.04f), r.getX(), r.getBottom(),
                                             false));
    g.fillRect (r);

    g.setTiledImageFill (material::brushed() ? material::brushedTile() : material::grainTile(), 0, 0, 1.0f);
    g.fillRect (r);

    // A machined edge: one lit line along the top, one shaded along the
    // bottom. In a rack this is also what separates one module from the next.
    g.setColour (juce::Colours::white.withAlpha (0.35f));
    g.fillRect (r.withHeight (1.0f));
    g.setColour (juce::Colours::black.withAlpha (0.18f));
    g.fillRect (r.withTop (r.getBottom() - 1.0f));
    g.fillRect (r.withLeft (r.getRight() - 1.0f));
}

void BmoLookAndFeel::fillEngraved (juce::Graphics& g, const juce::RectangleList<float>& marks, juce::Colour ink)
{
    if (! material::enabled())
    {
        g.setColour (ink);
        g.fillRectList (marks);
        return;
    }

    // A laser-cut channel, lit from above: the lip below and to the right of
    // the cut catches the light, the wall above and to the left is in shade,
    // and the ink sits in the channel. The three are separate lists filled
    // once each, so crossings -- a bus's ticks on its spine -- are not laid
    // down twice.
    auto shifted = [&marks] (float dx, float dy)
    {
        auto copy = marks;
        copy.offsetAll (dx, dy);
        return copy;
    };

    g.setColour (juce::Colours::white.withAlpha (0.55f));
    g.fillRectList (shifted (0.6f, 0.9f));

    g.setColour (juce::Colours::black.withAlpha (0.30f));
    g.fillRectList (shifted (-0.5f, -0.7f));

    g.setColour (ink);
    g.fillRectList (marks);
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

        // A knob whose parameter is a *position* draws its positions and
        // nothing else: no dotted arc, no rest dot, no minus and plus. See
        // Knob::setStepMarks. Everything below this block is the continuum
        // treatment and is skipped wholesale rather than partly suppressed,
        // because a step-marked face that still carried a plus would be
        // promising both things at once.
        if (const auto steps = knob != nullptr ? knob->getStepMarks() : 0; steps > 1)
        {
            // Radially, like the meter's own scale, and struck from the track
            // inwards so the marks sit in the same ring the dots would have.
            // The pointer reaches kPointerReach of the face, so these stop
            // clear of it and read as a scale around the knob rather than as
            // teeth on it.
            const auto labelEvery = knob->getStepLabelEvery();

            // **Step marks ride closer to the face than the dotted track.**
            //
            // Tokens::trackGap is the room a *ring of dots* wants, and a ring
            // reads as its own object at that distance. A scale does not: it
            // belongs to the knob it numbers, and at the dotted radius around
            // a small face it floats away from it. Pulling it in also buys the
            // numbers their room -- the component has to hold face, marks and
            // numerals, and every pixel the ring gives up is one the numbers
            // can have.
            const auto stepTrack = radius + 6.0f;

            // A numbered mark is struck inwards only, so the number can sit
            // just outside the ring without the tick reaching up into it. An
            // unnumbered one is centred on the ring and stays short -- the two
            // lengths are what let a reader count from 1 to 3 without a number
            // on 2.
            juce::Path marks;

            for (int i = 0; i < steps; ++i)
            {
                const auto fraction = (float) i / (float) (steps - 1);
                const auto angle    = startAngle + fraction * (endAngle - startAngle);
                const auto numbered = labelEvery > 0 && i % labelEvery == 0;

                marks.startNewSubPath (at (angle, stepTrack - (numbered ? 4.5f : 3.0f)));
                marks.lineTo          (at (angle, stepTrack + (numbered ? 1.5f : 3.0f)));
            }

            g.setColour (dim (accent.withAlpha (enabled ? 0.55f : 0.2f)));
            g.strokePath (marks, juce::PathStrokeType (1.8f));

            if (labelEvery > 0)
            {
                // The numbers are the module's own colour at full strength
                // rather than the marks' 0.55, because they are read and the
                // marks are only counted. Same argument the meter's scale
                // makes: colour marks the system, contrast does the reading.
                const auto font = labelFont (9.0f);
                g.setColour (dim (accent));
                g.setFont (font);

                for (int i = 0; i < steps; i += labelEvery)
                {
                    const auto fraction = (float) i / (float) (steps - 1);
                    const auto angle    = startAngle + fraction * (endAngle - startAngle);

                    // Centred on a box out beyond the ring. 11 px of reach is
                    // the mark's own 1.5 of overshoot, a clear gap, and half a
                    // digit -- so a numeral sits off the tick rather than on
                    // its end.
                    const auto where = at (angle, stepTrack + 8.0f);

                    g.drawText (juce::String (i + 1),
                                juce::Rectangle<float> (13.0f, 9.0f).withCentre (where),
                                juce::Justification::centred, false);
                }
            }
        }
        else
        {

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
    }

    if (! material::enabled())
    {
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
        return;
    }

    // PROTOTYPE material knob, in two forms under review: a skirt with a grip
    // and a cap on it, or (BMO_MATERIAL=...cap) a one-piece cap with a
    // chamfered rim. One light from above either way. The cap's centre is the
    // token face, flat, so every ratio measured against `face` still holds
    // where the pointer is read.
    {
        const auto onePiece = material::capKnob();
        const auto capR = onePiece ? radius : radius * 0.80f;
        const auto capBox = juce::Rectangle<float> (capR * 2.0f, capR * 2.0f).withCentre (centre);

        // Everything but the grip and the pointer is the same every time
        // this knob paints, so it is drawn once per (size, pixel scale,
        // colours) and blitted after. A knob repaints on every step of a
        // drag, and at 30 Hz of automation; the layers below are most of the
        // cost of a material knob and none of what moves.
        const auto edgeColour = character ? t.outline : t.knobEdge;
        const auto pixelScale = g.getInternalContext().getPhysicalPixelScaleFactor();
        const auto pad = radius * 0.30f;
        // Snapped to whole device pixels, so the blit below is a straight
        // copy rather than a resample.
        const auto snap = [pixelScale] (float v) { return std::floor (v * pixelScale) / pixelScale; };
        const auto rawArea = faceBox.expanded (pad).withY (faceBox.getY() - pad);
        const auto area = rawArea.withPosition (snap (rawArea.getX()), snap (rawArea.getY()));
        const auto key = juce::String (juce::roundToInt (radius * 4.0f)) + "/" + juce::String (pixelScale, 3) + "/"
                       + face.toString() + "/" + edgeColour.toString() + "/" + (enabled ? "1" : "0");

        static std::map<juce::String, juce::Image> cache;

        auto found = cache.find (key);

        if (found == cache.end())
        {
            if (cache.size() > 256)
                cache.clear();

            const auto w = juce::jmax (1, juce::roundToInt (area.getWidth()  * pixelScale) + 2);
            const auto h = juce::jmax (1, juce::roundToInt (area.getHeight() * pixelScale) + 2);
            juce::Image layer (juce::Image::ARGB, w, h, true);

            {
                juce::Graphics lg (layer);
                lg.addTransform (juce::AffineTransform::translation (-area.getX(), -area.getY())
                                     .scaled (pixelScale));
                // Contact shadow. A radial gradient, not a blurred image: one fill,
                // no allocation, and it scales with the editor like everything else.
                {
                    const auto sc = centre.translated (0.0f, radius * 0.14f);
                    juce::ColourGradient shadow (juce::Colours::black.withAlpha (enabled ? 0.30f : 0.12f), sc.x, sc.y,
                                                 juce::Colours::transparentBlack, sc.x + radius * 1.16f, sc.y, true);
                    shadow.addColour (0.72, juce::Colours::black.withAlpha (enabled ? 0.20f : 0.08f));
                    lg.setGradientFill (shadow);
                    lg.fillEllipse (juce::Rectangle<float> (radius * 2.32f, radius * 2.32f).withCentre (sc));
                }

                // Skirt: the face a step down, lit from the top.
                if (! onePiece)
                {
                    const auto skirt = face.interpolatedWith (t.knobEdge, 0.35f);
                    lg.setGradientFill (juce::ColourGradient (dim (skirt.brighter (0.25f)), centre.x, faceBox.getY(),
                                                             dim (skirt.darker (0.45f)), centre.x, faceBox.getBottom(), false));
                    lg.fillEllipse (faceBox);

                    lg.setColour (dim (character ? t.outline : t.knobEdge).withMultipliedAlpha (0.9f));
                    lg.drawEllipse (faceBox.reduced (0.5f), 1.0f);
                }

                // Cap: the token face, with a soft sheen off the top-left and a
                // bevel -- lit rim above, shaded rim below.
                lg.setColour (dim (face));
                lg.fillEllipse (capBox);

                {
                    const auto hc = centre.translated (-capR * 0.35f, -capR * 0.45f);
                    juce::ColourGradient sheen (juce::Colours::white.withAlpha (enabled ? 0.30f : 0.10f), hc.x, hc.y,
                                                juce::Colours::white.withAlpha (0.0f), hc.x + capR * 1.1f, hc.y, true);
                    lg.setGradientFill (sheen);
                    lg.fillEllipse (capBox);
                }

                lg.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (enabled ? 0.70f : 0.25f), centre.x, capBox.getY(),
                                                         juce::Colours::black.withAlpha (enabled ? 0.30f : 0.10f), centre.x, capBox.getBottom(), false));
                lg.drawEllipse (capBox.reduced (0.6f), 1.2f);

                if (onePiece)
                {
                    // The chamfer: a band round the rim, lit on top and shaded
                    // below, and the knob's edge outside it.
                    const auto band = capR * 0.12f;
                    lg.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (enabled ? 0.35f : 0.12f), centre.x, capBox.getY(),
                                                             juce::Colours::black.withAlpha (enabled ? 0.22f : 0.08f), centre.x, capBox.getBottom(), false));
                    lg.drawEllipse (capBox.reduced (band * 0.5f + 1.0f), band);

                    lg.setColour (dim (edgeColour).withMultipliedAlpha (0.9f));
                    lg.drawEllipse (capBox.reduced (0.5f), 1.0f);
                }
            }

            found = cache.emplace (key, std::move (layer)).first;
        }

        const auto& layer = found->second;
        g.setImageResamplingQuality (juce::Graphics::lowResamplingQuality);
        g.drawImage (layer, juce::Rectangle<float> ((float) layer.getWidth() / pixelScale,
                                                    (float) layer.getHeight() / pixelScale)
                                .withPosition (area.getPosition()));

        // Grip: fine flutes on the skirt that turn with the knob, so the knob
        // reads as turning even where the pointer is under a finger.
        if (! onePiece)
        {
            juce::Path flutes;
            constexpr int count = 36;

            for (int i = 0; i < count; ++i)
            {
                const auto a = angle + juce::MathConstants<float>::twoPi * (float) i / (float) count;
                flutes.startNewSubPath (at (a, capR + 1.2f));
                flutes.lineTo          (at (a, radius - 1.0f));
            }

            g.setColour (juce::Colours::black.withAlpha (enabled ? 0.16f : 0.06f));
            g.strokePath (flutes, juce::PathStrokeType (1.0f));
        }

        // Pointer: the same ink, sitting in an engraved groove -- a dark line
        // a pixel wider underneath it. On the pale caps the pointer is white
        // at 1.39-1.49:1 by Frosty's call; the groove is what lets a white
        // line read on a pale cap without changing that call.
        const auto tip  = onePiece ? capR * 0.86f - 1.0f : capR - 2.5f;
        const auto tail = capR * 0.18f;
        const juce::Line<float> line { at (angle, tail), at (angle, tip) };

        g.setColour (juce::Colours::black.withAlpha (enabled ? 0.38f : 0.12f));
        g.drawLine (line, 4.4f);
        g.setColour (dim (pointerInk));
        g.drawLine (line, 2.4f);
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

    if (material::enabled())
    {
        // PROTOTYPE material switch: raised when off, sunk and lit when on,
        // so on and off differ in form as well as in hue. The gradient is
        // +/-6 % about the fill, so the ink derived from the fill below
        // still holds at the label's middle.
        if (! on)
        {
            g.setColour (juce::Colours::black.withAlpha (0.22f));
            g.fillRoundedRectangle (bounds.translated (0.0f, 1.2f), Tokens::corner);
        }

        const auto top    = on ? fill.darker (0.10f) : fill.brighter (0.10f);
        const auto bottom = on ? fill.brighter (0.06f) : fill.darker (0.10f);

        g.setGradientFill (juce::ColourGradient (top, 0.0f, bounds.getY(), bottom, 0.0f, bounds.getBottom(), false));
        g.fillRoundedRectangle (bounds, Tokens::corner);

        const auto edge = bounds.reduced (0.5f);

        if (on)
        {
            // An inner shadow along the top edge: the key is pressed in.
            g.setColour (juce::Colours::black.withAlpha (0.28f));
            g.fillRoundedRectangle (edge.withHeight (1.6f), 0.8f);
        }
        else
        {
            g.setColour (juce::Colours::white.withAlpha (0.45f));
            g.fillRoundedRectangle (edge.withHeight (1.0f).reduced (1.5f, 0.0f), 0.5f);
        }

        g.setColour (juce::Colours::black.withAlpha (0.25f));
        g.drawRoundedRectangle (edge, Tokens::corner, 1.0f);
    }
    else
    {
        g.setColour (fill);
        g.fillRoundedRectangle (bounds, Tokens::corner);
    }

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
