#pragma once

// The matched-Z design, from `core/dsp` -- **not from `modules/deq/dsp`**.
//
// Those five files (Biquad.h, Design.h, Design.cpp, Prototype.h, Svf.h) moved
// out of BMO DEQ when BMO Defang became their second caller, and BMO Linger is
// the third. They arrived here as byte-identical copies of the blobs on
// `frosty-add-bmo-defang` rather than by branching off it, which is the house
// rule: never stack on unmerged work, reproduce the hunk instead. Verified by
// `git hash-object` at the time of writing --
//
//     Biquad.h     c7d278b4bb46cdd678e5c1bd2e9e42d40779145f
//     Design.h     001300c1407e75c8a8eab3b7dc3963298051e953
//     Design.cpp   26d5b12e17449fcaccb26495b5ee4370f926afcf
//     Prototype.h  ee680331ce5eb8ef17eaa86c6c8d003afd6a9e04
//     Svf.h        8680157d75e90f1b550d98a47c0b825962dd525e
//
// -- so whichever branch lands first, the others reproduce rather than
// conflict. **Nothing in those five files was edited.** Everything BMO Linger
// needed on top of them is in this file, which is the "separate block" half of
// the same rule.
#include "core/dsp/Design.h"

#include <array>
#include <cmath>

namespace bmo::reverb
{

//==============================================================================
/** Which of the two outer nodes are cuts.

    The parameter's four positions, in its order and with its indices --
    `params.h`'s `EqFilterChoice` and `kEqFilterNames` are the same four, and
    `eqFilterFor` below is the one place a detent becomes one of these. Both
    have to stay in step, and the test that walks all four is what holds them
    there.

    It was a `bool` until 2026-09-22, and the enum is a *widening* rather than
    a replacement: `off` is what `false` did and `bandpass` is what `true` did,
    exactly, so every claim the old mode made still reads. No implicit
    conversion is offered, deliberately -- a `bool` overload here would let
    `eqShapeOf (node, true)` keep compiling while silently meaning `bandpass`,
    and a mode with four positions should not have a second spelling for one of
    them. */
enum class EqFilter { off = 0, loCut, hiCut, bandpass };

inline constexpr int kNumEqFilters = 4;

/** The detent as the mode it names.

    **The one place an `eqfilter` detent becomes an `EqFilter`**, which is what
    keeps this list and `params.h`'s `EqFilterChoice` from drifting apart: they
    are tied together through this function and asserted through it, rather
    than by two lists agreeing on paper.

    Here rather than beside `typeFor` and `erModeFor` in `ReverbDsp.h`, which
    is where the module's other two detent conversions live: the panel needs
    this one too -- it designs the same filters the engine does, off the same
    struct -- and the panel does not include the engine's adapter.

    Out of range is Off, like its two neighbours' fallbacks, because an EQ that
    cannot read its own mode should be the identity rather than a cut nobody
    asked for. */
inline constexpr EqFilter eqFilterFor (int index) noexcept
{
    return index >= 0 && index < kNumEqFilters ? (EqFilter) index : EqFilter::off;
}

/** Whether node 1 is a cut in this mode. */
inline constexpr bool eqCutsLow (EqFilter f) noexcept
{
    return f == EqFilter::loCut || f == EqFilter::bandpass;
}

/** Whether node 3 is a cut in this mode. */
inline constexpr bool eqCutsHigh (EqFilter f) noexcept
{
    return f == EqFilter::hiCut || f == EqFilter::bandpass;
}

//==============================================================================
/** The Reverb EQ's three nodes, as the filters they actually are.

    **This is the one place the EQ's response is computed**, and both readers
    go through it: the panel's EQ screen draws this, and the engine will run
    this. 11 section 5 names sketch/DSP drift as the display's one real risk
    and `TapTables.h` is how the ER picture answers it -- this is the same
    answer for the EQ picture. A curve drawn from a hand-rolled first-order
    approximation beside an engine running a matched-Z biquad is two claims
    about one control, and only one of them is audible.

    That is a change of substance and not only of structure. The screen drew
    the shelves as `g / (1 + (f/f0)^2)` and the cut as a one-pole until
    2026-09-21, marked in `ReverbPanel`'s class comment as "not claimed to be
    the shipped filter" because there was no shipped filter to claim. There is
    one now.

    ## What a node is, and what it is not

    Node 1 is a low shelf, node 2 a bell, node 3 a high shelf, **fixed** --
    there is no shape selector and `params.h` carries why (a choice list's
    count cannot be revised after ship without remapping automation).

    `filter` turns one or both of the outer nodes into a cut -- see `EqFilter`,
    which was a bool until 2026-09-22. It is the only thing that changes a
    shape, it never reaches more than two of the three, and **node 2 is a bell
    in every position**. FREQ and Q reach the design unchanged whichever shape
    a node is in; GAIN does not, because `dsp::hasGain` is false for a cut and
    the prototype has nowhere to put it -- see `gainReaching`.

    ## IN HI-CUT is not one of these three

    The module has **two** high cuts and they are different controls in
    different places:

    - `inhicut`, the **input** high-cut, ahead of the EQ and ahead of both
      generators, on top of a fixed 20 Hz high-pass. It darkens what the room
      is given.
    - node 3 with `filter` on, the **reverb** high cut, one of the three nodes
      here. It darkens the room itself.

    Only the second is in this file. The screen draws `inhicut` as a fourth
    marked node on the same curve because the two are in series on the way in
    and a user reads one picture, but the arithmetic for it is the screen's and
    not this header's: it is a plain one-pole with no Q and no gain, and giving
    it a `Biquad` here would imply a design decision nobody has made.
*/
struct EqSettings
{
    EqFilter filter  = EqFilter::off;

