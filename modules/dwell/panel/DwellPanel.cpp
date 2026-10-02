#include "DwellPanel.h"
#include "core/ui/Fonts.h"
#include "modules/dwell/Module.h"
#include "modules/dwell/dsp/GainLaws.h"
#include "modules/dwell/params.h"

#include <array>
#include <cmath>
#include <utility>

namespace bmo::dwell
{

//==============================================================================
/** A row or grid of switches over one choice parameter.

    The click sets the parameter and the parameter lights the switches, so a
    click and host automation cannot disagree -- BMO Saturator's oversampling
    row and BMO DEQ's placement row are the same shape. There is no Off
    position in any of the choices here, so clicking the lit one does nothing.

    `columns` is how many cells stand side by side before the next line starts,
    which is 3 for every row on this panel now. A short last line is centred
    rather than left-hung, which three across three never needs and the old
    seven-cell stack did.

    `namePrefix` renames the *components* without touching what is drawn on
    them, and the lane's FX cells are what it is for. They repeat the main
    delay's words on purpose -- DIFFUSE, PAN and CRUSH under a LANE rule read
    as the same stage on the other engine, where LN-DIFFUSE would read as a bin
    of leftovers -- but `findNamed` in tests/ui/LayoutTests.cpp walks children
    by name, and two cells with one name resolve by child order, which changes
    every time a page turns.

    Plain `juce::ToggleButton`s rather than `ui::SwitchButton`s because a
    SwitchButton attaches itself to a *bool*; the look and feel draws both the
    same way, and `tests/ui/LayoutTests.cpp` measures both labels. */
class ChoiceRow final : public juce::Component
{
public:
    ChoiceRow (juce::RangedAudioParameter& parameter, juce::StringArray labels,
               juce::Colour tint, float labelPoints, int columnCount, int gapPx,
               const juce::String& namePrefix = {})
        : columns (juce::jmax (1, columnCount)), gap (gapPx), points (labelPoints),
          attachment (parameter, [this] (float v) { show ((int) std::lround (v)); })
    {
        for (int i = 0; i < labels.size(); ++i)
        {
            auto b = std::make_unique<juce::ToggleButton> (labels[i]);
            b->setColour (juce::ToggleButton::tickColourId, tint);
            b->setClickingTogglesState (false);
            b->getProperties().set (ui::BmoLookAndFeel::kSwitchLabelSize, points);
            b->onClick = [this, i] { attachment.setValueAsCompleteGesture ((float) i); };

            // The drawn label is the button's `text`, set by the constructor
            // above; this moves only the component's name. See the class
            // comment.
            if (namePrefix.isNotEmpty())
                b->setName (namePrefix + "." + labels[i]);

            addAndMakeVisible (*b);
            buttons.push_back (std::move (b));
        }

        // Lights whatever the parameter already says, so a render and a
        // reopened editor both come up in the state they were left in.
        attachment.sendInitialUpdate();
    }

    void resized() override
    {
        const auto n = (int) buttons.size();

        if (n == 0)
            return;

        auto area = getLocalBounds();
        const auto lines = (n + columns - 1) / columns;
        const auto lineHeight = (area.getHeight() - gap * (lines - 1)) / juce::jmax (1, lines);
        const auto cellWidth = (area.getWidth() - gap * (columns - 1)) / columns;

        for (int line = 0; line < lines; ++line)
        {
            auto row = area.removeFromTop (lineHeight);
            area.removeFromTop (gap);

            const auto first = line * columns;
            const auto inLine = juce::jmin (columns, n - first);

            // A short last line is centred, for whatever count does not divide
            // by its columns: one cell hung on the left edge reads as a mistake.
            auto band = row.withSizeKeepingCentre (inLine * cellWidth + (inLine - 1) * gap,
                                                   row.getHeight());

            for (int i = 0; i < inLine; ++i)
            {
                buttons[(size_t) (first + i)]->setBounds (band.removeFromLeft (cellWidth));
                band.removeFromLeft (gap);
            }
        }
    }

    void setRowEnabled (bool shouldBe)
    {
        for (auto& b : buttons)
            b->setEnabled (shouldBe);
    }

    /** What the lit cell draws in.

        For the lane's FX cells, which step back toward the hairline while
        FX LINK is holding them to the main delay's -- see
        DwellPanel::refreshFxLinkFollowing. It is a *colour* change and
        deliberately not `setRowEnabled (false)`: the dim a disabled control
        takes means "this stage is off" everywhere else in the suite, and these
        are not off. */
    void setRowTint (juce::Colour tint)
    {
        for (auto& b : buttons)
            b->setColour (juce::ToggleButton::tickColourId, tint);

        repaint();
    }

private:
    void show (int index)
    {
        for (int i = 0; i < (int) buttons.size(); ++i)
            buttons[(size_t) i]->setToggleState (i == index, juce::dontSendNotification);
    }

    int columns;
    int gap;
    float points;
    std::vector<std::unique_ptr<juce::ToggleButton>> buttons;
    juce::ParameterAttachment attachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ChoiceRow)
};

//==============================================================================
namespace
{
    constexpr int kRule = ui::ModulePanel::kRuleRow;   // 16

    constexpr int kSwitchHeight = ui::Tokens::switchHeight;   // 26
    constexpr int kSwitchWidth  = ui::Tokens::switchWidth;    // 70
    constexpr int kSwitchGap    = ui::Tokens::switchGap;      // 8

