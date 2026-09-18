#include "Line.h"

namespace bmo::ui
{

const Line& bmoLine()
{
    // No ground of its own, deliberately. BMO *is* the stock palette, so
    // giving it a copy of those three colours here would create a second place
    // that has to agree with Tokens.h and would eventually stop agreeing.
    static const Line line { "LT3a" };
    return line;
}

const Line& ltvLine()
{
    // Frosty's calls, 2026-09-14, from rendered candidates on AURORA.
    //
    // The pale ground is a Teletronix-style brushed silver. #c8c8c8 was chosen
    // over a brighter #d5d5d5 and a darker #bdbdbd: the bright one is closest
    // to polished aluminium and the darkest one needed no help, but the middle
    // reads most like a real panel.
    //
    // **The well is the part that is not a taste call.** Against the suite's
    // `#d6d6d6` a silver plate measures 1.01:1 at #d5d5d5 and 1.15:1 at
    // #c8c8c8 -- the recesses vanish, which a render showed before this
    // struct existed. So the line carries a well of its own, dropped until it
    // clears the plate by about what the suite's pair clears by (`#efefef`
    // over `#d6d6d6` is 1.26:1 and 8.8 L*). #b0b0b0 under #c8c8c8 is 1.26:1
    // and 8.7 L*, which is the same relationship one step darker.
    //
    // plateEdge follows the plate a touch darker, as `#e4e4e4` does under
    // `#efefef` in the stock set.
    //
    // The dark appearance is a graphite of the same family rather than the
    // silver carried through. Two answers were defensible -- a rack of black
    // units with one silver unit in it is what real hardware looks like -- and
    // this is the one that keeps the appearance preference meaningful on an
    // LTV panel. The spacing mirrors the stock dark set, whose greys are set
    // by L* rather than by ratio because below about L* 20 a ratio stops
    // discriminating.
    // Dark caps in both appearances -- Frosty, 2026-09-14 -- which is the
    // hardware look and the reason `Ground` carries a cap at all. They are not
    // the *same* dark in both, and the second call is the better one.
    //
    // **Graphite does double duty: the dark appearance's plate is the pale
    // appearance's knob cap.** That is not a coincidence kept for tidiness, it
    // is what stops this line needing a fifth colour. A knob on a silver panel
    // wants to be the dark thing on it; the dark thing this line already owns
    // is its own plate.
    static const juce::Colour graphite { 0xff3a3a3e };

    //   on silver #c8c8c8 :  6.77:1, 56.0 L*   -- the cap against its plate
    //   white pointer on it: 11.32:1           -- the mark against the cap
    //
    // Near-black on the graphite plate, because there the cap has to go darker
    // than a plate that is already dark, and #3a3a3e on itself is nothing.
    // What makes that affordable is graphite rather than silver in the dark
    // appearance: on the *suite's* dark plate there is almost nothing below it
    // -- #2e2e32 to pure black is 1.29:1, the ceiling Tokens.h records --
    // while graphite sits a step up and leaves room underneath.
    //
    //   #141414 on graphite: 1.63:1, 18.2 L*, with knobEdge ringing it
    //   white pointer on it: 18.42:1
    static const juce::Colour nearBlack { 0xff141414 };

    // The ink follows the knob, which is what Frosty asked for, and it can
    // only do that per appearance -- see Line::Ground::ink for the table.
    //
    // Light: the cap itself, 6.77:1 on silver. The caption is the colour of
    // the knob it names, exactly.
    //
    // Dark: the knob's *other* colour. There the cap is near-black on a dark
    // plate at 1.63:1, so following the cap would put a 15 pt caption at a
    // ratio the suite does not spend on anything, and the thing that actually
    // reads on that knob is the white mark across it. #e6e6ea is the dark
    // set's own text colour and 9.10:1 here, so the caption matches the
    // pointer rather than the face -- still the knob, the legible half of it.
    static const juce::Colour darkInk { 0xffe6e6ea };

    static const Line line
    {
        "LTV",
        Line::Ground { juce::Colour (0xffc8c8c8),    // plate, brushed silver
                       juce::Colour (0xffbdbdbd),    // plateEdge
                       juce::Colour (0xffb0b0b0),    // well
                       graphite,                     // cap
                       graphite },                   // ink
        Line::Ground { graphite,                     // plate
                       juce::Colour (0xff434347),    // plateEdge
                       juce::Colour (0xff27272b),    // well
                       nearBlack,                    // cap
                       darkInk },                    // ink
    };

    return line;
}

std::optional<juce::Colour> inkFor (const Line& line)
{
    if (! line.ownsGround())
        return {};

    return (isDarkMode() ? *line.dark : *line.light).ink;
}

std::optional<juce::Colour> capFor (const Line& line)
{
    if (! line.ownsGround())
        return {};

    return (isDarkMode() ? *line.dark : *line.light).knobCap;
}

Tokens groundFor (const Line& line)
{
    auto t = tokens();

    if (! line.ownsGround())
        return t;

    const auto& ground = isDarkMode() ? *line.dark : *line.light;

    // Each of the three yields independently. A theme that names only `plate`
    // has said something about faceplates and nothing about wells, so the
    // line keeps the well it needs to stay legible on the plate the theme
    // just imposed -- which is the behaviour that degrades most gracefully.
    if (! themeSets ("plate"))     t.plate     = ground.plate;
    if (! themeSets ("plateEdge")) t.plateEdge = ground.plateEdge;
    if (! themeSets ("well"))      t.well      = ground.well;

    return t;
}

} // namespace bmo::ui
