#pragma once

#include "core/dsp/ModuleDsp.h"
#include "core/state/ParamSpec.h"
#include "core/ui/Line.h"
#include "core/ui/ModulePanel.h"
#include <functional>
#include <memory>

namespace bmo
{

/** Everything a product needs to know about a module: enough to run it as its
    own plugin, and enough for the rack to put it in a slot.

    A module adds itself to the suite by providing one of these -- see
    modules/AGENTS.md for the checklist.
*/
struct ModuleDef
{
    const char* id;             ///< "eq": in state files and rack presets, never changes
    const char* name;           ///< "BMO EQ"
    int schemaVersion;          ///< bumped when the spec list changes shape
    int designWidth;            ///< panel width, px -- the compact one, if it has two
    juce::Colour accent;        ///< the module's own colour

    const ParamSpecs& specs;
    const std::vector<FactoryPreset>& factoryPresets;

    std::function<std::unique_ptr<ModuleDsp>()> createDsp;
    std::function<std::unique_ptr<ui::ModulePanel> (ui::ModuleContext)> createPanel;

    /** A second, wider layout the panel can be switched to, px; 0 for a
        module with one width, which is every module but BMO DEQ.

        An expandable module opens **expanded standalone and compact in a
        rack**: standalone, the window is the module's own; in a rack it
        shares the width with up to seven others. Either can be switched per
        instance, and the choice is kept with the session -- not with presets,
        which describe sound.

        The switch is on the host's bar -- the standalone header, the rack's
        slot bar (ui::ExpandButton) -- never on the panel, whose controls all
        change the sound. The panel chooses its layout from the width it is
        given and needs no other signal. */
    int expandedWidth = 0;

    /** The product line this module belongs to, or null for BMO.

        Last in the struct and defaulted on purpose: every module builds its
        def by positional aggregate initialisation, so a field added anywhere
        but the end would silently re-bind eight other modules' members.

        Null rather than `&ui::bmoLine()` because these defs are function-local
        statics in eight translation units; resolving it at the point of use
        keeps it out of static initialisation order entirely. */
    const ui::Line* line = nullptr;

    /** The line this module is drawn as -- BMO if it names none. */
    const ui::Line& lineOf() const noexcept
    {
        return line != nullptr ? *line : ui::bmoLine();
    }

    int numParams() const noexcept { return (int) specs.size(); }

    bool isExpandable() const noexcept { return expandedWidth > designWidth; }

    int widthFor (bool expanded) const noexcept
    {
        return expanded && isExpandable() ? expandedWidth : designWidth;
    }
};

/** Where an expandable module's view is kept in a saved session: an attribute
    on its PARAMS element, written only by getStateInformation, never by the
    captureState a preset file is made from. */
inline constexpr auto kViewAttribute = "view";
inline constexpr auto kViewExpanded  = "expanded";
inline constexpr auto kViewCompact   = "compact";

} // namespace bmo
