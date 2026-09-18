#pragma once

#include "core/product/ModuleDef.h"
#include "modules/vcomp/panel/LevelBars.h"

namespace bmo::vcomp
{

/** AMOUNT, three horizontal dBFS meters with the gate handle on the first of
    them, MAKEUP, then a COMPLEX/ARC switch row and the five detector knobs
    COMPLEX reveals.

    **Two knobs is the product, and the panel has to say so.** The whole claim
    of this module is that a vocal compressor needs one knob for how hard and
    one for how loud; a panel that opened with eight controls would be making
    the opposite claim whatever the DSP does. So ATTACK, RELEASE, SC HPF, LOW
    THRU and HIGH THRU are not on it until COMPLEX is on.

    **The gate is the exception, and it is not a knob.** It is a handle on the
    IN meter, at the level it will act at, because a gate threshold is the one
    parameter you set by watching the thing you are setting it against. That
    also keeps the face at two knobs while putting the gate where a user
    reaches it -- which is what the gate is for, since it exists to clean up
    what AMOUNT's makeup does to the silences.

    **Three bars, not a needle.** BMO Opto's DynamicsMeter is a period
    instrument: VU ballistics on a 1940s scale, right for a module modelling an
    LA-2A and wrong for this one. It also shows one reading at a time behind a
    three-way switch, and the three readings a compressor user wants -- what
    went in, what came off, what came out -- are wanted together. IN and OUT
    read left to right in dBFS; GR reads right to left from zero, the way every
    gain-reduction meter has ever read, because the bar is showing what is
    being taken away rather than what is arriving. See LevelBars.h.

    **The layout does not move when the complex knobs appear.** Every control
    is placed from the top at a fixed gap worked out for the complex-on
    layout, so the two knob rows are reserved whether or not anything is in
    them, and AMOUNT, the meters, MAKEUP and the switch row sit at the same
    pixel in both states. The cost is empty plate at the foot in standard mode,
    under a switch that says COMPLEX -- which reads as a closed drawer, and is
    the price of controls that stay put. Re-flowing the panel so standard mode
    breathes into the space was rejected for the reason modules/AGENTS.md gives
    against BMO Opto's old hide-and-shuffle COLOR switch.

    **ARC is visible in both states, and locked on in standard mode.** It is
    not hidden with the other five, because standard mode genuinely runs ARC --
    DspCore substitutes kStandardArc, not the parameter -- and a switch that
    vanished would leave the panel silent about the one thing most responsible
    for how the module behaves. Locked rather than disabled, for the reason BMO
    Opto's COLOR is locked in Tele mode: the disabled alpha collapses a
    switch's fill and its ink toward the plate together, so it says what it has
    to say at about 1.3:1. See timerCallback().

    One section, so no rules: the module is one compressor, and a line across
    it would mark a boundary that is not there. Same reasoning as BMO Opto, and
    it is modules/AGENTS.md's rule being followed rather than an exception to
    it. */
class VcompPanel final : public ui::ModulePanel,
                         private juce::Timer
{
public:
    explicit VcompPanel (ui::ModuleContext);
    ~VcompPanel() override;

    void resized() override;

protected:
    void paintPanel (juce::Graphics&) override;

private:
    /** Where the IGNORE legend and its two rules go, set in resized and drawn
        in paintPanel. Empty until the drawer is open. */
    juce::Rectangle<int> ignoreRow;

    void timerCallback() override;

    /** Shows or hides the five detector knobs and locks or frees ARC. Called
        from the constructor and from timerCallback when COMPLEX changes, so
        host automation of COMPLEX moves the panel as a click does. */
    void applyComplex (bool on);

    ui::PlainKnob amount, output;

    LevelBar inBar, grBar, outBar;

    ui::SwitchButton complexSwitch, arcSwitch;
    ui::PlainKnob attackKnob, releaseKnob, sidechainKnob, lowThruKnob, highThruKnob;

    bool lastComplex = false;
};

} // namespace bmo::vcomp
