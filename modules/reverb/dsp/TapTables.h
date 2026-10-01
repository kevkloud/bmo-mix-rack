#pragma once

#include <cmath>
#include <cstddef>
#include <cstdint>

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

    `pan` is -1 hard left, 0 centre, +1 hard right. It is a *bearing*, the
    sine of the image's angle off the listener's facing, and not an L/R offset
    on one tap set: the engine's decorrelation builds **different tap sets per
    channel** from it (`ErGenerator.h`), because an offset applied to a single
    set collapses to combing in mono (10 section 3).

    `order` is how many walls the image bounced off, 1..3, which is what the
    engine's order-banded filters key on. */
struct Tap
{
    float timeMs;      ///< arrival at kReferenceSizeM, relative to the direct sound
    float gain;        ///< linear, relative to the direct sound
    float pan;         ///< -1..+1, the image's bearing
    int   order = 1;   ///< wall bounces
};

inline constexpr int kNumReferenceTaps = 21;   ///< the base count, 10 section 3
inline constexpr int kNumTapTypes      = 6;    ///< Room, Chamber, Hall, Cavern, Plate, Ambience -- `numTypes` in params.h

//==============================================================================
/** **The six shipped tap tables**, in the frozen type order.

    Each is the image-source method run on a shoebox of proportions
    1 : 1.4 : 1.9 with the source and listener off centre, orders 1-3, at
    t_k = d_k / c with c = 343 m/s and a_k = (d_0 / d_k) * beta^n_k -- the
    physically correct 1/r spreading times exponential absorption (05 section
    10.3) -- then the level ceilings 10 section 3 imposes: no tap above
    -15.3 dB, Kuttruff's -0.6 t - 8 dB in the 2-20 ms colouring window with
    the 10 dB dichotic bonus off centre, the two flamming rules as ceilings,
    and a raised-cosine ramp-out over the last 10 ms. Times carry a
    deterministic +-3 % jitter from the seed written on each table.

    **The rows are printed, not typed.** `modules/reverb/dsp/ImageSource.h` is
    the generator, `measure_reverb tables` prints these rows, and
    `tests/dsp/ReverbDspTests.cpp` re-derives every table from its geometry
    and asserts the printed copy matches to the printed precision -- so a row
    edited by hand fails a build. A table that fails 11 section 6's spacing or
    flamming audit is **re-seeded, not patched** (10 section 8): change the
    seed in `imagesource::geometryFor`, reprint, paste. The seeds below are
    the first that passed.

    Every value but Room's beta is CALIBRATE and claims only what the type
    list already fixes: Room < Chamber < Hall < Cavern on reflectivity and
    window, Plate is a flat box with its source and listener high in it, and
    Ambience is the deadest. The listening pass is where they stop being
    placeholders; none has been heard.

    Generated on ICE QUEEN, 2026-09-24. */
