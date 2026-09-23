#include "Module.h"
#include "modules/fetcomp/dsp/FetcompDsp.h"
#include "modules/fetcomp/panel/FetcompPanel.h"
#include "modules/fetcomp/params.h"
#include "modules/fetcomp/presets/FactoryPresets.h"

namespace bmo::fetcomp
{

const ModuleDef& module()
{
    // `#5489d4`, the deep faceplate-stripe blue, and **an owner-approved
    // exception to both of the rules products/AGENTS.md sets for an accent.**
    // It is 16.4 degrees from the utility azure and 20.9 from LTV Comp's
    // periwinkle, against a table whose worst shipped separation is 26.8; and
    // it measures 3.80:1 on the dark plate against a shipped band of
    // 5.87-7.19, 3.09:1 on the pale one against 1.64-2.00. No blue could have
    // passed the hue rule -- the azure-to-periwinkle gap is only 37.3 degrees
    // wide -- so this is a rule the module cannot satisfy rather than one it
    // declined to try. The passing alternative was a violet and was refused
    // for not being blue. The full note and the candidate table are in
    // products/AGENTS.md's Accents section and in
    // docs/fet-comp/11-integration-and-test-plan.md 4c; do not raise the
    // arithmetic again as a new finding.
    //
    // `accent` is only ever a UI colour: nothing in the schema or the state
    // depends on it, and no test pins it.
    //
    // The line is left null, which means BMO (ModuleDef::lineOf).
    static const ModuleDef def {
        kModuleId, kModuleName, kSchemaVersion,
        260, juce::Colour (0xff5489d4),
        specs(), factory(),
        [] { return createDsp(); },
        [] (ui::ModuleContext ctx) -> std::unique_ptr<ui::ModulePanel>
        {
            return std::make_unique<FetcompPanel> (std::move (ctx));
        },
    };

    return def;
}

} // namespace bmo::fetcomp
