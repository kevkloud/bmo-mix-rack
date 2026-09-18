#pragma once

#include "Registry.h"
#include "core/rack/RackProcessor.h"
#include "modules/eq/params.h"
#include "modules/sat/params.h"
#include "modules/util/params.h"

namespace bmo::products
{

inline constexpr int kRackVersionHint  = 1;
inline constexpr int kRackStateVersion = 1;

inline ProductInfo rackInfo()
{
    return { "BMO Mix Rack",
             { "BMO Mix Rack", ".bmorack" },
             kRackVersionHint, kRackStateVersion };
}

/** The chains that ship. A rack preset is an order and a setting for each
    module in it; anything a module's entry does not mention is its default. */
inline const std::vector<RackPreset>& rackPresets()
{
    static const std::vector<RackPreset> presets {
        { "Init", {} },

        { "Channel Strip", {
            { util::kModuleId, {} },
            { eq::kModuleId,   {} },
            { sat::kModuleId,  {} } } },

        { "Vocal Chain", {
            { util::kModuleId, { { util::kGain, -3.0f } } },
            { eq::kModuleId,   { { eq::kHpfFreq, 2 }, { eq::kHfGain, 4.0f },
                                 { eq::kMidFreq, 3 }, { eq::kMidGain, 1.5f },
                                 { eq::kInputGain, 5.0f }, { eq::kOutputLevel, -4.2f } } },
            { sat::kModuleId,  { { sat::kInputGain, 2.0f }, { sat::kDrive, 34.0f },
                                 { sat::kOutputLevel, -1.40f } } } } },

        { "Drum Bus", {
            { eq::kModuleId,   { { eq::kMidFreq, 2 }, { eq::kMidGain, 2.0f }, { eq::kHfGain, 2.0f },
                                 { eq::kInputGain, 10.0f }, { eq::kOutputLevel, -10.0f } } },
            { sat::kModuleId,  { { sat::kInputGain, 3.0f }, { sat::kDrive, 46.0f }, { sat::kTone, 40.0f },
                                 { sat::kMix, 70.0f }, { sat::kOutputLevel, -0.77f } } },
            { util::kModuleId, { { util::kWidth, 120.0f } } } } },

        { "Mono Check", {
            { util::kModuleId, { { util::kMono, 1.0f } } } } },
    };

    return presets;
}

inline std::unique_ptr<RackProcessor> createRack()
{
    return std::make_unique<RackProcessor> (registry(), rackInfo(), rackPresets());
}

} // namespace bmo::products
