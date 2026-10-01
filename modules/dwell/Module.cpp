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
    // **One width, 380, and `expandedWidth` is 0** (Frosty, 2026-10-01: "go
    // with option one", BMO Linger's paged layout, chosen over a tabbed box
    // under the old face on renders of both).
    //
    // It was 280 compact and 840 expanded, BMO DEQ's mechanism, with the main
    // delay's depth and the throw lane in two columns behind an arrow. Paging
    // removes the reason for a second width exactly as it did for Linger: the
    // TONE, LANE and FX pages never need to be on screen at once, and a tab on
    // the screen reaches each in one click where the arrow reached them in one
    // click and 560 px. With `expandedWidth` at 0 the module is not
    // expandable, so the standalone header and the rack's slot bar stop
    // offering a switch with nothing to switch, and `fx` no longer opens
    // anything: the view-is-not-a-parameter rule that the arrow had to be
    // careful of has nothing left to guard.
    //
    // **380 is Linger's width, and for Linger's reason**: a panel insets by
    // `kPad` = 10 a side, so (380 - 20) / 3 is a 120 px cell, three across.
    // FEEDBACK is the one caption that does not fit one at 15 pt -- it
    // measures about 125 -- so the foot's trio takes uneven cells. See
    // `DwellPanel`.
    static const ModuleDef def {
        kModuleId, kModuleName, kSchemaVersion,
        380, juce::Colour (kAccent),
        specs(), factory(),
        [] { return createDsp(); },
        [] (ui::ModuleContext ctx) -> std::unique_ptr<ui::ModulePanel>
        {
            return std::make_unique<DwellPanel> (std::move (ctx));
        },
        0,
    };

    return def;
}

} // namespace bmo::dwell
