#pragma once

#include "Tokens.h"
#include "Fonts.h"

namespace bmo::ui
{

/** A rotary control drawn as a potentiometer: a face and a pointer.

    No value readout of any kind -- a plus one side, a minus the other where
    there is something to subtract, and nothing else. Numbers make people mix
    with their eyes, hunting a tidy figure and flinching from a large move.
*/
class Knob : public juce::Slider
{
public:
    enum class Style
    {
        utility,    ///< pale blue face, dotted track. Input, output, gain.
        character,  ///< the module's accent: a band's gain, drive, tone.
        filter,     ///< blue face, legend around it, no track. The cut filters.
        ring        ///< the white selector ring a band's gain sits inside.
    };

    Knob() : juce::Slider (juce::Slider::RotaryVerticalDrag, juce::Slider::NoTextBox) {}

    void setStyle (Style s) noexcept   { style = s; }
    Style getStyle() const noexcept    { return style; }

    /** The module's colour, for the character style. */
    void setAccent (juce::Colour c) noexcept { accent = c; }
    juce::Colour getAccent() const noexcept  { return accent; }

    /** What a *utility* knob draws in, when it should not be the suite azure.

        Transparent by default, meaning `tokens().track` -- which is what a
        trim knob has always used and what every INPUT and OUTPUT still uses.

        It exists for a drawer: LTV Comp reveals five trim knobs behind its
        COMPLEX switch, and they take the colour of the switch that revealed
        them, so the drawer reads as one thing rather than as five controls
        that happen to have turned up. Set it and the caption follows. */
    void setUtilityTint (juce::Colour c) noexcept { utilityTint = c; }
    juce::Colour getUtilityTint() const noexcept  { return utilityTint; }

    void setDetents (int count) noexcept { detents = count; }
    int  getDetents() const noexcept     { return detents; }

    void setFaceScale (float s) noexcept { faceScale = s; }
    float getFaceScale() const noexcept  { return faceScale; }

    /** Where the dotted track sits, in pixels from the centre. Zero means
        "just outside my own face". A band's gain has to clear the ring drawn
        around it, and the face inside knows nothing about the ring's size. */
    void setTrackRadius (float r) noexcept { trackRadius = r; }
    float getTrackRadius() const noexcept  { return trackRadius; }

    /** Whether the heavy dot marking the rest position is drawn at all.

        On everywhere by default, and the drawing already drops it where it
        would fuse with the plus or the minus at an end of the sweep. But that
        test is a *pixel* clearance converted to an angle, so it depends on the
        track radius: a control resting at an end loses its dot on a small knob
        and keeps it on a large one, where the same gap subtends less arc. BMO
        Tune RT's RETUNE is the case -- at face 96 the dot was suppressed, and
        growing the face to 112 brought it back as what reads as a doubled
        minus.

        Off means no rest mark at any size. For a control whose default *is* an
        end of its range, the pointer already says so when the panel opens.
        Frosty, 2026-09-16. */
    void setRestMark (bool b) noexcept { restMark = b; }
    bool hasRestMark() const noexcept  { return restMark; }

    /** What the two ends of the dotted track say.

        `lessMore` is the suite's minus and plus, and it is right for every
        control that runs from less of something to more of it. `leftRight` is
        for a control whose ends are two *directions* rather than two amounts --
        BMO Dimension's ROTATE and ASYM, which move the image left or right,
        where neither end is more than the other. */
    enum class EndMarks { lessMore, leftRight };

    void setEndMarks (EndMarks m) noexcept { endMarks = m; }
    EndMarks getEndMarks() const noexcept  { return endMarks; }

    /** The inner control of a concentric pair claims only its own circle, so
        the ring around it stays grabbable right up to the corners. */
    void setCircularHitTest (bool b) noexcept { circularHit = b; }

    bool hitTest (int x, int y) override
    {
        if (! circularHit)
            return juce::Slider::hitTest (x, y);

        const auto centre = getLocalBounds().toFloat().getCentre();
        const auto radius = (float) juce::jmin (getWidth(), getHeight()) * 0.5f * faceScale;

        return juce::Point<float> ((float) x, (float) y).getDistanceFrom (centre) <= radius + 4.0f;
    }

private:
    Style style = Style::utility;
    juce::Colour accent { tokens().accent };
    juce::Colour utilityTint;
    bool  circularHit = false;
    bool  restMark = true;
    EndMarks endMarks = EndMarks::lessMore;
    int   detents = 0;
    float faceScale = 1.0f;
    float trackRadius = 0.0f;
};

//==============================================================================
class BmoLookAndFeel final : public juce::LookAndFeel_V4
{
public:
    BmoLookAndFeel() { refreshColours(); }

    /** Re-reads the tokens. Call after a theme change. */
    void refreshColours();

    void drawRotarySlider (juce::Graphics&, int x, int y, int width, int height,
                           float sliderPos, float startAngle, float endAngle,
                           juce::Slider&) override;

    void drawToggleButton (juce::Graphics&, juce::ToggleButton&,
                           bool shouldDrawHighlighted, bool shouldDrawDown) override;

    juce::Font getLabelFont (juce::Label&) override;

    /** The preset strip is built from TextButtons, and it is panel text like
        any other. The popup list of preset names is not: a heavy display face
        makes a list of names slower to read, so that keeps the system font. */
    juce::Font getTextButtonFont (juce::TextButton&, int buttonHeight) override;

    /** The box a toggle's label is drawn in, and the face it is set in.

        One definition each, read by `drawToggleButton` and by the layout test
        that asserts the label fits. Same discipline as
        `PlainKnob::captionOverflow`: a fit test that derived the box its own
        way could agree with the very bug it exists to catch. */
    static juce::Rectangle<float> toggleLabelBox (const juce::ToggleButton&);
    static juce::Font toggleLabelFont (const juce::ToggleButton&);

    /** A property a switch may set on itself to pin its label's point size
        instead of having it derived from its height. See `toggleLabelFont`,
        and `SwitchButton::setLabelSize`, which is how a panel asks for it. */
    static constexpr const char* kSwitchLabelSize = "bmoSwitchLabelSize";

    /** How much wider a toggle's label is than its box, in pixels; zero or
        less fits.

        A polarity switch is measured as what is actually drawn -- the slashed
        circle is a path rather than a glyph, so its diameter is counted and
        whatever is set beside it is added. */
    static float toggleLabelOverflow (const juce::ToggleButton&);

    /** A ring of dots, used for the track around a gain control. */
    static void drawDottedArc (juce::Graphics&, juce::Point<float> centre, float radius,
                               float startAngle, float endAngle, juce::Colour, float dotSize);

    /** The slashed O of a polarity switch. */
    static const juce::String& phaseGlyph();
};

} // namespace bmo::ui
