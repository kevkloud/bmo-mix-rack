#pragma once

#include "modules/deq/params.h"
#include <vector>

namespace bmo::deq
{

/** The presets that ship with the module.

    Init is index 0 and is every default, which for this module is a wire:
    every band off, output at 0 dB. An EQ that shaped the sound the moment it
    was inserted would be making a decision nobody has made yet.

    Only Init, for now. Preset character is Frosty's call, and the listening
    test that settles serial against parallel comes first -- a preset tuned on
    a topology that then changes would have to be re-tuned by ear.
*/
inline const std::vector<FactoryPreset>& factory()
{
    static const std::vector<FactoryPreset> presets {
        { "Init", {} },
    };

    return presets;
}

} // namespace bmo::deq
