#pragma once

#include "core/state/ParamSpec.h"
#include <cmath>
#include <cstdio>

namespace bmo::reverb
{

//==============================================================================
// Parameter IDs. Permanent and append-only -- see modules/eq/params.h for why,
// and tests/plugin/ReverbTests.cpp for the table that holds them.
//
// The list, its order, the ranges, the steps, the defaults and both choice
// lists *with their index order* are the table in
// docs/reverb/11-integration-and-test-plan.md 4, which is the authoritative
// copy; 10-dsp-spec.md 6 restates the same names in the same order and the two
// were reconciled before this file was written. **Both of them still list
// thirty**, because docs/ is not this pass's to edit -- the six the trim below
// removed are named here and in AGENTS.md instead.
//
// **Thirty parameters, and the count is the decision.** It was thirty, six
// were cut on 2026-09-21, and later the same day the Reverb EQ spent six of
// the eight lanes that bought: `eqloq`, `eqmidfreq`, `eqmid`, `eqmidq`,
// `eqhiq` and `eqfilter`. A rack slot shows a host 32 lanes, so this fits with
// **two spare** and none of BMO DEQ's SlotOverflow machinery is needed.
//
// **The EQ change is purely additive.** No parameter was removed and no id
// changed meaning: `eqlofreq`/`eqlo` already were node 1's frequency and gain
// and `eqhifreq`/`eqhi` node 3's, so a state file written against the
// twenty-four restores every value it holds. What the six buy is a real
// three-node parametric -- a Q on each node, a middle bell, and a mode that
// turns the two outer nodes into cuts.
//
// **Two spare lanes is tight, and that is the trade that was made.** Freeze, a
// ducking control and the tempo-sync pair `syncon`/`syncdiv` are four
// candidates for two lanes and will now have to be argued against each other.
// The argument for spending it here is that an onboard EQ is the module's
// answer to five of the six things the control-set trim removed -- the owner's
// own sentence was "the frequencies should be handled by the onboard EQ" --
// and an EQ with no Q and no middle band could not honour it.
//
// **The trim was free only because nothing has shipped.** State is stored as
// plain values keyed by id (`ParamSet::toXml`), so any of the six re-appends
// at the end later at no cost to a saved session if listening disagrees. That
// is true of the five floats and the one bool that were cut, and it is **not**
// true of a choice: `juce::AudioParameterChoice` normalises as index/(n-1), so
// changing the count of `type` or `ermode` remaps every automation point ever
// written on that lane. Neither was touched, and neither may be.
//
//== The six that were cut, and where each one went ==========================
//
// Every one of them is **character rather than a mix move** -- what makes a
// Plate a Plate rather than what an engineer dials mid-session -- so each is
// now a constant in the per-type block below, written by the type and read by
// the engine without passing through a host lane.
//
//  - `attack` (was index 6), the tail's onset contour. Owner's call,
//    verbatim: attack should be type dependent.
//  - `decayshape` (5), the gated/linear curve. Owner's call, same sentence.
//  - `damplofreq` (8), the low damping crossover. Owner's call: "the
//    frequencies should be handled by the onboard EQ". A knee is a property of
//    a room rather than a mix decision, and `damplo` survives as a pure decay
//    multiplier at a fixed per-type knee.
//  - `damphifreq` (10), the high crossover, on the same argument -- and it
//    spanned only 1000-2100 Hz, 1.07 octaves, which the trim review called a
//    constant with a knob on it.
//  - `ershape` (18), the early cluster's rise exponent p. **The agent's call
//    and not the owner's**, flagged as such here and in AGENTS.md: a unitless
//    exponent whose end-stops the spec itself records as unconfirmed, and the
//    early cluster's contour -- the same "what kind of room is this" argument
//    the owner used for `attack` and `decayshape`.
//  - `prelink` (3), ER travelling with the pre-delayed tail. **Also the
//    agent's call.** Set-and-forget: off is the reference behaviour, every
//    type shipped it identically and nobody automates it, so it is one fixed
//    constant (`kPreLinkFixed`) rather than a per-type one.
//
// **The display name and the module id differ on purpose.** `reverb` is what
// lives in state files, rack presets and preset filenames and can never
// change; **BMO Linger** is what the panel says. `deesser`/"BMO Defang" and
// `fetcomp`/"BMO FET" are the same arrangement. Do not tidy one to match the
// other later: that breaks every saved session.
//==============================================================================

inline constexpr auto kModuleId   = "reverb";
inline constexpr auto kModuleName = "BMO Linger";

// The type. Six in v1, appended to later (10 section 1 names five more), and
// the index order is frozen -- see kTypeNames for what appending costs.
inline constexpr auto kType = "type";

// Room dimension in metres. Scales ER tap spacing and the late network's
// delays together: t_k(S) = t_k,ref * S / S_ref (10 section 3), which
// preserves the room's pattern rather than gliding it. Moving it **crossfades
// over 30 ms and never glides** -- a glide Dopplers every reflection at once,
// which on a vocal is a chorus and not a room.
inline constexpr auto kSize = "size";

// Tail-only, wet-only pre-delay. Dry is never delayed and never summed against
// a delayed copy of itself, which is the phasing trap behind the "100 % wet
// delay in front of the reverb" habit (10 section 2). **Cannot go negative**:
// a negative pre-delay delays the module's whole output against the host
// timeline, which is latency, and an automatable one would thrash PDC.
inline constexpr auto kPreDelay = "predelay";

// **Whether the early reflections travel with the pre-delayed tail is no
// longer a parameter.** Off -- ER with dry -- is the reference behaviour and
// is now fixed for every type: set-and-forget, identical in all six type
// blocks, and nobody automates it. Cut 2026-09-21 on the agent's call. It is a
// bare `bool` and not a `TypeConstants` field because no type wanted its own
// answer; if one ever does, move it into the struct rather than back onto a
// host lane.
inline constexpr bool kPreLinkFixed = false;

// T_mid, the mid-band 60 dB decay time, in seconds. The damping multipliers
// below scale it per band; they do not scale this.
inline constexpr auto kDecay = "decay";

// What feeds the tail: (1-d) * direct + d * ER, tapped from the ER bus
// *before* decorrelation and *before* the ER fader, so the two faders stay
// independent. **The caption is SOURCE**, decided by the owner 2026-09-21;
// 10 section 2 calls it Diffusion after the reference, and 11 section 4
// proposed TAIL FEED, but "diffusion" means density everywhere else in this
// suite and a third word beat both. The id does not change with the caption.
inline constexpr auto kFeed = "feed";

// The two absorbent-filter decay multipliers: T60 below the low knee is
// T_mid * damplo, above the high knee T_mid * damphi. Derived from a target
// T60(f) rather than tuned toward one, which is the whole reason the late
// network is an FDN (10 section 1).
//
// **The two knees themselves are per-type constants and not parameters** --
// `TypeConstants::dampLoFreqHz` and `dampHiFreqHz`. The owner's argument is
// that a mix reaches for the onboard EQ when it wants a frequency, and that
// where a room stops absorbing is a property of the room. So these two survive
// as pure multipliers over a knee the type sets.
inline constexpr auto kDampLo = "damplo";
inline constexpr auto kDampHi = "damphi";

// The Reverb EQ: **three nodes**, **pre both generators** rather than on the
// wet output, which is where the reference puts it (10 section 2). At the
// bottom of its travel each shelf reads "Cut" rather than "-24.0 dB", because
// -24 is where it stops being an EQ move and starts being a removal.
//
//== The three nodes, and why their shapes are fixed =========================
//
// Node 1 is a low shelf, node 2 a bell, node 3 a high shelf, **and none of the
// three has a shape selector**. Frosty's call, 2026-09-21, and the argument is
// the one `kTypeNames` makes about counts: `juce::AudioParameterChoice`
// normalises as index/(n-1), so a per-node shape list could never be revised
// after first ship without remapping every automation point written on it.
// Fixed shapes give a real three-band parametric with nothing permanent to
// regret, and BMO DEQ is already the module for arbitrary shapes.
//
// `eqlofreq`/`eqlo` and `eqhifreq`/`eqhi` **are** nodes 1 and 3; they kept
// their ids and their meanings when the middle node and the three Qs arrived,
// so no saved state refers to anything that moved.
inline constexpr auto kEqLoFreq  = "eqlofreq";
inline constexpr auto kEqLo      = "eqlo";
inline constexpr auto kEqLoQ     = "eqloq";
inline constexpr auto kEqMidFreq = "eqmidfreq";
inline constexpr auto kEqMid     = "eqmid";
inline constexpr auto kEqMidQ    = "eqmidq";
inline constexpr auto kEqHiFreq  = "eqhifreq";
inline constexpr auto kEqHi      = "eqhi";
inline constexpr auto kEqHiQ     = "eqhiq";

// **FILTER: the two outer nodes become cuts.** A bool, off by default, so a
// fresh instance is the shelving EQ that shipped before it existed.
//
// On, node 1 is a low cut and node 3 a high cut. FREQ and Q carry over
// unchanged in both modes -- a cut's corner is a corner and a cut's Q is its
// resonance, which is why the outer Qs stop at `kShelfMaxQ` rather than at a
// bell's 40 (see `kEqLoQ`'s range below). **GAIN has no meaning on a cut**, so
// `eqlo` and `eqhi` stop reaching the response and their knobs grey out; they
// are not written, so switching FILTER off restores the shelf gains the user
// had. Node 2's bell is untouched in both modes.
//
// A bool and not a third position on some node's shape list, for the reason
// above: a bool's normalisation is 0 or 1 forever, and this one is *per EQ*
// rather than per node, which is what makes it a mode rather than a shape.
inline constexpr auto kEqFilter  = "eqfilter";

/** The widest a shelf's -- or a cut's -- Q goes, and the range the two outer
    nodes' Q knobs are given rather than a limit clamped behind them.

    BMO DEQ carries the same number as `deq::kShelfMaxQ` and clamps to it,
    because a DEQ band's shape is a choice and one Q knob has to serve a bell
    at 40 as well as a shelf. Here the outer nodes can never be bells, so the
    knob itself stops where the design does: past about 2 a shelf's resonant
    bump is where the matched design is weakest near Nyquist, and a knob that
    travelled to 40 and did nothing over 2 would be lying about four fifths of
    itself. The middle node is always a bell and keeps the full 0.1-40. */
inline constexpr float kShelfMaxQ = 2.0f;

// How the early cluster is generated. Taps / Energy / Blend, index order
// frozen -- and see kErModeNames on what Blend is and is not.
inline constexpr auto kErMode = "ermode";

// The density bridge: discrete positional taps at the bottom, dense shaped
// early energy at the top, with no allpass at either end (10 section 3). It is
// a continuous weighting, not a switch, so no tap ever appears at a non-zero
// level and energy is renormalised across the sweep.
inline constexpr auto kErDensity = "erdensity";

// Energy mode's envelope. The sigma that sets how long the plateau lasts is a
// parameter; **the rise exponent p in (t/tau_r)^p is not** -- it is
// `TypeConstants::erShape`, cut from the schema on 2026-09-21 on the agent's
// call, because it is unitless, its end-stops are recorded as unconfirmed, and
// the early cluster's contour is character in exactly the way ATTACK and DECAY
// SHAPE are. It was already a per-type constant; what changed is that it is no
// longer also a knob.
//
// ER SPREAD is live in Taps mode too, since Blend reuses the Shape/Spread
// envelope -- whether it greys out in Taps mode or sits inert is 11 section
// 7's open owner-confirm question and is a panel decision, not a schema one.
inline constexpr auto kErSpread = "erspread";

// One post-ER shelf. The main anti-boxiness tool, and the fallback if the four
// order-banded per-tap filters ever have to go for CPU (10 section 6).
inline constexpr auto kErHiCut = "erhicut";

// Stepped decorrelation, seven positions. Per-channel tap permutations plus a
// lateral-spread scalar, ER only. **Position 6 is built differently** --
// Schroeder's complementary-comb pair, the only construction whose mono sum is
// provably flat, and the one where the ER vanish in mono entirely. The panel
// owes that a label (10 section 3).
inline constexpr auto kErVariation = "ervariation";

// Eight incommensurate smoothed-**random** delay modulators, not LFOs. Depth
// and rate are constrained together to the 3-cent pitch bound at the top of
// both; a visible rate in the spectrum means chorused rather than randomised,
// which is a bug.
inline constexpr auto kModDepth = "moddepth";
inline constexpr auto kModRate  = "modrate";

// M/S gain on the **tail only**. ER width is the stepped decorrelation above,
// which is a different mechanism and deliberately not folded into this one.
inline constexpr auto kWidth = "width";

// The input high-cut, ahead of both generators, on top of a fixed 20 Hz
// high-pass that is not a parameter. **Marked "owner confirm" in 11 section
// 4**: 10's body reads as 29 parameters plus an internal constant while its
// own list reads 30, and this is the one it disagrees with itself about. Kept,
// because section 2 gives it a user range a constant would not need and it is
// the only way to darken what feeds both generators independently of the EQ
// shelves. Deleting index 25 is free until first ship and returns a third
// spare lane; after that it is permanent.
inline constexpr auto kInHiCut = "inhicut";

// The two absolute trims. **Not a wet/dry pair**: each is the level of one
// generator, wet = ER + tail, and either can be switched off on its own. At
// -40 dB they read "Off" and mean it -- the bus is silent, not 40 dB down.
inline constexpr auto kErLevel   = "erlevel";
inline constexpr auto kVerbLevel = "verblevel";

inline constexpr auto kMix    = "mix";
inline constexpr auto kOutput = "output";

/** Positions in `specs()`, and in a rack slot's host lanes. Permanent **from
    first ship**, which has not happened -- the six the trim removed took their
    positions with them and everything after each one closed up, and the six
    the EQ added were then placed **beside their siblings rather than
    appended**. The ids are unchanged throughout, which is what makes a state
    file written before either change still restore every parameter it still
    has.

    **That freedom ends at first ship**, and this is the last change that has
    it. After a release the only legal move is appending at the end: a session
    stores plain values keyed by id and would survive a reshuffle, but a rack
    slot maps host lane N to parameter N (`core/rack/SlotParameter.h`), so
    moving a parameter moves the lane a DAW has already recorded automation on.
    Nothing errors and nothing warns. Readability was worth it once, here,
    because nothing has shipped; the next reader does not get the same choice.
    See modules/reverb/AGENTS.md, "Thirty parameters, and two spare lanes". */
enum Index
{
    type = 0, size, predelay, decay, feed,
    damplo, damphi,

