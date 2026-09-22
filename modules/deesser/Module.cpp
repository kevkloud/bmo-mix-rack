#include "Module.h"
#include "modules/deesser/dsp/DeesserDsp.h"
#include "modules/deesser/panel/DeesserPanel.h"
#include "modules/deesser/params.h"
#include "modules/deesser/presets/FactoryPresets.h"

namespace bmo::deesser
{

const ModuleDef& module()
{
    // `#ea9f9a`, muted coral, chosen by the owner on 2026-09-20 -- **and an
    // ordinary row in the Accents table, not an exception.** Hue 3.8 degrees
    // sits in the red gap, 27.8 from BMO CEQ's pink and 28.0 from the
    // Saturator's orange, both clear of the 26.8 bar that is the worst
    // separation the table has ever accepted; contrast is 6.39:1 on the dark
    // plate against a shipped band of 5.87-7.19, and 1.84:1 on the pale one
    // against 1.64-2.00. Six candidates were mocked on AURORA and all six
    // passed both rules; this one was taken because it is warm and because it
    // leaves the violet window free for a later module. The candidate table
    // and the figures are products/AGENTS.md's Accents section and
    // docs/deesser/11-integration-and-test-plan.md 2.
    //
    // `accent` is only ever a UI colour: nothing in the schema or the state
    // depends on it, and no test pins it.
    //
    // The line is left null, which means BMO (ModuleDef::lineOf).
    //
    // 260 wide, the suite's common single width: it is what lets the meter's
    // IN/GR/OUT row use the full switch width instead of BMO Opto's 220-px
    // exception, and what gives the band sketch a picture worth drawing.
    static const ModuleDef def {
        kModuleId, kModuleName, kSchemaVersion,
        260, juce::Colour (0xffea9f9a),
        specs(), factory(),
        [] { return createDsp(); },
        [] (ui::ModuleContext ctx) -> std::unique_ptr<ui::ModulePanel>
        {
            return std::make_unique<DeesserPanel> (std::move (ctx));
        },
    };

    return def;
}

} // namespace bmo::deesser
