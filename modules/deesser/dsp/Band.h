#pragma once

#include "core/dsp/Design.h"
#include "core/dsp/Svf.h"
#include "modules/deesser/params.h"

#include <algorithm>
#include <cmath>

namespace bmo::deesser
{

//==============================================================================
/** Which filter the module cuts with, and detects through.

    *Bell* is the surgical one: a constant-Q band, narrow enough to miss the
    vowel region. *High shelf* is the split-band mode without a crossover --
    one minimum-phase filter, so reconstruction error is identically zero at
    every depth, which is the whole reason this topology was chosen over a
    crossover (docs/deesser/10-dsp-spec.md 1).

    The labels and this index order are permanent; they are BMO DEQ's, so that
    two modules doing the same thing to a band call it the same thing.

    **It lives here rather than in `DspCore.h`** because the filters need it
    and the core includes them, not the other way round. */
enum class Shape { bell = 0, highShelf };

//==============================================================================
/** The cut this module makes, as a `dsp::Biquad`.

    Straight to the shared matched-Z design (`core/dsp/Design.h`), which is
    where BMO DEQ's bell and high shelf live since 2026-09-21. `depthDb` is a
    depth and enters negative: this module only ever cuts.

    A shelf's Q goes through `effectiveQ` here as everywhere else, so the
    engine, the panel sketch and the tests cannot disagree about what a shelf
    at Q 4 actually is. */
inline dsp::Biquad cutDesign (Shape shape, double hz, double q, double depthDb,
                              const dsp::DesignGrid& grid) noexcept
{
    const auto shapeIndex = shape == Shape::highShelf ? highShelf : bell;
    const auto shaped     = (double) effectiveQ (shapeIndex, (float) q);

    return dsp::designMatched (shape == Shape::highShelf ? dsp::Shape::highShelf
                                                         : dsp::Shape::bell,
                               hz, shaped, -depthDb, grid);
}

/** The filter the **detector** listens through, which is not the filter the
    module cuts with.

    Bell mode detects through a constant-Q bandpass at the same centre: narrow
    enough to miss the vowel region, wide enough to cover a male 3-6 kHz and a
    female 6-8 kHz concentration at one setting. Shelf mode detects through a
    second-order high-pass at the same corner, because that is what the shelf
    takes down -- detecting through a bandpass while cutting a shelf would put
    the decision and the action on different bands.

    **RBJ rather than matched-Z, and module-local rather than shared.** This is
    a sidechain: nothing downstream hears it, so the near-Nyquist cramping that
    matched-Z exists to avoid costs a fraction of a dB in a detector whose
    threshold is calibrated by ear anyway. The high-pass could have come from
    the shared design as `lowCut` and the bandpass could not -- there is no
    bandpass in it, because no module has ever cut with one -- and one
    detector built from two different design methods would be harder to reason
    about than one built from a cookbook that has both.

    Per the repo's rule, a module's detector is its own
    (docs/fet-comp/00-repo-conventions.md 2). This is the detector. */
inline dsp::Biquad detectorDesign (Shape shape, double hz, double q, double sampleRate) noexcept
{
    const auto w = 2.0 * dsp::kPi * hz / sampleRate;
    const auto cosW = std::cos (w), sinW = std::sin (w);
    const auto alpha = sinW / (2.0 * std::max (q, 0.1));

    const auto a0 = 1.0 + alpha;

    dsp::Biquad out;
    out.a1 = -2.0 * cosW / a0;
    out.a2 = (1.0 - alpha) / a0;

    if (shape == Shape::highShelf)
    {
        // Second-order high-pass at the corner the shelf turns over at.
        const auto b = (1.0 + cosW) / 2.0;
        out.b0 =  b / a0;
        out.b1 = -2.0 * b / a0;
        out.b2 =  b / a0;
    }
    else
    {
        // Constant-Q bandpass, unity at the centre -- the 0 dB peak form, so
        // the band level the detector reads is the signal's level in the band
        // rather than that scaled by Q.
        out.b0 =  alpha / a0;
        out.b1 =  0.0;
        out.b2 = -alpha / a0;
    }

    return out;
}

//==============================================================================
/** The reduction element: one TPT state-variable filter per channel, its
    coefficients re-derived on the control tick and interpolated between ticks.

    **Why an SVF and not the biquad the design is written as.** An SVF's state
    means the same thing before and after a coefficient change, so a
    sample-by-sample glide between two designs is smooth; a direct-form biquad
    has no such property. More to the point, `g > 0, k > 0` is the structure's
    stability condition and it holds *everywhere on a straight line between two
    stable sets* -- so no intermediate state can ring, however fast the
    detector moves. That is what makes an 0.8 ms attack safe on a filter whose
    depth is being modulated at audio rates (`core/dsp/Svf.h`, and
    docs/deesser/10-dsp-spec.md 5). */
struct Band
{
    static constexpr int kMaxChannels = 2;

