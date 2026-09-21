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
// copy; 10-dsp-spec.md 6 restates the same thirty names in the same order and
// the two were reconciled before this file was written.
//
// **Thirty parameters, and the count is the decision.** A rack slot shows a
// host 32 lanes, so this fits with **two spare** and none of BMO DEQ's
// SlotOverflow machinery is needed -- but two is all there is. A later Freeze,
// a ducking control, or the tempo-sync pair `syncon`/`syncdiv` would exhaust
// them between them. See AGENTS.md, which says so in the place a reader
// proposing a thirty-first control will look.
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

// Whether the early reflections travel with the pre-delayed tail or stay with
// the dry signal. Off is the reference behaviour: ER with dry.
inline constexpr auto kPreLink = "prelink";

// T_mid, the mid-band 60 dB decay time, in seconds. The damping multipliers
// below scale it per band; they do not scale this.
inline constexpr auto kDecay = "decay";

// Decay truncation, as an envelope multiplier on the FDN output retriggered by
// an input envelope follower. 3.5 is linear -- i.e. off, the tail decays as
// the network does -- and the bottom of the range is a gate. One control
// instead of separate gated and reverse algorithms (10 section 4).
inline constexpr auto kDecayShape = "decayshape";

// The tail's onset contour, a rising envelope on the FDN *input* ramping over
// 0-120 ms, so the tail blooms behind the ER rather than arriving with it. At
// 0 the tail is immediate, which is plate behaviour.
inline constexpr auto kAttack = "attack";

// What feeds the tail: (1-d) * direct + d * ER, tapped from the ER bus
// *before* decorrelation and *before* the ER fader, so the two faders stay
// independent. **The caption is SOURCE**, decided by the owner 2026-09-21;
// 10 section 2 calls it Diffusion after the reference, and 11 section 4
// proposed TAIL FEED, but "diffusion" means density everywhere else in this
// suite and a third word beat both. The id does not change with the caption.
inline constexpr auto kFeed = "feed";

// The two absorbent-filter knees and their decay multipliers: T60 below the
// low knee is T_mid * damplo, above the high knee T_mid * damphi. Derived from
// a target T60(f) rather than tuned toward one, which is the whole reason the
// late network is an FDN (10 section 1).
inline constexpr auto kDampLoFreq = "damplofreq";
inline constexpr auto kDampLo     = "damplo";
inline constexpr auto kDampHiFreq = "damphifreq";
inline constexpr auto kDampHi     = "damphi";

// The Reverb EQ: a low shelf and a high shelf, **pre both generators** rather
// than on the wet output, which is where the reference puts it (10 section 2).
// At the bottom of its travel each shelf reads "Cut" rather than "-24.0 dB",
// because -24 is where it stops being an EQ move and starts being a removal.
inline constexpr auto kEqLoFreq = "eqlofreq";
inline constexpr auto kEqLo     = "eqlo";
inline constexpr auto kEqHiFreq = "eqhifreq";
inline constexpr auto kEqHi     = "eqhi";

// How the early cluster is generated. Taps / Energy / Blend, index order
// frozen -- and see kErModeNames on what Blend is and is not.
inline constexpr auto kErMode = "ermode";

// The density bridge: discrete positional taps at the bottom, dense shaped
// early energy at the top, with no allpass at either end (10 section 3). It is
// a continuous weighting, not a switch, so no tap ever appears at a non-zero
// level and energy is renormalised across the sweep.
inline constexpr auto kErDensity = "erdensity";

// Energy mode's envelope: the rise exponent p in (t/tau_r)^p, and the sigma
// that sets how long the plateau lasts. Both are live in Taps mode too, since
// Blend reuses the Shape/Spread envelope -- whether they grey out in Taps mode
// or sit inert is 11 section 7's open owner-confirm question and is a panel
// decision, not a schema one.
inline constexpr auto kErShape  = "ershape";
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

