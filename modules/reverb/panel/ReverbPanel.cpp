#include "ReverbPanel.h"
// The picture's ramp width and its right-hand edge are the engine's own
// constants, not copies of them -- so the display and the DSP cannot come to
// disagree about where a tap arrives or how long a tail is allowed to be.
#include "modules/reverb/dsp/DspCore.h"
#include "modules/reverb/params.h"

#include <cmath>

namespace bmo::reverb
{

namespace
{
    //== The main face =========================================================

    /** The knob draws at this side; the cell is wider so the caption
        underneath has room. BMO Opto's MAKEUP is the case that forced the
        distinction -- at the width the knob wanted, the caption clipped to
        "MAKEU". */
    constexpr int kKnobSide = 84;
    /** A row of character knobs. 118 until the brackets arrived; 124 now,
        because a caption sits against its knob's foot rather than its cell's
        and the six extra pixels split evenly above and below it. At 118 the
        bracket's turned-up ends reached into the caption box, which is three
        pixels from drawing a line through a descender. 124 leaves 13 px under
        the caption, which is the clearance BMO Dimension's pairs have. */
    constexpr int kKnobRow  = 124;
    constexpr int kSketchHeight = 150;

    // **There is no printed-value row on this panel and there used not to be
    // two.** TYPE and ER MODE were stepped knobs over a list of names, so both
    // printed their choice under themselves on a 14 px line -- and because a
    // value line lifts a knob and its caption by its own height, the knobs
    // beside them had to reserve the same line blank or the pair stopped
    // reading as one row and the bracket ran through "Room". Both are
    // dropdowns now (`ui::ChoiceBox`) and print inside their own boxes, so the
    // line, the two blank reservations and the taller rows that carried them
    // are all gone. Kept as a note because the next control tempted to print a
    // value inherits the whole arrangement, not just the line.

    /** The level row is three abreast rather than two, so its cells are a
        third of the column instead of a half and the knob has to come down
        with them: 84 in a 93 px cell leaves the dotted tracks almost touching.
        64 is what BMO Dimension's paired knobs use and what the rest of the
        suite matches, so the row reads as the suite's pairs rather than as
        this panel's own size.

        The caption comes down one step with it. "REVERB" is 94 px at 15 pt
        and the cell is 93, which is BMO Opto's MAKEUP over again; at 13 it is
        82 and clears. Two caption sizes on one face is BMO DEQ's precedent --
        its band row and its detector row are set differently -- and
        `ui_layout_tests` asserts the overflow rather than trusting this. */
    constexpr int kTrioKnobSide = 64;
    constexpr float kTrioCaptionSize = 13.0f;

    /** And the level row's own height. Not `kKnobRow`: a caption sits against
        its knob's foot, so a 64 px knob in a 124 px cell leaves 24 px of plate
        under the name and the bracket drawn at the cell's foot floats away
        from the pair it is joining. 102 is what puts it the same 13 px under
        this row's captions as under the two above -- solve h = (h - 84)/2 + 93
        for the knob centred in h minus its caption row. */
    constexpr int kTrioRow = 102;

    /** The main face's own section headings. `ModulePanel::addRule` draws edge
        to edge and would cut a line through the group column at the expanded
        width, so these are painted here and confined to the face -- the same
        argument, and the same code, as the three group headings. */
    constexpr int kFaceRuleRow = ui::ModulePanel::kRuleRow;

    /** The bracket under a pair, lifted wholesale from BMO Dimension: how far
        above the box's foot the line sits, how tall its turned-up ends are,
        how far in from the row's sides it starts, and its stroke. It is drawn
        in the raw accent at the 0.55 the dotted tracks use, because a bracket
        is the knobs' own mark rather than a rule -- a rule divides and this
        joins. See `DimPanel::paintPanel`, which carries the argument. */
    constexpr float kBracketRise   = 6.0f;
    constexpr float kBracketEnd    = 7.0f;
    constexpr float kBracketInset  = 8.0f;
    constexpr float kBracketWeight = 2.0f;

    /** The main face's own width, and the width a rack slot gets. Everything
        on it is laid out against this rather than against `getWidth()`, so the
        column does not stretch when the panel is expanded -- the groups take
        the extra width, which is what they are for. */
    constexpr int kFaceWidth = 300;

    //== The expanded groups ===================================================

    constexpr int kGroupColumnWidth = 380;
    constexpr int kGutter           = 20;   ///< face column to group column

    constexpr int kGroupCols     = 3;
    constexpr int kGroupKnobSide = 52;
    constexpr int kGroupKnobRow  = 72;
    constexpr float kGroupCaptionSize = 10.0f;

    /** The heading row. `ui::ModulePanel::kRuleRow` is the suite's height for
        one, and the groups use it even though they are painted here rather
        than by `paintRules` -- a heading that was a different height from
        every other rule in the suite would read as a different kind of thing. */
    constexpr int kGroupRuleRow = ui::ModulePanel::kRuleRow;

