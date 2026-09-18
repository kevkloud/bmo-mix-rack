#pragma once

#include "core/state/ParamSpec.h"

namespace bmo::sat
{

//==============================================================================
// Parameter IDs. Permanent and append-only -- see modules/eq/params.h for why,
// and tests/plugin/SatTests.cpp for the table that holds them.
//==============================================================================

inline constexpr auto kModuleId   = "sat";
inline constexpr auto kModuleName = "BMO Saturator";

inline constexpr auto kInputGain    = "input_gain";
inline constexpr auto kDrive        = "drive";
inline constexpr auto kMix          = "mix";
inline constexpr auto kOutputLevel  = "output_level";
inline constexpr auto kSatIn        = "sat_in";
inline constexpr auto kPhase        = "phase";
inline constexpr auto kAutoGain     = "auto_gain";
inline constexpr auto kOversampling = "oversampling";
inline constexpr auto kTone         = "tone";       // appended in 0.2.0

enum Index
{
    inputGain, drive, mix, outputLevel, satIn, phase, autoGain, oversampling, tone,
    count
};

inline constexpr int kVersionHint   = 1;
inline constexpr int kStateVersion  = 1;
inline constexpr int kSchemaVersion = 1;

inline const ParamSpecs& specs()
{
    using S = ParamSpec;
    using F = ParamFormat;

    static const ParamSpecs s
    {
        // Input is how hard the signal arrives at the curve, and the curve is
        // level-dependent -- a second drive control in everything but name.
        S::floatParam (kInputGain, "Input", -24.0f, 24.0f, 0.01f, 0.0f, F::Decibels),

        // Drive scales the intensity of the curve and nothing else.
        S::floatParam (kDrive, "Drive", 0.0f, 100.0f, 0.1f, 40.0f, F::Percent),
        S::floatParam (kMix,   "Mix",   0.0f, 100.0f, 0.1f, 100.0f, F::Percent),
        S::floatParam (kOutputLevel, "Output", -24.0f, 24.0f, 0.01f, 0.0f, F::Decibels),

        S::boolParam (kSatIn,    "Sat In",    true),
        S::boolParam (kPhase,    "Phase",     false),
        S::boolParam (kAutoGain, "Auto Gain", false),

        // Off by default: the curve is anti-aliased by ADAA rather than by
        // rate, and the suite's rule is that a module reports zero latency in
        // its default state.
        S::choiceParam (kOversampling, "Oversampling", { "Off", "2x", "4x", "8x" }, 0),

        // Appended last: the voicing arrived after the first release.
        S::floatParam (kTone, "Tone", 0.0f, 100.0f, 0.1f, 100.0f, F::Percent),
    };

    return s;
}

} // namespace bmo::sat
