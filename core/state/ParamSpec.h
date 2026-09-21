#pragma once

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

namespace bmo
{

/** What a parameter is, stated without JUCE so the DSP-only builds and the
    rack's slot remapping can both read it.

    A module's parameter list is a permanent, append-only schema. Once a user
    saves a session, automation lanes and stored state are keyed by the ID and
    by the position in the list, and stored automation is normalised -- so
    widening a range silently rescales every automation point ever written.
    Nothing errors, nothing warns. The golden schema test in tests/plugin is
    where any change to one of these has to be argued for.
*/
enum class ParamKind { Float, Bool, Choice };

/** How a value reads on the panel and in the host. */
enum class ParamFormat
{
    Plain,       ///< the number
    Decibels,    ///< "+3.0 dB"
    Percent,     ///< "40 %"
    Pan,         ///< "L 50", "C", "R 50"
    Hertz,       ///< "850 Hz", "2.10 kHz"
    Milliseconds,///< "5.0 ms", "120 ms"
    Ratio,       ///< "3.0:1"

    //== Added on top of the shared textParam/textFn hunk, for BMO Linger ======
    //
    // Everything else in this file is byte-identical with `c142f37` and
    // `86095a5` -- the BMO FET and BMO Defang branches -- so that whichever of
    // the three lands first the others reproduce the hunk rather than stack on
    // it. **These two rows are BMO Linger's own addition and are in neither of
    // those branches**, so the byte-identical claim covers the file as it
    // stood before this block, not as it stands now.
    //
    // Appended rather than inserted, for the habit a permanent parameter list
    // teaches: nothing serialises a ParamFormat today, and nothing should have
    // to care whether that stays true.
    //
    // Neither is expressible as a `textParam`. A textParam prints itself but
    // is Plain by construction, so it reports no unit to the host and takes no
    // typed-entry parser -- right for BMO FET's "4 (126 us)", wrong for a
    // plain quantity in seconds or metres, where a host's automation lane
    // should say "s" and "m" the way it already says "dB" and "Hz".

    Seconds,     ///< "1.80 s", "12.0 s" -- BMO Linger's DECAY, 0.1..20 s
    Metres       ///< "12.0 m" -- BMO Linger's SIZE, a room dimension, 0.5..80 m
};

struct ParamSpec
{
    const char*  id   = "";
    const char*  name = "";
    ParamKind    kind = ParamKind::Float;
    float        min = 0.0f, max = 1.0f, def = 0.0f, step = 0.0f;
    ParamFormat  format = ParamFormat::Plain;
    std::vector<const char*> choices;

    /** Knob travel proportional to log(value) rather than to value: equal
        turns for equal ratios, which is how frequency, Q and time are heard.
        `min` must be above zero. The mapping lives here and nowhere else --
        the JUCE range for a standalone parameter is built from these functions
        (rangeFor, core/state/Parameters.h), so the two cannot disagree. */
    bool         logarithmic = false;

    /** The value string, for a parameter whose number does not say what it
        means on its own. Null for every parameter that has ever shipped, so
        `format` still decides for all of them.

        BMO FET's ATTACK and RELEASE are why. Their parameter *is* the knob
        position, 1..7, so the format is Plain and a host leaning on the raw
        number would print a bare "4" -- which says nothing about the time it
        selects. The displayed value has to carry both, "4 (126 us)", and that
        string is arithmetic on the position rather than a unit appended to it.

        A plain function pointer, not a std::function: a ParamSpec is copied
        into the lambdas that build a JUCE parameter (core/state/Parameters.h)
        and into the rack's slot parameters, and it is captured by value in
        static spec lists, so it stays cheap to copy and free of allocation.

        It is read by `text()` below, which is the one place a value becomes a
        string -- so the panel, the host's automation lane and a rack slot all
        print the same words. */
    std::string (*textFn) (float) = nullptr;