    /** The grid is three across, and that is the whole of the width's
        argument: 380 less `kPad` a side is 360, a 120 px cell. Module.cpp
        carries why 380. */
    constexpr int kCols = 3;

    /** 15 pt, the suite standard. FEEDBACK is the one caption a 120 px cell
        does not hold at this size -- it measures about 125 -- and the foot's
        uneven trio is what pays for it rather than a smaller word. */
    constexpr float kCaption = 15.0f;

    /** The small print: the two numbered strips under FEEDBACK and MIX. */
    constexpr float kSmallSize = 9.5f;

    /** How much height a PlainKnob spends under its knob at `kCaption`, plus
        the value line. The same arithmetic `PlainKnob::captionRow` does,
        written out because a row's height has to be known before a knob is
        in it. */
    constexpr int kCaptionRow = 22;
    constexpr int kValueRow   = 14;

    //== Knob sides. Two, and the hierarchy is the point. ======================
    //
    // The foot's three are the delay itself and draw at 86. Every page
    // control is a step down at 72 and prints its value inside the same
    // 108 px row an 86 px knob without one would take -- the arrangement the
    // value-rows pass settled on 2026-10-01, which carried straight over.
    constexpr int kFootKnob = 86;
    constexpr int kPageKnob = 72;

    constexpr float kFootFace = 0.58f;
    constexpr float kPageFace = 0.58f;

    //== Row heights ===========================================================
    constexpr int kFootRow  = kFootKnob + kCaptionRow + kValueRow;   // 122
    constexpr int kGridRow  = kPageKnob + kCaptionRow + kValueRow;   // 108
    constexpr int kStripRow = 20;
    constexpr int kSegRow   = 28;

    /** The foot's uneven trio: FEEDBACK's caption needs about 125 px, so the
        middle cell is 140 and TIME and MIX share what is left. */
    constexpr int kFeedbackCell = 140;

    //== The screen ============================================================
    constexpr int kBezelPad = 10;
    constexpr int kGap      = 12;

    /** The menu band at the top of the screen, and its divider. */
    constexpr float kMenuBand    = 26.0f;
    constexpr float kMenuDivider = 1.0f;
    constexpr float kMenuSize    = 11.0f;
    constexpr float kReadoutSize = 11.0f;

    /** The GR bar on TONE: the length of the bar in dB, matching DUCK's own
        range, and how tall it is drawn. */
    constexpr float kGrRangeDb = 24.0f;
    constexpr float kGrBar     = 8.0f;

    /** How wide the catch at the centre of the tail knob is, as a fraction of
        the travel either side of it: wide enough that a hand lands on unity
        without aiming, narrow enough that the sliver a *drag* can no longer
        stop in is smaller than the mark drawn over it. Wheel, arrow keys,
        typed entry and automation still reach every value in it, because the
        catch is on the drag alone (ui::Knob::setCatch). */
    constexpr double kLaneGainCatch = 0.03;

    /** What the percent moves, per FX type, `docs/delay/13` §6a. Indexed by
        `kFxTypeNames`, so the two lists move together. */
    juce::String amountCaptionFor (int type)
    {
        switch (type)
        {
            case 0:  return "SMEAR";    // Diffuse
            case 1:  return "DEPTH";    // Pan/Tremolo
            case 2:  return "BITS";     // Crush
            default: return "AMOUNT";
        }
    }

    juce::StringArray fxTypeLabels() { return { "DIFFUSE", "PAN", "CRUSH" }; }

    constexpr Page kPages[] { Page::tone, Page::lane, Page::fx };

    /** Three cells across a row, the last taking whatever the division left. */
    std::array<juce::Rectangle<int>, kCols> cellsOf (juce::Rectangle<int> row)
    {
        std::array<juce::Rectangle<int>, kCols> cells;
        const auto w = row.getWidth() / kCols;

        for (int i = 0; i < kCols; ++i)
            cells[(size_t) i] = (i == kCols - 1) ? row : row.removeFromLeft (w);

        return cells;
    }
}

//==============================================================================
DwellScreen::DwellScreen (ParamSet& p, juce::Colour moduleAccent, std::function<float()> gr)
    : params (p), accent (moduleAccent), reduction (std::move (gr))
{
    setName ("DISPLAY");
    lastHash = inputsHash();
    setPage (Page::tone);
}

DwellScreen::~DwellScreen() { stopTimer(); }

juce::String DwellScreen::pageName (Page p)
{
    switch (p)
    {
        case Page::tone: return "TONE";
        case Page::lane: return "LANE";
        case Page::fx:   return "FX";
    }

    return {};
}

void DwellScreen::setPage (Page p)
{
    page = p;

    // **The timer runs only on TONE**, the one page with something that moves
    // on its own (the GR bar). BMO Linger's analyser rule.
    if (page == Page::tone)
        startTimerHz (15);
    else
        stopTimer();

    repaint();
}

void DwellScreen::timerCallback()
{
    // Clamped rather than trusted: ModuleContext::gainReductionDb is signed.
    const auto now = reduction ? std::max (0.0f, reduction()) : 0.0f;

    if (std::abs (now - shownGr) > 0.05f)
    {
        shownGr = now;
        repaint();
    }
}

