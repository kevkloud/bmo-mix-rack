#pragma once

#include "core/dsp/AnalyserTap.h"
#include "core/ui/ModulePanel.h"
#include "modules/deesser/dsp/DspCore.h"

#include <vector>

namespace bmo::deesser
{

/** The sibilance ribbon: what arrived, which of it was sibilance, and what
    was taken off -- scrolling right to left under the band sketch.

    **Why it is not behind the curve.** Frosty asked whether the sibilance
    could be highlighted on a waveform drawn behind the bell. It cannot, and
    the reason is an axis: the sketch's x is *frequency* and a waveform's x is
    *time*. Drawn over one another, a loud ess would appear to sit at a
    frequency it has nothing to do with, and the picture would be answering a
    question nobody asked. So the two views stack instead, and are tied
    together by their inks rather than by their axes -- the band trace here is
    the sketch's accent, the reduction is the GR bar's warm token, and the
    panel reads top to bottom as where, when, how much.

    **The three layers.**

    - The **envelope**, in the well's own ink: what the module was given. Drawn
      as a silhouette about the centre line rather than as a wave, because at
      six frames a pixel a wave is a smear and an envelope is a shape.
    - The **band**, in the accent, inside the envelope: how much of that was in
      the band the detector is watching. This is the sibilance -- not inferred
      from it, but the detector's own envelope, so what is drawn and what the
      module acted on cannot disagree.
    - The **reduction**, in `meterGrWarm`, hanging from the top: what was taken
      off, on the same scale as the GR bar below.

    **It draws peaks, not samples.** Each frame from the tap is the peak over
    its window (`DspCore::kRibbonHz`), so a 2 ms onset cannot fall between two
    frames and vanish from the picture -- which is exactly what an ess onset
    would do at six frames a pixel.

    **It costs nothing when it is not on screen.** The tap is enabled here and
    disabled in the destructor, which is `AnalyserTap`'s contract and BMO DEQ's
    ResponseView's pattern. A session with no BMO Defang editor open pays one
    branch a sample in the DSP and nothing at all here. */
class Ribbon final : public juce::Component,
                     private juce::Timer
{
public:
    explicit Ribbon (juce::Colour accentColour);
    ~Ribbon() override;

    /** Hands over the module's tap and switches it on. Null is allowed and
        leaves the ribbon empty rather than refusing: a panel built against a
        core that offers no tap should draw a blank strip, not fail to open. */
    void setTap (AnalyserTap* tap) noexcept;

    /** The depth the reduction layer is drawn against, so the ribbon and the
        GR bar below it agree about what a full-height notch means. Follows
        RANGE. */
    void setRangeDb (float rangeDb);

    /** The strip under the well carrying the suggested frequency.

        Reserved rather than drawn inside the well -- `ui::LevelBar::kScaleRow`
        is the same shape of thing for the same reason. A number floating over
        a moving picture is read as part of it, and this one is not: the
        waveform is what happened and the suggestion is what to do about it. */
    static constexpr int kCaptionRow = 13;

    void paint (juce::Graphics&) override;

private:
    void timerCallback() override;

    /** The suggested frequency, in the corner. Takes the ink so the caller's
        one resolution of the azure against the well is not done twice. */
    void drawSuggestion (juce::Graphics&, juce::Rectangle<float> plot,
                         juce::Colour ink, int available);

    juce::Colour accent;
    AnalyserTap* source = nullptr;
    float range = 8.0f;

    /** Read once a frame into here rather than allocated in paint(). Sized for
        the whole tap, because a resize is not worth a second code path. */
    std::vector<float> frames;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Ribbon)
};

} // namespace bmo::deesser
