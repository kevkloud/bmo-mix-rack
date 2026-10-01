#pragma once

#include "core/state/ParamSpec.h"
#include <iterator>

namespace bmo::dwell
{

//==============================================================================
// BMO Dwell's parameter schema: **twenty-six parameters, 0-25, settled
// 2026-09-22**.
//
// See modules/eq/params.h for why a schema is permanent, and
// tests/plugin/DwellTests.cpp and tests/dsp/DwellDspTests.cpp for the two
// golden tables that pin this one.
//
// **Everything fits inside a rack slot's lanes again.** A slot carries
// `RackProcessor::kParamsPerSlot` = 32 host automation lanes; twenty-six is
// six under that, so every row here is automatable in a rack, `SlotOverflow`
// carries nothing of Dwell's, and the next idea can be argued on merit rather
// than against a ceiling. DwellTests asserts exactly that.
//
// **The lane shares the main delay's voicing rather than mirroring it**
// (DECIDED, Frosty 2026-09-22). It keeps what makes it a lane -- its own TIME,
// its own LEVEL, its tail, SEND / HOLD / CHOP and its own FX -- and runs the
// main delay's CHARACTER, STEREO, both cuts, both modulation rows and DRIVE.
// DUCK is the one main-delay row that does **not** reach it: ducking pushes
// the main wet out of the way of the dry, and the lane's whole job is to be
// heard.
//
// That deleted seven rows from the thirty-three-row table of the day before --
// `link`, `lane_character`, `lane_stereo`, `lane_low_cut`, `lane_high_cut`,
// `lane_mod_rate`, `lane_mod_depth` -- and everything after them renumbered
// with no hole left behind. `fx_link` lands at **id 25** rather than 32, and
// the panel column that carried the lane's voicing goes with them.
//
// **The mirror cost more than it bought.** Two sets of voicing controls put
// the module at thirty-three parameters and a 980 px panel with one row past
// the rack's automation ceiling, and controls were being weighed against that
// budget rather than on merit. A lane that sounds like the delay it was thrown
// out of is the ordinary case; a lane that sounds different was a switch and
// six knobs' worth of machinery for a sound nobody had asked for yet. It can
// be appended later -- additions are free and six lanes are spare -- which is
// the exact reason not to carry it now.
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
// three rows and re-type a fourth, the reason the 22nd could cut a name out of
// a choice list, and the reason the seven lane voicing rows could go the same
// day they were questioned. It is the last chance to do any of that.
//
// What moved from the twenty-parameter checkpoint, for anyone reading an older
// document: **VOICE is deleted** and everything after it renumbers, with no
// hole left behind; `throw` is **`send`**; `throw_mode` is **`lane_gain`** and
// is a bipolar float rather than a three-way choice; `freeze` is **`hold`**;
// and `chop` is new.
//
// What moved on 2026-09-22: `fx_type` drops from four entries to **three** --
// Sweep is cut, see kFxTypeNames -- the lane arrives as five rows at ids 20-24
// rather than twelve, and `fx_link` closes the table at 25. **Ids 0-19 have
// not moved** through any of it.
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
// The lane's five keep the `lane_` prefix on the main delay's own words, so a
// host's automation list reads as one delay with a throw lane on it rather
// than as two dozen unrelated rows.
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

inline constexpr auto kLaneLevel     = "lane_level";
inline constexpr auto kLaneTime      = "lane_time";
inline constexpr auto kLaneNote      = "lane_note";
inline constexpr auto kLaneFx        = "lane_fx";
inline constexpr auto kLaneFxType    = "lane_fx_type";
inline constexpr auto kLaneFxAmount  = "lane_fx_amount";

/** Id 25, and the last row in the table. See the header comment. */
inline constexpr auto kFxLink = "fx_link";

enum Index
{
    time, sync, note, feedback, character, stereo, lowCut, highCut,
    modRate, modDepth, drive, duck, mix, send, laneGain, hold, chop,
    fx, fxType, fxAmount,
    laneLevel, laneTime, laneNote, laneFx, laneFxType, laneFxAmount,
    fxLink,
    count
};

/** Twenty-seven rows into a slot's thirty-two lanes, with five to spare. The
    header comment says why that matters; DwellTests asserts it against
    `RackProcessor::kParamsPerSlot` rather than against the number 32. */
static_assert ((int) fxLink == 26, "fx_link closes the table at id 26");
static_assert ((int) count == 27, "the table allocates ids 0-26");

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
// The lane reuses `kFxTypeNames` rather than declaring its own: two lists that
// have to stay identical are two lists that can drift apart. It has no list of
// its own to reuse for the other two -- it runs the main delay's CHARACTER and
// STEREO outright from 2026-09-22, so there is one of each parameter, not two
// off one list.
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
    the stage still gated by a bool that defaults off. That is the one thing
    about this list which is not a candidate, and it survives both cuts below
    because Diffuse has stayed index 0 through both of them.

