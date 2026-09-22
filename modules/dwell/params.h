#pragma once

#include "core/state/ParamSpec.h"
#include <iterator>

namespace bmo::dwell
{

//==============================================================================
// BMO Dwell's parameter schema.
//
// **Permanent from the first release, and append-only afterwards** -- see
// modules/eq/params.h for why, docs/delay/11-integration-and-test-plan.md §3
// for the table this file is a transcription of, and tests/plugin/DwellTests.cpp
// for the golden table that pins it.
//
// Three things are frozen together and cannot be argued separately: the ids,
// their **order**, and the **index order of the four choice lists**. A saved
// session keys automation by position and stores a choice as its index, so
// moving a row or inserting a name into a list silently repoints every lane
// and every stored preset that ever referenced it. New parameters append at
// the end; new choices append at the end of their list.
//
// The one exception, and it expires at ship: **kFxTypeNames is a candidate
// list**. docs/delay/11 §3 and 14 §3 say the FX types are heard before they
// ship, and anything that fails listening comes *out* of the list rather than
// being left in as a dead index. Until ship the list and its order are free;
// after ship it is append-only like the other three.
//
// `specs()` order == `enum Index` order. All twenty are automatable, and
// twenty is inside a rack slot's 32 host lanes, so every one of them can be
// automated in the rack as well as standalone (modules/AGENTS.md, step 2).
//==============================================================================

inline constexpr auto kModuleId   = "dwell";
inline constexpr auto kModuleName = "BMO Dwell";

inline constexpr int kVersionHint   = 1;
inline constexpr int kStateVersion  = 1;
inline constexpr int kSchemaVersion = 1;

//==============================================================================
// The ids. Snake case in the string, camel case in the constant, which is what
// every other module here does -- docs/delay/11 §3 writes them camel case in
// its table because it is naming rows, not strings.
//
// kThrow's constant is kThrow and its enumerator is `throwHeld`: `throw` is a
// keyword. The id a host sees is still "throw".
//==============================================================================

inline constexpr auto kTime      = "time";
inline constexpr auto kSync      = "sync";
inline constexpr auto kNote      = "note";
inline constexpr auto kFeedback  = "feedback";
inline constexpr auto kCharacter = "character";
inline constexpr auto kStereo    = "stereo";
inline constexpr auto kLowCut    = "low_cut";
inline constexpr auto kHighCut   = "high_cut";
inline constexpr auto kVoice     = "voice";
inline constexpr auto kModRate   = "mod_rate";
inline constexpr auto kModDepth  = "mod_depth";
inline constexpr auto kDrive     = "drive";
inline constexpr auto kDuck      = "duck";
inline constexpr auto kMix       = "mix";
inline constexpr auto kThrow     = "throw";
inline constexpr auto kThrowMode = "throw_mode";
inline constexpr auto kFreeze    = "freeze";
inline constexpr auto kFx        = "fx";
inline constexpr auto kFxType    = "fx_type";
inline constexpr auto kFxAmount  = "fx_amount";

enum Index
{
    time, sync, note, feedback, character, stereo, lowCut, highCut, voice,
    modRate, modDepth, drive, duck, mix, throwHeld, throwMode, freeze,
    fx, fxType, fxAmount, count
};

static_assert ((int) count == 20, "docs/delay/11 §3 allocates ids 0-19 and no others");

//==============================================================================
// Choice lists. Index order is stored in sessions -- append only.
//
// Three of them run **least to most intervention**, so index 0 is the neutral
// value a corrupt or truncated state lands on. NOTE does not: its index *is*
// the automation lane, so it ascends in duration, and a sweep of the lane
// moves monotonically in time while a clockwise knob lengthens. That is why
// the grid ships complete -- anything appended later would sit at the end,
// out of order, forever.
//==============================================================================

/** Sixteen note values, ascending in duration. T is a triplet, D is dotted. */
inline constexpr const char* kNoteNames[]
{
    "1/32", "1/16T", "1/32D", "1/16", "1/8T", "1/16D", "1/8", "1/4T",
    "1/8D", "1/4", "1/2T", "1/4D", "1/2", "1/1T", "1/2D", "1/1"
};

/** Least to most coloured. */
inline constexpr const char* kCharacterNames[] { "Clean", "Tape", "Bucket-brigade" };

/** Least to most divergent. */
inline constexpr const char* kStereoNames[] { "Stereo", "Ping-pong", "Dual offset" };

/** Send open first, so THROW is inert on a fresh instance and the module is
    an ordinary delay until someone asks for more. */
inline constexpr const char* kThrowModeNames[] { "Send open", "Throw", "Build" };

/** Least to most intervention, and **index 0 is Diffuse rather than Off**:
    `fx` owns off, so a corrupt state landing on 0 gives the gentlest type with
    the stage still gated by a bool that defaults off. Candidates until ship;
    see the header note. */
inline constexpr const char* kFxTypeNames[]
{
    "Diffuse", "Sweep", "Pan/Tremolo", "Octave up", "Octave down", "Reverse", "Crush"
};

inline constexpr int kDefaultNote = 8;   ///< "1/8D"

//==============================================================================
// Fixed values the schema itself depends on. Everything else lives in the DSP.
//==============================================================================

/** The longest delay the ring is sized for, in ms (docs/delay/10 §10;
    DECIDED, Frosty 2026-09-20). The buffer is allocated at `prepare` from this
    figure and never from a parameter: 2.0 s at 192 kHz rounds up to 524 288
    samples per channel, 4.0 MB per instance. Bucket-brigade caps itself lower
    in the DSP; the allocation does not change. */
inline constexpr float kMaxTimeMs = 2000.0f;

/** **SYNC ships disabled.** Its slot, NOTE's slot and NOTE's index order are
    permanent from this release (DECIDED, Frosty 2026-09-20), but no host tempo
    reaches a `ModuleDsp` yet -- that is docs/delay/12's plumbing, its own
    workflow and its own pull request. Until it lands the DSP ignores both
    parameters and the panel shows the pair disabled. Flip this to true in the
    change that lands the plumbing; nothing about the schema moves with it. */
inline constexpr bool kSyncIsEnabled = false;

//==============================================================================
inline const ParamSpecs& specs()
{
    using S = ParamSpec;
    using F = ParamFormat;

    static const ParamSpecs s
    {
        // 0 -- TIME. Logarithmic: equal turns for equal ratios, which is how
        // delay time is heard, and the only law under which the short end is
        // reachable at all. The 0.01 ms step is there so the value survives
        // the round trip through a host's 32-bit normalised lane exactly
        // rather than landing a fraction off (the reason DEQ's frequencies
        // snap to 0.1 Hz); it is far below anything audible.
        S::logParam (kTime, "Time", 1.0f, kMaxTimeMs, 0.01f, 375.0f, F::Milliseconds),

        // 1 -- SYNC. Slot reserved now, feature disabled until the tempo
        // plumbing lands. See kSyncIsEnabled.
        S::boolParam (kSync, "Sync", false),

        // 2 -- NOTE. Sixteen values ascending in duration; see kNoteNames.
        S::choiceParam (kNote, "Note", { std::begin (kNoteNames), std::end (kNoteNames) }, kDefaultNote),

        // 3 -- FEEDBACK. Linear travel; the DSP maps it to loop gain as
        // g = 1.05 * fb^1.6, so unity lands near 97 % and the top of the
        // travel self-oscillates deliberately (docs/delay/10 §3). The panel
        // marks that zone. The bound that keeps it a limit cycle rather than a
        // divergence is the in-loop safety clip, not this range.
        S::floatParam (kFeedback, "Feedback", 0.0f, 100.0f, 0.1f, 35.0f, F::Percent),

        // 4 -- CHARACTER. One loop, three modes; they differ in the in-loop
        // filters, the time-change law and the modulation, not in topology.
        S::choiceParam (kCharacter, "Character", { std::begin (kCharacterNames), std::end (kCharacterNames) }, 0),

        // 5 -- STEREO.
        S::choiceParam (kStereo, "Stereo", { std::begin (kStereoNames), std::end (kStereoNames) }, 0),

        // 6, 7 -- the in-loop cuts. Both default to their own rail, so a fresh
        // instance runs the loop open. HIGH CUT's range tops out at 20 kHz and
        // the DSP caps the working corner at min(18 kHz, 0.45*fs): the cap is
        // load-bearing, it keeps the shaper's input away from Nyquist where
        // first-order antialiasing is weakest. The parameter keeps the round
        // number a user reads.
        S::logParam (kLowCut,  "Low Cut",     20.0f,  1000.0f, 0.1f,    20.0f, F::Hertz),
        S::logParam (kHighCut, "High Cut", 1000.0f, 20000.0f, 0.1f, 20000.0f, F::Hertz),

        // 8 -- VOICE, one continuous control (DECIDED, Frosty 2026-09-20: the
        // stepped voicing list is dropped). It sets the resonance of the two
        // in-loop filters, Q = 0.5 + VOICE*5.5; at 0 the pair is the neutral
        // cascade, so the default is a module that does not voice itself.
        S::floatParam (kVoice, "Voice", 0.0f, 100.0f, 0.1f, 0.0f, F::Percent),

        // 9, 10 -- modulation. DEPTH defaults to 0: wow and flutter are a
        // character someone asks for, not a thing a delay does unbidden. RATE
        // is logarithmic because it spans nearly two decades.
        S::logParam   (kModRate,  "Mod Rate",  0.1f,   8.0f, 0.01f, 0.6f, F::Hertz),
        S::floatParam (kModDepth, "Mod Depth", 0.0f, 100.0f, 0.1f,  0.0f, F::Percent),

        // 11 -- DRIVE into the in-loop shaper. Defaults to 0, where the stage
        // is inert, the same argument as BMO Opto's CRUSH and LTV Comp's
        // AMOUNT: a freshly inserted instance does nothing it was not asked to.
        S::floatParam (kDrive, "Drive", 0.0f, 100.0f, 0.1f, 0.0f, F::Percent),

        // 12 -- DUCK, in dB of reduction. **Default 0 dB** -- ducking ships
        // inert and opt-in (DECIDED, Frosty 2026-09-21; docs/delay/10 §6).
        //
        // It was 4 dB until then, on the strength of the reference survey's
        // 2-4 dB, and that is still the useful range. What overruled it is the
        // rule the rest of this table already runs on: a freshly inserted
        // instance does nothing it was not asked to, the same argument as
        // DRIVE, MOD DEPTH and VOICE above. It was also the last control here
        // whose default was not its inert end, which is what made it the one
        // control that could not honestly be hidden behind an arrow.
        //
        // The id, the slot and the range do not move: 0-24 dB at id 12. The
        // detector's key high-pass is fixed at build time and **no parameter
        // is reserved for it** (DECIDED, Frosty 2026-09-21; `10` §6). Applies
        // to the wet output after the loop tap, never inside the feedback
        // path, so ducking never shortens the tail.
        S::floatParam (kDuck, "Duck", 0.0f, 24.0f, 0.1f, 0.0f, F::Decibels),

        // 13 -- MIX, with the hinge at 50 % (docs/delay/10 §9). Below 50 % the
        // dry path is **bit-exact unity** -- the multiply is skipped, not done
        // by 1.0 -- and only the wet moves; above it the wet holds full and
        // only the dry fades. No auto-gain at the hinge (DECIDED, Frosty
        // 2026-09-20): a trim would multiply the dry and break exactly the
        // guarantee the law exists for.
        S::floatParam (kMix, "Mix", 0.0f, 100.0f, 0.1f, 35.0f, F::Percent),

        // 14, 15 -- THROW and what it does. THROW is momentary and gates the
        // input injection only, so a throw never disturbs an existing tail.
        // Its mode defaults to Send open, where THROW is inert and bit-exact.
        S::boolParam  (kThrow, "Throw", false),
        S::choiceParam (kThrowMode, "Throw Mode", { std::begin (kThrowModeNames), std::end (kThrowModeNames) }, 0),

        // 16 -- FREEZE. Its own button and its own slot, enabled in v1, never
        // folded into THROW's travel (DECIDED, Frosty 2026-09-20). Latched
        // rather than ramped: the loop becomes a bit-exact circulating buffer
        // with every in-loop stage bypassed, and a ramp would erode it.
        S::boolParam (kFreeze, "Freeze", false),

        // 17, 18, 19 -- the in-loop FX stage. `fx` is the sound and lives on
        // the compact panel; the expanded column that shows TYPE and AMOUNT is
        // a **session-only view**, not a parameter -- automation, preset load
        // and session recall never resize the module. See
        // docs/delay/11 §3 and modules/dwell/AGENTS.md.
        //
        // With `fx` off the stage is skipped outright, not faded, so TYPE and
        // AMOUNT cannot leak into the sound at any setting.
        S::boolParam   (kFx, "FX", false),
        S::choiceParam (kFxType, "FX Type", { std::begin (kFxTypeNames), std::end (kFxTypeNames) }, 0),
        S::floatParam  (kFxAmount, "FX Amount", 0.0f, 100.0f, 0.1f, 35.0f, F::Percent),
    };

    return s;
}

} // namespace bmo::dwell