    // The Reverb EQ, in the order the panel reads it: the mode, then each node
    // as freq/gain/Q.
    eqfilter,
    eqlofreq, eqlo, eqloq,
    eqmidfreq, eqmid, eqmidq,
    eqhifreq, eqhi, eqhiq,

    ermode, erdensity, erspread, erhicut, ervariation,
    moddepth, modrate, width, inhicut,
    erlevel, verblevel, mix, output,
    count
};

inline constexpr int kVersionHint   = 1;
inline constexpr int kStateVersion  = 1;
inline constexpr int kSchemaVersion = 1;

//==============================================================================
// The two choice lists. Index order is permanent with the ids.
//
// **Append-only means two different things here, and only one of them is
// safe.** A *session or a preset* stores plain real values keyed by parameter
// id (`ParamSet::toXml`), so a seventh type appended to the end changes
// nothing a saved file refers to. *Recorded automation* is not stored that
// way: `juce::AudioParameterChoice` normalises as index/(n-1), so going from
// six types to seven rescales every automation point ever written on this
// lane -- Room stays at 0.0 but Ambience moves from 1.0 to 0.833 and lands on
// Plate. Nothing errors and nothing warns.
//
// So appending a type is legal for state and lossy for automation, and that is
// the trade to make knowingly when types 7-10 arrive: Shaped Hall, Pattern
// Room, Positional Room, Vintage Room. **Church is no longer among them** --
// Cavern took index 3 on 2026-09-21 and carries its character, so it is struck
// from the reserve rather than waiting in it (kTypeNames).
//==============================================================================

enum TypeChoice { room = 0, chamber, hall, cavern, plate, ambience, numTypes };

/** Small to large, then plate, then ambience -- the reference core set, and
    the order is final. Ambience is the ER-star type: tiny tail, ER-dominant,
    and where "tail off, distance sets depth" lands by default.

    **Index 3 was Large Hall and is Cavern.** Settled by the owner on
    2026-09-21; 11 section 4 carries the argument. Large Hall was cut because
    the late network scales with the taps under SIZE -- Hall to Large Hall is
    tau-bar 55 to 80 ms, a factor of 1.45, inside a SIZE range spanning
    0.5-80 m -- so SIZE already covers it several times over, and its only
    non-size residual is beta, whose own ladder is indexed by size. Reference A
    offers two halls but nowhere states that the difference is size; that was
    this pack's inference and it does not hold. Cavern takes the slot, carrying
    what the pack had reserved as "Church": the long, dense, stone-reflective
    character, named secularly, so Church is struck from the reserved list
    rather than left waiting in it.

    **Renaming a position is free at any time. The count is what normalisation
    depends on, and six is unchanged** -- which is why this was a rename and
    not a cut: nothing an automation lane already holds moves. */
inline const char* const kTypeNames[] { "Room", "Chamber", "Hall", "Cavern", "Plate", "Ambience" };

enum ErModeChoice { taps = 0, energy, blend, numErModes };

/** Taps is the image-source table; Energy replaces tap times with velvet noise
    enveloped by ER SHAPE and ER SPREAD.

    **Blend is defined but unheard.** The proposal is: image-source tap *times
    and pans* from Taps, with the Energy generator's Shape/Spread envelope
    replacing the physical `(1/d) * beta^n` gain law, energy-renormalised so
    the mode change is not also a level change. That is a coherent third
    behaviour rather than a crossfade between two generators, and it is the one
    of the three that nobody has listened to. It holds index 2 now because the
    index order freezes at first ship and there is no way to insert it later --
    not because the behaviour is settled. The listening pass (11 section 6) is
    where it becomes real or becomes a synonym for one of its neighbours. */
inline const char* const kErModeNames[] { "Taps", "Energy", "Blend" };

//==============================================================================
/** **Room's per-type constants, by definition -- not merely its defaults.**

    A fresh instance opens on TYPE = Room, so whatever a fresh instance shows
    for `size`, `erdensity`, `erspread`, `moddepth`, `modrate`, `inhicut`,
    `feed`, `erlevel` and `verblevel` is a claim about what Room *is*. If the
    DSP later picks different Room constants, the panel lies about itself on
    the very first thing a user sees.

    **Fourteen, not ten**, since the 2026-09-21 control-set trim -- and nine of
    the fourteen are still parameters. `erlevel` and `verblevel` joined earlier
    the same day (11 section 4); without them Ambience was unbuildable as
    specified, because no type could set its own balance of the two generators
    and the one type whose whole character *is* that balance had nowhere to put
    it. Then `decayshape`, `attack`, `damplofreq` and `damphifreq` arrived off
    the schema, and `ershape` -- which was already here -- stopped being a knob
    as well.

    **Five of the fourteen have no parameter under them**: `erShape`,
    `decayShape`, `attack`, `dampLoFreqHz` and `dampHiFreqHz`. `typeSettings`
    therefore returns nine settings and not fourteen: those five reach the
    engine straight from this table in `ReverbDsp::paramsFrom`, keyed off the
    TYPE value in the same array, because there is no host lane to stamp them
    onto. That is the whole mechanical consequence of the trim.

    The defaults that are in neither list are ordinary defaults and are
    type-independent: `type` itself, `predelay`, `decay`, the two damping
    multipliers, the four EQ rows, `ermode`, `erhicut`, `ervariation`, `width`,
    `mix` and `output`. A type may move the *sound* those produce; it does not
    move the number the knob opens at, and selecting a type does not overwrite
    them.

    They are constants here rather than numbers inline in `specs()` so that the
    per-type table below can be checked against the same symbols the schema was
    built from instead of against a second transcription. */
namespace roomDefaults
{
    inline constexpr float kSizeM       = 12.0f;
    inline constexpr float kErDensity   = 50.0f;    ///< per cent
    inline constexpr float kErShape     = 1.0f;     ///< the contour exponent p
    inline constexpr float kErSpreadMs  = 80.0f;
    inline constexpr float kModDepthMs  = 0.28f;    ///< the 3-cent bound at 1 Hz
    inline constexpr float kModRateHz   = 0.50f;
    inline constexpr float kInHiCutHz   = 20000.0f;
    inline constexpr float kFeed        = 70.0f;    ///< per cent
    inline constexpr float kErLevelDb   = -6.0f;
    inline constexpr float kVerbLevelDb = -6.0f;