double DwellScreen::inputsHash() const
{
    // Not a hash in any careful sense -- a sum of the drawn inputs at distinct
    // weights, which moves whenever any of them does. All `refresh` needs.
    double h = 0.0;
    double w = 1.0;

    for (const auto i : { Index::sync, Index::note, Index::laneNote,
                          Index::time, Index::feedback, Index::laneTime, Index::laneGain,
                          Index::fx, Index::fxType, Index::fxAmount, Index::fxLink,
                          Index::laneFx, Index::laneFxType, Index::laneFxAmount })
    {
        h += (double) params.getReal (i) * w;
        w *= 1.37;
    }

    return h;
}

void DwellScreen::refresh()
{
    const auto h = inputsHash();

    if (h != lastHash)
    {
        lastHash = h;
        repaint();
    }
}

double DwellScreen::loopGain() const
{
    // `P_c` = 1: the loop's magnitude at its own peak. See the class comment.
    if (page == Page::lane)
        return (double) laneGainFor (laneGainOnDetent (params.getReal (Index::laneGain)), 1.0);

    return (double) feedbackGainFor (params.getReal (Index::feedback), 1.0);
}

double DwellScreen::delayMs() const
{
    return (double) params.getReal (page == Page::lane ? Index::laneTime : Index::time);
}

int DwellScreen::repeatsToFloor() const
{
    const auto g = loopGain();

    if (g >= 1.0)
        return -1;

    if (g <= 0.0)
        return 1;   // FEEDBACK at 0: the one repeat MIX lets through

    // The first repeat is the 0 dB reference; repeat k is g^(k-1) under it.
    const auto perLapDb = 20.0 * std::log10 (g);
    return 1 + (int) std::floor (kFloorDb / perLapDb + 1.0e-9);
}

juce::String DwellScreen::readout() const
{
    // With SYNC on the panel has the division but not the host's tempo -- that
    // reaches the DSP alone -- so the readout names the note rather than
    // printing a millisecond figure the engines are not running at.
    const auto synced = kSyncIsEnabled && params.getReal (Index::sync) > 0.5f;
    const auto noteIndex = juce::jlimit (0, (int) std::size (kNoteNames) - 1,
                                         juce::roundToInt (params.getReal (page == Page::lane ? Index::laneNote
                                                                                              : Index::note)));
    const auto ms = synced ? juce::String (kNoteNames[noteIndex])
                           : juce::String (juce::roundToInt (delayMs())) + " MS";
    const auto g = loopGain();

    if (page == Page::fx)
    {
        if (params.getReal (Index::fx) < 0.5f)
            return ms + "    FX OFF";

        const auto type = juce::jlimit (0, 2, juce::roundToInt (params.getReal (Index::fxType)));
        return ms + "    " + fxTypeLabels()[type] + " "
                  + juce::String (juce::roundToInt (params.getReal (Index::fxAmount))) + " %";
    }

    if (g > 1.0 + 1.0e-6)
    {
        if (synced)
            return ms + "    BUILDS";   // the rate needs the real time, which is the DSP's

        // How fast it climbs, in the units 10 §11.2 argues g_max in.
        const auto dbPerSecond = 20.0 * std::log10 (g) * 1000.0 / juce::jmax (1.0, delayMs());
        return ms + "    BUILDS " + juce::String (dbPerSecond, 1) + " DB/S";
    }

    if (g >= 1.0 - 1.0e-6)
        return ms + "    HOLDS";

    return ms + "    " + juce::String (repeatsToFloor()) + " REPEATS TO -60";
}

juce::Rectangle<float> DwellScreen::menuBand() const
{
    return getLocalBounds().toFloat().withHeight (kMenuBand);
}

juce::Rectangle<float> DwellScreen::menuSegment (Page p) const
{
    const auto band = menuBand();
    const auto w = band.getWidth() / (float) std::size (kPages);
    return { band.getX() + w * (float) (int) p, band.getY(), w, band.getHeight() };
}

juce::Rectangle<float> DwellScreen::plotBounds() const
{
    return getLocalBounds().toFloat().withTrimmedTop (kMenuBand).reduced (12.0f, 8.0f);
}

void DwellScreen::mouseUp (const juce::MouseEvent& e)
{
    if (! menuBand().contains (e.position))
        return;

    for (const auto p : kPages)
        if (menuSegment (p).contains (e.position))
        {
            if (onPageChosen)
                onPageChosen (p);
            else
                setPage (p);

            return;
        }
}

void DwellScreen::paint (juce::Graphics& g)
{
    const auto face = ui::tokens().meterFace;
    const auto ink = ui::accentInk (accent, face);

    g.setColour (face);
    g.fillRect (getLocalBounds());

    auto plot = plotBounds();
    auto readoutBand = plot.removeFromBottom (14.0f);
    plot.removeFromBottom (6.0f);

    if (page == Page::lane)
        paintRegion (g, plot.removeFromTop (14.0f), ink);

    if (page == Page::tone)
    {
        auto gr = plot.removeFromBottom (kGrBar + 12.0f);
        paintGr (g, gr, ink);
        plot.removeFromBottom (4.0f);
    }

    paintTrain (g, plot, ink);

    ui::drawLabel (g, readout(), readoutBand, juce::Justification::centred,
                   ui::labelFont (kReadoutSize, true), ink);

    paintMenu (g, ink);
}

