#include "TunePanel.h"
#include "modules/tune/Module.h"
#include "modules/tune/dsp/Scale.h"
#include "modules/tune/params.h"

namespace bmo::tune
{

namespace
{
    //== Layout, in panel-local pixels ========================================
    //
    // The studies' coordinates less the 52 px of header and preset strip the
    // product puts above the panel. Written out rather than derived, as the
    // studies were: it is a drawn layout, and every number here can be read
    // off design/panel-studies-round-7.html.

    constexpr int kSwitchW = ui::Tokens::switchWidth;
    constexpr int kSwitchH = ui::Tokens::switchHeight;
    constexpr int kGap     = ui::Tokens::switchGap;

    /** A field's label sits this far above its control: 9.5 pt and 4.5 of air. */
    constexpr int kLabelRow = 14;

    // Round 7 had a mode banner across the top and Latency under Key. Both
    // went with HYBRID and Studio (2026-09-11), so the target section moves
    // up by the banner's 32 px and Key centres on the column beside it.
    const juce::Rectangle<int> kKeyboard { 20, 20, 320, 70 };

    // Scale, Pitch Range and Ref A down the right, a field every 48 px.
    constexpr int kBoxW = 134;
    const juce::Rectangle<int> kScaleBox { 206, 122, kBoxW, kSwitchH };
    const juce::Rectangle<int> kRangeBox { 206, 170, kBoxW, kSwitchH };
    const juce::Rectangle<int> kRefBox   { 206, 218, kBoxW, kSwitchH };

    // Key, with its accidentals stacked beside it, centred on that column.
    const juce::Rectangle<int> kKeyBox { 20, 153, 92, 60 };
    const juce::Rectangle<int> kSharp  { 120, 153, kSwitchW, kSwitchH };
    const juce::Rectangle<int> kFlat   { 120, 153 + kSwitchH + kGap, kSwitchW, kSwitchH };

    /** The rule under the target section: drawn at y 272. */
    const juce::Rectangle<int> kRule { 0, 264, TunePanel::kDesignWidth, ui::ModulePanel::kRuleRow };

    // How it corrects. Retune is what this plugin is for (Frosty: "centered
    // around hard tuning or retune speed"), so it leads: the big knob directly
    // under the rule with its millisecond value beneath it, then Vibrato and
    // Relax as a row of modifiers at the foot.
    //
    // Round 7's clock -- Retune low, the two smalls at 10 and 2 -- was
    // replaced on 2026-09-16 (Frosty). The clock left 110 px of bare plate
    // under the rule and could not be grown out of it: the three knobs are
    // bound by the panel's 360 px width, not its height, and at face 132 the
    // smalls collide with Retune. Reordering was the lever, not sizing.
    // Frosty's calls, same day: Retune at the top of the stack, and the
    // modifiers a size down at 48 so they read as subordinate to it.
    const juce::Point<int> kClock { 180, 371 };
    constexpr int kClockX = 118, kClockY = -214;
    constexpr int kRetuneFace = 112, kSmallFace = 48;

    /** The knob's square: room for the face, its dotted track 10 px out, and
        the plus and minus on the track. */
    int knobSide (int face) { return face + 36; }

    const juce::String kSharpGlyph (juce::CharPointer_UTF8 ("\xe2\x99\xaf"));
    const juce::String kFlatGlyph  (juce::CharPointer_UTF8 ("\xe2\x99\xad"));

    constexpr const char* kNaturals = "CDEFGAB";

    struct RefChoice { float hz; const char* note; };
    constexpr RefChoice kRefs[] = {
        { 440.0f, "Standard" }, { 442.0f, "Orchestral" }, { 443.0f, "Orchestral" },
        { 444.0f, "" }, { 432.0f, "Verdi" }, { 415.0f, "Baroque" } };

    constexpr const char* kRangeSpans[] = {
        "80-1400 Hz", "160-1400 Hz", "100-1000 Hz", "55-500 Hz", "55-1760 Hz" };