/** Positions in `specs()`, and in a rack slot's host lanes. Permanent. */
enum Index
{
    type = 0, size, predelay, prelink, decay, decayshape, attack, feed,
    damplofreq, damplo, damphifreq, damphi,
    eqlofreq, eqlo, eqhifreq, eqhi,
    ermode, erdensity, ershape, erspread, erhicut, ervariation,
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
// the trade to make knowingly when types 7-11 arrive (10 section 1 names them:
// Church, Shaped Hall, Pattern Room, Positional Room, Vintage Room).
//==============================================================================

enum TypeChoice { room = 0, chamber, hall, largeHall, plate, ambience, numTypes };

/** Small to large, then plate, then ambience -- the reference core set, and
    the order is final. Ambience is the ER-star type: tiny tail, ER-dominant,
    and where "tail off, distance sets depth" lands by default. */
inline const char* const kTypeNames[] { "Room", "Chamber", "Hall", "Large Hall", "Plate", "Ambience" };

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
    for `size`, `erdensity`, `ershape`, `erspread`, `moddepth`, `modrate`,
    `inhicut` and `feed` is a claim about what Room *is*. If the DSP later
    picks different Room constants, the panel lies about itself on the very
    first thing a user sees.

    The per-type tables do not exist yet: 10 section 1 names the categories a
    type's constant block holds -- ER tap table, ER window and default density,
    the eight FDN times, input-diffusion depth, damping and modulation
    defaults, input bandwidth, default ER feed, and three reserved era fields
    -- and gives no numbers for any of them. When they are written, **Room's
    row is pinned to these eight values** and the other five types are free.

    The defaults that are *not* in this list are ordinary defaults and are
    type-independent: `type` itself, `predelay`, `prelink`, `decay`,
    `decayshape`, `attack`, the four damping and four EQ rows, `ermode`,
    `erhicut`, `ervariation`, `width`, the two levels, `mix` and `output`. A
    type may move the *sound* those produce; it does not move the number the
    knob opens at.

    They are constants here rather than numbers inline in `specs()` so that the
    per-type table, when it lands, can be checked against the same symbols the
    schema was built from instead of against a second transcription. */
namespace roomDefaults
{
    inline constexpr float kSizeM      = 12.0f;
    inline constexpr float kErDensity  = 50.0f;    ///< per cent
    inline constexpr float kErShape    = 1.0f;     ///< the contour exponent p
    inline constexpr float kErSpreadMs = 80.0f;
    inline constexpr float kModDepthMs = 0.28f;    ///< the 3-cent bound at 1 Hz
    inline constexpr float kModRateHz  = 0.50f;
    inline constexpr float kInHiCutHz  = 20000.0f;
    inline constexpr float kFeed       = 70.0f;    ///< per cent
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

    /** "3.50 (Linear)". 3.5 is linear -- the tail decays as the network does,
        which is the truncation switched off -- and the bottom of the travel is
        a gate. The word is what the number means; the number is what a second
        instance has to be set to to match. */
    inline std::string decayShapeText (float shape)
    {
        static const char* const words[] { "Gated", "Steep", "Natural", "Long", "Linear" };

        char buf[64];
        std::snprintf (buf, sizeof (buf), "%.2f (%s)", (double) shape,
                       shape >= 3.45f ? "Linear" : ladder (shape, 0.04f, 3.45f, words, 4));
        return buf;
    }

    /** "30 % (36 ms)". The percentage is the parameter; the milliseconds are
        the bloom it selects, over the 0-120 ms ramp 10 section 2 gives.

        **The mapping is linear because the contour is CALIBRATE**, not because
        anyone has measured it to be. 10 section 8 lists the Attack contour's
        shape among the things that can only be settled by ear, so a curve here
        would be a guess dressed as a figure. When the contour lands, this is
        the one place the printed number comes from. */
    inline std::string attackText (float percent)
    {
        char buf[64];
        std::snprintf (buf, sizeof (buf), "%d %% (%d ms)",
                       (int) std::lround (percent),
                       (int) std::lround (percent * 1.2f));
        return buf;
    }

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

