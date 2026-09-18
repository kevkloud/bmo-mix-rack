#pragma once

#include "core/product/SingleModuleProcessor.h"
#include "modules/sat/Module.h"
#include "modules/sat/params.h"

namespace bmo::products
{

inline ProductInfo satInfo()
{
    return { "BMO Saturator",
             { "BMO Saturator", ".bmosat" },
             sat::kVersionHint, sat::kStateVersion };
}

inline std::unique_ptr<SingleModuleProcessor> createSat()
{
    return std::make_unique<SingleModuleProcessor> (sat::module(), satInfo());
}

} // namespace bmo::products
