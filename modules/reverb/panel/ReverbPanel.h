#pragma once

#include "core/product/ModuleDef.h"
#include "modules/reverb/dsp/TapTables.h"

#include <array>
#include <vector>

namespace bmo::reverb
{

//==============================================================================
/** The ER/tail display: the early reflections and the decay envelope, drawn
    from the parameters.

    **Static: no tap, no FFT, no timer, no audio path.** It redraws when one of
    the parameters it draws from moves and at no other time, which is the whole
    of its cost -- one path rebuilt when a knob moves. It reads neither
    `AnalyserTap` nor any of `ModuleContext`'s meter callbacks, and it is not a
    fallback for a meter that has not been written: neither
    `docs/reverb/10-dsp-spec.md` nor `11-integration-and-test-plan.md` asks
    this module for metering at all. BMO Defang's band sketch is the precedent
    and the same argument holds -- 11 section 5 makes it -- with one addition:
    a reverb's failure modes (flamming, a second onset, a tail that arrives
    with the ER instead of behind them) are invisible in a spectrum and visible
    here, and the Defang pass proved a render can find what every test passed
    over.

    It draws the taps from `TapTables.h`, which is **the same table the engine
    will play**. 11 section 5 names sketch/DSP drift as the display's one real
    risk and asks for exactly this: one source of truth, plus a layout test
    asserting the sketch's first tap time equals the table's. That assertion is
    what `firstTapTimeMs` is public for.

    ## The two axes, and why they are what they are

    **Time is logarithmic, 1 ms to 30 s.** A fixed 0-500 ms window -- which is
    what 11 section 5 proposes -- shows the ER cluster beautifully and cannot
    show a 20 s decay at all, and DECAY reaches 20 s with a 2.0x damping
    multiplier over it. A linear window wide enough for the tail puts the
    entire early cluster inside the first pixel, which destroys the one thing
    the picture is for. On a log axis 1-100 ms takes 45 % of the width, so the
    ER cluster keeps roughly half the picture *and* a 20 s tail still ends
    inside the box.

    The ends are chosen rather than round. 1 ms is where a reflection stops
    fusing with the direct sound (05 section 1.1), so nothing below it would be
    heard as a separate event. 30 s is `DspCore::kMaxTailSeconds`, the ceiling
    on the tail this module will ever report to a host -- so the right-hand
    edge of the picture is the same number the host is told, rather than a
    second opinion about it.

    **The direct sound sits at t = 0, which is off a log axis entirely**, so it
    is drawn as a full-height line hard against the left edge. That is a
    deliberate small lie and the only one: it is the reference every other
    distance in the picture is measured from, and leaving it out would make the
    pre-delay gap look like the beginning of the sound.

    **Level is mirrored about a centre line**, as an envelope rather than as a
    dB axis running down the box. Full deflection is 0 dB and the centre line
    is `kFloorDb`. Mirrored, because that is what lets a tap's pan be drawn at
    all: a tap is split above and below the line in the ratio its bearing
    gives, so a centre-panned tap is symmetric, a hard-left one hangs entirely
    below and a hard-right one stands entirely above. **That is a reading aid,
    not a measurement** -- the shipped decorrelation uses different tap sets
    per channel rather than one set with offsets (10 section 3), and a single
    table cannot show two.

    ## What is drawn, and what is not

    Drawn: the direct impulse, the ER taps at their times with heights from
    their gains and the ER fader over them, the pre-delay gap, and the tail
    envelope -- its Attack bloom, then its decay to -60 dB, with a lighter band
    between the fastest and slowest of the three decay times the damping
    multipliers give. ER at "Off" draws no taps; REVERB at "Off" draws no tail.

    Not drawn, and each for a reason: the modulation (it is a sub-millisecond
    delay wobble and would be invisible at this scale), the Reverb EQ and both
    high-cuts (this is a time-domain picture and a tilt in it would read as a
    level change), and ER MODE (Energy mode replaces the tap *times*, and the
    generator that produces them does not exist yet -- so drawing anything for
    it would be drawing a guess). */
class ErTailSketch final : public juce::Component
{
public:
    explicit ErTailSketch (juce::Colour accentColour);

    /** Everything the picture is drawn from, in the units `specs()` uses.
        One struct rather than eleven arguments, because the panel refreshes
        all of it at once from the parameters and a partial update is not a
        state this sketch can be in. */
    struct State
    {
        float sizeM        = 12.0f;
        float preDelayMs   = 0.0f;
        bool  linkEr       = false;
        float decaySeconds = 1.8f;
        float dampLo       = 1.20f;
        float dampHi       = 0.40f;
        float attack       = 30.0f;   ///< per cent, over the 0-120 ms bloom
        float erDensity    = 50.0f;   ///< per cent
        float erLevelDb    = -6.0f;
        float verbLevelDb  = -6.0f;
    };

