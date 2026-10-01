#pragma once

#include "core/product/SingleModuleProcessor.h"
#include "modules/reverb/Module.h"
#include "modules/reverb/params.h"

namespace bmo::products
{

inline ProductInfo reverbInfo()
{
    return { "BMO Linger",
             { "BMO Linger", ".bmoreverb" },
             reverb::kVersionHint, reverb::kStateVersion };
}

inline std::unique_ptr<SingleModuleProcessor> createReverb()
{
    return std::make_unique<SingleModuleProcessor> (reverb::module(), reverbInfo());
}

} // namespace bmo::products
