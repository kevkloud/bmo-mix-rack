#include "FetcompPanel.h"
#include "modules/fetcomp/params.h"

namespace bmo::fetcomp
{

namespace
{
    /** **The bezel opacity this module ships with, and the one place to flip
        it.**

        `ui::DynamicsMeter` has always stroked its bezel at 0.7, and that alpha
        is the default of the opt-in setter, so leaving this at 0.7 leaves BMO
        Opto's render byte for byte where it was.

        BMO Opto keeps that default. This module ships at full alpha. It
        matters here and nowhere else in the suite, because this bezel carries
        the voicing: against `meterFace` the two states separate by 3.88:1 at
        0.7 and by 5.91:1 at 1.0, measured on the renders. Frosty took the call
        on those renders, 2026-09-20 on AURORA
        (docs/1176-comp/11-integration-and-test-plan.md 4d step 6) -- render
        the other one with `ui.bezel=stock` rather than by editing this. */
    constexpr float kStockBezelAlpha = 0.7f;
    constexpr float kFullBezelAlpha = 1.0f;
    constexpr float kBezelAlpha = kFullBezelAlpha;

    /** How thick the voicing bezel is stroked, against the suite's 1.5.

        The frame is the whole of the voicing display, and once the meter moved
        to the head of the panel a hairline could not carry it. Render the
        suite's weight with `ui.bezel=thin` rather than by editing this. */
    constexpr float kStockBezelThickness = 1.5f;
    constexpr float kBezelThickness      = 4.0f;

    /** ATTACK and RELEASE are knob positions 1..7 -- see params.h. The face
        draws one mark per position, so this is the same 7 the parameter range
        is built from rather than a number chosen to look right. */
    constexpr int kAttackReleasePositions = 7;

    /** Every other mark carries its number, so the face reads 1, 3, 5, 7.

        Seven numbers around a 58 px face would not fit and would not help: the
        odd ones are enough to count from, and because seven is odd this numbers
        both ends of the sweep. */
    constexpr int kAttackReleaseLabelEvery = 2;

    /** BMO FET's accent on the dark plate.

        The identity colour is `#5489d4`, and it stays that on the pale plate
        where it was chosen and measured. On the dark plate it is lifted to
        this, same hue, because `#5489d4` was picked against `#efefef` and
        misses the dark plate badly: a cap on it reads 3.80:1 against the
        suite's 5.87-6.84 band, and the pointer on the cap 3.97:1 against
        6.13-7.14. At `#8fb4e6` those become 6.34:1 and 6.61:1, both mid-band,
        and the caption goes 3.80:1 to 6.34:1 with them.

        **This is the mechanism the suite already has, pointing the other way.**
        `accentTextOn` darkens the four suite accents hard for the pale plate
        because they were chosen to clear 7:1 on the dark one raw (Tokens.cpp,
        "The accents need no dark variant at all"). BMO FET's blue is the
        opposite case -- it works on the pale plate and fails on the dark -- so
        it takes a dark variant instead of a pale one.

        **Everything the module tints takes it together.** Lifting only the
        knobs would put two blues 1.58:1 apart on one panel, which is the same
        clash that ruled out lighting the switches in `switchAlt`. Measured on
        AURORA, 2026-09-21; owner's call on renders the same day. */
    constexpr juce::uint32 kDarkPlateAccent = 0xff8fb4e6;


    /** The caption size ATTACK and RELEASE are set at, against the suite's 15.

        They share the drive column, so each has 74 px rather than 120, and
        "RELEASE" is the longest name on the panel. See the call site. */
    constexpr float kTimeCaptionSize     = 10.0f;

    /** The same caption across the full panel width, where a 120 px cell fits
        the suite's own size. */
    constexpr float kTimeCaptionSizeFull = 15.0f;

    // A knob draws at its own side; the cell is wider so the caption
    // underneath has room. BMO Opto's MAKEUP is the case that forced the
    // distinction -- at the width the knob wanted, the caption clipped to
    // "MAKEU". Both sides below are set with that in mind: the cells stay
    // 120 wide whatever the face does.
    constexpr int kMixRow = ui::ModulePanel::kTrimKnobRow;

