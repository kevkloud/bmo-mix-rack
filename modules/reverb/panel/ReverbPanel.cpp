#include "ReverbPanel.h"
// The screen's ramp width and the right-hand edge of its TAIL axis are the
// engine's own constants, not copies of them -- so the display and the DSP
// cannot come to disagree about where a tap arrives or how long a tail is
// allowed to be.
#include "modules/reverb/dsp/DspCore.h"
#include "modules/reverb/params.h"

#include <algorithm>
#include <cmath>

namespace bmo::reverb
{

namespace
{
    //== The grid ==============================================================

    /** Three columns, and the knob grid is laid out against them: the
        persistent row, the page cluster and the page keys. It was four until
        the 2026-09-21 control-set trim -- `ReverbPanel`'s class comment
        carries why three works now and did not before.

        The cell is `(380 - 2 * kPad) / 3 = 120` px, which is **exactly** what
        the cell was at four columns and 500, so no caption measured against
        the old grid can fail on the new one. */
    constexpr int kCols = 3;

    /** The foot is the one row that is not three across, because it holds four
        controls: ER, REVERB, MIX and TYPE in the corner the grille left. Three
        cells there would leave TYPE alone in a row, which is the fault this
        panel will not commit.

        **And its four cells are not equal.** Four equal cells at 380 are 90 px
        and the TYPE box is 12 px narrower than its cell, which leaves 78 for a
        list whose longest item, "Ambience", needs 90 -- it overflowed by
        11.7 px, measured. So TYPE keeps **a whole grid cell**, 120 px, which
        is the box it had in the persistent row and is known to fit, and the
        three levels divide the 240 that are left. A knob is 68 px wide and its
        captions are ER, REVERB and MIX, so 80 px a piece is comfortable where
        120 was generous. */
    constexpr int kFootLevels = 3;

    //== The bezel and its screen ==============================================

    /** How much bezel there is around the screen on every side. The recess is
        `noticeably larger` than what it holds, which is most of what makes it
        read as a bezel rather than as a border: 12 px of well on all four
        sides, plus the readout line's own row underneath. */
    constexpr int kBezelPad       = 12;
    constexpr int kScreenHeight   = 170;
    constexpr int kScreenToLine   = 4;

    /** The line of small printed text under the screen. It carries a reading
        for the page that is showing -- see `LingerScreen::readout`. */
    constexpr int   kReadoutRow  = 15;
    constexpr float kReadoutSize = 11.0f;

    constexpr int kBezelHeight = kBezelPad * 2 + kScreenHeight + kScreenToLine + kReadoutRow;

    //== The rows ==============================================================

    /** The page keys. Their own height is the circle plus the air under it
        plus the caption row, and `PageButton` lays out against exactly that,
        so the two cannot come apart. */
    constexpr int kPageRow = PageButton::kDotSide + 3 + PageButton::kCaptionRow;

    /** The persistent row and the level strip: the same size of knob and the
        same caption, because they are the same kind of control -- the ones
        that are there whatever page you have turned to.

        12 pt rather than the suite's 15. "PRE-DELAY" is the longest caption on
        this panel outside the cluster and a 110 px cell will not take it at
        15; two caption sizes on one face is BMO DEQ's precedent, its band row
        and its detector row being set differently, and `ui_layout_tests`
        measures the overflow rather than trusting this. */
    constexpr int   kMainKnobSide    = 68;
    constexpr float kMainCaptionSize = 12.0f;
    constexpr int   kMainRow         = 100;

    /** The cluster. Smaller knobs and a smaller caption, because there are up
        to six of them in two rows and the captions are the panel's longest
        -- "EQ HIGH FREQ" is twelve characters. */
    constexpr int   kClusterKnobSide    = 54;
    constexpr float kClusterCaptionSize = 10.0f;
    constexpr int   kClusterRow         = 74;
    constexpr int   kClusterRows        = 2;

    constexpr int kStripRow = 100;

    /** Everything above, added up, so the gap between the blocks is whatever
        is left over divided evenly rather than a number somebody has to redo
        when a block's height changes. */
    constexpr int kContentHeight = kBezelHeight + kPageRow + kMainRow
                                 + kClusterRow * kClusterRows
                                 + ui::ModulePanel::kRuleRow + kStripRow;

    // `kSwitchWidth` and `kSwitchHeight` were here for LINK ER, which was the
    // only switch this panel ever had and went with `prelink` in the
    // 2026-09-21 trim. Every control on this face is now a knob or a dropdown,
    // which is why `place` below has nothing to special-case.

    //== The screen's own arithmetic ===========================================

    /** How many infill pulses the density bridge has to spend, over and above
        the 21 core taps. 48 taps at the top of the knob, 10 section 3. */
    constexpr int kMaxInfill = 48 - kNumReferenceTaps;

    /** `DspCore::kRampWidth`, read through the DSP's own constant so the
        picture's ramp and the engine's cannot disagree about where a tap
        arrives. */
    constexpr float kRampWidth = DspCore::kRampWidth;

    /** Infill tap `i`'s activation threshold, theta_k.

        Spread over the *open* interval (0, 1) rather than over (0, 1]: with
        the last threshold at exactly 1 its weight -- clamp((D - theta) /
        Delta, 0, 1) -- is zero at the top of the knob, so the forty-eighth tap
        would never arrive at all. One more division than there are taps puts
        the last one at 27/28, inside the ramp at D = 1 and so fully on.

        The 21 core taps keep theta = 0 and are unaffected: they never switch
        off, which is what keeps the renormalising denominator bounded away
        from zero and the whole sweep continuous (10 section 3). */
    constexpr float infillThreshold (int i) noexcept
    {
        return (float) (i + 1) / (float) (kMaxInfill + 1);
    }

    /** The bloom a type's ATTACK constant selects, in milliseconds. Linear,
        because the contour is CALIBRATE (10 section 8) and a curve here would
        be a guess drawn as a fact.

        **This is the only place the bloom is printed now.** ATTACK lost its
        knob and its value string in the 2026-09-21 trim, so the readout line
        under the TAIL picture is where a user finds out what the selected type
        does to the onset. */
    constexpr float bloomMs (float percent) noexcept { return percent * 1.2f; }

    /** The right-hand edge of the TAIL page's time axis, in milliseconds. */
    constexpr float kMaxMs = LingerScreen::kMaxSeconds * 1000.0f;

