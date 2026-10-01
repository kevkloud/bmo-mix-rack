#pragma once

#include "RackProcessor.h"
#include "core/ui/ExpandButton.h"
#include "core/ui/PresetBar.h"
#include "core/ui/ProductHeader.h"

namespace bmo
{

/** The rack's window: a header with the rack's own preset strip, then the
    modules side by side, each under a slot header that names it and moves
    it, and a strip on the right for adding another.

    The window is as wide as the modules in it, so it grows and shrinks as
    the chain changes -- and when an expandable module switches between its
    compact and wide layouts. Everything is laid out at design size and scaled
    as a whole, like the standalone products.
*/
class RackEditor final : public juce::AudioProcessorEditor,
                         private RackProcessor::Listener,
                         private juce::Timer
{
public:
    explicit RackEditor (RackProcessor&);
    ~RackEditor() override;

    void resized() override;

    static constexpr int kHeader    = ui::ProductHeader::kHeight;
    static constexpr int kSlotBar   = 24;
    static constexpr int kAddStrip  = 40;
    static constexpr int kMinWidth  = 300;   ///< room for the header when empty
    static constexpr int kDesignHeight = kHeader + kSlotBar + ui::ModulePanel::kContentHeight;

private:
    //==========================================================================
    /** The bar over a module in a slot: its name (click for the menu),
        left, right, remove. */
    class SlotBar final : public juce::Component
    {
    public:
        SlotBar (RackEditor&, int slot);

        void paint (juce::Graphics&) override;
        void resized() override;

    private:
        void showMenu();

        RackEditor& owner;
        const int slot;
        juce::TextButton name, left { "<" }, right { ">" }, remove { "x" };
        std::unique_ptr<ui::ExpandButton> expand;   ///< only for an expandable module
    };

    /** The strip on the right: a "+" that offers the registry. */
    class AddStrip final : public juce::Component
    {
    public:
        explicit AddStrip (RackEditor&);
        void paint (juce::Graphics&) override;
        void resized() override;

    private:
        RackEditor& owner;
        juce::TextButton add { "+" };
    };

    /** A hairline down the boundary between two panels.

        A slot bar draws one along its own right edge, but the bar is 24 px
        tall and the panel under it is 688, so below the bars four identical
        plates ran together into a single field with nothing to say where one
        module ended. Every panel paints its own plate edge to edge, which is
        what makes a rack read as one surface; this is the seam that keeps it
        from reading as one *module*. */
    class Seam final : public juce::Component
    {
    public:
        Seam() { setInterceptsMouseClicks (false, false); }
        void paint (juce::Graphics& g) override { g.fillAll (ui::tokens().hairline); }
    };

    struct SlotView
    {
        std::unique_ptr<SlotBar> bar;
        std::unique_ptr<ui::ModulePanel> panel;
    };

    void rackChainWillChange() override;
    void rackChainChanged() override;
    void timerCallback() override;

    void rebuildViews();
    void layoutPlate();
    int designWidth() const;

    /** A slot's width now: its module's only one, or whichever of its two the
        slot's view asks for (ModuleDef::expandedWidth). */
    int slotWidth (int slot) const;

    /** Constrains and re-sizes the window to the plate at `scale`. */
    void refit (float scale);

    /** An expandable module's slot bar asks for its other width here. */
    void toggleSlotView (int slot);

    void showModuleMenu (juce::Component& target, std::function<void (const ModuleDef&)>);

    RackProcessor& proc;
    ui::BmoLookAndFeel lookAndFeel;

    juce::Component plate;
    ui::ProductHeader header;
    ui::PresetBar presetBar;
    std::vector<SlotView> views;
    std::vector<std::unique_ptr<Seam>> seams;
    AddStrip addStrip;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RackEditor)
};

} // namespace bmo