    /** "p 1.00" -- the rise exponent itself, named, because it has no unit and
        a bare "1.00" beside ER SPREAD's milliseconds would read as a time. */
    inline std::string shapeText (float p)
    {
        char buf[32];
        std::snprintf (buf, sizeof (buf), "p %.2f", (double) p);
        return buf;
    }

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
        S::choiceParam (kType, "Type",
                        { kTypeNames[room], kTypeNames[chamber], kTypeNames[hall],
                          kTypeNames[largeHall], kTypeNames[plate], kTypeNames[ambience] },
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

        // 3. LINK ER. Off is the reference behaviour: the ER travel with dry
        // and only the tail is delayed.
        S::boolParam (kPreLink, "Link ER", false),

        //== The tail ==========================================================

        // 4. DECAY, T_mid. Log, and 0.01 s of step so the low end has
        // resolution the top does not need.
        S::logParam (kDecay, "Decay", 0.1f, 20.0f, 0.01f, 1.8f, F::Seconds),

        // 5. DECAY SHAPE. Log, over nearly two decades, so the gated end gets
        // the travel it needs. Defaults to 3.5 -- linear, i.e. truncation off
        // -- because a reverb should not arrive gated.
        S::textParam (kDecayShape, "Decay Shape", 0.04f, 3.5f, 0.001f, 3.5f,
                      &detail::decayShapeText),

        // 6. ATTACK. Per cent of the 0-120 ms bloom, not the milliseconds
        // themselves, because the contour between them is CALIBRATE and a
        // millisecond parameter would freeze a shape nobody has heard.
        S::textParam (kAttack, "Attack", 0.0f, 100.0f, 0.1f, 30.0f, &detail::attackText),

        // 7. SOURCE. A balance, not a density knob -- see kFeed.
        S::textParam (kFeed, "Source", 0.0f, 100.0f, 0.1f, roomDefaults::kFeed,
                      &detail::feedText),

        //== Damping: two knees, two multipliers ===============================
        // Log on all four. The knees for the usual frequency reason; the
        // multipliers because 0.10 to 2.00 is a 20:1 ratio and the interesting
        // half of it is under 1.

        S::logParam (kDampLoFreq, "Low x Freq", 16.0f, 1600.0f, 0.1f, 200.0f, F::Hertz),
        S::textParam (kDampLo, "Low x", 0.10f, 2.00f, 0.01f, 1.20f, &detail::multiplierText),
        S::logParam (kDampHiFreq, "High x Freq", 1000.0f, 2100.0f, 0.1f, 1600.0f, F::Hertz),
        S::textParam (kDampHi, "High x", 0.10f, 2.00f, 0.01f, 0.40f, &detail::multiplierText),

        //== The Reverb EQ, pre both generators ================================

        S::logParam (kEqLoFreq, "EQ Low Freq", 16.0f, 1600.0f, 0.1f, 200.0f, F::Hertz),
        S::textParam (kEqLo, "EQ Low", -24.0f, 12.0f, 0.1f, 0.0f, &detail::shelfText),
        S::logParam (kEqHiFreq, "EQ High Freq", 1000.0f, 2100.0f, 0.1f, 1600.0f, F::Hertz),
        S::textParam (kEqHi, "EQ High", -24.0f, 12.0f, 0.1f, 0.0f, &detail::shelfText),

        //== Early reflections =================================================

        // 16. ER MODE. Taps is the default and the one the module is argued
        // from; Blend is defined-but-unheard (kErModeNames).
        S::choiceParam (kErMode, "ER Mode",
                        { kErModeNames[taps], kErModeNames[energy], kErModeNames[blend] },
                        taps),

        // 17. DENSITY. Linear per cent: the activation thresholds it sweeps
        // are spread over (0,1], so the knob and the weighting share a scale.
        S::textParam (kErDensity, "Density", 0.0f, 100.0f, 0.1f,
                      roomDefaults::kErDensity, &detail::densityText),

        // 18. ER SHAPE, the rise exponent p in (t/tau_r)^p. Linear: p is
        // already an exponent, and putting a log law on one is a second
        // exponent nobody asked for.
        S::textParam (kErShape, "ER Shape", 0.0f, 3.0f, 0.01f,
                      roomDefaults::kErShape, &detail::shapeText),

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

        // 26/27. The two absolute trims, both off at the bottom.
        S::textParam (kErLevel, "ER", -40.0f, 0.0f, 0.1f, -6.0f, &detail::levelText),
        S::textParam (kVerbLevel, "Reverb", -40.0f, 0.0f, 0.1f, -6.0f, &detail::levelText),

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
