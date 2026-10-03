#pragma once

#include "modules/sat/params.h"
#include <vector>

namespace bmo::sat
{

/** The presets that ship with the plugin.

    These are starting points, not verdicts. They have been checked against
    what the plugin measures -- `measure sweep` reports every one of these
    drive settings -- and they have not been checked by ear on real material,
    which is the only test that finally matters.

    Most of them push Input and pull Output back, because the curve is
    level-dependent: how hard the signal arrives is half of how much colour it
    gets, exactly as it is on hardware. The two are not equal and opposite,
    since the curve has a broadband loss of its own; the Output figures below
    are the ones that measured level on the reference voice, which is why they
    are odd numbers rather than round ones.

    Anything a preset does not mention goes back to its default, so a preset
    cannot leave a stray setting behind from whatever was loaded before it.

    **Every preset but Init names its TONE.** TONE's default went from 100 to
    55 on 2026-10-03, the owner's decision before the 0.2.6 schema freeze, and
    the seven that had left it to the default were given the 100 they had
    always had, so none of them changed sound. Init follows the default.
    SatTests holds this.

    **Re-solved in 0.5.0 from a measured run, not re-estimated.** Every one of
    these came back between 0.76 and 1.39 dB quiet, and every one of them had
    been passing, because the level-matching test's tolerance is +/-3 dB and a
    pass prints no number. Turning on BMO_PRINT_PRESET_LEVELS is what made a
    uniform droop across the whole product visible at all; run 34085918240
    gave all ten, and each Output figure below moves by its own delta.

    It is worth saying what this droop is *not*. 0.5.0 also narrowed the
    voicing bell (Q 0.90 -> 1.40, +11 -> +13.5 dB), which is the obvious
    suspect for a broadband level change. It is not the cause: weighting each
    bell's power response by the test signal's own spectrum puts the
    difference at **-0.009 dB**. The signal is a 75 Hz harmonic stack and
    almost all of its energy sits far below 7 kHz, so a bell up there barely
    touches its broadband level. The droop predates the bell change.

    **The two AUTO presets are a weaker case than the other eight.** Reference
    and Mix Bus Colour ship with Auto Gain on, so their level is set by a
    dynamic matcher, and their Output figures compensate that matcher's
    residual with a static number fitted on one signal. On material where
    AUTO settles differently the compensation will be differently wrong. They
    are the first two to re-measure if either ever reads off.
*/
inline const std::vector<FactoryPreset>& factory()
{
    static const std::vector<FactoryPreset> presets {
        { "Init", {} },

        // The calibration point: the drive at which the curve measures the
        // reference's own asymmetry, 0.62 against 0.84. Everything else in
        // this list is a move away from here.
        { "Reference", {
            { kDrive, 40.0f }, { kTone, 100.0f }, { kAutoGain, 1.0f },
            { kOutputLevel, 1.27f } } },   // AUTO's residual -- see the note above

        { "Vocal Sheen", {
            { kInputGain, 2.0f }, { kDrive, 34.0f }, { kTone, 100.0f },
            { kOutputLevel, -0.04f } } },

        // Pulled back in 0.4.0: at Drive 52 with 5 dB of input on top of it,
        // this sat past the point where the curve turns from colour into
        // overdrive, and it distorted on every source it was tried on. The
        // gain was doing most of the damage.
        { "Vocal Front", {
            { kInputGain, 1.5f }, { kDrive, 46.0f }, { kTone, 100.0f },
            { kOutputLevel, 0.82f } } },

        { "Whisper", {                             // barely there, for a take that only needs air
            { kDrive, 18.0f }, { kMix, 60.0f }, { kTone, 55.0f },
            { kOutputLevel, 1.73f } } },

        { "Drum Bus Glue", {
            { kInputGain, 3.0f }, { kDrive, 46.0f }, { kTone, 40.0f },
            { kMix, 70.0f },                       // parallel, so the transients stay whole
            { kOutputLevel, 0.27f } } },

        { "Snare Edge", {
            { kInputGain, 6.0f }, { kDrive, 66.0f }, { kTone, 100.0f },
            { kOutputLevel, -2.24f } } },

        { "Bass Warmth", {                         // no air on a bass; the curve only
            { kInputGain, 4.0f }, { kDrive, 30.0f }, { kTone, 0.0f },
            { kOutputLevel, -1.87f } } },

        { "Guitar Grit", {
            { kInputGain, 8.0f }, { kDrive, 78.0f }, { kTone, 100.0f },
            { kOutputLevel, -4.12f } } },

        { "Mix Bus Colour", {
            { kDrive, 24.0f }, { kMix, 45.0f }, { kTone, 100.0f },   // gentle, in parallel, level-matched
            { kAutoGain, 1.0f },
            { kOutputLevel, 0.76f } } },   // AUTO's residual -- see the note above

        // The top of the range, where the curve stops adding harmonics and
        // starts rearranging the waveform. Not subtle and not meant to be.
        { "Ruined", {
            { kInputGain, 10.0f }, { kDrive, 100.0f }, { kTone, 100.0f },
            { kOversampling, 2.0f },               // 4x: it needs the headroom up there
            { kOutputLevel, -6.40f } } },
    };

    return presets;
}

} // namespace bmo::sat