    **Seven to four on 2026-09-21** (docs/delay/15). Octave up and Octave down
    went because they compound in a feedback loop -- three repeats is three
    octaves -- and Reverse because it was the only type needing a second
    buffer, which must not be allocated on the audio thread.

    **Four to three on 2026-09-22.** **Sweep is cut** (DECIDED, Frosty): it was
    specified as sweeping VOICE's resonant centre, and VOICE was deleted the
    day before, so there is no filter left in the loop for it to sweep. LO CUT
    and HI CUT are plain one-poles with nothing to resonate. Giving the FX
    stage a resonant band-pass of its own purely to keep the name was weighed
    and is not worth a filter, a parameter's worth of tuning and a second
    thing that can self-oscillate.

    Choice lists are append-only after ship, so both cuts had to happen now or
    never. This is still a candidate list until the types are heard, and
    anything that fails listening comes *out* rather than being left in as a
    dead index. */
inline constexpr const char* kFxTypeNames[] { "Diffuse", "Pan/Tremolo", "Crush" };

inline constexpr int kDefaultNote = 8;   ///< "1/8D"

/** The lane's own division, "1/8" -- an eighth where the main delay is a dotted
    eighth, which is the lane's whole rhythmic point. Chosen so the two agree
    with their millisecond defaults at 120 BPM: 1/8D is 375 ms and 1/8 is 250,
    which is exactly what `time` and `lane_time` default to. Switching SYNC on
    at 120 therefore changes nothing, the way vcomp's COMPLEX does. */
inline constexpr int kDefaultLaneNote = 6;

//==============================================================================
// Fixed values the schema itself depends on. Everything else lives in the DSP.
//==============================================================================
/** The longest delay either engine's ring is sized for, in ms (docs/delay/10
    §10; DECIDED, Frosty 2026-09-20). The buffer is allocated at `prepare` from
    this figure and never from a parameter: 2.0 s at 192 kHz rounds up to
    524 288 samples per channel, which is 2.0 MB a ring in stereo.

    TIME and LANE TIME share the figure, so **the lane costs a second ring of
    the same size** -- and bucket-brigade's compander costs a third and fourth,
    because its expander reads the compressor's stored gain rather than
    re-detecting (10 §4), and that control ring is sized like the audio one.
    Two engines, each with audio and control, is **16 MB per instance** at
    192 kHz, or roughly 130 MB for a full rack. At 48 kHz, which is what most
    sessions run, it is about a quarter of that.

    Two earlier figures in this comment were wrong and are named so nobody
    reinstates them: 4.0 MB was one engine, left behind when the lane landed on
    2026-09-22; 8.0 MB counted both engines but not the compander's rings, and
    was corrected on 2026-09-23 when stage 2b built it.

    Bucket-brigade caps itself lower in the DSP; the allocation does not
    change. */
inline constexpr float kMaxTimeMs = 2000.0f;

/** **SYNC is live** (2026-10-01), now that PR #31 hands every module the
    host's tempo (`ModuleDsp::setTempo`). It shipped disabled until then, with
    its slot, NOTE's, LANE NOTE's and NOTE's index order already permanent
    (DECIDED, Frosty 2026-09-20 and 2026-09-23). One switch governs both
    engines; `dsp/DwellDsp.h` maps the divisions and `dsp/Timing.h` holds the
    arithmetic. Left as a flag so the panel and the adapter read one fact. */
inline constexpr bool kSyncIsEnabled = true;

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

