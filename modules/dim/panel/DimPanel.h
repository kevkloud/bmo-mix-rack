#pragma once

#include "core/product/ModuleDef.h"

#include <array>

namespace bmo::dim
{

/** Two sections, laid out and named in the UI pass of 2026-09-16/17 (Frosty):

        SOURCE   GENERATE, over DETUNE and DRIFT
        WIDTH    DIMENSION, then BLOOM and BELOW, then TURN and TILT

    **The panel's words are not the code's.** Captions and host names agree,
    so an automation lane is called what the knob is called, but the IDs, the
    `Index` enum and the members below keep the original words, because the
    IDs are permanent. The map is in modules/dim/AGENTS.md: DETUNE is `cents`,
    DRIFT is `diffuse`, DIMENSION is `width`, BLOOM and BELOW are `shuffle` and
    `shuffleFreq`, TURN and TILT are `rotation` and `asymmetry`, and the
    GENERATE switch is `detuneOn`.

    **Drift Rate and Drift Depth have no controls.** They are the diffuse
    stage's LFO, and the listening pass on 2026-09-09 found neither audible
    enough to earn the space -- so they are fixed at their defaults, 0.40 Hz
    and 50 %. The parameters stay in `params.h`: the IDs are permanent and
    append-only, and a host session that automated them must still load.

    Built on BMO Opto's panel rather than on BMO EQ's: blocks placed from the
    top on one derived gap, and no input or output section reserved. It had no
    rules either, on the argument that it was one idea. Frosty's legends settled
    that it is two -- make width, then shape it -- and the legends sit in gaps
    the rhythm already left, so adding them moved no control.

    **Pairs are bracketed.** Each pair of knobs has a line under its captions
    with the ends turned up, in the knobs' own track colour. It joins rather
    than divides, which is why it is painted here and is not a rule.

    The order down the panel is signal order -- generate, diffuse, image --
    which is *not* the order in params.h. That one is reach-for-first, because
    it is permanent and because it is what a host's automation list shows. The
    two are independent and each is right for the list it is in.

    DIMENSION is the hero, at 148 px where Opto puts its meter; everything else
    is paired at 64, the size the rest of the suite's paired knobs use. BELOW
    is the one knob here that prints its value -- it is a crossover, and "below
    what" is the question its caption raises -- and BLOOM keeps a blank line to
    stay level with it. TURN and TILT mark their ends L and R, since both move
    the image one way or the other rather than giving more or less of it.

    GENERATE is a switch rather than a zero position on the DETUNE knob. The
    stage it gates is the only part of the module that manufactures signal
    rather than shaping it, so it is worth being able to take out and put back
    without losing the amount you had set -- and worth reading as off at a
    glance. It is centred over its row but gates only DETUNE; DRIFT works on any
    side content.

    **A goniometer is the meter this panel wants, and it is deliberately not
    here yet.** Deferred 2026-09-08, on functionality first.

    It is not a panel change. `ui::ModuleContext` hands a panel five
    `std::function<float()>` and nothing else, and `ModuleEngine` fills them
    from `Meter` classes that reduce a block to a scalar -- so there is no path
    from the audio thread to the UI that carries L/R sample *pairs*, which is
    the one thing a Lissajous display needs. Adding one means a lock-free ring
    of pairs in `core/dsp`, a sixth member on `ModuleContext`, and a new
    component in `core/ui`. That is a core change touching every module's
    wiring, and it should be argued for on its own rather than riding in with
    module six.

    Two things to weigh when it is picked up. A **correlation meter** would fit
    the existing contract exactly -- one float in [-1, +1], one more callback,
    no new infrastructure -- and it is the same reading a goniometer is used
    for here, since the S1's "within 45 degrees of vertical" rule is a visual
    reading of correlation. And whatever is built, `tools/snapshot` feeds a
    panel no audio, so the meter renders empty in the review loop the repo
    relies on -- the same gap the Palette Book records against BMO Opto's meter
    modes.
*/
class DimPanel final : public ui::ModulePanel
{
public:
    explicit DimPanel (ui::ModuleContext);

    void resized() override;

private:
    void paintPanel (juce::Graphics&) override;

    // The three knob pairs, as laid out, so paintPanel can group them.
    std::array<juce::Rectangle<int>, 3> pairBoxes;

    ui::PlainKnob width, shuffle, shuffleFreq, cents, diffuse,
                  rotation, asymmetry;

    ui::SwitchButton detuneOn;
};

} // namespace bmo::dim
