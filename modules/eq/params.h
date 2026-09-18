#pragma once

#include "core/state/ParamSpec.h"

namespace bmo::eq
{

//==============================================================================
// Parameter IDs.
//
// This is a permanent, append-only schema. Once a user saves a session,
// automation lanes and stored state are keyed by these exact strings and by
// their position in specs(). Renaming, reordering or re-ranging silently loses
// settings in every existing project. Treat a change here the way you would
// treat a wire-protocol change: don't, and if you must, add a new ID on the
// end and migrate on load. tests/plugin/EqTests.cpp holds the whole table.
//==============================================================================

inline constexpr auto kModuleId   = "eq";
inline constexpr auto kModuleName = "BMO CEQ";

inline constexpr auto kHfFreq       = "hf_freq";
inline constexpr auto kHfGain       = "hf_gain";
inline constexpr auto kMidFreq      = "mid_freq";
inline constexpr auto kMidGain      = "mid_gain";
inline constexpr auto kMidHiQ       = "mid_hiq";
inline constexpr auto kLfFreq       = "lf_freq";
inline constexpr auto kLfGain       = "lf_gain";
inline constexpr auto kHpfFreq      = "hpf_freq";
inline constexpr auto kLpfFreq      = "lpf_freq";
inline constexpr auto kInputGain    = "input_gain";
inline constexpr auto kOutputLevel  = "output_level";
inline constexpr auto kEqIn         = "eq_in";
inline constexpr auto kPhase        = "phase";
inline constexpr auto kMix          = "mix";
inline constexpr auto kAutoGain     = "auto_gain";
inline constexpr auto kOversampling = "oversampling";

/** Positions in specs(). The DSP adapter reads its values by these. */
enum Index
{
    hfFreq, hfGain, midFreq, midGain, midHiQ, lfFreq, lfGain, hpfFreq, lpfFreq,
    inputGain, outputLevel, eqIn, phase, mix, autoGain, oversampling,
    count
};

/** Bump only when adding parameters; existing entries keep their original hint. */
inline constexpr int kVersionHint   = 1;
inline constexpr int kStateVersion  = 1;
inline constexpr int kSchemaVersion = 1;

inline const ParamSpecs& specs()
{
    using S = ParamSpec;
    using F = ParamFormat;

    static const ParamSpecs s
    {
        // -- High shelf --
        S::choiceParam (kHfFreq, "HF Freq", { "10 kHz", "12 kHz", "16 kHz" }, 1),
        S::floatParam  (kHfGain, "HF Gain", -16.0f, 16.0f, 0.01f, 0.0f, F::Decibels),

        // -- Mid bell --
        S::choiceParam (kMidFreq, "Mid Freq",
                        { "360 Hz", "700 Hz", "1.6 kHz", "3.2 kHz", "4.8 kHz", "7.2 kHz" }, 2),
        S::floatParam  (kMidGain, "Mid Gain", -18.0f, 18.0f, 0.01f, 0.0f, F::Decibels),
        S::boolParam   (kMidHiQ,  "Mid Hi-Q", false),

        // -- Low shelf --
        S::choiceParam (kLfFreq, "LF Freq", { "35 Hz", "60 Hz", "110 Hz", "220 Hz" }, 1),
        S::floatParam  (kLfGain, "LF Gain", -16.0f, 16.0f, 0.01f, 0.0f, F::Decibels),

        // -- Cut filters. Frequencies per the user manual; the tests hold these
        //    strings and the tables the filters are tuned from to the same figures.
        S::choiceParam (kHpfFreq, "Low Cut",  { "Off", "45 Hz", "70 Hz", "160 Hz", "360 Hz" }, 0),
        S::choiceParam (kLpfFreq, "High Cut", { "Off", "6 kHz", "8 kHz", "10 kHz", "14 kHz", "18 kHz" }, 0),

        // -- Levels and routing --
        S::floatParam (kInputGain,   "Input",  -24.0f, 24.0f, 0.01f, 0.0f, F::Decibels),
        S::floatParam (kOutputLevel, "Output", -24.0f, 24.0f, 0.01f, 0.0f, F::Decibels),
        S::boolParam  (kEqIn,  "EQ In", true),
        S::boolParam  (kPhase, "Phase", false),
        S::floatParam (kMix, "Mix", 0.0f, 100.0f, 0.1f, 100.0f, F::Percent),
        S::boolParam  (kAutoGain, "Auto Gain", false),

        // The one module in the suite whose default is not zero-latency: the
        // 1073 model's 16 kHz shelf needs the headroom. See the plan.
        S::choiceParam (kOversampling, "Oversampling", { "Off", "2x", "4x", "8x" }, 1),
    };

    return s;
}

} // namespace bmo::eq
