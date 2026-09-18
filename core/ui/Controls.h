#pragma once

#include "LookAndFeel.h"
#include "core/state/ParamSpec.h"
#include <juce_audio_processors/juce_audio_processors.h>
#include <functional>
#include <limits>
#include <vector>

namespace bmo::ui
{

/** A knob with its name underneath and nothing else: no number, only a minus
    at one end of its track and a plus at the other. They mean less and more,
    not negative and positive, so both ends carry one whatever the parameter's
    range is -- and a heavy dot marks where the control rests, unless that is
    an end a symbol already marks. */
class PlainKnob final : public juce::Component
{
public:
    /** Leave `captionColour` alone and the caption is derived from `accent`
        against the current plate -- `accentTextOn`, so it reads at 4.5:1 and
        it is the module's own colour.

        Until 0.2.2 it defaulted to the shared track azure, which put every
        caption in the suite at 1.95:1 and, worse, put INPUT and DRIVE in blue
        underneath an orange knob. BMO Opto had already worked around both by
        hardcoding its own hex. Pass a colour here only to override that. */
    PlainKnob (juce::RangedAudioParameter&, const juce::String& caption,
               Knob::Style style = Knob::Style::utility, float faceScale = 0.5f,
               juce::Colour accent = tokens().accent,
               juce::Colour captionColour = {});

    void paint (juce::Graphics&) override;
    void resized() override;

    void setKnobEnabled (bool);

    /** The colour a *utility* knob draws in, overriding the suite azure.
        Forwards to Knob::setUtilityTint; see it for why. */
    void setUtilityTint (juce::Colour);

    /** Whether the rest-position dot is drawn at all.
        Forwards to Knob::setRestMark; see it for why. */
    void setRestMark (bool);

    /** Draws the caption this many pixels higher, into the air a knob carries
        under its face.

        A caption hangs off the knob's *box*, and the box is square while the
        face is drawn at `faceScale` of it, so there is always a gap between the
        two. Closing it by shrinking the box would shrink the knob with it --
        the box is bound by its height on every panel in the suite. This moves
        the name alone, and the value line follows it up.

        Zero is what every knob laid out as before this existed. */
    void setCaptionLift (int pixels);

    /** L and R at the ends of the track instead of minus and plus.
        Forwards to Knob::setEndMarks; see it for why. */
    void setEndMarks (Knob::EndMarks);

    /** Re-colours the knob and, unless a caption colour was passed in, its
        caption with it. For a module whose colour depends on its own state --
        BMO Opto runs greyscale in Tele and lavender in Stressed -- rather than
        on which module it is. */
    void setAccent (juce::Colour);

    /** Caps how wide the knob itself may draw, leaving the rest of the
        component's width to the caption underneath.

        Without this a long caption can only be given room by widening the
        whole control, which widens the knob with it. BMO Opto's "MAKEUP" is
        the case that forced it: at the 92 px the knob wants, the caption
        clipped to "MAKEU". The knob is laid out square and centred, so the
        drawn radius -- jmin(width, height) -- is unchanged by a wider
        component once the side is capped. */
    void setKnobSide (int maxSide);

    /** How much wider the caption is than the room it is drawn in, in pixels.
        Zero or less fits; anything above it is clipped on the panel.

        Here rather than in the test that asserts on it, because it has to use
        the same box and the same font `paint` does. BMO Opto's MAKEUP drew as
        MAKEU for a full release -- a five-character overflow nobody saw -- and
        a test that measured it its own way could have agreed with the bug. */
    float captionOverflow() const;

    /** Point size for the name under the knob. 15 unless set.

        The row the name is drawn in follows it, and the name is drawn against
        the knob's own bottom edge rather than the component's, so shrinking
        the type leaves the label as close to the knob as it was rather than
        stranding it at the foot of the cell. */
    void setCaptionSize (float points);

