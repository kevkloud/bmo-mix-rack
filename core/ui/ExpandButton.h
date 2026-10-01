#pragma once

#include "Tokens.h"
#include <juce_gui_basics/juce_gui_basics.h>

namespace bmo::ui
{

/** Switches an expandable module (ModuleDef::expandedWidth) between its
    compact and wide layouts.

    It lives on the bar *above* a panel -- the standalone header, a rack
    slot's bar -- and never on the panel itself. Every control on a panel
    changes the sound; this changes the window, and putting it among them
    would make it read as one more of them.

    Two chevrons: pointing out when the next press widens, pointing in when
    it narrows. It asks rather than remembers, so whatever changed the view --
    this button, or a session being restored -- it always shows the truth. */
class ExpandButton final : public juce::Button
{
public:
    explicit ExpandButton (std::function<bool()> expandedNow)
        : juce::Button ("expand"), isExpandedNow (std::move (expandedNow))
    {
        setComponentID ("expand");
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
        updateText();
    }

    /** Call after the view changes, so the tooltip and title follow. */
    void refresh()
    {
        updateText();
        repaint();
    }

    void paintButton (juce::Graphics& g, bool over, bool down) override
    {
        const auto& t = tokens();
        const auto area = getLocalBounds().toFloat();

        if (over || down)
        {
            g.setColour (down ? t.well : t.plate);
            g.fillRoundedRectangle (area, Tokens::corner);
        }

        const auto expanded = isExpandedNow != nullptr && isExpandedNow();
        const auto c = area.getCentre();
        const auto h = area.getHeight() * 0.22f;   // half the chevron's height
        const auto d = area.getWidth() * 0.12f;    // how far a chevron reaches
        const auto gap = area.getWidth() * 0.20f;  // centre to each chevron's tip or back

        juce::Path p;

        for (const auto side : { -1.0f, 1.0f })
        {
            // Outward: tip away from the centre. Inward: tip toward it.
            const auto tipX  = expanded ? c.x + side * (gap - d) : c.x + side * (gap + d);
            const auto backX = expanded ? c.x + side * (gap + d) : c.x + side * (gap - d);
            p.startNewSubPath (backX, c.y - h);
            p.lineTo (tipX, c.y);
            p.lineTo (backX, c.y + h);
        }

        g.setColour (isEnabled() ? t.text1 : t.text2);
        g.strokePath (p, juce::PathStrokeType (1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

private:
    void updateText()
    {
        const auto expanded = isExpandedNow != nullptr && isExpandedNow();
        setTooltip (expanded ? "Collapse to the compact panel" : "Expand to the full panel");
        setTitle (expanded ? "Collapse" : "Expand");
    }

    std::function<bool()> isExpandedNow;
};

} // namespace bmo::ui