inline constexpr Tap kTypeTaps[kNumTapTypes][kNumReferenceTaps]
{
    // Room: beta 0.70, seed 2
    {
        {    1.785f,  0.1718f,  0.000f, 1 },
        {   10.046f,  0.1718f, -0.140f, 1 },
        {   11.775f,  0.1718f,  0.876f, 1 },
        {   18.006f,  0.1718f,  0.599f, 2 },
        {   18.960f,  0.1718f, -0.854f, 2 },
        {   23.087f,  0.1718f, -0.824f, 2 },
        {   24.316f,  0.1718f,  0.000f, 2 },
        {   25.949f,  0.1116f,  0.000f, 2 },
        {   29.362f,  0.1150f,  0.599f, 3 },
        {   31.310f,  0.0813f,  0.059f, 1 },
        {   32.709f,  0.0881f,  0.000f, 3 },
        {   34.865f,  0.0908f,  0.511f, 2 },
        {   37.153f,  0.0863f,  0.981f, 2 },
        {   38.236f,  0.0863f,  0.059f, 2 },
        {   39.151f,  0.0863f, -0.936f, 2 },
        {   41.963f,  0.0907f, -0.111f, 3 },
        {   44.428f,  0.0868f,  0.511f, 3 },
        {   45.743f,  0.0851f,  0.286f, 3 },
        {   48.354f,  0.0807f, -0.548f, 3 },
        {   54.846f,  0.0538f, -0.698f, 3 },
        {   60.849f,  0.0169f,  0.994f, 3 },
    },
    // Chamber: beta 0.76, seed 1
    {
        {    1.634f,  0.1718f,  0.000f, 1 },
        {   10.065f,  0.1718f, -0.104f, 1 },
        {   11.163f,  0.1718f,  0.839f, 1 },
        {   17.386f,  0.1718f,  0.591f, 2 },
        {   18.387f,  0.1718f,  0.591f, 3 },
        {   22.550f,  0.1718f, -0.104f, 2 },
        {   24.470f,  0.1718f, -0.796f, 3 },
        {   27.512f,  0.0961f,  0.041f, 1 },
        {   29.084f,  0.1212f,  0.839f, 3 },
        {   30.526f,  0.0984f,  0.839f, 3 },
        {   33.200f,  0.0984f,  0.497f, 3 },
        {   34.425f,  0.0984f, -0.848f, 3 },
        {   37.592f,  0.1092f,  0.041f, 2 },
        {   40.369f,  0.0705f, -0.936f, 3 },
        {   42.067f,  0.0705f,  0.041f, 3 },
        {   44.539f,  0.0678f, -0.507f, 3 },
        {   50.020f,  0.0854f,  0.794f, 3 },
        {   51.397f,  0.0842f,  0.000f, 3 },
        {   65.846f,  0.0729f, -0.967f, 3 },
        {   69.676f,  0.0467f,  0.052f, 2 },
        {   72.946f,  0.0194f, -0.267f, 3 },
    },
    // Hall: beta 0.82, seed 2
    {
        {    1.329f,  0.1718f, -0.000f, 1 },
        {    8.615f,  0.1718f, -0.130f, 1 },
        {    9.824f,  0.1718f,  0.787f, 1 },
        {   15.275f,  0.1718f, -0.777f, 1 },
        {   16.378f,  0.1718f,  0.550f, 2 },
        {   20.182f,  0.1718f, -0.767f, 2 },
        {   21.095f,  0.1718f, -0.767f, 3 },
        {   22.046f,  0.1718f,  0.787f, 2 },
        {   26.209f,  0.1361f, -0.130f, 3 },
        {   27.703f,  0.1361f,  0.539f, 3 },
        {   29.149f,  0.1211f, -0.107f, 2 },
        {   30.213f,  0.1361f, -0.767f, 3 },
        {   32.825f,  0.1211f,  0.953f, 2 },
        {   35.098f,  0.1106f,  0.539f, 3 },
        {   37.480f,  0.0984f, -0.904f, 2 },
        {   39.254f,  0.1106f, -0.913f, 3 },
        {   40.798f,  0.1361f,  0.953f, 3 },
        {   56.750f,  0.1361f,  0.982f, 3 },
        {   68.190f,  0.1205f,  0.059f, 2 },
        {   76.730f,  0.1183f,  0.059f, 3 },
        {   92.165f,  0.0252f,  0.062f, 3 },
    },
    // Cavern: beta 0.88, seed 1
    {
        {    1.757f,  0.1718f,  0.000f, 1 },
        {   10.078f,  0.1718f,  0.021f, 1 },
        {   11.364f,  0.1718f,  0.021f, 2 },
        {   14.195f,  0.1718f, -0.798f, 2 },
        {   18.049f,  0.1144f, -0.006f, 1 },
        {   19.518f,  0.1718f, -0.651f, 2 },
        {   25.883f,  0.0963f,  0.751f, 2 },
        {   27.617f,  0.0963f, -0.798f, 2 },
        {   29.172f,  0.1120f,  0.018f, 3 },
        {   31.730f,  0.1120f, -0.651f, 3 },
        {   33.761f,  0.1120f,  0.458f, 3 },
        {   34.819f,  0.0963f, -0.930f, 2 },
        {   38.807f,  0.1120f,  0.018f, 3 },
        {   43.155f,  0.0788f,  0.790f, 3 },
        {   44.247f,  0.0788f,  0.922f, 3 },
        {   59.357f,  0.1109f,  0.961f, 3 },
        {   70.297f,  0.0637f, -0.009f, 3 },
        {   72.564f,  0.0637f,  0.248f, 3 },
        {   74.212f,  0.0637f, -0.009f, 3 },
        {   78.936f,  0.1098f,  0.015f, 3 },
        {   85.600f,  0.0328f, -0.010f, 3 },
    },
    // Plate: beta 0.80, seed 8
    {
        {    2.363f,  0.1718f,  0.000f, 1 },
        {    4.459f,  0.1718f,  0.000f, 1 },
        {    9.739f,  0.1718f, -0.140f, 1 },
        {   10.760f,  0.1718f,  0.000f, 2 },
        {   12.663f,  0.1718f,  0.876f, 2 },
        {   17.355f,  0.1200f, -0.140f, 3 },
        {   18.439f,  0.1718f,  0.876f, 3 },
        {   19.351f,  0.1718f, -0.854f, 2 },
        {   20.690f,  0.1718f, -0.854f, 2 },
        {   21.976f,  0.1718f,  0.000f, 3 },
        {   23.435f,  0.1718f, -0.824f, 2 },
        {   24.824f,  0.1718f, -0.824f, 3 },
        {   26.413f,  0.1487f, -0.824f, 3 },
        {   31.300f,  0.1321f,  0.059f, 2 },
        {   37.175f,  0.1029f,  0.511f, 3 },
        {   39.388f,  0.0940f, -0.471f, 2 },
        {   40.629f,  0.0817f, -0.936f, 2 },
        {   42.629f,  0.0820f, -0.111f, 3 },
        {   44.315f,  0.0798f, -0.941f, 3 },
        {   48.763f,  0.1032f, -0.548f, 3 },
        {   55.890f,  0.0271f, -0.698f, 3 },
    },
    // Ambience: beta 0.66, seed 5
    {
        {    2.170f,  0.1718f,  0.000f, 1 },
        {   11.287f,  0.1718f, -0.131f, 1 },
        {   12.297f,  0.1702f, -0.131f, 2 },
        {   14.255f,  0.1718f,  0.915f, 2 },
        {   19.215f,  0.1056f,  0.000f, 1 },
        {   20.608f,  0.1056f,  0.619f, 3 },
        {   25.796f,  0.0659f, -0.131f, 2 },
        {   26.763f,  0.0659f,  0.000f, 2 },
        {   30.543f,  0.0516f,  0.619f, 3 },
        {   32.419f,  0.0570f, -0.894f, 2 },
        {   34.746f,  0.0516f,  0.064f, 1 },
        {   36.448f,  0.0528f, -0.841f, 3 },
        {   37.525f,  0.0520f, -0.894f, 3 },
        {   39.007f,  0.0647f,  0.496f, 2 },
        {   40.787f,  0.0505f,  0.496f, 3 },
        {   42.991f,  0.0484f, -0.953f, 3 },
        {   44.542f,  0.0676f, -0.463f, 2 },
        {   47.880f,  0.0506f,  0.496f, 3 },
        {   50.557f,  0.0318f, -0.463f, 3 },
        {   51.739f,  0.0237f, -0.104f, 3 },
        {   55.187f,  0.0128f,  0.000f, 3 },
    },
};

