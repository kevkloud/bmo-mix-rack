#include "Module.h"
#include "modules/dwell/dsp/DwellDsp.h"
#include "modules/dwell/panel/DwellPanel.h"
#include "modules/dwell/params.h"
#include "modules/dwell/presets/FactoryPresets.h"

namespace bmo::dwell
{

/** **Jade** (DECIDED, Frosty 2026-10-01), inside the suite's band and on the
    suite's own inks: the light-mode ink is the one `ui::accentInk` derives, the
    lit buttons take the accent and the choice rows the shared azure, as every
    module's do. Hue 150.0, in the widest gap left in the rack -- 21.1 degrees
    from BMO Util's green and 22.0 from BMO DEQ's teal, which makes it the
    tightest pair after the teal and the utility azure's 26.8, and on the light
    plate the three read close. `products/AGENTS.md` carries the row.

    The road here, so it is not walked again: the orchid `#f094e6`
    (2026-09-21) until BMO Linger merged at `#e694e0`, 2.1 degrees away; then,
    the same day, Pikachu yellow `#f8d030` with a charcoal light-mode ink and
    cheek-red buttons, built and committed and then reverted for this on a
    side-by-side render. The machinery that made the yellow possible stays in
    core -- `ui::declareLightInk` -- for themes. */
inline constexpr juce::uint32 kAccent = 0xff46c988;

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