    /** Repaints only when something has actually moved. */
    void setState (const State&);

    void paint (juce::Graphics&) override;

    //== Arithmetic, public so a test can assert it without rendering ==========
    //
    // Every one of these is a number the picture is built from rather than a
    // pixel it happens to land on, which is what makes them assertable at all
    // -- the same reason `ui::ModulePanel::getRules` and
    // `ui::DynamicsMeter::vuScale` are public.

    /** When the first reflection arrives, in milliseconds, at the current
        SIZE. **This is the sketch/DSP drift assertion**: it must equal
        `tapTimeMsAt (kReferenceTaps[0], size)` and it does so by calling it. */
    float firstTapTimeMs() const noexcept;

    /** When the last reflection arrives -- `t_ER,max` in the tail formula. */
    float lastTapTimeMs() const noexcept;

    /** When the tail's first sample arrives: the pre-delay, which is tail-only
        and can never be negative. */
    float tailStartMs() const noexcept { return state.preDelayMs; }

    /** Where the drawn tail reaches -60 dB, in seconds, on its slowest band --
        `decay * max(1, dampLo, dampHi)`. Clipped to the right-hand edge when
        it runs past it, which the picture shows by the envelope leaving the
        box rather than by stopping short of it. */
    float tailEndSeconds() const noexcept;

    /** How many of the table's taps DENSITY has switched on. The real bridge
        is a continuous ramp over 48 taps with a master sequence that does not
        exist yet (10 section 3); this is the 21 core taps scaled by the knob,
        so the picture thickens with the control. */
    int activeTapCount() const noexcept;

    /** The time window, as the class comment argues it. */
    static constexpr float kMinMs      = 1.0f;
    static constexpr float kMaxSeconds = 30.0f;

    /** The centre line, in dB below full deflection. -72 rather than -60 so
        the tail's own -60 point lands inside the box with room under it
        instead of on the line itself. */
    static constexpr float kFloorDb = -72.0f;

private:
    juce::Colour accent;
    State state;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ErTailSketch)
};

//==============================================================================
/** BMO Linger's panel: a main face of seven controls and the display, and an
    expanded column of three groups.

    ## The split

    **Main face:** the ER/tail display, TYPE, SIZE, PRE-DELAY, DECAY, **ER**,
    **REVERB**, MIX. The two faders are the thesis -- two generators with two
    absolute trims, so either can be switched off on its own -- and the tail-off
    depth-placement technique the module is partly for has to be reachable
    without expanding anything. `11` section 4 fixes this list.

    **Expanded**, three groups, in the order a reverb is built: **EARLY** (ER
    MODE, DENSITY, ER SHAPE, ER SPREAD, ER HI-CUT, VARIATION, LINK ER, SOURCE),
    **TAIL** (ATTACK, DECAY SHAPE and the four damping controls) and **TONE &
    OUT** (the four EQ rows, IN HI-CUT, WIDTH, MOD DEPTH, MOD RATE, OUTPUT).

    BMO DEQ's precedent throughout (`ModuleDef::expandedWidth`): the module
    opens **expanded standalone and compact in a rack**, the switch is on the
    host's bar and never on the panel, and the panel chooses its layout from
    the width it is given and needs no other signal.

    **The expanded controls are added and removed as children rather than
    hidden.** A hidden component still has bounds, and `tests/ui/LayoutTests`
    walks every child of a panel whether it is visible or not -- so a hidden
    control with a stale or zeroed rectangle either escapes the panel, overlaps
    something, or reports a caption overflowing a box of width zero. Unparenting
    is the one state in which a control is genuinely not part of this layout.
    They keep their parameter attachments throughout, so nothing is rebound and
    nothing is rebuilt when the width changes.

    ## Why the rules are painted rather than added

    `ui::ModulePanel::addRule` draws edge to edge, and at the expanded width a
    rule across the whole panel would cut through the column it does not belong
    to. So all six headings -- ROOM, TIME and LEVEL on the face, EARLY, TAIL and
    TONE & OUT over the groups -- are drawn in `paintPanel`, each confined to
    its own column, and `getFaceRules` and `getGroupRules` are public so a
    layout test can see where they landed. That is the same argument that made
    `getRules` public in the first place, and it is why this panel holds no
    `ModulePanel::Rule` at either width.

    Neither shared section is taken either: there is no input stage to trim
    (IN HI-CUT is a tone control in its own group, not a level) and OUTPUT
    belongs to TONE & OUT, where the rest of the output stage is.

    ## Two colours would read as two modules

    Every knob here is `ui::Knob::Style::character`, the module's accent, but
    OUTPUT, which is `utility` because a trim on the way out is what that style
    means -- the constructor carries the argument and BMO DEQ is the precedent.
    It is worth stating at this altitude because the failure is invisible from
    any one control: the twenty-three group knobs were `utility` as a block, so
    the module rendered violet on its face and suite azure below, and nothing
    in the layout suite asserts a knob's style.

    ## Why the face is 2 + 2 + 3

    Seven controls and no row with one thing in it: TYPE and SIZE, PRE-DELAY
    and DECAY, then ER, REVERB and MIX abreast with a bracket under the first
    two of the three. MIX had a row to itself, centred at half width, and a
    lone centred knob reads as an orphan however it is justified. The brackets
    and the headings-set-in-a-gap are both BMO Dimension's devices, and
    `getPairBoxes` exposes the boxes the brackets are drawn under. */
