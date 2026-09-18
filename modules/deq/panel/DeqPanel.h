#pragma once

#include "core/product/ModuleDef.h"
#include "modules/deq/panel/ResponseView.h"
#include "modules/deq/panel/Widgets.h"

namespace bmo::deq
{

/** BMO DEQ's panel, in two widths from one component, drawn to the mockups
    Frosty picked (A and C, 2026-09-11; `spec/decisions.md`).

    **Full, 600** (standalone's default) is mockup A: a curve big enough to
    work on, one row of twelve tabs, then the selected band in two strips --
    SHAPE / FREQ / GAIN / Q / placement / ON, and DYN+BELOW / THRESH / RANGE /
    RATIO / ATTACK / RELEASE / GR.
    **Compact, 320** (a rack's default) is mockup C: the curve as a map, the
    tabs in two rows, and the same controls stacked four deep.

    Which one is drawn is decided by nothing but the width the host hands the
    panel (ModuleDef::expandedWidth); the switch between them is on the host's
    bar, never here, because every control on this panel changes the sound.

    One band's controls at a time, rebuilt when another tab or node is chosen.
    Which band is selected is panel state, not a parameter: it decides what
    the knobs are bound to and changes nothing that is heard. The snapshot tool
    reaches it through setUiState ("band", "1".."12").

    Every knob prints its value (PlainKnob::setShowsValue) -- the one module in
    the suite that does, by Frosty's call: thirteen continuous controls a band
    are not something to set by eye.
*/
class DeqPanel final : public ui::ModulePanel,
                       private juce::Timer
{
public:
    explicit DeqPanel (ui::ModuleContext);
    ~DeqPanel() override;

    void resized() override;
    bool setUiState (const juce::String& key, const juce::String& value) override;

    int getSelectedBand() const noexcept { return selected; }
    bool isShowingExpanded() const noexcept;

    // A user action anywhere on the panel, heard as a mouse listener on every
    // child: see clampShelfQ.
    void mouseUp (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

private:
    void timerCallback() override;

    void selectBand (int band);

    /** Switches a band on or off, as one host gesture.

        Reached by double-clicking its tab, and the curve's nodes switch one off
        the same way. There is no ON switch on the panel any more: it sat inside
        the placement group and read as a fourth placement mode (Frosty,
        2026-09-15). */
    void toggleBand (int band);

    void bindBand();
    void layoutCompact (juce::Rectangle<int> area);
    void layoutExpanded (juce::Rectangle<int> area);
    void refreshEnablement();

    /** A shelf asked for more Q than it runs at (kShelfMaxQ) is written back
        down to it, so the knob reads what the band is doing. The engine and
        the curve already cap it (params.h, effectiveQ); this is the panel
        telling the truth about it.

        Only after the user has done something here -- chosen a shelf, turned
        Q, wheeled the curve -- and only once the mouse is up, so it never
        fights a drag and never rewrites a session or automation on its own. */
    void clampShelfQ();

    juce::RangedAudioParameter& bandParam (Control c) const { return context.params.param (indexOf (selected, c)); }

    int selected = 0;
    bool clampPending = false;

    ResponseView curve;
    BandTabs tabs;
    GainReductionBar reduction;

    // The selected band's controls: rebuilt by bindBand, so they are always
    // attached to the band on show and to nothing else.
    std::unique_ptr<ShapeDial> shape;
    std::unique_ptr<ui::SwitchButton> dynOn;
    std::unique_ptr<DynamicsMode> mode;
    std::unique_ptr<ChoiceRow> place;
    std::unique_ptr<ui::PlainKnob> freq, gain, q, thr, range, ratio, attack, release;

    ui::SwitchButton active, autoGain;
    ui::PlainKnob output;
};

} // namespace bmo::deq
