#pragma once

#include "core/product/ModuleDef.h"
#include "core/ui/ModulePanel.h"
#include <array>

namespace bmo::tune
{

/** BMO Tune RT's panel, built from the suite's own controls: round 7 of the
    panel studies (design/), less everything HYBRID and Studio had, which left
    with them on 2026-09-11.

    Top to bottom:
      - what to tune to: the keyboard (click a note out of the scale), Key
        with its ♯ and ♭, and Scale, Pitch Range and Ref A as value boxes that
        open menus;
      - a rule, then how it corrects: Retune, large, directly under it with
        its millisecond value beneath, then Vibrato and Relax a size down as a
        row of modifiers at the foot. Round 7's clock was replaced on
        2026-09-16; the layout block in the .cpp says why.

    `Relax` is the caption *and* the parameter's display name. Its id stays
    `flex`, which is what a saved session references.

    Colours are lime by the suite's rules, as the studies settled them: knob
    caps by `faceOf`, captions by `accentInk`, and every selector lit in the
    raw accent. Captions are drawn by the panel rather than by PlainKnob,
    whose caption is the raw accent -- lime is too light for that to read on
    the pale plate (1.35:1), and the studies were approved with `accentInk`.
*/
class TunePanel final : public ui::ModulePanel,
                        private juce::Timer
{
public:
    static constexpr int kDesignWidth = 360;

    explicit TunePanel (ui::ModuleContext);
    ~TunePanel() override;

    void resized() override;

    /** Every control on the panel that sets parameter `index` -- for the
        layout test, which checks every parameter has one. */
    std::vector<juce::Component*> controlsFor (int index);

    /** Every switch on the panel, for the label-fit test. */
    std::vector<juce::ToggleButton*> switches();

    /** What a value box shows, and how much wider that is than its box. */
    struct BoxText { juce::String id, text; float overflow; };
    std::vector<BoxText> boxTexts();

    /** The Retune Speed readout under the big knob, and how much wider it is
        than the room it is drawn in. */
    BoxText retuneReadout();

    /** Reads every parameter back into the panel now, rather than on the next
        timer tick. For the snapshot tool and the tests, which have no message
        loop to wait on. */
    void syncNow() { sync (true); }

    class ValueBox;
    class Keyboard;

private:
    void paintPanel (juce::Graphics&) override;
    void timerCallback() override;

    /** Parameters into switch states and boxes. Cheap when nothing changed;
        `force` redoes everything. */
    void sync (bool force);

    void setChoice (int index, int choice);
    void setReal (int index, float value);
    int choiceOf (int index) const;

    void showKeyMenu();
    void showScaleMenu();
    void showRangeMenu();
    void showRefMenu();
    void beginCustomRef();
    void commitCustomRef();

    /** The Key parameter as a letter and an accidental. */
    struct Spelling { int natural; int accidental; };   // natural 0..6 = C..B
    static Spelling spellingOf (int keyChoice);
    static int keyChoiceOf (Spelling);
    static bool hasSpelling (Spelling s) { return keyChoiceOf (s) >= 0; }

    juce::String keyText() const;
    juce::String refText() const;
    juce::String retuneText() const;

    static constexpr float kRetuneReadoutSize = 38.0f;
    static juce::Rectangle<float> retuneReadoutArea();

    struct KnobPlace { ui::PlainKnob* knob; const char* caption; juce::Point<int> centre; int face; float captionSize; };
    std::vector<KnobPlace> knobPlaces();

    ui::PlainKnob retune, vibrato, flex;

    juce::ToggleButton sharpButton, flatButton;

    std::unique_ptr<juce::Component> sharpGlyph, flatGlyph;

    std::unique_ptr<ValueBox> keyBox, scaleBox, rangeBox, refBox;
    std::unique_ptr<Keyboard> keyboard;
    juce::TextEditor refEditor;

    std::unique_ptr<juce::LookAndFeel_V4> menuLook;

    /** The last values sync() pushed into the controls. */
    std::array<float, 32> shown {};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TunePanel)
};

} // namespace bmo::tune
