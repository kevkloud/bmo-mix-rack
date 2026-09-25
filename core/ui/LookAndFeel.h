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

    /** Draw this many discrete tick marks around the track instead of the
        dotted arc and its two end symbols. Zero, the default, is the dotted
        arc every knob in the suite has always drawn.

        For a knob whose parameter **is** a position rather than an amount.
        BMO FET's ATTACK and RELEASE are 1..7, seven detents on the hardware,
        and the value line under them reads "4 (126 us)" -- so the face should
        say seven places, not a continuum with less at one end and more at the
        other. The dotted arc and the minus/plus are exactly the wrong promise
        there, which is why this replaces them rather than adding to them.

        The marks are laid across the same sweep the pointer travels, so the
        pointer lands on one at every whole position.

        `labelEvery` numbers every nth mark, counting the first: 2 over seven
        marks prints 1, 3, 5, 7, which is how a detented faceplate is marked --
        enough to count from without a number against every tooth. 0 draws the
        marks bare. An odd count with a stride of 2 numbers both ends, which is
        the arrangement worth having; nothing stops an even one, it just leaves
        the last mark unnumbered. */
    void setStepMarks (int count, int labelEvery = 0) noexcept
    {
        stepMarks      = juce::jmax (0, count);
        stepLabelEvery = juce::jmax (0, labelEvery);
    }

    int getStepMarks() const noexcept      { return stepMarks; }
    int getStepLabelEvery() const noexcept { return stepLabelEvery; }

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
    int   stepMarks = 0;        ///< see setStepMarks; 0 is the dotted arc
    int   stepLabelEvery = 0;   ///< number every nth mark; 0 draws them bare
    float faceScale = 1.0f;
    float trackRadius = 0.0f;
};

//==============================================================================
class BmoLookAndFeel final : public juce::LookAndFeel_V4
{
public:
    BmoLookAndFeel() { refreshColours(); }

    /** PROTOTYPE: whether the material pass is on (BMO_MATERIAL set), and
        the plate treatment it adds. See docs/ui-material-proposal.md. */
    static bool materialEnabled();
    static void paintPlateMaterial (juce::Graphics&, juce::Rectangle<int> area);

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

    /** A dropdown's closed text is panel text, so it is set in the panel face
        -- the same call and the same size `getTextButtonFont` uses, because a
        `ui::ChoiceBox` and the preset strip's buttons are the same 26 px row.

        Without this a choice reads in JUCE's default sans while everything
        around it is Minerva, which on a render looks like a control belonging
        to some other program. **The open menu is deliberately left alone**:
        `getPopupMenuFont` keeps the system font for the reason above, and a
        six-name type list is not worth splitting that decision over. */
    juce::Font getComboBoxFont (juce::ComboBox&) override;

    /** Where a dropdown's closed text is drawn.

        One definition, read by `positionComboBoxText` and by
        `comboTextOverflow` -- the discipline `toggleLabelBox` is under, and for
        the reason stated there: a fit test that measured the box its own way
        could agree with the very clip it exists to catch. The width is JUCE's
        own, `width - 30`, which is what LookAndFeel_V4's arrow zone leaves. */
    static juce::Rectangle<int> comboTextBox (const juce::ComboBox&);
    void positionComboBoxText (juce::ComboBox&, juce::Label&) override;

    /** How much wider a dropdown's **widest item** is than the room it has, in
        pixels; zero or less fits.

        Every item, not the one selected: a list that fits at "Room" and clips
        at "Chamber" renders perfectly until somebody turns it, which is how
        MAKEUP survived a whole release. */
    static float comboTextOverflow (const juce::ComboBox&);

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
