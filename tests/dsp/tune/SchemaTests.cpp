/*
    The parameter schema, written out in full (BMO Mix Rack's
    tests/plugin/*Tests.cpp are the model).

    FROZEN from the first plugin build, 2026-09-10 (Frosty): that build goes
    into Ableton, and a saved session references every id, its position, its
    kind, range, step and default, and every choice's name and order. This
    table is where a change has to be argued for; a new parameter goes on the
    end of specs() and of this table.

    2026-09-11: engine, glide, formant, formant_shift and latency removed, with
    HYBRID and Studio (Frosty). Argued in params.h: a VST3 host knows each
    parameter by a hash of its id and state is saved by id, so removing moves
    no other parameter. Their ids are retired, and checked here as such.

    2026-09-11, later: retune (a unitless knob) removed and retune_ms appended
    on the end, 146 steps in milliseconds (Frosty: "display ms ... .1 ms
    increments for 0-5 ms and then 1 ms increments through 100 ms"). A new id,
    not a new meaning for the old one, because a saved 36 meant 10 ms and
    would silently have become 36 ms. Argued in full at the head of params.h.

    Also checked: that the ModuleDsp adapter maps every value the way the
    spec list says, and that the schema fits a rack slot's 32 parameters
    (not a requirement for this product, but a free option on the future).
*/

#include "modules/tune/dsp/TuneDsp.h"
#include "tests/dsp/tune/TestUtil.h"
#include "tools/tune/common/Signals.h"

#include <cmath>
#include <cstring>
#include <string>

using namespace bmo::tune;
using namespace bmo::tune::test;

namespace
{
    struct Row { const char* id; bmo::ParamKind kind; float min, max, def, step; };

    const Row kGolden[] = {
        { "key",          bmo::ParamKind::Choice, 0.0f,   16.0f,  0.0f,   1.0f },
        { "scale",         bmo::ParamKind::Choice, 0.0f,   2.0f,   0.0f,   1.0f },
        { "range",         bmo::ParamKind::Choice, 0.0f,   4.0f,   0.0f,   1.0f },
        { "vibrato",       bmo::ParamKind::Float,  0.0f,   150.0f, 0.0f,   1.0f },
        { "flex",          bmo::ParamKind::Float,  0.0f,   100.0f, 0.0f,   1.0f },
        { "ref_a",         bmo::ParamKind::Float,  380.0f, 480.0f, 440.0f, 0.1f },
        { "note_c",  bmo::ParamKind::Bool, 0, 1, 1, 1 }, { "note_cs", bmo::ParamKind::Bool, 0, 1, 1, 1 },
        { "note_d",  bmo::ParamKind::Bool, 0, 1, 1, 1 }, { "note_ds", bmo::ParamKind::Bool, 0, 1, 1, 1 },
        { "note_e",  bmo::ParamKind::Bool, 0, 1, 1, 1 }, { "note_f",  bmo::ParamKind::Bool, 0, 1, 1, 1 },
        { "note_fs", bmo::ParamKind::Bool, 0, 1, 1, 1 }, { "note_g",  bmo::ParamKind::Bool, 0, 1, 1, 1 },
        { "note_gs", bmo::ParamKind::Bool, 0, 1, 1, 1 }, { "note_a",  bmo::ParamKind::Bool, 0, 1, 1, 1 },
        { "note_as", bmo::ParamKind::Bool, 0, 1, 1, 1 }, { "note_b",  bmo::ParamKind::Bool, 0, 1, 1, 1 },
        { "retune_ms",     bmo::ParamKind::Choice, 0.0f,   145.0f, 0.0f,   1.0f },
    };

    /** Retune Speed's 146 names, written out from the rule Frosty gave rather
        than from params.h, so a slip in either shows. */
    std::vector<std::string> retuneNames()
    {
        std::vector<std::string> n;
        for (int tenths = 0; tenths <= 50; ++tenths)
            n.push_back (std::to_string (tenths / 10) + "." + std::to_string (tenths % 10) + " ms");
        for (int ms = 6; ms <= 100; ++ms)
            n.push_back (std::to_string (ms) + " ms");
        return n;
    }

    /** Every choice parameter's names, in order. Renaming or reordering one
        re-points every saved session that chose it. */
    struct Choices { const char* id; std::vector<std::string> names; };

    const Choices kGoldenChoices[] = {
        { "key",     { "C", "C#", "Db", "D", "D#", "Eb", "E", "F", "F#", "Gb", "G", "G#", "Ab", "A", "A#", "Bb", "B" } },
        { "scale",   { "Chromatic", "Major", "Minor" } },
        { "range",   { "Auto", "Soprano", "Alto/Tenor", "Bass", "Instrument" } },
        { "retune_ms", retuneNames() },
    };
}

