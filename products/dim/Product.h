#pragma once

#include "core/product/SingleModuleProcessor.h"
#include "modules/dim/Module.h"
#include "modules/dim/params.h"

namespace bmo::products
{

inline ProductInfo dimInfo()
{
    return { "BMO Dimension",
             { "BMO Dimension", ".bmodim" },
             dim::kVersionHint, dim::kStateVersion };
}

inline std::unique_ptr<SingleModuleProcessor> createDim()
{
    return std::make_unique<SingleModuleProcessor> (dim::module(), dimInfo());
}

} // namespace bmo::products