    juce::Font smallLabelFont() { return ui::captionFont (9.5f); }

    /** The menus' look: the suite's greys, with the chosen item lit in the
        product's colour like every other selector on the panel. */
    struct MenuLook final : juce::LookAndFeel_V4
    {
        void refresh()
        {
            const auto& t = ui::tokens();
            setColour (juce::PopupMenu::backgroundColourId, t.well);
            setColour (juce::PopupMenu::textColourId, t.text1);
            setColour (juce::PopupMenu::headerTextColourId, t.text2);
            setColour (juce::PopupMenu::highlightedBackgroundColourId, kAccent);
            setColour (juce::PopupMenu::highlightedTextColourId, ui::onAccentOf (kAccent));
        }

        juce::Font getPopupMenuFont() override { return ui::labelFont (13.0f); }
    };
}

//==============================================================================
/** A box that shows a parameter's value and opens a menu of the others --
    Key, Scale, Pitch Range, Ref A. A JUCE PopupMenu shown against it already
    flips upward and stays on the display the window is on, which is the
    behaviour the studies asked for. */
class TunePanel::ValueBox final : public juce::Component
{
public:
    ValueBox (juce::String boxId, float size, std::function<juce::String()> textSource)
        : id (std::move (boxId)), textSize (size), text (std::move (textSource))
    {
        setName (id);
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
    }

    std::function<void()> onClick;
    juce::String id;
    float textSize;
    std::function<juce::String()> text;
    bool open = false;

    juce::Rectangle<float> textArea() const
    {
        return getLocalBounds().toFloat().reduced (10.0f, 0.0f).withTrimmedRight (14.0f);
    }

    float overflow() const
    {
        return juce::GlyphArrangement::getStringWidth (ui::labelFont (textSize), text()) - textArea().getWidth();
    }

    void paint (juce::Graphics& g) override
    {
        const auto& t = ui::tokens();
        const auto bounds = getLocalBounds().toFloat();

        g.setColour (hover ? t.well.interpolatedWith (t.text2, 0.12f) : t.well);
        g.fillRoundedRectangle (bounds, 5.0f);

        ui::drawLabel (g, text(), textArea(), juce::Justification::centredLeft,
                       ui::labelFont (textSize), t.text1);

        // The chevron: points down, or up while the menu is open.
        const auto c = juce::Point<float> (bounds.getRight() - 13.0f, bounds.getCentreY());
        juce::Path chev;
        if (open) chev.addTriangle (c.x - 4.0f, c.y + 2.5f, c.x + 4.0f, c.y + 2.5f, c.x, c.y - 2.5f);
        else      chev.addTriangle (c.x - 4.0f, c.y - 2.5f, c.x + 4.0f, c.y - 2.5f, c.x, c.y + 2.5f);
        g.setColour (t.text1.withAlpha (0.75f));
        g.fillPath (chev);
    }

    void mouseDown (const juce::MouseEvent&) override { if (onClick) onClick(); }
    void mouseEnter (const juce::MouseEvent&) override { hover = true;  repaint(); }
    void mouseExit  (const juce::MouseEvent&) override { hover = false; repaint(); }

private:
    bool hover = false;
};

//==============================================================================
/** The twelve notes, lit where the chosen key and scale allow them. Clicking
    a note of the scale switches it out, and back; notes outside the scale are
    drawn faded and do nothing. The dot marks the root. */
class TunePanel::Keyboard final : public juce::Component
{
public:
    explicit Keyboard (TunePanel& p) : panel (p)
    {
        setName ("Allowed notes");
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
    }

