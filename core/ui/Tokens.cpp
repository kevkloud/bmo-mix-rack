#include "Tokens.h"
#include "core/state/PresetManager.h"
#include <array>
#include <cmath>

namespace bmo::ui
{

namespace
{
    struct Entry { const char* name; juce::Colour Tokens::* member; };

    const Entry kEntries[]
    {
        { "plate",      &Tokens::plate },
        { "plateEdge",  &Tokens::plateEdge },
        { "well",       &Tokens::well },
        { "hairline",   &Tokens::hairline },
        { "outline",    &Tokens::outline },
        { "text1",      &Tokens::text1 },
        { "text2",      &Tokens::text2 },
        { "knobFace",   &Tokens::knobFace },
        { "knobEdge",   &Tokens::knobEdge },
        { "knobTint",   &Tokens::knobTint },
        { "pointer",    &Tokens::pointer },
        { "ringFace",   &Tokens::ringFace },
        { "meterInk",   &Tokens::meterInk },
        { "meterFace",  &Tokens::meterFace },
        { "track",      &Tokens::track },
        { "trackFill",  &Tokens::trackFill },
        { "switchOff",  &Tokens::switchOff },
        { "switchAlt",  &Tokens::switchAlt },
        { "polarity",   &Tokens::polarity },
        { "accent",     &Tokens::accent },
        { "neutral",    &Tokens::neutral },
        { "utilGain",   &Tokens::utilGain },
        { "meterQuiet", &Tokens::meterQuiet },
        { "meterLow",   &Tokens::meterLow },
        { "meterHigh",  &Tokens::meterHigh },
        { "meterClip",  &Tokens::meterClip },
        { "meterGr",    &Tokens::meterGr },
        { "meterGrWarm", &Tokens::meterGrWarm },
        { "dynamicsAccent", &Tokens::dynamicsAccent },
        { "placeMid",   &Tokens::placeMid },
        { "placeSide",  &Tokens::placeSide },
        { "meterCut",   &Tokens::meterCut },
        { "meterBoost", &Tokens::meterBoost },
        { "analyserOrange",  &Tokens::analyserOrange },
        { "analyserGold",    &Tokens::analyserGold },
        { "analyserPink",    &Tokens::analyserPink },
        { "analyserNeutral", &Tokens::analyserNeutral },
    };

    Tokens current;
    juce::Time lastModified, lastPreferenceModified;
    bool loadedOnce = false;
    bool dark = false;
    bool preferenceRead = false;

    // Set by overrideAppearance: the poll then leaves the appearance alone,
    // so a tool can render one palette without the machine-wide preference
    // being touched or read.
    bool appearanceOverridden = false;

    // Set by overrideThemeFile, and the same idea one level along: a tool can
    // render a candidate palette without writing over the machine-wide theme,
    // which is a file the user owns and which every open plugin is watching.
    juce::File themeOverride;

    // Which token names the theme in force actually sets, as opposed to which
    // ones simply have a value. See ui::themeSets -- ui::Line needs to know
    // the difference and nothing else in the palette records it.
    //
    // Kept beside `current` and rewritten by the same three places that
    // rewrite it, rather than inside tokensFromJson: that function is a pure
    // overlay documented as exposed for tests, and a global side effect in it
    // would be a surprise to every caller.
    juce::StringArray themedKeys;

    void recordThemedKeys (const juce::var& object)
    {
        themedKeys.clear();

        if (auto* obj = object.getDynamicObject())
            for (const auto& e : kEntries)
                if (obj->hasProperty (e.name))
                    themedKeys.add (e.name);
    }

    bool parseColour (const juce::var& v, juce::Colour& out)
    {
        if (! v.isString())
            return false;

        auto s = v.toString().trim();

        if (s.startsWithChar ('#'))
            s = s.substring (1);

        if (s.length() == 6)
            s = "ff" + s;

        if (s.length() != 8)
            return false;

        out = juce::Colour ((juce::uint32) s.getHexValue64());
        return true;
    }
}

const Tokens& tokens() noexcept { return current; }

//== Derived colours ==========================================================

namespace
{
    float toLinear (float c) noexcept
    {
        return c <= 0.03928f ? c / 12.92f : std::pow ((c + 0.055f) / 1.055f, 2.4f);
    }

