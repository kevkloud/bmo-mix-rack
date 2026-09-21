#pragma once

#include <cstddef>

namespace bmo::reverb
{

//==============================================================================
/** One early reflection, at the reference room size.

    JUCE-free and panel-includable **on purpose, and it is the only header in
    `dsp/` that is required to be**. The panel's ER/tail sketch draws the taps,
    and the engine plays them; if they read two tables the picture drifts from
    the sound and nothing notices. `docs/reverb/11-integration-and-test-plan.md`
    section 5 names that as the display's one real risk and asks for exactly
    this: one source of truth, plus a layout test asserting the sketch's first
    tap time equals the table's.

    `pan` is -1 hard left, 0 centre, +1 hard right. It is a *bearing*, not an
    L/R offset on one tap set: the real decorrelation uses **different tap sets
    per channel**, because an offset applied to a single set collapses to
    combing in mono (10 section 3). A single-table skeleton cannot express
    that, which is one of the reasons the table below is a placeholder. */
struct Tap
{
    float timeMs;   ///< arrival at kReferenceSizeM, relative to the direct sound
    float gain;     ///< linear, relative to the direct sound
    float pan;      ///< -1..+1, the image's bearing
};

//==============================================================================
/** **PLACEHOLDER GEOMETRY. These are not the shipped tap tables.**

    What is real here is the *shape* of the data, the reference size the times
    are quoted at, and the Size law that scales them. What is not real is every
    number in the table.

    The shipped tables come offline from the image-source method for a shoebox
    of proportions 1 : 1.4 : 1.9 with the source and the listener off centre,
    orders 1-3, at t_k = d_k / c with c = 343 m/s and a_k = (1 m / d_k) *
    beta^n_k -- 21 core taps per type, beta from 0.70 (Room) to 0.88 (Large
    Hall) and CALIBRATE (10 section 3). None of that exists yet. The twenty-one
    rows below are a hand-written stand-in with plausible times, a gain law of
    roughly the right slope and bearings that alternate about the centre, so
    that the panel has a picture to draw and the layout test has a number to
    check against before the generator is written.

    **They are not claimed to satisfy any of the rules the real tables must.**
    Minimum separation 0.9 ms, no two inter-tap gaps within 2 % of each other,
    mutually prime times, the Kuttruff level ceiling in the 2-20 ms colouring
    window, the no-full-band-tap-between-1-and-8-ms rule, the flamming rules
    and the lateral-fraction target are all in 11 section 6 and none of them is
    asserted here. When the real tables land, **a failing table is re-seeded,
    not patched** (10 section 8), and the comb and flam audits run *after* the
    jitter, not before it.

    The first two taps stay near the centre deliberately, which is the one
    property of the real set this stand-in does reproduce: the phantom centre
    has to hold, so the earliest reflections cannot be thrown wide. */
inline constexpr Tap kReferenceTaps[]
{
    {   7.3f, 0.501f,  0.05f },
    {  11.9f, 0.447f, -0.08f },
    {  15.1f, 0.398f,  0.34f },
    {  18.7f, 0.372f, -0.41f },
    {  22.3f, 0.339f,  0.22f },
    {  25.9f, 0.309f, -0.55f },
    {  29.2f, 0.282f,  0.61f },
    {  32.7f, 0.263f, -0.19f },
    {  36.1f, 0.240f,  0.48f },
    {  39.8f, 0.219f, -0.67f },
    {  43.3f, 0.204f,  0.14f },
    {  46.9f, 0.186f, -0.36f },
    {  50.4f, 0.170f,  0.72f },
    {  54.1f, 0.158f, -0.27f },
    {  57.6f, 0.145f,  0.39f },
    {  61.3f, 0.132f, -0.74f },
    {  64.8f, 0.123f,  0.18f },
    {  68.4f, 0.112f, -0.49f },
    {  71.9f, 0.104f,  0.66f },
    {  75.6f, 0.095f, -0.31f },
    {  79.1f, 0.087f,  0.43f },
};

inline constexpr int kNumReferenceTaps = (int) (sizeof (kReferenceTaps) / sizeof (Tap));

/** The size the table's times are quoted at. 10 section 3's Size law is
    t_k(S) = t_k,ref * S / S_ref, so this is S_ref -- and it is Room's default
    SIZE, which is what makes a fresh instance's picture the table's own
    numbers rather than a scaled copy of them. Kept in step with
    `roomDefaults::kSizeM` by a test rather than by a comment. */
inline constexpr float kReferenceSizeM = 12.0f;

/** The tap's arrival at room size `sizeM`. Times scale with the dimension;
    gains scale as 1 / d and so as the inverse of the same factor. */
inline constexpr float tapTimeMsAt (const Tap& t, float sizeM) noexcept
{
    return t.timeMs * sizeM / kReferenceSizeM;
}

inline constexpr float tapGainAt (const Tap& t, float sizeM) noexcept
{
    return t.gain * kReferenceSizeM / (sizeM > 0.01f ? sizeM : 0.01f);
}

/** When the last reflection arrives, in milliseconds, at room size `sizeM`.
    This is `t_ER,max` in the tail-length formula (10 section 5). */
inline constexpr float erSpanMsAt (float sizeM) noexcept
{
    return tapTimeMsAt (kReferenceTaps[kNumReferenceTaps - 1], sizeM);
}

} // namespace bmo::reverb
