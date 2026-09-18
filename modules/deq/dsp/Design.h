#pragma once

#include "modules/deq/dsp/Prototype.h"
#include <array>

namespace bmo::deq
{

/** Limits every design is clamped to before it is computed. Degenerate input
    (f0 = 0, f0 = Nyquist, Q -> 0) is clamped rather than rejected, so no
    parameter combination a host can send produces NaN. */
struct DesignLimits
{
    static constexpr double kMinHz        = 5.0;
    static constexpr double kMaxNyquist   = 0.499;   // fraction of the sample rate
    static constexpr double kMinQ         = 0.1;
    static constexpr double kMaxQ         = 40.0;
    static constexpr double kMaxGainDb    = 30.0;
    static constexpr double kUnityGainDb  = 1.0e-9;  // below this a gain band is the identity
};

/** The frequencies a least-squares zero fit is evaluated at, for one sample
    rate. Computing them costs 48 pow() and sin() calls, which was most of a
    shelf design's 2.4 us; an engine builds one in prepare() and reuses it.
    Plain data, owned by the caller -- no statics, so two instances on two
    threads never share anything. */
struct DesignGrid
{
    static constexpr int kPoints = 48;

    double sampleRate = 0.0;
    std::array<double, kPoints> omega {}, omega2 {}, phi {};

    static DesignGrid make (double sampleRate) noexcept;
};

/** The zero-latency design: a second-order filter whose poles are the
    prototype's poles mapped exactly (z = e^(sT), the matched Z-transform) and
    whose zeros are fitted to the prototype's magnitude response.

    Why not the bilinear transform: it warps the frequency axis, so a bell or
    shelf near Nyquist is squeezed ("cramped") toward it. The usual fix is to
    oversample, which is what costs TDR Nova its latency. Mapping the poles
    exactly and fitting the zeros to the analogue magnitude gets most of the
    accuracy back at zero samples. The cookbook bilinear designs are kept only
    as a reference in modules/deq/reference, never on this path.

    Per shape:

    - **bell** -- after Vicanek: unity at DC, the exact gain at f0, and zero
      slope at f0, so the peak sits where the knob says at the height it says.
    - **low shelf, high cut** -- a least-squares fit of |H|^2 over the band
      with DC pinned. The three-point fits (DC/f0/Nyquist) fail outright on
      some shelves and are several dB out on others.
    - **low cut** -- double zero at DC, the one remaining degree of freedom by
      least squares.
    - **high shelf** -- built from the low shelf. A boosted high shelf's poles
      sit at f0 * sqrtA, above Nyquist for large boosts near the top, and the
      matched transform aliases a pole mapped from there (up to 25 dB of
      error). HS(A) = A^2 / LS(A) exactly, so the high shelf's error in dB is
      the low shelf's, negated.

    **A cut is the exact reciprocal of the matching boost**, for bells and
    shelves both. The analogue prototypes satisfy H(-g) = 1/H(g), so this makes
    boost-then-cut null exactly, and it makes a cut exactly as accurate as its
    boost -- which matters, because cuts designed directly came out about three
    times worse (a cut's poles are wider, so more of its skirt sits past
    Nyquist).

    Always returns a stable, finite filter. If a zero fit has no real
    solution, it falls back to mapping the prototype's zeros as well (the
    classic matched Z-transform), which cannot fail.
*/
Biquad designMatched (Shape shape, double frequencyHz, double q, double gainDb, const DesignGrid& grid) noexcept;

/** The same, building a grid for the call. For tests and tools; an engine
    should hold a grid. */
Biquad designMatched (Shape shape, double frequencyHz, double q, double gainDb, double sampleRate) noexcept;

/** The prototype's poles, mapped exactly. Exposed for the coefficient
    integrity tests: a2 must equal e^(-(d1/d2) T) for the prototype's own
    poles, which is the invariant the spec states against the knob values and
    gets wrong for bells and shelves (see Prototype.h). */
void matchedPoles (const Prototype& prototype, double sampleRate, double& a1, double& a2) noexcept;

/** The clamped values a design actually uses. */
double clampFrequency (double frequencyHz, double sampleRate) noexcept;
double clampQ (double q) noexcept;
double clampGainDb (double gainDb) noexcept;

} // namespace bmo::deq
