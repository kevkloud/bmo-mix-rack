#include "LevelBars.h"
#include "core/ui/ModulePanel.h"

namespace bmo::vcomp
{

namespace
{
    /** The suite's peak ballistics, from OutputMeter::timerCallback: straight
        to a new high, back down at 0.16 of the distance a tick. */
    constexpr float kRiseRate = 1.0f;
    constexpr float kFallRate = 0.16f;

    constexpr float kCaptionSize = 10.0f;
    constexpr float kScaleSize   = 8.0f;   ///< the printed dB figures
    // 7 rather than 3 -- Frosty, 2026-09-14. The gate is the one control on
    // this panel that is not a knob or a switch, and at 3 px it read as a tick
    // mark on the meter rather than as the thing you drag. The hit test never
    // needed the width: a click anywhere on the bar moves the handle, so this
    // says there is something here to grab, it does not say where to grab.
    constexpr int   kHandleWidth = 7;

    // The flag over the well: its width, and how far it stands above the bar.
    // Frosty chose the shape over a plain bar and an I-beam, rendered side by
    // side on 2026-09-14, and took it down a size on the 15th -- at 17 by 8 it
    // was competing with the meter it points at.
    constexpr int   kHandleCap   = 13;
    constexpr float kFlagHeight  = 6.0f;

    /** Air between the well and its printed figures.

        Was the gate handle clearance, back when the handle hung below the
        well. It does not any more, so this is only breathing room now. */
    constexpr float kScaleGap    = 2.0f;