/** **Room's table, which is what the panel draws.** The display reads one
    table for every type today; the engine plays the type's own. That gap is
    the panel's to close and is recorded in modules/reverb/AGENTS.md. */
inline constexpr const Tap (&kReferenceTaps)[kNumReferenceTaps] = kTypeTaps[0];

/** The size the tables' times are quoted at. 10 section 3's Size law is
    t_k(S) = t_k,ref * S / S_ref, so this is S_ref -- and it is Room's default
    SIZE, which is what makes a fresh instance's picture the table's own
    numbers rather than a scaled copy of them. Kept in step with
    `roomDefaults::kSizeM` by a test rather than by a comment. */
inline constexpr float kReferenceSizeM = 12.0f;

/** The room size below which a tap stops getting louder.

    The 1 / d law is physical -- closer walls, stronger reflections -- but it has
    no bottom, and SIZE reaches 0.5 m: at the settings a user has, pink noise at
    -18 dBFS RMS came out over full scale under about 4 m and at +18.6 dBFS at
    0.5 m (the PR #27 review, on AURORA). Nothing under 6 m was in either
    listening set, so Frosty's call on 2026-09-30 was to hold the gain at its
    6 m figure for every smaller room. Everything he heard is untouched, and
    only the gain is held: the times go on scaling, so a smaller room is still
    an earlier and tighter one.

    **Holding the gain did not hold the level.** Taps that bunch up sum more
    coherently in the bass, so with the gain merely held the output still rose
    by up to about 7 dB between 6 m and 0.5 m on a bass-heavy signal, and the
    bottom corner read +1.8 dBFS on that same noise. `kSmallRoomSlope` below
    is what takes that back out. */
