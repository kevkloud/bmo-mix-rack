#include "Module.h"
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
    // **Two widths, BMO DEQ's precedent.** 300 compact, which a rack opens it
    // at, and 700 full, which standalone opens it at. Both are multiples of
    // 20. The main face is the same seven controls and the same display at
    // either width and keeps its own 300 px: the extra 400 is the three
    // expanded groups, and nothing on the face stretches to fill it.
    static const ModuleDef def {
        kModuleId, kModuleName, kSchemaVersion,
        300, juce::Colour (0xffe694e0),
        specs(), factory(),
        [] { return createDsp(); },
        [] (ui::ModuleContext ctx) -> std::unique_ptr<ui::ModulePanel>
        {
            return std::make_unique<ReverbPanel> (std::move (ctx));
        },
        700,
    };

    return def;
}

} // namespace bmo::reverb
