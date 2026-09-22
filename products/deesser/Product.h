#pragma once

#include "core/product/SingleModuleProcessor.h"
#include "modules/deesser/Module.h"
#include "modules/deesser/params.h"

namespace bmo::products
{

inline ProductInfo deesserInfo()
{
    return { "BMO Defang",
             { "BMO Defang", ".bmodeesser" },
             deesser::kVersionHint, deesser::kStateVersion };
}

inline std::unique_ptr<SingleModuleProcessor> createDeesser()
{
    return std::make_unique<SingleModuleProcessor> (deesser::module(), deesserInfo());
}

} // namespace bmo::products