    /** Prints the parameter's value under its name -- "2.10 kHz", "-2.0 dB".

        Off by default, and every module but BMO DEQ leaves it off: the suite's
        knobs say less and more, not how much (the class comment). DEQ is a
        parametric EQ with thirteen continuous controls a band, and Frosty's
        call (2026-09-11) is that they need numbers. The text is the host's own
        -- getCurrentValueAsText -- so it reads the same standalone and in a
        rack slot, and cannot disagree with the automation lane. */
    void setShowsValue (bool shouldShow);
    bool isShowingValue() const noexcept { return showsValue; }

    /** The cap's share of the knob's side, as the constructor's faceScale. The
        dotted track sits a fixed Tokens::trackGap outside the cap, so a small
        knob at a large scale runs its track off its own edge and is clipped;
        a panel that sizes knobs at layout time sets this with the side. */
    void setFaceScale (float scale)  { knob.setFaceScale (scale); knob.repaint(); }

    /** Rewrites the host's text before it is drawn -- a narrow panel's
        "2.10k" for "2.10 kHz". Paint only; the host, the automation lane and
        typed entry keep the full text. Measured by captionOverflow like the
        rest, so a format cannot hide an overflow. Empty restores the host's. */
    void setValueFormat (std::function<juce::String (const juce::String&)> format);

private:
    juce::String valueText (const juce::String& hostText) const;
    /** Room under the knob for its name, at the current caption size, and for
        the value under that when one is shown.

        1.2 x the point size plus four, which is the 22 px row a 15 pt caption
        had when the number was fixed -- so a knob that never sets a size lays
        out exactly as it did. */
    int captionRow() const { return juce::roundToInt (captionSize * 1.2f) + 4 + (showsValue ? valueRow() : 0); }
    int valueRow() const   { return juce::roundToInt (kValueSize * 1.2f) + 1; }

    /** The box the value is drawn in; empty when none is shown. */
    juce::Rectangle<int> valueBox() const;

    static constexpr float kValueSize = 11.0f;

    /** The box the caption is drawn in. One definition, read by `paint` and by
        `captionOverflow`, so the drawing and the assertion cannot disagree. */
    juce::Rectangle<int> captionBox() const;

    juce::String caption;
    juce::Colour captionColour;   ///< transparent means "derive from accentColour"
    juce::Colour accentColour;
    int knobSide = std::numeric_limits<int>::max();
    float captionSize = 15.0f;
    int captionLift = 0;
    bool showsValue = false;
    std::function<juce::String (const juce::String&)> valueFormat;
    juce::RangedAudioParameter& parameter;
    Knob knob;
    std::unique_ptr<juce::SliderParameterAttachment> attachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PlainKnob)
};

//==============================================================================
/** A band, or a filter.

    With a gain parameter it is a band: the selector is the white ring,
    legended with its switch positions, and the cut and boost sits inside it.
    Grab the ring for the selector, the middle for gain. Without one it is a
    filter: a single knob with the same legend around it.

    The legend comes from the selector's spec, so it is right in the rack too,
    where the parameter object underneath is a generic slot parameter.
*/
class ConcentricBand final : public juce::Component
{
public:
    /** `outsetFan` runs this band's frequency fan just past 12 and 6 o'clock
        instead of stopping just short of them. Alternate it down a panel: see
        the constructor. */
    ConcentricBand (juce::RangedAudioParameter& selector, const ParamSpec& selectorSpec,
                    juce::RangedAudioParameter* gain, juce::Colour accent = tokens().accent,
                    bool outsetFan = false);

    void paint (juce::Graphics&) override;
    void resized() override;

    void setRingEnabled (bool);

    /** Replaces the legend read off the selector's spec, one label a position.

        For a selector whose choices are not frequencies: BMO DEQ's band shape
        is "Bell", "Low Shelf" and so on, which the host shows in full and a
        38 px legend box cannot, so its panel hands in BELL, LS, HS, LC, HC.
        The host's names are untouched; this is paint only. */
    void setLegend (const juce::StringArray& labels);

    /** How far the widest legend label runs past its box, in px; <= 0 fits.
        For layout tests, like PlainKnob::captionOverflow. */
    float legendOverflow() const;

