#pragma once

#include "core/product/SingleModuleProcessor.h"
#include "modules/vcomp/Module.h"
#include "modules/vcomp/params.h"

namespace bmo::products
{

inline ProductInfo vcompInfo()
{
    // The legacy pair is the BMO name this product carried until 2026-09-14,
    // and it is here for exactly one machine: 0.2.4 went onto AURORA as BMO
    // Vcomp, so a preset saved against that build sits in the old folder under
    // the old extension. One hop is enough -- unlike BMO CEQ, which needs a
    // second because it has been renamed once already.
    return { "LTV Comp",
             { "LTV Comp", ".ltvcomp", { { "BMO Vcomp", ".bmovcomp" } } },
             vcomp::kVersionHint, vcomp::kStateVersion };
}

inline std::unique_ptr<SingleModuleProcessor> createVcomp()
{
    return std::make_unique<SingleModuleProcessor> (vcomp::module(), vcompInfo());
}

} // namespace bmo::products
