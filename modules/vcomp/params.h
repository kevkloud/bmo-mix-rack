#pragma once

#include "core/state/ParamSpec.h"

namespace bmo::vcomp
{

//==============================================================================
// Parameter IDs. Permanent and append-only from the first release -- see
// modules/eq/params.h for why, and tests/plugin/VcompTests.cpp for the table
// that holds them.
//==============================================================================

inline constexpr auto kModuleId   = "ltvcomp";
inline constexpr auto kModuleName = "LTV Comp";

//==============================================================================
// The face, and the switch that reveals the rest.
//
// BMO Vcomp is a *vocal* compressor, not the suite's general-purpose one, and
// the face it presents is the RVox/DC1A face: one knob for how hard it works,
// one for how loud it comes out, and a gate, because a compressor with 26 dB
// of makeup on it lifts the room tone by 26 dB too.
//
// **GATE has no knob.** It is a handle dragged along the IN meter, which is
// the only control on the panel that is not a knob or a switch, and it is that
// way because a gate threshold is the one parameter you set by *looking at the
// level you are setting it against*. A knob would make you read a number and
// translate. See VcompPanel.h.
//
// COMPLEX is the escape hatch, and it is a parameter rather than a panel state
// because it changes the sound: with it off the DSP ignores kAttack, kRelease,
// kArc, kSidechain, kLowThru and kHighThru entirely and runs its own figures.
// Same shape as BMO Opto's Color-in-Tele lock, enforced in DspCore rather than
// in the panel so it holds whatever the panel does.
//==============================================================================

inline constexpr auto kAmount   = "amount";
inline constexpr auto kGate     = "gate";
inline constexpr auto kOutput   = "output";
inline constexpr auto kComplex  = "complex";
inline constexpr auto kAttack   = "attack";
inline constexpr auto kRelease  = "release";
inline constexpr auto kArc      = "arc";
inline constexpr auto kSidechain = "sidechain";
inline constexpr auto kLowThru  = "low_thru";
inline constexpr auto kHighThru = "high_thru";

enum Index { amount, gate, output, complex, attack, release, arc, sidechain,
             lowThru, highThru, count };

inline constexpr int kVersionHint   = 1;
inline constexpr int kStateVersion  = 1;
inline constexpr int kSchemaVersion = 1;

//==============================================================================
// The rails that mean "off", and the standard-mode figures.
//
// Three parameters are inert at one end of their travel rather than having a
// switch: GATE at the bottom, LOW THRU at the bottom, HIGH THRU at the top.
// The DSP tests the rail exactly, so "off" is off and not "nearly off" -- see
// DspCore, and testRailsAreExactlyOff.
//
// The IN meter's scale and kGateOffDb are the same number on purpose: the gate
// handle at the far left of the meter is the gate switched off, which is what
// makes the control readable without a caption saying so. Move one and move
// the other -- VcompTests pins that they agree.
//==============================================================================

inline constexpr float kGateOffDb      = -60.0f;   ///< also the IN/OUT meters' floor
inline constexpr float kLowThruOffHz   = 20.0f;
inline constexpr float kHighThruOffHz  = 20000.0f;

// What the DSP uses for the six complex controls while COMPLEX is off. They
// are deliberately the same numbers as those parameters' defaults: a user who
// switches COMPLEX on should find the knobs sitting where the module was
// already working, so turning it on with untouched knobs changes nothing.
// VcompDspTests pins that -- it is the difference between an escape hatch and
// a trapdoor. Change one of these and change the matching default.
inline constexpr float kStandardAttackMs    = 5.0f;
inline constexpr float kStandardReleaseMs   = 200.0f;
inline constexpr float kStandardSidechainHz = 90.0f;
inline constexpr bool  kStandardArc         = true;
inline constexpr float kStandardLowThruHz   = kLowThruOffHz;    ///< standard mode compresses the whole band
inline constexpr float kStandardHighThruHz  = kHighThruOffHz;

inline const ParamSpecs& specs()
{
    using S = ParamSpec;
    using F = ParamFormat;

    static const ParamSpecs s
    {
        // AMOUNT: the whole compressor on one knob. It sweeps the threshold
        // down, tightens the knee and raises the ratio together, and adds the
        // makeup that keeps the level where it was -- so turning it up makes
        // the vocal denser and more forward rather than louder, which is what
        // RVox's one knob does and what people actually want from it. See
        // curveFor() in Detector.h for the three sweeps and autoMakeupDb()
        // for the compensation.
        //
        // Defaults to 0, like BMO Opto's CRUSH: a freshly inserted instance
        // does nothing until the ear asks it to. At 0 the ratio sweep is at
        // 1:1, which is a slope of zero, so the curve reduces nothing at any
        // level -- the module is a wire, not merely quiet.
        S::floatParam (kAmount, "Amount", 0.0f, 100.0f, 0.1f, 0.0f, F::Percent),

        // GATE: the threshold of a downward expander ahead of the compressor,
        // in dBFS. It exists because of the auto makeup -- at AMOUNT 80 this
        // module adds about 26 dB, and it adds it to room tone, headphone
        // bleed and mic noise between lines just as willingly as to the voice.
        // RVox has exactly this, for exactly this reason, and exactly one
        // control for it: no ratio, no timing, no range.
        //
        // Defaults to kGateOffDb, where it is exactly inert. The range runs to
        // -10 rather than to 0 because a gate threshold up at the programme's
        // own level is not a setting, it is a mistake with a wide travel.
        S::floatParam (kGate, "Gate", kGateOffDb, -10.0f, 0.1f, kGateOffDb, F::Decibels),

        // MAKEUP: makeup on top of the automatic makeup, for the ear. The
        // automatic one is not switchable and does not appear here -- it is
        // part of what AMOUNT *is*, not a feature layered over it. Compare
        // BMO Opto, which rejected auto-makeup outright because neither unit
        // it models has one; this module models a unit that does.
        //
        // **The id stays `output` and the panel says MAKEUP** -- Frosty,
        // 2026-09-14, correcting a caption that had never agreed with the
        // paragraph above it. An id is frozen once it ships and this one has;
        // a caption is a UI string and free. That is the same split BMO Opto
        // already runs twice, `level`/"Level"/MAKEUP and `crush`/"Crush"/COMP.
        // The host-facing "Output" is left alone as well, so an automation
        // lane written against 0.2.4 still reads what it read then.
        //
        // **It is not a trim**, which is the substance of the correction
        // rather than a side effect of it: this is the module's own gain
        // stage, the same control as BMO Opto's MAKEUP down to the range, the
        // step and the default. It is why neither compressor takes the shared
        // output section -- an output trim belongs on that line and a makeup
        // stage does not.
        S::floatParam (kOutput, "Output", -24.0f, 24.0f, 0.01f, 0.0f, F::Decibels),

        // COMPLEX: reveals the six below and makes the DSP honour them.
        S::boolParam (kComplex, "Complex", false),

        // Attack and release are logarithmic: equal turns for equal ratios,
        // which is how time is heard.
        S::logParam (kAttack,  "Attack",  0.1f, 100.0f,  0.01f, kStandardAttackMs,  F::Milliseconds),
        S::logParam (kRelease, "Release", 20.0f, 1000.0f, 0.1f, kStandardReleaseMs, F::Milliseconds),

        // ARC: the programme-dependent release, on by default and always on
        // in standard mode. RELEASE still means something with it on -- it
        // scales all three of ARC's branches rather than being ignored -- see
        // ReleaseStage in Detector.h.
        S::boolParam (kArc, "Arc", kStandardArc),

        // SIDECHAIN: a high-pass on the detector only, never on the audio.
        // **This is what the compressor listens to, not what it acts on** --
        // the two are different controls and LOW THRU below is the other one.
        // Turning this up stops plosives and proximity effect from ducking the
        // phrase; it does not stop the low end being ducked when something
        // else triggers the compressor.
        //
        // There is no Off position: 20 Hz is the bottom of the range and is
        // flat over anything a voice puts out, which is the same thing without
        // spending a switch on it.
        S::logParam (kSidechain, "Sidechain", 20.0f, 500.0f, 1.0f, kStandardSidechainHz, F::Hertz),

        // LOW THRU / HIGH THRU: **what the compressor acts on.** Everything
        // below LOW THRU and above HIGH THRU is split off and passes through
        // uncompressed -- not merely unheard by the detector, genuinely not
        // reduced. The chest of a voice keeps its weight while the midrange is
        // levelled; air and sibilance keep their top while the body is held
        // down.
        //
        // Each is inert at one rail (kLowThruOffHz, kHighThruOffHz) and at
        // both rails the crossover is bypassed outright rather than run with
        // nothing in the outer bands, because a crossover left in circuit
        // still costs the allpass phase shift it always costs. See
        // DspCore::bandsActive().
        S::logParam (kLowThru,  "Low Thru",  kLowThruOffHz, 500.0f,   1.0f, kStandardLowThruHz,  F::Hertz),
        S::logParam (kHighThru, "High Thru", 2000.0f, kHighThruOffHz, 1.0f, kStandardHighThruHz, F::Hertz),
    };

    return s;
}

} // namespace bmo::vcomp
