#pragma once

#include "Controls.h"
#include "Line.h"
#include "core/dsp/AnalyserTap.h"
#include "core/state/ParamSet.h"

namespace bmo
{
struct ModuleDef;
}

namespace bmo::ui
{

class ModulePanel;

/** The palette a control should paint a *ground* with -- a meter trough, a
    pressed button, anything cut into the faceplate.

    Use this in place of `tokens()` wherever a control fills a recess. Ink is
    unaffected: a line owns its ground and nothing else, so text, knob faces,
    accents and meter colours all still come from `tokens()`.

    Falls back to `tokens()` for a control that is not inside a panel. */
Tokens panelTokensFor (const juce::Component& c);

/** The line a control belongs to, found by walking up to its panel. BMO for a
    control that is in no panel. */
const Line& panelLineFor (const juce::Component& c);

/** The colour a control should use in place of a module accent: the line own
    ink where it has one, and `fallback` -- normally the module accent --
    where it does not. */
juce::Colour panelAccentFor (const juce::Component& c, juce::Colour fallback);

/** What a module's panel is built against. The same whether the module is
    running as its own plugin or sitting in a rack slot. */
struct ModuleContext
{
    ParamSet& params;
    const ModuleDef& def;
    std::function<float()> peak;    ///< output level, linear, both channels
    std::function<float()> rms;

    // Optional: empty for every module that does not ask for one. Only a
    // dynamics module's panel (BMO Opto's DynamicsMeter) reads these today --
    // see core/product/ModuleEngine.h. Check before calling: a
    // default-constructed std::function throws if invoked.
    std::function<float()> inputPeak;         ///< input level, linear, before the DSP
    std::function<float()> inputRms;
    /** Gain the module is moving right now, dB, **signed: positive is gain
        taken away, negative is gain added**.

        It read "always >= 0" until 2026-09-15, and for BMO Opto and LTV Comp
        it still is -- a compressor only cuts. BMO DEQ is why it widened: an
        upward dynamic band was reporting zero, so a band boosting 12 dB drew
        the same empty meter as a band switched off. A panel that reads this
        and shows only reduction should clamp at zero rather than assume the
        sign, because whether it can arrive depends on the module underneath
        and not on this declaration. */
    std::function<float()> gainReductionDb;

    /** The rate the module is running at, or 0 before the host has prepared it.

        For a panel that draws something rate-dependent. BMO DEQ's response
        curve is the only one today: its bands are designed at the running rate
        and it drew the 48 kHz design at every rate until 2026-09-15, which is
        up to 1 dB out in the top octave at 44.1 and 96 k. Poll it rather than
        reading it once -- a host can re-prepare a plugin with its editor open,
        and 0 means "not yet", not "no audio". */
    std::function<double()> sampleRate;

    // The panel-to-DSP direction, and the only one that is not a parameter.
    // `setSolo` is momentary: the panel calls it while a control is held and
    // calls it with -1 on release. Nothing saves it and nothing automates it.
    // `analyser` is null unless the module has a tap; a panel enables it when
    // it opens and disables it when it closes, so a module with no editor on
    // screen pays nothing (core/dsp/AnalyserTap.h).
    std::function<void (int)> setSolo;
    AnalyserTap* analyser = nullptr;
};

/** Base of every module panel: a fixed-size faceplate of the module's design
    width and the common content height, below whatever header the product
    puts over it.

    A panel paints its own plate, so a rack of them reads as one surface.

    It does **not** guarantee that section rules line up across modules, which
    this comment claimed until 0.2.3 and which has never been true: in a rack
    of util, eq, sat and opto the first rule sits at y 379, 283, 347 and
    nowhere respectively. Lining them up needs a shared row grid, and whether
    they should line up is per-module -- Frosty's call was "some should, some
    should not". Until that pass happens, a rule is at whatever height its own
    panel's layout puts it.
*/
class ModulePanel : public juce::Component
{
public:
    /** Header 28 + preset row 24 + this = the common 740. */
    static constexpr int kContentHeight = 688;
    static constexpr int kPad     = 10;

    /** Section legend type size. */
    static constexpr float kLegendSize = 13.0f;
    static constexpr int kRuleRow = 16;