inline constexpr float kGainFloorSizeM = 6.0f;

/** How fast a tap eases down as the room shrinks below `kGainFloorSizeM`: its
    gain is the 6 m gain times (SIZE / 6) ^ this, about 1.5 dB per halving.

    Frosty's call on 2026-10-01, and his reason: short of an extreme resonance
    or a room mode, a real room's reflections never double what went in, so a
    room that only got smaller should not come out louder. 0.25 is the one
    slope that offsets the bunching across all six tables -- measured on
    AURORA with the ER alone, a room of 0.5 to 3 m peaks between 2.2 dB above
    and 3.5 dB below its 6 m figure, and at every type's own voicing the
    output stays under full scale at every SIZE (worst -0.7 dBFS, Room at
    0.5 m). One slope cannot put six tables on zero, and it errs quiet. */
inline constexpr float kSmallRoomSlope = 0.25f;

/** The tap's arrival at room size `sizeM`. Times scale with the dimension;
    gains scale as 1 / d and so as the inverse of the same factor, down to
    `kGainFloorSizeM`; below it they ease back down (`kSmallRoomSlope`). **The time law is linear over the whole
    0.5-80 m range**: 10 section 3's window clamp
    (5-100 ms, 5-200 ms for halls) is not applied, because the panel's sketch
    scales linearly and the two would otherwise disagree. Recorded as an open
    point in the M2 note. */
inline constexpr float tapTimeMsAt (const Tap& t, float sizeM) noexcept
{
    return t.timeMs * sizeM / kReferenceSizeM;
}

inline float tapGainAt (const Tap& t, float sizeM) noexcept
{
    if (sizeM >= kGainFloorSizeM)
        return t.gain * kReferenceSizeM / sizeM;

    const auto ratio = (sizeM > 0.01f ? sizeM : 0.01f) / kGainFloorSizeM;
    return t.gain * kReferenceSizeM / kGainFloorSizeM * std::pow (ratio, kSmallRoomSlope);
}

/** When a type's last reflection arrives, in milliseconds, at room size
    `sizeM`. This is `t_ER,max` in the tail-length formula (10 section 5). */
inline constexpr float erSpanMsAt (int typeIndex, float sizeM) noexcept
{
    const auto t = typeIndex >= 0 && typeIndex < kNumTapTypes ? typeIndex : 0;
    return tapTimeMsAt (kTypeTaps[t][kNumReferenceTaps - 1], sizeM);
}

/** Room's span: what the panel and the older tests read. */
inline constexpr float erSpanMsAt (float sizeM) noexcept
{
    return erSpanMsAt (0, sizeM);
}

//==============================================================================
// A small deterministic hash, so a seed and an index give the same jitter on
// every machine and every build. Shared by the offline generator (the table
// jitter) and the engine (infill placement, thresholds, split offsets).

inline constexpr std::uint32_t hash32 (std::uint32_t seed, std::uint32_t index) noexcept
{
    auto x = seed * 0x9E3779B9u ^ (index + 0x7F4A7C15u) * 0x85EBCA6Bu;
    x ^= x >> 16; x *= 0x7FEB352Du;
    x ^= x >> 15; x *= 0x846CA68Bu;
    x ^= x >> 16;
    return x;
}

/** Uniform in [-1, 1). */
inline constexpr float signedUnit (std::uint32_t seed, std::uint32_t index) noexcept
{
    return (float) (hash32 (seed, index) & 0xFFFFFFu) / (float) 0x800000u - 1.0f;
}

/** Uniform in [0, 1). */
inline constexpr float unit (std::uint32_t seed, std::uint32_t index) noexcept
{
    return (float) (hash32 (seed, index) & 0xFFFFFFu) / (float) 0x1000000u;
}

} // namespace bmo::reverb