    /** A frequency for the readout line, ASCII and short: "200 HZ",
        "1.60 KHZ". Three of them have to fit one line under the screen. */
    juce::String hzText (float hz)
    {
        return hz >= 1000.0f ? juce::String (hz / 1000.0f, 2) + " KHZ"
                             : juce::String (juce::roundToInt (hz)) + " HZ";
    }

    /** A round step for a linear time axis: the first of 1, 2, 5, 10 ... that
        leaves no more than eight divisions across `span`. Chosen rather than
        fixed, because the EARLY window scales with SIZE over a 160:1 range and
        a fixed step would be one mark at the small end and four hundred at the
        large one. */
    float niceStepMs (float span) noexcept
    {
        static constexpr float steps[] { 1.0f, 2.0f, 5.0f, 10.0f, 20.0f, 50.0f,
                                         100.0f, 200.0f, 500.0f, 1000.0f, 2000.0f };
        auto step = steps[0];

        for (const auto s : steps)
        {
            step = s;

            if (span / s <= 8.0f)
                break;
        }

        return step;
    }
}

//==============================================================================
LingerScreen::LingerScreen (juce::Colour accentColour) : accent (accentColour)
{
    setName ("DISPLAY");
    setInterceptsMouseClicks (false, false);
}

void LingerScreen::setState (const State& s)
{
    const auto same = juce::approximatelyEqual (s.sizeM, state.sizeM)
                   && juce::approximatelyEqual (s.preDelayMs, state.preDelayMs)
                   && juce::approximatelyEqual (s.erDensity, state.erDensity)
                   && juce::approximatelyEqual (s.erLevelDb, state.erLevelDb)
                   && juce::approximatelyEqual (s.decaySeconds, state.decaySeconds)
                   && juce::approximatelyEqual (s.dampLo, state.dampLo)
                   && juce::approximatelyEqual (s.dampHi, state.dampHi)
                   && juce::approximatelyEqual (s.attack, state.attack)
                   && juce::approximatelyEqual (s.verbLevelDb, state.verbLevelDb)
                   && juce::approximatelyEqual (s.eqLoFreqHz, state.eqLoFreqHz)
                   && juce::approximatelyEqual (s.eqLoDb, state.eqLoDb)
                   && juce::approximatelyEqual (s.eqHiFreqHz, state.eqHiFreqHz)
                   && juce::approximatelyEqual (s.eqHiDb, state.eqHiDb)
                   && juce::approximatelyEqual (s.inHiCutHz, state.inHiCutHz);

    if (same)
        return;

    state = s;
    repaint();
}

void LingerScreen::setPage (Page p)
{
    if (p == page)
        return;

    page = p;
    repaint();
}

float LingerScreen::firstTapTimeMs() const noexcept
{
    // Through the table's own function, not through a copy of its arithmetic:
    // a check that re-derived this its own way could agree with the bug it
    // exists to catch. `PlainKnob::captionOverflow` is written under the same
    // discipline.
    //
    // **No pre-delay term any more.** `kPreLinkFixed` is false, so the ER
    // always travel with dry and the taps never move with PRE-DELAY. The
    // static_assert is what keeps that true: flip the constant and this stops
    // compiling rather than quietly drawing the wrong picture.
    static_assert (! kPreLinkFixed, "the ER stems are drawn unshifted by PRE-DELAY");

    return tapTimeMsAt (kReferenceTaps[0], state.sizeM);
}

float LingerScreen::lastTapTimeMs() const noexcept
{
    return erSpanMsAt (state.sizeM);
}

float LingerScreen::erWindowMs() const noexcept
{
    return juce::jmax (10.0f, lastTapTimeMs() * 1.1f);
}

float LingerScreen::tailEndSeconds() const noexcept
{
    const auto slowest = std::max (1.0f, std::max (state.dampLo, state.dampHi));
    return state.preDelayMs * 0.001f + state.decaySeconds * slowest;
}

int LingerScreen::activeTapCount() const noexcept
{
    // **The 21 core taps never switch off.** That is not a simplification: it
    // is what keeps the renormalising denominator bounded away from zero, and
    // so what makes the whole density sweep continuous and click-free
    // (10 section 3). DENSITY spends infill on top of them.
    const auto d = juce::jlimit (0.0f, 1.0f, state.erDensity * 0.01f);

    int infill = 0;

    for (int i = 0; i < kMaxInfill; ++i)
        if ((d - infillThreshold (i)) / kRampWidth > 0.0f)
            ++infill;

    return kNumReferenceTaps + infill;
}

float LingerScreen::responseDbAt (float hz) const noexcept
{
    // Three nodes in series, summed in dB, and **first-order** -- see the
    // class comment for why that is a decision rather than a shortcut. Each
    // one is written as its own limit rather than as a filter: the shelves go
    // to their full gain on one side of the corner and to nothing on the
    // other, and the cut is a one-pole roll-off.
    const auto f = juce::jmax (1.0f, hz);

    const auto lo = f / juce::jmax (1.0f, state.eqLoFreqHz);
    const auto hi = f / juce::jmax (1.0f, state.eqHiFreqHz);
    const auto cut = f / juce::jmax (1.0f, state.inHiCutHz);

    const auto lowShelf  = state.eqLoDb / (1.0f + lo * lo);
    const auto highShelf = state.eqHiDb * (hi * hi) / (1.0f + hi * hi);
    const auto hiCut     = -10.0f * std::log10 (1.0f + cut * cut);

    return lowShelf + highShelf + hiCut;
}

std::array<float, 3> LingerScreen::nodeFrequencies() const noexcept
{
    return { state.eqLoFreqHz, state.eqHiFreqHz, state.inHiCutHz };
}

juce::String LingerScreen::readout() const
{
    switch (page)
    {
        case Page::early:
            // The tap count and the ER window, which are the two numbers the
            // EARLY picture is of and neither of which is on a knob: DENSITY
            // is a per cent and SIZE is metres.
            return juce::String (activeTapCount()) + " TAPS   "
                     + juce::String (firstTapTimeMs(), 1) + "-"
                     + juce::String (lastTapTimeMs(), 1) + " MS";

        case Page::tail:
            // The decay itself, the bloom in the milliseconds ATTACK's per
            // cent selects, and where the drawn tail actually ends -- which is
            // DECAY times the slowest damping multiplier and is the figure the
            // knob cannot show.
            return "DECAY " + juce::String (state.decaySeconds, 2) + " S   BLOOM "
                     + juce::String (juce::roundToInt (bloomMs (state.attack))) + " MS   TAIL "
                     + juce::String (tailEndSeconds(), 2) + " S";

        case Page::tone:
            // The three crossover points, in the order the curve crosses them.
            return "LOW " + hzText (state.eqLoFreqHz) + "   HIGH " + hzText (state.eqHiFreqHz)
                     + "   CUT " + hzText (state.inHiCutHz);
    }

    return {};
}

//==============================================================================
void LingerScreen::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();
    const auto plot = bounds.reduced (5.0f);