    //== The input and output sections ==========================================
    //
    // Two blocks a module may opt into, so that the modules which have them put
    // them in the same place. A trim knob and a rule at the top; a rule, a
    // switch row and a trim knob at the bottom. Take them with takeInputSection
    // and takeOutputSection rather than laying the rows out by hand.
    //
    // They are opt-in and most modules will not want both. BMO EQ and the
    // Saturator take both. BMO Util takes neither -- its VOLUME is the thing
    // that module does rather than a trim either side of it, and it has no
    // output stage -- but it still takes the *reservation*, because that is
    // what puts its lower rule on the same line as theirs. BMO Opto takes
    // neither and reserves nothing.
    //
    // In panel-local pixels, on a content area inset by (kPad, 4):
    //
    //     input   knob 4..81, rule 82..97          line drawn at y 90
    //     output  rule 558..573, switches 574..601,
    //             knob 602..679, then 4 px of foot  line drawn at y 566
    //
    // which leaves 98..557 -- 460 px -- for whatever the module actually is.
    //
    // These lived in BMO EQ until 0.2.3, with the Saturator holding a second
    // copy under names that said "Eq" and Util deriving its rule position from
    // EQ's total column height by arithmetic. Three encodings of one fact, none
    // of them tested, and all three drifted at least once.

    static constexpr int kTrimKnobRow     = 78;   ///< input and output alike
    static constexpr int kOutputSwitchRow = 28;   ///< bypass, polarity, one more
    static constexpr int kFootMargin      = 4;    ///< plate under the output knob

    /** What takeOutputSection reserves off the foot of the content area. */
    static constexpr int kOutputSection = kRuleRow + kOutputSwitchRow
                                        + kTrimKnobRow + kFootMargin;

    struct InputSection  { juce::Rectangle<int> knob, rule; };
    struct OutputSection
    {
        juce::Rectangle<int> rule;      ///< the hairline row
        juce::Rectangle<int> switches;  ///< bypass / polarity / module switch
        juce::Rectangle<int> knob;      ///< the output trim
        juce::Rectangle<int> body;      ///< everything under the rule, foot included
    };

    /** Takes the input section off the top of `area`. */
    static InputSection takeInputSection (juce::Rectangle<int>& area)
    {
        InputSection in;
        in.knob = area.removeFromTop (kTrimKnobRow);
        in.rule = area.removeFromTop (kRuleRow);
        return in;
    }

    /** Takes the output section off the **bottom** of `area`.

        Off the bottom, and before anything else is placed, which is the whole
        point: what a module's own controls get is then whatever is left over,
        rather than a number someone worked out and has to redo when a row
        changes. A module that wants only the rule position -- Util -- uses
        `rule` and `body` and ignores the other two. */
    static OutputSection takeOutputSection (juce::Rectangle<int>& area)
    {
        auto section = area.removeFromBottom (kOutputSection);

        OutputSection out;
        out.rule     = section.removeFromTop (kRuleRow);
        out.body     = section;
        out.switches = section.removeFromTop (kOutputSwitchRow);
        out.knob     = section.removeFromTop (kTrimKnobRow);
        return out;
    }

    /** One size and one type size for every trim knob in the suite.

        INPUT and OUTPUT are the same control wherever they appear and are read
        once, when you go looking for them -- so they are capped at the size BMO
        EQ's crowded column can afford, and named a step under the section
        legends rather than at the 15 pt a knob you actually turn gets. */
    static void styleTrimKnob (PlainKnob& knob)
    {
        knob.setKnobSide (Tokens::gainKnobSide);
        knob.setCaptionSize (Tokens::gainCaptionSize);
    }

    //== Section rules =========================================================
    //
    // A rule is painted, not placed, so unlike every other thing on a panel it
    // has no component and no bounds anyone can read. Three panels each kept a
    // private copy of this vector and a byte-identical paintPanel to draw it,
    // which is three encodings of one fact -- the same shape as the section
    // constants before 0.2.3, and it drifts the same way.
    //
    // It lives here now, and `getRules` is public because a layout test has no
    // other way to see where a rule landed. That is the point: the rules are
    // what the panels are supposed to agree about.

    struct Rule
    {
        juce::Rectangle<int> row;
        juce::String text;          ///< empty for a bare rule
    };

    /** The rules this panel laid out, in the order `resized` added them. */
    const std::vector<Rule>& getRules() const noexcept { return rules; }

    //== UI state that is not a parameter ======================================

