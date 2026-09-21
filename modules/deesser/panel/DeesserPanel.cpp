#include "DeesserPanel.h"
#include "modules/deesser/params.h"

namespace bmo::deesser
{

namespace
{
    // The knob draws at kKnobSide; the cell is wider so the caption underneath
    // has room. BMO Opto's MAKEUP is the case that forced the distinction --
    // at the width the knob wanted, the caption clipped to "MAKEU".
    constexpr int kKnobSide = 84;
    constexpr int kPairRow  = 120;   ///< a row of two character knobs

    constexpr int kSwitchWidth  = ui::Tokens::switchWidth;
    constexpr int kSwitchHeight = ui::Tokens::switchHeight;
    constexpr int kSwitchGap    = ui::Tokens::switchGap;

    /** The GR bar's width, which is still the 226 the old IN/GR/OUT row wanted
        -- three switches and their two gaps.

        The row is gone (see the panel's class comment) but the number is kept
        deliberately: it is the width the shape pair and LISTEN already align
        to, so the bar's well lines up with the switches above and below it
        rather than introducing a third edge on a 260-px panel.

        **It is a quarter of the height the needle took.** The meter block was
        102 px of face plus a 4-px gap and a 20-px switch row, 126 in all; the
        bar is 22 of well and air plus its printed scale. What that bought is
        not empty plate -- it is where the sibilance strip goes. */
    constexpr int kMeterWidth = kSwitchWidth * 3 + kSwitchGap * 2;
    constexpr int kBarRow     = 22 + ui::LevelBar::kScaleRow;

    /** The top of the GR bar, and it is RANGE's ceiling rather than a meter
        convention.

        `params.h` freezes RANGE at 18 dB, and RANGE is the deepest cut the
        module can make -- so 18 dB of reduction is the most this meter can
        ever be asked to show. The shared needle's 24 dB scale left a top
        quarter that no setting could reach, which is a scale that lies about
        the instrument. If RANGE's ceiling ever moves, this moves with it; it
        is not free to drift on its own. */
    constexpr float kMaxReductionDb = 18.0f;

    /** A pair of switches side by side. */
    constexpr int kPairWidth = kSwitchWidth * 2 + kSwitchGap;

    constexpr int kSketchHeight = 104;

    /** The ribbon under it. 44 px of the 51 the needle left behind, which is
        where that space was always going -- see the class comment. Shallower
        than the sketch on purpose: the sketch is the thing being set and the
        ribbon is the thing being watched, and a reader should be able to tell
        which is which without reading either. */
    constexpr int kRibbonHeight = 44;

    //== The sketch's axes =====================================================
    //
    // Both are fixed rather than fitted to the band, so that moving FREQ moves
    // the bump across the picture and moving RANGE makes it deeper. A picture
    // that rescaled itself would hold the curve still while the numbers
    // changed, which is the opposite of what it is for.

    /** Wider than `freq`'s own 2-10 kHz travel, so the band is drawn in
        context: at either end of the knob there is still air on both sides,
        and the shelf has somewhere to run out to. */
    constexpr float kMinHz = 1000.0f, kMaxHz = 20000.0f;