    constexpr int kSwitchWidth  = ui::Tokens::switchWidth;
    constexpr int kSwitchHeight = ui::Tokens::switchHeight;
    constexpr int kSwitchGap    = ui::Tokens::switchGap;

    //== The sketch ============================================================

    /** The right-hand edge of the time axis, in milliseconds. */
    constexpr float kMaxMs = ErTailSketch::kMaxSeconds * 1000.0f;

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

    /** The bloom ATTACK selects, in milliseconds, at `percent`. Linear,
        because the contour is CALIBRATE (10 section 8) and a curve here would
        be a guess drawn as a fact -- the same reason `detail::attackText`
        prints a linear figure. */
    constexpr float bloomMs (float percent) noexcept { return percent * 1.2f; }
}

//==============================================================================
ErTailSketch::ErTailSketch (juce::Colour accentColour) : accent (accentColour)
{
    setName ("DISPLAY");
    setInterceptsMouseClicks (false, false);
}

void ErTailSketch::setState (const State& s)
{
    const auto same = juce::approximatelyEqual (s.sizeM, state.sizeM)
                   && juce::approximatelyEqual (s.preDelayMs, state.preDelayMs)
                   && s.linkEr == state.linkEr
                   && juce::approximatelyEqual (s.decaySeconds, state.decaySeconds)
                   && juce::approximatelyEqual (s.dampLo, state.dampLo)
                   && juce::approximatelyEqual (s.dampHi, state.dampHi)
                   && juce::approximatelyEqual (s.attack, state.attack)
                   && juce::approximatelyEqual (s.erDensity, state.erDensity)
                   && juce::approximatelyEqual (s.erLevelDb, state.erLevelDb)
                   && juce::approximatelyEqual (s.verbLevelDb, state.verbLevelDb);

    if (same)
        return;

    state = s;
    repaint();
}

float ErTailSketch::firstTapTimeMs() const noexcept
{
    // Through the table's own function, not through a copy of its arithmetic:
    // a check that re-derived this its own way could agree with the bug it
    // exists to catch. `PlainKnob::captionOverflow` is written under the same
    // discipline.
    return tapTimeMsAt (kReferenceTaps[0], state.sizeM)
             + (state.linkEr ? state.preDelayMs : 0.0f);
}

float ErTailSketch::lastTapTimeMs() const noexcept
{
    return erSpanMsAt (state.sizeM) + (state.linkEr ? state.preDelayMs : 0.0f);
}

float ErTailSketch::tailEndSeconds() const noexcept
{
    const auto slowest = std::max (1.0f, std::max (state.dampLo, state.dampHi));
    return state.preDelayMs * 0.001f + state.decaySeconds * slowest;
}

int ErTailSketch::activeTapCount() const noexcept
{
    // **The 21 core taps never switch off.** That is not a simplification: it
    // is what keeps the renormalising denominator bounded away from zero, and
    // so what makes the whole density sweep continuous and click-free
    // (10 section 3). DENSITY spends infill on top of them.
    const auto d = juce::jlimit (0.0f, 1.0f, state.erDensity * 0.01f);

    int infill = 0;

    for (int i = 0; i < kMaxInfill; ++i)
    {
        const auto threshold = infillThreshold (i);

        if ((d - threshold) / kRampWidth > 0.0f)
            ++infill;
    }

    return kNumReferenceTaps + infill;
}

void ErTailSketch::paint (juce::Graphics& g)
{
    const auto t = ui::panelTokensFor (*this);
    const auto bounds = getLocalBounds().toFloat();
    const auto plot = bounds.reduced (1.0f);

    // A recess, like every other ground cut into a faceplate. panelTokensFor
    // rather than tokens(), so an LTV plate would move the well with it.
    g.setColour (t.well);
    g.fillRoundedRectangle (bounds, 3.0f);

    const auto centre = plot.getCentreY();
    const auto halfHeight = plot.getHeight() * 0.5f - 2.0f;

    const auto xFor = [&plot] (float ms)
    {
        const auto n = std::log (juce::jmax (kMinMs, ms) / kMinMs) / std::log (kMaxMs / kMinMs);
        return plot.getX() + plot.getWidth() * juce::jlimit (0.0f, 1.0f, (float) n);
    };

    /** A level in dB as a half-deflection in pixels. */
    const auto halfFor = [halfHeight] (float db)
    {
        const auto n = (db - kFloorDb) / (0.0f - kFloorDb);
        return halfHeight * juce::jlimit (0.0f, 1.0f, n);
    };

    const auto ink = ui::accentInk (accent, t.well);

    // The centre line, so a deflection is read against something rather than
    // guessed at from the height of the box.
    g.setColour (ui::tokens().hairline);
    g.fillRect (juce::Rectangle<float> (plot.getX(), centre, plot.getWidth(),
                                        ui::Tokens::hairlineWeight));

    // Decade marks -- 1, 10, 100 ms, 1 s, 10 s -- because a logarithmic axis
    // with nothing on it reads as a linear one that has gone wrong.
    {
        g.setColour (ui::tokens().hairline.withMultipliedAlpha (0.6f));

        for (float ms = 10.0f; ms < kMaxMs; ms *= 10.0f)
            g.fillRect (juce::Rectangle<float> (xFor (ms), plot.getY(),
                                                ui::Tokens::hairlineWeight, plot.getHeight()));
    }

    //== The direct sound ======================================================
    //
    // At t = 0, which is off a logarithmic axis entirely, so it is drawn hard
    // against the left edge at full height. It is the reference every other
    // distance here is measured from; leaving it out would make the pre-delay
    // gap look like the beginning of the sound rather than a gap after it.
    {
        g.setColour (ink.withAlpha (0.85f));
        g.fillRect (juce::Rectangle<float> (plot.getX(), centre - halfHeight,
                                            2.0f, halfHeight * 2.0f));
    }

    //== The early reflections =================================================
    //
    // Each tap is split above and below the centre line in the ratio its
    // bearing gives: centre-panned is symmetric, hard-left hangs entirely
    // below, hard-right stands entirely above. A reading aid and not a
    // measurement -- the shipped decorrelation uses different tap sets per
    // channel rather than one set with offsets (10 section 3).
    //
    // ER at "Off" draws nothing, because "Off" is silence and not -40 dB.
    // **Drawn last, after the tail**, although it is described here: the taps
    // are the detail in the picture and the tail is the shape behind them, and
    // a translucent envelope painted over a 1.6 px line swallows it. Defined
    // where it belongs in the reading order and called at the foot of paint().
    const auto drawEarlyReflections = [&]
    {
        if (state.erLevelDb <= -39.95f)
            return;

        const auto erShift = state.linkEr ? state.preDelayMs : 0.0f;

        const auto drawTap = [&] (float ms, float gain, float pan, float alpha)
        {
            if (gain <= 0.0f)
                return;

            const auto db = 20.0f * std::log10 (gain) + state.erLevelDb;
            const auto half = halfFor (db);

            if (half <= 0.0f)
                return;

            const auto x = xFor (ms);
            const auto up   = half * (1.0f + juce::jlimit (-1.0f, 1.0f, pan)) * 0.5f;
            const auto down = half * (1.0f - juce::jlimit (-1.0f, 1.0f, pan)) * 0.5f;

            g.setColour (ink.withAlpha (alpha));
            g.fillRect (juce::Rectangle<float> (x, centre - up, 1.6f, up + down));
        };

        // The core taps, always on, at full strength.
        for (const auto& tap : kReferenceTaps)
            drawTap (tapTimeMsAt (tap, state.sizeM) + erShift,
                     tapGainAt (tap, state.sizeM), tap.pan, 0.9f);

        // The infill DENSITY spends, drawn fainter because it is the part of
        // the picture the generator has not been written for yet. Times are
        // one per equal window with a deterministic nudge -- grid-based with
        // jitter, which 10 section 3 says sounds smoother than fully random
        // placement at the same density -- and gains come from the same 1/t
        // envelope evaluated at their own times, so the contour is identical
        // at every density. **The energy renormalisation is not drawn**: it
        // holds total ER energy constant to within 0.2 dB across the sweep,
        // which is a property of the sum rather than of any one line here.
        {
            const auto d = juce::jlimit (0.0f, 1.0f, state.erDensity * 0.01f);
            const auto first = tapTimeMsAt (kReferenceTaps[0], state.sizeM);
            const auto last  = erSpanMsAt (state.sizeM);
            const auto span  = juce::jmax (1.0f, last - first);

            for (int i = 0; i < kMaxInfill; ++i)
            {
                const auto threshold = infillThreshold (i);
                const auto w = juce::jlimit (0.0f, 1.0f, (d - threshold) / kRampWidth);

                if (w <= 0.0f)
                    continue;

                // A fixed integer hash for the jitter, so the picture is the
                // same every time it is drawn and the same on both machines.
                const auto jitter = 1.0f + 0.03f * (float) (((i * 37) % 7) - 3) / 3.0f;
                const auto ms = first + span * ((float) i + 0.5f) / (float) kMaxInfill * jitter;

                const auto gain = tapGainAt (kReferenceTaps[0], state.sizeM) * first / juce::jmax (1.0f, ms);
                const auto pan = (i % 2 == 0 ? 1.0f : -1.0f) * 0.35f;

                drawTap (ms + erShift, gain, pan, 0.30f + 0.35f * w);
            }
        }
    };

    //== The tail ==============================================================
    //
    // The pre-delay gap is drawn by its absence: nothing is between the direct
    // sound and `tailStartMs`, which is what a gap is. REVERB at "Off" draws
    // no tail at all.
    if (state.verbLevelDb > -39.95f)
    {
        const auto start = juce::jmax (kMinMs, state.preDelayMs);
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

        const auto mid  = state.decaySeconds;
        const auto fast = state.decaySeconds * std::min (state.dampLo, state.dampHi);
        const auto slow = state.decaySeconds * std::max (state.dampLo, state.dampHi);

        const auto x0 = xFor (start);
        const auto steps = juce::jmax (8, (int) plot.getWidth());

        /** Half-deflections across the drawn span, one per step, for a 60 dB
            time of `t60` seconds. Computed once and reused for the outline and
            for the filled band, so the two cannot disagree about where the
            envelope is. */
        const auto halves = [&] (float t60)
        {
            std::vector<float> out ((size_t) steps + 1);

            for (int i = 0; i <= steps; ++i)
            {
                const auto x = x0 + (plot.getRight() - x0) * (float) i / (float) steps;

                // Back out of the axis to the time this pixel stands for.
                const auto axis = (x - plot.getX()) / juce::jmax (1.0f, plot.getWidth());
                const auto ms = kMinMs * std::pow (kMaxMs / kMinMs, axis);

                out[(size_t) i] = halfFor (envelopeDb (ms, t60));
            }

            return out;
        };

        const auto xAt = [&] (int i)
        {
            return x0 + (plot.getRight() - x0) * (float) i / (float) steps;
        };

        /** The closed region between the centre line and an envelope, above
            and below: out along the top and back along the bottom. */
        const auto region = [&] (const std::vector<float>& h)
        {
            juce::Path p;
            p.startNewSubPath (xAt (0), centre - h[0]);

            for (int i = 1; i <= steps; ++i)
                p.lineTo (xAt (i), centre - h[(size_t) i]);

            for (int i = steps; i >= 0; --i)
                p.lineTo (xAt (i), centre + h[(size_t) i]);

            p.closeSubPath();
            return p;
        };

        /** One side of an envelope, as a line to stroke. */
        const auto edge = [&] (const std::vector<float>& h, float sign)
        {
            juce::Path p;
            p.startNewSubPath (xAt (0), centre + sign * h[0]);

            for (int i = 1; i <= steps; ++i)
                p.lineTo (xAt (i), centre + sign * h[(size_t) i]);

            return p;
        };

        // The band out to the slowest of the three decay times, with the
        // fastest outlined inside it: this is the damping multipliers made
        // visible, which is the one thing about them a number on a knob does
        // not say. At 1.20x low and 0.40x high -- the defaults -- the band is
        // wide, and that is the point.
        {
            const auto slowHalves = halves (slow);
            const auto fastHalves = halves (fast);

            g.setColour (ink.withAlpha (0.16f));
            g.fillPath (region (slowHalves));

            g.setColour (ink.withAlpha (0.35f));
            g.strokePath (edge (fastHalves, -1.0f), juce::PathStrokeType (1.0f));
            g.strokePath (edge (fastHalves,  1.0f), juce::PathStrokeType (1.0f));
        }

        // The mid band itself, which is what DECAY says.
        {
            const auto midHalves = halves (mid);

            g.setColour (ink);
            g.strokePath (edge (midHalves, -1.0f), juce::PathStrokeType (1.4f));
            g.strokePath (edge (midHalves,  1.0f), juce::PathStrokeType (1.4f));
        }
    }

    drawEarlyReflections();

    g.setColour (ui::tokens().outline);
    g.drawRoundedRectangle (bounds.reduced (0.5f), 3.0f, ui::Tokens::hairlineWeight);
}

//==============================================================================
ReverbPanel::ReverbPanel (ui::ModuleContext ctx)
    : ModulePanel (std::move (ctx)),
      sketch (context.def.accent),