    // The four the trim brought in, and **every one of them is the number the
    // schema already had**: these were `specs()`' own defaults until
    // 2026-09-21, so a fresh Room sounds after the trim exactly as it did
    // before it, and nothing about the cut is also a retune. They are marked
    // SCHEMA rather than CALIBRATE for that reason -- a CALIBRATE number is
    // one invented here to give an ordering, and these were not invented here.
    inline constexpr float kDecayShape   = 3.50f;     // SCHEMA: 3.5 is linear, i.e. truncation off
    inline constexpr float kAttack       = 30.0f;     // SCHEMA: per cent of the 0-120 ms onset
    inline constexpr float kDampLoFreqHz = 200.0f;    // SCHEMA
    inline constexpr float kDampHiFreqHz = 1600.0f;   // SCHEMA
}

//==============================================================================
/** One type's block of the constants that are also **parameters**.

    This is the visible half of what 10 section 1 calls a type's constant
    block. The invisible half -- the ER tap table, the eight FDN times, the
    input-diffusion depth, beta, the per-tap cutoff law and the three reserved
    era fields -- has no host lane and belongs in `dsp/`, where it can be
    retuned without touching the schema. What is here is only the part a knob
    shows, because that is the part a type change has to *write*.

    The field order is `roomDefaults`' order exactly -- the two levels, then
    the four the trim brought in, each appended as it arrived -- so the two
    lists can be read against each other line by line.

    **The last five fields have no parameter.** `erShape` lost its knob in the
    trim; `decayShape`, `attack`, `dampLoFreqHz` and `dampHiFreqHz` arrived in
    it. Nothing writes them onto a host lane, so `typeSettings` does not list
    them and `TypeVoicing` never sees them: they reach the engine directly, in
    `ReverbDsp::paramsFrom`, from the row the TYPE value selects. A reader
    adding a field should decide which half it is in before adding it. */
struct TypeConstants
{
    float sizeM;
    float erDensity;     ///< per cent
    float erShape;       ///< the contour exponent p -- engine only, no host lane
    float erSpreadMs;
    float modDepthMs;
    float modRateHz;
    float inHiCutHz;
    float feed;          ///< per cent
    float erLevelDb;
    float verbLevelDb;

