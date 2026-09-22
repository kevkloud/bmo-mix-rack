#pragma once

#include "core/state/ParamSpec.h"
#include <iterator>

namespace bmo::dwell
{

//==============================================================================
// BMO Dwell's parameter schema: **thirty-two parameters, settled 2026-09-21**.
//
// docs/delay/15-lane-redesign.md, "THE PARAMETER TABLE", is the authoritative
// one and is what this file transcribes. **docs/delay/11 §3 is stale** -- it
// still prints the twenty-parameter checkpoint this replaced, and is rewritten
// in its own pass. See modules/eq/params.h for why a schema is permanent, and
// tests/plugin/DwellTests.cpp and tests/dsp/DwellDspTests.cpp for the two
// golden tables that pin this one.
//
// **Thirty-two is the ceiling, not a coincidence.** A rack slot carries
// `RackProcessor::kParamsPerSlot` = 32 host automation lanes. `SlotOverflow`
// keeps anything past 32 working -- in the panel, the DSP, presets and saved
// state -- but gives it no host lane, so it **cannot be automated in a rack**.
// Dwell fits exactly, so every parameter here is automatable everywhere, and
// DwellTests asserts `specs().size() <= 32` to keep it that way. Anything
// appended after ship lands past the grid and pays that price knowingly
// (docs/delay/15 names lane DRIVE as the candidate).
//
// Three things are frozen together at ship and cannot be argued separately:
// the ids, their **order**, and the **index order of the three choice lists**.
// A saved session keys automation by position and stores a choice as its index,
// so moving a row or inserting a name into a list silently repoints every lane
// and every stored preset that ever referenced it.
//
// **Additions and deletions are not equal.** After ship a parameter may be
// appended and nothing else may move: no deletion, no reorder, no rename, no
// change of TYPE. Nothing has shipped yet, which is the whole reason the
// 2026-09-21 table could delete VOICE, renumber everything after it, rename
// three rows and re-type a fourth. It is the last chance to do any of that.
//
// What moved from the twenty-parameter checkpoint, for anyone reading an older
// document: **VOICE is deleted** and everything after it renumbers, with no
// hole left behind; `throw` is **`send`**; `throw_mode` is **`lane_gain`** and
// is a bipolar float rather than a three-way choice; `freeze` is **`hold`**;
// `chop` is new; `fx_type` drops to **four** entries; and twelve **lane**
// parameters are appended at ids 20-31.
//
// `specs()` order == `enum Index` order.
//==============================================================================

inline constexpr auto kModuleId   = "dwell";
inline constexpr auto kModuleName = "BMO Dwell";

inline constexpr int kVersionHint   = 1;
inline constexpr int kStateVersion  = 1;
inline constexpr int kSchemaVersion = 1;

//==============================================================================
// The ids. Snake case in the string, camel case in the constant, which is what
// every other module here does.
//
// The lane's twelve mirror the main delay's names with a `lane_` prefix, so a
// host's automation list reads as two engines rather than as twenty-odd
// unrelated rows.
//==============================================================================

inline constexpr auto kTime      = "time";
inline constexpr auto kSync      = "sync";
inline constexpr auto kNote      = "note";
inline constexpr auto kFeedback  = "feedback";
inline constexpr auto kCharacter = "character";
inline constexpr auto kStereo    = "stereo";
inline constexpr auto kLowCut    = "low_cut";
inline constexpr auto kHighCut   = "high_cut";
inline constexpr auto kModRate   = "mod_rate";
inline constexpr auto kModDepth  = "mod_depth";
inline constexpr auto kDrive     = "drive";
inline constexpr auto kDuck      = "duck";
inline constexpr auto kMix       = "mix";
inline constexpr auto kSend      = "send";
inline constexpr auto kLaneGain  = "lane_gain";
inline constexpr auto kHold      = "hold";
inline constexpr auto kChop      = "chop";
inline constexpr auto kFx        = "fx";
inline constexpr auto kFxType    = "fx_type";
inline constexpr auto kFxAmount  = "fx_amount";

inline constexpr auto kLink          = "link";
inline constexpr auto kLaneLevel     = "lane_level";
inline constexpr auto kLaneTime      = "lane_time";
inline constexpr auto kLaneCharacter = "lane_character";
inline constexpr auto kLaneStereo    = "lane_stereo";
inline constexpr auto kLaneLowCut    = "lane_low_cut";
inline constexpr auto kLaneHighCut   = "lane_high_cut";
inline constexpr auto kLaneModRate   = "lane_mod_rate";
inline constexpr auto kLaneModDepth  = "lane_mod_depth";
inline constexpr auto kLaneFx        = "lane_fx";
inline constexpr auto kLaneFxType    = "lane_fx_type";
inline constexpr auto kLaneFxAmount  = "lane_fx_amount";

enum Index
{
    time, sync, note, feedback, character, stereo, lowCut, highCut,
    modRate, modDepth, drive, duck, mix, send, laneGain, hold, chop,
    fx, fxType, fxAmount,
    link, laneLevel, laneTime, laneCharacter, laneStereo, laneLowCut,
    laneHighCut, laneModRate, laneModDepth, laneFx, laneFxType, laneFxAmount,
    count
};

static_assert ((int) count == 32,
               "docs/delay/15's table allocates ids 0-31, which is exactly a rack slot's lanes");

//==============================================================================
// Choice lists. Index order is stored in sessions -- append only after ship.
//
// Two of them run **least to most intervention**, so index 0 is the neutral
// value a corrupt or truncated state lands on. NOTE does not: its index *is*
// the automation lane, so it ascends in duration, and a sweep of the lane
// moves monotonically in time while a clockwise knob lengthens. That is why
// the grid ships complete -- anything appended later would sit at the end,
// out of order, forever.
//
// The lane reuses these three lists rather than declaring its own: the lane is
// a mirror of the main delay, and two lists that have to stay identical are
// two lists that can drift apart.
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

/** Least to most intervention, and **index 0 is Diffuse rather than Off**:
    `fx` owns off, so a corrupt state landing on 0 gives the gentlest type with
    the stage still gated by a bool that defaults off.

    **Four, from 2026-09-21** (docs/delay/15). Octave up and Octave down are cut
    because they compound in a feedback loop -- three repeats is three octaves
    -- and Reverse because it was the only type needing a second buffer, which
    must not be allocated on the audio thread. Four also tiles as a 2x2 grid
    rather than an awkward seven. Choice lists are append-only after ship, so
    this had to happen now or never; it is still a candidate list until the
    types are heard, and anything that fails listening comes *out* rather than
    being left in as a dead index. */
inline constexpr const char* kFxTypeNames[] { "Diffuse", "Sweep", "Pan/Tremolo", "Crush" };

inline constexpr int kDefaultNote = 8;   ///< "1/8D"

//==============================================================================
// Fixed values the schema itself depends on. Everything else lives in the DSP.
//==============================================================================

/** The longest delay either engine's ring is sized for, in ms (docs/delay/10
    §10; DECIDED, Frosty 2026-09-20). The buffer is allocated at `prepare` from
    this figure and never from a parameter: 2.0 s at 192 kHz rounds up to
    524 288 samples per channel, 4.0 MB per instance. Bucket-brigade caps itself
    lower in the DSP; the allocation does not change. TIME and LANE TIME share
    the figure, so the lane costs a second ring of the same size. */
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
        //
        // **The law changes, the range and the default do not.** docs/delay/15
        // divides it by the character's peak in-loop magnitude, because tape's
        // +2 dB head bump reaches unity at 84 % rather than 97 % and a detent
        // labelled as a hold has to actually hold. That is a DSP pass of its
        // own and **is not implemented here**; nothing in this row moves when
        // it lands.
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
        //
        // With VOICE gone these are plain one-poles: VOICE only added
        // resonance on top of them, and cutting it took the peak-normalised
        // state-variable pair and `10` §11's self-oscillation warning with it.
        S::logParam (kLowCut,  "Low Cut",     20.0f,  1000.0f, 0.1f,    20.0f, F::Hertz),
        S::logParam (kHighCut, "High Cut", 1000.0f, 20000.0f, 0.1f, 20000.0f, F::Hertz),