    /** **INPUT and OUTPUT are the big pair, ATTACK and RELEASE the small one.**

        The two that decide how much compression there is have no threshold
        knob between them, so they *are* the compression control, and the panel
        now says so by size. ATTACK and RELEASE set how fast it moves, which is
        the smaller decision -- and since they are read off seven marks and a
        printed value rather than off the pointer's angle, their faces can give
        room up without losing anything.

        41 against 100 is the widest split in the suite. It is affordable here
        because the numbered marks do the reading these two would otherwise
        need a big face for. */
    constexpr int kDriveKnobSide = 100;
    constexpr int kTimeRow       = 104;   ///< ATTACK and RELEASE abreast

    /** **A step-marked knob is a small face in a big box.**

        `radius` is the box's smaller side times `faceScale`, and everything
        outside the face -- the marks and the numbers on them -- has to fit in
        what is left, or it is clipped by the component's own bounds. So these
        two do not cap their side the way the drive pair does. They take the
        row's full height and shrink the *face* instead, which is what leaves
        room for a scale around it.

        0.42 of a 104 px row puts the face at about 41 px against the drive
        pair's 62, and leaves 18 px of clearance for a 6 px ring, an 8 px gap
        and a 9 px numeral. Capping the side at 58 instead, which is what this
        did first, clipped the numbers off at the top of the box. */
    constexpr float kTimeFaceScale = 0.42f;

    /** **INPUT over OUTPUT in one column, the ratio strip in the column
        beside them.**

        A stacked pair costs no more than an abreast pair here, which is the
        whole reason this works: laid out as two separate rows the two knobs
        take two of the panel's seven divisions and leave nothing for the
        ratios, but laid out as one block they take one -- and the ratio strip
        moves into the air to their right, which a two-across knob row was
        wasting anyway.

        129 a knob: 100 of face and the caption under it. 5 x 26 plus the gaps
        is 170 for the ratio column against the block's 258, so the strip
        centres in it with room at both ends. */
    constexpr int kDriveStackRow = 129;   ///< one stacked drive knob and caption
    constexpr int kDriveBlock    = kDriveStackRow * 2;

    /** The gap between the two columns, and between the drive stack and the
        time knobs under it.

        Small on purpose. The body is one region read as two columns, not two
        regions that happen to be side by side, and the panel's own `gap` --
        which is what separates the meter, the body, MIX and the oversampling
        row -- would break it into pieces if it were used inside as well. */
    constexpr int kColumnGap = 10;

    /** The whole two-column body: the drive stack, the time knobs under it,
        and the switch column beside both. The left column sets the height. */
    constexpr int kBodyBlock = kDriveBlock + kColumnGap + kTimeRow;

    constexpr int kSwitchWidth  = ui::Tokens::switchWidth;
    constexpr int kSwitchHeight = ui::Tokens::switchHeight;
    constexpr int kSwitchGap    = ui::Tokens::switchGap;

    /** The meter and the row under it are the same width, and it is the width
        three switches and their two gaps want: 226.

        modules/AGENTS.md records BMO Opto's IN/GR/OUT row as the one place a
        switch may be narrower than `switchWidth`, because three of them plus
        two gaps need 226 px and that panel is 220 wide. It also says to use
        the full width and delete the exception on a panel that can afford it.
        This one is 260, so it does. */
    constexpr int kMeterWidth     = kSwitchWidth * 3 + kSwitchGap * 2;
    constexpr int kMeterHeight    = 102;
    constexpr int kMeterButtonGap = 4;      ///< meter face to its IN/GR/OUT row
    constexpr int kMeterBlock     = kMeterHeight + kMeterButtonGap + kSwitchHeight;

    /** Two rows of two, then ALL on its own. See the class comment for why the
        fifth button is set apart rather than made the end of a row. */
    constexpr int kRatioBlock = kSwitchHeight * 3 + kSwitchGap * 2;

    /** A pair of switches side by side, and the block they make. */
    constexpr int kPairWidth = kSwitchWidth * 2 + kSwitchGap;

    /** The bracket that gathers the four ratios and leads to ALL.

        BMO Dimension's shape turned on its side -- a line with its ends turned
        in toward what it gathers, in the raw accent at the 0.55 the dotted
        tracks use, so it is the buttons' own mark rather than a rule. A rule
        divides; this joins.

        `kBracketChannel` is the strip of plate the spine stands in, taken from
        the knob column rather than from the buttons: the buttons are already
        exactly `switchWidth` and shrinking them would break the row. */
    constexpr int   kBracketChannel = 14;
    constexpr float kBracketWeight  = 2.0f;
    constexpr float kBracketEnd     = 7.0f;   ///< how far the turned ends reach
    constexpr float kBracketGap     = 5.0f;   ///< spine to the buttons' left edge

