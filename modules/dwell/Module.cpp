#include "Module.h"
#include "modules/dwell/dsp/DwellDsp.h"
#include "modules/dwell/panel/DwellPanel.h"
#include "modules/dwell/params.h"
#include "modules/dwell/presets/FactoryPresets.h"

namespace bmo::dwell
{

/** **The accent is NOT yet decided -- Frosty picks it from a render.** This is
    the recommended candidate, left set so the tree renders the recommendation.

    **The olive-gold that used to stand here is rejected** -- "i hate this
    color", Frosty 2026-09-21 -- and so is the pale gold `#e6e278`, which
    measured out of band at both ends. Neither comes back.

    Magenta is where the arithmetic points, and it was checked rather than
    assumed. Taking every allocated accent in `products/AGENTS.md` plus the two
    in flight -- BMO FET's `#5489d4` at 215.2 degrees and the de-esser's rose
    `#ea9f9a` at 3.8 -- and the utility azure `#4fb8e8` at 198.8, which is not
    an accent but is engaged-switch colour on this very panel, the free arc
    between BMO Dimension's lavender (271.6) and BMO EQ's pink (336.0) is
    **64.4 degrees wide, half again as wide as any other**. Its centre is 32
    degrees from both neighbours; nothing else free reaches 25.

    The indigo slot people reach for next is **not** second. It looks like one
    gap from FET's blue to Dimension's lavender, but LTV Comp's periwinkle
    (236.1) sits inside it and splits it into 20.9 and 35.5 degrees; the best
    an indigo can do is 17.8, fifth behind chartreuse and jade.

    Three magentas were rendered and measured before this one, off the pixels
    with `Inspect.exe ratio` rather than from the formula: `#ee85f5` (296.3 deg,
    5.99 dark / 1.96 pale), `#f288eb` (304.0, 6.12 / 1.92) and `#f587df` (312.0,
    6.06 / 1.94). Frosty chose the orchid `13` section 6 had proposed instead,
    which sits between the first two.

    **Measured on the render, not computed**: **6.49:1** on the dark plate
    `#2e2e32` and **1.81:1** on the pale `#efefef`, read off the knob face of a
    rendered panel in both appearances. Both are mid-band -- the shipped
    accents run 5.87 to 7.19 dark and 1.72 to 2.00 pale. Section 6 of `13`
    predicted 6.5 and 1.84 from the formula and was right.

    Its nearest neighbour is BMO EQ's pink at 29.5 degrees, with BMO Dimension's
    lavender 34.9 the other way. `13` section 6 flagged an orchid as something
    that "may read as EQ in a rack", and that objection is real and was put to
    Frosty with renders before he chose it; 29.5 still clears the 26.8 the
    shipped teal and the utility azure already live with.

    It is one literal, here, so changing it is a one-line edit. Nothing else in
    the module names a colour: a panel asks for `ui::accentInk`,
    `ui::onAccentOf` or `ui::accentTextOn` and gets this derived against the
    current appearance (modules/AGENTS.md, "Do not write a hex in a panel"). */
inline constexpr juce::uint32 kAccent = 0xfff094e6;

const ModuleDef& module()
{
    // Two widths, the mechanism BMO DEQ established: 280 compact, which a rack
    // opens it at, and 840 expanded, which standalone opens it at and which
    // carries the main delay's depth and the whole of the lane. Both are
    // multiples of 20.
    //
    // The view is session-only -- an attribute on the saved state, never a
    // parameter and never in a preset -- so automating or preset-loading `fx`
    // cannot resize the module. Where Dwell goes beyond DEQ is that its panel
    // has an arrow of its own to open and close the column, rather than only
    // the host bar's ui::ExpandButton; that touch point is the panel's, not
    // this file's.
    static const ModuleDef def {
        kModuleId, kModuleName, kSchemaVersion,
        280, juce::Colour (kAccent),
        specs(), factory(),
        [] { return createDsp(); },
        [] (ui::ModuleContext ctx) -> std::unique_ptr<ui::ModulePanel>
        {
            return std::make_unique<DwellPanel> (std::move (ctx));
        },
        // **840: three even columns, and the arithmetic is exact rather than
        // approximate.** 840 less `kPad` each side is 820 of content, which is
        // 260 + 20 + 260 + 20 + 260 -- the face's own column, the main delay's
        // depth, and the lane, with 20 px gutters. A multiple of 20, as BMO
        // DEQ's two widths are.
        //
        // **It was 980 while the lane mirrored the main delay.** The lane's
        // column had to be 400 then: its widest rows were a four-knob voicing
        // quad and an FX band laid out as a 172 px stack beside a 220 px
        // AMOUNT. Both are gone -- the voicing with the schema cut of
        // 2026-09-22, the stacked grid because a plain column takes the three
        // cells across a row the way the depth column already does -- so the
        // lane is an ordinary 260 px column and the three are even.
        //
        // **Two columns was asked for and does not fit.** Folding the depth
        // and the lane into one revealed column means 590 px of rows in the
        // 603 px between the first rule and the foot: the LOOP rule, four
        // knobs across, the GR bar, the FX rule and its band, the LANE rule,
        // the tail band, its region caption, the lane's FX rule and its band.
        // That leaves 13 px to spend as air across eleven helpings, where the
        // face spends 9 px on each of twelve. It can only be bought by cutting
        // the GR meter and the THROW / FREEZE / BUILD caption, which are two
        // of the things this pass was told to keep. Three even columns is what
        // the sixteen revealed controls actually cost; 140 px of width is what
        // the schema cut bought.
        //
        // **There is exactly one of these.** Two separate reveals -- one for
        // the main delay's depth and one for the lane -- are not available
        // however much a composition might want them: `ModuleDef` carries a
        // single `expandedWidth` and the view flag behind it is one bool.
        840,
    };

    return def;
}

} // namespace bmo::dwell