    float loFreqHz   = 200.0f;
    float loDb       = 0.0f;
    float loQ        = 0.71f;

    float midFreqHz  = 1000.0f;
    float midDb      = 0.0f;
    float midQ       = 0.71f;

    float hiFreqHz   = 1600.0f;
    float hiDb       = 0.0f;
    float hiQ        = 0.71f;
};

/** The three, in the order they are designed, drawn and marked. */
enum class EqNode { low = 0, mid, high };

inline constexpr int kNumEqNodes = 3;

/** The rate the EQ is drawn at before a host has said otherwise.

    BMO DEQ's `ResponseView::kDesignRate` and its argument: a matched-Z design
    is rate-dependent by construction, so a curve drawn at a fixed 48 k while
    the module runs at 96 k is up to a dB out in the top octave. The panel
    polls `ModuleContext::sampleRate` and falls back to this. */
inline constexpr double kEqDesignRate = 48000.0;

/** What shape a node takes in a given mode. The **only** branch `filter`
    causes: node 2 is absent from it because a bell is a bell in all four.

    The two outer nodes read their own half of the mode -- `eqCutsLow` and
    `eqCutsHigh` -- so Lo Cut and Hi Cut are one rule each rather than a table
    of four rows that could disagree with itself about Bandpass. */
inline constexpr dsp::Shape eqShapeOf (EqNode node, EqFilter filter) noexcept
{
    switch (node)
    {
        case EqNode::low:  return eqCutsLow  (filter) ? dsp::Shape::lowCut  : dsp::Shape::lowShelf;
        case EqNode::high: return eqCutsHigh (filter) ? dsp::Shape::highCut : dsp::Shape::highShelf;
        case EqNode::mid:  break;
    }

    return dsp::Shape::bell;
}

inline constexpr float eqFreqOf (const EqSettings& s, EqNode node) noexcept
{
    switch (node)
    {
        case EqNode::low:  return s.loFreqHz;
        case EqNode::mid:  return s.midFreqHz;
        case EqNode::high: return s.hiFreqHz;
    }

    return 1000.0f;
}

inline constexpr float eqQOf (const EqSettings& s, EqNode node) noexcept
{
    switch (node)
    {
        case EqNode::low:  return s.loQ;
        case EqNode::mid:  return s.midQ;
        case EqNode::high: return s.hiQ;
    }

    return 0.71f;
}

/** The node's GAIN knob, whatever the mode -- what the parameter holds. */
inline constexpr float eqKnobGainDbOf (const EqSettings& s, EqNode node) noexcept
{
    switch (node)
    {
        case EqNode::low:  return s.loDb;
        case EqNode::mid:  return s.midDb;
        case EqNode::high: return s.hiDb;
    }

    return 0.0f;
}

/** The gain that actually reaches the design: the knob, or **zero for a cut**.

    This is the whole of what greying out a GAIN knob means in the DSP, and it
    is here rather than in the panel so that the picture and the sound cannot
    disagree about it. The parameter itself is never written, so a trip through
    a cut position and back restores the shelf gain the user had -- a mode must
    not eat an edit. With four positions that matters more than it did with
    two: Lo Cut withholds node 1's gain while node 3 goes on using its own. */
inline constexpr float eqGainReachingDesign (const EqSettings& s, EqNode node) noexcept
{
    return dsp::hasGain (eqShapeOf (node, s.filter)) ? eqKnobGainDbOf (s, node) : 0.0f;
}

/** Whether a node's GAIN does anything in this mode -- false for an outer node
    the mode has made a cut, which is what the panel greys out on. */
inline constexpr bool eqNodeHasGain (EqNode node, EqFilter filter) noexcept
{
    return dsp::hasGain (eqShapeOf (node, filter));
}

//==============================================================================
/** The three designed filters, and their product.

    Serial, so the magnitudes multiply and the dB add. The nodes are designed
    rather than approximated, which is what makes `magnitudeDbAt` an assertion
    a test can pin absolutes to: at the defaults every node is `designMatched`'s
    exact unity case -- numerator equal to denominator -- so the sum is
    **0.0 dB and not nearly zero**. `tests/dsp/ReverbDspTests.cpp` asserts it
    that way, which is `tests/dsp/OptoDspTests.cpp`'s house rule: a relative
    test there passed for a whole release while both of the things it compared
    were broken. */
struct EqNodes
{
    std::array<dsp::Biquad, kNumEqNodes> node {};

