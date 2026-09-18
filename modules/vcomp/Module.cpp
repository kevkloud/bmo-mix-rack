#include "Module.h"
#include "modules/vcomp/dsp/VcompDsp.h"
#include "modules/vcomp/panel/VcompPanel.h"
#include "modules/vcomp/params.h"
#include "modules/vcomp/presets/FactoryPresets.h"

namespace bmo::vcomp
{

const ModuleDef& module()
{
    // Periwinkle #a2a8ff, allocated in products/AGENTS.md, which is the
    // registry -- the Palette Book is the evidence and has drifted from the
    // code twice.
    //
    // It sits at hue 236.1 degrees, in the widest gap the table had left: 37.3
    // degrees clear of the utility azure below it and 35.5 clear of BMO
    // Dimension's lavender above, which is a wider separation than the lime
    // Frosty was talked through and kept. It measures 6.17 on the dark plate,
    // inside the 5.87-7.19 the shipped accents run, and 1.91 on the pale,
    // which is second only to BMO EQ's 2.00 -- so unlike the lime it costs
    // nothing in the pale appearance.
    //
    // 260 wide, matching BMO Saturator. It is the narrowest width that fits
    // three switches at Tokens::switchWidth across the meter's row, which is
    // what lets the IN/GR/OUT row use the suite's switch size instead of BMO
    // Opto's three-way split -- see VcompPanel.h.
    static const ModuleDef def {
        kModuleId, kModuleName, kSchemaVersion,
        260, juce::Colour (0xffa2a8ff),
        specs(), factory(),
        [] { return createDsp(); },
        [] (ui::ModuleContext ctx) -> std::unique_ptr<ui::ModulePanel>
        {
            return std::make_unique<VcompPanel> (std::move (ctx));
        },
        0,                  // expandedWidth: one width, like every module but DEQ
        &ui::ltvLine(),     // the collaboration line -- silver plate, LTV marque
    };

    return def;
}

} // namespace bmo::vcomp
