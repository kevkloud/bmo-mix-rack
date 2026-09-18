#pragma once

#include "core/state/ParamSpec.h"
#include <array>
#include <string>

namespace bmo::deq
{

//==============================================================================
// Parameter IDs. Permanent and append-only from the first release -- see
// modules/eq/params.h for why, and tests/plugin/DeqTests.cpp for the table
// that holds them.
//==============================================================================

inline constexpr auto kModuleId   = "deq";
inline constexpr auto kModuleName = "BMO DEQ";

inline constexpr int kVersionHint   = 1;
inline constexpr int kStateVersion  = 1;
inline constexpr int kSchemaVersion = 1;

/** Twelve bands (decided 2026-09-11; the spec's A6 asked for 24). */
inline constexpr int kBands = 12;

/** What each band has, thirteen of them. The order here is the order a band's
    controls appear in when a band is written out whole (bands 7-12); it is not
    the order of the list, which is below. */
enum class Control { on, shape, freq, gain, q, place, dyn, dir, thr, range, ratio, attack, release, count };

inline constexpr int kPerBand = (int) Control::count;

/** Choice lists. Their order is stored in sessions -- append only. */
inline constexpr const char* kShapeNames[] { "Bell", "Low Shelf", "High Shelf", "Low Cut", "High Cut" };
inline constexpr const char* kPlaceNames[] { "Stereo", "Mid", "Side" };
inline constexpr const char* kDirNames[]   { "Above", "Below" };

//==============================================================================
/** Where everything sits in the list -- the part that can never change.

    A rack slot has 32 host lanes, and only a module's first 32 parameters get
    one (core/rack/SlotOverflow.h). Frosty's allocation, 2026-09-11:

        0        output
        1..30    bands 1-6 x (freq, gain, Q, threshold, range)
        31       DEQ in/out
        32..79   bands 1-6, the rest: on, shape, placement, dyn, direction,
                 ratio, attack, release
        80..157  bands 7-12, all thirteen each, in Control order
        158      AUTO (added 2026-09-11, before any release; appended so
                 nothing above it moved)

    So in a rack every band a user reaches for first can be automated by its
    frequency, gain, Q and dynamic depth; standalone, everything can.
*/
inline constexpr int kOutput = 0;
inline constexpr int kActive = 31;
inline constexpr int kLaneBands = 6;
inline constexpr int kAutoGain = 1 + kLaneBands * 5 + 1 + kLaneBands * 8 + (kBands - kLaneBands) * kPerBand;
inline constexpr int kCount = kAutoGain + 1;
static_assert (kAutoGain == 158 && kCount == 159);

/** The widest a shelf's Q goes. Past about 2 a shelf's resonant bump is where
    the matched design is weakest near Nyquist (up to 6 dB out, AGENTS.md
    "Numbers"); at or under it the worst is 0.87 dB. The parameter keeps the
    bells' 0.1-40 -- a band's Q is one host parameter whatever its shape -- and
    the engine, the curve and the panel all read a shelf's through this. */
inline constexpr float kShelfMaxQ = 2.0f;

/** The Q a band actually runs at: its knob, or kShelfMaxQ for a shelf asked
    for more. shapeChoice is the stored choice index (kShapeNames). */
inline constexpr float effectiveQ (int shapeChoice, float q) noexcept
{
    return (shapeChoice == 1 || shapeChoice == 2) && q > kShelfMaxQ ? kShelfMaxQ : q;
}

inline constexpr int indexOf (int band, Control c) noexcept
{
    constexpr Control lane[] { Control::freq, Control::gain, Control::q, Control::thr, Control::range };
    constexpr Control rest[] { Control::on, Control::shape, Control::place, Control::dyn, Control::dir,
                               Control::ratio, Control::attack, Control::release };

    if (band >= kLaneBands)
        return 1 + kLaneBands * 5 + 1 + kLaneBands * 8 + (band - kLaneBands) * kPerBand + (int) c;

    for (int i = 0; i < 5; ++i)
        if (lane[i] == c)
            return 1 + band * 5 + i;

    for (int i = 0; i < 8; ++i)
        if (rest[i] == c)
            return 1 + kLaneBands * 5 + 1 + band * 8 + i;

    return -1;
}

/** A fresh band sits where it would be useful when switched on: spread from a
    30 Hz low cut to an 18 kHz high cut. Every band starts off, so Init is a
    wire and a new instance does nothing until asked. */
inline constexpr float kDefaultHz[kBands] { 30, 80, 160, 300, 500, 800, 1300, 2000, 3200, 5000, 10000, 18000 };
inline constexpr int   kDefaultShape[kBands] { 3, 1, 0, 0, 0, 0, 0, 0, 0, 0, 2, 4 };

//==============================================================================
inline const ParamSpecs& specs()
{
    // Generated, because 159 hand-written lines would be 159 chances to put one
    // in the wrong place -- but generated into a fixed table whose every entry
    // DeqTests writes out in full. The strings live here, forever: the specs
    // (and the rack's slot parameters) keep pointers into them.
    struct Table
    {
        std::array<std::string, kCount> ids, names;
        ParamSpecs list;

        Table()
        {
            list.resize ((size_t) kCount);

            auto put = [this] (int i, std::string id, std::string name, ParamSpec s)
            {
                ids[(size_t) i] = std::move (id);
                names[(size_t) i] = std::move (name);
                s.id = ids[(size_t) i].c_str();
                s.name = names[(size_t) i].c_str();
                list[(size_t) i] = std::move (s);
            };

            using S = ParamSpec;
            using F = ParamFormat;

            put (kOutput, "out", "Output", S::floatParam ("", "", -24.0f, 24.0f, 0.1f, 0.0f, F::Decibels));
            put (kActive, "active", "DEQ", S::boolParam ("", "", true));
            put (kAutoGain, "auto_gain", "Auto Gain", S::boolParam ("", "", false));

            for (int b = 0; b < kBands; ++b)
            {
                const auto p = "b" + std::to_string (b + 1) + "_";
                const auto n = "Band " + std::to_string (b + 1) + " ";
                auto at = [b] (Control c) { return indexOf (b, c); };

                put (at (Control::on),      p + "on",    n + "On",      S::boolParam ("", "", false));
                put (at (Control::shape),   p + "shape", n + "Shape",   S::choiceParam ("", "", { std::begin (kShapeNames), std::end (kShapeNames) }, kDefaultShape[b]));
                // A 0.1 Hz step rather than none: a host carries a parameter as a
                // 32-bit normalised value, and a continuous log law round-trips
                // through that with ~3e-4 Hz of error at the top of the range.
                // Snapping to 0.1 makes the round trip exact and costs nothing
                // anyone could hear.
                put (at (Control::freq),    p + "freq",  n + "Freq",    S::logParam   ("", "", 20.0f, 20000.0f, 0.1f, kDefaultHz[b], F::Hertz));
                put (at (Control::gain),    p + "gain",  n + "Gain",    S::floatParam ("", "", -24.0f, 24.0f, 0.1f, 0.0f, F::Decibels));
                put (at (Control::q),       p + "q",     n + "Q",       S::logParam   ("", "", 0.1f, 40.0f, 0.01f, 0.71f));
                put (at (Control::place),   p + "place", n + "Place",   S::choiceParam ("", "", { std::begin (kPlaceNames), std::end (kPlaceNames) }, 0));
                put (at (Control::dyn),     p + "dyn",   n + "Dyn",     S::boolParam ("", "", false));
                put (at (Control::dir),     p + "dir",   n + "Dir",     S::choiceParam ("", "", { std::begin (kDirNames), std::end (kDirNames) }, 0));
                put (at (Control::thr),     p + "thr",   n + "Thresh",  S::floatParam ("", "", -60.0f, 0.0f, 0.1f, -24.0f, F::Decibels));
                put (at (Control::range),   p + "range", n + "Range",   S::floatParam ("", "", -24.0f, 24.0f, 0.1f, -6.0f, F::Decibels));
                put (at (Control::ratio),   p + "ratio", n + "Ratio",   S::logParam   ("", "", 1.0f, 20.0f, 0.01f, 2.0f, F::Ratio));
                put (at (Control::attack),  p + "atk",   n + "Attack",  S::logParam   ("", "", 0.1f, 200.0f, 0.01f, 5.0f, F::Milliseconds));
                put (at (Control::release), p + "rel",   n + "Release", S::logParam   ("", "", 5.0f, 2000.0f, 0.1f, 120.0f, F::Milliseconds));
            }
        }
    };

    static const Table table;
    return table.list;
}

} // namespace bmo::deq
