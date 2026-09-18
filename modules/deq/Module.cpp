#include "Module.h"
#include "modules/deq/dsp/DeqDsp.h"
#include "modules/deq/panel/DeqPanel.h"
#include "modules/deq/params.h"
#include "modules/deq/presets/FactoryPresets.h"

namespace bmo::deq
{

const ModuleDef& module()
{
    // The teal, held for "BMO Parametric" and passed to BMO DEQ with the rest
    // of that reservation on 2026-09-10 -- products/AGENTS.md has the
    // allocation and its known objection (26.8 degrees from the utility azure).
    //
    // Two widths: 320 compact, which a rack opens it at, and 600 full, which
    // standalone opens it at (Frosty, 2026-09-11). Both are multiples of 20.
    static const ModuleDef def {
        kModuleId, kModuleName, kSchemaVersion,
        320, juce::Colour (0xff5ecfc0),
        specs(), factory(),
        [] { return createDsp(); },
        [] (ui::ModuleContext ctx) -> std::unique_ptr<ui::ModulePanel>
        {
            return std::make_unique<DeqPanel> (std::move (ctx));
        },
        600,
    };

    return def;
}

} // namespace bmo::deq