void DwellScreen::paintMenu (juce::Graphics& g, juce::Colour ink) const
{
    const auto band = menuBand();

    g.setColour (ui::tokens().meterFace);
    g.fillRect (band);

    for (const auto p : kPages)
    {
        const auto seg = menuSegment (p);
        const auto on  = (p == page);

        // **Inverted video for the page you are on**, BMO Linger's: a block of
        // the screen's ink with the ground punched out of it in letters. No
        // bevel, no glow, no second colour.
        if (on)
        {
            g.setColour (ink);
            g.fillRect (seg.reduced (kMenuDivider, kMenuDivider));
        }

        ui::drawLabel (g, pageName (p), seg, juce::Justification::centred,
                       ui::labelFont (kMenuSize, true),
                       on ? ui::tokens().meterFace : ink.withAlpha (0.55f));

        if (p != Page::tone)
        {
            g.setColour (ink.withAlpha (0.35f));
            g.fillRect (juce::Rectangle<float> (seg.getX(), band.getY() + 3.0f,
                                                kMenuDivider, band.getHeight() - 6.0f));
        }
    }

    g.setColour (ink.withAlpha (0.45f));
    g.fillRect (juce::Rectangle<float> (band.getX(), band.getBottom() - kMenuDivider,
                                        band.getWidth(), kMenuDivider));
}

void DwellScreen::paintTrain (juce::Graphics& g, juce::Rectangle<float> plot, juce::Colour ink) const
{
    const auto yFor = [&] (double db)
    {
        const auto t = (juce::jlimit (kFloorDb, kTopDb, db) - kFloorDb) / (kTopDb - kFloorDb);
        return plot.getBottom() - (float) t * plot.getHeight();
    };

    // The axis: the -60 floor, and the 0 dB line the first repeat stands on,
    // which is also the line a hold runs along.
    g.setColour (ink.withAlpha (0.25f));
    g.fillRect (juce::Rectangle<float> (plot.getX(), plot.getBottom(), plot.getWidth(), 1.0f));
    g.setColour (ink.withAlpha (0.18f));
    g.fillRect (juce::Rectangle<float> (plot.getX(), yFor (0.0), plot.getWidth(), 1.0f));

    const auto gain = loopGain();
    const auto perLapDb = gain > 0.0 ? 20.0 * std::log10 (gain) : -1000.0;
    const auto toFloor = repeatsToFloor();
    const auto stems = toFloor < 0 ? kMaxStems : juce::jlimit (1, kMaxStems, toFloor);

    // Spaced to fill the plot whatever the count, so the train reads as a
    // shape rather than as a time axis; the readout carries the time.
    const auto pitch = plot.getWidth() / (float) (stems + 1);
    const auto stemWidth = juce::jlimit (1.0f, 3.0f, pitch * 0.35f);

    g.setColour (ink);

    for (int k = 0; k < stems; ++k)
    {
        const auto db = perLapDb * (double) k;

        if (db < kFloorDb)
            break;

        const auto x = plot.getX() + pitch * (float) (k + 1);
        const auto top = yFor (db);

        g.fillRect (juce::Rectangle<float> (x - stemWidth * 0.5f, top, stemWidth, plot.getBottom() - top));
    }
}

void DwellScreen::paintRegion (juce::Graphics& g, juce::Rectangle<float> band, juce::Colour ink) const
{
    // Half a step either side of centre counts as the centre, so a drag caught
    // at the detent and a value back off a host's normalised lane both read
    // FREEZE.
    const auto slack = params.spec (Index::laneGain).step * 0.5f;
    const auto v = params.getReal (Index::laneGain);
    const auto region = v < -slack ? -1 : (v > slack ? 1 : 0);

    const char* words[] { "THROW", "FREEZE", "BUILD" };
    const auto w = band.getWidth() / 3.0f;

    for (int i = 0; i < 3; ++i)
    {
        const auto box = juce::Rectangle<float> (band.getX() + w * (float) i, band.getY(), w, band.getHeight());
        const auto lit = (i - 1) == region;

        ui::drawLabel (g, words[i], box, juce::Justification::centred,
                       ui::labelFont (kMenuSize, true), lit ? ink : ink.withAlpha (0.35f));
    }
}

void DwellScreen::paintGr (juce::Graphics& g, juce::Rectangle<float> band, juce::Colour ink) const
{
    auto label = band.removeFromLeft (24.0f);
    ui::drawLabel (g, "GR", label, juce::Justification::centredLeft,
                   ui::labelFont (kMenuSize, true), ink.withAlpha (0.7f));

    const auto bar = band.withSizeKeepingCentre (band.getWidth(), kGrBar);

    g.setColour (ink.withAlpha (0.18f));
    g.fillRect (bar);

    const auto fraction = juce::jlimit (0.0f, 1.0f, shownGr / kGrRangeDb);

    if (fraction > 0.0f)
    {
        g.setColour (ink);
        g.fillRect (bar.withWidth (std::max (2.0f, bar.getWidth() * fraction)));
    }

    // Quarter marks, so the bar carries a scale without printing numbers.
    g.setColour (ui::tokens().meterFace);

    for (int i = 1; i < 4; ++i)
        g.fillRect (juce::Rectangle<float> (bar.getX() + bar.getWidth() * (float) i * 0.25f,
                                            bar.getY(), 1.0f, bar.getHeight()));
}