int main()
{
    const auto& s = specs();
    constexpr auto golden = sizeof (kGolden) / sizeof (kGolden[0]);

    check (s.size() == golden, "the schema has exactly the golden table's parameters");
    check ((int) s.size() == Index::count, "enum Index and specs() agree on the count");
    check (s.size() <= 32, "it fits a BMO Mix Rack slot's 32 parameters");

    for (const auto* retired : kRetiredIds)
        check (bmo::indexOfParam (s, retired) < 0, std::string ("the retired id ") + retired + " is not reused");

    for (size_t i = 0; i < std::min (s.size(), golden); ++i)
    {
        const auto& g = kGolden[i];
        const auto& p = s[i];
        const auto where = std::string (g.id) + " at position " + std::to_string (i);
        check (std::strcmp (p.id, g.id) == 0, "id " + where);
        check (p.kind == g.kind, "kind of " + where);
        check (p.min == g.min && p.max == g.max, "range of " + where);
        check (p.def == g.def, "default of " + where);
        check (p.step == g.step, "step of " + where);
    }

    int choiceParams = 0;
    for (const auto& p : s)
        choiceParams += p.kind == bmo::ParamKind::Choice ? 1 : 0;
    check (choiceParams == (int) (sizeof (kGoldenChoices) / sizeof (kGoldenChoices[0])),
           "every choice parameter has its names in the golden list");

    for (const auto& c : kGoldenChoices)
    {
        const auto i = bmo::indexOfParam (s, c.id);
        check (i >= 0, std::string ("choice ") + c.id + " exists");
        if (i < 0)
            continue;

        std::vector<std::string> names;
        for (const auto* n : s[(size_t) i].choices)
            names.push_back (n);
        check (names == c.names, std::string ("the names and order of ") + c.id + "'s choices");
    }

    // A Key spelling names its pitch class: the letter's, moved by its
    // accidental. Worked out from the names, so a mistyped table in params.h
    // cannot agree with itself.
    for (int k = 0; k < kNumKeySpellings; ++k)
    {
        static constexpr int letter[7] = { 9, 11, 0, 2, 4, 5, 7 };   // A B C D E F G
        const std::string n = kKeySpellings[k];
        auto pc = letter[n[0] - 'A'];
        if (n.size() > 1)
            pc += n[1] == '#' ? 1 : -1;
        pc = (pc + 12) % 12;
        check (pitchClassOfKey (k) == pc, "Key " + n + " is pitch class " + std::to_string (pc));
    }

    // Defaults are the hard-tune, chromatic instance.
    std::vector<float> v;
    for (const auto& p : s)
        v.push_back (p.def);
    const auto d = TuneParams::fromValues (v.data(), (int) v.size());
    check (d.retuneMs == 0.0 && d.scale == ScaleType::chromatic && d.allowed == kAllNotes && d.refA == 440.0,
           "the defaults are a hard-tune, chromatic instance");
    check (d.key == 0, "in C");

    {
        // Each Retune Speed step reaches the core as the milliseconds its
        // name says -- the host shows the name, the core hears the number.
        const auto names = retuneNames();
        bool all = true;
        for (int step = 0; step < kNumRetuneSteps; ++step)
        {
            auto w = v;
            w[(size_t) Index::retuneMs] = (float) step;
            const auto p = TuneParams::fromValues (w.data(), (int) w.size());
            all = all && std::abs (p.retuneMs - std::stod (names[(size_t) step])) < 1.0e-9;
        }
        check (all, "every Retune Speed step reaches the core as the ms its name reads");
    }

    {
        auto bFlat = v;
        bFlat[(size_t) Index::key] = 15.0f;       // Bb
        const auto b = TuneParams::fromValues (bFlat.data(), (int) bFlat.size());
        check (b.key == 10, "Key Bb reaches the core as pitch class 10");
    }

    // The adapter: values in, through ModuleDsp, sound out.
    {
        TuneDsp dsp;
        dsp.setParams (v.data(), (int) v.size());
        dsp.prepare (48000.0, 256, 2);

        auto left = signals::voice (signals::steady (220.0, 0.2, 48000.0), 48000.0).samples;
        auto right = std::vector<float> (left.size(), 0.0f);
        for (size_t at = 0; at < left.size(); at += 256)
        {
            const auto n = (int) std::min<size_t> (256, left.size() - at);
            float* ch[2] { left.data() + at, right.data() + at };
            dsp.setParams (v.data(), (int) v.size());
            dsp.process (ch, 2, n);
        }

        check (left == right, "the adapter processes mono and copies it to every channel");
        check (dsp.latencyForParams (v.data(), (int) v.size()) == 0, "and reports 0 latency: Live is the only contract");
    }

    return finish ("schema");
}
