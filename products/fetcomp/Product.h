#pragma once

#include "core/product/SingleModuleProcessor.h"
#include "modules/fetcomp/Module.h"
#include "modules/fetcomp/params.h"

namespace bmo::products
{

inline ProductInfo fetcompInfo()
{
    return { "BMO FET",
             { "BMO FET", ".bmofetcomp" },
             fetcomp::kVersionHint, fetcomp::kStateVersion };
}

inline std::unique_ptr<SingleModuleProcessor> createFetcomp()
{
    return std::make_unique<SingleModuleProcessor> (fetcomp::module(), fetcompInfo());
}

} // namespace bmo::products
