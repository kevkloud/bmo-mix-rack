#include "Module.h"
#include "modules/tune/dsp/TuneDsp.h"
#include "modules/tune/panel/TunePanel.h"
#include "modules/tune/params.h"

namespace bmo::tune
{

const ModuleDef& module()
{
    // No factory presets yet: which settings deserve a name is a listening
    // decision, and none has been listened to. The preset strip still saves
    // and loads the user's own.
    static const std::vector<FactoryPreset> factory;

    static const ModuleDef def {
        kModuleId, kModuleName, kSchemaVersion,
        TunePanel::kDesignWidth, kAccent,
        specs(), factory,
        [] { return std::make_unique<TuneDsp>(); },
        [] (ui::ModuleContext ctx) -> std::unique_ptr<ui::ModulePanel>
        {
            return std::make_unique<TunePanel> (std::move (ctx));
        },
    };

    return def;
}

} // namespace bmo::tune