    /** Sets a piece of panel state that has no parameter behind it, and
        returns false for any key or value this panel does not understand.

        BMO Opto's meter mode is the only one today, and the reason this
        exists: `DynamicsMeter::Mode` is set through `setMode` rather than
        being a parameter -- rightly, since `specs()` is frozen and
        append-only and which way a meter is pointing does not belong in a
        session. But it meant `tools/snapshot` could only ever render OUT, so
        every VU change on the ui-editor branch was verified in one mode of
        three, including 0.2.3's resizing of the IN/GR/OUT row itself.

        **Refuse what you do not understand rather than ignoring it.** The
        snapshot tool learned this once already: a mistyped choice name used
        to come back 0.0 from getFloatValue() and render an entirely plausible
        panel of the wrong thing, which is why `realValueFor` now refuses.
        Quietly ignoring `ui.meter=GR` would hand back an OUT render that
        everything downstream would label a GR one. */
    virtual bool setUiState (const juce::String& key, const juce::String& value)
    {
        juce::ignoreUnused (key, value);
        return false;
    }

    explicit ModulePanel (ModuleContext ctx) : context (std::move (ctx)) {}

    const ModuleContext& getContext() const noexcept { return context; }

    /** Out of line, with paintRules and for the same reason: the ground comes
        from the module's line and `def` is only forward-declared here. */
    void paint (juce::Graphics& g) override;

    /** The palette this panel paints with: the tokens in force with its
        line's ground substituted. `tokens()` for every BMO module, silver or
        graphite for an LTV one. See ui::Line. */
    Tokens panelTokens() const;

protected:
    /** Anything the module draws itself, over its rules and its plate. */
    virtual void paintPanel (juce::Graphics&) {}

    /** Call at the top of `resized`, before laying any rule out again. */
    void clearRules() { rules.clear(); }

    /** Records a rule so the panel paints it and a test can see it. */
    void addRule (juce::Rectangle<int> row, juce::String text = {})
    {
        rules.push_back ({ row, std::move (text) });
    }

private:
    /** Defined in ModulePanel.cpp: reading `def.accent` needs the complete
        ModuleDef, and this header is the one ModuleDef.h includes. */
    void paintRules (juce::Graphics&) const;

    std::vector<Rule> rules;

protected:

    /** A hairline through the middle of a row, inset by the padding. */
    void drawRule (juce::Graphics& g, juce::Rectangle<int> row) const
    {
        g.setColour (tokens().hairline);
        g.fillRect (juce::Rectangle<float> ((float) kPad, (float) row.getCentreY(),
                                            (float) (getWidth() - kPad * 2), Tokens::hairlineWeight));
    }

    /** A section name drawn on a rule, in the module's own colour.

        Pass the raw accent: it is stepped to a legible contrast against the
        plate here, so a panel never has to know how. As the raw accent these
        measured 1.72-2.00:1 -- the panel's navigation was the second least
        readable thing on it. */
    void drawRuleLegend (juce::Graphics& g, juce::Rectangle<int> row,
                         const juce::String& text, juce::Colour accent,
                         juce::Colour plate) const
    {
        // The module's accent stepped until it is legible. A section legend is
        // the smaller of the two labels on a panel -- 13 pt against a knob
        // caption's 15 -- and contrast is worth more to the smaller of two
        // sizes than to the larger, so the legible step goes here and the raw
        // accent goes on the caption. See PlainKnob::paint, which is the other
        // half of this and carries the numbers.
        //
        // 0.2.2 had it the other way round, and the legend went to 13 pt to
        // survive being set in the raw accent at 2.00:1. It keeps the size:
        // the two labels want to be different sizes whichever way the colours
        // fall, and this is the one a panel is navigated by.
        drawRule (g, row);

        const auto font = labelFont (kLegendSize, true);
        const auto width = juce::GlyphArrangement::getStringWidth (font, text) + 14.0f;
        const auto box = juce::Rectangle<float> (width, (float) row.getHeight())
                             .withCentre (row.toFloat().getCentre());

        // The panel's plate, passed in rather than read from tokens(): a
        // legend knocks a hole in the rule it sits on, and on an LTV panel
        // that hole has to be silver or the rule shows through it.
        g.setColour (plate);
        g.fillRect (box);
        drawLabel (g, text, box, juce::Justification::centred, font, accentInk (accent, plate));
    }

    ModuleContext context;
};

} // namespace bmo::ui
