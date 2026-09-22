#pragma once

#include "core/product/SingleModuleProcessor.h"
#include "modules/dwell/Module.h"
#include "modules/dwell/params.h"

namespace bmo::products
{

inline ProductInfo dwellInfo()
{
    // New in this release, so there is no legacy preset folder to read: the
    // name, the extension and the bundle id have never been anything else.
    return { "BMO Dwell",
             { "BMO Dwell", ".bmodwell" },
             dwell::kVersionHint, dwell::kStateVersion };
}

inline std::unique_ptr<SingleModuleProcessor> createDwell()
{
    return std::make_unique<SingleModuleProcessor> (dwell::module(), dwellInfo());
}

} // namespace bmo::products