    // A dark face, in `meterFace` -- the token a needle meter's scale is
    // printed on. A value rather than a hue, and the same one in both
    // appearances, because a screen that went pale in the light theme would
    // stop reading as a screen.
    g.setColour (ui::tokens().meterFace);
    g.fillRoundedRectangle (bounds, ui::Tokens::corner);

    const auto ink = ui::accentInk (accent, ui::tokens().meterFace);

    // The dot matrix, under everything: a faint regular grid, which is what
    // makes the box read as a display rather than as a hole in the plate. It
    // is drawn from the plot's own origin so the dots do not crawl when the
    // panel is laid out again.
    {
        g.setColour (ui::tokens().hairline.withMultipliedAlpha (0.22f));

        for (auto y = plot.getY() + 3.0f; y < plot.getBottom(); y += 8.0f)
            for (auto x = plot.getX() + 3.0f; x < plot.getRight(); x += 8.0f)
                g.fillRect (juce::Rectangle<float> (x, y, 1.0f, 1.0f));
    }

    switch (page)
    {
        case Page::early: paintEarly (g, plot, ink); break;
        case Page::tail:  paintTail  (g, plot, ink); break;
        case Page::tone:  paintTone  (g, plot, ink); break;
    }

    g.setColour (ui::tokens().outline);
    g.drawRoundedRectangle (bounds.reduced (0.5f), ui::Tokens::corner, ui::Tokens::hairlineWeight);
}

//==============================================================================
void LingerScreen::paintEarly (juce::Graphics& g, juce::Rectangle<float> plot,
                               juce::Colour ink) const
{
    // **Discrete stems from a baseline, spanning the real ER window.** The
    // mirrored envelope this replaced bloomed and closed to a point and left
    // most of the box empty; a tap is an event at a time with a level, and
    // that is a stem.
    const auto base   = plot.getBottom();
    const auto top    = plot.getY() + 2.0f;
    const auto window = erWindowMs();

    const auto xFor = [&] (float ms)
    {
        return plot.getX() + plot.getWidth() * juce::jlimit (0.0f, 1.0f, ms / window);
    };

    /** A level in dB as a stem height in pixels, measured up from the floor --
        against `kTapFloorDb`, the ER fader's own bottom, and not against the
        tail's -72. See the constant for why. */
    const auto heightFor = [&] (float db)
    {
        const auto n = juce::jlimit (0.0f, 1.0f, (db - kTapFloorDb) / (0.0f - kTapFloorDb));
        return (base - top) * n;
    };

    // The time marks, at a round step chosen for the window rather than fixed:
    // SIZE moves the window over 160:1 and no single step survives that.
    {
        g.setColour (ui::tokens().hairline.withMultipliedAlpha (0.5f));

        for (auto ms = niceStepMs (window); ms < window; ms += niceStepMs (window))
            g.fillRect (juce::Rectangle<float> (xFor (ms), plot.getY(),
                                                ui::Tokens::hairlineWeight, plot.getHeight()));
    }

    // The floor the stems stand on.
    g.setColour (ui::tokens().hairline.withMultipliedAlpha (0.85f));
    g.fillRect (juce::Rectangle<float> (plot.getX(), base, plot.getWidth(),
                                        ui::Tokens::hairlineWeight));

    // The direct sound, at t = 0, full height and hard against the left edge.
    // On this page that is honest rather than a licence: the axis is linear
    // and starts at zero, so the left edge *is* t = 0. It is the reference
    // every arrival here is measured from, and leaving it out would make the
    // first reflection look like the beginning of the sound.
    g.setColour (ink.withAlpha (0.85f));
    g.fillRect (juce::Rectangle<float> (plot.getX(), top, 2.0f, base - top));

    // ER at "Off" draws no taps at all, because "Off" is silence and not
    // -40 dB.
    if (state.erLevelDb <= -39.95f)
        return;

    const auto drawTap = [&] (float ms, float gain, float alpha)
    {
        if (gain <= 0.0f)
            return;

        const auto h = heightFor (20.0f * std::log10 (gain) + state.erLevelDb);

        if (h <= 0.0f)
            return;

        g.setColour (ink.withAlpha (alpha));
        g.fillRect (juce::Rectangle<float> (xFor (ms), base - h, 2.0f, h));
    };

    // No shift: `kPreLinkFixed` is false, so the ER sit where the table puts
    // them whatever PRE-DELAY reads. See `firstTapTimeMs`.

    // The core taps, always on, at full strength.
    for (const auto& tap : kReferenceTaps)
        drawTap (tapTimeMsAt (tap, state.sizeM),
                 tapGainAt (tap, state.sizeM), 0.92f);

    // The infill DENSITY spends, drawn fainter because it is the part of the
    // picture the generator has not been written for yet. Times are one per
    // equal window with a deterministic nudge -- grid-based with jitter, which
    // 10 section 3 says sounds smoother than fully random placement at the
    // same density -- and gains come from the same 1/t envelope evaluated at
    // their own times, so the contour is identical at every density. **The
    // energy renormalisation is not drawn**: it holds total ER energy constant
    // to within 0.2 dB across the sweep, which is a property of the sum rather
    // than of any one stem here.
    {
        const auto d     = juce::jlimit (0.0f, 1.0f, state.erDensity * 0.01f);
        const auto first = tapTimeMsAt (kReferenceTaps[0], state.sizeM);
        const auto last  = erSpanMsAt (state.sizeM);
        const auto span  = juce::jmax (1.0f, last - first);

        for (int i = 0; i < kMaxInfill; ++i)
        {
            const auto w = juce::jlimit (0.0f, 1.0f, (d - infillThreshold (i)) / kRampWidth);

            if (w <= 0.0f)
                continue;

            // A fixed integer hash for the jitter, so the picture is the same
            // every time it is drawn and the same on both machines.
            const auto jitter = 1.0f + 0.03f * (float) (((i * 37) % 7) - 3) / 3.0f;
            const auto ms = first + span * ((float) i + 0.5f) / (float) kMaxInfill * jitter;
            const auto gain = tapGainAt (kReferenceTaps[0], state.sizeM) * first
                                / juce::jmax (1.0f, ms);

            drawTap (ms, gain, 0.26f + 0.34f * w);
        }
    }
}