        // 8, 9 -- modulation. DEPTH defaults to 0: wow and flutter are a
        // character someone asks for, not a thing a delay does unbidden. RATE
        // is logarithmic because it spans nearly two decades.
        S::logParam   (kModRate,  "Mod Rate",  0.1f,   8.0f, 0.01f, 0.6f, F::Hertz),
        S::floatParam (kModDepth, "Mod Depth", 0.0f, 100.0f, 0.1f,  0.0f, F::Percent),

        // 10 -- DRIVE into the in-loop shaper. Defaults to 0, where the stage
        // is inert, the same argument as BMO Opto's CRUSH and LTV Comp's
        // AMOUNT: a freshly inserted instance does nothing it was not asked to.
        S::floatParam (kDrive, "Drive", 0.0f, 100.0f, 0.1f, 0.0f, F::Percent),

        // 11 -- DUCK, in dB of reduction. **Default 0 dB** -- ducking ships
        // inert and opt-in (DECIDED, Frosty 2026-09-21; docs/delay/10 §6).
        //
        // It was 4 dB until then, on the strength of the reference survey's
        // 2-4 dB, and that is still the useful range. What overruled it is the
        // rule the rest of this table already runs on: a freshly inserted
        // instance does nothing it was not asked to, the same argument as
        // DRIVE and MOD DEPTH above.
        //
        // The range does not move: 0-24 dB. The detector's key high-pass is
        // fixed at build time and **no parameter is reserved for it**
        // (DECIDED, Frosty 2026-09-21; `10` §6). Applies to the main delay's
        // wet output after the loop tap, never inside the feedback path, so
        // ducking never shortens the tail -- and **never touches the lane**,
        // whose whole job is to be heard (docs/delay/15).
        S::floatParam (kDuck, "Duck", 0.0f, 24.0f, 0.1f, 0.0f, F::Decibels),