class ReverbPanel final : public ui::ModulePanel
{
public:
    explicit ReverbPanel (ui::ModuleContext);

    void resized() override;

    /** True when the panel has been given at least the expanded width. */
    bool isShowingExpanded() const noexcept;

    /** Where the three main-face headings were laid out, in the order ROOM,
        TIME, LEVEL. Present at both widths: the face is the same at either.
        See the class comment for why these are not `ModulePanel::Rule`s. */
    const std::vector<juce::Rectangle<int>>& getFaceRules() const noexcept { return faceRules; }

    /** The boxes the main face's three brackets are drawn under, in the same
        order: TYPE + SIZE, PRE-DELAY + DECAY, and ER + REVERB -- which is two
        thirds of the level row rather than the whole of it, because MIX shares
        that row and is not part of the pair. */
    const std::array<juce::Rectangle<int>, 3>& getPairBoxes() const noexcept { return pairBoxes; }

    /** Where the three group headings were laid out, in the order EARLY,
        TAIL, TONE & OUT -- empty at the compact width, where there are none.
        See the class comment for why these are not `ModulePanel::Rule`s. */
    const std::vector<juce::Rectangle<int>>& getGroupRules() const noexcept { return groupRules; }

    /** The column the groups are laid out in, empty when compact. For a test
        that wants to assert a group control is inside it rather than merely
        inside the panel. */
    juce::Rectangle<int> getGroupColumn() const noexcept { return groupColumn; }

    const ErTailSketch& getSketch() const noexcept { return sketch; }

private:
    void paintPanel (juce::Graphics&) override;

    /** Re-reads every parameter the display is drawn from and hands them over
        as one state. */
    void refreshSketch();

    /** The controls that only exist at the expanded width, in layout order. */
    std::vector<juce::Component*> groupControls() const;

    ErTailSketch sketch;

    // The main face. Seven controls, and the order of the members is the order
    // they are read in: what the room is, when and how long it rings, how much
    // of each generator, and how much of the whole thing.
    ui::PlainKnob typeKnob, sizeKnob, preDelayKnob, decayKnob;
    ui::PlainKnob erLevelKnob, verbLevelKnob, mixKnob;

    // EARLY.
    ui::PlainKnob erModeKnob, densityKnob, erShapeKnob, erSpreadKnob,
                  erHiCutKnob, variationKnob, feedKnob;
    ui::SwitchButton linkErSwitch;

    // TAIL.
    ui::PlainKnob attackKnob, decayShapeKnob,
                  dampLoFreqKnob, dampLoKnob, dampHiFreqKnob, dampHiKnob;

    // TONE & OUT.
    ui::PlainKnob eqLoFreqKnob, eqLoKnob, eqHiFreqKnob, eqHiKnob,
                  inHiCutKnob, widthKnob, modDepthKnob, modRateKnob, outputKnob;

    /** One per parameter the sketch is drawn from. Ten of them, and the list
        in the constructor is exactly `ErTailSketch::State`'s fields -- which
        is the thing a reader wants to be able to check at a glance. */
    std::array<std::unique_ptr<juce::ParameterAttachment>, 10> sketchAttachments;

    std::vector<juce::Rectangle<int>> faceRules;
    std::array<juce::Rectangle<int>, 3> pairBoxes {};

    std::vector<juce::Rectangle<int>> groupRules;
    juce::Rectangle<int> groupColumn;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ReverbPanel)
};

} // namespace bmo::reverb
