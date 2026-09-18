#pragma once

#include "core/product/ModuleDef.h"

namespace bmo::util
{

/** Gain, then the stereo image -- pan, width, mono --
    then the two polarity flips. The narrow one: it goes at the front of a
    chain and stays out of the way.

    No output meter, which is a deliberate exception to the rule in
    modules/AGENTS.md that every module ends with one.

    All three knobs take one row, and their names sit 11 px up into the air
    under each face -- the distance BMO Opto's and LTV Comp's names sit at,
    measured. VOLUME and PAN print their values; WIDTH reserves the same line
    and prints nothing on it, so the three names stay level. WIDTH also dims
    while MONO is on, because the sum happens before the mid/side stage and
    leaves it nothing to scale. See testing-notes/ui-pass-util-2026-09-17.md. */
class UtilPanel final : public ui::ModulePanel,
                        private juce::Timer
{
public:
    explicit UtilPanel (ui::ModuleContext);
    ~UtilPanel() override;

    void resized() override;

private:
    /** Reads MONO back and dims WIDTH while it is on -- see the call site for
        why. Polled at 15 Hz rather than listened for, the same as BMO Opto's
        mode, so host automation and a preset land where a click does. */
    void timerCallback() override;

    ui::PlainKnob gain, pan, width;
    ui::SwitchButton phaseL, phaseR, mono;

    bool lastMonoWasOn = false;
};

} // namespace bmo::util