        // 12 -- MIX, with the hinge at 50 % (docs/delay/10 §9). Below 50 % the
        // dry path is **bit-exact unity** -- the multiply is skipped, not done
        // by 1.0 -- and only the wet moves; above it the wet holds full and
        // only the dry fades. No auto-gain at the hinge (DECIDED, Frosty
        // 2026-09-20): a trim would multiply the dry and break exactly the
        // guarantee the law exists for. The lane sums into the wet, so MIX
        // governs both engines together.
        S::floatParam (kMix, "Mix", 0.0f, 100.0f, 0.1f, 35.0f, F::Percent),

        // 13, 14, 15, 16 -- the lane's four gates and its tail (docs/delay/15).
        //
        // SEND gates the lane's *input*, a word at a time, and is meant to be
        // automated; sending onto an occupied lane sums, so words layer into a
        // chord. The main delay has no input gate at all any more, which is
        // what makes a send provably unable to disturb it.
        //
        // LANE GAIN is the tail: one bipolar knob where the old three-way
        // `throw_mode` was. Below centre the lane decays, **at 0 it holds at
        // exact unity** and above it builds -- three regions of one loop gain
        // rather than three modes, which is why one control covers them and
        // why this is a float and not a choice. The default of -40 is an
        // ordinary short decay, so a first send behaves. 0 has to land exactly
        // on the detent through a host's normalised lane, and it does: the
        // 0.1 step divides the travel evenly about the centre.
        //
        // HOLD gates the lane's life and, switched off, **clears** it -- it
        // must clear rather than mute, because a muted-but-circulating buffer
        // would stack on the next send. CHOP gates the lane's *output* only,
        // with the shortest fade that does not click, for rhythmic stuttering
        // of a held note.
        S::boolParam  (kSend, "Send", false),
        S::floatParam (kLaneGain, "Lane Gain", -100.0f, 100.0f, 0.1f, -40.0f, F::Percent),
        S::boolParam  (kHold, "Hold", false),
        S::boolParam  (kChop, "Chop", false),

        // 17, 18, 19 -- the main loop's FX stage. `fx` is the sound; TYPE and
        // AMOUNT are drawn in a **session-only view**, not a parameter --
        // automation, preset load and session recall never resize the module.
        //
        // With `fx` off the stage is skipped outright, not faded, so TYPE and
        // AMOUNT cannot leak into the sound at any setting.
        S::boolParam   (kFx, "FX", false),
        S::choiceParam (kFxType, "FX Type", { std::begin (kFxTypeNames), std::end (kFxTypeNames) }, 0),
        S::floatParam  (kFxAmount, "FX Amount", 0.0f, 100.0f, 0.1f, 35.0f, F::Percent),

