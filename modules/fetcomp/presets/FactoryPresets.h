#pragma once

#include "modules/fetcomp/params.h"
#include <vector>

namespace bmo::fetcomp
{

/** Four starting points and Init, named for the job rather than for a record,
    an engineer or a piece of hardware -- the same rule the rest of the suite
    follows.

    **The makeup figures are not solved yet, and they are deliberately zero.**
    Every other module's presets carry an OUTPUT that matches the preset's
    loudness to Init's, and `tests/plugin/*Tests.cpp` checks it. That check
    cannot mean anything here while `modules/fetcomp/dsp/DspCore.h` is a
    placeholder: with no cell in circuit, INPUT is a plain gain and the only
    honest makeup for it is its own negative, which is not the figure the real
    compressor will want. Writing a number now would be writing a guess that
    looks like a measurement.

    So these presets set the controls that *say what the preset is* -- the
    drive, the ratio, the two positions, the voicing -- and leave OUTPUT at 0.
    The makeup pass belongs with the DSP (milestone M6), and
    `tests/plugin/FetcompTests.cpp` says the same thing where the level check
    would otherwise sit.

    The four are the listening checklist in
    docs/fet-comp/11-integration-and-test-plan.md 3, which is where the
    settings come from: a lead vocal driven hard at 4:1, a room mic with every
    button in and both knobs fast, a bass at 20:1 with the release long enough
    to grind, and a light bus setting that leans on MIX rather than on depth. */
inline const std::vector<FactoryPreset>& factory()
{
    static const std::vector<FactoryPreset> presets {
        { "Init", {} },

        // A vocal, driven to where the needle sits well into the scale: 4:1,
        // a medium attack so the consonants still arrive, and a fast release.
        { "Vocal Front", { { kInput, 18.0f },
                           { kRatio, (float) ratio4 },
                           { kAttack, 3.0f }, { kRelease, 6.0f },
                           { kVoicing, (float) black } } },

        // A room mic with every button in, both knobs at the fast end. This is
        // the setting the all-buttons curve exists for.
        { "Drum Room All In", { { kInput, 24.0f },
                                { kRatio, (float) ratioAll },
                                { kAttack, 7.0f }, { kRelease, 7.0f },
                                { kVoicing, (float) blue } } },

        // Bass at 20:1, deep, with the release slow enough that the low end
        // rides rather than pumps.
        { "Bass Hold", { { kInput, 20.0f },
                         { kRatio, (float) ratio20 },
                         { kAttack, 2.0f }, { kRelease, 2.0f },
                         { kVoicing, (float) black } } },

        // Parallel: the compressor works hard and MIX decides how much of it
        // is heard, which is what the dry path is delay-matched for.
        { "Parallel Glue", { { kInput, 26.0f },
                             { kRatio, (float) ratio8 },
                             { kAttack, 5.0f }, { kRelease, 5.0f },
                             { kMix, 35.0f },
                             { kVoicing, (float) black } } },
    };

    return presets;
}

} // namespace bmo::fetcomp