    void paint (juce::Graphics& g) override
    {
        const auto& t = ui::tokens();
        const auto dark = ui::isDarkMode();

        // Not tokens of the suite's -- no module has a keyboard -- so derived
        // from tokens, per appearance, as the studies drew them: white keys
        // are the lightest surface on the plate, black keys the darkest.
        // An instrument graphic rather than a control surface: white keys are
        // white in both appearances (`knobTint` has no dark variant, the way
        // `meterFace` has none) and a black key is the darkest thing the
        // appearance offers, so it reads as black rather than as dimmed.
        const auto whiteKey = t.knobTint;
        const auto blackKey = dark ? t.well : t.meterFace;

        const auto mask = scaleMask();
        const auto root = pitchClassOfKey (panel.choiceOf (Index::key));

        auto draw = [&] (int note, juce::Rectangle<float> r, juce::Colour base)
        {
            const bool inScale = (mask >> note) & 1u;
            const bool allowed = panel.context.params.getReal (Index::noteC + note) >= 0.5f;
            const bool lit = inScale && allowed;

            // Faded toward the plate, and opaque: a see-through black key let
            // the lit white keys under it show through as an olive ghost.
            const auto fade = [&] (juce::Colour c) { return inScale ? c : c.interpolatedWith (t.plate, 0.65f); };

            const auto fill = fade (base);
            g.setColour (fill);
            g.fillRoundedRectangle (r, 2.5f);
            g.setColour (fade (t.outline));
            g.drawRoundedRectangle (r.reduced (0.5f), 2.5f, 1.0f);

            // In the scale: the accent as a band at the foot of the key. The
            // key keeps its own colour, so the accent has two grounds to read
            // on -- raw lime is 9.10:1 on a black key and 1.29:1 on a white
            // one -- and `accentTextOn` steps it off whichever it lands on.
            const auto onKey = ui::accentTextOn (kAccent, fill);

            if (lit)
            {
                g.setColour (onKey);
                g.fillRoundedRectangle (juce::Rectangle<float> (r.getWidth() - 7.0f, 5.0f)
                                            .withCentre ({ r.getCentreX(), r.getBottom() - 6.5f }), 1.5f);
            }

            if (note == root)
            {
                g.setColour (lit ? onKey : fade (t.text2));
                g.fillEllipse (juce::Rectangle<float> (5.6f, 5.6f).withCentre ({ r.getCentreX(), r.getBottom() - 18.0f }));
            }

            // Switched out of the scale: a bar where a lit key would be.
            if (inScale && ! allowed)
            {
                g.setColour (t.text2);
                g.fillRect (juce::Rectangle<float> (r.getWidth() * 0.4f, 1.6f)
                                .withCentre ({ r.getCentreX(), r.getBottom() - 14.0f }));
            }
        };

        for (int i = 0; i < 7; ++i)
            draw (kWhites[i], whiteRect (i), whiteKey);

        for (const auto& b : kBlacks)
            draw (b.note, blackRect (b.after), blackKey);
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        const auto note = noteAt (e.position);

        if (note < 0 || ((scaleMask() >> note) & 1u) == 0)
            return;

        const auto index = Index::noteC + note;
        panel.setReal (index, panel.context.params.getReal (index) >= 0.5f ? 0.0f : 1.0f);
    }

private:
    static constexpr int kWhites[7] = { 0, 2, 4, 5, 7, 9, 11 };
    struct Black { int note, after; };
    static constexpr Black kBlacks[5] = { { 1, 0 }, { 3, 1 }, { 6, 3 }, { 8, 4 }, { 10, 5 } };

    float keyWidth() const { return (float) getWidth() / 7.0f; }

    juce::Rectangle<float> whiteRect (int i) const
    {
        return { (float) i * keyWidth() + 1.0f, 0.0f, keyWidth() - 2.0f, (float) getHeight() };
    }

    juce::Rectangle<float> blackRect (int after) const
    {
        const auto w = keyWidth() * 0.62f;
        return { (float) (after + 1) * keyWidth() - w * 0.5f, 0.0f, w, (float) getHeight() * 0.6f };
    }

    int noteAt (juce::Point<float> p) const
    {
        for (const auto& b : kBlacks)
            if (blackRect (b.after).contains (p))
                return b.note;

        for (int i = 0; i < 7; ++i)
            if (whiteRect (i).contains (p))
                return kWhites[i];

        return -1;
    }

