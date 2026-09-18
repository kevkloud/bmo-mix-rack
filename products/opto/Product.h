#pragma once

#include "core/product/SingleModuleProcessor.h"
#include "modules/opto/Module.h"
#include "modules/opto/params.h"

namespace bmo::products
{

inline ProductInfo optoInfo()
{
    return { "BMO Opto",
             { "BMO Opto", ".bmoopto" },
             opto::kVersionHint, opto::kStateVersion };
}

inline std::unique_ptr<SingleModuleProcessor> createOpto()
{
    return std::make_unique<SingleModuleProcessor> (opto::module(), optoInfo());
}

} // namespace bmo::products