    static ParamSpec floatParam (const char* id, const char* name, float min, float max,
                                 float step, float def, ParamFormat format = ParamFormat::Plain)
    {
        ParamSpec s;
        s.id = id; s.name = name; s.kind = ParamKind::Float;
        s.min = min; s.max = max; s.step = step; s.def = def; s.format = format;
        return s;
    }

    static ParamSpec logParam (const char* id, const char* name, float min, float max,
                               float step, float def, ParamFormat format = ParamFormat::Plain)
    {
        auto s = floatParam (id, name, min, max, step, def, format);
        s.logarithmic = min > 0.0f;
        return s;
    }

    /** A continuous parameter that prints itself. See `textFn`. The format is
        Plain by construction: a parameter that needs its own string has no
        unit the table of formats already knows. */
    static ParamSpec textParam (const char* id, const char* name, float min, float max,
                                float step, float def, std::string (*text) (float))
    {
        auto s = floatParam (id, name, min, max, step, def, ParamFormat::Plain);
        s.textFn = text;
        return s;
    }

    static ParamSpec boolParam (const char* id, const char* name, bool def)
    {
        ParamSpec s;
        s.id = id; s.name = name; s.kind = ParamKind::Bool;
        s.min = 0.0f; s.max = 1.0f; s.step = 1.0f; s.def = def ? 1.0f : 0.0f;
        return s;
    }

    static ParamSpec choiceParam (const char* id, const char* name,
                                  std::vector<const char*> choices, int def)
    {
        ParamSpec s;
        s.id = id; s.name = name; s.kind = ParamKind::Choice;
        s.choices = std::move (choices);
        s.min = 0.0f; s.max = (float) (s.choices.size() - 1); s.step = 1.0f; s.def = (float) def;
        return s;
    }

    int numChoices() const noexcept { return (int) choices.size(); }

    /** The unit the host shows beside the value. */
    const char* label() const noexcept
    {
        switch (format)
        {
            case ParamFormat::Decibels:     return "dB";
            case ParamFormat::Percent:      return "%";
            case ParamFormat::Hertz:        return "Hz";
            case ParamFormat::Milliseconds: return "ms";
            case ParamFormat::Seconds:      return "s";    // BMO Linger
            case ParamFormat::Metres:       return "m";    // BMO Linger
            case ParamFormat::Pan:          return "";
            case ParamFormat::Ratio:        return "";
            case ParamFormat::Plain:        return "";
        }

        return "";
    }

    //== Normalised <-> real, without JUCE ====================================
    // The rack's generic slot parameters hold normalised values and convert
    // through these, so they have to agree with what juce::NormalisableRange
    // does for the same range: linear, snapped to the step.

    float clampReal (float v) const noexcept
    {
        v = v < min ? min : (v > max ? max : v);

        if (step > 0.0f)
            v = min + step * std::round ((v - min) / step);

        return v < min ? min : (v > max ? max : v);
    }

    float toNormalised (float real) const noexcept
    {
        if (max <= min)
            return 0.0f;

        const auto v = clampReal (real);
        const auto n = logarithmic ? std::log (v / min) / std::log (max / min)
                                   : (v - min) / (max - min);
        return n < 0.0f ? 0.0f : (n > 1.0f ? 1.0f : n);
    }

    float fromNormalised (float n) const noexcept
    {
        n = n < 0.0f ? 0.0f : (n > 1.0f ? 1.0f : n);
        return clampReal (logarithmic ? min * std::pow (max / min, n)
                                      : min + n * (max - min));
    }

