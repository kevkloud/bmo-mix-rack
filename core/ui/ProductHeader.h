#pragma once

#include "ExpandButton.h"
#include "Line.h"
#include "LookAndFeel.h"

namespace bmo::ui
{

/** The strip across the top of a standalone product: the name on the left,
    the maker on the right. Drawn, not laid out -- except for an expandable
    module (ModuleDef::expandedWidth), whose expand switch sits just left of
    the maker's mark. Nothing else on it is clickable. */
class ProductHeader final : public juce::Component
{
public:
    static constexpr int kHeight = 28;

    /** `productLine` supplies the marque printed at the right-hand end and the
        ground this strip is filled with -- "LT3a" on the suite grey for BMO,
        "LTV" on silver for a collaboration. */
    ProductHeader (juce::String productName, juce::Colour accent, const Line& productLine)
        : name (std::move (productName)), tint (accent), line (&productLine) {}

    /** Gives the header an expand switch. `expandedNow` answers what the view
        is; `toggle` asks for the other one. Only an expandable module calls
        this; every other header stays as it was. */
    void setExpandable (std::function<bool()> expandedNow, std::function<void()> toggle)
    {
        expand = std::make_unique<ExpandButton> (std::move (expandedNow));
        expand->onClick = std::move (toggle);
        addAndMakeVisible (*expand);
        resized();
    }

    /** After the view changes, so the switch's glyph and tooltip follow. */
    void refreshExpand()
    {
        if (expand != nullptr)
            expand->refresh();
    }

    void paint (juce::Graphics& g) override
    {
        const auto t = groundFor (*line);
        auto area = getLocalBounds();

        g.fillAll (t.plateEdge);

        // The module's colour, as a thin bar along the top edge -- or the
        // line's ink where it has one, so an LTV header does not keep an
        // accent its panel no longer uses anywhere.
        g.setColour (inkFor (*line).value_or (tint));
        g.fillRect (area.removeFromTop (3));

        drawLabel (g, name, area.reduced (10, 0).toFloat(), juce::Justification::centredLeft,
                   labelFont (12.5f, true), t.text1);
        drawLabel (g, line->marque, area.reduced (10, 0).toFloat(), juce::Justification::centredRight,
                   labelFont (10.0f, true), t.text2);
    }

    void resized() override
    {
        // Left of "LT3a", which is right-aligned in the same 10 px inset; the
        // mark is under 30 px wide at 10 pt, so 36 clears it with room.
        if (expand != nullptr)
            expand->setBounds (getWidth() - 10 - 36 - kSwitch, 3 + (kHeight - 3 - kSwitch) / 2, kSwitch, kSwitch);
    }

private:
    static constexpr int kSwitch = 18;   ///< the rack's slot-bar buttons are 18 too

    juce::String name;
    juce::Colour tint;
    const Line* line;
    std::unique_ptr<ExpandButton> expand;
};

} // namespace bmo::ui
