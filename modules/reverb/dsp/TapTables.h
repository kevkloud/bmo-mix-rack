#pragma once

#include <cstddef>

namespace bmo::reverb
{

//==============================================================================
/** One early reflection, at the reference room size, and the Size law for it.

    **The placeholder table that lived here is retired** (2026-09-24). It was a
    hand-written stand-in, 21 taps with plausible times, that the panel drew
    while the image-source tables did not exist. They do now -- `ErTable.h`,
    served from the generated `ErTableData.inc` -- and the panel, the engine
    and the tail formula all read those, so nothing read the stand-in any
    more. What stays is what was always real here: the reference size the
    tables are quoted at, and the per-tap Size law.

    JUCE-free and panel-includable, like `ErTable.h`: the panel's scatter
    draws the taps the engine plays, and `docs/reverb/11-integration-and-
    test-plan.md` section 5 names drift between the two as the display's one
    real risk.

    `pan` is -1 hard left, 0 centre, +1 hard right: a *bearing*, not an L/R
    offset on one tap set. The real decorrelation is **different tap sets per
    channel**, because an offset applied to a single set collapses to combing
    in mono (10 section 3). */
struct Tap
{
    float timeMs;   ///< arrival at kReferenceSizeM, relative to the direct sound
    float gain;     ///< linear, relative to the direct sound
    float pan;      ///< -1..+1, the image's bearing
};

/** The size the tables' times are quoted at. 10 section 3's Size law is
    t_k(S) = t_k,ref * S / S_ref, so this is S_ref -- and it is Room's default
    SIZE, so Room's table at its default is its own numbers rather than a
    scaled copy of them. Kept in step with `roomDefaults::kSizeM` by a test
    rather than by a comment. */
inline constexpr float kReferenceSizeM = 12.0f;

/** A tap's arrival at room size `sizeM`: times scale with the dimension.
    **The raw law, with no window clamp** -- what the engine plays is this
    held to the table's window, which is `erSizeScale` in ErTable.h. */
inline constexpr float tapTimeMsAt (const Tap& t, float sizeM) noexcept
{
    return t.timeMs * sizeM / kReferenceSizeM;
}

/** A tap's gain at room size `sizeM`: 1 / d, so the inverse of the same
    factor. Raw, as `tapTimeMsAt` is. */
inline constexpr float tapGainAt (const Tap& t, float sizeM) noexcept
{
    return t.gain * kReferenceSizeM / (sizeM > 0.01f ? sizeM : 0.01f);
}

} // namespace bmo::reverb