    /** The value as the panel and the host print it. */
    std::string text (float real) const
    {
        char buf[64];

        // A parameter that prints itself does so before anything else. Only a
        // continuous one ever sets this -- a choice already has its names.
        if (textFn != nullptr && kind == ParamKind::Float)
            return textFn (real);

        switch (kind)
        {
            case ParamKind::Bool:
                return real >= 0.5f ? "On" : "Off";

            case ParamKind::Choice:
            {
                const auto i = (int) std::lround (real);
                return i >= 0 && i < numChoices() ? choices[(size_t) i] : "";
            }

            case ParamKind::Float:
                break;
        }

        switch (format)
        {
            case ParamFormat::Decibels:
                std::snprintf (buf, sizeof (buf), "%s%.1f dB", real > 0.0f ? "+" : "", (double) real);
                return buf;

            case ParamFormat::Percent:
                std::snprintf (buf, sizeof (buf), "%d %%", (int) std::lround (real));
                return buf;

            case ParamFormat::Pan:
            {
                const auto i = (int) std::lround (real);
                if (i == 0) return "C";
                std::snprintf (buf, sizeof (buf), "%s %d", i < 0 ? "L" : "R", std::abs (i));
                return buf;
            }

            case ParamFormat::Hertz:
                if (real >= 10000.0f)     std::snprintf (buf, sizeof (buf), "%.1f kHz", (double) real / 1000.0);
                else if (real >= 1000.0f) std::snprintf (buf, sizeof (buf), "%.2f kHz", (double) real / 1000.0);
                else if (real >= 100.0f)  std::snprintf (buf, sizeof (buf), "%.0f Hz", (double) real);
                else                      std::snprintf (buf, sizeof (buf), "%.1f Hz", (double) real);
                return buf;

            case ParamFormat::Milliseconds:
                std::snprintf (buf, sizeof (buf), real < 10.0f ? "%.1f ms" : "%.0f ms", (double) real);
                return buf;

            case ParamFormat::Ratio:
                std::snprintf (buf, sizeof (buf), "%.1f:1", (double) real);
                return buf;

            // BMO Linger. Two decimals under ten seconds and one above it, so
            // DECAY reads "1.80 s" where a tenth is the JND-ish step and
            // "12.0 s" where it is not -- the same thinning Milliseconds and
            // Hertz already do at a decade boundary, rather than a new habit.
            case ParamFormat::Seconds:
                std::snprintf (buf, sizeof (buf), real < 10.0f ? "%.2f s" : "%.1f s", (double) real);
                return buf;

            // BMO Linger. One decimal the whole way: SIZE is a room dimension
            // and 0.1 m is already finer than anyone hears across a 0.5-80 m
            // travel, so "0.5 m" and "80.0 m" both read as lengths.
            case ParamFormat::Metres:
                std::snprintf (buf, sizeof (buf), "%.1f m", (double) real);
                return buf;

            case ParamFormat::Plain:
                break;
        }

        std::snprintf (buf, sizeof (buf), "%g", (double) real);
        return buf;
    }

    /** What a typed entry means, in real units, for the formats that print a
        unit a plain number parse would get wrong: "2.1k" and "2.1 kHz" are
        2100 Hz. Everything else is the number in front, which is what JUCE's
        own parse does -- so no existing format's text entry changes. */
    float valueFromText (const std::string& text) const
    {
        const auto* s = text.c_str();
        while (*s == ' ') ++s;

        char* end = nullptr;
        auto v = std::strtof (s, &end);

        if (end == s)
            return def;

        if (format == ParamFormat::Hertz)
        {
            while (*end == ' ') ++end;
            if (*end == 'k' || *end == 'K')
                v *= 1000.0f;
        }

        return clampReal (v);
    }
};

using ParamSpecs = std::vector<ParamSpec>;

/** Position of an ID in a spec list, or -1. */
inline int indexOfParam (const ParamSpecs& specs, const char* id) noexcept
{
    for (size_t i = 0; i < specs.size(); ++i)
        if (std::string (specs[i].id) == id)
            return (int) i;

    return -1;
}

/** One parameter setting inside a factory preset, in real units: a detent
    index for a choice, 0 or 1 for a switch. */
struct Setting
{
    const char* id;
    float value;
};

struct FactoryPreset
{
    const char* name;
    std::vector<Setting> settings;
};

} // namespace bmo
