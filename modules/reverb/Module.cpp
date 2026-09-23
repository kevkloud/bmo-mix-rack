#include "Module.h"
#include "modules/reverb/TypeVoicing.h"
#include "modules/reverb/dsp/ReverbDsp.h"
#include "modules/reverb/panel/ReverbPanel.h"
#include "modules/reverb/params.h"
#include "modules/reverb/presets/FactoryPresets.h"

namespace bmo::reverb
{

const ModuleDef& module()
{
    // `#e694e0`, mauve-orchid, chosen by the owner on 2026-09-21 from a proof
    // sheet that drew four candidates through the real panel rules -- `faceOf`
    // for knob caps, `accentInk` for captions, `onAccentOf` for switch ink --
    // on both plates, rather than from hex.
    //
    // **It spends the last window in the Accents table, knowingly.** Hue 304.4
    // degrees; the admissible set, swept at 0.1 degrees over the whole circle,
    // is the single arc 298.4-309.2 degrees, and this sits at its centre. It
    // clears 32.8 degrees from BMO Dimension's lavender and 31.6 from BMO
    // CEQ's pink, both past the 26.8-degree bar that is the worst separation
    // the table has ever accepted. Contrast is 6.23:1 on the dark plate
    // against a shipped band of 5.87-7.19, and 1.89:1 on the pale one against
    // 1.64-2.00. The arithmetic and the four candidates are
    // docs/reverb/11-integration-and-test-plan.md 3 and products/AGENTS.md's
    // Accents section.
    //
    // The consequence is recorded there and is worth repeating where it will
    // be read: **BMO Dwell cannot also be violet.** Two accents would need
    // 2 x 26.8 degrees and the arc is 10.8 wide, a 16.0-degree shortfall. That
    // is Dwell's call to make, but it is now a call and not a free choice.
    //
    // `accent` is only ever a UI colour: nothing in the schema or the state
    // depends on it, and no test pins it.
    //
    // The line is left null, which means BMO (ModuleDef::lineOf).
    //
    // **One width, 380, and `expandedWidth` is 0.**
    //
    // It was 300 compact and 700 full, BMO DEQ's precedent, with the same face
    // down the left of both and three groups of knobs filling the extra
    // 400 px. The panel is a paged handheld as of 2026-09-21 and paging
    // removes the reason for a second width: six, five and six controls never
    // need to be on screen at once, and a page key under the screen reaches
    // them in one click where the expand switch reached them in one click and
    // 400 px. With `expandedWidth` at 0 the module is not expandable, so the
    // standalone header and the rack's slot bar stop offering a switch that
    // has nothing to switch -- which is the arrangement every module but BMO
    // DEQ already had.
    //
    // **It was 500 for the four-column grid and is 380 for the three-column
    // one**, since the control-set trim took the schema from thirty
    // parameters to twenty-four later the same day. 380 is not an estimate: a
    // panel insets its content by `kPad` = 10 a side, so (380 - 20) / 3 is
    // 120 px, which is exactly the cell (500 - 20) / 4 gave -- every caption
    // on this face was measured against a 120 px cell and still is. It is a
    // multiple of 20 like every other panel in the suite. `ReverbPanel`'s
    // class comment carries the grid's own argument.
    //
    // A rack slot is 80 px wider than the 300 the compact face had. **It is
    // still the widest module in the suite** -- BMO DEQ is next at 320, and
    // BMO Util is 160 -- which is what a screen, three page keys and a
    // twenty-four-control schema cost. Do not read 380 as roomy.
    static const ModuleDef def {
        kModuleId, kModuleName, kSchemaVersion,
        380, juce::Colour (0xffe694e0),
        specs(), factory(),
        [] { return createDsp(); },
        [] (ui::ModuleContext ctx) -> std::unique_ptr<ui::ModulePanel>
        {
            return std::make_unique<ReverbPanel> (std::move (ctx));
        },
        0,

        // `line` spelled out only so the field after it can be. Null is BMO,
        // which is what it was defaulting to.
        nullptr,

        // **The one module in the suite whose parameters write each other.**
        // TYPE is a voicing: selecting one re-applies its nine writable
        // constants over the parameters that hold them, every time, and the
        // engine reads five more off the same row. The engine owns this
        // rather than the panel, so it happens with no editor open --
        // `modules/reverb/TypeVoicing.h` for the whole argument, including the
        // automation conflict it knowingly creates.
        &createParamLink,

        // **The one module that offers a host mono in, stereo out**
        // (`ModuleDef::acceptsMonoInput`, opt-in by Frosty's decision on
        // 2026-09-23). A reverb on a mono source is the case the layout exists
        // for: the engine is handed the input in both channels and
        // decorrelates its own tail from it, so a mono vocal comes back with a
        // stereo room around it.
        true,
    };

    return def;
}

} // namespace bmo::reverb
