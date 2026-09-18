#pragma once

#include "core/ui/Controls.h"
#include "core/ui/Tokens.h"
#include <juce_audio_processors/juce_audio_processors.h>

namespace bmo::deq
{

//==============================================================================
/** The colour a band wears, from its placement.

    Stereo is the module accent; Mid and Side each have their own token. One
    function because three surfaces ask the same question -- the band tabs, the
    curve's nodes and the selected band's knobs -- and three copies of this
    would be three chances to disagree about what colour a band is. */
inline juce::Colour placementColour (int placeChoice, juce::Colour accent)
{
    switch (placeChoice)
    {
        case 1:  return ui::tokens().placeMid;
        case 2:  return ui::tokens().placeSide;
        default: return accent;
    }
}

//==============================================================================
/** A choice parameter as a row of switches, one lit -- or none.

    The suite has a switch for a bool (ui::SwitchButton) and a dial for a
    stepped choice (ui::ConcentricBand), and nothing for a short choice that
    reads best as words side by side. So these are plain juce::ToggleButtons
    with the same tint SwitchButton sets, drawn by the same look and feel: they
    are the suite's switches in every pixel, and only the wiring is new. A
    ParameterAttachment carries the choice, so a click is one gesture and host
    automation moves the lit one.

    **`implicitChoice` is the option that gets no button of its own.** Pass -1
    and every choice has one, which is the plain behaviour. Pass an index and
    that choice is drawn as *nothing lit*, and clicking a lit button returns to
    it.

    BMO DEQ's placement is why (Frosty, 2026-09-15). STEREO / MID / SIDE read as
    three modes to pick between, and STEREO is not a mode -- it is what a band
    does when you have not asked for anything, on a mono source as much as a
    stereo one. A button for it invites the question "which of these three am I
    in", and the honest answer is that two of them are the special cases. So the
    panel offers MID and SIDE, and neither lit is the default behaviour. */
class ChoiceRow final : public juce::Component
{
public:
    ChoiceRow (juce::RangedAudioParameter& parameter, juce::StringArray labels,
               std::function<juce::Colour (int)> tintFor,
               bool vertical = false, int implicitChoice = -1)
        : stacked (vertical), implicit (implicitChoice),
          attachment (parameter, [this] (float v) { show ((int) std::lround (v)); })
    {
        for (int i = 0; i < labels.size(); ++i)
        {
            if (i == implicit)
                continue;

            auto b = std::make_unique<juce::ToggleButton> (labels[i]);
            b->setColour (juce::ToggleButton::tickColourId,
                          tintFor ? tintFor (i) : ui::tokens().switchAlt);
            b->setClickingTogglesState (false);

            // Clicking the lit one goes back to the implicit choice, so the
            // default is reachable without a button for it. With no implicit
            // choice this is a plain radio set and a lit button ignores itself.
            b->onClick = [this, i]
            {
                const auto going = (current == i && implicit >= 0) ? implicit : i;
                attachment.setValueAsCompleteGesture ((float) going);
            };

            addAndMakeVisible (*b);
            buttons.push_back (std::move (b));
            choices.push_back (i);
        }

        attachment.sendInitialUpdate();
    }

    void resized() override
    {
        auto area = getLocalBounds();
        const auto n = (int) buttons.size();
        const auto gap = ui::Tokens::switchGap;

        for (int i = 0; i < n; ++i)
        {
            if (stacked)
            {
                const auto h = (area.getHeight() - gap * (n - 1 - i)) / (n - i);
                buttons[(size_t) i]->setBounds (area.removeFromTop (h));
                area.removeFromTop (gap);
            }
            else
            {
                const auto w = (area.getWidth() - gap * (n - 1 - i)) / (n - i);
                buttons[(size_t) i]->setBounds (area.removeFromLeft (w));
                area.removeFromLeft (gap);
            }
        }
    }

    const std::vector<std::unique_ptr<juce::ToggleButton>>& getButtons() const noexcept { return buttons; }