//==============================================================================
void LingerScreen::paintTail (juce::Graphics& g, juce::Rectangle<float> plot,
                              juce::Colour ink) const
{
    const auto xFor = [&] (float ms)
    {
        const auto n = std::log (juce::jmax (kMinMs, ms) / kMinMs) / std::log (kMaxMs / kMinMs);
        return plot.getX() + plot.getWidth() * juce::jlimit (0.0f, 1.0f, (float) n);
    };

    const auto yFor = [&] (float db)
    {
        const auto n = juce::jlimit (0.0f, 1.0f, (db - kFloorDb) / (0.0f - kFloorDb));
        return plot.getBottom() - plot.getHeight() * n;
    };

    // Decade marks -- 10, 100 ms, 1 s, 10 s -- because a logarithmic axis with
    // nothing on it reads as a linear one that has gone wrong.
    {
        g.setColour (ui::tokens().hairline.withMultipliedAlpha (0.5f));

        for (auto ms = 10.0f; ms < kMaxMs; ms *= 10.0f)
            g.fillRect (juce::Rectangle<float> (xFor (ms), plot.getY(),
                                                ui::Tokens::hairlineWeight, plot.getHeight()));
    }

    // -60 dB, the line the decay time is quoted against, and the floor. Both
    // hairlines, because one of them is where the answer is and the other is
    // where the box stops.
    g.setColour (ui::tokens().hairline.withMultipliedAlpha (0.65f));
    g.fillRect (juce::Rectangle<float> (plot.getX(), yFor (-60.0f), plot.getWidth(),
                                        ui::Tokens::hairlineWeight));
    g.fillRect (juce::Rectangle<float> (plot.getX(), plot.getBottom(), plot.getWidth(),
                                        ui::Tokens::hairlineWeight));

    // REVERB at "Off" draws no tail at all. The pre-delay gap is drawn by its
    // absence: nothing is between the left edge and `tailStartMs`, which is
    // what a gap is.
    if (state.verbLevelDb <= -39.95f)
        return;

    const auto bloom = bloomMs (state.attack);

    /** The envelope at `ms`, for a 60 dB time of `t60` seconds. Below the
        bloom it rises; after it, it decays. */
    const auto envelopeDb = [&] (float ms, float t60)
    {
        const auto since = ms - state.preDelayMs;

        if (since <= 0.0f)
            return kFloorDb;

        const auto rise = bloom > 0.0f && since < bloom
                            ? 20.0f * std::log10 (juce::jmax (1.0e-3f, since / bloom))
                            : 0.0f;

        const auto decayFrom = bloom > 0.0f ? juce::jmax (0.0f, since - bloom) : since;

        return state.verbLevelDb + rise - 60.0f * (decayFrom * 0.001f) / juce::jmax (0.01f, t60);
    };

    const auto x0    = xFor (juce::jmax (kMinMs, state.preDelayMs));
    const auto steps = juce::jmax (8, (int) plot.getWidth());

    const auto xAt = [&] (int i)
    {
        return x0 + (plot.getRight() - x0) * (float) i / (float) steps;
    };

    /** The envelope across the drawn span, one y per step, for a 60 dB time of
        `t60` seconds. Computed once and reused for the outline and the fill,
        so the two cannot disagree about where the envelope is. */
    const auto curve = [&] (float t60)
    {
        std::vector<float> out ((size_t) steps + 1);

        for (int i = 0; i <= steps; ++i)
        {
            // Back out of the axis to the time this pixel stands for.
            const auto axis = (xAt (i) - plot.getX()) / juce::jmax (1.0f, plot.getWidth());
            const auto ms = kMinMs * std::pow (kMaxMs / kMinMs, axis);

            out[(size_t) i] = yFor (envelopeDb (ms, t60));
        }

        return out;
    };

    const auto line = [&] (const std::vector<float>& ys)
    {
        juce::Path p;
        p.startNewSubPath (xAt (0), ys[0]);

        for (int i = 1; i <= steps; ++i)
            p.lineTo (xAt (i), ys[(size_t) i]);

        return p;
    };

    const auto mid  = state.decaySeconds;
    const auto fast = state.decaySeconds * std::min (state.dampLo, state.dampHi);
    const auto slow = state.decaySeconds * std::max (state.dampLo, state.dampHi);

    // The band between the fastest and slowest of the three decay times: the
    // damping multipliers made visible, which is the one thing about them a
    // number on a knob does not say. Filled from the slowest envelope down to
    // the floor, with the fastest outlined inside it.
    {
        const auto slowYs = curve (slow);
        const auto fastYs = curve (fast);

        auto region = line (slowYs);
        region.lineTo (xAt (steps), plot.getBottom());
        region.lineTo (xAt (0), plot.getBottom());
        region.closeSubPath();

        g.setColour (ink.withAlpha (0.16f));
        g.fillPath (region);

        g.setColour (ink.withAlpha (0.35f));
        g.strokePath (line (fastYs), juce::PathStrokeType (1.0f));
    }

    // And the mid band itself, which is what DECAY says.
    g.setColour (ink);
    g.strokePath (line (curve (mid)), juce::PathStrokeType (1.6f));
}

