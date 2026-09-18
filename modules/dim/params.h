#pragma once

#include "core/state/ParamSpec.h"

namespace bmo::dim
{

//==============================================================================
// Parameter IDs. Permanent and append-only -- see modules/eq/params.h for why,
// and tests/plugin/DimTests.cpp for the table that holds them.
//==============================================================================

inline constexpr auto kModuleId   = "dim";
inline constexpr auto kModuleName = "BMO Dimension";

// The imaging stage: what the module does to side content that already
// exists. Modelled on the Waves S1, including Gerzon's asymmetry control and
// his bass shuffler.
inline constexpr auto kWidth       = "width";
inline constexpr auto kShuffle     = "shuffle";
inline constexpr auto kShuffleFreq = "shuffle_freq";

// The generate stage: what manufactures side content when there is none.
// On a mono source S = 0, and an all-pass of zero is zero -- so without
// this stage the other two have nothing to work on. It is the only part of
// the module that is not mono-safe, which is why it is the part that
// switches out.
inline constexpr auto kDetune   = "detune";
inline constexpr auto kDetuneOn = "detune_on";

// The diffuse stage: a modulated all-pass on the side signal. A phaser is a
// modulated all-pass, so this is the phaser and the decorrelator at once --
// see modules/dim/dsp/DimDsp.h for why they are not two stages.
inline constexpr auto kDiffuse = "diffuse";
inline constexpr auto kRate    = "rate";
inline constexpr auto kDepth   = "depth";

// The rest of the S1's matrix. Rotation turns the whole soundfield; asymmetry
// skews left against right without moving centre material. Both are reached
// for far less often than width, which is why they sit at the end.
inline constexpr auto kRotation  = "rotation";
inline constexpr auto kAsymmetry = "asymmetry";

// Order is reach-for-first, not signal order, per modules/AGENTS.md: the
// controls a user opens this panel for go at the top. That is width, not
// detune -- even though detune runs first in the chain. The panel lays out in
// signal order regardless; spec order and panel order are independent, and
// only this one is permanent.
enum Index
{
    width, shuffle, shuffleFreq,
    detune, detuneOn,
    diffuse, rate, depth,
    rotation, asymmetry,
    count
};

inline constexpr int kVersionHint   = 1;
inline constexpr int kStateVersion  = 1;
inline constexpr int kSchemaVersion = 1;

inline const ParamSpecs& specs()
{
    using S = ParamSpec;
    using F = ParamFormat;

    static const ParamSpecs s
    {
        // WIDTH: the side signal scaled, 100 % being unity. Same law and same
        // range as BMO Util's width, deliberately -- a user who knows one
        // knows the other, and the two modules disagreeing about what 150 %
        // means would be worse than the duplication.
        S::floatParam (kWidth, "Dimension", 0.0f, 200.0f, 1.0f, 100.0f, F::Percent),

        // SHUFFLE: Gerzon's bass shuffler, which widens the low end alone to
        // correct for the ears hearing stereo as narrower in the bass than in
        // the treble. 1.0 is no shuffling and is the default; the S1's manual
        // puts the useful range at 1.6-2.5 and its maximum at 3.
        S::floatParam (kShuffle, "Bloom", 1.0f, 3.0f, 0.01f, 1.0f),

        // The corner the shuffler works below. The S1 allows 350-1400 Hz and
        // recommends 600-700 for normal monitoring; 700 is the default here.
        S::floatParam (kShuffleFreq, "Below", 350.0f, 1400.0f, 1.0f, 700.0f, F::Hertz),

        // DETUNE: two voices, one shifted up and one down by this many cents,
        // opposed so the pair sums back toward the centre. The classic
        // spreader setting is around 10 cents and the range stops well short
        // of MicroPitch's 50 -- past about 25 it stops widening and starts
        // sounding out of tune, and a range that can only be wrong at the top
        // is a range that is too wide.
        S::floatParam (kDetune, "Detune", 0.0f, 25.0f, 0.1f, 10.0f),

        // Off by default, so a freshly inserted instance is transparent and
        // adds no latency until it is asked for. See latencyForParams for why
        // the reported latency does not follow this switch.
        S::boolParam (kDetuneOn, "Generate", false),

        // DIFFUSE: how much of the side signal goes through the all-pass
        // network. 0 % is the dry side signal and is the default.
        S::floatParam (kDiffuse, "Drift", 0.0f, 100.0f, 1.0f, 0.0f, F::Percent),

        // The all-pass coefficients are swept by an LFO -- this is what makes
        // the stage a phaser rather than a fixed decorrelator. Slow by
        // default: this is a widener, and an audible sweep is a different job.
        S::floatParam (kRate,  "Drift Rate",  0.05f, 5.0f, 0.01f, 0.40f),
        S::floatParam (kDepth, "Drift Depth", 0.0f, 100.0f, 1.0f, 50.0f, F::Percent),

        // ROTATION: the whole stereo stage turned, without changing the
        // relative levels of anything standing on it. Degrees, and the S1's
        // own control is unbounded in principle -- this stops at a quarter
        // turn either way, past which the image is inverted rather than
        // rotated.
        S::floatParam (kRotation, "Turn", -45.0f, 45.0f, 0.5f, 0.0f),

        // ASYMMETRY: left against right, with centre material left where it
        // is. Gerzon's control, and the one the S1 was the first product to
        // ship -- it is not a pan, and it is the reason this module is not
        // just a width knob with a crossover. The S1's manual is the source
        // for the law and is quoted at the point of use; see the shear in
        // modules/dim/dsp/DspCore.h, and the test that asserts a dead-centre
        // source comes through it unmoved.
        S::floatParam (kAsymmetry, "Tilt", -100.0f, 100.0f, 1.0f, 0.0f, F::Percent),
    };

    return s;
}

} // namespace bmo::dim