    /** Stacked top to bottom, or side by side. */
    void setVertical (bool shouldStack)
    {
        stacked = shouldStack;
        resized();
    }

    /** New words for the same choices -- a wider panel can afford the long
        forms. Indexed by **choice**, including any that has no button. */
    void setLabels (const juce::StringArray& labels)
    {
        for (size_t b = 0; b < buttons.size(); ++b)
            if (choices[b] < labels.size())
                buttons[b]->setButtonText (labels[choices[b]]);
    }

private:
    void show (int index)
    {
        current = index;

        for (size_t b = 0; b < buttons.size(); ++b)
            buttons[b]->setToggleState (choices[b] == index, juce::dontSendNotification);
    }

    bool stacked;
    int implicit = -1;
    int current = -1;
    std::vector<std::unique_ptr<juce::ToggleButton>> buttons;
    std::vector<int> choices;   ///< which choice each button writes
    juce::ParameterAttachment attachment;
};

//==============================================================================
/** COMPRESS and EXPAND -- what the band's dynamics actually do.

    **Not a rename of ABOVE / BELOW**, which is what it replaced on 2026-09-15,
    and the difference is the whole reason it exists. Whether a band compresses
    or expands is the direction **and the sign of RANGE together**:

    | direction | RANGE | what happens | which |
    |---|---|---|---|
    | Above | cut | loud gets quieter | compress |
    | Above | boost | loud gets louder | expand |
    | Below | cut | quiet gets quieter | expand |
    | Below | boost | quiet gets louder | compress |

    So compressing is `(dir == Above) == (RANGE < 0)`. A switch labelled ABOVE
    told you the mechanism and left you to work the rest out; this tells you the
    result, which is what anybody reaching for it actually wants.

    Pressing one writes `dir` to whichever value produces that behaviour at the
    RANGE the band is on. **Flipping RANGE's sign therefore re-lights the other
    button with nobody touching it** -- correct, and Frosty's call knowing it:
    with RANGE flipped the same `dir` genuinely is the other behaviour, and the
    old switch simply hid that.

    `dir` keeps its Above / Below choice names, so a host and every saved
    session are untouched. */
class DynamicsMode final : public juce::Component
{
public:
    DynamicsMode (juce::RangedAudioParameter& direction, std::function<float()> range,
                  juce::Colour compressTint, juce::Colour expandTint)
        : rangeDb (std::move (range)),
          attachment (direction, [this] (float v) { lastDir = v; refresh(); })
    {
        auto make = [this] (const char* text, juce::Colour tint, bool wantsCompress)
        {
            auto b = std::make_unique<juce::ToggleButton> (text);
            b->setColour (juce::ToggleButton::tickColourId, tint);
            b->setClickingTogglesState (false);
            b->onClick = [this, wantsCompress] { choose (wantsCompress); };
            addAndMakeVisible (*b);
            return b;
        };

        compress = make ("COMP", compressTint, true);
        expand   = make ("EXP",  expandTint,   false);

        attachment.sendInitialUpdate();
    }

    /** True when the band compresses at its current direction and RANGE. */
    bool isCompressing() const
    {
        const auto below = lastDir > 0.5f;
        const auto cut = (rangeDb ? rangeDb() : 0.0f) < 0.0f;
        return below != cut;   // above+cut, or below+boost
    }

    /** Re-reads RANGE and re-lights. RANGE is a knob and moves without telling
        this, so the panel's timer calls it. */
    void refresh()
    {
        const auto c = isCompressing();
        compress->setToggleState (c, juce::dontSendNotification);
        expand->setToggleState (! c, juce::dontSendNotification);
    }

    void setVertical (bool shouldStack) { stacked = shouldStack; resized(); }

    void setModeEnabled (bool e) { compress->setEnabled (e); expand->setEnabled (e); }