    /** A little headroom over the 0 dB line and 20 dB under it -- `range` caps
        at 18, so the deepest bell the module can draw still clears the floor
        of the box rather than being clipped by it. */
    constexpr float kTopDb = 2.0f, kBottomDb = -20.0f;
}

//==============================================================================
BandSketch::BandSketch (juce::Colour accentColour) : accent (accentColour)
{
    setInterceptsMouseClicks (false, false);
}

void BandSketch::setBand (float newFreqHz, float newQ, int newShape, float newRangeDb)
{
    if (juce::approximatelyEqual (newFreqHz, freqHz)
        && juce::approximatelyEqual (newQ, q)
        && juce::approximatelyEqual (newRangeDb, rangeDb)
        && newShape == shapeChoice)
        return;

    freqHz      = newFreqHz;
    q           = newQ;
    rangeDb     = newRangeDb;
    shapeChoice = newShape;

    repaint();
}

float BandSketch::responseDbAt (float hz) const noexcept
{
    // The analogue prototype's magnitude, not the running filter's. The
    // difference is near-Nyquist warping, which the shipped engine does not
    // have either -- it is matched-Z, so it maps the pole exactly
    // (docs/deesser/10-dsp-spec.md 7) -- and which at this size would be
    // sub-pixel anyway. What matters is that the drawing and the DSP agree
    // about *shape*, and the prototype is what they both design from.
    const auto w  = (double) hz;
    const auto w0 = (double) freqHz;

    // A shelf's Q through effectiveQ, which is what the engine will design
    // through: past 2 the shelf grows a resonant dip below its corner and
    // climbs back above it, which is not a shelf. The knob still reads what it
    // reads -- Q is one parameter whatever the shape. This picture drawing the
    // raw Q is how the rule was found; see modules/deesser/params.h.
    const auto qq = (double) juce::jmax (0.1f, effectiveQ (shapeChoice, q));

    // The cut, as a linear gain: `range` is a depth, so it enters negative.
    const auto a = std::pow (10.0, -(double) rangeDb / 40.0);

    if (shapeChoice == highShelf)
    {
        // RBJ's analogue high shelf, cutting above the corner: unity at DC,
        // the full depth well above it. The shelf is the split-band mode with
        // no crossover -- one minimum-phase filter, so there is nothing to
        // reconstruct and nothing to comb.
        const auto rootA = std::sqrt (a);
        const auto skirt = rootA * w * w0 / qq;

        const auto num = a * std::hypot (w0 * w0 - a * w * w, skirt);
        const auto den = std::hypot (a * w0 * w0 - w * w, skirt);

        return (float) (20.0 * std::log10 (std::max (num / std::max (den, 1.0e-12), 1.0e-12)));
    }

    // The peaking bell, unity at both ends and `range` deep at the centre.
    const auto gap   = w0 * w0 - w * w;
    const auto num   = std::hypot (gap, w * a * w0 / qq);
    const auto den   = std::hypot (gap, w * w0 / (a * qq));

    return (float) (20.0 * std::log10 (std::max (num / std::max (den, 1.0e-12), 1.0e-12)));
}

void BandSketch::paint (juce::Graphics& g)
{
    const auto t = ui::panelTokensFor (*this);
    const auto bounds = getLocalBounds().toFloat();
    const auto plot = bounds.reduced (1.0f);

    // A recess, like every other ground cut into a faceplate. panelTokensFor
    // rather than tokens(), so an LTV plate would move the well with it.
    g.setColour (t.well);
    g.fillRoundedRectangle (bounds, 3.0f);

    const auto xFor = [&plot] (float hz)
    {
        const auto n = std::log (hz / kMinHz) / std::log (kMaxHz / kMinHz);
        return plot.getX() + plot.getWidth() * (float) n;
    };

    const auto yFor = [&plot] (float db)
    {
        const auto n = (kTopDb - db) / (kTopDb - kBottomDb);
        return plot.getY() + plot.getHeight() * juce::jlimit (0.0f, 1.0f, n);
    };

    const auto zero = yFor (0.0f);

    // Unity, so the depth of the cut is read against something rather than
    // guessed at from the height of the box.
    g.setColour (ui::tokens().hairline);
    g.fillRect (juce::Rectangle<float> (plot.getX(), zero, plot.getWidth(),
                                        ui::Tokens::hairlineWeight));

    // The band, legible against the well it is drawn on rather than trusted to
    // be -- the well is pale in one appearance and dark in the other, and the
    // raw accent cannot clear both. See modules/AGENTS.md: no hex in a panel.
    const auto ink = ui::accentInk (accent, t.well);

    juce::Path curve;

    for (int x = 0; x <= (int) plot.getWidth(); ++x)
    {
        const auto n  = (float) x / juce::jmax (1.0f, plot.getWidth());
        const auto hz = kMinHz * std::pow (kMaxHz / kMinHz, n);
        const auto px = plot.getX() + (float) x;
        const auto py = yFor (responseDbAt (hz));

        if (x == 0)
            curve.startNewSubPath (px, py);
        else
            curve.lineTo (px, py);
    }

    // The cut itself, shaded between the curve and unity: the area is the
    // thing being described, and an outline alone reads as a line rather than
    // as a notch taken out of the top end.
    {
        auto shaded = curve;
        shaded.lineTo (plot.getRight(), zero);
        shaded.lineTo (plot.getX(), zero);
        shaded.closeSubPath();

        g.setColour (ink.withAlpha (0.28f));
        g.fillPath (shaded);
    }

    // Where the band is centred -- the corner, in shelf mode. **A drop line
    // from the curve, not a full-height rule.** Drawn edge to edge it split
    // the box in two and read as a divider between two halves of a picture
    // that has no halves; hung under the curve it reads as what it is, a tick
    // saying where FREQ is. Under the stroke, so the curve crosses it cleanly.
    {
        const auto x = xFor (juce::jlimit (kMinHz, kMaxHz, freqHz));
        const auto top = yFor (responseDbAt (juce::jlimit (kMinHz, kMaxHz, freqHz)));

        g.setColour (ink.withAlpha (0.35f));
        g.fillRect (juce::Rectangle<float> (x, top, ui::Tokens::hairlineWeight,
                                            plot.getBottom() - top));
    }

    g.setColour (ink);
    g.strokePath (curve, juce::PathStrokeType (1.6f));

    g.setColour (ui::tokens().outline);
    g.drawRoundedRectangle (bounds.reduced (0.5f), 3.0f, ui::Tokens::hairlineWeight);
}

//==============================================================================
DeesserPanel::DeesserPanel (ui::ModuleContext ctx)
    : ModulePanel (std::move (ctx)),
      // All four are the module: character style, in the module's own colour.
      // There is no trim knob on this panel -- a de-esser has no input stage
      // to set and no makeup to give back, since a band cut takes under a dB
      // of broadband energy (docs/deesser/10-dsp-spec.md 8).
      freqKnob   (context.params.param (Index::freq),   "FREQ",
                  ui::Knob::Style::character, 0.62f, context.def.accent),
      qKnob      (context.params.param (Index::q),      "Q",
                  ui::Knob::Style::character, 0.62f, context.def.accent),
      // THRESH and not THRESHOLD: the full word overflows its caption box by
      // 19.8 px at this cell width, which ui_layout caught on the first build.
      // BMO DEQ's band already prints the short form, so this is the house
      // spelling rather than a squeeze -- the parameter is still named
      // "Threshold" where a host shows it.
      threshKnob (context.params.param (Index::thresh), "THRESH",
                  ui::Knob::Style::character, 0.62f, context.def.accent),
      rangeKnob  (context.params.param (Index::range),  "RANGE",
                  ui::Knob::Style::character, 0.62f, context.def.accent),
      sketch (context.def.accent),
      ribbon (context.def.accent),
      grBar ("GR", ui::LevelBar::Grow::leftward, 0.0f, kMaxReductionDb,
             [this]
             {
                 if (grOverride)
                     return *grOverride;

                 return context.gainReductionDb ? context.gainReductionDb() : 0.0f;
             }),
      bellButton ("BELL"), shelfButton ("SHELF"),
      listenButton ("LISTEN")
{
    for (auto* k : { &freqKnob, &qKnob, &threshKnob, &rangeKnob })
        k->setKnobSide (kKnobSide);

    // THRESH alone prints its value, and it is the one that has to: its
    // number is prominence over the detector's reference, not a level, and
    // "+3.0 dB over" is the only place on the panel or in a host's automation
    // lane that says so. The other three are either drawn in the sketch above
    // them or carry a unit that speaks for itself.
    threshKnob.setShowsValue (true);

    // FREQ prints its value too -- Frosty, 2026-09-21.
    //
    // The sketch above already shows *where* the band is, which was the
    // argument for leaving this off. What a drawing cannot give anyone is the
    // number: the one to write down, to type into the same module on another
    // track, or to compare against the take from last week. A de-esser is
    // tuned to a voice, and the frequency is the setting worth carrying
    // between sessions. The format is Hertz, so it reads "6.50 kHz" and not a
    // bare 6500.
    freqKnob.setShowsValue (true);

    // The printed scale. Positions are amounts of reduction, 0..18 and
    // positive, because that is what the DSP reports; the text is negative,
    // because what the meter means is gain. `ScaleMark` keeps the two apart
    // for exactly this -- deriving the text from the position would print 18
    // where the meter means -18.
    //
    // Hand-placed and opening out toward 0, LTV Comp's GR curve scaled to this
    // module's shorter range. Per-dB density across the marks runs 0.090,
    // 0.070, 0.0575, 0.045 -- monotonic toward 0, which is the property to
    // preserve if one is ever moved. The reason is the same one LTV gives: the
    // difference between 1 and 3 dB of de-essing is worth seeing and the
    // difference between 14 and 18 is not, because by then the ess is gone and
    // the only question left is how much of the consonant went with it.
    grBar.setScale ({ {  0.0f, 0.00f,   "0" }, {  2.0f, 0.18f,  "-2" },
                      {  4.0f, 0.32f,  "-4" }, {  8.0f, 0.55f,  "-8" },
                      { 18.0f, 1.00f, "-18" } });

    // Flat, and in the shared warm token rather than the module's accent.
    //
    // Flat for LTV Comp's reason, which applies harder here: a gradient says
    // "further along is worse", and on a de-esser further along is only
    // further along -- 12 dB off a shouted ess is the module working, not the
    // module in trouble.
    //
    // Not the accent, though the accent is warm and would have looked well:
    // the fill would then be the one colour on the panel that also means
    // "this is BMO Defang", and a bar that fills with the module's identity
    // reads as a brand animation rather than as a reading. meterGrWarm is the
    // token LTV Comp's GR bar already uses, so the two modules' reductions
    // read the same in a rack -- which is the whole argument for a shared
    // meter and it survives the needle being dropped.
    grBar.setFlatColour (ui::tokens().meterGrWarm);

    for (auto* b : { &bellButton, &shelfButton })
    {
        // The shape pair are radios over their state rather than toggles: the
        // click sets it and the state lights the buttons, so host automation
        // and a click cannot disagree.
        b->setClickingTogglesState (false);
        b->setColour (juce::ToggleButton::tickColourId, ui::tokens().switchAlt);
        addAndMakeVisible (b);
    }

    listenButton.setColour (juce::ToggleButton::tickColourId, ui::tokens().switchAlt);
    addAndMakeVisible (listenButton);

    for (auto* c : std::initializer_list<juce::Component*> {
             &sketch, &ribbon, &freqKnob, &qKnob, &threshKnob, &rangeKnob, &grBar })
        addAndMakeVisible (c);

    // The ribbon's tap. Handing it over is what enables it, and ~Ribbon is
    // what switches it off again -- so a session with no BMO Defang window
    // open costs the audio thread one branch a sample and nothing else.
    ribbon.setTap (context.analyser);

    // One timer for the panel, not one per bar. `ui::LevelBar::refresh` is
    // driven from outside for that reason.
    startTimerHz (30);

    shapeAttachment = std::make_unique<juce::ParameterAttachment> (
        context.params.param (Index::shape),
        [this] (float value) { showShape (juce::roundToInt (value)); });

    const auto chooseShape = [this] (int choice)
    {
        return [this, choice] { shapeAttachment->setValueAsCompleteGesture ((float) choice); };
    };

    bellButton .onClick = chooseShape (bell);
    shelfButton.onClick = chooseShape (highShelf);

    // Every parameter the picture is drawn from redraws it, SHAPE included --
    // so SHAPE carries two attachments, one lighting its pair of switches and
    // one redrawing the curve. Two small listeners rather than one callback
    // doing both jobs: the sketch's list is then exactly the four parameters
    // it draws from, which is the thing a reader wants to check.
    {
        const int drawn[] { Index::freq, Index::q, Index::range, Index::shape };

        for (size_t i = 0; i < sketchAttachments.size(); ++i)
            sketchAttachments[i] = std::make_unique<juce::ParameterAttachment> (
                context.params.param (drawn[i]),
                [this] (float) { refreshSketch(); });
    }

    // LISTEN, held. -1 on release, and -1 again in the destructor: a window can
    // close with the mouse still down, and an engine left soloed would stay
    // that way with nothing on screen to clear it.
    listenButton.onHeld = [this] (bool down) { setListening (down); };

    // Light whatever the parameters already say, which is how a render and a
    // reopened editor come up in the state they were left in.
    shapeAttachment->sendInitialUpdate();
    refreshSketch();
}

DeesserPanel::~DeesserPanel()
{
    // Unconditional. Clearing a solo that is already clear costs one relaxed
    // store; leaving one set costs a user their next bounce. BMO DEQ's panel
    // does the same, for the same reason.
    setListening (false);
}

//==============================================================================
void DeesserPanel::timerCallback()
{
    grBar.refresh();
}

void DeesserPanel::showShape (int choice)
{
    bellButton .setToggleState (choice == bell,      juce::dontSendNotification);
    shelfButton.setToggleState (choice == highShelf, juce::dontSendNotification);
}

void DeesserPanel::setListening (bool shouldListen)
{
    if (shouldListen == listening)
        return;

    listening = shouldListen;
    listenButton.setToggleState (shouldListen, juce::dontSendNotification);

    // One band, so 0 is the only index this module ever sends; -1 clears, which
    // is the contract ModuleContext::setSolo states.
    if (context.setSolo)
        context.setSolo (shouldListen ? 0 : -1);
}

void DeesserPanel::refreshSketch()
{
    sketch.setBand (context.params.getReal (Index::freq),
                    context.params.getReal (Index::q),
                    juce::roundToInt (context.params.getReal (Index::shape)),
                    context.params.getReal (Index::range));

    // The ribbon is drawn against RANGE too, so a full-height notch there and
    // a full bar below mean the same thing.
    ribbon.setRangeDb (context.params.getReal (Index::range));
}

bool DeesserPanel::setUiState (const juce::String& key, const juce::String& value)
{
    if (key == "gr")
    {
        // containsOnly rather than trusting getFloatValue, which answers 0 for
        // anything it cannot read -- so "eight" would have parked the bar at
        // no reduction and rendered a plausible lie.
        if (value.isEmpty() || ! value.containsOnly ("0123456789.-+"))
            return false;

        const auto db = value.getFloatValue();

        if (db < 0.0f || db > kMaxReductionDb)
            return false;

        grOverride = db;

        // Straight to the reading rather than letting the ballistics walk
        // there: a render captures one frame, and a bar still on its way up
        // would be a picture of the fall rate.
        grBar.refresh();
        return true;
    }

    if (key == "listen")
    {
        if (value.equalsIgnoreCase ("on"))  { setListening (true);  return true; }
        if (value.equalsIgnoreCase ("off")) { setListening (false); return true; }

        return false;
    }

    return false;
}

//==============================================================================
void DeesserPanel::resized()
{
    // 4, the same content inset every other panel measures from.
    auto area = getLocalBounds().reduced (kPad, 4);

    const auto content = kSketchHeight + kRibbonHeight + kPairRow + kPairRow + kSwitchHeight
                           + kBarRow + kSwitchHeight;

    // Seven divisions for six blocks: a margin above the first and below the
    // last as well as between them, so the spacing stays even if a block's
    // height changes later.
    const auto gap = juce::jmax (kSwitchGap, (area.getHeight() - content) / 7);

    const auto centredRow = [] (juce::Rectangle<int> row, int width)
    {
        return row.withSizeKeepingCentre (width, kSwitchHeight);
    };

    /** A pair of switches centred on one row. */
    const auto layOutPair = [&] (juce::Rectangle<int> row, juce::Button& left, juce::Button& right)
    {
        auto pair = centredRow (row, kPairWidth);
        left .setBounds (pair.removeFromLeft (kSwitchWidth));
        pair.removeFromLeft (kSwitchGap);
        right.setBounds (pair);
    };

    /** Two knobs abreast, each in half the width. */
    const auto layOutKnobs = [] (juce::Rectangle<int> row, ui::PlainKnob& left, ui::PlainKnob& right)
    {
        const auto half = row.getWidth() / 2;
        left .setBounds (row.removeFromLeft (half));
        right.setBounds (row);
    };

    area.removeFromTop (gap);

    // The picture first: all four knobs move it, so it sits over all four
    // rather than beside any one of them.
    sketch.setBounds (area.removeFromTop (kSketchHeight));

    // Directly under the sketch with only a hairline of air, because the two
    // are one instrument: where the band is, and what it has been doing.
    area.removeFromTop (4);
    ribbon.setBounds (area.removeFromTop (kRibbonHeight));
    area.removeFromTop (gap);

    // Where the band is, and how wide.
    layOutKnobs (area.removeFromTop (kPairRow), freqKnob, qKnob);
    area.removeFromTop (gap);

    // How far over the band has to stand, and how deep the cut may go.
    layOutKnobs (area.removeFromTop (kPairRow), threshKnob, rangeKnob);
    area.removeFromTop (gap);

    // Directly under the knobs, because it changes what the picture is showing
    // rather than where the picture is.
    layOutPair (area.removeFromTop (kSwitchHeight), bellButton, shelfButton);
    area.removeFromTop (gap);

    // The GR bar, at the width the switch rows above and below it already use,
    // so the well's ends line up with theirs rather than adding a third edge.
    {
        auto block = area.removeFromTop (kBarRow);
        const auto width = juce::jmin (block.getWidth(), kMeterWidth);

        grBar.setBounds (block.withSizeKeepingCentre (width, kBarRow));
    }
    area.removeFromTop (gap);

    // LISTEN under the bar: what it auditions is what the bar is reading.
    listenButton.setBounds (centredRow (area.removeFromTop (kSwitchHeight), kSwitchWidth));
}

} // namespace bmo::deesser