    /** The column the ratio strip stands in, beside INPUT and OUTPUT: one
        switch, plus the channel the bracket's spine needs. The knobs keep the
        rest, which is the wider share and the one that needs it -- "OUTPUT" is
        a word and "20:1" is four characters. */
    constexpr int kRatioColumnWidth = kSwitchWidth + kBracketChannel;

    /** How far the stacked captions come up toward their own faces, in design
        px. See the call site: it is a grouping fix, not a tidy-up. */
    constexpr int kDriveCaptionLift = 6;

    /** The module accent for the appearance now showing. See
        kDarkPlateAccent. */
    juce::Colour accentFor (juce::Colour identity) noexcept
    {
        return ui::isDarkMode() ? juce::Colour (kDarkPlateAccent) : identity;
    }
}

FetcompPanel::FetcompPanel (ui::ModuleContext ctx)
    : ModulePanel (std::move (ctx)),
      // The four knobs are the module: character style, in the module's own
      // colour. MIX is not -- it is set once and left, so it takes the shared
      // trim size and the utility blue every INPUT and OUTPUT in the suite
      // wears, which is also what keeps it from reading as a fifth headline
      // control.
      inputKnob   (context.params.param (Index::input),   "INPUT",
                   ui::Knob::Style::character, 0.62f, accentFor (context.def.accent)),
      outputKnob  (context.params.param (Index::output),  "OUTPUT",
                   ui::Knob::Style::character, 0.62f, accentFor (context.def.accent)),
      attackKnob  (context.params.param (Index::attack),  "ATTACK",
                   ui::Knob::Style::character, kTimeFaceScale, accentFor (context.def.accent)),
      releaseKnob (context.params.param (Index::release), "RELEASE",
                   ui::Knob::Style::character, kTimeFaceScale, accentFor (context.def.accent)),
      mixKnob     (context.params.param (Index::mix),     "MIX"),
      meter (context.inputRms, context.rms, context.gainReductionDb,
             ui::DynamicsMeter::Mode::reduction, accentFor (context.def.accent),
             ui::accentTextOn (accentFor (context.def.accent), ui::tokens().meterFace)),
      meterInButton ("IN"), meterGrButton ("GR"), meterOutButton ("OUT"),
      ratio4Button ("4:1"), ratio8Button ("8:1"), ratio12Button ("12:1"),
      ratio20Button ("20:1"), ratioAllButton ("ALL"),
      voicingBlueButton ("BLUE"), voicingBlackButton ("BLACK"),
      os2xButton ("2x"), os4xButton ("4x")
{
    // Only the drive pair caps its side. ATTACK and RELEASE take their row
    // whole and shrink the face instead -- see kTimeFaceScale.
    for (auto* k : { &inputKnob, &outputKnob })
    {
        k->setKnobSide (kDriveKnobSide);

        // **A stacked caption has to be pulled up to its own knob.**
        //
        // A caption under a knob in a *row* is unambiguous: there is nothing
        // below it to belong to. In a column there is -- the next knob -- and
        // the two gaps then compete. Measured on the render before this, INPUT
        // sat 34 px under its own face and 44 px above OUTPUT's, which is not
        // enough of a split to read: 1 to 1.3 leaves the eye choosing.
        //
        // The lift takes 12 render px off the first gap and gives them to the
        // second, which makes it 22 against 56 -- better than 1 to 2.5, and the
        // caption plainly belongs to the face above it. This is the same
        // mechanism module 7 used for the opposite problem, and the reason it
        // is set here per box rather than suite-wide: the suite's 30 is a
        // figure for knobs in a row.
        k->setCaptionLift (kDriveCaptionLift);
    }

    /* **ATTACK and RELEASE are marked on the face, not printed underneath.**

       The parameter *is* the knob position, 1..7 -- see modules/fetcomp/params.h
       -- so these two are the only knobs in the suite whose face should say
       places rather than amounts. The dotted arc with a minus and a plus
       promises a continuum, which is the wrong promise; seven marks numbered
       1, 3, 5, 7 is how a detented faceplate says it.

       They printed "4 (126 us)" under the caption until 2026-09-20. The number
       was the position, which the marks now show, and the time was a figure
       nobody sets a compressor by -- you turn it until it sounds right. With
       the face marked, the readout was saying the same thing twice and taking
       the room the numbers needed. Frosty's call on renders. */
    for (auto* k : { &attackKnob, &releaseKnob })
    {
        k->setStepMarks (kAttackReleasePositions, kAttackReleaseLabelEvery);

        // Sharing the drive column puts these two in half of 148 rather than
        // half of 240, and "RELEASE" does not fit a 74 px cell at the suite's
        // 15 pt -- that is BMO Opto's MAKEUP bug waiting to happen, and
        // ui_layout_tests asserts the overflow rather than trusting this
        // comment.
        //
        // Shrinking the type is the right answer rather than a reluctant one:
        // these are the secondary pair, they are read off the marks, and a
        // smaller name says so. setCaptionSize keeps the name against the
        // knob's own bottom edge, so it does not strand itself at the foot of
        // the cell as it shrinks.
        k->setCaptionSize (kTimeCaptionSize);
    }

    styleTrimKnob (mixKnob);

    // The hot zone -- 0 VU and above -- is the accent stepped off the meter's
    // own face until it clears 4.5:1 rather than trusted to, which is
    // OptoPanel::hotColourFor's pattern. It is the same in both voicings: only
    // the bezel carries the voicing.
    meter.setBezelAlpha (kBezelAlpha);

    // The bezel carries the voicing and the meter now heads the panel, so the
    // frame is stroked heavier than the suite's 1.5 and goes back over the
    // needle rather than being cut by it at full sweep. Both are opt-in on
    // ui::DynamicsMeter and both default to what every other meter draws, so
    // BMO Opto is untouched. Frosty's call on renders, 2026-09-20.
    meter.setBezelThickness (kBezelThickness);
    meter.setBezelInFront (true);

    for (auto* b : { &meterInButton, &meterGrButton, &meterOutButton,
                     &ratio4Button, &ratio8Button, &ratio12Button, &ratio20Button,
                     &ratioAllButton, &voicingBlueButton, &voicingBlackButton,
                     &os2xButton, &os4xButton })
    {
        // Every one of these is a radio over a choice parameter rather than a
        // toggle over a switch: the click sets the parameter and the parameter
        // lights the buttons, so host automation and a click cannot disagree.
        b->setClickingTogglesState (false);
        b->setColour (juce::ToggleButton::tickColourId, accentFor (context.def.accent));
        addAndMakeVisible (b);
    }

    // The initialiser list above already used accentFor, so record which
    // appearance that was for: paintPanel re-applies only on a change.
    accentIsDark = ui::isDarkMode();

    // **MIX is monochrome: white on the dark plate, black on the pale one.**
    //
    // It used to wear the suite's utility azure, the same one every INPUT and
    // OUTPUT outside this module wears. That was fine while the module accent
    // was #5489d4, which the azure cleared by 1.58:1. Against the lifted dark
    // accent it is 1.05:1 -- the same lightness -- so the two blues separated
    // by hue alone and MIX stopped reading as a different kind of control.
    // Owner's call on renders, AURORA 2026-09-21: take the blue off it
    // entirely rather than hunt for a third one that clears both.
    //
    // setUtilityTint carries the cap, the dotted track and the caption
    // together, and the pointer follows on its own: `pointer` is near-black on
    // the dark plate and white on the pale one, which is exactly backwards
    // from the cap here and therefore right.
    mixKnob.setUtilityTint (ui::isDarkMode() ? juce::Colours::white
                                             : juce::Colours::black);

    for (auto* c : std::initializer_list<juce::Component*> {
             &inputKnob, &outputKnob, &attackKnob, &releaseKnob, &mixKnob, &meter })
        addAndMakeVisible (c);

    meterInButton .onClick = [this] { selectMeterMode (ui::DynamicsMeter::Mode::input); };
    meterGrButton .onClick = [this] { selectMeterMode (ui::DynamicsMeter::Mode::reduction); };
    meterOutButton.onClick = [this] { selectMeterMode (ui::DynamicsMeter::Mode::output); };

    // GR is the mode this module is for, so it is where the meter opens and it
    // is the middle button -- IN and OUT then read left to right as signal
    // flow either side of it, the order BMO Opto's row already uses.
    selectMeterMode (ui::DynamicsMeter::Mode::reduction);

    ratioAttachment = std::make_unique<juce::ParameterAttachment> (
        context.params.param (Index::ratio),
        [this] (float value) { showRatio (juce::roundToInt (value)); });

    voicingAttachment = std::make_unique<juce::ParameterAttachment> (
        context.params.param (Index::voicing),
        [this] (float value) { showVoicing (juce::roundToInt (value)); });

    osAttachment = std::make_unique<juce::ParameterAttachment> (
        context.params.param (Index::oversampling),
        [this] (float value) { showOversampling (juce::roundToInt (value)); });

    // Every ratio has a button of its own, so a click always names a position
    // and clicking the lit one is a no-op rather than a way out of it.
    const auto chooseRatio = [this] (int choice)
    {
        return [this, choice] { ratioAttachment->setValueAsCompleteGesture ((float) choice); };
    };

    ratio4Button  .onClick = chooseRatio (ratio4);
    ratio8Button  .onClick = chooseRatio (ratio8);
    ratio12Button .onClick = chooseRatio (ratio12);
    ratio20Button .onClick = chooseRatio (ratio20);
    ratioAllButton.onClick = chooseRatio (ratioAll);

    const auto chooseVoicing = [this] (int choice)
    {
        return [this, choice] { voicingAttachment->setValueAsCompleteGesture ((float) choice); };
    };

    voicingBlueButton .onClick = chooseVoicing (blue);
    voicingBlackButton.onClick = chooseVoicing (black);

    // Off has no switch of its own, so clicking the lit one is the way back to
    // it -- the Saturator's and BMO CEQ's oversampling rows do the same.
    const auto chooseOversampling = [this] (int choice)
    {
        return [this, choice]
        {
            const auto& p = context.params.param (Index::oversampling);
            const auto current = juce::roundToInt (p.convertFrom0to1 (p.getValue()));

            osAttachment->setValueAsCompleteGesture ((float) (current == choice ? osOff : choice));
        };
    };

    os2xButton.onClick = chooseOversampling (os2x);
    os4xButton.onClick = chooseOversampling (os4x);

    // Light whatever the parameters already say, which is how a render and a
    // reopened editor come up in the state they were left in.
    ratioAttachment  ->sendInitialUpdate();
    voicingAttachment->sendInitialUpdate();
    osAttachment     ->sendInitialUpdate();
}

//==============================================================================
void FetcompPanel::selectMeterMode (ui::DynamicsMeter::Mode mode)
{
    meter.setMode (mode);

    meterInButton .setToggleState (mode == ui::DynamicsMeter::Mode::input,     juce::dontSendNotification);
    meterGrButton .setToggleState (mode == ui::DynamicsMeter::Mode::reduction, juce::dontSendNotification);
    meterOutButton.setToggleState (mode == ui::DynamicsMeter::Mode::output,    juce::dontSendNotification);
}

void FetcompPanel::showRatio (int choice)
{
    ratio4Button  .setToggleState (choice == ratio4,   juce::dontSendNotification);
    ratio8Button  .setToggleState (choice == ratio8,   juce::dontSendNotification);
    ratio12Button .setToggleState (choice == ratio12,  juce::dontSendNotification);
    ratio20Button .setToggleState (choice == ratio20,  juce::dontSendNotification);
    ratioAllButton.setToggleState (choice == ratioAll, juce::dontSendNotification);
}

juce::Colour FetcompPanel::bezelFor (int voicingChoice) const
{
    return voicingChoice == blue ? accentFor (context.def.accent) : juce::Colours::black;
}

void FetcompPanel::showVoicing (int choice)
{
    voicingBlueButton .setToggleState (choice == blue,  juce::dontSendNotification);
    voicingBlackButton.setToggleState (choice == black, juce::dontSendNotification);

    if (choice == lastVoicing)
        return;

    lastVoicing = choice;

    // The bezel, and only the bezel. The hot zone keeps the accent in both
    // states, so the meter's warning colour does not change meaning when the
    // voicing does.
    meter.setColours (bezelFor (choice),
                      ui::accentTextOn (accentFor (context.def.accent), ui::tokens().meterFace));
}

void FetcompPanel::showOversampling (int choice)
{
    os2xButton.setToggleState (choice == os2x, juce::dontSendNotification);
    os4xButton.setToggleState (choice == os4x, juce::dontSendNotification);
}

bool FetcompPanel::setUiState (const juce::String& key, const juce::String& value)
{
    if (key == "meter")
    {
        if (value.equalsIgnoreCase ("IN"))  { selectMeterMode (ui::DynamicsMeter::Mode::input);     return true; }
        if (value.equalsIgnoreCase ("GR"))  { selectMeterMode (ui::DynamicsMeter::Mode::reduction); return true; }
        if (value.equalsIgnoreCase ("OUT")) { selectMeterMode (ui::DynamicsMeter::Mode::output);    return true; }

        return false;
    }

    if (key == "bezel")
    {
        if (value.equalsIgnoreCase ("stock")) { meter.setBezelAlpha (kStockBezelAlpha); return true; }
        if (value.equalsIgnoreCase ("full"))  { meter.setBezelAlpha (kFullBezelAlpha); return true; }

        // The weight, so the heavy frame and the suite's hairline can be put
        // side by side without a rebuild, the way the alpha gate was.
        if (value.equalsIgnoreCase ("thin"))  { meter.setBezelThickness (kStockBezelThickness); return true; }
        if (value.equalsIgnoreCase ("heavy")) { meter.setBezelThickness (kBezelThickness);      return true; }

        // And whether the frame is cut by the needle or sits over it.
        if (value.equalsIgnoreCase ("behind")) { meter.setBezelInFront (false); return true; }
        if (value.equalsIgnoreCase ("front"))  { meter.setBezelInFront (true);  return true; }

        return false;
    }

    if (key == "time")
    {
        // Where ATTACK and RELEASE sit, which is an open layout question: in
        // the drive column, so the panel keeps one centre line all the way
        // down, or across the full width, which fills the plate the ratio
        // strip leaves behind but moves the centre line twice. Rendered both
        // ways rather than argued -- the same reason `bezel` exists.
        const auto wanted = value.equalsIgnoreCase ("column") ? true
                          : value.equalsIgnoreCase ("full")   ? false
                                                              : timeKnobsInColumn;

        if (wanted == timeKnobsInColumn && ! (value.equalsIgnoreCase ("column")
                                              || value.equalsIgnoreCase ("full")))
            return false;

        timeKnobsInColumn = wanted;

        // The caption only has to shrink when the cell does.
        for (auto* k : { &attackKnob, &releaseKnob })
            k->setCaptionSize (timeKnobsInColumn ? kTimeCaptionSize : kTimeCaptionSizeFull);

        resized();
        return true;
    }

    return false;
}

//==============================================================================
void FetcompPanel::applyAccent()
{
    accentIsDark = ui::isDarkMode();

    const auto accent = accentFor (context.def.accent);

    for (auto* k : { &inputKnob, &outputKnob, &attackKnob, &releaseKnob })
        k->setAccent (accent);

    // MIX goes with it: white on the dark plate, black on the pale one. See
    // the constructor for why it is monochrome rather than the suite azure.
    mixKnob.setUtilityTint (accentIsDark ? juce::Colours::white
                                         : juce::Colours::black);

    for (auto* b : { &meterInButton, &meterGrButton, &meterOutButton,
                     &ratio4Button, &ratio8Button, &ratio12Button, &ratio20Button,
                     &ratioAllButton, &voicingBlueButton, &voicingBlackButton,
                     &os2xButton, &os4xButton })
        b->setColour (juce::ToggleButton::tickColourId, accent);

    // The bezel is the voicing's, not the accent's, but Blue's bezel *is* the
    // accent -- so it moves with it, and Black's stays black.
    meter.setColours (bezelFor (lastVoicing),
                      ui::accentTextOn (accent, ui::tokens().meterFace));
}

void FetcompPanel::paintPanel (juce::Graphics& g)
{
    // The appearance can change under an open editor: ProductEditor polls the
    // theme and repaints, and this is that repaint. Done before the early
    // return below, because the accent is the panel's whether or not the
    // ratio bracket has been laid out yet.
    if (ui::isDarkMode() != accentIsDark)
        applyAccent();

    // Nothing to gather until resized() has run.
    if (ratio4Button.getBounds().isEmpty() || ratioAllButton.getBounds().isEmpty())
        return;

    const auto spineX = (float) ratio4Button.getX() - kBracketGap - kBracketWeight;

    // **A tick into the middle of every button, not just the ends.**
    //
    // With turns at the two ends alone this was a bracket, and a bracket only
    // says "these belong together" -- which the column's own spacing already
    // said. A tick struck into each ratio at its centre makes it a bus: four
    // lines running into one spine that runs into ALL, so the panel draws the
    // sentence "all of them at once" rather than hinting at it.
    const juce::Button* wired[] { &ratio4Button, &ratio8Button, &ratio12Button,
                                  &ratio20Button, &ratioAllButton };

    const auto centreOf = [] (const juce::Button* b)
    {
        return (float) b->getBounds().getCentreY();
    };

    const auto top  = centreOf (wired[0]);
    const auto foot = centreOf (wired[4]);

    // **Gathered into one list and filled once, because the ink is
    // translucent.**
    //
    // The bracket is the raw accent at the 0.55 alpha the dotted tracks use.
    // Filled as six separate rectangles it was six separate composites, so
    // every place a tick crossed the spine got the colour laid down twice --
    // 0.55 over 0.55 is an effective 0.80 -- and the join read as a darker
    // knuckle on what is meant to be one continuous line. `RectangleList`
    // keeps its contents disjoint as they are added, so the union is painted
    // exactly once and the bus is one weight end to end.
    juce::RectangleList<float> bus;

    // The spine spans first tick to last, so it begins and ends on a tick
    // rather than overshooting into bare plate at either end.
    bus.add ({ spineX, top, kBracketWeight, foot - top });

    for (const auto* b : wired)
        bus.add ({ spineX, centreOf (b) - kBracketWeight * 0.5f,
                   kBracketEnd, kBracketWeight });

    g.setColour (accentFor (context.def.accent).withAlpha (0.55f));
    g.fillRectList (bus);
}

//==============================================================================
void FetcompPanel::resized()
{
    // 4, the same content inset every other panel measures from.
    auto area = getLocalBounds().reduced (kPad, 4);

    // Four blocks now: the meter, the two-column body, MIX, and the
    // oversampling row at the foot. Five divisions for four blocks.
    //
    // **The body is one region in two columns, not a stack of rows.** Every
    // knob is in the left column and every switch that belongs to them is in
    // the right, which is what finally makes the stacked drive pair work: the
    // ratio strip stands beside INPUT and OUTPUT, the voicing pair stands
    // beside ATTACK and RELEASE, and neither column leaves a hole for the
    // other to sit next to. Laying the same controls out as six full-width
    // rows is what forced the knobs small and the panel airy.
    const auto content = kMeterBlock + kBodyBlock + kMixRow + kSwitchHeight;
    const auto gap     = juce::jmax (kSwitchGap, (area.getHeight() - content) / 5);

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

    // The VU over its IN/GR/OUT row, both the same width. GR in the middle
    // because it is the reading this module is for.
    //
    // **Why the meter is up here and not in the middle.** The hardware this is
    // modelled on is a wide face read left to right: four knobs, the ratio
    // strip, then the meter and its mode buttons at the right-hand end. Stand
    // that face on its end to fit a rack module 260 px wide and the reading
    // order becomes top to bottom, which puts the meter at the top -- and the
    // meter itself, whose long axis ran along the wide side, turns through 90
    // degrees to lie across the narrow one. It is already drawn that way, so
    // this costs a move and not a widget: `ui::DynamicsMeter` is untouched and
    // BMO Opto's three hashes cannot move. Frosty's call, 2026-09-20.
    {
        auto block = area.removeFromTop (kMeterBlock);
        const auto width = juce::jmin (block.getWidth(), kMeterWidth);

        meter.setBounds (block.removeFromTop (kMeterHeight)
                              .withSizeKeepingCentre (width, kMeterHeight));
        block.removeFromTop (kMeterButtonGap);

        auto buttons = block.withSizeKeepingCentre (width, kSwitchHeight);
        const auto buttonWidth = (width - kSwitchGap * 2) / 3;

        meterInButton.setBounds (buttons.removeFromLeft (buttonWidth));
        buttons.removeFromLeft (kSwitchGap);
        meterGrButton.setBounds (buttons.removeFromLeft (buttonWidth));
        buttons.removeFromLeft (kSwitchGap);
        meterOutButton.setBounds (buttons.removeFromLeft (buttonWidth));
    }
    area.removeFromTop (gap);

    // **INPUT over OUTPUT, the ratio strip in the column beside them.**
    //
    // There is no threshold knob, so INPUT and OUTPUT are the compression
    // control between them -- and a compressor with no threshold is set by
    // driving one and making up the other, which is a thing you do in order,
    // top down, rather than a pair you compare side to side. The ratio picks
    // which curve all that drive runs into, so it belongs against them rather
    // than in a strip of its own further down.
    {
        auto body = area.removeFromTop (kBodyBlock);

        auto switchColumn = body.removeFromRight (kRatioColumnWidth);
        body.removeFromRight (kColumnGap);

        // -- the left column: every knob on the panel bar MIX ---------------
        //
        // The knobs take the wider share, which is the one that needs it:
        // their captions are words and the switches' are four characters.
        auto driveBlock = body.removeFromTop (kDriveBlock);
        inputKnob .setBounds (driveBlock.removeFromTop (kDriveStackRow));
        outputKnob.setBounds (driveBlock);

        body.removeFromTop (kColumnGap);

        // ATTACK and RELEASE close the column, abreast in its width. Both run
        // backwards -- 7 is the fast end -- and their positions are read off
        // the marks around the face.
        layOutKnobs (body.removeFromTop (kTimeRow), attackKnob, releaseKnob);

        // -- the right column: the switches those knobs answer to -----------
        //
        // The ratio strip centres on the drive stack and the voicing pair on
        // the time knobs, so each group of switches sits against the knobs it
        // belongs to rather than at some average height of its own.
        auto ratioColumn = switchColumn.removeFromTop (kDriveBlock);
        switchColumn.removeFromTop (kColumnGap);

        // Four ratios, then ALL set apart by a double gap -- the fifth button
        // is every button pushed in at once, which is a different curve rather
        // than a steeper one. In a single column that separation is the gap
        // doing the work the 2 x 2 block's own shape used to do.
        const auto strip = kSwitchHeight * 5 + kSwitchGap * 3 + kSwitchGap * 2;

        // The buttons keep switchWidth and sit against the column's right
        // edge; the channel the bracket stands in is what is left on the left.
        auto buttons = ratioColumn.withSizeKeepingCentre (ratioColumn.getWidth(), strip)
                                  .removeFromRight (kSwitchWidth);

        const auto place = [&] (juce::Button& b)
        {
            b.setBounds (buttons.removeFromTop (kSwitchHeight));
            buttons.removeFromTop (kSwitchGap);
        };

        place (ratio4Button);
        place (ratio8Button);
        place (ratio12Button);
        place (ratio20Button);

        buttons.removeFromTop (kSwitchGap);   // ALL stands apart
        ratioAllButton.setBounds (buttons.removeFromTop (kSwitchHeight));

        // **The voicing stacks under the ratios rather than sitting at the
        // foot of the panel.** It is a choice of instrument, and putting it in
        // the switch column beside the time knobs says that it belongs with
        // the controls rather than with the setup rows -- and it fills the
        // plate the time knobs would otherwise sit next to. Two switches, so
        // stacked rather than abreast: the column is one switch wide.
        // Flush right, exactly as the ratios are, rather than centred in the
        // column. The column is a switch plus the bracket's channel, so
        // centring in it lands these two 7 px left of every other button --
        // aligned to the bus line's space instead of to the buttons the bus
        // runs into. The channel belongs to the bracket alone.
        const auto pair = kSwitchHeight * 2 + kSwitchGap;

        auto voicing = switchColumn.withSizeKeepingCentre (switchColumn.getWidth(), pair)
                                   .removeFromRight (kSwitchWidth);

        voicingBlueButton .setBounds (voicing.removeFromTop (kSwitchHeight));
        voicing.removeFromTop (kSwitchGap);
        voicingBlackButton.setBounds (voicing.removeFromTop (kSwitchHeight));
    }
    area.removeFromTop (gap);

    // MIX and the oversampling row have no counterpart on the hardware, so
    // they keep the foot of the panel: both are set once and left.
    mixKnob.setBounds (area.removeFromTop (kMixRow));
    area.removeFromTop (gap);

    // Oversampling closes the panel. It is the one control here that is not a
    // sound decision at all -- it is a cost decision, set once for the session
    // -- so it sits below everything that is, at the foot where MIX already
    // was. Off is the position with no switch of its own: neither lit is Off.
    layOutPair (area.removeFromTop (kSwitchHeight), os2xButton, os4xButton);
}

} // namespace bmo::fetcomp