    /** Draws the dial and its legend this far right of the cell's own centre.

        For a band whose cell has been cut short on one side to keep clear of
        something beside it, while the dial itself should stay where it was. BMO
        CEQ's mid bell does exactly that: HI-Q sits in the margin to its right,
        the cell stops short of it so that neither takes the other's clicks, and
        the dial is pushed back onto the panel's centre line with its
        neighbours. Zero is what every other band draws at. */
    void setDialOffset (int dx);

    /** How far the dial's ink reaches either side of its centre: the legend's
        own outer limit plus half a label box, so it holds wherever the widest
        label is drawn.

        A band is a circle in a letterbox -- its radius comes off the cell's
        height, so there is bare plate either side of it. A panel that wants to
        put something in that plate needs this to place it against the dial
        rather than against the cell's edge. BMO CEQ puts HI-Q there, beside the
        mid bell it belongs to. */
    int inkHalfWidth() const;

private:
    /** How far a band's fan stops short of 12 and 6 o'clock -- or runs past
        them, when the band is outset. 15 degrees. */
    static constexpr float kFanNudge = 0.2618f;

    /** Frequency legend type: size when selected, when not, and which face. */
    static constexpr float kPointSize         = 9.9f;
    static constexpr float kPointSizeIdle     = 9.0f;
    static constexpr bool  kPointUsesCaption  = false;

    /** The box a legend label is drawn into. */
    static constexpr float kLegendBoxWidth  = 38.0f;
    static constexpr float kLegendBoxHeight = 15.0f;

    /** Where the dial and its legend sit inside the cell.

        `shift` is how far down the whole assembly is nudged. It is zero for a
        band, whose legend is symmetric about the dial. A filter's is not: its
        positions run around a full circle with the last one blank, and that
        blank falls at the foot, so there is ink above the dial with no
        counterpart below it. Centring the dial in the cell then reads as the
        dial sitting high, because what the eye centres is the ink. The shift
        is half that imbalance, measured rather than guessed, so it follows if
        the number of positions or the radius ever changes. */
    struct Geometry
    {
        float ringRadius;
        float textRadius;   ///< a band's single legend radius; unused by a filter
        float maxRadius;    ///< how far out anything may go before it clips
        int shift;
    };
    Geometry geometry() const;

    /** The radius one of a filter's legend labels sits at.

        Per label, not one radius for all of them, because what the eye
        measures is the gap to the nearest **ink** and every label presents a
        different part of itself to the dial. A label at twelve o'clock shows a
        flat edge; one on a diagonal shows a corner, and a wide word pushes
        that corner further out still. On one shared radius BMO EQ's low cut
        cleared 6.5 px at 45 and 4.5 at 360, which reads as uneven because it
        is.

        So: the knob edge, the gap, and then however far this particular
        label's own box reaches back along its own ray. Measured at the
        selected point size in both states, so a label does not move when you
        switch onto it. */
    float filterLabelRadius (float angle, const juce::String& text,
                             float ringRadius, float maxRadius) const;

    Knob ring, centre;
    std::unique_ptr<juce::SliderParameterAttachment> ringAttachment, centreAttachment;

    juce::StringArray legend;
    juce::Colour accentColour;
    bool hasCentre = false, ringEnabled = true;
    int dialOffset = 0;   ///< see setDialOffset

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ConcentricBand)
};

//==============================================================================
class SwitchButton final : public juce::Component
{
public:
    /** `tint` is what the switch lights up in, and it is required rather than
        defaulted. What a switch lights up in is not a free choice -- the table
        in modules/AGENTS.md gives it: the module's accent for a bypass or for
        mono, `polarity` for a polarity flip, `switchAlt` for anything else. A
        default argument here quietly said otherwise, and the colour it
        defaulted to was `switchOn`, a second name for BMO EQ's pink that no
        call site had used since before 0.2.2. */
    SwitchButton (juce::RangedAudioParameter&, const juce::String& text,
                  juce::Colour tint);