        //== The lane, ids 20-24 ==============================================
        //
        // **Five rows, not twelve.** The lane runs the main delay's CHARACTER,
        // STEREO, LO CUT, HI CUT, MOD RATE, MOD DEPTH and DRIVE -- one set of
        // voicing controls governing both engines -- so what it declares for
        // itself is only what makes it a lane: how loud it is, how long its
        // own repeat is, and its own FX stage. Its gates and its tail are up
        // at ids 13-16, beside the main delay's own gates, because that is
        // where a hand reaches for them.
        //
        // The seven rows that stood here until 2026-09-22 -- `link` and the
        // six `lane_` voicing values -- are in the header comment, with why
        // they went and why appending them back later is the cheap direction.
        //
        // 20 -- LANE LEVEL. The lane's loudness against the main delay's wet,
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

        // 21 -- LANE TIME. The same law and the same ring size as TIME; the
        // default is shorter because a thrown word is an echo of a phrase
        // rather than the phrase's own tempo. It is the one voicing-adjacent
        // value the lane kept, and it kept it because a throw at the main
        // delay's own time is not a throw, it is a louder repeat.
        S::logParam (kLaneTime, "Lane Time", 1.0f, kMaxTimeMs, 0.01f, 250.0f, F::Milliseconds),

        // 22 -- the lane's own note division, for when SYNC is enabled. There is
        // ONE sync switch (id 1) and it governs both engines: the module is
        // either on the grid or it is not. Each engine then picks its own
        // division, which is how a quarter runs underneath while throws land
        // on a dotted eighth. Ships disabled with SYNC and NOTE.
        S::choiceParam (kLaneNote, "Lane Note", { std::begin (kNoteNames), std::end (kNoteNames) }, kDefaultLaneNote),

        // 22, 23, 24 -- the lane's FX stage, the same three rows as the main's
        // and off the same candidate list.
        //
        // **This is the one place the lane is still allowed to differ**, and
        // it is the place worth spending it: a thrown word crushed against a
        // clean main delay is a sound, where a thrown word with its own low
        // cut is a setting. `fx_link` at id 25 is what ties the trio back to
        // the main's when nobody wants that divergence.
        //
        // Lane DRIVE is deliberately absent: saturation is slow and
        // cumulative, and a thrown word decaying over a second or two has the
        // least to work with. The lane runs the main's DRIVE with everything
        // else it runs.
        S::boolParam   (kLaneFx, "Lane FX", false),
        S::choiceParam (kLaneFxType, "Lane FX Type", { std::begin (kFxTypeNames), std::end (kFxTypeNames) }, 0),
        S::floatParam  (kLaneFxAmount, "Lane FX Amount", 0.0f, 100.0f, 0.1f, 35.0f, F::Percent),

        //== Id 25, the last row ==============================================
        //
        // 25 -- FX LINK, the one bool in this table that **defaults on**.
        //
        // It ties the lane's FX trio to the main delay's, and it is the only
        // link left: the voicing rows it used to stand beside are gone, and
        // the lane follows the main delay on those outright rather than by a
        // switch. Default on for the reason the old LINK defaulted on -- a
        // fresh instance is one delay with one set of controls, and you unlink
        // to diverge rather than link to agree.
        //
        // **It is an ordinary row now.** Until 2026-09-22 it sat at id 32,
        // deliberately past a rack slot's lanes, because the table had run out
        // of them; at twenty-six parameters there are six lanes spare and no
        // reason to put a parameter anywhere but where it reads best.
        S::boolParam (kFxLink, "FX Link", true),
    };

    return s;
}

} // namespace bmo::dwell
