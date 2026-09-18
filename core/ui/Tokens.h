#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace bmo::ui
{

/** The design tokens: every colour the suite draws with, named for what it is
    used on rather than for what it looks like, so a theme can change any of
    them without touching a panel.

    Every module uses the same neutral plate and the same structure; only
    `accent` differs between modules, and that is set per module by
    ModuleDef::accent rather than here. A rack reads as one instrument, and you
    can still tell the EQ from the saturator at a glance.

    The non-colour tokens -- corner radius, stroke weights, the knob-to-label
    gaps -- are fixed and not themable, so a theme cannot break a layout.
*/
struct Tokens
{
    juce::Colour plate      { 0xffefefef };   ///< the faceplate
    juce::Colour plateEdge  { 0xffe4e4e4 };   ///< header and preset strip
    juce::Colour well       { 0xffd6d6d6 };   ///< recessed areas, meter backgrounds
    juce::Colour hairline   { 0xffb4b4b4 };   ///< section rules
    juce::Colour outline    { 0xff9e9e9e };   ///< knob edges, control outlines

    juce::Colour text1      { 0xff6f6f6f };   ///< legends, values
    juce::Colour text2      { 0xff9a9a9a };   ///< secondary, dimmed, disabled

    juce::Colour knobFace   { 0xff97ddff };   ///< utility knob caps
    juce::Colour knobEdge   { 0xffa6a6a6 };   ///< single-element rings, disengaged switches

    /** What a character knob's cap is mixed half way to -- see faceOf.

        White in both appearances today. It is still a per-appearance token
        rather than the literal `Colours::white` it was inside faceOf() until
        0.2.2, because that literal was the one part of the palette a theme
        could not reach, and because the knob treatments are meant to be able
        to differ per appearance even while they happen not to. */
    juce::Colour knobTint   { 0xffffffff };

    // pointer, ringFace and meterInk were one `pointer` token, #ffffff, until
    // 0.2.2. It was doing five jobs at once, and they stopped agreeing the
    // moment the plate was allowed to go dark: a needle wants maximum contrast
    // against its own face, an annulus wants to read as a raised ring on the
    // plate, and a knob's pointer answers to the cap rather than to either.

    /** The pointer on a knob cap, and the one thing that really differs
        between the two appearances.

        White on the pale plate, and 1.39-1.49:1 against the caps it is drawn
        on -- Frosty's call, taken with the number in front of him: it is the
        suite's look and he would rather have it than the contrast. Near-black
        on the dark plate, where the caps are the lightest thing in the window
        and dark is the handsome choice as well as the legible one, at about
        9.5:1. */
    juce::Colour pointer    { 0xffffffff };

    /** The selector-ring annulus on a band -- BMO EQ's frequency switches.

        White on the pale plate, where it measures 1.15:1 and is defined by the
        two hairline circles drawn around it rather than by its own value. That
        is the suite's look and it stays. On the dark plate the same white ring
        became the brightest thing in the window, so the dark set takes it to a
        middle grey -- see darkTokens. */
    juce::Colour ringFace   { 0xffffffff };
    juce::Colour meterInk   { 0xffffffff };   ///< VU needle, ticks and printed scale

    /** The plate a needle meter's scale is printed on.

        Lightened from #3a3a3a in 0.2.2. White numbers were never the limit --
        they still read 9.41:1 here, and would survive a face two steps
        lighter again. What stops it is the hot zone: the amber that marks
        0 VU and above is 4.68:1 on this face and 4.51:1 one step lighter, and
        the red washes visibly toward pink as the face comes up. So the meter
        is as light as its own warning colour allows, not as light as its
        numbers allow.

        A token rather than a panel constant since 0.2.2, which is what lets a
        dark theme raise it above the plate -- a meter window that reads as lit
        rather than as a hole. */
    juce::Colour meterFace  { 0xff464649 };

    juce::Colour track      { 0xff4fb8e8 };   ///< dotted gain tracks and their plus/minus
    juce::Colour trackFill  { 0xff7fd0f2 };   ///< highlights derived from the track colour

    // Darkened from #a6a6a6 in 0.2.2: at the old value a disengaged switch
    // put its white label at 2.43:1, which is close enough to the 1.98-2.55:1
    // of an *engaged* one that on and off were told apart by hue alone.
    juce::Colour switchOff  { 0xff6f7076 };

    /** Secondary switches: Hi-Q, Auto, Mono. Module-specific functions, but
        none of them is the module's bypass, so none takes the module's colour.

        Was #4cacdc until 0.2.2, which sat 1.13:1 from `track` -- the same blue
        by any measure that matters, arrived at twice. It now carries track's
        value outright. Kept as its own token rather than folded into `track`
        because the two mean different things and a theme may want to separate
        them again -- equal here by intent, which is what `switchOn` was not:
        a second name for BMO EQ's pink, one channel off it, and deleted in
        0.2.2 once nothing was left using it. */
    juce::Colour switchAlt  { 0xff4fb8e8 };

    /** Polarity inversion, wherever it appears.

        **The rule, for any module added later: a polarity switch is this
        colour.** Not the module's accent, not switchAlt. It flips phase and
        nothing else, it means exactly the same thing on every panel in the
        suite, and it is the one control a person hunts for by sight rather
        than by reading -- so it is the one that most has to look identical
        everywhere. Before 0.2.2 it lit in BMO EQ's pink, the Saturator's
        orange and Util's green.

        White, so an engaged polarity switch is the brightest thing in a row
        of switches and the state reads as an inversion: dark fill with a
        light label off, light fill with a dark one on. It is the only lit
        colour in the suite that carries no hue, which suits the only control
        in the suite that is not an amount of anything. */
    juce::Colour polarity   { 0xffffffff };

    /** BMO Util's gain, and nothing else yet.

        It behaves exactly like INPUT and OUTPUT -- same range, same control --
        and until 0.2.3 it was drawn as one of them, in `track`, in the
        `utility` style. That said it *was* one of them, and it is not: INPUT
        and OUTPUT are the pair every module begins and ends with, trims either
        side of whatever the module does. Util's gain is the thing Util does.
        Same behaviour, different job, so it keeps rules of its own -- which is
        why this is still a token rather than a second use of `track`, even
        though the two now hold the same hex. `track`, `switchAlt` and `meterGr`
        already share it on the same terms: one name per job, so a colour can
        move for one of them without silently moving for the others.

        **The value came back to the azure on Frosty's call, 2026-09-14, from
        rendered candidates rather than from the table of ratios.** 0.2.3 had
        parked it on BMO Opto's old lavender as a placeholder that
        differentiated rather than a colour chosen for this knob, and that
        placeholder was the fault: at hue 271.5 degrees against BMO Dimension's
        271.6, VOLUME read as a Dimension control whenever the two sat in one
        rack.

        Three candidates were rendered, panel and rack, both appearances:

        | | hue | on the dark plate | on the pale one |
        |---|---|---|---|
        | Util's own green `#7fc98a` | 128.9 | 6.84:1 | 1.72:1 |
        | **the utility azure** | **198.8** | **6.02:1** | **1.95:1** |
        | gold `#e8c95a` | 46.9 | 8.33:1 | 1.41:1 |

        Green needed no new colour and cleared the rack, but rendered it took
        VOLUME to the same green as PAN and WIDTH and the panel went monochrome
        -- the headline knob stopped being the headline. Numbers could not show
        that; only the render did. Gold read best of the three in the rack and
        is the one that was recommended, and it was not taken: it puts a section
        legend at 1.41:1 on the pale plate, below the 1.72-2.00 band the raw
        legend rule already spends, and it buys a permanent new hue to get
        there. The azure is second in the rack, clear of every accent by hue,
        and the only candidate that stays inside the band already signed off. */
    juce::Colour utilGain   { 0xff4fb8e8 };

    juce::Colour accent     { 0xfff08cb4 };   ///< the module's own colour; see ModuleDef

    /** What a module uses in place of its accent when it is deliberately
        showing no colour identity -- BMO Opto's Tele mode, which runs the
        whole panel in greyscale so that Stressed reads as the louder of the
        two by colour alone.

        Not luminance-matched to any accent, and it cannot be: the four
        accents survive pale knob caps at 1.2-1.3:1 against the plate because
        hue separates them from it, and a grey has no hue to spend. Matched
        for lightness this would be #b8b8b8 and its cap would land at 1.19:1
        with nothing else to tell it from the faceplate. This is a step darker
        so the cap reads at 1.28:1, inside the range the coloured caps already
        occupy. */
    juce::Colour neutral    { 0xffababab };

    /** A meter fill that is not a warning yet, on a panel that does not use
        the green. BMO Opto and the suite meters run meterLow; the LTV line is
        greyscale and a green bar on it is the same loose end periwinkle was.

        Mid, because it has to read in a well at either end of the range: it
        clears the LTV pale well #b0b0b0 by 24.5 L* and the dark one #27272b by
        31.6, which is the best either end gets from a single value. */
    juce::Colour meterQuiet { 0xff6f7076 };

    juce::Colour meterLow   { 0xff6bbf7a };
    juce::Colour meterHigh  { 0xffe0b040 };
    juce::Colour meterClip  { 0xffe0685a };
    juce::Colour meterGr    { 0xff4fb8e8 };   ///< gain reduction, for the modules that show it

    /** Gain reduction on a panel that carries no suite colour.

        BMO LTV Comp's, which is greyscale. A second GR colour rather than a new
        value for `meterGr`, because BMO DEQ draws with that one in three places
        and this was not a change to how BMO looks.

        It stayed LTV Comp's alone: BMO DEQ's bidirectional bar was offered the
        bronze on 2026-09-15 and took `meterCut` instead, so the LTV line keeps
        a GR colour of its own.

        Bronze -- Frosty, 2026-09-15, from four warm candidates rendered in
        both appearances. **The warm gap is narrower than it looks**: the LTV
        level bars already run a gradient from amber at 42 degrees to red at 6,
        so anything warm *between* those reads as a level rather than as a
        different quantity. Deep gold came back looking like the gradient's
        amber zone and copper like its red. This is muted and dark enough to
        read as neither -- 24.5 L* clear of the silver well, 31.6 of the
        graphite -- which is what a bar measuring a different thing from the
        two bars either side of it needs to do. */
    juce::Colour meterGrWarm { 0xffb98a5e };

    /** Gain a module is **taking away**, on a meter that shows both directions.
        The other half of the pair is `meterBoost`; they exist together or not
        at all, and nothing should use one without meaning the other.

        **Azure's complement.** BMO DEQ's bar reads gain added in the azure, so
        gain taken away is the colour directly opposite it: azure is hue 198.8
        degrees and this is 18.8, at azure's own saturation. The hue is the
        complement's and the lightness is chosen for legibility -- the same
        method `analyserPink` records for the teal, and for the same reason,
        which is that a true complement at the original's lightness is usually
        the faintest thing on the panel.

        Frosty picked it from a rendered ladder of that hue, 2026-09-15. At
        4.51:1 on the dark well and 2.62:1 on the pale one it is the most
        saturated of the four and the strongest of them where the suite is
        weakest, which is the pale plate.

        **Not the amber** `meterHigh`, which was asked for and rendered first.
        That is hue 42, and it is what BMO LTV Comp's level bars turn from -9 dB
        up -- in a rack, a DEQ metering gain reduction and an LTV Comp
        approaching its ceiling would have been the same colour. This sits 23
        degrees clear of it. */
    /** A band's **placement**, as a colour it carries everywhere.

        BMO DEQ, 2026-09-15: Stereo keeps the module accent, and Mid and Side
        each take one of these -- on the band's tab, on its node, and on the
        knobs when it is the band being edited. So which of the three a band is
        reads at a glance whether or not it is selected, which a lit switch in
        the strip below could only say about the selected one.

        Placement is the right thing to spend a colour on because it barely
        moves. The compress/expand mode was tinted this way for one round and
        taken back off: it flips whenever RANGE crosses zero, and a panel that
        recolours itself that often is louder than what it is reporting.

        **The hue space is crowded and these are what is left.** Sixteen hues in
        the suite are already spoken for -- five module accents, the utility
        azure, four meter states, four analyser options and BMO Tune's lime --
        so the choice was made from the gaps, and both were picked from rendered
        candidates rather than from the table (Frosty, 2026-09-15).

        **Mid is the indigo on its pale-plate figure.** Magenta at 320 and green
        at 104 were both rendered against it. Magenta measures 1.62:1 on the
        pale well and green 1.39; the indigo is **1.90**, the only one of the
        three inside the 1.72-2.00 band the suite's raw legends already spend --
        and the pale plate is where this whole scheme is weakest, because Side
        is down at 1.09 there whatever its hue. Green lost for a second reason
        the numbers do not show: at 104 it is green-family with the teal at 172,
        so a Stereo tab and a Mid tab read alike at a glance, which is the thing
        this is for.

        Its risk is off-panel and is accepted: BMO Dimension's lavender is at
        271.6, eighteen degrees away, so a DEQ and a Dimension in one rack are
        neighbours. */
    juce::Colour placeMid  { 0xffa390df };   ///< hue 254
    juce::Colour placeSide { 0xffdcd060 };   ///< hue 54

    /** The dynamics half of a panel: its switches, its knobs and their names.

        BMO DEQ, 2026-09-15. The panel divides into an EQ half and a dynamics
        half, and they were the same colour with only a rule between them.

        **The azure** (Frosty, 2026-09-15), so everything dynamic on the panel
        agrees: these knobs, COMP and EXP, DYN, the dot on a band's tab that
        says it has dynamics, and the whisker on its node that shows their
        range. Before this they were four colours doing one job.

        Six candidates were rendered whole before it landed here -- CEQ's pink,
        a grey of its own, indigo, green, magenta and no section colour at all.
        The pink crossed the accent table; the grey read well but took DYN's
        "way in" quality off it, which was the thing that sent DYN up to sit
        with MID and SIDE in the first place.

        The same hex as `switchAlt`, and a token of its own on the terms this
        file sets throughout: one name per job, so a colour can move for one of
        them without silently moving for the others. Five candidates moved
        through this line and only this line. */
    juce::Colour dynamicsAccent { 0xff4fb8e8 };   ///< the azure, same hex as switchAlt

    juce::Colour meterCut { 0xffe46830 };

    /** Gain a module is **adding** -- upward expansion -- for a meter that
        shows both directions.

        BMO DEQ is the only one: its dynamic bands expand upward as well as
        compress downward, and since 2026-09-15 its bar reads gain taken away
        down from the top and gain added up from the bottom. Two quantities in
        one track, and drawn in one colour the picture said how much while only
        the readout's sign said which way.

        **The azure, and `meterCut` is its opposite** (Frosty, 2026-09-15) --
        chosen in that order, so the cut colour is the one that had to move.
        Gain going up keeps the colour BMO DEQ has always metered dynamics in,
        and gain coming down takes the hue directly across the wheel from it.

        A separate token from `meterGr` even though the two hold the same hex,
        on the terms this file already sets for `track`, `switchAlt` and
        `meterGr`: one name per job, so a colour can move for one of them
        without silently moving for the others. `meterGr` still draws BMO DEQ's
        band-tab dynamics dot and the curve's range whisker, neither of which
        is a direction; this one is half of a pair that means one. */
    juce::Colour meterBoost { 0xff4fb8e8 };

    /** The spectrum analyser's four alternatives, and its default.

        **None of these is an accent**, and that is the whole point of writing
        them down here rather than picking one in a panel. A colour in this set
        means something *inside* one panel: it never touches a knob cap, a
        caption or a header bar, and it claims no hue for the module drawing it.
        The same is true of BMO Opto's red and amber, and the table beside these
        lives in `products/AGENTS.md` for the same reason -- without it, a later
        module reads BMO DEQ as owning 23.6 degrees.

        A preference with five options, Frosty 2026-09-12, measured against the
        well `#1b1b1f`. The fifth is the module's own accent and needs no token,
        which is why there are four here:

        | option | hue | on well | nearest claimed hue |
        |---|---|---|---|
        | Accent (the module's own) | -- | -- | it *is* the accent; no separation from the curve |
        | orange | 23.6 | 6.92:1 | 8 degrees from BMO Saturator |
        | gold | 46.9 | 10.57:1 | 5 degrees from BMO Opto's amber state |
        | pink | 352.0 | 7.44:1 | 16 degrees from BMO EQ |
        | **neutral, the default** | 220.0 | 8.25:1 | claims nothing |

        **Neutral is the default**: it collides with nothing, it never competes
        with the curve in front of it, and a panel that ships in someone else's
        colour has made a claim on their behalf. The other four are there for
        people who want one.

        The pink is the true complement of BMO DEQ's teal -- 352.0 against
        172.0 -- but lifted. The complement at the teal's own saturation and
        lightness is `#cf5e6d`, which measures 4.48:1 and is the faintest thing
        on the panel. The hue is the complement's; the lightness is the suite's
        legibility. */
    juce::Colour analyserOrange  { 0xffef8b4a };
    juce::Colour analyserGold    { 0xffe8c95a };
    juce::Colour analyserPink    { 0xffe6949f };
    juce::Colour analyserNeutral { 0xffaeb4c0 };

    //== Fixed, not themable ===================================================

    /** Every switch in the suite, in px.

        One constant rather than four, because four is what let them drift to
        56x24, 62x26, 70x26 and 70x26 -- three sizes across four modules that
        sit side by side in a rack. BMO Util and BMO Opto were already here;
        BMO EQ's row was the one that looked undersized, 180 px of switches in
        a 260 px well.

        A layout value, so it lives with the corner radius and the stroke
        weights rather than with the colours: a theme cannot reach it and so
        cannot break a panel with it. */
    static constexpr int switchWidth  = 70;
    static constexpr int switchHeight = 26;

    /** Between two switches, whether they sit in a row or a stack. Was 6 in
        BMO EQ and the Saturator against 8 in Util and Opto -- the same drift
        the sizes had, one number smaller. */
    static constexpr int switchGap    = 8;

    /** How wide a utility gain knob -- INPUT, OUTPUT, Util's GAIN -- may draw.

        One number, because those three are the same control wherever they
        appear and were drawing at four different sizes: 56 in BMO EQ, 78 and
        98 in the Saturator, 104 in Util. The row each sits in still differs
        per panel; this caps the knob inside it, so a taller row buys the
        caption room rather than a bigger circle.

        56 is what BMO EQ can afford and therefore what the suite can afford.
        Its column carries three bands, a filter, a switch row and two gain
        knobs inside the common 688, with four pixels to spare -- so it is the
        panel that sets this number, and the others come down to meet it. */
    static constexpr int gainKnobSide = 56;

    /** Point size for INPUT's and OUTPUT's names, against 15 for a knob whose
        setting you read off the panel.

        These two are named so you can find them, not so you can watch them.
        11 puts them a step under the 13 pt section legends, which is the
        order they should be read in. */
    static constexpr float gainCaptionSize = 11.0f;

    static constexpr float corner       = 3.0f;
    static constexpr float hairlineWeight = 1.0f;
    static constexpr float knobStroke   = 2.2f;
    static constexpr float trackGap     = 10.0f;   ///< face edge to the dotted track

    /** Ring edge to the dotted track, for a gain that sits inside a selector.

        Tighter than `trackGap` because the space is not the same space. A
        utility knob has ten clear pixels between its face and its track; a
        band's gain has a ring drawn around it and its legend clamped to the
        cell height above that. */
    static constexpr float concentricTrackGap = 4.5f;
    static constexpr float legendGap    = 12.0f;   ///< track to the legend
    /** Knob edge to the legend on a filter. Half what it was: at 20 the
        numbers read as a separate ring floating around the dial rather than as
        its own markings. Frosty's call, on a render. Only BMO EQ's low cut is
        a filter, so this reaches nothing else. */
    static constexpr float filterLegendGap = 10.0f;
};

//== Derived colours ==========================================================
//
// A module states one colour, its accent, and everything else it needs is
// computed from that colour and the plate underneath it. BMO Opto is why:
// its lavender is unreadable as ink and unreadable under white text, so the
// panel hardcoded #9c71c3 for its captions -- against the rule in
// core/AGENTS.md that tokens are the only place colours live. It broke the
// rule because the token it needed did not exist. Module six would have
// hand-rolled its own hex for the same reason.
//
// Deriving against the *current* plate rather than a fixed one is also what
// makes a dark theme nearly free: on #efefef the accents have to be darkened
// hard to be legible, and on a dark plate all four already clear 7:1, so the
// same call returns the accent untouched.

/** WCAG 2.x contrast ratio, 1.0 to 21.0. Order does not matter. */
float contrastRatio (juce::Colour, juce::Colour) noexcept;

/** The accent, moved away from `ground` until it clears `minRatio` against
    it -- darkened on a pale plate, lightened on a dark one. Hue is preserved,
    so the result still reads as the module's own colour.

    This is what a caption, a section legend and a selected legend are set in.
    4.5:1 is the floor for text this size. */
juce::Colour accentTextOn (juce::Colour accent, juce::Colour ground,
                           float minRatio = 4.5f) noexcept;

/** Ink for text drawn *on* a filled accent -- an engaged switch. A darkened
    step of the fill's own hue rather than flat black, so the switch stays
    monochromatic. White was 1.98-2.55:1 on the four accents. */
juce::Colour onAccentOf (juce::Colour fill, float minRatio = 4.5f) noexcept;

/** A module's own colour used as ink on the plate: a knob's caption, a section
    legend, the selected position on a band. Paired with `faceOf`, and the two
    trade places between the appearances.

    On the pale plate the cap is a wash of the accent and the ink is the accent
    stepped down until it reads -- the wash would vanish as text there.

    On the dark plate they swap. The cap carries the module's colour at full
    strength, which is what a knob wants on a dark panel, and the ink is the
    wash, which is what text wants: 9.07-9.73:1 against the plate, against the
    5.87-6.84:1 the raw accent managed. Both directions come out better than
    they went in, which is why this is a swap and not a compromise. */
juce::Colour accentInk (juce::Colour accent) noexcept;

/** The same, derived against a stated ground rather than against the suite&apos;s
    plate. For a panel on a ui::Line that carries a plate of its own: a legend
    stepped for #efefef and then printed on silver is legible for a plate the
    reader is not looking at. */
juce::Colour accentInk (juce::Colour accent, juce::Colour ground) noexcept;

/** The current tokens: the built-in set for whichever appearance is chosen,
    with whatever the user's theme file overrides on top. */
const Tokens& tokens() noexcept;

//== Appearance ===============================================================
//
// Light or dark, stored once per machine rather than per plugin instance and
// per project. It is not a parameter: specs() is frozen and append-only, a
// parameter would be automatable and saved into every session, and a look is
// not something a session should carry. See modules/eq/params.h.
//
// Every open editor already polls for theme changes once a second, so the
// choice reaches every instance -- standalone and in a rack, this plugin and
// the one in the next track -- without any of them knowing about each other.

/** The built-in dark palette. The light one is `Tokens {}`. */
Tokens darkTokens() noexcept;

/** Where the appearance is remembered: one small JSON file beside the themes. */
juce::File uiPreferenceFile();

bool isDarkMode() noexcept;

/** Writes the preference and applies it here immediately; other open editors
    pick it up on their next poll. */
void setDarkMode (bool);

/** Chooses an appearance for **this process only**: the machine-wide
    preference is neither written nor read again, and the theme poll stops
    touching the appearance.

    `setDarkMode` is the one a user's click goes through, and it persists. This
    one deliberately does not, because a tool that renders the dark palette
    should not flip the appearance of every plugin open on the machine, and
    should not leave a preference changed behind it if it falls over.

    It exists because there was no other way to see the dark set without
    changing that preference — which is a large part of why half the faults on
    the ui-editor branch existed in one appearance only, against a house rule
    that says to check both every time. See `tools/snapshot`'s
    `appearance=dark|light`. */
void overrideAppearance (bool shouldBeDark);

/** A character knob's cap -- and see `accentInk`, which is the other half of
    this and trades places with it between the appearances.

    On the pale plate the cap is the accent washed half way to `knobTint`: the
    accent at full strength would be a very loud knob on a near-white panel.
    On the dark plate it is the accent itself, because there it is not loud, it
    is the module's colour reading properly for the first time -- 5.87-6.84:1
    against the plate with the pointer at 6.13-7.14:1 on top of it.

    Mixing the accent toward the *dark plate* was tried first and rejected on
    sight: pink went to a clean maroon and green to a deep green, but the
    Saturator's orange landed on a brown. Lifting the accent's saturation was
    tried second and measured fine, and the caps shouted. Neither number said
    anything was wrong, which is the argument for rendering over computing. */
inline juce::Colour faceOf (juce::Colour accent) noexcept
{
    return isDarkMode() ? accent
                        : accent.interpolatedWith (tokens().knobTint, 0.5f);
}

//== Theming (option A from the plan: a flat JSON file of token -> hex) ========
//
//  ~/Library/Audio/Presets/LT3 Audio/Themes/Default.json
//
//  { "plate": "#efefef", "accent": "#f08cb4", ... }
//
// Any key that is not a token name is ignored, and any token the file leaves
// out keeps the built-in value. The file is polled by editors on a slow timer,
// so editing it while a plugin is open recolours the panel.

/** Where theme files live. */
juce::File themeDirectory();

/** The file an editor watches. */
juce::File themeFile();

/** Points `themeFile()` somewhere else for the rest of the process.

    For tools that render a *candidate* palette. Without it the only way to see
    one is to write the machine-wide `Themes/Default.json`, which is a file the
    user owns and which every open plugin is watching on a 1 Hz poll -- so
    rendering a colour you were only considering would recolour the session
    going on in the next window. Pass an empty File to go back to the default.

    Tools only. Nothing in a plugin should call this. */
void overrideThemeFile (const juce::File& file);

/** Re-reads the theme if the file has changed since the last look. True when
    the tokens changed, in which case the caller repaints. */
bool pollTheme();

/** Every token name the theme file may set, for writing a template. */
juce::StringArray tokenNames();

/** Whether the theme file currently in force sets this token by name.

    The difference between "the plate is #efefef because that is the built-in"
    and "the plate is #efefef because a theme says so", which nothing could ask
    before: `tokens()` hands back a colour and not its provenance.

    It exists for `ui::Line`. A line carries its own ground -- the LTV plugins
    are silver rather than the suite's pale grey -- and that has to give way
    the moment someone applies a theme, because a theme is a statement about
    the whole window and a line is only a statement about one product in it.
    Without this, a theme that set `plate` would recolour seven panels and
    leave the eighth silver, which is the one outcome nobody wants. */
bool themeSets (juce::StringRef tokenName);

/** Applies a parsed theme object over `base`, which defaults to the light
    built-in set. A theme is an overlay, not a whole palette, so choosing dark
    and then hand-editing two colours works the way it reads. Exposed for
    tests. */
Tokens tokensFromJson (const juce::var& object, Tokens base = Tokens {});

} // namespace bmo::ui