    NoteMask scaleMask() const
    {
        return tune::scaleMask ((ScaleType) panel.choiceOf (Index::scale),
                                pitchClassOfKey (panel.choiceOf (Index::key)));
    }

    TunePanel& panel;
};

//==============================================================================
/** The ♯ or ♭ on its switch, drawn over it.

    The panel face has neither glyph, so set as a switch's label they came
    back from a fallback face at the switch's 12 pt -- a speck. This sits on
    the switch, lets clicks through, and draws the glyph at a size that reads,
    in the ink the suite's look and feel would give the switch's label. */
class AccidentalGlyph final : public juce::Component
{
public:
    AccidentalGlyph (juce::ToggleButton& b, juce::String g) : button (b), glyph (std::move (g))
    {
        setInterceptsMouseClicks (false, false);
    }

    void paint (juce::Graphics& g) override
    {
        const auto fill = button.getToggleState() ? kAccent : ui::tokens().switchOff;
        const auto ink = ui::onAccentOf (fill).withAlpha (button.isEnabled() ? 1.0f : 0.4f);

        g.setColour (ink);
        g.setFont (juce::Font (juce::FontOptions (21.0f)));
        g.drawText (glyph, getLocalBounds().translated (0, -1), juce::Justification::centred, false);
    }

private:
    juce::ToggleButton& button;
    juce::String glyph;
};

//==============================================================================
TunePanel::TunePanel (ui::ModuleContext ctx)
    : ModulePanel (std::move (ctx)),
      retune  (context.params.param (Index::retuneMs), {}, ui::Knob::Style::character,
               (float) kRetuneFace / (float) knobSide (kRetuneFace), kAccent),
      vibrato (context.params.param (Index::vibrato), {}, ui::Knob::Style::character,
               (float) kSmallFace / (float) knobSide (kSmallFace), kAccent),
      flex    (context.params.param (Index::flex),    {}, ui::Knob::Style::character,
               (float) kSmallFace / (float) knobSide (kSmallFace), kAccent)
{
    menuLook = std::make_unique<MenuLook>();

    for (auto& k : knobPlaces())
    {
        // The caption is the panel's to draw (see the class comment), so the
        // knob gets its name here, for accessibility and the layout test.
        k.knob->setName (k.caption);
        addAndMakeVisible (k.knob);
    }

    // RETUNE rests at its minimum, so its rest dot lands on the minus. The
    // shared clearance test drops a dot that fuses with an end symbol, but it
    // is a pixel gap turned into an angle, so this face is large enough to
    // clear it by a fraction and draw both -- which reads as a doubled minus.
    // No rest mark at all is Frosty's call, 2026-09-16: the pointer already
    // sits at the end when the panel opens. Vibrato and Relax default to an
    // end too and are small enough that the shared test already hides theirs.
    retune.setRestMark (false);

    // The accidentals light in the product's colour, like every selector.
    // Clicking sets a value rather than toggling, so a click and host
    // automation land in the same place; sync() reads the states back.
    for (auto* b : { &sharpButton, &flatButton })
    {
        b->setClickingTogglesState (false);
        b->setColour (juce::ToggleButton::tickColourId, kAccent);
        addAndMakeVisible (b);
    }

    // No label text: the glyph is drawn over the switch (AccidentalGlyph).
    sharpButton.setName ("Sharp");
    flatButton.setName ("Flat");
    sharpButton.setTitle ("Sharp");
    flatButton.setTitle ("Flat");
    sharpGlyph = std::make_unique<AccidentalGlyph> (sharpButton, kSharpGlyph);
    flatGlyph  = std::make_unique<AccidentalGlyph> (flatButton, kFlatGlyph);
    sharpButton.addAndMakeVisible (*sharpGlyph);
    flatButton.addAndMakeVisible (*flatGlyph);

    // Clicking the lit accidental returns to the natural.
    auto accidental = [this] (int direction)
    {
        const auto s = spellingOf (choiceOf (Index::key));
        const Spelling next { s.natural, s.accidental == direction ? 0 : direction };

        if (hasSpelling (next))
            setChoice (Index::key, keyChoiceOf (next));
    };

    sharpButton.onClick = [accidental] { accidental (+1); };
    flatButton .onClick = [accidental] { accidental (-1); };

    keyBox   = std::make_unique<ValueBox> ("Key", 28.0f, [this] { return keyText(); });
    scaleBox = std::make_unique<ValueBox> ("Scale", 13.0f, [this]
               { return juce::String (specs()[(size_t) Index::scale].text ((float) choiceOf (Index::scale))); });
    rangeBox = std::make_unique<ValueBox> ("Pitch Range", 13.0f, [this]
               { return juce::String (specs()[(size_t) Index::range].text ((float) choiceOf (Index::range))); });
    refBox   = std::make_unique<ValueBox> ("Ref A", 13.0f, [this] { return refText(); });

    keyBox  ->onClick = [this] { showKeyMenu(); };
    scaleBox->onClick = [this] { showScaleMenu(); };
    rangeBox->onClick = [this] { showRangeMenu(); };
    refBox  ->onClick = [this] { showRefMenu(); };

    for (auto* b : { keyBox.get(), scaleBox.get(), rangeBox.get(), refBox.get() })
        addAndMakeVisible (b);

    keyboard = std::make_unique<Keyboard> (*this);
    addAndMakeVisible (*keyboard);

    // Ref A's Custom… entry: typed into the box itself, 380 to 480 Hz.
    refEditor.setInputRestrictions (6, "0123456789.");
    refEditor.setJustification (juce::Justification::centredLeft);
    refEditor.setIndents (10, 0);
    refEditor.setFont (ui::labelFont (13.0f));
    refEditor.onReturnKey = [this] { commitCustomRef(); };
    refEditor.onFocusLost = [this] { commitCustomRef(); };
    refEditor.onEscapeKey = [this] { refEditor.setVisible (false); refBox->setVisible (true); };
    addChildComponent (refEditor);

    sync (true);
    startTimerHz (15);
}

TunePanel::~TunePanel()
{
    stopTimer();
}

//==============================================================================
std::vector<TunePanel::KnobPlace> TunePanel::knobPlaces()
{
    return {
        { &retune,  "RETUNE",  kClock,                                         kRetuneFace, 18.0f },
        { &vibrato, "VIBRATO", kClock + juce::Point<int> (-kClockX, -kClockY), kSmallFace,  15.0f },
        { &flex,    "RELAX",   kClock + juce::Point<int> ( kClockX, -kClockY), kSmallFace,  15.0f },
    };
}

void TunePanel::resized()
{
    clearRules();
    addRule (kRule);

    keyboard->setBounds (kKeyboard);

    keyBox->setBounds (kKeyBox);
    sharpButton.setBounds (kSharp);
    flatButton.setBounds (kFlat);
    sharpGlyph->setBounds (sharpButton.getLocalBounds());
    flatGlyph->setBounds (flatButton.getLocalBounds());

    scaleBox->setBounds (kScaleBox);
    rangeBox->setBounds (kRangeBox);
    refBox->setBounds (kRefBox);
    refEditor.setBounds (kRefBox);

    // A knob's square is centred on its place in the clock; the caption row
    // PlainKnob keeps under it stays empty, since the panel draws captions.
    for (auto& k : knobPlaces())
    {
        const auto side = knobSide (k.face);
        k.knob->setBounds (k.centre.x - side / 2, k.centre.y - side / 2, side, side + 22);
    }
}

void TunePanel::paintPanel (juce::Graphics& g)
{
    const auto& t = ui::tokens();

    auto label = [&] (const char* text, int x, int y, juce::Justification j = juce::Justification::centredLeft, int w = 200)
    {
        const auto area = j == juce::Justification::centred
                            ? juce::Rectangle<float> ((float) (x - w / 2), (float) y, (float) w, 10.0f)
                            : juce::Rectangle<float> ((float) x, (float) y, (float) w, 10.0f);
        ui::drawLabel (g, text, area, j, smallLabelFont(), t.text2);
    };

    label ("KEY",         kKeyBox.getX(),   kKeyBox.getY()   - kLabelRow);
    label ("SCALE",       kScaleBox.getX(), kScaleBox.getY() - kLabelRow);
    label ("PITCH RANGE", kRangeBox.getX(), kRangeBox.getY() - kLabelRow);
    label ("REF A",       kRefBox.getX(),   kRefBox.getY()   - kLabelRow);

    // Knob captions, under each face in the product's ink -- accentInk, so
    // they read on both plates.
    const auto ink = ui::accentInk (kAccent);

    for (auto& k : knobPlaces())
    {
        const auto track = (float) k.face * 0.5f + ui::Tokens::trackGap;
        const auto top = (float) k.centre.y + track + 4.0f;
        ui::drawLabel (g, k.caption, { (float) k.centre.x - 70.0f, top, 140.0f, k.captionSize * 1.2f },
                       juce::Justification::centredTop, ui::captionFont (k.captionSize), ink);
    }

    // Retune Speed in milliseconds, under its caption (Frosty, 2026-09-11:
    // "display ms"). The number is what a session is set by and what the
    // other tuners print, so it is on the panel rather than only in the host.
    ui::drawLabel (g, retuneText(), retuneReadoutArea(), juce::Justification::centredTop,
                   ui::labelFont (kRetuneReadoutSize), t.text1);
}

juce::String TunePanel::retuneText() const
{
    return juce::String (specs()[(size_t) Index::retuneMs].text ((float) choiceOf (Index::retuneMs)));
}

juce::Rectangle<float> TunePanel::retuneReadoutArea()
{
    return { 60.0f, 471.0f, 240.0f, kRetuneReadoutSize * 1.3f };
}

TunePanel::BoxText TunePanel::retuneReadout()
{
    return { "Retune Speed", retuneText(),
             juce::GlyphArrangement::getStringWidth (ui::labelFont (kRetuneReadoutSize), retuneText())
                 - retuneReadoutArea().getWidth() };
}

//==============================================================================
void TunePanel::timerCallback()
{
    sync (false);
}

void TunePanel::sync (bool force)
{
    std::array<float, 32> now {};
    for (int i = 0; i < Index::count; ++i)
        now[(size_t) i] = context.params.getReal (i);

    if (! force && now == shown)
        return;

    shown = now;

    // The accidentals: lit for the one the key has, and disabled where the
    // letter has no such key -- E♯, B♯, C♭ and F♭ are not in the list.
    const auto s = spellingOf (choiceOf (Index::key));
    sharpButton.setToggleState (s.accidental > 0, juce::dontSendNotification);
    flatButton .setToggleState (s.accidental < 0, juce::dontSendNotification);
    sharpButton.setEnabled (hasSpelling ({ s.natural, +1 }));
    flatButton .setEnabled (hasSpelling ({ s.natural, -1 }));

    repaint();
}

std::vector<juce::Component*> TunePanel::controlsFor (int index)
{
    switch (index)
    {
        case Index::retuneMs:     return { &retune };
        case Index::key:          return { keyBox.get(), &sharpButton, &flatButton };
        case Index::scale:        return { scaleBox.get() };
        case Index::range:        return { rangeBox.get() };
        case Index::vibrato:      return { &vibrato };
        case Index::flex:         return { &flex };
        case Index::refA:         return { refBox.get() };
        default:                  break;
    }

    if (index >= Index::noteC && index <= Index::noteB)
        return { keyboard.get() };

    return {};
}

std::vector<juce::ToggleButton*> TunePanel::switches()
{
    return { &sharpButton, &flatButton };
}

std::vector<TunePanel::BoxText> TunePanel::boxTexts()
{
    std::vector<BoxText> out;
    for (auto* b : { keyBox.get(), scaleBox.get(), rangeBox.get(), refBox.get() })
        out.push_back ({ b->id, b->text(), b->overflow() });
    return out;
}

//==============================================================================
int TunePanel::choiceOf (int index) const
{
    return juce::roundToInt (context.params.getReal (index));
}

void TunePanel::setChoice (int index, int choice)
{
    setReal (index, (float) choice);
}

void TunePanel::setReal (int index, float value)
{
    auto& p = context.params.param (index);
    p.beginChangeGesture();
    p.setValueNotifyingHost (p.convertTo0to1 (value));
    p.endChangeGesture();
    sync (true);
}

//==============================================================================
TunePanel::Spelling TunePanel::spellingOf (int keyChoice)
{
    const juce::String name (kKeySpellings[juce::jlimit (0, kNumKeySpellings - 1, keyChoice)]);
    const auto natural = juce::String (kNaturals).indexOfChar (name[0]);
    const auto accidental = name.length() < 2 ? 0 : (name[1] == '#' ? 1 : -1);
    return { juce::jmax (0, natural), accidental };
}

int TunePanel::keyChoiceOf (Spelling s)
{
    for (int k = 0; k < kNumKeySpellings; ++k)
    {
        const auto candidate = spellingOf (k);
        if (candidate.natural == s.natural && candidate.accidental == s.accidental)
            return k;
    }

    return -1;
}

juce::String TunePanel::keyText() const
{
    const auto s = spellingOf (choiceOf (Index::key));
    return juce::String::charToString ((juce::juce_wchar) kNaturals[s.natural])
         + (s.accidental > 0 ? kSharpGlyph : s.accidental < 0 ? kFlatGlyph : juce::String());
}

juce::String TunePanel::refText() const
{
    const auto hz = context.params.getReal (Index::refA);
    const auto whole = std::abs (hz - std::round (hz)) < 0.05f;
    return (whole ? juce::String (juce::roundToInt (hz)) : juce::String (hz, 1)) + " Hz";
}

//==============================================================================
void TunePanel::showKeyMenu()
{
    auto* look = static_cast<MenuLook*> (menuLook.get());
    look->refresh();

    const auto current = spellingOf (choiceOf (Index::key));
    juce::PopupMenu menu;
    menu.setLookAndFeel (look);

    for (int n = 0; n < 7; ++n)
        menu.addItem (juce::PopupMenu::Item (juce::String::charToString ((juce::juce_wchar) kNaturals[n]))
                          .setID (n + 1).setTicked (n == current.natural));

    keyBox->open = true;
    keyBox->repaint();

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (keyBox.get())
                                                 .withMinimumWidth (keyBox->getWidth()),
                        [safe = juce::Component::SafePointer<TunePanel> (this)] (int result)
    {
        if (safe == nullptr)
            return;

        safe->keyBox->open = false;
        safe->keyBox->repaint();

        if (result <= 0)
            return;

        // Keep the accidental where the new letter has it; drop it where it
        // does not (E♯, B♯, C♭, F♭).
        const auto s = spellingOf (safe->choiceOf (Index::key));
        Spelling next { result - 1, s.accidental };
        if (! hasSpelling (next))
            next.accidental = 0;

        safe->setChoice (Index::key, keyChoiceOf (next));
    });
}