    void resized() override;
    void setSwitchEnabled (bool);

    /** What the switch lights up in. See PlainKnob::setAccent. */
    void setTint (juce::Colour);

    /** The label to use while engaged, instead of one derived from the fill.

        For a switch whose fill is the same in every module -- polarity, which
        is always white -- so the label is what says which module it belongs
        to. Pass the module's accent: it is stepped against the fill here, not
        against the plate, so it stays dark on a white switch whatever the
        plate underneath is doing. */
    void setActiveInkFrom (juce::Colour accent);

    /** Sets the label at a fixed point size instead of one derived from the
        switch's height.

        Only a switch that is not the suite's own shape needs this. The derived
        size is 62% of the box, which is right while every switch is 70 x 26 and
        wrong for a square one: the label grows with the height and outgrows the
        width, so "HI-Q" overflows a square switch at every size. BMO CEQ's
        HI-Q, which sits square in the margin beside the mid bell, pins the size
        the 26 px row uses so it sets like its neighbours. */
    void setLabelSize (float points);

    /** Drawn engaged and not clickable, for a control the DSP holds on
        regardless of its parameter.

        Not the same as `setSwitchEnabled (false)`, and the difference is the
        whole point. A disabled switch is dimmed and still draws whatever its
        parameter says, so BMO Opto's COLOR spent 0.2.1 and 0.2.2 telling you
        colour was *off* in Tele while DspCore had it on -- and telling you at
        1.27:1, because the disabled alpha collapses the fill and its ink
        toward the plate together. Locked draws the switch at full strength in
        the state the DSP is actually in.

        The state is asserted here rather than written to the parameter: the
        parameter still holds what the user set for the mode where it counts,
        and gets it back the moment the lock lifts. */
    void setLockedOn (bool);

    /** True while setLockedOn(true) is holding the switch engaged. */
    bool isLockedOn() const noexcept { return locked; }

    /** How much wider the label is than the room it has, in pixels; zero or
        less fits. The knob-caption equivalent is PlainKnob::captionOverflow,
        and this exists for the same reason: MAKEUP clipped to MAKEU for a
        whole release because nothing measured it. A switch's text is drawn by
        the look and feel, so the measurement lives there too. */
    float labelOverflow() const { return BmoLookAndFeel::toggleLabelOverflow (button); }

    /** Set the drawn state without writing to the parameter. For restoring a
        switch to what its parameter says after a lock lifts. */
    void setToggleStateSilently (bool);

private:
    bool locked = false;


    juce::ToggleButton button;
    std::unique_ptr<juce::ButtonParameterAttachment> attachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SwitchButton)
};

//==============================================================================
/** Output meter, switchable between peak dBFS and VU by clicking it.

    VU is not a different scale on the same number: it is an RMS reading with
    slow ballistics, 0 VU at -18 dBFS, which is why it reads weight where a peak
    meter reads headroom.
*/
class OutputMeter final : public juce::Component,
                          private juce::Timer
{
public:
    OutputMeter (std::function<float()> peakSource, std::function<float()> rmsSource);

    void paint (juce::Graphics&) override;
    void mouseUp (const juce::MouseEvent&) override;

private:
    void timerCallback() override;

    std::function<float()> peak, rms;
    float displayed = 0.0f;
    bool  vuMode = false;

    static constexpr float kVuReference = -18.0f;
    static constexpr int   kBarWidth    = 14;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (OutputMeter)
};