    static EqNodes design (const EqSettings& s, const dsp::DesignGrid& grid) noexcept
    {
        EqNodes out;

        for (int i = 0; i < kNumEqNodes; ++i)
        {
            const auto which = (EqNode) i;

            out.node[(size_t) i] = dsp::designMatched (eqShapeOf (which, s.filter),
                                                       (double) eqFreqOf (s, which),
                                                       (double) eqQOf (s, which),
                                                       (double) eqGainReachingDesign (s, which),
                                                       grid);
        }

        return out;
    }

    /** The same, building a grid for the call. For tests and for a panel that
        has no engine to borrow one from; an engine holds a grid. */
    static EqNodes design (const EqSettings& s, double sampleRate) noexcept
    {
        return design (s, dsp::DesignGrid::make (sampleRate));
    }

    /** One node's own contribution, in dB. */
    double nodeDbAt (EqNode which, double hz, double sampleRate) const noexcept
    {
        return node[(size_t) which].magnitudeDbAt (hz, sampleRate);
    }

    /** The three in series, in dB. */
    double magnitudeDbAt (double hz, double sampleRate) const noexcept
    {
        auto db = 0.0;

        for (const auto& n : node)
            db += n.magnitudeDbAt (hz, sampleRate);

        return db;
    }

    bool isStable() const noexcept
    {
        for (const auto& n : node)
            if (! (n.isStable() && n.isFinite()))
                return false;

        return true;
    }
};

} // namespace bmo::reverb