      // **One colour for the whole module.** `ui::Knob::Style` says what the
      // two faces mean, and the meaning is not "main face" against "expanded":
      // `utility` is the pale blue of *input, output, gain*, `character` is
      // the module's accent worn by anything that shapes the sound. BMO DEQ is
      // the precedent, and rendering the two side by side is how to check it
      // -- FREQ, GAIN and Q are its accent teal and OUTPUT alone is blue.
      //
      // So every knob here is `character` but `output`, the one control in
      // this schema that is literally an output trim. The seven on the face
      // were already; the twenty-three in the groups were `utility` and so
      // drew the suite azure, which rendered the module violet above and blue
      // below as though it were two modules sharing a slot.
      //
      // IN HI-CUT is the near miss and stays `character`: it is a tone control
      // on the way in rather than a level, which is the reason this panel
      // takes neither shared section in the first place. WIDTH is the other,
      // and stays `character` too -- it sets how wide the tail is made, which
      // is BMO Dimension's DIMENSION and not anybody's gain.
      // **TYPE is the one control on the face that is a list of names rather
      // than an amount, so it is the one that is not a knob.** The style
      // argument above applies to everything that has a face to draw; a
      // dropdown carries the module's colour on its arrow and its caption
      // instead. See `ui::ChoiceBox`.
      typeBox       (context.params.param (Index::type),
                     context.params.spec  (Index::type),      "TYPE",      context.def.accent),
      sizeKnob      (context.params.param (Index::size),      "SIZE",      ui::Knob::Style::character, 0.62f, context.def.accent),
      preDelayKnob  (context.params.param (Index::predelay),  "PRE-DELAY", ui::Knob::Style::character, 0.62f, context.def.accent),
      decayKnob     (context.params.param (Index::decay),     "DECAY",     ui::Knob::Style::character, 0.62f, context.def.accent),
      erLevelKnob   (context.params.param (Index::erlevel),   "ER",        ui::Knob::Style::character, 0.62f, context.def.accent),
      verbLevelKnob (context.params.param (Index::verblevel), "REVERB",    ui::Knob::Style::character, 0.62f, context.def.accent),
      mixKnob       (context.params.param (Index::mix),       "MIX",       ui::Knob::Style::character, 0.62f, context.def.accent),

