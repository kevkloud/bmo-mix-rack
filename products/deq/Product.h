#pragma once

#include "core/product/SingleModuleProcessor.h"
#include "modules/deq/Module.h"
#include "modules/deq/params.h"

namespace bmo::products
{

inline ProductInfo deqInfo()
{
    return { "BMO DEQ",
             { "BMO DEQ", ".bmodeq" },
             deq::kVersionHint, deq::kStateVersion };
}

inline std::unique_ptr<SingleModuleProcessor> createDeq()
{
    return std::make_unique<SingleModuleProcessor> (deq::module(), deqInfo());
}

} // namespace bmo::products
