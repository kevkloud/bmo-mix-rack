#pragma once

#include "core/product/ModuleDef.h"

#include <array>

namespace bmo::deesser
{

//==============================================================================
/** The band, drawn as the cut it would make at full depth.

    **Static: no tap, no FFT, no timer.** It redraws from `freq`, `q`, `shape`
    and `range` and from nothing else, which is the whole of its cost -- one
    path rebuilt when a knob moves.

    BMO DEQ pays for a spectrum analyser (`modules/deq/panel/Analyser.h`, a
    4096-point Hann transform per repaint) because its interaction *is* the
    curve: twelve bands, and no way to tell which one you are on without
    seeing them. This module has one band, and what a de-esser's user needs to
    know -- is it catching the sibilance, and how hard -- is answered by the GR
    meter and by holding LISTEN. So v1 draws the shape and not the signal;
    docs/deesser/11-integration-and-test-plan.md 4 is the decision.

    **It shows the maximum cut, not the current one.** The curve is drawn at
    `range` deep, because `range` is the control the picture is explaining:
    what the drawing says is "this is the deepest this will go and this is the
    shape it will have", which is a true statement at every moment. Drawing the
    *applied* depth would need the detector, would move at audio rates, and
    would be a second gain-reduction meter in a different unit. */
class BandSketch final : public juce::Component
{
public:
    explicit BandSketch (juce::Colour accentColour);

    /** Bell or shelf, at `freq`, `q` deep by `rangeDb`. `shapeChoice` is the
        parameter's detent index (`ShapeChoice`). Repaints only when something
        has actually moved. */
    void setBand (float freqHz, float qValue, int shapeChoice, float rangeDb);

    void paint (juce::Graphics&) override;

    /** The response the sketch draws, dB at a frequency, for the current band.
        Public so a test can assert the shape without rendering it: it is the
        analogue prototype's magnitude and it is arithmetic, not drawing. */
    float responseDbAt (float hz) const noexcept;

private:
    juce::Colour accent;

    float freqHz = 6500.0f, q = 2.5f, rangeDb = 8.0f;
    int shapeChoice = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BandSketch)
};

//==============================================================================
/** A switch that is only on while it is held down.

    The suite's other switches are radios over a choice parameter: a click sets
    the parameter and the parameter lights the button. This one has no
    parameter at all -- see `DeesserPanel`'s comment on LISTEN -- so it reports
    its own held state instead, and the panel turns that into
    `ModuleContext::setSolo`.

    A `juce::ToggleButton` rather than a `ui::SwitchButton` because
    `SwitchButton` takes a `RangedAudioParameter&` by construction, which is
    exactly what is not available here. It is drawn by the same look and feel,
    so it reads as the same kind of control as the shape pair above it. */
class HoldButton final : public juce::ToggleButton
{
public:
    explicit HoldButton (const juce::String& text) : juce::ToggleButton (text)
    {
        setClickingTogglesState (false);
    }

    /** Called with true when the button goes down and false when it comes back
        up, and not called at all for anything else -- a mouse moving over it,
        a keyboard focus, a repaint. */
    std::function<void (bool)> onHeld;

private:
    void buttonStateChanged() override
    {
        const auto down = isDown();

        if (down == held)
            return;

        held = down;

        if (onHeld != nullptr)
            onHeld (down);
    }

    bool held = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (HoldButton)
};

//==============================================================================
/** The band sketch, FREQ and Q abreast, THRESH and RANGE under them, the
    shape pair, the GR meter with its IN/GR/OUT row, and LISTEN at the foot.

    **The picture is at the top because it is what the four knobs are for.**
    Every one of them moves it, so it sits above all four rather than beside
    one, and the two pairs read as "where and how wide" then "how much and how
    deep".

    **THRESH is the one knob that prints its value.** Its number is in
    *prominence* dB and not dBFS -- how far the band stands above the
    detector's reference -- and a bare number cannot say that. The other three
    are either drawn in the sketch (FREQ, Q, and RANGE as its depth) or carry
    an obvious unit. The caption is the short form, as BMO DEQ's band already
    spells it; the parameter is named "Threshold" where a host shows it.

    **LISTEN is momentary and is not a parameter.** It is held, not toggled,
    and it rides `ModuleContext::setSolo` -- BMO DEQ's band-solo precedent
    exactly (`modules/deq/panel/DeqPanel.cpp` 143-149). Nothing saves it,
    nothing automates it, and it costs no schema slot. A solo that could be
    saved would be recalled into a session or printed into a bounce, which is
    the reason the path exists in the first place. The panel clears it on
    destruction as well as on release, because a window can close with the
    mouse still down.

    **No section rules.** This is one de-esser: a rule is a divider, and a
    panel that is a single idea has nothing to divide -- the argument BMO Opto
    and LTV Comp both make. It takes neither shared section either: there is no
    input stage to trim and no makeup to give back, because a band cut takes
    under a dB of broadband energy (docs/deesser/10-dsp-spec.md 8).

    **Every switch lights in `switchAlt`**, which is the table in
    modules/AGENTS.md with no exception taken: the shape pair, the meter row
    and LISTEN are all "anything else". The module's accent is at hue 3.8
    degrees and the utility azure at 198.8, nearly 165 degrees apart, so a lit
    switch cannot be mistaken for the module's own colour. The accent is spent
    where it belongs -- the knobs, the band sketch and the meter bezel. */
class DeesserPanel final : public ui::ModulePanel
{
public:
    explicit DeesserPanel (ui::ModuleContext);
    ~DeesserPanel() override;

    void resized() override;

    /** Accepts `meter=IN|GR|OUT` and `listen=on|off`, and nothing else.

        `listen` is here because the listen path has no parameter for
        `tools/snapshot` to set, and the panel has to be renderable in both
        states -- docs/deesser/11-integration-and-test-plan.md 3 asks for it by
        that name. It drives the same code a held button does, so a rendered
        LISTEN and a clicked one cannot diverge. */
    bool setUiState (const juce::String& key, const juce::String& value) override;

private:
    /** Points the meter at `mode` and lights the one button of the three that
        says so, so a mode set from the command line lands where a click would
        have left it. */
    void selectMeterMode (ui::DynamicsMeter::Mode);

    /** Lights the one shape switch that `choice` names. Called from the click
        handlers and from the parameter, so a setting made by the host and one
        made by a click land in the same state. */
    void showShape (int choice);

    /** The one place LISTEN becomes a call to the engine. -1 clears it; this
        module has one band, so 0 is the only index it ever sends. */
    void setListening (bool shouldListen);

    /** Re-reads all four band parameters and hands them to the sketch. */
    void refreshSketch();

    ui::PlainKnob freqKnob, qKnob, threshKnob, rangeKnob;
    BandSketch sketch;
    ui::DynamicsMeter meter;

    juce::ToggleButton meterInButton, meterGrButton, meterOutButton;
    juce::ToggleButton bellButton, shelfButton;
    HoldButton listenButton;

    std::unique_ptr<juce::ParameterAttachment> shapeAttachment;
    std::array<std::unique_ptr<juce::ParameterAttachment>, 4> sketchAttachments;

    bool listening = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DeesserPanel)
};

} // namespace bmo::deesser
