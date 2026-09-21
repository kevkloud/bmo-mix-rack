#pragma once

#include "modules/reverb/params.h"
#include <vector>

namespace bmo::reverb
{

/** Six starting points and Init, **named for the source or the job rather
    than for a record, an engineer or a piece of hardware** -- the rule the
    rest of the suite follows, and the reason none of these carries a person's
    name or a room's.

    They are the listening checklist in
    `docs/reverb/11-integration-and-test-plan.md` section 6 turned into
    settings: a lead vocal in a dense mix, the rap-vocal case that is the
    hardest (ER only, tail off, depth without wash), a close dry snare, a drum
    room, an acoustic guitar, and one long tail for the thing a reverb is
    obviously for.

    **Every figure below is a starting point and not a measurement, and one of
    them is not yet even that.** The per-type constant blocks do not exist --
    10 section 1 names the categories and gives no numbers -- so a preset that
    selects Plate is selecting a name today and a sound later. The *relative*
    settings are the part that survives: Ambience with the tail off asks for
    less than a hall, a snare asks for a shorter pre-delay than a vocal,
    overheads ask to be left alone. Revisit them with the listening pass,
    milestone M6.

    **There is no preset level check here, and there will be one later.** Every
    other module's suite checks that a preset comes out at the level it went
    in, against its OUTPUT. This module has an OUTPUT, so the check belongs --
    but the DSP is a pass-through, so every preset comes out at exactly the
    input level and the check would pass for the wrong reason. It is written
    when there is a reverb to match. That is a deferral, and it is stated as
    one in `tests/plugin/ReverbTests.cpp` where the check will go. */
inline const std::vector<FactoryPreset>& factory()
{
    static const std::vector<FactoryPreset> presets {
        { "Init", {} },

        // A lead vocal in a dense mix: a chamber rather than a hall, the tail
        // held back behind the ER, and a pre-delay long enough to keep the
        // consonants clear of it. ER HI-CUT down from the default, because
        // what clouds a vocal is the top of the early cluster.
        { "Vocal Chamber", { { kType, (float) chamber },
                             { kSize, 18.0f },
                             { kPreDelay, 40.0f },
                             { kDecay, 1.6f },
                             { kAttack, 45.0f },
                             { kErHiCut, 5500.0f },
                             { kErLevel, -12.0f },
                             { kVerbLevel, -9.0f },
                             { kMix, 100.0f } } },

        // **The hardest case, and the one the module is partly for.** A rap
        // vocal wants depth and not reverb: the tail is off outright, and the
        // early cluster at 15-25 ms is the only distance cue. Ambience is the
        // ER-star type. If this does not read as the source moving back
        // without wash, the ER generator is wrong -- 11 section 6 makes that
        // milestone M2's exit condition.
        { "Vocal Depth, No Tail", { { kType, (float) ambience },
                                    { kSize, 8.0f },
                                    { kPreDelay, 0.0f },
                                    { kDecay, 0.6f },
                                    { kErDensity, 25.0f },
                                    { kErSpread, 25.0f },
                                    { kErHiCut, 6500.0f },
                                    { kErVariation, 2.0f },
                                    { kErLevel, -10.0f },
                                    { kVerbLevel, -40.0f },   // Off, and it means it
                                    { kMix, 100.0f } } },

        // A dry close snare. Short, bright and dense, with the pre-delay short
        // enough that the reverb is part of the hit rather than after it --
        // and this is the preset the flamming rules are heard on.
        { "Snare Room", { { kType, (float) room },
                          { kSize, 9.0f },
                          { kPreDelay, 12.0f },
                          { kDecay, 1.1f },
                          { kDecayShape, 1.4f },
                          { kErDensity, 70.0f },
                          { kErHiCut, 9000.0f },
                          { kErLevel, -6.0f },
                          { kVerbLevel, -10.0f } } },

        // A drum room, the ambient-mic sound: the ER are most of it, the tail
        // is short and dark, and SOURCE is high so the tail inherits the
        // room's own timing rather than arriving as a separate space.
        { "Drum Room", { { kType, (float) room },
                         { kSize, 14.0f },
                         { kPreDelay, 5.0f },
                         { kDecay, 0.9f },
                         { kFeed, 90.0f },
                         { kErDensity, 60.0f },
                         { kDampHi, 0.30f },
                         { kErLevel, -4.0f },
                         { kVerbLevel, -12.0f },
                         { kWidth, 130.0f } } },

        // Acoustic guitar: a plate, because a plate has no room pattern to
        // fight the instrument's own, with the modulation held down. **This is
        // the preset the 3-cent bound is judged on** -- 10 section 4 flags a
        // held note as the case that may still read as wobble -- so the depth
        // sits under the default rather than at it.
        { "Guitar Plate", { { kType, (float) plate },
                            { kSize, 22.0f },
                            { kPreDelay, 25.0f },
                            { kDecay, 2.2f },
                            { kModDepth, 0.18f },
                            { kModRate, 0.35f },
                            { kErLevel, -14.0f },
                            { kVerbLevel, -7.0f },
                            { kEqHi, -3.0f } } },

        // The long one. A large hall with the low end ringing longer than the
        // top, which is what every real large room does and what the absorbent
        // filters exist to make accurate rather than approximate.
        { "Long Hall", { { kType, (float) largeHall },
                         { kSize, 45.0f },
                         { kPreDelay, 60.0f },
                         { kDecay, 5.5f },
                         { kAttack, 60.0f },
                         { kDampLo, 1.40f },
                         { kDampHi, 0.35f },
                         { kErSpread, 140.0f },
                         { kErLevel, -16.0f },
                         { kVerbLevel, -5.0f },
                         { kWidth, 120.0f } } },
    };

    return presets;
}

} // namespace bmo::reverb