    float relativeLuminance (juce::Colour c) noexcept
    {
        return 0.2126f * toLinear (c.getFloatRed())
             + 0.7152f * toLinear (c.getFloatGreen())
             + 0.0722f * toLinear (c.getFloatBlue());
    }

    /** Both derivations walk the accent toward one end of the range in fixed
        steps and stop at the first value that clears. Called from paint, and
        each step costs three pow(), so the answers are memoised: there are
        only ever a handful of live (colour, ground) pairs -- one per module
        per ground -- and a linear scan of eight is cheaper than one step of
        the search it replaces. Painting is all on the message thread, so no
        locking is needed here. */
    struct Memo
    {
        juce::uint32 from = 0, ground = 0;
        float ratio = 0.0f;
        juce::Colour result;
        bool valid = false;
    };

    juce::Colour searchCached (juce::Colour from, juce::Colour ground, float minRatio,
                               juce::Colour target)
    {
        static std::array<Memo, 8> memo {};
        static size_t next = 0;

        const auto fromKey = from.getARGB();
        const auto groundKey = ground.getARGB();

        for (const auto& m : memo)
            if (m.valid && m.from == fromKey && m.ground == groundKey && m.ratio == minRatio)
                return m.result;

        auto result = from;

        // 50 steps of 2% is enough to reach either end of the range; stopping
        // at the first pass keeps as much of the original colour as the ratio
        // allows, so the hue stays recognisable.
        for (int i = 0; i <= 50; ++i)
        {
            result = from.interpolatedWith (target, (float) i * 0.02f);

            if (contrastRatio (result, ground) >= minRatio)
                break;
        }

        memo[next] = { fromKey, groundKey, minRatio, result, true };
        next = (next + 1) % memo.size();

        return result;
    }
}

float contrastRatio (juce::Colour a, juce::Colour b) noexcept
{
    const auto la = relativeLuminance (a);
    const auto lb = relativeLuminance (b);

    return (juce::jmax (la, lb) + 0.05f) / (juce::jmin (la, lb) + 0.05f);
}

juce::Colour accentTextOn (juce::Colour accent, juce::Colour ground, float minRatio) noexcept
{
    // Away from the ground: darker on a pale plate, lighter on a dark one.
    const auto target = relativeLuminance (ground) > 0.18f ? juce::Colours::black
                                                           : juce::Colours::white;

    return searchCached (accent, ground, minRatio, target);
}

juce::Colour onAccentOf (juce::Colour fill, float minRatio) noexcept
{
    // The ink sits on the fill, so the fill is its own ground. Every accent
    // in the suite is light enough that darkening is the direction that
    // works; a future dark accent gets white out of the same test.
    const auto target = relativeLuminance (fill) > 0.18f ? juce::Colours::black
                                                         : juce::Colours::white;

    // The search stops at the first value that clears, which keeps as much of
    // the fill's own hue in its label as the ratio allows. On a fill that has
    // no hue there is nothing to keep, and stopping early only costs contrast:
    // white polarity switches came out with a #757575 label at exactly 4.5:1
    // where black was free and reads at 21:1. So an achromatic fill goes
    // straight to the end of the range.
    if (fill.getSaturation() < 0.12f)
        return target;

    return searchCached (fill, fill, minRatio, target);
}

//== Appearance ===============================================================

Tokens darkTokens() noexcept
{
    Tokens t;

    // Structural greys spaced by CIE L* rather than by contrast ratio. Below
    // about L* 20 the ratio is useless: from this plate the most contrast
    // available by going darker -- all the way to black -- is 1.29:1, because
    // the +0.05 term in the formula dominates. Spaced by lightness these carry
    // the light set's own intervals, within a few tenths: plate to well 9.2
    // against 8.8, plate to plateEdge 4.1 against 3.8, to hairline 20.9
    // against 21.1, to outline 29.1 against 29.3.
    //
    // Three of them invert direction -- lighter than the plate rather than
    // darker -- which is what a dark surface has to do to read as raised.
    t.well      = juce::Colour (0xff1b1b1f);
    t.plate     = juce::Colour (0xff2e2e32);
    t.plateEdge = juce::Colour (0xff37373b);
    t.hairline  = juce::Colour (0xff5e5e62);
    t.outline   = juce::Colour (0xff727276);

    t.text1     = juce::Colour (0xffe6e6ea);   // 10.86:1 on the plate
    t.text2     = juce::Colour (0xff9a9aa4);   // 4.85:1

    // A cap here carries its colour at full strength and the text under it
    // takes the wash -- the reverse of the pale plate. See faceOf and
    // accentInk, which are the two halves of that. knobFace is the utility
    // knobs' own colour and swaps with them: the raw track blue, where on the
    // pale plate it is #97ddff, a wash of the same.
    //
    // The pointer goes near-black with it, 6.13-7.14:1 on the caps above.
    t.knobFace  = juce::Colour (0xff4fb8e8);
    t.knobEdge  = juce::Colour (0xff8d8d98);
    t.pointer   = juce::Colour (0xff2b2b2e);

    // A band's selector ring. Just above the plate here, white on the pale one.
    //
    // A band dial is five concentric rings, and the pale plate builds them by
    // alternating: knobEdge, ringFace, knobEdge, the plate showing through the
    // gap between the ring and the gain cap, knobEdge again. Three hairlines
    // at #a6a6a6 and two light grounds at #ffffff and #efefef -- the second of
    // those is the plate, and it only reads as part of the dial because the
    // pale plate happens to sit 16 levels off ringFace. Nobody chose that; it
    // was true for free.
    //
    // 0.2.2 put this at middle grey #8e8e93, on the reasoning that white was
    // the brightest thing in a dark window and 4.15:1 against the plate let
    // the ring carry its own edge. Carrying its own edge is exactly what went
    // wrong: at that value it measured 1.01:1 against knobEdge -- #8e8e93 and
    // #8d8d98 are the same colour -- so the outer hairline, the ring and the
    // inner hairline fused into one flat 21 px slab, and the gap behind it
    // stayed plate-dark. Five rings became a slab, a dark gap and a hairline.
    //
    // So the pattern inverts instead of surviving. Dark grounds and light
    // hairlines: ringFace goes to just above the plate, the gap is the plate
    // itself, and knobEdge's #8d8d98 draws all three circles at 3.30:1 on
    // them. 1.27:1 ring against plate here, against 1.15:1 white-on-pale --
    // the same interval, which is what makes it the same dial.
    //
    // It also puts the band marker back on the same side as everything else.
    // accentTextOn steps away from the ground it is given, and from a middle
    // grey the most contrast available going lighter is 3.26:1 -- under the
    // 4.5 it needs -- so it had no choice but to go darker, and the one mark
    // saying which frequency is selected came out near-black while every other
    // thing on the panel got brighter. From this ground it steps up.
    t.ringFace  = juce::Colour (0xff3e3e42);

    // meterFace is deliberately left at its light value, which is *above*
    // this plate rather than below it: the meter window reads as lit instead
    // of as a hole punched in the panel.

    // The accents need no dark variant at all -- all four clear 7:1 on this
    // plate raw, where on the pale one they measure 1.72-2.00:1 and have to be
    // darkened hard. accentTextOn hands them back untouched here.

    return t;
}

juce::Colour accentInk (juce::Colour accent, juce::Colour ground) noexcept
{
    // The other half of faceOf. On the dark plate the cap takes the accent
    // whole and the ink takes the wash; on the pale one it is the other way
    // round. The wash is written out here rather than calling faceOf, which
    // would hand back the accent in this appearance and defeat the swap.
    return isDarkMode() ? accent.interpolatedWith (current.knobTint, 0.5f)
                        : accentTextOn (accent, ground);
}

juce::Colour accentInk (juce::Colour accent) noexcept
{
    // The suite's plate, for every caller that is not on a line with a ground
    // of its own. Kept as the default rather than made the only form because
    // a derivation against the wrong ground is silent: it returns a perfectly
    // legible colour for a plate the reader is not looking at.
    return accentInk (accent, current.plate);
}

juce::File themeDirectory() { return suitePresetRoot().getChildFile ("Themes"); }

juce::File themeFile()
{
    // A tool may point this somewhere else for the length of its own run; see
    // overrideThemeFile. Everything downstream -- overrideAppearance, the poll,
    // setDarkMode -- goes through here, so one override covers all of them.
    return themeOverride != juce::File {} ? themeOverride
                                          : themeDirectory().getChildFile ("Default.json");
}

void overrideThemeFile (const juce::File& file)
{
    themeOverride = file;
}

juce::File uiPreferenceFile() { return suitePresetRoot().getChildFile ("UI.json"); }

bool isDarkMode() noexcept { return dark; }

void overrideAppearance (bool shouldBeDark)
{
    appearanceOverridden = true;
    dark = shouldBeDark;
    preferenceRead = true;
    current = shouldBeDark ? darkTokens() : Tokens {};

    if (themeFile().existsAsFile())
    {
        loadedOnce = true;
        lastModified = themeFile().getLastModificationTime();
        const auto parsedTheme = juce::JSON::parse (themeFile().loadFileAsString());
        recordThemedKeys (parsedTheme);
        current = tokensFromJson (parsedTheme, current);
    }
}

void setDarkMode (bool shouldBeDark)
{
    dark = shouldBeDark;

    const auto file = uiPreferenceFile();
    file.getParentDirectory().createDirectory();

    auto* object = new juce::DynamicObject();
    object->setProperty ("appearance", shouldBeDark ? "dark" : "light");
    file.replaceWithText (juce::JSON::toString (juce::var (object)));

    // Apply here rather than waiting for a poll, so the panel the click landed
    // on repaints at once; everyone else follows within a second.
    //
    // Deliberately left looking unread, so this editor's next poll still
    // reports a change and refreshes the look-and-feel with it. The tokens
    // below only cover what the panels paint themselves -- popup menus, the
    // preset strip's buttons and the alert windows are JUCE colour IDs set in
    // BmoLookAndFeel::refreshColours, and nothing here can reach those.
    preferenceRead = false;
    loadedOnce = false;
    lastModified = {};
    current = shouldBeDark ? darkTokens() : Tokens {};

    if (themeFile().existsAsFile())
    {
        loadedOnce = true;
        lastModified = themeFile().getLastModificationTime();
        const auto parsedTheme = juce::JSON::parse (themeFile().loadFileAsString());
        recordThemedKeys (parsedTheme);
        current = tokensFromJson (parsedTheme, current);
    }
}

juce::StringArray tokenNames()
{
    juce::StringArray names;
    for (const auto& e : kEntries)
        names.add (e.name);
    return names;
}

Tokens tokensFromJson (const juce::var& object, Tokens base)
{
    if (auto* obj = object.getDynamicObject())
        for (const auto& e : kEntries)
            if (obj->hasProperty (e.name))
                parseColour (obj->getProperty (e.name), base.*(e.member));

    return base;
}

bool pollTheme()
{
    // Two files, and either changing rebuilds the palette: the appearance
    // preference chooses the base set, the theme file is an overlay on top of
    // it. Both are polled rather than watched, which is what lets a change
    // made in one plugin instance reach every other one within a second
    // without any of them holding a reference to the others.
    bool changed = false;

    if (! appearanceOverridden)
    {
        const auto file = uiPreferenceFile();
        const auto exists = file.existsAsFile();
        const auto modified = exists ? file.getLastModificationTime() : juce::Time {};

        if (! preferenceRead || modified != lastPreferenceModified)
        {
            const auto wanted = exists
                && juce::JSON::parse (file.loadFileAsString())
                       .getProperty ("appearance", "light").toString()
                       .equalsIgnoreCase ("dark");

            changed = (! preferenceRead) || wanted != dark;
            dark = wanted;
            preferenceRead = true;
            lastPreferenceModified = modified;
        }
    }

    const auto file = themeFile();

    if (! file.existsAsFile())
    {
        if (loadedOnce)
        {
            // The theme file went away: back to the chosen built-in set, and
            // to no themed keys -- which is what hands a line's own ground
            // back to it when a theme is deleted.
            loadedOnce = false;
            lastModified = {};
            themedKeys.clear();
            changed = true;
        }

        if (changed)
            current = dark ? darkTokens() : Tokens {};

        return changed;
    }

    const auto modified = file.getLastModificationTime();

    if (! changed && loadedOnce && modified == lastModified)
        return false;

    lastModified = modified;
    loadedOnce = true;

    const auto parsed = juce::JSON::parse (file.loadFileAsString());
    recordThemedKeys (parsed);
    current = tokensFromJson (parsed, dark ? darkTokens() : Tokens {});
    return true;
}

bool themeSets (juce::StringRef tokenName)
{
    return themedKeys.contains (tokenName);
}

} // namespace bmo::ui