      // EARLY. ER MODE is the panel's other list of names, and its other
      // dropdown: Energy is not more than Taps.
      erModeBox     (context.params.param (Index::ermode),
                     context.params.spec  (Index::ermode),      "ER MODE",   context.def.accent),
      densityKnob   (context.params.param (Index::erdensity),   "DENSITY",   ui::Knob::Style::character, 0.58f, context.def.accent),
      erShapeKnob   (context.params.param (Index::ershape),     "ER SHAPE",  ui::Knob::Style::character, 0.58f, context.def.accent),
      erSpreadKnob  (context.params.param (Index::erspread),    "ER SPREAD", ui::Knob::Style::character, 0.58f, context.def.accent),
      erHiCutKnob   (context.params.param (Index::erhicut),     "ER HI-CUT", ui::Knob::Style::character, 0.58f, context.def.accent),
      variationKnob (context.params.param (Index::ervariation), "VARIATION", ui::Knob::Style::character, 0.58f, context.def.accent),
      feedKnob      (context.params.param (Index::feed),        "SOURCE",    ui::Knob::Style::character, 0.58f, context.def.accent),
      // LINK ER lights in `switchAlt`, which is modules/AGENTS.md's table with
      // no exception taken: it is neither a bypass nor mono nor a polarity
      // flip, so it is "anything else". So the accent is on every knob that
      // shapes the sound and on the display; what is *not* in it is this
      // switch and the OUTPUT trim, and nothing else.
      linkErSwitch  (context.params.param (Index::prelink), "LINK ER", ui::tokens().switchAlt),