    //== No parameter under any of these four ==================================
    float decayShape;    ///< 0.04..3.5; 3.5 is linear, i.e. truncation off
    float attack;        ///< per cent of the 0-120 ms onset
    float dampLoFreqHz;  ///< the low absorption knee
    float dampHiFreqHz;  ///< the high absorption knee
};

//==============================================================================
// **Room's row is real. The other five are CALIBRATE placeholders, and every
// number in them is marked.**
//
// 10 section 1 names the categories a type's constant block holds and gives no
// numbers for any of them; 10 section 7 is the table they will come from and
// nearly every row there is already marked CALIBRATE. So there is nothing to
// transcribe yet, and a plausible-looking number typed here and left unmarked
// would be indistinguishable, six months from now, from one that had been
// fitted by ear.
//
// **What is claimed of the placeholder rows, and it is the only thing:** the
// ordering the type list already fixes. Room < Chamber < Hall < Cavern on
// SIZE; Ambience is the small one; Plate has no room geometry at all and its
// SIZE is a stand-in for a plate's dimensions rather than a room's. Nothing
// else -- not a ratio, not a curve, not a level -- is derived from anything.
// They exist so that selecting a type does something audible to argue with
// during the listening pass (11 section 6, milestone M6), which is how they
// stop being placeholders.
//
// **The four the trim brought in claim this much and no more:**
//
//  - `attack`. **Plate is 0 and that one is not a guess** -- "at 0 the tail is
//    immediate, which is plate behaviour" is the spec's own sentence, and it
//    is the whole reason the owner said attack should be type dependent. The
//    rest rise with the room: Ambience under Room under Chamber under Hall
//    under Cavern, because a larger room's tail arrives later behind its ER.
//  - `dampHiFreqHz`. It falls as the room gets larger and stonier -- Plate
//    highest, Cavern lowest -- which is the *same* ordering `inHiCutHz`
//    already carries in these rows, and it is claimed only because the two
//    disagreeing would be incoherent rather than merely unfitted.
//  - `dampLoFreqHz` and `decayShape` claim **nothing at all**. Every type
//    ships 3.50 on the shape, which is linear, which is the truncation switched
//    off: a reverb should not arrive gated, and no type in the pack is
//    described as one. The low knee moves a little with the room for the same
//    reason the high one does and should be read as unfitted.
//
// **The shape is what is real.** When the fitted table lands it drops into
// these namespaces value for value: same names, same units, same fourteen
// fields, no change to `kTypeConstants`, `typeSettings` or anything that reads
// them. Replace the numbers and delete the CALIBRATE markers as each one is
// heard.
//==============================================================================

namespace chamberDefaults
{
    inline constexpr float kSizeM       = 18.0f;      // CALIBRATE
    inline constexpr float kErDensity   = 55.0f;      // CALIBRATE
    inline constexpr float kErShape     = 1.0f;       // CALIBRATE
    inline constexpr float kErSpreadMs  = 90.0f;      // CALIBRATE
    inline constexpr float kModDepthMs  = 0.28f;      // CALIBRATE
    inline constexpr float kModRateHz   = 0.50f;      // CALIBRATE
    inline constexpr float kInHiCutHz   = 20000.0f;   // CALIBRATE
    inline constexpr float kFeed        = 70.0f;      // CALIBRATE
    inline constexpr float kErLevelDb   = -6.0f;      // CALIBRATE
    inline constexpr float kVerbLevelDb = -6.0f;      // CALIBRATE

    inline constexpr float kDecayShape   = 3.50f;     // CALIBRATE -- linear, like every type
    inline constexpr float kAttack       = 35.0f;     // CALIBRATE
    inline constexpr float kDampLoFreqHz = 200.0f;    // CALIBRATE
    inline constexpr float kDampHiFreqHz = 1600.0f;   // CALIBRATE
}

namespace hallDefaults
{
    inline constexpr float kSizeM       = 34.0f;      // CALIBRATE
    inline constexpr float kErDensity   = 60.0f;      // CALIBRATE
    inline constexpr float kErShape     = 1.20f;      // CALIBRATE
    inline constexpr float kErSpreadMs  = 120.0f;     // CALIBRATE
    inline constexpr float kModDepthMs  = 0.30f;      // CALIBRATE
    inline constexpr float kModRateHz   = 0.45f;      // CALIBRATE
    inline constexpr float kInHiCutHz   = 18000.0f;   // CALIBRATE
    inline constexpr float kFeed        = 65.0f;      // CALIBRATE
    inline constexpr float kErLevelDb   = -8.0f;      // CALIBRATE
    inline constexpr float kVerbLevelDb = -5.0f;      // CALIBRATE