        //== The lane, ids 20-31 ==============================================
        //
        // The lane is a full mirror of the main delay -- its own time,
        // character, stereo mode, filters, modulation and FX -- so that a
        // thrown word can be a different sound from the repeats it lands in.
        //
        // 20 -- LINK, and it is the one bool on this panel that **defaults
        // on**: a mirror nobody has asked to differ should follow the main
        // delay, and a fresh instance is then one delay with one set of
        // controls. Unlinking seeds the lane from the main's current values so
        // nothing jumps -- and that seeding is a **UI gesture, not a side
        // effect of this parameter changing**, or automating LINK would
        // rewrite eight parameters on every pass and fight the user's own
        // automation (docs/delay/15).
        S::boolParam (kLink, "Link", true),

        // 21 -- LANE LEVEL. The lane's loudness against the main delay's wet,
        // which LANE GAIN cannot set: that one is the tail. Without it the
        // relative volume of a thrown word would be fixed by construction,
        // which is wrong for a feature whose whole job is emphasis.
        //
        // -24..+24 dB at 0.01, matching every other level in the suite
        // (modules/eq Output, modules/opto Level, modules/vcomp Output) rather
        // than inventing a range. Default 0 dB is unity against the main wet.
        // The clip that bounds a building lane is tested at the top of this
        // travel, not at unity (docs/delay/15).
        S::floatParam (kLaneLevel, "Lane Level", -24.0f, 24.0f, 0.01f, 0.0f, F::Decibels),

        // 22 -- LANE TIME. The same law and the same ring size as TIME; the
        // default is shorter because a thrown word is an echo of a phrase
        // rather than the phrase's own tempo.
        S::logParam (kLaneTime, "Lane Time", 1.0f, kMaxTimeMs, 0.01f, 250.0f, F::Milliseconds),

        // 23, 24 -- the lane's voicing and its stereo mode, from the same two
        // lists the main delay uses.
        S::choiceParam (kLaneCharacter, "Lane Character", { std::begin (kCharacterNames), std::end (kCharacterNames) }, 0),
        S::choiceParam (kLaneStereo, "Lane Stereo", { std::begin (kStereoNames), std::end (kStereoNames) }, 0),

        // 25, 26 -- the lane's in-loop cuts, on their own rails like the
        // main's, so a fresh lane runs open.
        S::logParam (kLaneLowCut,  "Lane Low Cut",     20.0f,  1000.0f, 0.1f,    20.0f, F::Hertz),
        S::logParam (kLaneHighCut, "Lane High Cut", 1000.0f, 20000.0f, 0.1f, 20000.0f, F::Hertz),

        // 27, 28 -- the lane's modulation, inert at its default like the
        // main's.
        S::logParam   (kLaneModRate,  "Lane Mod Rate",  0.1f,   8.0f, 0.01f, 0.6f, F::Hertz),
        S::floatParam (kLaneModDepth, "Lane Mod Depth", 0.0f, 100.0f, 0.1f,  0.0f, F::Percent),

        // 29, 30, 31 -- the lane's FX stage, the same three rows as the main's
        // and off the same candidate list.
        //
        // **Lane DRIVE is deliberately absent and is the one to reconsider**
        // (docs/delay/15): saturation is slow and cumulative, and a thrown word
        // decaying over a second or two has the least to work with. If the
        // sound wants it, append it -- it would land at id 32, past the rack's
        // lanes, which costs little for a set-and-forget amount.
        S::boolParam   (kLaneFx, "Lane FX", false),
        S::choiceParam (kLaneFxType, "Lane FX Type", { std::begin (kFxTypeNames), std::end (kFxTypeNames) }, 0),
        S::floatParam  (kLaneFxAmount, "Lane FX Amount", 0.0f, 100.0f, 0.1f, 35.0f, F::Percent),
    };

    return s;
}

} // namespace bmo::dwell