//==============================================================================
/** Input, output or gain-reduction, one at a time -- a horizontal needle VU
    meter with a printed scale, BMO Opto's centrepiece, and any dynamics
    module after it.

    Input and output read as VU, calibrated the same way as OutputMeter (0 VU
    = -18 dBFS, see its class comment), with the needle swept across a scale
    approximating a classic VU faceplate's non-linear spacing (compressed at
    the low end, spread out from 0 to +3) -- not derived from any one real
    meter's calibration data, just close enough to read as the genre. Gain
    reduction reads the module's own published figure directly, in dB, on a
    plain linear 0..kGrRangeDb scale.

    Mode is switched externally via setMode() -- the panel owns a row of
    labelled buttons for that (see modules/opto/panel/OptoPanel.cpp); this
    class used to cycle modes on click, which tested as unintuitive with
    nothing on screen to say what clicking would do.

    That row is also the only thing that names the current mode. This class
    printed a caption of its own under the face until 0.2.2, which said the
    same word the lit button said, three pixels below it, at 9 pt and 2.45:1
    on the pale plate. A module that gives this meter no such row needs to
    name the mode somewhere -- but every dynamics module owes its meter one
    (see modules/AGENTS.md), so there is no such module. */
class DynamicsMeter final : public juce::Component,
                            private juce::Timer
{
public:
    enum class Mode { input, output, reduction };

    /** `hotColour` marks 0 VU and above -- a classic VU meter's red zone,
        but left up to the caller since a module's own theme may want
        something other than red there.

        The face the scale is printed on is `meterFace` and is not a parameter:
        it was a caller's choice while it was a raw hex in a panel, and the
        reason given was that the face and its ink together decide whether the
        meter can be read at all -- the first cut drew a #97ddff hot zone on
        `well` #d6d6d6, 1.02:1, invisible. A token settles that once for every
        module and lets a theme move it, which a constructor argument captured
        at build time could not. */
    DynamicsMeter (std::function<float()> inputRmsSource,
                   std::function<float()> outputRmsSource,
                   std::function<float()> gainReductionDbSource,
                   Mode initialMode = Mode::output,
                   juce::Colour accent = tokens().accent,
                   juce::Colour hotColour = tokens().meterClip);

    void paint (juce::Graphics&) override;

    void setMode (Mode) noexcept;
    Mode getMode() const noexcept { return mode; }

    /** Bezel and hot-zone colour, for a module whose palette depends on its
        own state rather than on which module it is. The face stays as
        constructed: a needle meter needs a dark one whatever the mode. */
    void setColours (juce::Colour accent, juce::Colour hot) noexcept;

    /** One control point on the printed scale: a value in the mode's own unit
        (dB relative to the VU reference, or dB of gain reduction), where it
        sits across the needle's sweep, 0..1, and whether it is numbered.

        Not every tick is numbered, because a VU scale crowds hard from -3
        upwards and printing all of it there is what made the numbers
        unreadable, and the reduction scale is deliberately coarse above 12.
        Hardware faceplates do the same: every tick is struck, only the round
        ones are inked. */
    struct ScalePoint { float value; float fraction; bool numbered = true; };

    /** The two printed scales.

        Public because a scale is *painted* rather than placed, so it has no
        bounds a layout test can read -- the same reason
        `ui::ModulePanel::getRules` is public.

        It earns it here: the reduction scale's fractions are hand-placed
        rather than computed, so a typo in that table is silent and a render is
        the only thing that would show it. Monotonicity and the endpoints can
        be asserted without rendering anything. */
    static const std::vector<ScalePoint>& vuScale();
    static const std::vector<ScalePoint>& reductionScale();

private:
    void timerCallback() override;

    /** A std::vector rather than a juce::Array because the scales are written
        out as a braced list of braced pairs, and juce::Array's initialiser-list
        constructor is a template whose element type cannot be deduced from
        nested braces -- std::vector's is not, so `{ { -20.0f, 0.0f }, ... }`
        just works. */
    float fractionFor (float value, const std::vector<ScalePoint>& scale) const noexcept;

    std::function<float()> inputRms, outputRms, gainReductionDb;
    Mode mode;
    float displayed = 0.0f;

    static constexpr float kVuReference = -18.0f;
    static constexpr float kGrRangeDb   = 24.0f;
    juce::Colour accentColour, hotColour;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DynamicsMeter)
};

/** "1.6 kHz" -> "1k6", "360 Hz" -> "360", "Off" -> "OFF". A legend has to fit
    around a knob, and this is how the hardware prints it. */
juce::String compactFrequency (const juce::String& text);

} // namespace bmo::ui