    inline constexpr float kDecayShape   = 3.50f;     // CALIBRATE -- linear, like every type
    inline constexpr float kAttack       = 50.0f;     // CALIBRATE
    inline constexpr float kDampLoFreqHz = 180.0f;    // CALIBRATE
    inline constexpr float kDampHiFreqHz = 1400.0f;   // CALIBRATE
}

namespace cavernDefaults
{
    inline constexpr float kSizeM       = 55.0f;      // CALIBRATE
    inline constexpr float kErDensity   = 70.0f;      // CALIBRATE
    inline constexpr float kErShape     = 1.40f;      // CALIBRATE
    inline constexpr float kErSpreadMs  = 160.0f;     // CALIBRATE
    inline constexpr float kModDepthMs  = 0.35f;      // CALIBRATE
    inline constexpr float kModRateHz   = 0.35f;      // CALIBRATE
    inline constexpr float kInHiCutHz   = 14000.0f;   // CALIBRATE
    inline constexpr float kFeed        = 60.0f;      // CALIBRATE
    inline constexpr float kErLevelDb   = -10.0f;     // CALIBRATE
    inline constexpr float kVerbLevelDb = -4.0f;      // CALIBRATE

    inline constexpr float kDecayShape   = 3.50f;     // CALIBRATE -- linear, like every type
    inline constexpr float kAttack       = 65.0f;     // CALIBRATE
    inline constexpr float kDampLoFreqHz = 160.0f;    // CALIBRATE
    inline constexpr float kDampHiFreqHz = 1100.0f;   // CALIBRATE
}

namespace plateDefaults
{
    inline constexpr float kSizeM       = 22.0f;      // CALIBRATE
    inline constexpr float kErDensity   = 85.0f;      // CALIBRATE
    inline constexpr float kErShape     = 0.60f;      // CALIBRATE
    inline constexpr float kErSpreadMs  = 45.0f;      // CALIBRATE
    inline constexpr float kModDepthMs  = 0.22f;      // CALIBRATE
    inline constexpr float kModRateHz   = 0.60f;      // CALIBRATE
    inline constexpr float kInHiCutHz   = 20000.0f;   // CALIBRATE
    inline constexpr float kFeed        = 40.0f;      // CALIBRATE
    inline constexpr float kErLevelDb   = -12.0f;     // CALIBRATE
    inline constexpr float kVerbLevelDb = -5.0f;      // CALIBRATE

    inline constexpr float kDecayShape   = 3.50f;     // CALIBRATE -- linear, like every type
    // **Zero, and this one is the spec's own sentence rather than a
    // placeholder's ordering**: at 0 the tail is immediate, which is plate
    // behaviour. It is still marked, because the *contour* between 0 and 100
    // is CALIBRATE even where an end-stop is not.
    inline constexpr float kAttack       = 0.0f;      // CALIBRATE (the value is not; the contour is)
    inline constexpr float kDampLoFreqHz = 250.0f;    // CALIBRATE
    inline constexpr float kDampHiFreqHz = 2000.0f;   // CALIBRATE
}

/** Ambience, **the row the whole change was made for**. The two levels are the
    only thing in this file that says what an ER-star type is: a loud early
    cluster and a tail that is present but well under it. The numbers are
    CALIBRATE like the rest of the row -- what is not CALIBRATE is that
    `kErLevelDb` is above `kVerbLevelDb` here and below it in every other type,
    which is the ordering the pack describes and the one to preserve if these
    are retuned. */
namespace ambienceDefaults
{
    inline constexpr float kSizeM       = 8.0f;       // CALIBRATE
    inline constexpr float kErDensity   = 40.0f;      // CALIBRATE
    inline constexpr float kErShape     = 0.80f;      // CALIBRATE
    inline constexpr float kErSpreadMs  = 30.0f;      // CALIBRATE
    inline constexpr float kModDepthMs  = 0.20f;      // CALIBRATE
    inline constexpr float kModRateHz   = 0.55f;      // CALIBRATE
    inline constexpr float kInHiCutHz   = 20000.0f;   // CALIBRATE
    inline constexpr float kFeed        = 85.0f;      // CALIBRATE
    inline constexpr float kErLevelDb   = -4.0f;      // CALIBRATE
    inline constexpr float kVerbLevelDb = -20.0f;     // CALIBRATE