    /** The box the gate's name is drawn in. Wide enough for "GATE" at
        kScaleSize with air either side. */
    constexpr float kGateLabelWidth = 34.0f;

}

LevelBar::LevelBar (juce::String captionText, Grow growDirection,
                    float minimumDb, float maximumDb, std::function<float()> levelSource)
    : caption (std::move (captionText)),
      grow (growDirection),
      minDb (minimumDb),
      maxDb (maximumDb),
      source (std::move (levelSource))
{
    // Named after the caption a reader sees, like every other control in the
    // suite -- see PlainKnob's constructor. It is what lets a layout test ask
    // for "IN" and get the bar the gate is on.
    setName (caption);
    setInterceptsMouseClicks (false, false);
}

void LevelBar::setScale (std::vector<ScaleMark> marks)
{
    scale = std::move (marks);
}

void LevelBar::setZones (std::vector<ZoneStop> stops)
{
    zones = std::move (stops);
}

void LevelBar::setFlatColour (juce::Colour colour)
{
    flat = colour;
    useFlatColour = true;
}

void LevelBar::attachThreshold (juce::RangedAudioParameter& param, juce::Colour colour)
{
    threshold = &param;
    handleColour = colour;

    // Only a bar carrying a handle takes the mouse. The other two are read,
    // not touched, and a meter that swallows clicks it does nothing with is a
    // meter that feels broken.
    setInterceptsMouseClicks (true, false);
}

juce::Rectangle<int> LevelBar::wellBounds() const
{
    auto area = getLocalBounds();
    area.removeFromLeft (kCaptionWidth);
    area.removeFromBottom (kScaleRow);

    if (threshold != nullptr)
        area.removeFromTop (kTagRow);
    return area.withSizeKeepingCentre (area.getWidth(), kBarHeight);
}

float LevelBar::normalised (float db) const
{
    // Piecewise-linear through the scale marks, which is what puts -18 at the
    // halfway point of a -60..0 bar. Straight ratio before setScale has been
    // called, so a bar without a scale still reads.
    if (scale.size() < 2)
        return juce::jlimit (0.0f, 1.0f, (db - minDb) / (maxDb - minDb));

    if (db <= scale.front().db) return scale.front().fraction;
    if (db >= scale.back().db)  return scale.back().fraction;

    for (size_t i = 1; i < scale.size(); ++i)
    {
        const auto& a = scale[i - 1];
        const auto& b = scale[i];

        if (db <= b.db)
            return a.fraction + (db - a.db) / (b.db - a.db) * (b.fraction - a.fraction);
    }

    return scale.back().fraction;
}

float LevelBar::dbAtFraction (float fraction) const
{
    // The inverse, for the mouse. Without it a drag and the handle it drags
    // would be on two different scales, and the handle would slide out from
    // under the cursor everywhere the curve is not straight.
    if (scale.size() < 2)
        return minDb + fraction * (maxDb - minDb);

    if (fraction <= scale.front().fraction) return scale.front().db;
    if (fraction >= scale.back().fraction)  return scale.back().db;

    for (size_t i = 1; i < scale.size(); ++i)
    {
        const auto& a = scale[i - 1];
        const auto& b = scale[i];

        if (fraction <= b.fraction)
        {
            const auto span = b.fraction - a.fraction;
            return a.db + (span > 0.0f ? (fraction - a.fraction) / span : 0.0f) * (b.db - a.db);
        }
    }

    return scale.back().db;
}

float LevelBar::positionOf (float db) const
{
    const auto n = normalised (db);
    return grow == Grow::rightward ? n : 1.0f - n;
}

float LevelBar::gateMarkerXFor (float db) const
{
    // `normalised`, not `positionOf`: only a rightward bar carries a handle,
    // so the two agree here. If a leftward one ever does, this needs the flip
    // and so does setThresholdFromX -- together, or they part company again.
    const auto well = wellBounds().toFloat();
    return well.getX() + well.getWidth() * normalised (db);
}

juce::Rectangle<float> LevelBar::gateLabelBoundsFor (float db) const
{
    const auto well = wellBounds().toFloat();
    const auto at = gateMarkerXFor (db);

    // Clamped to the **component**, never to the well. Clamping it to the well
    // is what pinned the name in place over the last 17 px of leftward travel
    // while the flag went on without it -- worst at the gate's own default of
    // -60, hard left, where the two sat 17 px apart.
    //
    // At this parameter's range the clamp cannot engage at all: the gate runs
    // kGateOffDb to -10 rather than to 0, so the box wants component-x 21 at
    // one end and a right edge of 192.8 at the other, inside a component 240
    // wide. It is here so that a widened range clips the word at the panel's
    // edge rather than silently detaching it from the thing it names.
    const auto x = juce::jlimit (0.0f, (float) getWidth() - kGateLabelWidth,
                                 at - kGateLabelWidth * 0.5f);

    return { x, well.getY() - (float) kTagRow,
             kGateLabelWidth, (float) kTagRow - kFlagHeight - 1.0f };
}

void LevelBar::refresh()
{
    const auto reading = source ? source() : minDb;
    const auto target = normalised (reading);

    displayed += (target > displayed ? kRiseRate : kFallRate) * (target - displayed);

    repaint();
}

void LevelBar::paint (juce::Graphics& g)
{
    const auto t = ui::panelTokensFor (*this);

    const auto well = wellBounds().toFloat();

    // Centred on the *well*, not on the component. The component grew a scale
    // strip under it on 2026-09-14 and the caption carried on centring itself
    // in the whole row, which left IN, GR and OUT sitting a few pixels below
    // the bars they name -- consistently, so it read as sloppy rather than as
    // broken, which is the harder kind to notice.
    ui::drawLabel (g, caption,
                   juce::Rectangle<float> (0.0f, well.getY(),
                                           (float) kCaptionWidth - 6.0f, well.getHeight()),
                   juce::Justification::centredRight, ui::labelFont (kCaptionSize), t.text1);

    g.setColour (t.well);
    g.fillRoundedRectangle (well, 2.0f);

    if (displayed > 0.002f)
    {
        auto bar = well.reduced (1.5f);
        const auto length = bar.getWidth() * displayed;

        bar = grow == Grow::rightward ? bar.removeFromLeft (length)
                                      : bar.removeFromRight (length);

        if (useFlatColour)
        {
            g.setColour (flat);
        }
        else if (zones.size() >= 2)
        {
            // A gradient across the whole well, then clipped to the filled
            // part -- not a gradient across the fill. Laying it over the well
            // is what makes a given dB always the same colour: stretch it to
            // the fill instead and the quiet end of the bar changes hue every
            // time the loud end moves.
            juce::ColourGradient gradient (zones.front().colour,
                                           well.getX(), 0.0f,
                                           zones.back().colour,
                                           well.getRight(), 0.0f, false);

            for (size_t i = 1; i + 1 < zones.size(); ++i)
                gradient.addColour (juce::jlimit (0.001, 0.999,
                                                  (double) positionOf (zones[i].db)),
                                    zones[i].colour);

            g.setGradientFill (gradient);
        }
        else
        {
            const auto db = minDb + displayed * (maxDb - minDb);
            g.setColour (db > -1.0f ? t.meterClip : db > -9.0f ? t.meterHigh : t.meterLow);
        }

        g.fillRoundedRectangle (bar, 1.5f);
    }

    // Ticks every 12 dB, drawn over the fill. Without them the bars are three
    // lengths with no units, which is tolerable for IN and OUT -- you read
    // those against each other -- and not tolerable on the bar carrying the
    // gate handle, where the whole point is knowing what level you are setting
    // the threshold to. So the scale is on every bar rather than only the one
    // that needs it: three bars with two kinds of well would read as two
    // different instruments.
    g.setColour (t.meterInk.withAlpha (0.22f));

    for (size_t i = 1; i + 1 < scale.size(); ++i)
    {
        const auto at = well.getX() + well.getWidth() * positionOf (scale[i].db);
        g.drawVerticalLine ((int) at, well.getY() + 2.0f, well.getBottom() - 2.0f);
    }

    g.setColour (t.outline.withAlpha (0.6f));
    g.drawRoundedRectangle (well.reduced (0.5f), 2.0f, 1.0f);

    // The printed scale, in the strip wellBounds reserves under the well.
    //
    // Every bar carries its own rather than one shared strip under the block,
    // because the three do not share a scale: IN and OUT run -60..0 dBFS left
    // to right, GR runs 0..24 of reduction right to left. One strip would be
    // right about two of them and a lie about the third.
    //
    // The ends are justified into the well rather than centred on their own
    // positions, so the outermost figures sit inside the meter instead of half
    // over its edge.
    {
        const auto font = ui::labelFont (kScaleSize);
        const auto top = well.getBottom() + kScaleGap + 1.0f;
        const auto strip = juce::Rectangle<float> (well.getX(), top,
                                                   well.getWidth(),
                                                   well.getBottom() + (float) kScaleRow - top);

        const auto figure = [&] (const ScaleMark& mark, juce::Justification justify)
        {
            const auto box = justify == juce::Justification::centred
                                 ? strip.withX (strip.getX() + well.getWidth() * positionOf (mark.db) - 16.0f)
                                        .withWidth (32.0f)
                                 : strip;

            // Stepped against the plate this panel actually has, not left at
            // the token. text2 is #9a9a9a in the pale set and lands at 1.68:1
            // on LTV silver, where it is #9a9aa4 and 4.02:1 on the graphite --
            // so the figures were legible in one appearance and washed out in
            // the other. accentTextOn fixes the pale one and leaves the dark
            // alone, because it only moves a colour that has not already
            // cleared. 3.0 rather than the 4.5 default: a touch darker, not a
            // second set of captions.
            ui::drawLabel (g, mark.text, box, justify, font,
                           ui::accentTextOn (t.text2, t.plate, 3.0f));
        };

        for (size_t i = 1; i + 1 < scale.size(); ++i)
            figure (scale[i], juce::Justification::centred);

        // The two ends, justified into the well rather than centred on their
        // own positions, so they sit inside the meter instead of half over its
        // edge. Which mark is which end depends on the fill direction.
        if (scale.size() >= 2)
        {
            const auto& low  = scale.front();
            const auto& high = scale.back();

            figure (positionOf (low.db) < positionOf (high.db) ? low : high,
                    juce::Justification::centredLeft);
            figure (positionOf (low.db) < positionOf (high.db) ? high : low,
                    juce::Justification::centredRight);
        }
    }

    if (threshold != nullptr)
    {
        // The handle is drawn on the same scale the fill is, so where it sits
        // is literally the level it will act at -- which is the entire reason
        // the gate lives here instead of on a knob.
        const auto value = threshold->convertFrom0to1 (threshold->getValue());
        const auto at = gateMarkerXFor (value);

        // Whatever attachThreshold was given, unrouted: the handle is red now
        // rather than the module accent, so there is no accent here for a line
        // to override. See VcompPanel, where the colour is chosen.
        // The line ink, not the red -- Frosty, 2026-09-15. The gate used to be
        // tokens().meterClip, and the meter it sits on is a gradient into red
        // now, so a red handle over a red bar was two different meanings in
        // one colour. Matching the printed scale instead makes it read as part
        // of the instrument's lettering, which is what it is.
        g.setColour (ui::panelAccentFor (*this, handleColour));

        {
            // A flag over the well -- a triangle pointing down at the level it
            // sets -- with the stem carrying the eye through the bar, and the
            // control's name riding above it.
            //
            // Frosty's call, 2026-09-14, over a plain bar and an I-beam. What
            // the name buys is the thing neither shape could say: a mark on a
            // meter is a reading until something tells you it is a control,
            // and GATE moving with it says both at once.
            //
            // **The stem is exactly the well's height** -- Frosty, 2026-09-15.
            // It used to stand kHandleProud past both edges, left over from
            // the bar this replaced, and hanging below the meter made the
            // handle read as taller than the thing it sits in.
            //
            // That also settles the printed scale properly. It was kept clear
            // of the handle by a margin; now nothing on the handle descends
            // past the well at all, so the figures are out of its reach by
            // geometry rather than by clearance -- which is the stronger form
            // of the same fix BMO Opto's GR needle needed.
            g.fillRect (juce::Rectangle<float> (at - (float) kHandleWidth * 0.5f, well.getY(),
                                                (float) kHandleWidth, well.getHeight()));

            juce::Path flag;
            flag.addTriangle (at - (float) kHandleCap * 0.5f, well.getY() - kFlagHeight,
                              at + (float) kHandleCap * 0.5f, well.getY() - kFlagHeight,
                              at,                             well.getY());
            g.fillPath (flag);

            // The name rides **with** the flag. They are one control and they
            // have to move as one, which they did not until 2026-09-19: the
            // label was clamped into the *well*, so over the last 17 px of
            // leftward travel the flag went on and the word stayed behind.
            // Worst at the gate's own default, -60, where it is hard left and
            // the two sat 17 px apart -- measured on AURORA, flag centre at
            // component-x 37.8 against the label's 54.8.
            //
            // The clamp was defending against an overflow that cannot happen.
            // **The gate's range is kGateOffDb to -10, not to 0** (params.h),
            // and the label is 34 px wide, so at the two extremes it wants
            // component-x 21 and a right edge of 192.8 inside a component 240
            // wide. Both fit, with 21 px spare at one end and 47 at the other.
            //
            // Clamped to the component rather than to the well, which is what
            // the clamp should always have said: it cannot engage at this
            // range, and if the range ever widens it stops the word being
            // clipped off the panel instead of pinning it inside the trough.
            ui::drawLabel (g, "GATE", gateLabelBoundsFor (value),
                           juce::Justification::centred, ui::labelFont (kScaleSize),
                           ui::panelAccentFor (*this, handleColour));
        }

    }
}

void LevelBar::setThresholdFromX (int x)
{
    const auto well = wellBounds();
    const auto proportion = juce::jlimit (0.0f, 1.0f,
                                          (float) (x - well.getX()) / (float) well.getWidth());

    // Through the curve, not a straight ratio -- see dbAtFraction. positionOf
    // is not needed here because only a rightward bar carries a handle; if a
    // leftward one ever does, this needs the flip too.
    threshold->setValueNotifyingHost (
        threshold->convertTo0to1 (dbAtFraction (proportion)));
}

void LevelBar::mouseDown (const juce::MouseEvent& e)
{
    if (threshold == nullptr)
        return;

    // Anywhere **on the well**, not anywhere on the component. A handle you
    // have to grab exactly is a handle you miss, so the whole trough is a
    // target -- but the component is taller and wider than its trough, and
    // everything else in it is lettering.
    //
    // Outside the well this used to arm the drag anyway and set the threshold
    // from a clamped x, so clicking the word IN, a printed figure or the GATE
    // tag slammed the gate shut at -60. It grew worse as the class did: the
    // caption column was the only dead zone until 2026-09-15, when a 13 px
    // scale strip and a 16 px tag row joined it. A mis-click that silently
    // closes a gate is the worst kind, because the panel looks identical
    // afterwards and only the sound is wrong.
    if (! wellBounds().contains (e.getPosition()))
        return;

    dragging = true;
    threshold->beginChangeGesture();
    setThresholdFromX (e.x);
}

void LevelBar::mouseDrag (const juce::MouseEvent& e)
{
    if (dragging)
        setThresholdFromX (e.x);
}

void LevelBar::mouseUp (const juce::MouseEvent&)
{
    if (! dragging)
        return;

    dragging = false;
    threshold->endChangeGesture();
}

} // namespace bmo::vcomp
