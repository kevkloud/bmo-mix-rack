#pragma once

#include "modules/vcomp/params.h"
#include <vector>

namespace bmo::vcomp
{

/** **None of these set MAKEUP, and that is the point.**

    modules/AGENTS.md asks that every preset come out at the level it went in,
    and the plugin tests check it. Every other module in the suite pays for
    that with a hand-solved makeup figure per preset, re-solved by CI whenever
    the curve moves -- modules/opto/presets/FactoryPresets.h is four screens of
    exactly that, twice re-derived.

    This module gets it for nothing. AMOUNT carries its own static makeup
    (autoMakeupDb, Detector.h), so a preset that moves AMOUNT and nothing else
    is level-matched by construction, at every setting, and cannot drift when
    the curve is revoiced. The level-matching test is still worth having as a
    tripwire on the auto makeup itself; it just has no numbers here to keep up
    to date.

    So a preset that wants to be louder should say so with MAKEUP, deliberately,
    and none of these do.

    **Nor do any of them set GATE.** A gate threshold is an absolute level, and
    the right one depends entirely on how loud the track was recorded and how
    much bleed is on it -- a number chosen here would be wrong for almost every
    session it loaded into, and wrong in the expensive direction (a gate set
    too high eats words). It is left at its rail, off, and set by dragging the
    handle on the IN meter against the level that is actually there. That is
    what the handle is for.

    The complex presets set the detector controls *and* COMPLEX, because the
    DSP ignores those six with COMPLEX off (DspCore::applyTimings) -- a preset
    that set ATTACK without setting COMPLEX would store a number that does
    nothing and show a panel that does not have the knob on it. VcompTests
    checks that.

    None of these are **ear-tuned against real programme material**. They are
    round numbers at the shape of the curve, and Frosty should expect to move
    them after a listening pass -- tools/measure/vcomp renders every one of
    them for exactly that. */
inline const std::vector<FactoryPreset>& factory()
{
    static const std::vector<FactoryPreset> presets {
        { "Init", {} },

        // Standard mode: one number each.
        { "Lift",     { { kAmount, 25.0f } } },   // levelling, barely a sound of its own
        { "Forward",  { { kAmount, 55.0f } } },   // the vocal sits up; the working setting
        { "In Front", { { kAmount, 80.0f } } },   // dense and modern, consonants held down

        // Complex mode, each saying what it is for by which controls it moves
        // away from the standard-mode figures.
        { "Fast Vocal", { { kComplex, 1.0f }, { kAmount, 65.0f },
                          { kAttack, 0.8f }, { kRelease, 90.0f },
                          { kSidechain, 120.0f } } },   // catches consonants, lets go quickly

        { "Smooth Lead", { { kComplex, 1.0f }, { kAmount, 45.0f },
                           { kAttack, 20.0f }, { kRelease, 400.0f },
                           { kSidechain, 70.0f } } },   // lets the transient through, rides the body

        // The two that use the band split, which is the thing this module has
        // that a one-knob vocal compressor normally does not.
        // **AMOUNT 35 and not 70, because of what the makeup does here.** The
        // thru band takes the makeup along with everything else, so an
        // uncompressed low end rises by the whole makeup figure: at 70 that is
        // about 19 dB and the preset came out 7.3 dB loud, at 45 it was still
        // 3.8. The level-matching test caught both. This preset is where a
        // user meets LOW THRU for the first time, so it has to sit where the
        // feature works rather than where it is most obvious -- and how far
        // that is, is the open question in AGENTS.md.
        { "Keep The Chest", { { kComplex, 1.0f }, { kAmount, 35.0f },
                              { kLowThru, 160.0f } } },   // body levelled, weight left alone

        // Same figure as Keep The Chest and for the same reason: at 70 the thru
        // band takes 19 dB of makeup uncompressed, which on a real vocal is a
        // +19 dB sibilance boost driven into the limiter. The level-matching
        // test never saw it because the harness voice has almost no energy
        // above 6 kHz (0.2.4 review, 2026-09-14).
        { "Keep The Air", { { kComplex, 1.0f }, { kAmount, 35.0f },
                            { kHighThru, 6000.0f } } },   // top stays open over a held-down body

        // ARC off is the one preset that hands the release back to the number
        // on the knob, which is what you want under a performance that is
        // already even and does not need the detector second-guessing it.
        { "Manual", { { kComplex, 1.0f }, { kAmount, 50.0f }, { kArc, 0.0f },
                      { kAttack, 5.0f }, { kRelease, 150.0f } } },
    };

    return presets;
}

} // namespace bmo::vcomp