    inline constexpr float kDecayShape   = 3.50f;     // CALIBRATE -- linear, like every type
    inline constexpr float kAttack       = 10.0f;     // CALIBRATE
    inline constexpr float kDampLoFreqHz = 200.0f;    // CALIBRATE
    inline constexpr float kDampHiFreqHz = 1800.0f;   // CALIBRATE
}

/** The six rows, in the frozen index order. Built from the namespaces above
    rather than from literals, so the symbols the schema is built from and the
    table a type change stamps cannot drift apart -- Room's row is
    `roomDefaults` itself, which is what makes "Room's defaults are Room's
    constants" true by construction instead of by a test. */
inline constexpr TypeConstants kTypeConstants[numTypes]
{
    { roomDefaults::kSizeM,        roomDefaults::kErDensity,   roomDefaults::kErShape,
      roomDefaults::kErSpreadMs,   roomDefaults::kModDepthMs,  roomDefaults::kModRateHz,
      roomDefaults::kInHiCutHz,    roomDefaults::kFeed,
      roomDefaults::kErLevelDb,    roomDefaults::kVerbLevelDb,
      roomDefaults::kDecayShape,   roomDefaults::kAttack,
      roomDefaults::kDampLoFreqHz, roomDefaults::kDampHiFreqHz },

    { chamberDefaults::kSizeM,        chamberDefaults::kErDensity,  chamberDefaults::kErShape,
      chamberDefaults::kErSpreadMs,   chamberDefaults::kModDepthMs, chamberDefaults::kModRateHz,
      chamberDefaults::kInHiCutHz,    chamberDefaults::kFeed,
      chamberDefaults::kErLevelDb,    chamberDefaults::kVerbLevelDb,
      chamberDefaults::kDecayShape,   chamberDefaults::kAttack,
      chamberDefaults::kDampLoFreqHz, chamberDefaults::kDampHiFreqHz },

    { hallDefaults::kSizeM,        hallDefaults::kErDensity,  hallDefaults::kErShape,
      hallDefaults::kErSpreadMs,   hallDefaults::kModDepthMs, hallDefaults::kModRateHz,
      hallDefaults::kInHiCutHz,    hallDefaults::kFeed,
      hallDefaults::kErLevelDb,    hallDefaults::kVerbLevelDb,
      hallDefaults::kDecayShape,   hallDefaults::kAttack,
      hallDefaults::kDampLoFreqHz, hallDefaults::kDampHiFreqHz },

    { cavernDefaults::kSizeM,        cavernDefaults::kErDensity,  cavernDefaults::kErShape,
      cavernDefaults::kErSpreadMs,   cavernDefaults::kModDepthMs, cavernDefaults::kModRateHz,
      cavernDefaults::kInHiCutHz,    cavernDefaults::kFeed,
      cavernDefaults::kErLevelDb,    cavernDefaults::kVerbLevelDb,
      cavernDefaults::kDecayShape,   cavernDefaults::kAttack,
      cavernDefaults::kDampLoFreqHz, cavernDefaults::kDampHiFreqHz },

    { plateDefaults::kSizeM,        plateDefaults::kErDensity,  plateDefaults::kErShape,
      plateDefaults::kErSpreadMs,   plateDefaults::kModDepthMs, plateDefaults::kModRateHz,
      plateDefaults::kInHiCutHz,    plateDefaults::kFeed,
      plateDefaults::kErLevelDb,    plateDefaults::kVerbLevelDb,
      plateDefaults::kDecayShape,   plateDefaults::kAttack,
      plateDefaults::kDampLoFreqHz, plateDefaults::kDampHiFreqHz },

    { ambienceDefaults::kSizeM,        ambienceDefaults::kErDensity,  ambienceDefaults::kErShape,
      ambienceDefaults::kErSpreadMs,   ambienceDefaults::kModDepthMs, ambienceDefaults::kModRateHz,
      ambienceDefaults::kInHiCutHz,    ambienceDefaults::kFeed,
      ambienceDefaults::kErLevelDb,    ambienceDefaults::kVerbLevelDb,
      ambienceDefaults::kDecayShape,   ambienceDefaults::kAttack,
      ambienceDefaults::kDampLoFreqHz, ambienceDefaults::kDampHiFreqHz },
};

/** A detent's row, with anything out of range reading as Room. */
inline constexpr const TypeConstants& constantsFor (int typeIndex) noexcept
{
    return kTypeConstants[(size_t) (typeIndex >= 0 && typeIndex < numTypes ? typeIndex : (int) room)];
}

/** A type's block as the **nine** parameter writes that apply it, in real
    units -- nine and not fourteen, because five of the row's fields have no
    parameter to write. `erShape` lost its knob in the 2026-09-21 trim and
    `decayShape`, `attack`, `dampLoFreqHz` and `dampHiFreqHz` arrived in it, so
    those five reach the engine in `ReverbDsp::paramsFrom` straight from
    `constantsFor` instead.

    **That is not a gap and it does not need closing.** A `Setting` can only
    name a parameter id; a field with no id has nothing to be set on. It does
    mean the five are applied by the engine on the next block rather than by
    `TypeVoicing` on the message thread -- which is strictly the safer half,
    since nothing a host is automating can fight them.

    **A `Setting` list and not a bespoke struct on purpose**: this is exactly
    what a factory preset is (`FactoryPreset::settings`), so applying a type
    goes through `ParamSet::apply` -- the same call, on the same values, in the
    same units -- rather than through a second path that could disagree with a
    preset recall about what writing a parameter means. See
    `modules/reverb/TypeVoicing.h`, which is the only caller.

    `type` is **not** in the list it returns, and that is load-bearing: it is
    what makes it impossible for applying a type to select another one. */
inline std::vector<Setting> typeSettings (int typeIndex)
{
    const auto& c = constantsFor (typeIndex);

    return {
        { kSize,      c.sizeM },
        { kErDensity, c.erDensity },
        { kErSpread,  c.erSpreadMs },
        { kModDepth,  c.modDepthMs },
        { kModRate,   c.modRateHz },
        { kInHiCut,   c.inHiCutHz },
        { kFeed,      c.feed },
        { kErLevel,   c.erLevelDb },
        { kVerbLevel, c.verbLevelDb },
    };
}

//==============================================================================
/** The value strings that carry a word as well as a number.

    Every one of them is **ASCII only**, and that is a build constraint rather
    than a preference: the two display faces are licensed individually and live
    outside this repository, so a glyph outside ASCII is one this suite cannot
    promise it can draw. That is why the damping multipliers print "1.20x" and
    their captions read "LOW x" rather than using a multiplication sign. */
namespace detail
{
    /** Where `value` falls across `min`..`max`, as one of `n` words. */
    inline const char* ladder (float value, float min, float max,
                               const char* const* words, int n)
    {
        const auto span = max - min;
        const auto t = span > 0.0f ? (value - min) / span : 0.0f;
        auto i = (int) (t * (float) n);
        i = i < 0 ? 0 : (i >= n ? n - 1 : i);
        return words[i];
    }

    // **`decayShapeText` and `attackText` were here and went with the trim.**
    // A value string exists to make a host's automation lane readable, and
    // neither DECAY SHAPE nor ATTACK has a lane any more. The words they
    // printed -- Gated / Steep / Natural / Long / Linear, and the tail onset in
    // milliseconds -- are not lost: the onset is what the TAIL page's readout
    // line prints from `TypeConstants::attack`, and the shape ladder was only
    // ever a gloss on a number the type now owns. Restore them with the
    // parameter if one ever comes back.

    /** "70 % (Mostly Early)". d = 0 feeds the tail from the direct signal --
        a unified diffuse reverb with no articulated pattern -- and d = 1 feeds
        it from the ER bus, so the tail inherits the room's timing, colour and
        spacing. The two ends are the two references the design is between. */
    inline std::string feedText (float percent)
    {
        static const char* const words[] { "Direct", "Mostly Direct", "Mixed",
                                           "Mostly Early", "Early" };

        char buf[96];
        std::snprintf (buf, sizeof (buf), "%d %% (%s)", (int) std::lround (percent),
                       ladder (percent, 0.0f, 100.0f, words, 5));
        return buf;
    }

    /** "50 % (Diffuse)". One knob from discrete positional taps to dense early
        energy, with the tap count rising through a ramp rather than a switch. */
    inline std::string densityText (float percent)
    {
        static const char* const words[] { "Discrete", "Sparse", "Diffuse", "Thick", "Dense" };

        char buf[64];
        std::snprintf (buf, sizeof (buf), "%d %% (%s)", (int) std::lround (percent),
                       ladder (percent, 0.0f, 100.0f, words, 5));
        return buf;
    }

    // `shapeText` printed "p 1.00" for ER SHAPE and went with it in the trim,
    // for `decayShapeText`'s reason. Its argument -- that a bare "1.00" beside
    // ER SPREAD's milliseconds reads as a time -- was the clue that p was a
    // poor knob in the first place.

    /** "Var 2". Seven decorrelation positions, and **Var 6 is not more of Var
        5**: it is Schroeder's complementary-comb pair, the widest setting and
        the only provably uncoloured-in-mono one, and the ER disappear entirely
        in a mono sum there. The string says so, because it is the only place
        an automation lane can. */
    inline std::string variationText (float v)
    {
        const auto i = (int) std::lround (v);

        char buf[64];
        if (i >= 6)
            std::snprintf (buf, sizeof (buf), "Var 6 (mono null)");
        else
            std::snprintf (buf, sizeof (buf), "Var %d", i);

        return buf;
    }