    void resized() override
    {
        auto area = getLocalBounds();
        const auto gap = ui::Tokens::switchGap;

        if (stacked)
        {
            compress->setBounds (area.removeFromTop ((area.getHeight() - gap) / 2));
            area.removeFromTop (gap);
            expand->setBounds (area);
        }
        else
        {
            compress->setBounds (area.removeFromLeft ((area.getWidth() - gap) / 2));
            area.removeFromLeft (gap);
            expand->setBounds (area);
        }
    }

private:
    void choose (bool wantsCompress)
    {
        // Whichever `dir` produces the asked-for behaviour at the RANGE the
        // band is on now: compressing is above+cut, or below+boost.
        const auto cut = (rangeDb ? rangeDb() : 0.0f) < 0.0f;
        const auto below = wantsCompress ? ! cut : cut;

        attachment.setValueAsCompleteGesture (below ? 1.0f : 0.0f);
    }

    bool stacked = true;
    float lastDir = 0.0f;
    std::function<float()> rangeDb;
    std::unique_ptr<juce::ToggleButton> compress, expand;
    juce::ParameterAttachment attachment;
};

//==============================================================================
/** The twelve band tabs. Selecting one is panel state, not a parameter -- it
    decides which band the controls under it are bound to, and nothing more.

    A tab shows whether its band is on (switch fill against the dark well of
    an off one) and whether its dynamics are (a dot in the gain-reduction
    azure), so which bands are doing something reads without selecting any.

    Three gestures, and between them the whole of a band's existence: click to
    select, double-click to switch on or off, **right-click held to solo**. The
    tab strip fills its row at both widths, so there was never anywhere to put a
    button for any of them. */
class BandTabs final : public juce::Component,
                       private juce::Timer
{
public:
    /** `toggle` switches a band on or off. It is the **only** way to do that
        from the tabs, and it is on the double-click because a single click
        already selects: a tab that switched a band off when you were only
        trying to look at it would be unusable.

        The band's ON switch used to live in the strip below and was dropped on
        2026-09-15 -- it sat inside the placement group and read as a fourth
        placement mode. This gesture and the one on the curve's nodes replace
        it. */
    BandTabs (int count, std::function<bool (int)> isOn, std::function<bool (int)> isDynamic,
              std::function<void (int)> choose, std::function<void (int)> toggle = {},
              std::function<void (int)> solo = {}, std::function<juce::Colour (int)> colour = {})
        : bands (count), on (std::move (isOn)), dynamic (std::move (isDynamic)),
          onChoose (std::move (choose)), onToggle (std::move (toggle)), onSolo (std::move (solo)),
          bandColour (std::move (colour))
    {
        startTimerHz (10);
    }

    void setSelected (int band) { selected = band; repaint(); }
    void setRows (int r)        { rows = juce::jmax (1, r); repaint(); }

    /** Space between tabs: 8 on the full panel (the switch gap), 7 on the
        compact one, where six tabs and five gaps have to come out of 300. */
    void setGap (int g)         { gap = juce::jmax (0, g); repaint(); }

    /** How wide the strip is for tabs `tabWidth` across, in `rows` rows. */
    int widthFor (int tabWidth) const
    {
        const auto perRow = (bands + rows - 1) / rows;
        return perRow * tabWidth + (perRow - 1) * gap;
    }

    juce::Rectangle<int> tabBounds (int band) const
    {
        const auto perRow = (bands + rows - 1) / rows;
        const auto row = band / perRow, col = band % perRow;
        const auto w = (getWidth() - gap * (perRow - 1)) / perRow;
        const auto h = (getHeight() - gap * (rows - 1)) / rows;
        return { col * (w + gap), row * (h + gap), w, h };
    }

    void paint (juce::Graphics& g) override
    {
        const auto& t = ui::tokens();

        for (int b = 0; b < bands; ++b)
        {
            const auto r = tabBounds (b).toFloat();
            const auto isSel = b == selected, isOn = on && on (b);
            const auto mine = bandColour ? bandColour (b) : accent;

            g.setColour (isSel ? mine : (isOn ? t.switchOff : t.well));
            g.fillRoundedRectangle (r, ui::Tokens::corner);

            // A band that is on but not selected still says what it is: a bar
            // along the foot of its tab in its own colour. The fill cannot do
            // it -- an unselected tab has to stay quiet enough that the
            // selected one reads -- but three pixels along the bottom are
            // unmistakable at a glance and cost the number nothing.
            if (isOn && ! isSel)
            {
                g.setColour (mine);
                g.fillRect (r.withTop (r.getBottom() - 3.0f).reduced (3.0f, 0.0f));
            }

            if (! isSel && ! isOn)
            {
                g.setColour (t.hairline);
                g.drawRoundedRectangle (r.reduced (0.5f), ui::Tokens::corner, 1.0f);
            }

            const auto ink = isSel ? ui::onAccentOf (mine) : (isOn ? t.text1 : t.text2);
            ui::drawLabel (g, juce::String (b + 1), r, juce::Justification::centred, ui::labelFont (12.0f, true), ink);

            if (dynamic && dynamic (b))
            {
                g.setColour (isSel ? ink : t.meterGr);
                g.fillEllipse (r.getRight() - 9.0f, r.getY() + 4.0f, 5.0f, 5.0f);
            }

            // Soloed, while the button is held. A ring in the primary ink
            // rather than a fill: solo is the loudest thing the panel can do
            // and it has to read instantly against every other tab state, but
            // it lasts as long as a mouse button and must not be mistaken for
            // one of them. Not `polarity`, whose white is reserved suite-wide
            // for phase inversion and nothing else.
            if (b == soloed)
            {
                g.setColour (t.text1);
                g.drawRoundedRectangle (r.reduced (1.0f), ui::Tokens::corner, 2.0f);
            }
        }
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        for (int b = 0; b < bands; ++b)
            if (tabBounds (b).contains (e.getPosition()))
            {
                // **Right-click held is solo** (Frosty, 2026-09-15). Momentary,
                // never a parameter, and it does not change the selection --
                // auditioning a band is not the same as going to work on it.
                //
                // The right button rather than a plain hold, because a plain
                // hold cannot be told from a slow click; and rather than a
                // button, because there is nowhere to put twelve of them and
                // the tab strip already fills its row in both widths. It costs
                // no context menu: the tabs have never had one.
                if (e.mods.isPopupMenu())
                {
                    soloed = b;
                    repaint();
                    if (onSolo) onSolo (b);
                    return;
                }

                selected = b;
                repaint();
                if (onChoose) onChoose (b);
                return;
            }
    }

    void mouseUp (const juce::MouseEvent&) override
    {
        // -1 clears it, which is the contract ModuleContext::setSolo states.
        // Unconditional: a release anywhere ends a solo, including one that
        // began on a tab and ended off the strip.
        if (soloed < 0)
            return;

        soloed = -1;
        repaint();
        if (onSolo) onSolo (-1);
    }

    void mouseDoubleClick (const juce::MouseEvent& e) override
    {
        // The first click of the pair has already selected this band, so a
        // double-click is always "the band I am looking at, on or off".
        for (int b = 0; b < bands; ++b)
            if (tabBounds (b).contains (e.getPosition()))
            {
                if (onToggle) onToggle (b);
                repaint();
                return;
            }
    }

    juce::Colour accent = ui::tokens().accent;

private:
    void timerCallback() override { repaint(); }

    int bands, rows = 1, selected = 0, soloed = -1, gap = 7;
    std::function<bool (int)> on, dynamic;
    std::function<void (int)> onChoose, onToggle, onSolo;
    std::function<juce::Colour (int)> bandColour;
};

//==============================================================================
/** What the dynamics are doing to the gain, as a bar read from both ends.

    The module's deepest band, from the context's gainReductionDb -- the panel
    has no path to a single band's figure, by design (a panel sees parameters
    and five meters, never the DSP).

    **Gain taken away grows down from the top. Gain added grows up from the
    bottom.** Frosty, 2026-09-15, after the UI pass measured that an upward
    band drew exactly the same empty bar as a band with its dynamics switched
    off -- the source was `max (0, ...)` and the meter could not tell the two
    apart. See `DspCore::currentGainReductionDb`.

    Both directions run the **full** height for the full range, so they share
    the track rather than splitting it and neither costs the other any
    resolution. That is the reason this is not a centre-out bar, which was the
    other candidate rendered: centre-out halves both. It works because only one
    of them can be non-zero at a time -- the source is one band's offset, not a
    sum -- so the two fills can never collide, and which end a fill starts from
    *is* the sign.

    Mockups A and C: a 12 px bar, GR under it, and on the full panel the
    figure under that. The bar is centred in whatever width it is given, so
    the cell can be as wide as its words. */
class GainReductionBar final : public juce::Component,
                               private juce::Timer
{
public:
    explicit GainReductionBar (std::function<float()> source) : reduction (std::move (source))
    {
        startTimerHz (30);
    }

    void setShowsValue (bool s) { showsValue = s; repaint(); }

    /** The height a bar of `barHeight` needs with its words under it. */
    int heightFor (int barHeight) const { return barHeight + kCaptionRow + (showsValue ? kValueRow : 0); }

    /** The readout for a signed figure: a real minus for gain taken away, a
        plus for gain added. Tenths, so the bar says how much at a glance and
        this says it exactly. */
    static juce::String valueText (float db)
    {
        return juce::String (db >= 0.0f ? juce::CharPointer_UTF8 ("\xe2\x88\x92")
                                        : juce::CharPointer_UTF8 ("+"))
             + juce::String (std::abs (db), 1);
    }

    /** The widest readout this bar can ever print.

        Fixed rather than sampled, because a cell sized to whatever the meter
        happened to read when somebody looked at it is a cell that clips later.
        `DspCore::currentGainReductionDb` returns the deepest *single* band
        rather than a sum, and a band's offset is bounded by its own range
        parameter, which runs to 24 dB either way -- so one of these two is the
        widest string, and it cannot grow without the schema changing.

        Both ends are measured rather than one, because the minus and the plus
        are different glyphs and which is wider is a property of the caption
        face. Assuming would be how the next version of "-12." gets written. */
    static juce::String widestValue()
    {
        const auto cut = valueText (kRangeDb), added = valueText (-kRangeDb);
        const auto font = ui::captionFont (kValueSize);

        return juce::GlyphArrangement::getStringWidth (font, added)
             > juce::GlyphArrangement::getStringWidth (font, cut) ? added : cut;
    }

    /** How far the widest word this bar draws runs past its own box; <= 0
        fits. The caption is drawn always, the readout only on the full panel.

        The readout clipped to "-12." on the 600 from its first build until the
        2026-09-15 UI pass, and no assertion could have caught it: the suite's
        text-fits checks walk PlainKnob captions and switch labels, and this is
        painted by hand inside a Component of its own. */
    float valueOverflow() const
    {
        auto widest = juce::GlyphArrangement::getStringWidth (ui::labelFont (kCaptionSize), "GR");

        if (showsValue)
            widest = juce::jmax (widest, juce::GlyphArrangement::getStringWidth (ui::captionFont (kValueSize), widestValue()));

        return widest - (float) getWidth();
    }

    void paint (juce::Graphics& g) override
    {
        const auto& t = ui::tokens();
        auto area = getLocalBounds().toFloat();
        const auto value = showsValue ? area.removeFromBottom ((float) kValueRow) : juce::Rectangle<float>();
        const auto caption = area.removeFromBottom ((float) kCaptionRow);
        const auto bar = area.withSizeKeepingCentre (kBarWidth, area.getHeight());

        g.setColour (t.well);
        g.fillRoundedRectangle (bar, 2.0f);
        g.setColour (t.hairline.withAlpha (0.5f));
        g.drawRoundedRectangle (bar.reduced (0.5f), 2.0f, 1.0f);

        // Down from the top for gain taken away, up from the bottom for gain
        // added, both over the whole track. One fill or the other, never both.
        const auto inner = bar.reduced (2.0f);
        const auto depth = juce::jlimit (-1.0f, 1.0f, shown / kRangeDb);

        // Two quantities in one track, so two colours. Drawn in one, the picture
        // said how much while only the readout's sign said which way -- and a
        // bar is read at a glance where a figure is not.
        //
        // Azure up for gain added -- the colour this module has always metered
        // dynamics in -- and its complement down for gain taken away. See
        // Tokens::meterCut: the hue is 180 degrees off the azure, and 23 clear
        // of the amber LTV Comp turns its level bars at.
        if (depth > 0.0f)
        {
            g.setColour (t.meterCut);
            g.fillRect (inner.withHeight (inner.getHeight() * depth));
        }
        else if (depth < 0.0f)
        {
            g.setColour (t.meterBoost);
            g.fillRect (inner.withTrimmedTop (inner.getHeight() * (1.0f + depth)));
        }

        ui::drawLabel (g, "GR", caption, juce::Justification::centredBottom, ui::labelFont (kCaptionSize), t.text1);

        // Blank at rest rather than "-0.0", and the sign carries which end the
        // fill is growing from -- so the figure and the picture agree even
        // when the bar is too short to read a direction off.
        if (showsValue && std::abs (shown) >= 0.05f)
            ui::drawLabel (g, valueText (shown), value,
                           juce::Justification::centredTop, ui::captionFont (kValueSize), t.text2);
    }

private:
    void timerCallback() override
    {
        // Snap to a bigger move, ease back from it -- by magnitude now that
        // the value is signed, so a deep boost holds the way a deep cut does
        // and does not get overtaken by a shallower cut of the opposite sign.
        const auto now = reduction ? reduction() : 0.0f;
        shown = std::abs (now) > std::abs (shown) ? now : shown + 0.25f * (now - shown);
        repaint();
    }

    static constexpr float kRangeDb = 24.0f, kBarWidth = 12.0f;
    static constexpr float kCaptionSize = 11.0f, kValueSize = 11.0f;
    static constexpr int kCaptionRow = 16, kValueRow = 14;
    std::function<float()> reduction;
    float shown = 0.0f;
    bool showsValue = false;
};

//==============================================================================
/** The band's shape as the suite's stepped selector: a ui::ConcentricBand
    with no gain -- BMO EQ's cut-filter dial -- legended BELL, LS, HS, LC, HC,
    with SHAPE under it. Mockups A and C drew it so; the first build used a
    row of five switches instead, which is the thing Frosty called out.

    The dial is centred in the space above the caption and is as big as the
    shorter side of that space allows, so the cell is made wider than it is
    tall to give the legend's diagonals room. */
class ShapeDial final : public juce::Component
{
public:
    ShapeDial (juce::RangedAudioParameter& shape, const ParamSpec& spec, juce::Colour accent)
        : dial (shape, spec, nullptr, accent)
    {
        dial.setLegend ({ "BELL", "LS", "HS", "LC", "HC" });
        addAndMakeVisible (dial);
        setName ("SHAPE");
    }

    void setCaptionSize (float points) { captionSize = points; resized(); repaint(); }

    /** The row under the dial that SHAPE is drawn in. */
    int captionRow() const { return juce::roundToInt (captionSize * 1.2f) + 4; }

    /** How far SHAPE or the widest legend label runs past its box; <= 0 fits. */
    float captionOverflow() const
    {
        return juce::jmax (juce::GlyphArrangement::getStringWidth (ui::captionFont (captionSize), "SHAPE") - (float) getWidth(),
                           dial.legendOverflow());
    }

    ui::ConcentricBand& getDial() noexcept { return dial; }

    void resized() override
    {
        dial.setBounds (getLocalBounds().withTrimmedBottom (captionRow()));
    }

    void paint (juce::Graphics& g) override
    {
        ui::drawLabel (g, "SHAPE", getLocalBounds().removeFromBottom (captionRow()).toFloat(),
                       juce::Justification::centred, ui::captionFont (captionSize), ui::tokens().text1);
    }

private:
    ui::ConcentricBand dial;
    float captionSize = 11.0f;
};

} // namespace bmo::deq
