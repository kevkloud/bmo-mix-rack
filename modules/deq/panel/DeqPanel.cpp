#include "DeqPanel.h"
#include "modules/deq/dsp/DeqDsp.h"
#include <cstring>

namespace bmo::deq
{

namespace
{
    constexpr int kSwitchH = ui::Tokens::switchHeight;
    constexpr int kSwitchW = ui::Tokens::switchWidth;
    constexpr int kGap     = ui::Tokens::switchGap;

    const juce::StringArray kPlaceLabels { "STEREO", "MID", "SIDE" };

    /** One thing in a strip: what it is, and the box it wants. */
    struct Cell
    {
        juce::Component* component;
        int width, height;
    };

    /** Lays a strip out the way the mockups' flex rows did: each cell centred
        on the row's midline, and the space left over shared out -- around every
        cell (`between` false, CSS space-around) or only between them (`between`
        true, space-between, the ends flush with the row). */
    void spread (juce::Rectangle<int> row, std::initializer_list<Cell> cells, bool between)
    {
        auto total = 0;
        for (const auto& c : cells)
            total += c.width;

        const auto n = (int) cells.size();
        const auto spare = juce::jmax (0, row.getWidth() - total);
        const auto share = between ? (n > 1 ? (float) spare / (float) (n - 1) : 0.0f)
                                   : (float) spare / (float) n;

        auto x = (float) row.getX() + (between ? 0.0f : share * 0.5f);

        for (const auto& c : cells)
        {
            if (c.component != nullptr)
                c.component->setBounds (juce::roundToInt (x), row.getCentreY() - c.height / 2, c.width, c.height);

            x += (float) c.width + share;
        }
    }

    /** A knob cell's height: the knob, its name, its value. */
    int knobHeight (int side, float captionPoints)
    {
        return side + juce::roundToInt (captionPoints * 1.2f) + 4 + juce::roundToInt (11.0f * 1.2f) + 1;
    }

    /** A knob's side and its caption size -- and its cap, which is 0.62 of the
        side where the side can afford it and less where it cannot. The dotted
        track is a fixed 10 px outside the cap (Tokens::trackGap), and with its
        dots it needs 13 px of the side's half, so a 50 px knob at 0.62 ran its
        track off its own edge on the first render of mockup C's timing row. */
    void size (ui::PlainKnob& k, int side, float captionPoints)
    {
        k.setKnobSide (side);
        k.setCaptionSize (captionPoints);
        k.setFaceScale (juce::jmin (0.62f, 1.0f - 26.0f / (float) side));
    }

    /** Mockup C's values: "2.10k", "-2.0", "1.80" -- the unit is the knob's
        name, and at 72 px a cell has no room to say it twice ("+24.0 dB" is
        76 px in the caption face). The full panel keeps the host's text. */
    juce::String shortValue (const juce::String& text)
    {
        if (text.endsWith (" kHz"))
            return text.dropLastCharacters (4) + "k";

        for (const auto* unit : { " Hz", " dB", " ms" })
            if (text.endsWith (unit))
                return text.dropLastCharacters ((int) std::strlen (unit));

        return text;
    }
}

DeqPanel::DeqPanel (ui::ModuleContext ctx)
    : ModulePanel (std::move (ctx)),
      curve (context.params, context.def.accent,
             [this] { return selected; },
             [this] (int b) { selectBand (b); },
             context.sampleRate),
      tabs (kBands,
            [this] (int b) { return context.params.getReal (indexOf (b, Control::on)) > 0.5f; },
            [this] (int b) { return context.params.getReal (indexOf (b, Control::dyn)) > 0.5f
                                  && context.params.getReal (indexOf (b, Control::on)) > 0.5f; },
            [this] (int b) { selectBand (b); },
            [this] (int b) { toggleBand (b); },
            [this] (int b) { if (context.setSolo) context.setSolo (b); },
            [this] (int b) { return placementColour ((int) std::lround (context.params.getReal (indexOf (b, Control::place))),
                                                    context.def.accent); }),
      reduction (context.gainReductionDb),

