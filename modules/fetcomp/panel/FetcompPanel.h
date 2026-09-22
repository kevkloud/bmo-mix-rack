#pragma once

#include "core/product/ModuleDef.h"

namespace bmo::fetcomp
{

/** INPUT and OUTPUT abreast, ATTACK and RELEASE under them, the ratio buttons,
    the VU with its IN/GR/OUT row, the voicing pair, MIX, and the oversampling
    row at the foot.

    **The four knobs sit in two pairs because that is the instrument.** The
    hardware this is modelled on has no threshold: you set how hard the signal
    arrives and how loud it leaves, and the two knobs that do that belong
    together -- as do the two that decide how fast it moves. Stacking all four
    down the panel would have said they were four unrelated amounts.

    **The ratio is five buttons and not a dial**, in a 2 x 2 block with ALL on
    its own underneath. Four of them are ratios; the fifth is every button
    pushed in at once, which is a different curve rather than a steeper one --
    so it is set apart rather than made the end of a row of five.

    **The voicing is shown as the border around the VU and nowhere else**, and
    the pair of buttons that sets it sits directly under the meter for that
    reason. Blue lights the bezel in the module's accent; Black draws it in
    literal black. That pair was chosen over three others because the two
    states land on **opposite sides of the meter face** -- the accent sits
    2.00:1 above `meterFace` and black 1.94:1 below it -- so they separate by
    luminance rather than by hue, which is the failure mode the suite has
    already fixed once on its switch colours. The rack accent stays blue in
    both states and nothing else on the panel changes colour. See
    docs/1176-comp/11-integration-and-test-plan.md 4b, which carries the
    figures and the three rejected pairs.

    **No section rules.** This is one compressor, the same argument BMO Opto
    and LTV Comp make: a rule is a divider, and a panel that is a single idea
    has nothing to divide.

    **Every switch here lights in the module's accent**, which departs from the
    table in modules/AGENTS.md -- ratio, voicing and oversampling are all
    "anything else", which that table gives `switchAlt`. `switchAlt` is the
    utility azure at hue 198.8 degrees and this module's accent is at 215.2,
    16.4 degrees away, so an "anything else" switch on this panel would be a
    second blue nobody could tell from the first. It is the third exception in
    the suite and the first taken for hue rather than for greyscale; see
    modules/fetcomp/AGENTS.md. */
class FetcompPanel final : public ui::ModulePanel
{
public:
    explicit FetcompPanel (ui::ModuleContext);

    void resized() override;

    /** Accepts `meter=IN|GR|OUT` and `bezel=stock|full`, and nothing else.

        `bezel` is the border-alpha gate (11 section 4d step 6): both variants
        have to be rendered in both voicing states and both appearances before
        either can ship, and a constant that needs a rebuild to flip cannot be
        rendered eight ways in one pass. It is a render control, not a setting
        -- the shipped value is kBezelAlpha in the .cpp, in one place. */
    bool setUiState (const juce::String& key, const juce::String& value) override;

private:
    /** Draws the bracket that gathers the four ratios and leads down to ALL.

        ALL is every button pushed in at once, which is the one thing about
        this panel a reader cannot get from the labels: "ALL" beside four
        ratios reads as a fifth ratio. The bracket says it is made of them. */
    void paintPanel (juce::Graphics&) override;

    /** Points the meter at `mode` and lights the one button of the three that
        says so, so a mode set from the command line lands where a click would
        have left it. */
    void selectMeterMode (ui::DynamicsMeter::Mode);

    /** Lights the one ratio button that `choice` names. Called from the click
        handlers and from the parameter, so a setting made by the host and one
        made by a click land in the same state. */
    void showRatio (int choice);

    /** Lights BLUE or BLACK, and repaints the meter's bezel in the colour that
        voicing means. The bezel is the only thing on the panel that moves. */
    void showVoicing (int choice);

    /** Lights 2x or 4x, or neither when the choice is Off. */
    void showOversampling (int choice);

    /** The bezel colour for a voicing: the accent for Blue, literal black for
        Black. Black is a real colour here rather than a token, because what is
        wanted is the bottom of the range and nothing in the palette is lower
        -- see the class comment. */
    juce::Colour bezelFor (int voicingChoice) const;

    /** Re-apply the module accent after the appearance changes.

        The accent is stored in each knob rather than read at paint time, so
        an appearance switch has to push the new one out. `ProductEditor`
        polls the theme and repaints; this catches that repaint and updates
        what the repaint is about to draw with. Without it the panel renders
        correctly at whichever appearance it was constructed in and keeps that
        accent for ever, which a snapshot would never show -- the tool
        constructs the editor after setting the appearance. */
    void applyAccent();

    /** The appearance the accent above was last applied for. */
    bool accentIsDark = false;


    ui::PlainKnob inputKnob, outputKnob, attackKnob, releaseKnob, mixKnob;
    ui::DynamicsMeter meter;

    juce::ToggleButton meterInButton, meterGrButton, meterOutButton;
    juce::ToggleButton ratio4Button, ratio8Button, ratio12Button, ratio20Button, ratioAllButton;
    juce::ToggleButton voicingBlueButton, voicingBlackButton;
    juce::ToggleButton os2xButton, os4xButton;

    std::unique_ptr<juce::ParameterAttachment> ratioAttachment, voicingAttachment, osAttachment;

    int lastVoicing = -1;

    /** Whether ATTACK and RELEASE share the drive column or take the panel's
        full width. See setUiState's `time` key -- an open layout question,
        rendered both ways. */
    bool timeKnobsInColumn = true;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FetcompPanel)
};

} // namespace bmo::fetcomp