void TunePanel::showScaleMenu()
{
    auto* look = static_cast<MenuLook*> (menuLook.get());
    look->refresh();

    const auto& spec = specs()[(size_t) Index::scale];
    juce::PopupMenu menu;
    menu.setLookAndFeel (look);

    for (int i = 0; i < spec.numChoices(); ++i)
        menu.addItem (juce::PopupMenu::Item (spec.choices[(size_t) i]).setID (i + 1)
                          .setTicked (i == choiceOf (Index::scale)));

    scaleBox->open = true;
    scaleBox->repaint();

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (scaleBox.get())
                                                 .withMinimumWidth (scaleBox->getWidth()),
                        [safe = juce::Component::SafePointer<TunePanel> (this)] (int result)
    {
        if (safe == nullptr)
            return;

        safe->scaleBox->open = false;
        safe->scaleBox->repaint();

        if (result > 0)
            safe->setChoice (Index::scale, result - 1);
    });
}

void TunePanel::showRangeMenu()
{
    auto* look = static_cast<MenuLook*> (menuLook.get());
    look->refresh();

    const auto& spec = specs()[(size_t) Index::range];
    juce::PopupMenu menu;
    menu.setLookAndFeel (look);

    // Each range's span in the shortcut column, right-aligned: what the
    // choice actually does, where it can be read without widening the name.
    for (int i = 0; i < spec.numChoices(); ++i)
    {
        juce::PopupMenu::Item item (spec.choices[(size_t) i]);
        item.setID (i + 1).setTicked (i == choiceOf (Index::range));
        item.shortcutKeyDescription = kRangeSpans[i];
        menu.addItem (item);
    }

    rangeBox->open = true;
    rangeBox->repaint();

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (rangeBox.get())
                                                 .withMinimumWidth (rangeBox->getWidth()),
                        [safe = juce::Component::SafePointer<TunePanel> (this)] (int result)
    {
        if (safe == nullptr)
            return;

        safe->rangeBox->open = false;
        safe->rangeBox->repaint();

        if (result > 0)
            safe->setChoice (Index::range, result - 1);
    });
}