    /** "1.20x" -- a decay *multiplier* on T_mid, not a gain and not a time. A
        bare "1.20" would read as either. */
    inline std::string multiplierText (float m)
    {
        char buf[32];
        std::snprintf (buf, sizeof (buf), "%.2fx", (double) m);
        return buf;
    }

    /** "+3.0 dB", or "Cut" at the bottom of the travel. -24 dB on a shelf is
        where an EQ move stops being a move and becomes a removal, and the word
        is worth more there than the number. The sign convention above it is
        ParamFormat::Decibels' exactly. */
    inline std::string shelfText (float db)
    {
        if (db <= -23.95f)
            return "Cut";

        char buf[32];
        std::snprintf (buf, sizeof (buf), "%s%.1f dB", db > 0.0f ? "+" : "", (double) db);
        return buf;
    }

    /** "-6.0 dB", or "Off" at -40. **Off, not -40 dB**: the bus is silent, at
        or below -100 dB relative to itself, and 11 section 6 tests it as a
        silence rather than as a level. A fader that printed "-40.0 dB" at the
        end of its travel would be promising an audible tail that is not
        there. */
    inline std::string levelText (float db)
    {
        if (db <= -39.95f)
            return "Off";

        char buf[32];
        std::snprintf (buf, sizeof (buf), "%s%.1f dB", db > 0.0f ? "+" : "", (double) db);
        return buf;
    }

