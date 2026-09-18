#pragma once

#include "core/product/SingleModuleProcessor.h"
#include "modules/eq/Module.h"
#include "modules/eq/params.h"

namespace bmo::products
{

/** BMO CEQ -- the console EQ. It was FrostyEQ, then BMO EQ, and the plugin
    code and bundle ID are still FrostyEQ's, so a session saved under either
    old name opens with this. Only the display name and the preset folder
    moved; both old folders are copied across on first run, newest first so a
    name that exists in both arrives from BMO EQ rather than from FrostyEQ. */
inline ProductInfo eqInfo()
{
    return { "BMO CEQ",
             { "BMO CEQ", ".bmoceq",
               { { "BMO EQ", ".bmoeq" }, { "FrostyEQ", ".frostyeq" } } },
             eq::kVersionHint, eq::kStateVersion };
}

inline std::unique_ptr<SingleModuleProcessor> createEq()
{
    return std::make_unique<SingleModuleProcessor> (eq::module(), eqInfo());
}

} // namespace bmo::products