//==============================================================================
void LingerScreen::paintTone (juce::Graphics& g, juce::Rectangle<float> plot,
                              juce::Colour ink) const
{
    const auto xFor = [&] (float hz)
    {
        const auto n = std::log (juce::jlimit (kMinHz, kMaxHz, hz) / kMinHz)
                         / std::log (kMaxHz / kMinHz);
        return plot.getX() + plot.getWidth() * (float) n;
    };

    const auto yFor = [&] (float db)
    {
        const auto n = juce::jlimit (-1.0f, 1.0f, db / kToneRangeDb);
        return plot.getCentreY() - plot.getHeight() * 0.5f * n;
    };

    // The decades, and the line a flat response sits on.
    {
        g.setColour (ui::tokens().hairline.withMultipliedAlpha (0.5f));

        for (auto hz = 100.0f; hz < kMaxHz; hz *= 10.0f)
            g.fillRect (juce::Rectangle<float> (xFor (hz), plot.getY(),
                                                ui::Tokens::hairlineWeight, plot.getHeight()));

        g.setColour (ui::tokens().hairline.withMultipliedAlpha (0.8f));
        g.fillRect (juce::Rectangle<float> (plot.getX(), yFor (0.0f), plot.getWidth(),
                                            ui::Tokens::hairlineWeight));
    }

    // The summed curve, one sample a pixel.
    {
        const auto steps = juce::jmax (8, (int) plot.getWidth());

        juce::Path p;

        for (int i = 0; i <= steps; ++i)
        {
            const auto n = (float) i / (float) steps;
            const auto hz = kMinHz * std::pow (kMaxHz / kMinHz, n);
            const auto x = plot.getX() + plot.getWidth() * n;
            const auto y = yFor (responseDbAt (hz));

            if (i == 0)
                p.startNewSubPath (x, y);
            else
                p.lineTo (x, y);
        }

        g.setColour (ink);
        g.strokePath (p, juce::PathStrokeType (1.6f));
    }

    // **A marked node per band**, sitting on the summed curve rather than on
    // its own contribution: what a serial EQ's node says is "this control's
    // corner is here, and here is what the chain is doing at that corner".
    {
        constexpr auto radius = 3.4f;

        for (const auto hz : nodeFrequencies())
        {
            const auto centre = juce::Point<float> (xFor (hz), yFor (responseDbAt (hz)));
            const auto dot = juce::Rectangle<float> (radius * 2.0f, radius * 2.0f)
                                 .withCentre (centre);

            g.setColour (ui::tokens().meterFace);
            g.fillEllipse (dot.expanded (1.6f));
            g.setColour (ink);
            g.drawEllipse (dot, 1.6f);
        }
    }
}

//==============================================================================
PageButton::PageButton (const juce::String& name, juce::Colour accent)
    : juce::Button (name), accentColour (accent)
{
    // The panel owns which of the three is lit -- they are a radio set and not
    // three toggles -- so a click asks for a page rather than flipping a
    // state. BMO Opto's meter row is the precedent and the reason: host
    // automation and a click should land in the same place.
    setClickingTogglesState (false);
}

juce::Rectangle<int> PageButton::dotBounds() const
{
    const auto area = getLocalBounds().withTrimmedBottom (kCaptionRow);
    const auto side = juce::jmin (area.getWidth(), area.getHeight(), kDotSide);

    return area.withSizeKeepingCentre (side, side);
}

juce::Rectangle<int> PageButton::captionBox() const
{
    return { 0, dotBounds().getBottom(), getWidth(), kCaptionRow - 4 };
}

float PageButton::labelOverflow() const
{
    return juce::GlyphArrangement::getStringWidth (ui::captionFont (kCaptionSize), getButtonText())
             - (float) captionBox().getWidth();
}

void PageButton::setAccent (juce::Colour colour)
{
    accentColour = colour;
    repaint();
}

void PageButton::paintButton (juce::Graphics& g, bool shouldDrawHighlighted, bool shouldDrawDown)
{
    const auto on = getToggleState();
    const auto plate = ui::panelTokensFor (*this).plate;

    // The same fill logic a switch has, one shape over: the module's accent
    // when it is the page you are on, and `switchOff` -- the raised grey every
    // unlit switch in the suite is filled with -- when it is not.
    auto fill = on ? accentColour : ui::tokens().switchOff;

    if (shouldDrawDown)             fill = fill.darker (0.12f);
    else if (shouldDrawHighlighted) fill = fill.brighter (0.06f);

    const auto dot = dotBounds().toFloat();

    g.setColour (fill);
    g.fillEllipse (dot);
    g.setColour (ui::tokens().outline);
    g.drawEllipse (dot.reduced (0.6f), ui::Tokens::hairlineWeight);

    // The name under the key, in the module's own ink when its page is
    // showing and in the secondary grey when it is not -- so the lit key and
    // its name say the same thing twice rather than once.
    ui::drawLabel (g, getButtonText(), captionBox().toFloat(), juce::Justification::centred,
                   ui::captionFont (kCaptionSize),
                   on ? ui::accentInk (accentColour, plate) : ui::tokens().text2);
}

