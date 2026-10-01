#pragma once

#include "core/product/ModuleDef.h"

#include <functional>
#include <vector>

namespace bmo::dwell
{

class ChoiceRow;

//==============================================================================
/** Which page the screen is showing.

    **UI state, not a parameter**, for BMO Linger's reasons (modules/reverb/
    panel/ReverbPanel.h): which page somebody is looking at is not something a
    session should carry, a host should automate or a preset should recall. It
    reaches the panel through `ModulePanel::setUiState` ("ui.page=tone|lane|fx"
    in tools/snapshot), which is also what makes every page renderable
    headlessly. */
enum class Page { tone = 0, lane, fx };

//==============================================================================
/** The screen: a dark display inside the bezel, carrying its own page menu and
    drawing a picture of the page under it. BMO Linger's handheld, on purpose
    (Frosty, 2026-10-01: "mimic linger's tab/page layout").

    **The top band is the menu**, TONE / LANE / FX in the screen's own ink with
    the page you are on in inverted video. It takes clicks -- `onPageChosen` --
    and nothing under it does.

    **What it draws is the loop, from the engine's own law.** TONE and FX draw
    the main delay's repeats and LANE draws the lane's, as a train of stems:
    one per lap at the delay time, each `g` times the last, from the first
    repeat at the 0 dB line down to -60. `g` comes from `GainLaws.h`, the same
    two functions `DspCore` runs, at `P_c` = 1 -- which is exactly the loop's
    magnitude at its own peak, because that normalisation is what makes the
    law character-independent (docs/delay/10 §3). So the picture is the
    slowest-decaying frequency's decay, which is the one a listener hears last.

    A loop at or above unity -- FEEDBACK past 97 %, or the lane at FREEZE or in
    BUILD -- is drawn as what it is: level stems for a hold, rising ones for a
    build, stopping at the +12 dB ceiling the axis carries. The safety clip is
    what really bounds it, and the readout says HOLDS or BUILDS rather than a
    count that would be infinite.

    On LANE the three regions of the tail knob are lettered across the top of
    the picture, the live one lit -- the THROW / FREEZE / BUILD caption that
    stood under the knob in the column layout, moved to where the picture of
    what each one does is.

    **The GR bar lives here now**, along the foot of the TONE page, because
    DUCK is on that page and a 120 px cell has nowhere beside a knob to put a
    bar. It is the only thing on the screen that moves on its own, so the
    screen's timer runs **only while TONE is showing**, BMO Linger's analyser
    rule: the other two pages cost nothing while nobody is touching them. */
class DwellScreen final : public juce::Component,
                          private juce::Timer
{
public:
    DwellScreen (ParamSet& params, juce::Colour accent, std::function<float()> gainReductionDb);
    ~DwellScreen() override;

    void setPage (Page);
    Page getPage() const noexcept { return page; }

    /** Called when the menu band is clicked. The panel answers it with its own
        `setPage`, so the controls under the screen follow the picture. */
    std::function<void (Page)> onPageChosen;

    /** Repaints if anything the picture is drawn from has moved. Polled by the
        panel's timer, because a lane, a preset and a knob all move these. */
    void refresh();

    void paint (juce::Graphics&) override;
    void mouseUp (const juce::MouseEvent&) override;

    //== The numbers the picture is built from ================================
    //
    // Public so a test can read them without rendering anything, the way BMO
    // Linger's screen exposes its tap times.

    /** The loop gain the current page draws: FEEDBACK's on TONE and FX, the
        lane's on LANE, both at `P_c` = 1. */
    double loopGain() const;

    /** How many repeats are at or above -60 dB relative to the first, or -1
        when the loop holds or builds and the answer is "all of them". */
    int repeatsToFloor() const;

    /** The delay time the current page draws, in ms. */
    double delayMs() const;

    /** The line printed along the foot of the picture. */
    juce::String readout() const;

    juce::Rectangle<float> menuBand() const;
    juce::Rectangle<float> menuSegment (Page) const;
    juce::Rectangle<float> plotBounds() const;

    static juce::String pageName (Page);

    /** The +12 dB the axis tops out at, and the -60 it floors at. */
    static constexpr double kTopDb   = 12.0;
    static constexpr double kFloorDb = -60.0;

    /** The most stems drawn. A loop at 99 % feedback decays for thousands of
        laps; past this the train is drawn as far as it goes and the readout
        carries the count. */
    static constexpr int kMaxStems = 48;

private:
    void timerCallback() override;

    void paintMenu   (juce::Graphics&, juce::Colour ink) const;
    void paintTrain  (juce::Graphics&, juce::Rectangle<float> plot, juce::Colour ink) const;
    void paintRegion (juce::Graphics&, juce::Rectangle<float> band, juce::Colour ink) const;
    void paintGr     (juce::Graphics&, juce::Rectangle<float> band, juce::Colour ink) const;

    /** Everything `paint` reads, folded into one number, so `refresh` can tell
        whether anything moved without keeping a copy of each. */
    double inputsHash() const;

    ParamSet& params;
    juce::Colour accent;
    std::function<float()> reduction;
    Page page = Page::tone;
    double lastHash = 0.0;
    float shownGr = 0.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DwellScreen)
};

