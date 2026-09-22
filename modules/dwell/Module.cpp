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

    Three magentas were rendered at 2x, compact, expanded and lit, in both
    appearances, and measured off the pixels with `Inspect.exe ratio` rather
    than from the formula: `#ee85f5` (296.3 deg, 5.99 dark / 1.96 pale),
    this one (304.0 deg, 6.12 / 1.92) and `#f587df` (312.0 deg, 6.06 / 1.94).
    All three are inside the shipped band -- 5.87 to 7.19 on
    `#2e2e32` and 1.72 to 2.00 on `#efefef`. This one is the centre of the gap
    and the only one whose *nearest* neighbour is more than 25 degrees away.

    **Measured on the render, not computed**: 6.12:1 on the dark plate and
    1.92:1 on the pale one, both from `ee85f5`-style pixel reads of the knob
    face and the caption ink.

    It is one literal, here, so changing it is a one-line edit. Nothing else in
    the module names a colour: a panel asks for `ui::accentInk`,
    `ui::onAccentOf` or `ui::accentTextOn` and gets this derived against the
    current appearance (modules/AGENTS.md, "Do not write a hex in a panel"). */
inline constexpr juce::uint32 kAccent = 0xfff288eb;

const ModuleDef& module()
{
    // Two widths, the mechanism BMO DEQ established: 280 compact, which a rack
    // opens it at, and 460 expanded, which standalone opens it at and which
    // carries the FX column. Both are multiples of 20.
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
        // **560, not docs/delay/13 §6a's 460 -- a redesign proposal, not a
        // settled number.** 280 less the padding is a 260 px column, and this
        // panel is laid out in whole columns: 560 is two of them with a 20 px
        // gutter between. At 460 the second column is 168 px, too narrow for a
        // switch grid three cells across, so the seven FX types have to stack
        // seven deep and the wide view ends up taller and busier than the
        // compact one it was meant to relieve. Still a multiple of 20, as BMO
        // DEQ's two widths are. Frosty picks from the renders; if candidate B
        // wins this goes back to 460.
        560,
    };

    return def;
}

} // namespace bmo::dwell