void TunePanel::showRefMenu()
{
    auto* look = static_cast<MenuLook*> (menuLook.get());
    look->refresh();

    const auto hz = context.params.getReal (Index::refA);
    constexpr int kCustom = 100;
    bool listed = false;

    juce::PopupMenu menu;
    menu.setLookAndFeel (look);

    for (int i = 0; i < (int) std::size (kRefs); ++i)
    {
        const auto chosen = std::abs (hz - kRefs[i].hz) < 0.05f;
        listed = listed || chosen;

        juce::PopupMenu::Item item (juce::String (juce::roundToInt (kRefs[i].hz)) + " Hz");
        item.setID (i + 1).setTicked (chosen);
        item.shortcutKeyDescription = kRefs[i].note;
        menu.addItem (item);
    }

    menu.addSeparator();
    menu.addItem (juce::PopupMenu::Item (juce::String (juce::CharPointer_UTF8 ("Custom\xe2\x80\xa6")))
                      .setID (kCustom).setTicked (! listed));

    refBox->open = true;
    refBox->repaint();

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (refBox.get())
                                                 .withMinimumWidth (refBox->getWidth()),
                        [safe = juce::Component::SafePointer<TunePanel> (this)] (int result)
    {
        if (safe == nullptr)
            return;

        safe->refBox->open = false;
        safe->refBox->repaint();

        if (result == kCustom)
            safe->beginCustomRef();
        else if (result > 0)
            safe->setReal (Index::refA, kRefs[result - 1].hz);
    });
}

void TunePanel::beginCustomRef()
{
    const auto& t = ui::tokens();
    refEditor.setColour (juce::TextEditor::backgroundColourId, t.well);
    refEditor.setColour (juce::TextEditor::textColourId, t.text1);
    refEditor.setColour (juce::TextEditor::outlineColourId, kAccent);
    refEditor.setColour (juce::TextEditor::focusedOutlineColourId, kAccent);
    refEditor.setText (refText().upToFirstOccurrenceOf (" ", false, false), false);

    refBox->setVisible (false);
    refEditor.setVisible (true);
    refEditor.grabKeyboardFocus();
    refEditor.selectAll();
}

void TunePanel::commitCustomRef()
{
    if (! refEditor.isVisible())
        return;

    const auto text = refEditor.getText().trim();
    refEditor.setVisible (false);
    refBox->setVisible (true);

    if (text.isNotEmpty() && text.containsOnly ("0123456789."))
    {
        const auto& spec = specs()[(size_t) Index::refA];
        setReal (Index::refA, spec.clampReal (text.getFloatValue()));
    }
}

} // namespace bmo::tune
