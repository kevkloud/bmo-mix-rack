#pragma once

#include "core/ui/ModulePanel.h"

namespace bmo::deesser
{

/** BMO Defang's two panes -- the band sketch and the ribbon -- drawn as BMO
    Linger's screen is (Frosty, 2026-09-26: "apply the same dot matrix/screen
    treatment to it"), which BMO DEQ's graph already follows.

    A bezel cut into the plate in `well`, with an `outline` hairline; inside it
    a dark face in `meterFace` -- a value rather than a hue, the same in both
    appearances and both surfaces -- carrying Linger's dot matrix. One place,
    so the two panes stay one instrument in two panes rather than two drawings
    that happen to match.

    Paints the bezel, the face and the dots, and hands back the face. The
    caller draws its picture inside it and calls `paintScreenEdge` last. */
struct Screen
{
    /** The bezel's margin round the face. BMO DEQ's, for the same reason: the
        panes are sized for their pictures, and a Linger-sized bezel would take
        the picture's room. */
    static constexpr float kBezelPad = 3.0f;
    static constexpr float kCorner   = 3.0f;

    static juce::Rectangle<float> paint (juce::Graphics& g, const juce::Component& c,
                                         juce::Rectangle<float> bounds)
    {
        const auto t = ui::panelTokensFor (c);

        // panelTokensFor, so an LTV plate would move the well with it.
        g.setColour (t.well);
        g.fillRoundedRectangle (bounds, kCorner + kBezelPad);
        g.setColour (ui::tokens().outline);
        ui::strokeInside (g, bounds, kCorner + kBezelPad, ui::Tokens::hairlineWeight);

        const auto face = bounds.reduced (kBezelPad);
        g.setColour (ui::tokens().meterFace);
        g.fillRoundedRectangle (face, kCorner);

        // Linger's dot matrix: a faint regular grid from the face's own
        // origin, which is what makes the box read as a display.
        g.setColour (ui::tokens().hairline.withMultipliedAlpha (0.22f));

        for (auto y = face.getY() + 3.0f; y < face.getBottom(); y += 8.0f)
            for (auto x = face.getX() + 3.0f; x < face.getRight(); x += 8.0f)
                g.fillRect (juce::Rectangle<float> (x, y, 1.0f, 1.0f));

        return face;
    }

    /** The face's own edge, over whatever was drawn on it, as Linger's is. */
    static void paintEdge (juce::Graphics& g, juce::Rectangle<float> face)
    {
        g.setColour (ui::tokens().outline);
        ui::strokeInside (g, face, kCorner, ui::Tokens::hairlineWeight);
    }

    /** The line a picture is read against -- unity, silence -- in Linger's
        weight for it. */
    static juce::Colour baseline() { return ui::tokens().hairline.withMultipliedAlpha (0.8f); }

    /** A colour as ink on the face. */
    static juce::Colour ink (juce::Colour c) { return ui::accentInk (c, ui::tokens().meterFace); }
};

} // namespace bmo::deesser