    /** "0.28 ms", two decimals. ParamFormat::Milliseconds prints one below ten
        and would show the whole 0.1-0.8 ms travel as eight steps, rounding the
        default to "0.3 ms" -- and 0.28 is not a round number, it is the peak
        deviation the 3-cent pitch bound allows at 1 Hz (10 section 4). */
    inline std::string modDepthText (float ms)
    {
        char buf[32];
        std::snprintf (buf, sizeof (buf), "%.2f ms", (double) ms);
        return buf;
    }
}

//==============================================================================
inline const ParamSpecs& specs()
{
    using S = ParamSpec;
    using F = ParamFormat;

    static const ParamSpecs s
    {
        //== The room ==========================================================

        // 0. TYPE. Six constant blocks over one topology -- no audio-path
        // branch beyond a table lookup, and buffers sized in prepare() for the
        // largest type, so switching never allocates. A switch may be a large
        // jump in sound but must not click: 30 ms raised-cosine dip on the wet
        // bus, tables swapped at the minimum.
        //
        // **And it writes nine other parameters, and five things that are not
        // parameters at all.** A type is a voicing, so selecting one re-applies
        // `kTypeConstants`' row for it over whatever those nine currently hold,
        // every time and not only at instantiation. The mechanism, the
        // re-entrancy argument and the automation conflict it creates are all
        // in `modules/reverb/TypeVoicing.h`. The other five -- ER SHAPE, DECAY
        // SHAPE, ATTACK and the two damping knees -- have no host lane since
        // the 2026-09-21 trim and are read off the same row by the engine.
        S::choiceParam (kType, "Type",
                        { kTypeNames[room], kTypeNames[chamber], kTypeNames[hall],
                          kTypeNames[cavern], kTypeNames[plate], kTypeNames[ambience] },
                        room),

        // 1. SIZE. Logarithmic, because equal turns should be equal ratios on
        // a dimension. The 0.1 m step is BMO DEQ's reason rather than a
        // precision anyone turns a knob to: a host carries the value as a
        // 32-bit normalised float and a continuous log law will not round-trip
        // exactly through one, so the step is what makes a saved session come
        // back the size it was saved at.
        S::logParam (kSize, "Size", 0.5f, 80.0f, 0.1f, roomDefaults::kSizeM, F::Metres),

        // 2. PRE-DELAY. Linear: this is a time interval read against the dry
        // signal, and the ITDG argument it serves is in milliseconds (05
        // section 2), not in ratios. **The range cannot go negative.**
        S::floatParam (kPreDelay, "Pre-Delay", 0.0f, 250.0f, 0.1f, 0.0f, F::Milliseconds),

        // LINK ER was index 3 and is now `kPreLinkFixed`; DECAY SHAPE was 5
        // and ATTACK was 6, and both are per-type constants. See the trim
        // block at the top of this file.

        //== The tail ==========================================================

        // 3. DECAY, T_mid. Log, and 0.01 s of step so the low end has
        // resolution the top does not need.
        S::logParam (kDecay, "Decay", 0.1f, 20.0f, 0.01f, 1.8f, F::Seconds),

        // 4. SOURCE. A balance, not a density knob -- see kFeed.
        S::textParam (kFeed, "Source", 0.0f, 100.0f, 0.1f, roomDefaults::kFeed,
                      &detail::feedText),

        //== Damping: two multipliers, over knees the type sets ================
        // Log on both, because 0.10 to 2.00 is a 20:1 ratio and the
        // interesting half of it is under 1. The two knees were parameters 8
        // and 10 and are `TypeConstants::dampLoFreqHz` and `dampHiFreqHz`: the
        // owner's call is that a frequency belongs to the onboard EQ and a
        // knee belongs to the room.

        S::textParam (kDampLo, "Low x", 0.10f, 2.00f, 0.01f, 1.20f, &detail::multiplierText),
        S::textParam (kDampHi, "High x", 0.10f, 2.00f, 0.01f, 0.40f, &detail::multiplierText),

        //== The Reverb EQ, pre both generators ================================
        //
        // Three nodes, fixed shapes, and **neutral at every default**: both
        // shelf gains and the bell's gain open at 0 dB and FILTER opens off,
        // so a fresh instance's EQ is the identity. `tests/dsp` asserts that
        // as an absolute rather than as "close to what it was", which is
        // OptoDspTests' house rule.
        //
        // The 0.1 Hz and 0.01 Q steps are BMO DEQ's argument, not a precision
        // anyone turns a knob to: a host carries a value as a 32-bit
        // normalised float and a continuous log law does not round-trip
        // exactly through one, so the step is what makes a saved session come
        // back where it was saved.

        // 7. FILTER. The outer two nodes become a low cut and a high cut, and
        // their GAIN knobs grey out because a cut has no gain. See kEqFilter.
        S::boolParam (kEqFilter, "EQ Filter", false),

        // 8-10. Node 1: low shelf, or a low cut with FILTER on. The Q ceiling
        // is kShelfMaxQ and the knob stops there rather than travelling to a
        // bell's 40 and doing nothing over 2 -- see kShelfMaxQ. 0.71 is
        // maximally flat, which is what makes the default neutral in shape as
        // well as in gain.
        S::logParam (kEqLoFreq, "EQ Low Freq", 16.0f, 1600.0f, 0.1f, 200.0f, F::Hertz),
        S::textParam (kEqLo, "EQ Low", -24.0f, 12.0f, 0.1f, 0.0f, &detail::shelfText),
        S::logParam (kEqLoQ, "EQ Low Q", 0.1f, kShelfMaxQ, 0.01f, 0.71f),

        // 11-13. Node 2: a bell, in both modes, and the only node FILTER does
        // not touch. **The full 20 Hz - 20 kHz**, because a parametric bell
        // should sweep the whole band -- that is what makes it the node you
        // reach for when the other two cannot help.
        //
        // It was originally widened for a worse reason: node 1 stopped at
        // 1.6 kHz and node 3 at 2.1 kHz, so the bell was the only way for the
        // Reverb EQ to reach the presence region at all. Node 3 now runs to
        // 20 kHz, so the bell is wide because a bell should be, not because it
        // was covering for a shelf that could not reach air. A bell's Q is
        // BMO DEQ's own 0.1-40; 1 kHz and 0.71 are the conventional opening,
        // and the gain is 0, so it is doing nothing until it is asked to.
        S::logParam (kEqMidFreq, "EQ Mid Freq", 20.0f, 20000.0f, 0.1f, 1000.0f, F::Hertz),
        // Decibels rather than `shelfText`: -24 on a shelf is a removal and
        // the word "Cut" is worth more than the number there, and -24 on a
        // bell is a deep notch at one frequency, which is an EQ move like any
        // other. The three gain knobs share a travel and not a value string.
        S::floatParam (kEqMid, "EQ Mid", -24.0f, 12.0f, 0.1f, 0.0f, F::Decibels),
        S::logParam (kEqMidQ, "EQ Mid Q", 0.1f, 40.0f, 0.01f, 0.71f),

        // 14-16. Node 3: high shelf, or a high cut with FILTER on.
        //
        // **1 kHz - 20 kHz, opening at 6 kHz.** It was 1000-2100 Hz, which is
        // 1.07 octaves: a high shelf that could not reach air, on a module
        // whose commonest EQ move is darkening or brightening a tail. The
        // range was inherited rather than chosen -- `eqhifreq` predates the
        // parametric, and making that change "purely additive" to preserve the
        // id preserved its range with it. The bell above was then widened to
        // the full band to compensate, which treated the symptom: read its
        // comment with this one.
        //
        // Widening a range and moving a default is free until first ship and
        // changes no id. The three nodes now open at 200 Hz, 1 kHz and 6 kHz,
        // spread across the band, instead of the bell and the shelf sitting
        // 0.68 octaves apart with their markers touching on the screen.
        S::logParam (kEqHiFreq, "EQ High Freq", 1000.0f, 20000.0f, 0.1f, 6000.0f, F::Hertz),
        S::textParam (kEqHi, "EQ High", -24.0f, 12.0f, 0.1f, 0.0f, &detail::shelfText),
        S::logParam (kEqHiQ, "EQ High Q", 0.1f, kShelfMaxQ, 0.01f, 0.71f),

        //== Early reflections =================================================

        // 17. ER MODE. Taps is the default and the one the module is argued
        // from; Blend is defined-but-unheard (kErModeNames). **A choice, so
        // the trim did not touch it**: three is what its normalisation
        // depends on.
        S::choiceParam (kErMode, "ER Mode",
                        { kErModeNames[taps], kErModeNames[energy], kErModeNames[blend] },
                        taps),

        // 18. DENSITY. Linear per cent: the activation thresholds it sweeps
        // are spread over (0,1], so the knob and the weighting share a scale.
        S::textParam (kErDensity, "Density", 0.0f, 100.0f, 0.1f,
                      roomDefaults::kErDensity, &detail::densityText),

        // ER SHAPE, the rise exponent p, was index 18 and is now
        // `TypeConstants::erShape`. It was already per-type; the trim took
        // away the knob, not the number.

        // 19. ER SPREAD, the envelope's sigma. Log, as a time.
        S::logParam (kErSpread, "ER Spread", 5.0f, 200.0f, 0.1f,
                     roomDefaults::kErSpreadMs, F::Milliseconds),

        // 20. ER HI-CUT. Log, 1-20 kHz, defaulting to 7 kHz.
        S::logParam (kErHiCut, "ER Hi-Cut", 1000.0f, 20000.0f, 0.1f, 7000.0f, F::Hertz),

        // 21. VARIATION. **Stepped, not a choice list.** Seven positions that
        // are an ordered amount of decorrelation rather than seven named
        // behaviours, so they belong on a float with a step of one: a stepped
        // float normalises as (v - min) / (max - min), which is stable if a
        // later position is ever added at the end, where a choice list's
        // index/(n-1) is not. Var 6 is a different construction all the same,
        // and the value string says so.
        S::textParam (kErVariation, "Variation", 0.0f, 6.0f, 1.0f, 2.0f,
                      &detail::variationText),

        //== Modulation, width, input bandwidth ================================

        // 22. MOD DEPTH. Linear over a sub-millisecond travel.
        S::textParam (kModDepth, "Mod Depth", 0.1f, 0.8f, 0.01f,
                      roomDefaults::kModDepthMs, &detail::modDepthText),

        // 23. MOD RATE. Log: 0.1 to 1.2 Hz is a bit over a decade and the slow
        // end is where the difference between randomised and chorused lives.
        S::logParam (kModRate, "Mod Rate", 0.1f, 1.2f, 0.01f,
                     roomDefaults::kModRateHz, F::Hertz),

        // 24. WIDTH. M/S gain on the tail only. 100 % is unity, 0 is mono and
        // 200 is the widest the M/S law allows before it stops being one.
        S::floatParam (kWidth, "Width", 0.0f, 200.0f, 1.0f, 100.0f, F::Percent),

        // 25. IN HI-CUT. Defaults wide open, so a fresh instance is not
        // quietly darker than the signal it was given. See kInHiCut for why
        // this parameter is marked "owner confirm".
        S::logParam (kInHiCut, "In Hi-Cut", 2000.0f, 20000.0f, 0.1f,
                     roomDefaults::kInHiCutHz, F::Hertz),

        //== Output ============================================================

        // 26/27. The two absolute trims, both off at the bottom -- and both
        // per-type since 2026-09-21, which is what makes Ambience buildable.
        // The defaults are Room's row, like every other per-type default here.
        S::textParam (kErLevel, "ER", -40.0f, 0.0f, 0.1f,
                      roomDefaults::kErLevelDb, &detail::levelText),
        S::textParam (kVerbLevel, "Reverb", -40.0f, 0.0f, 0.1f,
                      roomDefaults::kVerbLevelDb, &detail::levelText),

        // 28. MIX. Defaults to 100 %, because the two faders above are the
        // wet balance and this is the dry/wet one -- a reverb used as a send,
        // which is the normal case, wants the dry out of the way. **The MIX
        // law itself is 11 section 7's open owner-confirm item**; what is
        // frozen here is the range, the step and the default.
        S::floatParam (kMix, "Mix", 0.0f, 100.0f, 0.1f, 100.0f, F::Percent),

        // 29. OUTPUT. Trim only, cut only.
        S::floatParam (kOutput, "Output", -24.0f, 0.0f, 0.1f, 0.0f, F::Decibels),
    };

    return s;
}

} // namespace bmo::reverb
