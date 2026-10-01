#pragma once

#include "modules/dwell/dsp/GainLaws.h"
#include "modules/dwell/params.h"

#include <algorithm>
#include <cmath>

/*  Tempo and tail arithmetic, JUCE-free and stateless, so the adapter, the
    panel and the tests can all ask the same question the same way.

    - `syncedMs` is docs/delay/10 §7's mapping from a NOTE index at a tempo to
      a delay time, halved until it fits the ring.
    - `tailSecondsFor` is §9's tail, widened by §11.6 to the larger of the two
      engines', computed from parameters only so a host can be told before
      the audio thread has caught up (core/dsp/ModuleDsp.h). */

namespace bmo::dwell
{

/** Beats -- quarter notes -- per NOTE index, in `kNoteNames` order: 1/32 is an
    eighth of a beat, dotted is x1.5, triplet x2/3 (§7). Sixteen, ascending,
    because the index *is* the automation lane. */
inline constexpr double kNoteBeats[]
{
    0.125, 0.25 * 2.0 / 3.0, 0.1875, 0.25, 0.5 * 2.0 / 3.0, 0.375, 0.5, 1.0 * 2.0 / 3.0,
    0.75,  1.0,              2.0 * 2.0 / 3.0, 1.5, 2.0,     4.0 * 2.0 / 3.0, 3.0, 4.0
};

static_assert (std::size (kNoteBeats) == std::size (kNoteNames),
               "one beat figure per NOTE index");

/** NOTE index `choice` at `bpm`, in ms. **Halved until it fits** the
    `kMaxTimeMs` ring (§7): a whole note at 60 bpm is 4 s, which plays as 2 s
    -- the same note an octave of time shorter, on the grid, rather than a
    clamp that would put it off the grid. `bpm` is whatever the host's
    validity window let through (core/product/HostTempo.h, 10-999), which is
    wider than §7's 20-999; halving is what makes the bottom of it safe. */
inline double syncedMs (int choice, double bpm) noexcept
{
    const auto i = std::clamp (choice, 0, (int) std::size (kNoteBeats) - 1);
    auto ms = kNoteBeats[i] * 60000.0 / std::max (bpm, 1.0);

    while (ms > (double) kMaxTimeMs)
        ms *= 0.5;

    return ms;
}

/** The ceiling and floor §9 puts on a reported tail, in seconds. */
inline constexpr double kTailFloorSeconds   = 0.5;
inline constexpr double kTailCeilingSeconds = 30.0;

/** Laps from the first repeat to -60 dB at loop gain `g`,
    `ceil(60 / -20 log10 g)`, or -1 when the loop holds or builds and never
    gets there. A loop with no feedback still plays one repeat.

    **The gain is not capped.** §9 writes `min(g, 0.97)`, and that cap counted
    the laps for a loop faster than the one running: between about 95 % and
    97 % FEEDBACK the figure came out at a fraction of the real decay --
    measured on AURORA 2026-10-01, FEEDBACK 96.9 % at TIME 20 ms reported
    4.54 s and was still ringing after 40. Uncapped, a gain near unity asks for
    thousands of laps, and the 30 s ceiling in `tailSecondsFor` is what bounds
    the answer, as it already did for a loop at or past unity. */
inline double lapsToSixtyDb (double g) noexcept
{
    if (g >= 1.0)
        return -1.0;

    if (g <= 0.0)
        return 1.0;

    return std::ceil (60.0 / (-20.0 * std::log10 (g)));
}

/** How long Dwell rings on after its input stops, in seconds, for the
    parameter values `v`.

    **The larger of the two engines'** (§11.6). The main delay's is its time
    times its laps to -60 at FEEDBACK's loop gain. The lane's counts only while
    HOLD is on: a THROW decays like the main delay, at its own time and its
    tail's gain; a FREEZE or a BUILD never decays and reports the ceiling.
    Clamped to [0.5 s, 30 s]; a loop at or past unity reports 30.

    **Loop gains are taken at `P_c` = 1**, which is each character's loop
    magnitude at its own peak -- the slowest-decaying frequency, the last one
    a listener hears -- so the figure is never shorter than the measured time
    to -60 (`11` §4j).

    **With SYNC on, each engine's time is taken at the ring's full 2 s.** A
    tail comes from parameters alone, and the tempo is not a parameter; the
    adapter's held tempo lives on the audio thread and a host may ask for the
    tail from any thread. 2 s is the longest any division can play once
    halved to fit, so the figure is conservative, never short. */
inline double tailSecondsFor (const float* v, int count) noexcept
{
    if (v == nullptr || count < Index::count)
        return 0.0;

    const auto synced = kSyncIsEnabled && v[Index::sync] > 0.5f;
    const auto mainT = (synced ? (double) kMaxTimeMs : (double) v[Index::time]) / 1000.0;
    const auto laneT = (synced ? (double) kMaxTimeMs : (double) v[Index::laneTime]) / 1000.0;

    const auto engineTail = [] (double seconds, double g)
    {
        const auto laps = lapsToSixtyDb (g);
        return laps < 0.0 ? kTailCeilingSeconds : seconds * laps;
    };

    auto tail = engineTail (mainT, (double) feedbackGainFor (v[Index::feedback], 1.0));

    if (v[Index::hold] > 0.5f)
    {
        // The detent is a literal: lane_gain at 0 is FREEZE (laneGainFor).
        const auto laneTail = v[Index::laneGain] >= 0.0f
                                ? kTailCeilingSeconds
                                : engineTail (laneT, (double) laneGainFor (v[Index::laneGain], 1.0));
        tail = std::max (tail, laneTail);
    }

    return std::clamp (tail, kTailFloorSeconds, kTailCeilingSeconds);
}

} // namespace bmo::dwell