      // TAIL.
      attackKnob     (context.params.param (Index::attack),     "ATTACK",      ui::Knob::Style::character, 0.58f, context.def.accent),
      decayShapeKnob (context.params.param (Index::decayshape), "DECAY SHAPE", ui::Knob::Style::character, 0.58f, context.def.accent),
      // "LOW x" and not "LOW x" with a multiplication sign: the two display
      // faces are licensed individually and live outside this repository, so a
      // glyph outside ASCII is one this suite cannot promise it can draw. The
      // value strings follow, printing "1.20x".
      dampLoFreqKnob (context.params.param (Index::damplofreq), "LOW x FREQ",  ui::Knob::Style::character, 0.58f, context.def.accent),
      dampLoKnob     (context.params.param (Index::damplo),     "LOW x",       ui::Knob::Style::character, 0.58f, context.def.accent),
      dampHiFreqKnob (context.params.param (Index::damphifreq), "HIGH x FREQ", ui::Knob::Style::character, 0.58f, context.def.accent),
      dampHiKnob     (context.params.param (Index::damphi),     "HIGH x",      ui::Knob::Style::character, 0.58f, context.def.accent),

      // TONE & OUT. OUTPUT is the one `utility` knob on this panel: a trim on
      // the way out is exactly what the style is for, and it is the same knob
      // BMO DEQ leaves blue at the foot of its own accent-coloured face.
      eqLoFreqKnob (context.params.param (Index::eqlofreq), "EQ LOW FREQ",  ui::Knob::Style::character, 0.58f, context.def.accent),
      eqLoKnob     (context.params.param (Index::eqlo),     "EQ LOW",       ui::Knob::Style::character, 0.58f, context.def.accent),
      eqHiFreqKnob (context.params.param (Index::eqhifreq), "EQ HIGH FREQ", ui::Knob::Style::character, 0.58f, context.def.accent),
      eqHiKnob     (context.params.param (Index::eqhi),     "EQ HIGH",      ui::Knob::Style::character, 0.58f, context.def.accent),
      inHiCutKnob  (context.params.param (Index::inhicut),  "IN HI-CUT",    ui::Knob::Style::character, 0.58f, context.def.accent),
      widthKnob    (context.params.param (Index::width),    "WIDTH",        ui::Knob::Style::character, 0.58f, context.def.accent),
      modDepthKnob (context.params.param (Index::moddepth), "MOD DEPTH",    ui::Knob::Style::character, 0.58f, context.def.accent),
      modRateKnob  (context.params.param (Index::modrate),  "MOD RATE",     ui::Knob::Style::character, 0.58f, context.def.accent),
      outputKnob   (context.params.param (Index::output),   "OUTPUT",       ui::Knob::Style::utility,   0.58f, context.def.accent)
{
    addAndMakeVisible (sketch);

    // TYPE hangs its box at the foot of the square a `kKnobSide` knob would
    // occupy, so its caption lands on SIZE's line rather than twenty pixels
    // off it. `ui::ChoiceBox::setControlSide` carries the argument.
    typeBox.setControlSide (kKnobSide);
    addAndMakeVisible (typeBox);

    for (auto* k : { &sizeKnob, &preDelayKnob, &decayKnob,
                     &erLevelKnob, &verbLevelKnob, &mixKnob })
    {
        k->setKnobSide (kKnobSide);
        addAndMakeVisible (k);
    }

    // The level row, three abreast in a third of the column each. See
    // kTrioKnobSide for why they are smaller than the four above them.
    for (auto* k : { &erLevelKnob, &verbLevelKnob, &mixKnob })
    {
        k->setKnobSide (kTrioKnobSide);
        k->setCaptionSize (kTrioCaptionSize);
    }

    // **Nothing on this panel prints its value any more, and the two controls
    // that used to are the reason.** TYPE and ER MODE were stepped knobs over
    // a list of names, which is unreadable without a printed value, so both
    // opted in -- and their row-mates then had to reserve the same blank line
    // to stay level with them. The dropdowns print their own choice inside the
    // box, so the value line and the two blank ones it forced go with them,
    // and the panel is back to the suite's default: knobs say less and more,
    // not how much (`ui::PlainKnob`'s class comment).

    for (auto* c : groupControls())
    {
        if (auto* k = dynamic_cast<ui::PlainKnob*> (c))
        {
            k->setKnobSide (kGroupKnobSide);
            k->setCaptionSize (kGroupCaptionSize);
        }
        else if (auto* b = dynamic_cast<ui::ChoiceBox*> (c))
        {
            b->setControlSide (kGroupKnobSide);
            b->setCaptionSize (kGroupCaptionSize);
        }
    }

    // Every parameter the picture is drawn from redraws it, and the list is
    // exactly `ErTailSketch::State`'s fields -- which is the thing a reader
    // wants to be able to check at a glance.
    {
        const int drawn[] { Index::size, Index::predelay, Index::prelink, Index::decay,
                            Index::damplo, Index::damphi, Index::attack, Index::erdensity,
                            Index::erlevel, Index::verblevel };

        static_assert (sizeof (drawn) / sizeof (int) == 10, "one attachment per drawn parameter");

        for (size_t i = 0; i < sketchAttachments.size(); ++i)
            sketchAttachments[i] = std::make_unique<juce::ParameterAttachment> (
                context.params.param (drawn[i]),
                [this] (float) { refreshSketch(); });
    }

    // Draw whatever the parameters already say, which is how a render and a
    // reopened editor come up in the state they were left in.
    refreshSketch();
}

std::vector<juce::Component*> ReverbPanel::groupControls() const
{
    // const_cast because this is a list of the panel's own members and the
    // caller parents them; making it non-const would mean two copies of a list
    // whose whole value is that there is one.
    auto* self = const_cast<ReverbPanel*> (this);

    return {
        &self->erModeBox, &self->densityKnob, &self->erShapeKnob,
        &self->erSpreadKnob, &self->erHiCutKnob, &self->variationKnob,
        &self->feedKnob, &self->linkErSwitch,

        &self->attackKnob, &self->decayShapeKnob,
        &self->dampLoFreqKnob, &self->dampLoKnob,
        &self->dampHiFreqKnob, &self->dampHiKnob,

        &self->eqLoFreqKnob, &self->eqLoKnob, &self->eqHiFreqKnob, &self->eqHiKnob,
        &self->inHiCutKnob, &self->widthKnob, &self->modDepthKnob,
        &self->modRateKnob, &self->outputKnob,
    };
}

bool ReverbPanel::isShowingExpanded() const noexcept
{
    return context.def.isExpandable() && getWidth() >= context.def.expandedWidth;
}

void ReverbPanel::refreshSketch()
{
    ErTailSketch::State s;

    s.sizeM        = context.params.getReal (Index::size);
    s.preDelayMs   = context.params.getReal (Index::predelay);
    s.linkEr       = context.params.getReal (Index::prelink) >= 0.5f;
    s.decaySeconds = context.params.getReal (Index::decay);
    s.dampLo       = context.params.getReal (Index::damplo);
    s.dampHi       = context.params.getReal (Index::damphi);
    s.attack       = context.params.getReal (Index::attack);
    s.erDensity    = context.params.getReal (Index::erdensity);
    s.erLevelDb    = context.params.getReal (Index::erlevel);
    s.verbLevelDb  = context.params.getReal (Index::verblevel);

    sketch.setState (s);
}

//==============================================================================
void ReverbPanel::paintPanel (juce::Graphics& g)
{
    // Six headings, three on the face and three over the groups, all painted
    // here rather than through `ModulePanel::addRule`, which draws edge to
    // edge: at the expanded width that would cut a line straight through the
    // other column as well as through the one it belongs to. See the class
    // comment.
    const auto plate = panelTokens().plate;
    const auto ink = ui::accentInk (context.def.accent, plate);
    const auto font = ui::labelFont (kLegendSize, true);

    /** `ModulePanel::drawRuleLegend` confined to one column: a hairline across
        the row with the name knocked out of the middle of it. */
    const auto legend = [&] (juce::Rectangle<int> row, const char* name)
    {
        g.setColour (ui::tokens().hairline);
        g.fillRect (juce::Rectangle<float> ((float) row.getX(), (float) row.getCentreY(),
                                            (float) row.getWidth(), ui::Tokens::hairlineWeight));

        const juce::String text { name };
        const auto width = juce::GlyphArrangement::getStringWidth (font, text) + 14.0f;
        const auto box = juce::Rectangle<float> (width, (float) row.getHeight())
                             .withCentre (row.toFloat().getCentre());

        // The legend knocks a hole in the rule it sits on, and on an LTV panel
        // that hole has to be silver or the rule shows through it.
        g.setColour (plate);
        g.fillRect (box);
        ui::drawLabel (g, text, box, juce::Justification::centred, font, ink);
    };

    // The face, at both widths: what the room is, when it arrives and how long
    // it rings, and how much of each of it.
    static const char* const faceNames[] { "ROOM", "TIME", "LEVEL" };

    for (size_t i = 0; i < faceRules.size() && i < 3; ++i)
        legend (faceRules[i], faceNames[i]);

    // The groups, expanded only, in the order a reverb is built.
    static const char* const groupNames[] { "EARLY", "TAIL", "TONE & OUT" };

    for (size_t i = 0; i < groupRules.size() && i < 3; ++i)
        legend (groupRules[i], groupNames[i]);

    // And a bracket under each pair on the face: a line with its ends turned
    // up, in the raw accent at the 0.55 the dotted tracks use, so it is the
    // knobs' own mark rather than a rule -- a rule divides and this joins.
    // BMO Dimension's device, and the render that chose it over a well is
    // argued in `DimPanel::paintPanel`.
    g.setColour (context.def.accent.withAlpha (0.55f));

    for (const auto& box : pairBoxes)
    {
        if (box.isEmpty())
            continue;

        const auto y  = (float) box.getBottom() - kBracketRise;
        const auto x0 = (float) box.getX() + kBracketInset;
        const auto x1 = (float) box.getRight() - kBracketInset;

        g.fillRect (juce::Rectangle<float> (x0, y - kBracketWeight * 0.5f, x1 - x0, kBracketWeight));
        g.fillRect (juce::Rectangle<float> (x0, y - kBracketEnd, kBracketWeight, kBracketEnd));
        g.fillRect (juce::Rectangle<float> (x1 - kBracketWeight, y - kBracketEnd, kBracketWeight, kBracketEnd));
    }
}

//==============================================================================
void ReverbPanel::resized()
{
    clearRules();
    faceRules.clear();
    groupRules.clear();
    pairBoxes = {};

    const auto expanded = isShowingExpanded();

    // Added and removed rather than shown and hidden: a hidden component still
    // has bounds, and the layout suite walks every child whether it is visible
    // or not. See the class comment.
    for (auto* c : groupControls())
    {
        if (expanded)
            addAndMakeVisible (c);
        else
            removeChildComponent (c);
    }

    // 4, the same content inset every other panel measures from.
    auto area = getLocalBounds().reduced (kPad, 4);

    // The face column keeps its own width whatever the panel's is. The extra
    // width belongs to the groups.
    auto face = area.removeFromLeft (juce::jmin (area.getWidth(), kFaceWidth - kPad * 2));

    //== The main face =========================================================
    {
        // Four blocks -- the display and three control rows -- and five gaps: a
        // margin above the first and below the last as well as between them,
        // so the spacing stays even if a block's height changes later.
        //
        // **The ROOM row was one value line taller than the others** while
        // TYPE was a knob printing its choice under itself. TYPE is a dropdown
        // and prints its choice inside its box, so the extra line is gone and
        // the three rows are the plain ones again.
        const auto content = kSketchHeight + kKnobRow * 2 + kTrioRow;
        const auto gap = juce::jmax (kSwitchGap, (face.getHeight() - content) / 5);

        /** A heading set *in* a gap the rhythm was going to leave anyway, so
            nothing moves to make room for it. BMO Dimension's `legendIn`. */
        const auto legendIn = [this] (juce::Rectangle<int> gapRow)
        {
            faceRules.push_back (gapRow.withSizeKeepingCentre (gapRow.getWidth(), kFaceRuleRow));
        };

        // `juce::Component&` rather than `ui::PlainKnob&`: the ROOM row is a
        // dropdown beside a knob, and both halves of a pair get the same cell
        // whatever they are -- which is also what keeps their tops and their
        // feet level, the thing `ui_layout_tests` reads to decide that two
        // controls are in one row.
        const auto pair = [] (juce::Rectangle<int> row, juce::Component& left, juce::Component& right)
        {
            const auto half = row.getWidth() / 2;
            left .setBounds (row.removeFromLeft (half));
            right.setBounds (row);
        };

        face.removeFromTop (gap);

        // The display first: every control under it moves it, so it sits over
        // all of them rather than beside any one.
        sketch.setBounds (face.removeFromTop (kSketchHeight));

        // ROOM: what the room is.
        legendIn (face.removeFromTop (gap));
        pairBoxes[0] = face.removeFromTop (kKnobRow);
        pair (pairBoxes[0], typeBox, sizeKnob);

        // TIME: when it arrives and how long it rings.
        legendIn (face.removeFromTop (gap));
        pairBoxes[1] = face.removeFromTop (kKnobRow);
        pair (pairBoxes[1], preDelayKnob, decayKnob);

        // LEVEL: the two absolute trims -- the thesis, and the pair that has
        // to be reachable without expanding anything -- and beside them how
        // much of the whole thing.
        //
        // **Three abreast, because seven controls divide 2 + 2 + 3 and no
        // other way that leaves nothing standing alone in a row.** MIX had a
        // row of its own, centred at half width, and a centred knob with two
        // empty quarters beside it is what an orphan looks like however it is
        // justified. The bracket under ER and REVERB is what still says that
        // *those two* are the pair and MIX is the one after them.
        legendIn (face.removeFromTop (gap));
        {
            auto row = face.removeFromTop (kTrioRow);
            const auto cell = row.getWidth() / 3;

            // Three whole cells centred, so the pixels left over from the
            // division fall evenly either side instead of all on MIX.
            row = row.withSizeKeepingCentre (cell * 3, row.getHeight());

            pairBoxes[2] = row.removeFromLeft (cell * 2);
            pair (pairBoxes[2], erLevelKnob, verbLevelKnob);
            mixKnob.setBounds (row);
        }
    }

    if (! expanded)
    {
        groupColumn = {};
        return;
    }

    //== The expanded groups ===================================================

    area.removeFromLeft (kGutter);
    auto groups = area;
    groupColumn = groups;

    const auto cellWidth = groups.getWidth() / kGroupCols;

    // Four divisions for three blocks, not three: the fourth is the margin
    // under TONE & OUT. With three, the last group's bottom row ran into the
    // foot of the panel while the face column stopped short of it, and that
    // difference is most of what made the two halves look unbalanced.
    //
    // EARLY's first row was one value line taller, for ER MODE's printed
    // choice, exactly as the face's ROOM row was for TYPE's. Both lines went
    // with the knobs that needed them.
    const auto content = kGroupRuleRow * 3 + kGroupKnobRow * 8;
    const auto gap = juce::jmax (kSwitchGap, (groups.getHeight() - content) / 4);

    const auto heading = [&] (juce::Rectangle<int>& column)
    {
        groupRules.push_back (column.removeFromTop (kGroupRuleRow));
    };

    /** Up to three controls abreast, each in a third of the column.

        `juce::Component*` rather than `ui::PlainKnob*`, for the face's `pair`
        reason: EARLY's first row is a dropdown beside two knobs, and a cell is
        a cell. It used to take an `extra` for the value line ER MODE's printed
        choice needed; the dropdown prints inside its box, so every row here is
        now the same height. */
    const auto row = [&] (juce::Rectangle<int>& column,
                          std::initializer_list<juce::Component*> controls)
    {
        auto cells = column.removeFromTop (kGroupKnobRow);

        for (auto* c : controls)
            c->setBounds (cells.removeFromLeft (cellWidth));
    };

    groups.removeFromTop (gap);

    // EARLY: how the early cluster is made, and what feeds the tail from it.
    heading (groups);
    row (groups, { &erModeBox, &densityKnob, &erShapeKnob });
    row (groups, { &erSpreadKnob, &erHiCutKnob, &variationKnob });
    {
        auto cells = groups.removeFromTop (kGroupKnobRow);

        // **Two cells centred in the three, not left-packed.** The hole a
        // left-packed pair left in the third column read as a control that had
        // gone missing rather than as a group of two, and it was the one ragged
        // row in the expanded column.
        cells = cells.withSizeKeepingCentre (cellWidth * 2, cells.getHeight());

        feedKnob.setBounds (cells.removeFromLeft (cellWidth));

        // LINK ER beside SOURCE: they are the two controls in this group that
        // are about the ER's relationship to something else rather than about
        // the ER themselves. Vertically centred in the knob row, because a
        // switch is shorter than a knob and hanging it off the top would read
        // as a heading for the cell to its right.
        linkErSwitch.setBounds (cells.removeFromLeft (cellWidth)
                                     .withSizeKeepingCentre (kSwitchWidth, kSwitchHeight));
    }
    groups.removeFromTop (gap);

    // TAIL: the late network's onset, its truncation and its damping.
    heading (groups);
    row (groups, { &attackKnob, &decayShapeKnob, &dampLoFreqKnob });
    row (groups, { &dampLoKnob, &dampHiFreqKnob, &dampHiKnob });
    groups.removeFromTop (gap);

    // TONE & OUT: the Reverb EQ, what feeds both generators, and the output.
    heading (groups);
    row (groups, { &eqLoFreqKnob, &eqLoKnob, &eqHiFreqKnob });
    row (groups, { &eqHiKnob, &inHiCutKnob, &widthKnob });
    row (groups, { &modDepthKnob, &modRateKnob, &outputKnob });
}

} // namespace bmo::reverb