//==============================================================================
ReverbPanel::ReverbPanel (ui::ModuleContext ctx)
    : ModulePanel (std::move (ctx)),
      screen (context.def.accent),

      // **One colour for the whole module.** `ui::Knob::Style` says what the
      // two faces mean, and the meaning is not "persistent row" against
      // "cluster": `utility` is the pale blue of *input, output, gain*,
      // `character` is the module's accent worn by anything that shapes the
      // sound. BMO DEQ is the precedent, and rendering the two side by side is
      // how to check it -- FREQ, GAIN and Q are its accent teal and OUTPUT
      // alone is blue.
      //
      // So every knob here is `character` but `output`, the one control in
      // this schema that is literally an output trim. The cluster's knobs were
      // `utility` as a block once, and the module rendered violet at the top
      // and suite azure below as though it were two plugins sharing a slot.
      //
      // IN HI-CUT is the near miss and stays `character`: it is a tone control
      // on the way in rather than a level. WIDTH is the other, and stays
      // `character` too -- it sets how wide the tail is made, which is BMO
      // Dimension's DIMENSION and not anybody's gain.
      sizeKnob      (context.params.param (Index::size),      "SIZE",      ui::Knob::Style::character, 0.62f, context.def.accent),
      preDelayKnob  (context.params.param (Index::predelay),  "PRE-DELAY", ui::Knob::Style::character, 0.62f, context.def.accent),
      decayKnob     (context.params.param (Index::decay),     "DECAY",     ui::Knob::Style::character, 0.62f, context.def.accent),

      // TYPE, in the corner at the foot rather than in the knob row it used to
      // open. See the class comment.
      typeBox       (context.params.param (Index::type),
                     context.params.spec  (Index::type),      "TYPE",      context.def.accent),

      // EARLY. ER MODE is the panel's other list of names, and its other
      // dropdown: Energy is not more than Taps.
      erModeBox     (context.params.param (Index::ermode),
                     context.params.spec  (Index::ermode),      "ER MODE",   context.def.accent),
      densityKnob   (context.params.param (Index::erdensity),   "DENSITY",   ui::Knob::Style::character, 0.58f, context.def.accent),
      erSpreadKnob  (context.params.param (Index::erspread),    "ER SPREAD", ui::Knob::Style::character, 0.58f, context.def.accent),
      erHiCutKnob   (context.params.param (Index::erhicut),     "ER HI-CUT", ui::Knob::Style::character, 0.58f, context.def.accent),
      variationKnob (context.params.param (Index::ervariation), "VARIATION", ui::Knob::Style::character, 0.58f, context.def.accent),
      feedKnob      (context.params.param (Index::feed),        "SOURCE",    ui::Knob::Style::character, 0.58f, context.def.accent),

      // TAIL. "LOW x" and not "LOW x" with a multiplication sign: the two
      // display faces are licensed individually and live outside this
      // repository, so a glyph outside ASCII is one this suite cannot promise
      // it can draw. The value strings follow, printing "1.20x".
      //
      // The two knees that used to sit beside these went into the per-type
      // block, which is why the captions are "LOW x" and not "LOW x FREQ" --
      // there is nothing left to disambiguate them from.
      dampLoKnob     (context.params.param (Index::damplo),     "LOW x",       ui::Knob::Style::character, 0.58f, context.def.accent),
      dampHiKnob     (context.params.param (Index::damphi),     "HIGH x",      ui::Knob::Style::character, 0.58f, context.def.accent),
      modDepthKnob   (context.params.param (Index::moddepth),   "MOD DEPTH",   ui::Knob::Style::character, 0.58f, context.def.accent),
      modRateKnob    (context.params.param (Index::modrate),    "MOD RATE",    ui::Knob::Style::character, 0.58f, context.def.accent),
      widthKnob      (context.params.param (Index::width),      "WIDTH",       ui::Knob::Style::character, 0.58f, context.def.accent),

      // TONE. OUTPUT is the one `utility` knob on this panel: a trim on the
      // way out is exactly what the style is for, and it is the same knob BMO
      // DEQ leaves blue at the foot of its own accent-coloured face.
      eqLoFreqKnob (context.params.param (Index::eqlofreq), "EQ LOW FREQ",  ui::Knob::Style::character, 0.58f, context.def.accent),
      eqLoKnob     (context.params.param (Index::eqlo),     "EQ LOW",       ui::Knob::Style::character, 0.58f, context.def.accent),
      eqHiFreqKnob (context.params.param (Index::eqhifreq), "EQ HIGH FREQ", ui::Knob::Style::character, 0.58f, context.def.accent),
      eqHiKnob     (context.params.param (Index::eqhi),     "EQ HIGH",      ui::Knob::Style::character, 0.58f, context.def.accent),
      inHiCutKnob  (context.params.param (Index::inhicut),  "IN HI-CUT",    ui::Knob::Style::character, 0.58f, context.def.accent),
      outputKnob   (context.params.param (Index::output),   "OUTPUT",       ui::Knob::Style::utility,   0.58f, context.def.accent),

      // The strip at the foot.
      erLevelKnob   (context.params.param (Index::erlevel),   "ER",     ui::Knob::Style::character, 0.62f, context.def.accent),
      verbLevelKnob (context.params.param (Index::verblevel), "REVERB", ui::Knob::Style::character, 0.62f, context.def.accent),
      mixKnob       (context.params.param (Index::mix),       "MIX",    ui::Knob::Style::character, 0.62f, context.def.accent)
{
    addAndMakeVisible (screen);

    // The three keys, in page order, so `pageButtons[(size_t) page]` is the
    // one that is lit and nothing has to map between them.
    {
        static const char* const names[] { "EARLY", "TAIL", "TONE" };
        static constexpr Page pages[] { Page::early, Page::tail, Page::tone };

        for (size_t i = 0; i < pageButtons.size(); ++i)
        {
            pageButtons[i] = std::make_unique<PageButton> (names[i], context.def.accent);
            pageButtons[i]->onClick = [this, p = pages[i]] { setPage (p); };
            addAndMakeVisible (*pageButtons[i]);
        }
    }

    // TYPE hangs its box at the foot of the square a `kMainKnobSide` knob
    // would occupy, so its caption lands on MIX's line rather than twenty
    // pixels off it -- the same trick it used beside SIZE, now that it sits in
    // the foot row instead. `ui::ChoiceBox::setControlSide` carries the
    // argument.
    typeBox.setControlSide (kMainKnobSide);
    typeBox.setCaptionSize (kMainCaptionSize);
    addAndMakeVisible (typeBox);

    for (auto* k : { &sizeKnob, &preDelayKnob, &decayKnob,
                     &erLevelKnob, &verbLevelKnob, &mixKnob })
    {
        k->setKnobSide (kMainKnobSide);
        k->setCaptionSize (kMainCaptionSize);
        addAndMakeVisible (k);
    }

    // The cluster's controls are sized here and parented in `resized`, which
    // is the only place that decides which page's are children. See the class
    // comment for why they are unparented rather than hidden.
    for (auto* c : allPageControls())
    {
        if (auto* k = dynamic_cast<ui::PlainKnob*> (c))
        {
            k->setKnobSide (kClusterKnobSide);
            k->setCaptionSize (kClusterCaptionSize);
        }
        else if (auto* b = dynamic_cast<ui::ChoiceBox*> (c))
        {
            b->setControlSide (kClusterKnobSide);
            b->setCaptionSize (kClusterCaptionSize);
        }
    }

    // **Nothing on this panel prints its value.** TYPE and ER MODE were
    // stepped knobs over a list of names, which is unreadable without a
    // printed value, so both opted in -- and their row-mates then had to
    // reserve the same blank line to stay level with them. The dropdowns print
    // their own choice inside the box, so the value line and the two blank
    // ones it forced went with them, and the panel is back to the suite's
    // default: knobs say less and more, not how much (`ui::PlainKnob`'s class
    // comment). The numbers a page cannot show are on the readout line under
    // the screen instead, which is one line rather than thirty.

    // Every parameter the screen is drawn from redraws it, and the list is
    // exactly `LingerScreen::State`'s fields -- which is the thing a reader
    // wants to be able to check at a glance.
    {
        // **TYPE is in the list and is not itself drawn.** The TAIL picture's
        // bloom is `TypeConstants::attack`, which has no parameter since the
        // 2026-09-21 trim, so a type change is the only thing that moves it --
        // and without this the bloom would only redraw when some *other* knob
        // happened to move. It is also what makes the nine the type stamps
        // redraw as one event rather than nine.
        const int drawn[] { Index::type,
                            Index::size, Index::predelay,
                            Index::erdensity, Index::erlevel,
                            Index::decay, Index::damplo, Index::damphi,
                            Index::verblevel,
                            Index::eqlofreq, Index::eqlo, Index::eqhifreq, Index::eqhi,
                            Index::inhicut };

        static_assert (sizeof (drawn) / sizeof (int) == 14, "one attachment per drawn parameter");

        for (size_t i = 0; i < screenAttachments.size(); ++i)
            screenAttachments[i] = std::make_unique<juce::ParameterAttachment> (
                context.params.param (drawn[i]),
                [this] (float) { refreshScreen(); });
    }

    pageButtons[(size_t) page]->setToggleState (true, juce::dontSendNotification);
    screen.setPage (page);

    // Draw whatever the parameters already say, which is how a render and a
    // reopened editor come up in the state they were left in.
    refreshScreen();
}