//==============================================================================
DwellPanel::DwellPanel (ui::ModuleContext ctx)
    : ModulePanel (std::move (ctx)),
      time     (context.params.param (Index::time),     "TIME",     ui::Knob::Style::character, kFootFace, context.def.accent),
      note     (context.params.param (Index::note),     "NOTE",     ui::Knob::Style::character, kFootFace, context.def.accent),
      feedback (context.params.param (Index::feedback), "FEEDBACK", ui::Knob::Style::character, kFootFace, context.def.accent),
      mix      (context.params.param (Index::mix),      "MIX",      ui::Knob::Style::character, kFootFace, context.def.accent),
      sync     (context.params.param (Index::sync),     "SYNC", ui::tokens().switchAlt),
      lowCut   (context.params.param (Index::lowCut),   "LO CUT", context.def.accent),
      highCut  (context.params.param (Index::highCut),  "HI CUT", context.def.accent),
      drive    (context.params.param (Index::drive),    "DRIVE",  ui::Knob::Style::character, kPageFace, context.def.accent),
      modRate  (context.params.param (Index::modRate),  "RATE",   ui::Knob::Style::character, kPageFace, context.def.accent),
      modDepth (context.params.param (Index::modDepth), "DEPTH",  ui::Knob::Style::character, kPageFace, context.def.accent),
      duck     (context.params.param (Index::duck),     "DUCK",   context.def.accent),
      sendHeld (context.params.param (Index::send),     "SEND", context.def.accent),
      hold     (context.params.param (Index::hold),     "HOLD", context.def.accent),
      chop     (context.params.param (Index::chop),     "CHOP", context.def.accent),
      // **ON, not FX.** It sits on the LANE page beside the lane's AMOUNT,
      // under the lane's own type row; "FX" there would read as the main
      // delay's gate, which has its own page. Component name LANE FX.
      laneFx   (context.params.param (Index::laneFx),   "ON",   context.def.accent),
      fxLink   (context.params.param (Index::fxLink),   "LINK", context.def.accent),
      laneGain  (context.params.param (Index::laneGain),  "TAIL",  ui::Knob::Style::character, kPageFace, context.def.accent),
      laneTime  (context.params.param (Index::laneTime),  "TIME",  ui::Knob::Style::character, kPageFace, context.def.accent),
      // The lane's sync division, in LANE TIME's cell while SYNC is on, exactly
      // as NOTE takes TIME's in the foot. One switch governs both engines
      // (params.h), so the two swap together.
      laneNote  (context.params.param (Index::laneNote),  "NOTE",  ui::Knob::Style::character, kPageFace, context.def.accent),
      laneLevel (context.params.param (Index::laneLevel), "LEVEL", ui::Knob::Style::character, kPageFace, context.def.accent),
      fx       (context.params.param (Index::fx),       "FX",   context.def.accent),
      screen   (context.params, context.def.accent, context.gainReductionDb)
{
    character = std::make_unique<ChoiceRow> (context.params.param (Index::character),
                                             juce::StringArray { "CLEAN", "TAPE", "BUCKET" },
                                             ui::tokens().switchAlt, 13.0f, 3, kSwitchGap);

    stereo = std::make_unique<ChoiceRow> (context.params.param (Index::stereo),
                                          juce::StringArray { "STEREO", "PING-PONG", "DUAL" },
                                          ui::tokens().switchAlt, 12.0f, 3, kSwitchGap);

    fxType = std::make_unique<ChoiceRow> (context.params.param (Index::fxType),
                                          fxTypeLabels(),
                                          ui::tokens().switchAlt, 12.0f, 3, kSwitchGap);

    // The lane's cells: the same words off the same list, with prefixed
    // component names. See ChoiceRow's class comment.
    laneFxType = std::make_unique<ChoiceRow> (context.params.param (Index::laneFxType),
                                              fxTypeLabels(),
                                              ui::tokens().switchAlt, 12.0f, 3, kSwitchGap, "LANE");

    laneGain.setName  ("LANE TAIL");
    laneTime.setName  ("LANE TIME");
    laneNote.setName  ("LANE NOTE");
    laneLevel.setName ("LANE LEVEL");
    laneFx.setName    ("LANE FX");
    fxLink.setName    ("FX LINK");

    // The foot: the delay itself, a step larger than everything above it.
    for (auto* k : { &time, &note, &feedback, &mix })
    {
        k->setKnobSide (kFootKnob);
        k->setCaptionSize (kCaption);
        k->setShowsValue (true);
    }

    // Every page control prints its value, in Hertz, decibels, milliseconds or
    // a percent that means something -- none of which is found by eye.
    //
    // **And they are tagged one-piece for the Textured surface.** At 72 px the
    // cap is 20.88 px, 0.12 under the 21 px line `texturedFormFor` draws
    // between one-piece and ringed, so the size rule would decide it by a
    // fraction of a pixel of relayout. They are the secondary step, which is
    // what one-piece means (Frosty, 2026-09-25), so the tag says it outright.
    for (auto* k : { &drive, &modRate, &modDepth,
                     &laneGain, &laneTime, &laneNote, &laneLevel })
    {
        k->setKnobSide (kPageKnob);
        k->setCaptionSize (kCaption);
        k->setShowsValue (true);
        k->setTexturedForm (ui::Knob::TexturedForm::onePiece);
    }

    // The three faders on TONE print their values by default (ui::Fader), and
    // take the panel's caption size like every knob here.
    for (auto* f : { &lowCut, &highCut, &duck })
        f->setCaptionSize (kCaption);

    // **The catch at unity.** `lane_gain` is the one control on this panel
    // with a value that has to be hit exactly: 0 is the lane holding at unity.
    laneGain.setCatch (0.0, kLaneGainCatch);

    // SYNC is live from 2026-10-01 (params.h, kSyncIsEnabled): one switch, both
    // engines, each on its own division.
    sync.setSwitchEnabled (dwell::kSyncIsEnabled);
    sync.setLabelSize (11.0f);

    for (auto* s : { &sendHeld, &hold, &chop })
        s->setLabelSize (14.0f);

    for (auto* s : { &fx, &laneFx, &fxLink })
        s->setLabelSize (12.0f);

    screen.onPageChosen = [this] (Page p) { setPage (p); };

    for (auto* c : std::initializer_list<juce::Component*> { &screen, &feedback, &mix, &sync })
        addAndMakeVisible (c);

    lastSyncWasOn = context.params.getReal (Index::sync) > 0.5f;
    showNote (lastSyncWasOn);

    lastFxWasOn     = context.params.getReal (Index::fx) > 0.5f;
    lastLaneFxWasOn = context.params.getReal (Index::laneFx) > 0.5f;
    lastFxLinkWasOn = context.params.getReal (Index::fxLink) > 0.5f;

    buildFxAmount (false, juce::roundToInt (context.params.getReal (Index::fxType)));
    buildFxAmount (true,  juce::roundToInt (context.params.getReal (Index::laneFxType)));

    refreshFxLinkFollowing();

    startTimerHz (15);
}