      // The module's in/out, so it lights in the module's own colour; AUTO is
      // "anything else" (modules/AGENTS.md, "what a switch lights up in").
      active (context.params.param (kActive), "DEQ", context.def.accent),
      autoGain (context.params.param (kAutoGain), "AUTO", ui::tokens().switchAlt),
      output (context.params.param (kOutput), "OUTPUT")
{
    tabs.accent = context.def.accent;
    styleTrimKnob (output);

    // The analyser draws the module's own post-EQ tap. Handing the tap over is
    // what enables it, and ~ResponseView is what disables it again -- so a
    // session with no DEQ window open costs the audio thread nothing, which is
    // the contract `AnalyserTap` is emphatic about.
    curve.setAnalyserTap (context.analyser);

    // The curve's nodes carry the same momentary solo the tabs do.
    curve.onSoloChanged ([this] (int b) { if (context.setSolo) context.setSolo (b); });

    // Open on the first band that is doing something, so a session reopens on
    // its work rather than on band 1.
    for (int b = 0; b < kBands; ++b)
        if (context.params.getReal (indexOf (b, Control::on)) > 0.5f)
        {
            selected = b;
            break;
        }

    tabs.setSelected (selected);

    for (auto* c : std::initializer_list<juce::Component*> { &curve, &tabs, &reduction, &active, &autoGain, &output })
        addAndMakeVisible (c);

    bindBand();

    // Every child's mouse-ups and wheels, for clampShelfQ.
    addMouseListener (this, true);
    startTimerHz (10);
}

DeqPanel::~DeqPanel()
{
    removeMouseListener (this);

    // A solo is held by a mouse button, and a window can close while one is
    // down. Nothing saves solo, so an engine left soloed would stay that way
    // with no panel on screen to release it and nothing in the session to
    // explain it -- which is the support ticket `spec/decisions.md` names as
    // the reason solo is not a parameter in the first place.
    if (context.setSolo)
        context.setSolo (-1);
}

bool DeqPanel::isShowingExpanded() const noexcept
{
    return context.def.isExpandable() && getWidth() >= context.def.expandedWidth;
}

//==============================================================================
void DeqPanel::selectBand (int band)
{
    if (band < 0 || band >= kBands || band == selected)
        return;

    selected = band;
    tabs.setSelected (band);
    bindBand();
    resized();
    repaint();
}

void DeqPanel::toggleBand (int band)
{
    if (band < 0 || band >= kBands)
        return;

    // A host gesture, like every other parameter move on this panel, so it
    // automates and undoes the way turning a knob does.
    const auto index = indexOf (band, Control::on);
    auto& p = context.params.param (index);

    p.beginChangeGesture();
    context.params.setReal (index, context.params.getReal (index) > 0.5f ? 0.0f : 1.0f);
    p.endChangeGesture();

    // The enablement of the strip below follows the band that is selected, and
    // toggling one is the commonest way to change what it should show.
    if (band == selected)
        refreshEnablement();
}

bool DeqPanel::setUiState (const juce::String& key, const juce::String& value)
{
    if (key != "band")
        return false;

    const auto band = value.getIntValue();

    // Refuse what does not parse rather than falling back to band 1: a render
    // labelled band 7 that shows band 1 is worse than no render.
    if (band < 1 || band > kBands || juce::String (band) != value.trim())
        return false;

    selectBand (band - 1);
    return true;
}

void DeqPanel::bindBand()
{
    const auto accent = context.def.accent;
    const auto alt = ui::tokens().switchAlt;

    auto knob = [this, accent] (Control c, const char* caption)
    {
        auto k = std::make_unique<ui::PlainKnob> (bandParam (c), caption, ui::Knob::Style::character, 0.62f, accent);
        k->setShowsValue (true);
        return k;
    };

    shape  = std::make_unique<ShapeDial> (bandParam (Control::shape), context.params.spec (indexOf (selected, Control::shape)), accent);
    dynOn  = std::make_unique<ui::SwitchButton> (bandParam (Control::dyn), "DYN", ui::tokens().dynamicsAccent);

    // COMPRESS or EXPAND, which is the direction and RANGE's sign read
    // together -- see DynamicsMode. Each lights in the colour its half of the
    // GR bar fills in, so the switch, the meter and the knobs below all say
    // the same thing at once.
    mode   = std::make_unique<DynamicsMode> (bandParam (Control::dir),
                                             [this] { return context.params.getReal (indexOf (selected, Control::range)); },
                                             ui::tokens().dynamicsAccent, ui::tokens().dynamicsAccent);
    // Placement offers the two special cases; nothing lit is Stereo, which is
    // what a band does when it has not been asked for anything. Frosty,
    // 2026-09-15 -- see ChoiceRow for the argument.
    //
    // **Each button lights in its own placement colour**, through the same
    // `placementColour` the tabs and the nodes go through -- so a lit MID, a
    // Mid band's tab and its node cannot disagree about what colour Mid is.
    //
    // **In the accent, not switchAlt**, which extends the mono row of the table
    // in modules/AGENTS.md rather than breaking its "anything else" row: MID
    // and SIDE are a summing decision, not a per-channel one, and the table
    // gives mono the module colour for exactly that reason -- it reads as
    // something the module does. White was asked for first and withdrawn: the
    // suite reserves white for polarity, always, and BMO EQ and BMO Util both
    // draw one.
    place  = std::make_unique<ChoiceRow> (bandParam (Control::place), kPlaceLabels,
                                          [accent] (int c) { return placementColour (c, accent); },
                                          false, 0);

    freq    = knob (Control::freq,    "FREQ");
    gain    = knob (Control::gain,    "GAIN");
    q       = knob (Control::q,       "Q");
    thr     = knob (Control::thr,     "THRESH");
    range   = knob (Control::range,   "RANGE");
    ratio   = knob (Control::ratio,   "RATIO");
    attack  = knob (Control::attack,  "ATTACK");
    release = knob (Control::release, "RELEASE");

    for (auto* c : std::initializer_list<juce::Component*> {
             shape.get(), dynOn.get(), mode.get(), place.get(),
             freq.get(), gain.get(), q.get(), thr.get(), range.get(), ratio.get(), attack.get(), release.get() })
        addAndMakeVisible (c);

    refreshEnablement();
}

void DeqPanel::refreshEnablement()
{
    // A cut filter has no gain, and a band without dynamics has nothing for
    // its detector knobs to do: dim them, the way PlainKnob::setKnobEnabled
    // was written for (BMO Dimension's "three dead knobs on a fresh insert").
    const auto shapeNow = DeqDsp::shapeFor ((int) std::lround (context.params.getReal (indexOf (selected, Control::shape))));
    const auto cut = shapeNow == Shape::lowCut || shapeNow == Shape::highCut;
    const auto dynamic = context.params.getReal (indexOf (selected, Control::dyn)) > 0.5f && ! cut;

    if (gain != nullptr) gain->setKnobEnabled (! cut);
    if (mode != nullptr) mode->setModeEnabled (dynamic);

    // **DYN is never dimmed.** Frosty, 2026-09-15: it is one of the three
    // behaviour switches and it is the one that unlocks the strip below, so a
    // dimmed DYN read as a control that could not be reached rather than as the
    // way in. A switch nobody believes they can press is worse than a switch
    // that does nothing on one shape.
    //
    // It is inert on a cut filter, which is the cost of this and is real: a
    // cut has no gain for the dynamics to move, so pressing DYN there lights
    // the switch and changes nothing. The knobs below still dim, so the panel
    // does say the section is asleep -- it just no longer says the door is
    // locked.
    if (dynOn != nullptr) dynOn->setSwitchEnabled (true);

    // **The knobs stay teal.** Placement colours the band.s tab, its node and
    // the MID and SIDE switches, and stops there -- Frosty, 2026-09-15, after
    // seeing it on FREQ, GAIN and Q for a round. The knobs are where the values
    // are read, and three of them changing hue with a switch is motion where
    // none is wanted; the tab and the node carry the identity without it.

    // The dynamics half wears its own colour -- knobs, captions and the mode
    // pair -- so the two halves of the panel are told apart by more than the
    // rule between them. See Tokens::dynamicsAccent, including what it costs.
    for (auto* k : { thr.get(), range.get(), ratio.get(), attack.get(), release.get() })
        if (k != nullptr)
        {
            k->setAccent (ui::tokens().dynamicsAccent);
            k->setKnobEnabled (dynamic);
        }
}

void DeqPanel::clampShelfQ()
{
    const auto shapeChoice = (int) std::lround (context.params.getReal (indexOf (selected, Control::shape)));
    const auto index = indexOf (selected, Control::q);
    const auto now = context.params.getReal (index);
    const auto capped = effectiveQ (shapeChoice, now);

    if (capped < now)
    {
        auto& p = context.params.param (index);
        p.beginChangeGesture();
        context.params.setReal (index, capped);
        p.endChangeGesture();
    }
}

void DeqPanel::mouseUp (const juce::MouseEvent&)
{
    clampPending = true;
}

void DeqPanel::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
{
    clampPending = true;

    // Heard as a listener on the children too. A wheel over one of them has
    // already been handled by it; only one over the bare plate goes on up, as
    // Component's own default would have sent it.
    if (e.eventComponent == this)
        Component::mouseWheelMove (e, wheel);
}

void DeqPanel::timerCallback()
{
    // Shape, DYN and RANGE can be moved by automation or a preset, not only by
    // the controls here. RANGE matters because its **sign** decides whether the
    // band compresses or expands, so turning it through zero re-lights the
    // mode pair and repaints five knob caps with nobody touching a switch.
    if (mode != nullptr)
        mode->refresh();

    refreshEnablement();

    if (clampPending && ! juce::ModifierKeys::currentModifiers.isAnyMouseButtonDown())
    {
        clampPending = false;
        clampShelfQ();
    }
}

//==============================================================================
void DeqPanel::resized()
{
    clearRules();
    auto area = getLocalBounds().reduced (kPad, 4);

    const auto out = takeOutputSection (area);
    addRule (out.rule);

    {
        auto row = out.switches.withSizeKeepingCentre (kSwitchW * 2 + kGap, kSwitchH);
        active.setBounds (row.removeFromLeft (kSwitchW));
        autoGain.setBounds (row.removeFromRight (kSwitchW));
    }
    output.setBounds (out.knob);

    curve.setCompact (! isShowingExpanded());

    for (auto* k : { freq.get(), gain.get(), q.get(), thr.get(), range.get(), ratio.get(), attack.get(), release.get() })
        k->setValueFormat (isShowingExpanded() ? nullptr : shortValue);

    if (isShowingExpanded())
        layoutExpanded (area);
    else
        layoutCompact (area);
}

/*  Mockup C, 320 wide. Content-area y, the mockup's own positions less the
    52 px of header and preset bar above it:

        curve 8-120, tabs 126-185, rule 198, SHAPE/FREQ/GAIN/Q,
        placement + ON, rule, DYN+BELOW/THRESH/RANGE/GR, RATIO/ATTACK/RELEASE,
        and the output section's rule at 566.

    The mockup's second rule was at 384, under an M/S knob the lean band set
    dropped (decisions.md); with numbers under every knob the two dynamics
    strips need the room, so the rule moves up to 360 and they take it. */
void DeqPanel::layoutCompact (juce::Rectangle<int> area)
{
    const auto top = area.getY();
    auto at = [&] (int y0, int y1) { return juce::Rectangle<int> (area.getX(), top + y0 - 4, area.getWidth(), y1 - y0); };

    // 8 px higher than the well it draws, and 8 px wider each side: the extra
    // is ResponseView::plot's inset, so the well lands exactly where mockup C
    // has it while a node at the end of its range still has room to hang over
    // the edge instead of being cut by it.
    curve.setBounds (at (8, 120).expanded (ResponseView::kOverhang, 0)
                                .withTrimmedTop (-ResponseView::kOverhang));

    tabs.setRows (2);
    tabs.setGap (7);
    tabs.setBounds (at (126, 185));

    addRule (at (190, 206));

    {
        const auto knobSide = 62;
        for (auto* k : { freq.get(), gain.get(), q.get() })
            size (*k, knobSide, 11.0f);

        shape->setCaptionSize (11.0f);
        const auto h = knobHeight (knobSide, 11.0f);
        spread (at (206, 302), { { shape.get(), 84, h }, { freq.get(), 72, h }, { gain.get(), 72, h }, { q.get(), 72, h } }, true);
    }

    {
        // MID, SIDE, DYN -- what this band does, as opposed to where it does
        // it. Three switches of 66 and two gaps are 214, centred, which is the
        // block placement alone used to occupy before the ON switch was
        // dropped and DYN came up from the dynamics strip.
        auto row = at (314, 340).withSizeKeepingCentre (66 * 3 + kGap * 2, kSwitchH);
        place->setVertical (false);
        place->setBounds (row.removeFromLeft (66 * 2 + kGap));
        row.removeFromLeft (kGap);
        dynOn->setBounds (row.removeFromLeft (66));
    }

    addRule (at (352, 368));

    {
        const auto knobSide = 60;
        for (auto* k : { thr.get(), range.get() })
            size (*k, knobSide, 11.0f);

        reduction.setShowsValue (false);
        const auto h = knobHeight (knobSide, 11.0f);

        // COMPRESS over EXPAND, where BELOW was.
        mode->setVertical (true);
        spread (at (370, 466), { { mode.get(), kSwitchW, kSwitchH * 2 + kGap }, { thr.get(), 76, h }, { range.get(), 76, h },
                                 { &reduction, 36, reduction.heightFor (62) } }, true);
    }

    {
        const auto knobSide = 56;
        for (auto* k : { ratio.get(), attack.get(), release.get() })
            size (*k, knobSide, 11.0f);

        const auto h = knobHeight (knobSide, 11.0f);
        spread (at (468, 556), { { ratio.get(), 90, h }, { attack.get(), 90, h }, { release.get(), 90, h } }, false);
    }
}

/*  Mockup A, 600 wide. Content-area y as above:

        curve 8-222 (the well 196, its axis 18), tabs 230-256 (twelve of 40 at
        the switch gap, centred), rule 270, the EQ strip, rule 416, the
        dynamics strip, and the output section's rule at 566.

    The mockup's last EQ cell was an M/S amount, which the lean band set
    dropped; the band's ON switch takes its place. */
void DeqPanel::layoutExpanded (juce::Rectangle<int> area)
{
    const auto top = area.getY();
    auto at = [&] (int y0, int y1) { return juce::Rectangle<int> (area.getX(), top + y0 - 4, area.getWidth(), y1 - y0); };

    // 8 px higher and 8 px wider each side than the well -- see layoutCompact.
    curve.setBounds (at (8, 222).expanded (ResponseView::kOverhang, 0)
                                .withTrimmedTop (-ResponseView::kOverhang));

    tabs.setRows (1);
    tabs.setGap (kGap);
    tabs.setBounds (at (230, 256).withSizeKeepingCentre (tabs.widthFor (40), kSwitchH));

    addRule (at (262, 278));

    {
        const auto knobSide = 77;
        for (auto* k : { freq.get(), gain.get(), q.get() })
            size (*k, knobSide, 15.0f);

        shape->setCaptionSize (11.0f);
        const auto h = knobHeight (knobSide, 15.0f);
        place->setVertical (true);

        // MID over SIDE over DYN, in the one cell the three-high placement
        // stack used to fill. Laid out empty and filled after, the same way the
        // dynamics strip does it: three switches, two of them one parameter.
        juce::Component bandCell;
        spread (at (280, 406), { { shape.get(), 110, 108 }, { freq.get(), 84, h }, { gain.get(), 84, h }, { q.get(), 84, h },
                                 { &bandCell, kSwitchW, kSwitchH * 3 + kGap * 2 } }, false);

        auto cell = bandCell.getBounds();
        place->setBounds (cell.removeFromTop (kSwitchH * 2 + kGap));
        cell.removeFromTop (kGap);
        dynOn->setBounds (cell.removeFromTop (kSwitchH));
    }

    addRule (at (408, 424));

    {
        const auto big = 65, small = 58;
        for (auto* k : { thr.get(), range.get() })
            size (*k, big, 15.0f);
        for (auto* k : { ratio.get(), attack.get(), release.get() })
            size (*k, small, 11.0f);

        reduction.setShowsValue (true);
        const auto hb = knobHeight (big, 15.0f), hs = knobHeight (small, 11.0f);
        const auto row = at (426, 556);

        // Cells as wide as their words in the suite's caption face, which
        // sets THRESH at 15 pt 94 px wide and RELEASE at 11 pt 78.
        //
        // The meter's cell is the one that was not. 36 px was the 12 px bar
        // and a margin; it drew "-12." for the module's whole life.
        //
        // 49, which is "+24.0" -- the widest of the two ends the bar can now
        // report, and 2.8 px wider than "-24.0". The plus being the wider
        // glyph is why `widestValue` measures both rather than taking the
        // obvious one: this cell was 46 for exactly as long as it took the bar
        // to learn a second sign. checkDeqPanel asserts it at both widths.
        // COMPRESS over EXPAND, as on the compact panel.
        mode->setVertical (true);
        spread (row, { { mode.get(), kSwitchW, kSwitchH * 2 + kGap }, { thr.get(), 96, hb }, { range.get(), 88, hb },
                       { ratio.get(), 80, hs }, { attack.get(), 80, hs }, { release.get(), 80, hs },
                       { &reduction, 49, reduction.heightFor (92) } }, false);
    }
}

} // namespace bmo::deq
