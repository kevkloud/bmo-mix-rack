#pragma once

#include "core/ui/Tokens.h"
#include <optional>

namespace bmo::ui
{

/** A product line: a family of plugins that share a look and a marque.

    The suite was one line for its whole life, so the look lived in `Tokens`
    and the marque was a string literal in `ProductHeader`. It is two lines
    now -- **BMO**, and the **LTV** collaborations -- and more of the second
    kind are planned, so the thing that differs between them has a name.

    ## What a line may change, and what it may not

    A line owns **the ground it is drawn on**: the faceplate, the strip behind
    the header, and the wells that sit in it. It owns its **marque**, the badge
    at the head of every panel.

    A line does **not** own ink. No text colour, no knob face, no accent, no
    meter colour is a line's to set. That is the whole of the legibility rule
    here and it is structural rather than a promise: a line can move the
    ground, and every ink in the suite is then measured against the grounds
    that actually ship. Let a line set its own ink too and the number of
    (ink, ground) pairs to check stops being a list and starts being a
    product of two lists.

    A line owns no layout either. `ui_layout_tests` asserts absolute rows
    across every panel, and a rack puts lines side by side on those rows, so a
    line that moved a control would break the one thing a rack guarantees.

    ## Why the ground comes in pairs

    Because the suite has two appearances and a line still has to answer for
    both. A silver faceplate is a pale-appearance object by nature, and the
    question of what it becomes in the dark appearance is a real one with two
    defensible answers -- stay silver, so a rack reads like a real rack with a
    silver unit among black ones, or darken with everything else. The pair
    lets a line say which; giving both entries the same colour is how a line
    says "stay".

    ## Why a theme beats a line

    `groundFor` yields to any theme that names the token. A theme is a
    statement about the whole window; a line is a statement about one product
    in it. A theme that set `plate` and recoloured seven panels while leaving
    the eighth silver would be obeying both rules and serving neither, so the
    broader statement wins. `ui::themeSets` is what makes the distinction
    available at all -- `tokens()` returns a colour, never its provenance.
*/
struct Line
{
    /** The badge at the top right of every panel in the line. Three or four
        characters; it is set beside the product name at 10 pt and there is
        room for no more. */
    const char* marque;

    /** One appearance's ground. `plate` is the faceplate, `plateEdge` the
        strip behind the header and preset bar, `well` every recess cut into
        the plate -- meter troughs, the rack's gutter, a pressed button.

        The three move together and that is the point. A plate set alone is
        the fault this struct exists to prevent: the suite's `well` is
        `#d6d6d6`, so a silver plate at `#d5d5d5` measures **1.01:1** against
        it and every meter trough on the panel disappears. Measured on AURORA,
        2026-09-14, from a render -- the wells stopped appearing in a
        histogram of the row at all. */
    struct Ground
    {
        juce::Colour plate, plateEdge, well;

        /** The knob caps, if the line fixes them rather than deriving them
            from each module's accent. Every knob on the line takes it --
            character and utility alike -- so a panel's controls read as one
            instrument whatever colour their lettering carries.

            This is the one thing on a panel that is not ground and is a
            line's anyway, so the boundary above is really *surfaces, not
            ink*: a plate, a recess and a knob cap are all physical faces of
            a panel, and text, accents and meter colours are information
            printed on them. A line owns the first kind.

            LTV fixes it because that is the look: black knobs on a silver
            panel, which is what the hardware does. `faceOf` is bypassed when
            this is set, so the accent stops reaching the cap -- and the
            module's colour then lives on the caption alone, where `accentInk`
            already puts it on the pale plate.

            **The pointer follows the cap, not the appearance.** `pointer` is
            white on the pale plate and near-black `#2b2b2e` on the dark one,
            because a dark-mode cap is normally the accent at full strength
            and therefore light. A fixed black cap inverts that, so a line
            with a cap derives its pointer from the cap itself through
            `onAccentOf` -- which is what Tokens.h already says should happen
            in principle: "a knob's pointer answers to the cap rather than to
            either [plate]". White on a near-black cap measures 16.6-21:1. */
        std::optional<juce::Colour> knobCap {};

        /** What this line's panels use in place of a module's accent, if it
            has one: knob captions, the header's tint bar, section legends.

            **This is the line owning ink, which the rule above says it does
            not, and the exception is argued rather than assumed.** Frosty
            asked for the header and the captions to match the knob, which
            sounds like one colour and cannot be: the cap is darker than its
            plate in *both* appearances, so an ink that matches it inverts
            between them. Measured against the two LTV grounds --

                #3a3a3e  6.77:1 on silver,  1.00:1 on graphite
                #141414 11.01:1 on silver,  1.63:1 on graphite
                #6f6f76  2.98:1 on silver,  2.27:1 on graphite

            -- the light cap is *literally the dark plate*, and the best single
            compromise is mediocre on both. So the ink is per appearance like
            everything else here.

            What that costs is the reason the rule existed: the (ink, ground)
            pairs to check stop being a list and become a product. With two
            lines it is four grounds and still countable by hand, which is why
            it is affordable now and why the contrast assertions in
            docs/ui-workflow-brief.md §4 stop being the cheapest open item and
            become the thing that keeps this honest. Do not add a third line
            without them. */
        std::optional<juce::Colour> ink {};
    };

    /** Empty for a line that takes the suite's ground, which is BMO. */
    std::optional<Ground> light {}, dark {};

    bool ownsGround() const noexcept { return light.has_value() && dark.has_value(); }
};

/** The original line, and the suite's own: takes the stock ground in both
    appearances, so `groundFor` leaves every token alone. */
const Line& bmoLine();

/** The collaborations. Silver in the pale appearance; see the definition for
    what it does in the dark one and why. */
const Line& ltvLine();

/** The palette a panel of this line should paint with: the tokens in force,
    with the line's ground substituted for any of the three the current theme
    does not itself set.

    Call it in `paint`, not in a constructor: the appearance and the theme
    both change under a running editor, and the 1 Hz poll repaints rather
    than rebuilding anything. */
Tokens groundFor (const Line& line);

/** The knob cap this line fixes for the appearance in force, or nothing if it
    leaves caps to `faceOf` and the module's accent. */
std::optional<juce::Colour> capFor (const Line& line);

/** The ink this line uses in place of a module's accent, or nothing if it
    leaves the accent alone. */
std::optional<juce::Colour> inkFor (const Line& line);

} // namespace bmo::ui