DwellPanel::~DwellPanel()
{
    stopTimer();
}

bool DwellPanel::setUiState (const juce::String& key, const juce::String& value)
{
    if (key == "page")
    {
        for (const auto p : kPages)
            if (value.equalsIgnoreCase (DwellScreen::pageName (p)))
            {
                setPage (p);
                return true;
            }

        return false;
    }

    return ModulePanel::setUiState (key, value);
}

void DwellPanel::setPage (Page p)
{
    screen.setPage (p);
    resized();
    repaint();
}

//==============================================================================
void DwellPanel::showNote (bool syncOn)
{
    // One cell, one caption, two parameters. The dead one is removed rather
    // than hidden: a knob left parented with no bounds still gets measured by
    // the caption-fit assertion, and it would measure a zero-width box.
    auto& live = syncOn ? note : time;
    auto& dead = syncOn ? time : note;

    removeChildComponent (&dead);
    addAndMakeVisible (live);

    live.setKnobEnabled (dwell::kSyncIsEnabled || ! syncOn);

    // The lane's pair follows the same switch. Both are page controls, so
    // `resized` parents whichever is live; only its enablement is set here.
    (syncOn ? laneNote : laneTime).setKnobEnabled (dwell::kSyncIsEnabled || ! syncOn);
}

void DwellPanel::buildFxAmount (bool lane, int type)
{
    auto& slot = lane ? laneFxAmount : fxAmount;
    auto& held = lane ? laneFxAmountType : fxAmountType;

    if (slot != nullptr && held == type)
        return;

    const auto wasPresent = slot != nullptr && slot->getParentComponent() == this;
    const auto bounds = slot != nullptr ? slot->getBounds() : juce::Rectangle<int>();

    if (wasPresent)
        removeChildComponent (slot.get());

    slot = std::make_unique<ui::PlainKnob> (context.params.param (lane ? Index::laneFxAmount
                                                                      : Index::fxAmount),
                                            amountCaptionFor (type),
                                            ui::Knob::Style::character,
                                            kPageFace, context.def.accent);
    slot->setKnobSide (kPageKnob);
    slot->setCaptionSize (kCaption);
    slot->setShowsValue (true);
    slot->setTexturedForm (ui::Knob::TexturedForm::onePiece);
    held = type;

    // The drawn caption names what the percent moves; the component name says
    // which stage, for `findNamed`.
    slot->setName ((lane ? "LANE AMOUNT " : "AMOUNT ") + amountCaptionFor (type));

    if (wasPresent)
    {
        addAndMakeVisible (*slot);
        slot->setBounds (bounds);
    }

    refreshFxEnablement();

    // A rebuilt knob is a new object with the module accent on it, so the
    // following colour has to be put back.
    refreshFxLinkFollowing();
}

void DwellPanel::refreshFxEnablement()
{
    // With a stage's gate off its controls grey rather than vanishing. The DSP
    // skips the stage regardless; this is the panel saying so.
    const auto mainOn = context.params.getReal (Index::fx) > 0.5f;
    const auto laneOn = context.params.getReal (Index::laneFx) > 0.5f;

    fxType->setRowEnabled (mainOn);
    laneFxType->setRowEnabled (laneOn);

    if (fxAmount != nullptr)
        fxAmount->setKnobEnabled (mainOn);

    if (laneFxAmount != nullptr)
        laneFxAmount->setKnobEnabled (laneOn);
}

void DwellPanel::refreshFxLinkFollowing()
{
    // **Dimmed, not switched off.** The disabled look in this suite means "this
    // stage is not running", which a followed control is not -- it is running,
    // on the main delay's numbers. So the accent is dimmed by transparency
    // (kFollowAlpha): the same accent, quieter, rather than a step toward the
    // hairline grey (Frosty, 2026-10-01: "dim instead of grey").
    const auto followed = context.def.accent.withAlpha (kFollowAlpha);
    const auto own      = context.def.accent;
    const auto linked   = context.params.getReal (Index::fxLink) > 0.5f;

    if (laneFxType != nullptr)
        laneFxType->setRowTint (linked ? followed : ui::tokens().switchAlt);

    if (laneFxAmount != nullptr)
        laneFxAmount->setAccent (linked ? followed : own);
}