//==============================================================================
std::vector<juce::Component*> ReverbPanel::pageControls (Page p) const
{
    // const_cast because this is a list of the panel's own members and the
    // caller parents them; making it non-const would mean two copies of a list
    // whose whole value is that there is one.
    auto* self = const_cast<ReverbPanel*> (this);

    switch (p)
    {
        case Page::early:
            // Row one is how the cluster is generated; row two is what is done
            // to it and what it is tied to. Three and three.
            return { &self->erModeBox, &self->densityKnob, &self->erSpreadKnob,
                     &self->erHiCutKnob, &self->variationKnob, &self->feedKnob };

        case Page::tail:
            // Three and two. WIDTH is here rather than on TONE because it is
            // M/S gain on the tail and nothing else -- see the class comment.
            return { &self->dampLoKnob, &self->dampHiKnob, &self->modDepthKnob,
                     &self->modRateKnob, &self->widthKnob };

        case Page::tone:
            // Three and three: the two shelves with their corners, then the
            // input cut and the output trim.
            return { &self->eqLoFreqKnob, &self->eqLoKnob, &self->eqHiFreqKnob,
                     &self->eqHiKnob, &self->inHiCutKnob, &self->outputKnob };
    }

    return {};
}

std::vector<juce::Component*> ReverbPanel::alwaysOnControls() const
{
    auto* self = const_cast<ReverbPanel*> (this);

    return { &self->sizeKnob, &self->preDelayKnob, &self->decayKnob,
             &self->erLevelKnob, &self->verbLevelKnob, &self->mixKnob, &self->typeBox };
}

std::vector<juce::Component*> ReverbPanel::allPageControls() const
{
    std::vector<juce::Component*> all;

    for (const auto p : { Page::early, Page::tail, Page::tone })
        for (auto* c : pageControls (p))
            all.push_back (c);

    return all;
}

//==============================================================================
void ReverbPanel::setPage (Page p)
{
    page = p;
    screen.setPage (p);

    for (size_t i = 0; i < pageButtons.size(); ++i)
        pageButtons[i]->setToggleState (i == (size_t) p, juce::dontSendNotification);

    // `resized` is what parents the new page's controls and unparents the old
    // one's, so turning a page is a layout and nothing else: no control is
    // rebuilt and no parameter attachment is touched.
    resized();
    repaint();
}

bool ReverbPanel::setUiState (const juce::String& key, const juce::String& value)
{
    if (key != "page")
        return false;

    if (value.equalsIgnoreCase ("early")) { setPage (Page::early); return true; }
    if (value.equalsIgnoreCase ("tail"))  { setPage (Page::tail);  return true; }
    if (value.equalsIgnoreCase ("tone"))  { setPage (Page::tone);  return true; }

    // **Refused, not defaulted.** BMO DEQ's comment is the argument and it
    // holds exactly here: a render labelled TONE that shows EARLY is worse
    // than no render, and nothing downstream could tell the two apart. The
    // snapshot tool treats a false as fatal for the same reason.
    return false;
}

void ReverbPanel::refreshScreen()
{
    LingerScreen::State s;

    // **Thirteen values and one row.** ATTACK has no parameter since the
    // 2026-09-21 trim, so the bloom the TAIL page draws comes off the selected
    // type's constants -- through `constantsFor`, which is the same call
    // `ReverbDsp::paramsFrom` makes, so the picture and the engine cannot come
    // to disagree about what a Plate's onset is.
    const auto& voicing = constantsFor ((int) context.params.getReal (Index::type));

    s.sizeM        = context.params.getReal (Index::size);
    s.preDelayMs   = context.params.getReal (Index::predelay);
    s.erDensity    = context.params.getReal (Index::erdensity);
    s.erLevelDb    = context.params.getReal (Index::erlevel);

    s.decaySeconds = context.params.getReal (Index::decay);
    s.dampLo       = context.params.getReal (Index::damplo);
    s.dampHi       = context.params.getReal (Index::damphi);
    s.attack       = voicing.attack;
    s.verbLevelDb  = context.params.getReal (Index::verblevel);

    s.eqLoFreqHz   = context.params.getReal (Index::eqlofreq);
    s.eqLoDb       = context.params.getReal (Index::eqlo);
    s.eqHiFreqHz   = context.params.getReal (Index::eqhifreq);
    s.eqHiDb       = context.params.getReal (Index::eqhi);
    s.inHiCutHz    = context.params.getReal (Index::inhicut);

    screen.setState (s);

    // The readout line is painted by the panel and lives in the bezel, so it
    // is the panel that has to redraw it when a number under it moves.
    repaint (readoutBox);
}

