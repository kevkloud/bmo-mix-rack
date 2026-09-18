#pragma once

#include "core/product/SingleModuleProcessor.h"
#include "modules/tune/Module.h"
#include "modules/tune/params.h"

namespace bmo::products
{

/** BMO Tune RT, as the rack's products are: the module in the suite's own
    SingleModuleProcessor, which is everything this plugin needs -- the voice
    is its only input, so there is nothing MIDI or sidechain to add. */
inline ProductInfo tuneInfo()
{
    return { "BMO Tune RT",
             { "BMO Tune RT", ".bmotune" },
             tune::kVersionHint, tune::kStateVersion };
}

inline std::unique_ptr<SingleModuleProcessor> createTune()
{
    return std::make_unique<SingleModuleProcessor> (tune::module(), tuneInfo());
}

} // namespace bmo::products