std::vector<juce::Component*> DwellPanel::allPageControls()
{
    std::vector<juce::Component*> all {
        character.get(), stereo.get(), &lowCut, &highCut, &drive, &modRate, &modDepth, &duck,
        &sendHeld, &hold, &chop, laneFxType.get(), &laneGain, &laneTime, &laneNote, &laneLevel,
        &laneFx, laneFxAmount.get(), &fxLink,
        fxType.get(), &fx, fxAmount.get() };

    all.erase (std::remove (all.begin(), all.end(), nullptr), all.end());
    return all;
}

void DwellPanel::timerCallback()
{
    const auto syncOn = context.params.getReal (Index::sync) > 0.5f;

    if (syncOn != lastSyncWasOn)
    {
        lastSyncWasOn = syncOn;
        showNote (syncOn);
        resized();
        repaint();
    }

    const auto fxOn = context.params.getReal (Index::fx) > 0.5f;
    const auto laneFxOn = context.params.getReal (Index::laneFx) > 0.5f;

    if (fxOn != lastFxWasOn || laneFxOn != lastLaneFxWasOn)
    {
        lastFxWasOn = fxOn;
        lastLaneFxWasOn = laneFxOn;
        refreshFxEnablement();
        repaint();                 // the glow follows both gates
    }

    const auto fxLinkOn = context.params.getReal (Index::fxLink) > 0.5f;

    if (fxLinkOn != lastFxLinkWasOn)
    {
        lastFxLinkWasOn = fxLinkOn;
        refreshFxLinkFollowing();
        repaint();
    }

    // The captions name what each percent moves, so they follow their types.
    buildFxAmount (false, juce::roundToInt (context.params.getReal (Index::fxType)));
    buildFxAmount (true,  juce::roundToInt (context.params.getReal (Index::laneFxType)));

    screen.refresh();
}

//==============================================================================
void DwellPanel::resized()
{
    clearRules();

    // SYNC read here as well as by the timer, so a layout always shows the
    // knobs the parameter says are live -- whoever asked for the layout.
    if (const auto syncOn = context.params.getReal (Index::sync) > 0.5f; syncOn != lastSyncWasOn)
    {
        lastSyncWasOn = syncOn;
        showNote (syncOn);
    }

    mixNoteBand = feedbackNoteBand = {};

    auto area = getLocalBounds().reduced (kPad, 4);
    area.removeFromBottom (kFootMargin);

    //== The foot, off the bottom first, so it sits on the panel's foot line ===
    {
        auto strips = area.removeFromBottom (kStripRow);
        auto foot = area.removeFromBottom (kFootRow);
        // The rule row is as tall as the switch on it, not the 16 px a bare rule
        // takes: SYNC centred on a 16 px row overhangs it by five pixels each
        // way and lands in MIX's box.
        auto rule = area.removeFromBottom (kSwitchHeight + 2);

        // The uneven trio. See kFeedbackCell.
        const auto side = (foot.getWidth() - kFeedbackCell) / 2;
        auto timeCell = foot.removeFromLeft (side);
        auto mixCell  = foot.removeFromRight (side);
        auto fbCell   = foot;

        (lastSyncWasOn ? note : time).setBounds (timeCell);
        feedback.setBounds (fbCell);
        mix.setBounds (mixCell);

        // The strips belong to the knobs above them and sit on their columns.
        feedbackNoteBand = strips.withX (fbCell.getX()).withWidth (fbCell.getWidth());
        mixNoteBand      = strips.withX (mixCell.getX()).withWidth (mixCell.getWidth());

        // SYNC at the right-hand end of the DELAY rule: it decides what TIME
        // *is*, so it belongs to the section rather than to the first row.
        sync.setBounds (rule.removeFromRight (kSwitchWidth)
                            .withSizeKeepingCentre (kSwitchWidth, kSwitchHeight));
        rule.removeFromRight (kSwitchGap);
        addRule (rule, "DELAY", { rule.getX(), rule.getRight() });
    }

    area.removeFromBottom (kGap);

    //== The page's grid and its two segment rows ==============================
    auto rowB = area.removeFromBottom (kGridRow);
    auto rowA = area.removeFromBottom (kGridRow);

    area.removeFromBottom (kGap);

    auto segB = area.removeFromBottom (kSegRow);
    area.removeFromBottom (kSwitchGap);
    auto segA = area.removeFromBottom (kSegRow);

    area.removeFromBottom (kGap);
    area.removeFromTop (kGap);

    //== The bezel and the screen, which take what is left =====================
    bezelBox = area;
    screenBox = area.reduced (kBezelPad);
    screen.setBounds (screenBox);

    //== Which controls this page carries ======================================
    for (auto* c : allPageControls())
        removeChildComponent (c);

    const auto put = [this] (juce::Component* c, juce::Rectangle<int> r)
    {
        if (c == nullptr)
            return;

        addAndMakeVisible (c);
        c->setBounds (r);
    };

    const auto putSwitch = [&put] (juce::Component& c, juce::Rectangle<int> cell)
    {
        put (&c, cell.reduced (kSwitchGap / 2, 0).withSizeKeepingCentre (cell.getWidth() - kSwitchGap,
                                                                           kSwitchHeight));
    };

    const auto putRow = [&put] (ChoiceRow* row, juce::Rectangle<int> band)
    {
        put (row,
             band.withSizeKeepingCentre (band.getWidth(), kSwitchHeight));
    };

    const auto a = cellsOf (rowA);
    const auto b = cellsOf (rowB);

    switch (getPage())
    {
        case Page::tone:
            // What the repeats are made of and where they sit, then three
            // faders down both grid rows: what is cut from them, and how far the
            // dry pushes them down (Frosty, 2026-10-01, "3 with sliders").
        {
            putRow (character.get(), segA);
            putRow (stereo.get(), segB);

            const auto tall = cellsOf (rowA.getUnion (rowB));
            put (&lowCut,  tall[0]);
            put (&highCut, tall[1]);
            put (&duck,    tall[2]);
            break;
        }

        case Page::lane:
        {
            // The throw: its three gates, then its tail, time and level, then
            // its own FX stage -- in the cells the FX page puts the main
            // delay's in.
            const auto gates = cellsOf (segA);
            put (&sendHeld, gates[0].reduced (kSwitchGap / 2, 0));
            put (&hold,     gates[1].reduced (kSwitchGap / 2, 0));
            put (&chop,     gates[2].reduced (kSwitchGap / 2, 0));

            putRow (laneFxType.get(), segB);

            put (&laneGain,  a[0]);
            put (lastSyncWasOn ? &laneNote : &laneTime, a[1]);
            put (&laneLevel, a[2]);

            putSwitch (laneFx, b[0]);
            put (laneFxAmount.get(), b[1]);
            putSwitch (fxLink, b[2]);
            break;
        }

        case Page::fx:
            // Everything done to the repeats inside the loop: the loop's colour
            // -- DRIVE, RATE, DEPTH -- on the top row, and the main delay's FX
            // stage under it, in the cells the lane's FX takes on LANE.
            putRow (fxType.get(), segB);
            put (&drive,    a[0]);
            put (&modRate,  a[1]);
            put (&modDepth, a[2]);
            putSwitch (fx, b[0]);
            put (fxAmount.get(), b[1]);
            break;
    }
}

