#pragma once

#include "core/product/SingleModuleProcessor.h"
#include "modules/util/Module.h"
#include "modules/util/params.h"

namespace bmo::products
{

inline ProductInfo utilInfo()
{
    return { "BMO Util",
             { "BMO Util", ".bmoutil" },
             util::kVersionHint, util::kStateVersion };
}

inline std::unique_ptr<SingleModuleProcessor> createUtil()
{
    return std::make_unique<SingleModuleProcessor> (util::module(), utilInfo());
}

} // namespace bmo::products