    dsp::SvfCoeffs cur {}, next {}, step {};
    dsp::SvfTaps   taps {};
    std::array<dsp::SvfState, kMaxChannels> state {};

    /** The detector's sidechain filter, run on the **dry** input. Never the
        output filter, whose poles track its own gain: tapping that would close
        a feedback loop (the reason is recorded in BMO DEQ's core, the engine
        this one is shaped after). */
    dsp::SvfCoeffs sideCoeffs {};
    dsp::SvfTaps   sideTaps {};
    std::array<dsp::SvfState, kMaxChannels> sideState {};

    /** What `next` was designed from, so a band nobody is moving is not
        redesigned 6000 times a second. */
    Shape  designedShape = Shape::bell;
    double designedHz = -1.0, designedQ = -1.0, designedDepth = -1.0;
    Shape  sideShape = Shape::bell;
    double sideHz = -1.0, sideQ = -1.0;

    void reset() noexcept
    {
        for (auto& s : state)     s.reset();
        for (auto& s : sideState) s.reset();
    }

    /** Re-derive the cut if anything it depends on has moved, and set the
        per-sample increment that walks `cur` to it over one interval. */
    void design (Shape shape, double hz, double q, double depthDb,
                 const dsp::DesignGrid& grid, int interval, bool snap) noexcept
    {
        const auto moved = shape != designedShape
                        || std::abs (hz - designedHz) > 1.0e-9
                        || std::abs (q - designedQ) > 1.0e-9
                        || std::abs (depthDb - designedDepth) > 1.0e-9;

        if (moved)
        {
            const auto design = cutDesign (shape, hz, q, depthDb, grid);
            const auto c = dsp::SvfCoeffs::fromBiquad (design);

            // A design that is not finite or not stable is dropped rather than
            // loaded. Every input is clamped upstream so this should be
            // unreachable, but "should be unreachable" is how a NaN reaches a
            // speaker.
            if (design.isFinite() && design.isStable() && c.isStable())
            {
                next = c;
                designedShape = shape;
                designedHz = hz; designedQ = q; designedDepth = depthDb;
            }
        }

        if (snap)
            cur = next;

        const auto n = (double) std::max (interval, 1);
        step.g  = (next.g  - cur.g)  / n;
        step.k  = (next.k  - cur.k)  / n;
        step.m0 = (next.m0 - cur.m0) / n;
        step.m1 = (next.m1 - cur.m1) / n;
        step.m2 = (next.m2 - cur.m2) / n;

        taps = dsp::SvfTaps::of (cur.g, cur.k);
    }

    /** The detector's filter, redesigned only when its own inputs move. It
        does **not** follow the depth: the band being listened to is a property
        of the settings, not of how hard the module happens to be working. */
    void designSide (Shape shape, double hz, double q, double sampleRate) noexcept
    {
        if (shape == sideShape
            && std::abs (hz - sideHz) < 1.0e-9
            && std::abs (q - sideQ) < 1.0e-9)
            return;

        const auto design = detectorDesign (shape, hz, q, sampleRate);
        const auto c = dsp::SvfCoeffs::fromBiquad (design);

        if (! design.isFinite() || ! design.isStable() || ! c.isStable())
            return;

        sideCoeffs = c;
        sideTaps = dsp::SvfTaps::of (c.g, c.k);
        sideShape = shape; sideHz = hz; sideQ = q;
    }

    /** Walk the live coefficients one sample toward the target. */
    void advance() noexcept
    {
        cur.g  += step.g;
        cur.k  += step.k;
        cur.m0 += step.m0;
        cur.m1 += step.m1;
        cur.m2 += step.m2;

        taps = dsp::SvfTaps::of (cur.g, cur.k);
    }

    double filterSample (int channel, double x) noexcept
    {
        return state[(size_t) channel].process (taps, cur, x);
    }

    double sideSample (int channel, double x) noexcept
    {
        return sideState[(size_t) channel].process (sideTaps, sideCoeffs, x);
    }

    void flushTiny() noexcept
    {
        for (auto& s : state)     s.flushTiny();
        for (auto& s : sideState) s.flushTiny();
    }
};

} // namespace bmo::deesser