//==============================================================================
void DwellPanel::paintPanel (juce::Graphics& g)
{
    const auto t = panelTokens();
    const auto accent = context.def.accent;

    // The bezel: a recess in `well`, BMO Linger's, holding the screen.
    g.setColour (t.well);
    g.fillRoundedRectangle (bezelBox.toFloat(), ui::Tokens::corner);
    g.setColour (ui::tokens().outline);
    ui::strokeInside (g, bezelBox.toFloat(), ui::Tokens::corner, ui::Tokens::hairlineWeight);

    const auto small = ui::labelFont (kSmallSize, true);

    /** The lit state of a gate: the accent with a glow, painted here under the
        switch because it has to spill past the button's own bounds. */
    const auto glow = [&] (const juce::Component& c, bool lit)
    {
        if (! lit || c.getBounds().isEmpty() || c.getParentComponent() != this)
            return;

        const juce::DropShadow bloom { accent.withAlpha (0.55f), 14, {} };
        bloom.drawForRectangle (g, c.getBounds());
    };

    glow (sendHeld, context.params.getReal (Index::send)   > 0.5f);
    glow (hold,     context.params.getReal (Index::hold)   > 0.5f);
    glow (chop,     context.params.getReal (Index::chop)   > 0.5f);
    glow (fx,       context.params.getReal (Index::fx)     > 0.5f);
    glow (laneFx,   context.params.getReal (Index::laneFx) > 0.5f);
    glow (fxLink,   context.params.getReal (Index::fxLink) > 0.5f);

    /** A short travel strip with one numbered mark on it: what a knob's own
        track cannot say, drawn under the knob it belongs to. */
    const auto strip = [&] (juce::Rectangle<int> band, float mark, float hotFrom,
                            const juce::String& text)
    {
        if (band.isEmpty())
            return;

        const auto w = juce::jmin (100, band.getWidth() - 6);
        auto line = juce::Rectangle<float> ((float) w, 3.0f)
                        .withCentre ({ (float) band.getCentreX(), (float) band.getY() + 5.0f });

        g.setColour (t.hairline);
        g.fillRect (line);

        if (hotFrom < 1.0f)
        {
            g.setColour (ui::tokens().meterClip);
            g.fillRect (line.withTrimmedLeft (line.getWidth() * hotFrom));
        }

        g.setColour (t.text1);
        g.fillRect (juce::Rectangle<float> (1.0f, 7.0f)
                        .withCentre ({ line.getX() + line.getWidth() * mark, line.getCentreY() + 1.0f }));

        ui::drawLabel (g, text, band.toFloat().withTrimmedTop (10.0f),
                       juce::Justification::centredTop, small, t.text1);
    };

    // FEEDBACK runs past unity: the loop's peak magnitude is 1 at 97 %, and
    // the top of the travel self-oscillates on purpose (docs/delay/10 §3).
    strip (feedbackNoteBand, 0.97f, 0.97f, "97 = UNITY");

    // MIX is not a crossfade: the dry holds bit-exact unity to 50 % and only
    // fades above it (docs/delay/10 §9).
    strip (mixNoteBand, 0.5f, 1.0f, "DRY TO 50");
}

} // namespace bmo::dwell
