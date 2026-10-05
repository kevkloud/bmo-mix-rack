#pragma once

#include "modules/util/params.h"
#include <vector>

namespace bmo::util
{

/** The presets that ship with the module. A utility has no sound of its own,
    so these are the handful of jobs it is reached for, named for the job. */
inline const std::vector<FactoryPreset>& factory()
{
    static const std::vector<FactoryPreset> presets {
        { "Init", {} },

        { "Mono Check", { { kMono, 1.0f } } },              // does the mix survive a phone

        { "Flip Polarity", { { kPhaseL, 1.0f }, { kPhaseR, 1.0f } } },

        { "Wide", { { kWidth, 200.0f } } },                 // the side doubled, the mid kept

        { "Narrow", { { kWidth, 60.0f } } },                // pulled in for a bus

        { "Pad -6", { { kGain, -6.0f } } },                 // headroom before a chain
    };

    return presets;
}

} // namespace bmo::util