//==============================================================================
/** BMO Dwell's panel: BMO Linger's paged handheld, one width, 380.

    Top to bottom:

    - **The bezel and the screen**, whose top band is the page menu. See
      `DwellScreen`.
    - **Two segment rows**, which each page fills or leaves bare.
    - **A 3 x 2 grid of the page's controls**, in 120 px cells.
    - **The foot**, which never changes: the DELAY rule with SYNC on it, then
      TIME, FEEDBACK and MIX, and the two strips that mark FEEDBACK's unity and
      MIX's dry hinge.

    | page | segment rows | grid |
    |---|---|---|
    | TONE | CHARACTER; STEREO | LO CUT, HI CUT, DRIVE / RATE, DEPTH, DUCK |
    | LANE | SEND, HOLD, CHOP; the lane's FX types | TAIL, TIME, LEVEL / FX ON, AMOUNT, LINK |
    | FX   | --; the main delay's FX types | -- / FX, AMOUNT, -- |

    **The lane's FX is on the LANE page** (Frosty, 2026-10-01), and the FX page
    is the main delay's alone. The two FX stages sit in the **same cells** on
    their two pages -- types in the second segment row, gate, AMOUNT and link
    across the bottom row -- so flipping between LANE and FX moves nothing but
    what the controls are bound to. That is also why the FX page's top row is
    bare: the main stage has three controls and they are kept where the lane's
    are, rather than moved up to fill the page.

    **Every page's controls are unparented when it is not showing**, not
    hidden: a hidden component still has bounds, and every walker in the
    layout suite reads them. BMO Linger's rule.

    ## The lane gain knob

    `lane_gain` is bipolar, -100..+100, and **0 is exact unity**: below it the
    caught word decays, at it the word holds, above it the word builds. One
    loop gain with three regions rather than three modes, which is why the
    parameter is a float and this is a knob. It carries a **catch at the
    centre** (`ui::Knob::setCatch`), drag-only and opt-in, so unity is findable
    by hand; the region it is in is lettered on the screen above it.

    ## FX LINK

    FX LINK ties the lane's FX stage to the main delay's and **defaults on**.
    While it is on, the lane's type row and AMOUNT are drawn **following rather
    than dead**: the module accent stepped back toward the hairline, never the
    disabled dim, which in this suite means "this stage is off" and would be a
    lie. Nothing is seeded when it is switched off.

    ## What is not touched here

    The accent is the one literal in `Module.cpp`. Captions are 15 pt, the
    suite default. No control is labelled DWELL, and nothing on the panel names
    a piece of hardware. */
class DwellPanel final : public ui::ModulePanel,
                         private juce::Timer
{
public:
    explicit DwellPanel (ui::ModuleContext);
    ~DwellPanel() override;

    void resized() override;

    /** "page" = "tone" | "lane" | "fx". */
    bool setUiState (const juce::String& key, const juce::String& value) override;

    void setPage (Page);
    Page getPage() const noexcept { return screen.getPage(); }

    juce::Rectangle<int> getBezelBox() const noexcept  { return bezelBox; }
    juce::Rectangle<int> getScreenBox() const noexcept { return screenBox; }
    DwellScreen& getScreen() noexcept                  { return screen; }

private:
    void paintPanel (juce::Graphics&) override;
    void timerCallback() override;

    /** TIME and NOTE share one cell, so only one of them is ever a child. */
    void showNote (bool sync);

    /** An FX AMOUNT knob is rebuilt when its type changes, because its caption
        names what the percent moves and a PlainKnob's caption is fixed at
        construction. */
    void buildFxAmount (bool lane, int type);

    void refreshFxEnablement();

    /** Draws the lane's FX type row and AMOUNT as following the main delay's,
        or as the lane's own. */
    void refreshFxLinkFollowing();

    /** Every control a page can carry, so `resized` can unparent the ones the
        current page does not. */
    std::vector<juce::Component*> allPageControls();

    juce::Rectangle<int> bezelBox, screenBox;
    juce::Rectangle<int> mixNoteBand, feedbackNoteBand;

    // The foot.
    ui::PlainKnob time, note, feedback, mix;
    ui::SwitchButton sync;

    // TONE.
    std::unique_ptr<ChoiceRow> character, stereo;
    ui::PlainKnob lowCut, highCut, drive, modRate, modDepth, duck;

    // LANE. `laneGain` is captioned TAIL, and the lane's knobs repeat the main
    // delay's words; they are told apart by **component names** (LANE TAIL,
    // LANE TIME, ...), which is what `findNamed` in tests/ui/LayoutTests.cpp
    // walks.
    ui::SwitchButton sendHeld, hold, chop, laneFx, fxLink;
    ui::PlainKnob laneGain, laneTime, laneLevel;
    std::unique_ptr<ChoiceRow> laneFxType;
    std::unique_ptr<ui::PlainKnob> laneFxAmount;

    // FX, the main delay's stage.
    ui::SwitchButton fx;
    std::unique_ptr<ChoiceRow> fxType;
    std::unique_ptr<ui::PlainKnob> fxAmount;

    DwellScreen screen;

    int  fxAmountType = -1;
    int  laneFxAmountType = -1;
    bool lastSyncWasOn = false;
    bool lastFxWasOn = false;
    bool lastLaneFxWasOn = false;
    bool lastFxLinkWasOn = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DwellPanel)
};

} // namespace bmo::dwell
