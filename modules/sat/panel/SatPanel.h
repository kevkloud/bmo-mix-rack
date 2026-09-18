#pragma once

#include "core/product/ModuleDef.h"

namespace bmo::sat
{

/** Input, drive, tone and mix, three switches, output. Drive is the plugin:
    it gets the middle of the panel and the largest face. */
class SatPanel final : public ui::ModulePanel
{
public:
    explicit SatPanel (ui::ModuleContext);

    void resized() override;

private:

    /** Lights the one switch of the three that `choice` names, or none of them
        when it is Off.

        Called from the click handlers and from the parameter, so a setting made
        by the host lands in exactly the state a click would have left. */
    void showOversampling (int choice);

    ui::PlainKnob inputGain, drive, tone, mix, outputLevel;
    ui::SwitchButton satIn, phase, autoGain;

    /** Oversampling is one choice parameter, not three switches, and these are
        in radio behaviour over it the way BMO Opto's TELE / ELD and its meter's
        IN / GR / OUT are: the click sets the parameter and the parameter sets
        the buttons. Clicking the lit one puts it back to Off, which is the
        position that has no switch of its own. */
    juce::ToggleButton os2x, os4x, osHq;
    std::unique_ptr<juce::ParameterAttachment> osAttachment;

};

} // namespace bmo::sat