//==============================================================================
void ReverbPanel::paintPanel (juce::Graphics& g)
{
    const auto t = panelTokens();
    const auto ink = ui::accentInk (context.def.accent, t.well);

    // The bezel: a recess in `well`, like every other ground cut into a
    // faceplate, holding the screen and the line of printed text under it.
    // `panelTokens()` rather than `tokens()`, so an LTV plate would move the
    // well with it.
    g.setColour (t.well);
    g.fillRoundedRectangle (bezelBox.toFloat(), ui::Tokens::corner);
    g.setColour (ui::tokens().outline);
    g.drawRoundedRectangle (bezelBox.toFloat().reduced (0.5f), ui::Tokens::corner,
                            ui::Tokens::hairlineWeight);

    // The reading for the page that is showing. Small, printed, and the only
    // number anywhere on this panel -- see the constructor for why no knob
    // prints one.
    ui::drawLabel (g, screen.readout(), readoutBox.toFloat(), juce::Justification::centred,
                   ui::labelFont (kReadoutSize, true), ink);

    //== And that is everything this panel paints ==============================
    //
    // A raked speaker grille filled the fourth column of the level strip until
    // 2026-09-21. It was texture and nothing else -- no control, no state,
    // nothing to click -- and it stopped earning the space when the body's
    // corners went square: rendered, it read as a flat swatch beside the
    // knobs rather than as a grille behind them. Frosty cut it, TYPE has the
    // corner, and the only thing painted here now is the bezel and the line
    // of text inside it.
}

//==============================================================================
void ReverbPanel::resized()
{
    clearRules();

    // 4, the same content inset every other panel measures from.
    auto area = getLocalBounds().reduced (kPad, 4);

    // Six blocks and five gaps between them, the leftover falling at the foot.
    // The gap is what is left over rather than a number of its own, so a block
    // that changes height does not need anything else changed with it.
    const auto gap = juce::jmax (ui::Tokens::switchGap,
                                 (area.getHeight() - kContentHeight) / 5);

    // 120 px at the design width, which is what the four-column cell was at
    // 500 -- see `kCols`.
    const auto cell = area.getWidth() / kCols;

    // 80 px, and only the three levels at the foot use it. TYPE takes `cell`.
    const auto levelCell = (area.getWidth() - cell) / kFootLevels;

    area.removeFromTop (gap);

    //== The bezel, the screen and the readout =================================
    {
        bezelBox = area.removeFromTop (kBezelHeight);

        auto inner = bezelBox.reduced (kBezelPad);
        screenBox = inner.removeFromTop (kScreenHeight);
        inner.removeFromTop (kScreenToLine);
        readoutBox = inner.removeFromTop (kReadoutRow);

        screen.setBounds (screenBox);
    }

    //== The three page keys, one per column ===================================
    //
    // Three keys over three columns, so the row fills the grid exactly. It was
    // three of four centred while the grid was four wide.
    {
        area.removeFromTop (gap);

        auto row = area.removeFromTop (kPageRow);

        for (auto& button : pageButtons)
            button->setBounds (row.removeFromLeft (cell));
    }

    //== The persistent row ====================================================
    //
    // What the room is, when the tail arrives and how long it rings. Three
    // cells, on screen at every page. TYPE was the fourth and is at the foot.
    {
        area.removeFromTop (gap);

        auto row = area.removeFromTop (kMainRow);

        for (auto* c : std::initializer_list<juce::Component*> { &sizeKnob, &preDelayKnob,
                                                                 &decayKnob })
            c->setBounds (row.removeFromLeft (cell));
    }

    //== The page cluster ======================================================
    //
    // Added and removed as children rather than shown and hidden: a hidden
    // component still has bounds, and the layout suite walks every child
    // whether it is visible or not. See the class comment.
    {
        area.removeFromTop (gap);
        clusterBox = area.removeFromTop (kClusterRow * kClusterRows);

        const auto showing = pageControls (page);

        for (auto* c : allPageControls())
            if (std::find (showing.begin(), showing.end(), c) == showing.end())
                removeChildComponent (c);

        for (auto* c : showing)
            addAndMakeVisible (c);

        auto block = clusterBox;

        for (size_t first = 0; first < showing.size(); first += (size_t) kCols)
        {
            const auto n = (int) juce::jmin ((size_t) kCols, showing.size() - first);

            auto cells = block.removeFromTop (kClusterRow);

            // **A short row is centred in the three, not left-packed.** TAIL's
            // second row is two; left-packed, the hole in the third column
            // reads as a control that has gone missing rather than as a row of
            // two, which is the same fault MIX's lone centred knob had on the
            // old face.
            if (n < kCols)
                cells = cells.withSizeKeepingCentre (cell * n, cells.getHeight());

            for (int i = 0; i < n; ++i)
                showing[first + (size_t) i]->setBounds (cells.removeFromLeft (cell));
        }
    }

    //== LEVEL, the strip, and TYPE in the corner ==============================
    //
    // **Four controls here and three columns above**, which is the one place
    // this panel leaves its own grid. Three cells would put TYPE alone in a
    // row of its own, and "no control stands alone in a row" is the older
    // rule. TYPE keeps a full 120 px grid cell and the three levels divide
    // what is left -- see `kFootLevels` for the measurement that forced it.
    {
        area.removeFromTop (gap);

        // A shared rule, through `ModulePanel::addRule`, which draws edge to
        // edge -- and edge to edge is right now. The old panel painted six
        // headings itself because at the expanded width a shared rule would
        // have cut a line through the column it did not belong to; there is
        // one column here, so the suite's own path is the correct one again.
        //
        // It is legended LEVEL and TYPE now sits under it, which is the
        // wrinkle the class comment owns: a rule that stopped one cell short
        // would be a second kind of rule in the suite for one corner's sake.
        addRule (area.removeFromTop (ui::ModulePanel::kRuleRow), "LEVEL");

        auto row = area.removeFromTop (kStripRow);

        // TYPE last, in the corner the grille had, and it takes the whole of
        // what is left rather than a fourth share -- `row` is 120 px wide by
        // then, which is a grid cell exactly. It is a `ChoiceBox` and hangs
        // its box at the foot of a `kMainKnobSide` square, so its caption
        // lands on the same line as the three knobs beside it.
        for (auto* c : { &erLevelKnob, &verbLevelKnob, &mixKnob })
            c->setBounds (row.removeFromLeft (levelCell));

        typeBox.setBounds (row);
    }
}

} // namespace bmo::reverb
