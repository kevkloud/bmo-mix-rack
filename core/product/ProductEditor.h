#pragma once

#include "SingleModuleProcessor.h"
#include "core/ui/PresetBar.h"
#include "core/ui/ProductHeader.h"

namespace bmo
{

/** The window of a standalone product: header, preset strip, the module's
    panel. Laid out once at the module's design size and scaled as a whole,
    so knobs, legends, fonts and spacing keep their proportions at any size.

    An expandable module (ModuleDef::expandedWidth) has two design sizes. The
    panel asks for the other through its context; the window keeps the scale
    the user had and changes width around it.
*/
class ProductEditor final : public juce::AudioProcessorEditor,
                            private juce::Timer
{
public:
    explicit ProductEditor (SingleModuleProcessor&);
    ~ProductEditor() override;

    void resized() override;

    static constexpr int kDesignHeight = ui::ProductHeader::kHeight + 24 + ui::ModulePanel::kContentHeight;

private:
    void timerCallback() override;

    /** Lays out at the processor's current view and resizes the window to
        it, keeping the current scale. */
    void applyView();

    SingleModuleProcessor& proc;
    ui::BmoLookAndFeel lookAndFeel;

    /** Everything at design size; the editor scales this. */
    juce::Component plate;
    ui::ProductHeader header;
    ui::PresetBar presetBar;
    std::unique_ptr<ui::ModulePanel> panel;

    /** The design width laid out now: the module's only one, or whichever of
        its two the view asks for. */
    int designWidth;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ProductEditor)
};

} // namespace bmo
