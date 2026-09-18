#pragma once

#include "core/state/ParamSet.h"
#include "modules/deq/dsp/Design.h"
#include "modules/deq/dsp/DspCore.h"
#include "modules/deq/panel/Analyser.h"
#include "modules/deq/params.h"
#include <juce_gui_basics/juce_gui_basics.h>

namespace bmo::deq
{

/** The curve: every band's node over the response the audio path actually has.

    The line is the bands' own matched-Z designs multiplied together -- the
    serial topology, for a centred source -- evaluated from the same code the
    DSP runs, so what is drawn cannot drift from what is heard. (A side band
    does nothing to a centred source and so does not bend it; its node is still
    drawn.) The selected band's own contribution is filled under it, and a
    dynamic band's range is a whisker from where its gain sits to where the
    dynamics can take it.

    Drag a node for frequency and gain, wheel for Q, double-click the curve to
    switch on the next free band there. Every move is a host gesture, so it
    automates and undoes like a knob.
*/
class ResponseView final : public juce::Component,
                           private juce::Timer
{
public:
    /** `hostRate` is the rate the module is actually running at, polled on the
        same timer as everything else here -- `ModuleContext::sampleRate`. Null,
        or returning 0, both mean "not prepared yet" and leave the curve on
        `kDesignRate`. */
    ResponseView (ParamSet& params, juce::Colour accent,
                  std::function<int()> selectedBand, std::function<void (int)> selectBand,
                  std::function<double()> hostRate = {});
    ~ResponseView() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

    /** Mockup C's curve: fewer grid lines and labels, small bare nodes, no gain
        scale. The compact panel's curve is 300 px wide. */
    void setCompact (bool c)
    {
        if (c != compact)
        {
            compact = c;
            rebuildPaths();   // the axis row's height, so the plot, follows it
        }
        repaint();
    }

    /** The plotting area, inside the axis labels. */
    juce::Rectangle<float> plot() const;

    /** The spectrum drawn behind the curve.

        The view owns it because the well is the view's, and because the shape
        has to be built on the same `xFor` the grid lines, the axis labels and
        the nodes all go through -- see `Analyser::buildPath`. A spectrum that
        drew its own log axis would drift from the one it is drawn against. */
    Analyser& getAnalyser() noexcept { return analyser; }

    /** The tap to draw, or null for a module with none. Enabling the tap is
        what makes the audio thread write at all. */
    void setAnalyserTap (AnalyserTap* tap) { analyser.setTap (tap); }

    /** Called with a band while it is soloed and with -1 on release.

        Momentary and never a parameter -- see `ModuleContext::setSolo`. The
        curve raises it for a right-click on a node, **including one made while
        that node is already being dragged with the left button**, which is the
        case that decided where the gesture is handled: see `mouseDrag`. */
    void onSoloChanged (std::function<void (int)> f) { onSolo = std::move (f); }

    static constexpr float kSpanDb = 24.0f;   ///< the gain range, top to centre

    /** How far the component runs past the well on each side, so a node at
        either end of the range is drawn whole. Place it with
        `wellBounds.expanded (kOverhang, 0)`. */
    static constexpr int kOverhang = 8;

private:
    struct Band
    {
        bool on = false, dynamic = false;
        Shape shape = Shape::bell;
        Placement placement = Placement::stereo;
        double hz = 1000.0, q = 0.71, gainDb = 0.0, rangeDb = 0.0;
    };

    void timerCallback() override;
    bool readBands();
    void rebuildPaths();
    void rebuildSpectrum();
    void setSolo (int band);

    float xFor (double hz) const;
    double hzFor (float x) const;
    float yFor (double db) const;
    double dbFor (float y) const;
    juce::Point<float> nodeFor (const Band&) const;
    int bandAt (juce::Point<float>) const;

    float value (int band, Control c) const { return params.getReal (indexOf (band, c)); }

    ParamSet& params;
    juce::Colour accent;
    std::function<int()> selected;
    std::function<void (int)> select;
    std::function<double()> hostRate;

    std::array<Band, kBands> bands;
    juce::Path curve, selectedFill;
    Analyser analyser;
    juce::Path spectrum;

    /** The grid the curve is evaluated on, at whatever rate the module is
        running. Rebuilt when the host re-prepares -- `DesignGrid::make` is a
        handful of trig and this happens once per rate change, so it is done on
        the timer rather than cached per rate. */
    double drawnAt = kDesignRate;
    DesignGrid grid = DesignGrid::make (kDesignRate);
    bool compact = false;

    int dragging = -1;
    int soloing = -1;
    std::function<void (int)> onSolo;
    int lastSelected = -2;   ///< per instance: two DEQs in a session each have their own

    /** The rate assumed before the host has said otherwise. It was the rate
        the curve was drawn at *always* until 2026-09-15, which put the drawn
        response up to 1 dB from the audible one in the top octave at 44.1 and
        96 k -- the curve is built from the same matched-Z design the DSP runs,
        and that design is rate-dependent by construction. */
    static constexpr double kDesignRate = 48000.0;
};

} // namespace bmo::deq
