/*
    BMO Dwell's DSP, stage 1: the schema and the wiring, before there is any
    delay to measure.

    docs/delay/11-integration-and-test-plan.md §4 lists the suites this file
    grows into -- time accuracy, feedback decay, the mix law, the FX stage.
    None of them can be written yet. What can be written, and is worth writing
    first, is everything that is *permanent*: ids 0-31 in their frozen order,
    the three choice lists in their frozen index order, and the mapping from
    spec index to named value in DwellDsp::setParams.

    **The table is docs/delay/15's, settled 2026-09-21: thirty-two parameters,
    two engines.** `11` §3 still prints the twenty-parameter checkpoint and is
    stale until its own pass rewrites it.

    That last one is the reason this file exists now rather than with the DSP.
    The schema cannot move after release, so a lane read off by one here would
    be wrong for the life of the product, and it would look like a DSP fault
    -- a delay whose FEEDBACK knob sets DRIVE -- rather than the wiring fault
    it is. Pinning it before any arithmetic is written against it costs one
    test and removes a whole class of stage-2 debugging.

    Also pinned: latency is exactly 0 at every setting, and the pass-through
    core is bit-exact. Both are claims about the finished module, not about the
    skeleton, so they stay here unchanged as the DSP arrives.

    JUCE-free; runs in the CI `dsp` job.
*/

#include "modules/dwell/dsp/DspCore.h"
#include "modules/dwell/dsp/DwellDsp.h"

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace P = bmo::dwell;

namespace
{

int failures = 0, checks = 0;

void check (bool condition, const std::string& what)
{
    ++checks;

    if (! condition)
    {
        std::printf ("FAIL  %s\n", what.c_str());
        ++failures;
    }
}

/** Every parameter at its default, in real units, as the host would hand them
    over. */
std::vector<float> defaults()
{
    std::vector<float> v;
    for (const auto& s : P::specs())
        v.push_back (s.def);
    return v;
}

//==============================================================================
/** The schema, written out here as well as in tests/plugin/DwellTests.cpp.

    Two tables rather than one, deliberately: this one runs in the JUCE-free CI
    job, which is the one that runs on every push, and it is the only check on
    the schema that a DSP-only container can make. If they disagree, one of
    them was edited and the other was not, which is exactly the event worth
    failing on. */
struct Row { int index; const char* id; float min, max, def; int choices; };

const Row kSchema[]
{
    {  0, "time",       1.0f, 2000.0f,   375.0f,  0 },
    {  1, "sync",       0.0f,    1.0f,     0.0f,  0 },
    {  2, "note",       0.0f,   15.0f,     8.0f, 16 },
    {  3, "feedback",   0.0f,  100.0f,    35.0f,  0 },
    {  4, "character",  0.0f,    2.0f,     0.0f,  3 },
    {  5, "stereo",     0.0f,    2.0f,     0.0f,  3 },
    {  6, "low_cut",   20.0f, 1000.0f,    20.0f,  0 },
    {  7, "high_cut", 1000.0f, 20000.0f, 20000.0f, 0 },
    {  8, "mod_rate",   0.1f,    8.0f,     0.6f,  0 },
    {  9, "mod_depth",  0.0f,  100.0f,     0.0f,  0 },
    { 10, "drive",      0.0f,  100.0f,     0.0f,  0 },
    // DUCK defaults to 0 dB from 2026-09-21 -- ducking ships inert and opt-in
    // (DECIDED, Frosty; docs/delay/10 §6). The 0-24 dB range is unchanged;
    // only the default moved, and the slot moved with VOICE's deletion.
    { 11, "duck",       0.0f,   24.0f,     0.0f,  0 },
    { 12, "mix",        0.0f,  100.0f,    35.0f,  0 },
    // The lane's gates and its tail. `lane_gain` is bipolar and is a **float**,
    // not the three-way choice `throw_mode` was: below 0 the lane decays, at 0
    // it holds at exact unity, above it builds.
    { 13, "send",       0.0f,    1.0f,     0.0f,  0 },
    { 14, "lane_gain", -100.0f, 100.0f,  -40.0f,  0 },
    { 15, "hold",       0.0f,    1.0f,     0.0f,  0 },
    { 16, "chop",       0.0f,    1.0f,     0.0f,  0 },
    { 17, "fx",         0.0f,    1.0f,     0.0f,  0 },
    { 18, "fx_type",    0.0f,    2.0f,     0.0f,  3 },
    { 19, "fx_amount",  0.0f,  100.0f,    35.0f,  0 },
    // The lane, ids 20-31. LINK is the one bool in this schema that defaults
    // **on**: a mirror nobody has asked to differ follows the main delay.
    { 20, "link",            0.0f,     1.0f,     1.0f,  0 },
    { 21, "lane_level",    -24.0f,    24.0f,     0.0f,  0 },
    { 22, "lane_time",       1.0f,  2000.0f,   250.0f,  0 },
    { 23, "lane_character",  0.0f,     2.0f,     0.0f,  3 },
    { 24, "lane_stereo",     0.0f,     2.0f,     0.0f,  3 },
    { 25, "lane_low_cut",   20.0f,  1000.0f,    20.0f,  0 },
    { 26, "lane_high_cut", 1000.0f, 20000.0f, 20000.0f, 0 },
    { 27, "lane_mod_rate",   0.1f,     8.0f,     0.6f,  0 },
    { 28, "lane_mod_depth",  0.0f,   100.0f,     0.0f,  0 },
    { 29, "lane_fx",         0.0f,     1.0f,     0.0f,  0 },
    { 30, "lane_fx_type",    0.0f,     2.0f,     0.0f,  3 },
    { 31, "lane_fx_amount",  0.0f,   100.0f,    35.0f,  0 },
    // Id 32, the one row deliberately past a rack slot's 32 host lanes.
    // SlotOverflow carries it everywhere but a rack automation lane; see
    // modules/dwell/params.h, and testSchemaIsWhatItWillAlwaysBe below for
    // the assertion that it is the only one.
    { 32, "fx_link",          0.0f,     1.0f,     1.0f,  0 },
};

void testSchemaIsWhatItWillAlwaysBe()
{
    const auto& specs = P::specs();

    check (specs.size() == 33, "thirty-three parameters, ids 0-32");
    check (specs.size() == (size_t) P::Index::count, "the Index enum matches specs()");
    check ((int) P::Index::count == 33, "Index::count is 33");

    // The same shape tests/plugin/DwellTests.cpp asserts, checked here too
    // because this is the suite a DSP-only container runs. **Not "everything
    // fits" any more**: past 32 a parameter keeps working everywhere but a
    // rack automation lane (SlotOverflow), and `fx_link` is deliberately the
    // one row over that line. Exactly one, and exactly that one.
    check (specs.size() == 33, "exactly one parameter sits past a rack slot's 32 host lanes");
    check (std::string (specs[32].id) == P::kFxLink,
           "the one parameter with no rack automation lane is fx_link, deliberately");

    if (specs.size() != 33)
        return;

    for (const auto& row : kSchema)
    {
        const auto& s = specs[(size_t) row.index];
        const std::string where { std::string ("parameter ") + std::to_string (row.index)
                                  + " (" + row.id + ")" };

        check (std::string (s.id) == row.id, where + " has the id it will keep forever");
        check (std::abs (s.min - row.min) < 1.0e-4f, where + " keeps its minimum");
        check (std::abs (s.max - row.max) < 1.0e-4f, where + " keeps its maximum");
        check (std::abs (s.def - row.def) < 1.0e-4f, where + " keeps its default");
        check (s.numChoices() == row.choices, where + " keeps its choice count");
    }

    // The enum is the other half of the same fact: the DSP indexes with it.
    check (std::string (specs[(size_t) P::Index::time].id)     == P::kTime,     "Index::time is time");
    check (std::string (specs[(size_t) P::Index::mix].id)      == P::kMix,      "Index::mix is mix");
    check (std::string (specs[(size_t) P::Index::send].id)     == P::kSend,     "Index::send is send");
    check (std::string (specs[(size_t) P::Index::fxAmount].id) == P::kFxAmount, "Index::fxAmount is fx_amount");
    check (std::string (specs[(size_t) P::Index::link].id)     == P::kLink,     "Index::link is link");
    check (std::string (specs[(size_t) P::Index::laneFxAmount].id) == P::kLaneFxAmount,
           "Index::laneFxAmount is lane_fx_amount, and it is the last one");
}

/** Choice lists are stored by index, so the order is as permanent as the ids.

    NOTE is the one that is not "least to most": its index is the automation
    lane, so it ascends in duration and all sixteen ship at once. The other
    two run least to most intervention so that index 0 is the neutral value a
    corrupt state lands on.

    The lane's three choices are the *same lists*, so checking the main
    delay's checks both -- and the pair of assertions at the end is what
    catches them being split into two lists that can drift apart. */
void testChoiceListsKeepTheirOrder()
{
    const auto& specs = P::specs();

    const char* expectedNotes[]
    {
        "1/32", "1/16T", "1/32D", "1/16", "1/8T", "1/16D", "1/8", "1/4T",
        "1/8D", "1/4", "1/2T", "1/4D", "1/2", "1/1T", "1/2D", "1/1"
    };

    const auto& note = specs[(size_t) P::Index::note];
    check (note.numChoices() == 16, "sixteen note values");

    for (int i = 0; i < note.numChoices() && i < 16; ++i)
        check (std::string (note.choices[(size_t) i]) == expectedNotes[i],
               std::string ("note index ") + std::to_string (i) + " is " + expectedNotes[i]);

    check (note.def == 8.0f && std::string (note.choices[8]) == "1/8D",
           "the default note is the dotted eighth at index 8");

    const char* expectedCharacter[] { "Clean", "Tape", "Bucket-brigade" };
    const auto& character = specs[(size_t) P::Index::character];
    check (character.numChoices() == 3, "three characters");
    for (int i = 0; i < character.numChoices() && i < 3; ++i)
        check (std::string (character.choices[(size_t) i]) == expectedCharacter[i],
               std::string ("character index ") + std::to_string (i) + " is " + expectedCharacter[i]);

    const char* expectedStereo[] { "Stereo", "Ping-pong", "Dual offset" };
    const auto& stereo = specs[(size_t) P::Index::stereo];
    check (stereo.numChoices() == 3, "three stereo modes");
    for (int i = 0; i < stereo.numChoices() && i < 3; ++i)
        check (std::string (stereo.choices[(size_t) i]) == expectedStereo[i],
               std::string ("stereo index ") + std::to_string (i) + " is " + expectedStereo[i]);

    // The candidate list, **three entries from 2026-09-22**. Octave up, Octave
    // down and Reverse went on the 21st; **Sweep went on the 22nd**, because
    // it swept VOICE's resonant centre and VOICE was deleted the day before,
    // which left no filter in the loop for it to sweep. A cut is only possible
    // before ship, and the contents may still change until then -- but that
    // index 0 is a type and not "Off" may not, and has survived both cuts:
    // `fx` owns off, and a corrupt state landing on 0 has to give the gentlest
    // type with the stage still gated by a bool that defaults off.
    const char* expectedFx[] { "Diffuse", "Pan/Tremolo", "Crush" };
    const auto& fxType = specs[(size_t) P::Index::fxType];
    check (fxType.numChoices() == 3, "three FX candidates");
    check (fxType.def == 0.0f, "the default FX type is index 0");
    for (int i = 0; i < fxType.numChoices() && i < 3; ++i)
        check (std::string (fxType.choices[(size_t) i]) == expectedFx[i],
               std::string ("FX type index ") + std::to_string (i) + " is " + expectedFx[i]);
    for (int i = 0; i < fxType.numChoices(); ++i)
        check (std::string (fxType.choices[(size_t) i]) != "Off",
               "no FX type is called Off -- fx owns off");

    // The lane mirrors the main delay off the same three lists rather than
    // declaring its own, which is the only way the two cannot drift apart.
    const auto& laneCharacter = specs[(size_t) P::Index::laneCharacter];
    const auto& laneStereo    = specs[(size_t) P::Index::laneStereo];
    const auto& laneFxType    = specs[(size_t) P::Index::laneFxType];

    check (laneCharacter.choices == character.choices, "the lane's characters are the main delay's");
    check (laneStereo.choices    == stereo.choices,    "the lane's stereo modes are the main delay's");
    check (laneFxType.choices    == fxType.choices,    "the lane's FX types are the main delay's");
}

/** Every value reaches the core under the name it was given.

    One parameter at a time, each set to a value nothing else uses, so a swap
    between two lanes cannot pass by reading the right number from the wrong
    place. */
void testEveryParameterIsWiredToItsOwnValue()
{
    P::DwellDsp dsp;
    dsp.prepare (48000.0, 512, 2);

    auto v = defaults();

    v[P::Index::time]      = 913.0f;
    v[P::Index::sync]      = 1.0f;
    v[P::Index::note]      = 3.0f;
    v[P::Index::feedback]  = 61.0f;
    v[P::Index::character] = 2.0f;
    v[P::Index::stereo]    = 1.0f;
    v[P::Index::lowCut]    = 137.0f;
    v[P::Index::highCut]   = 7300.0f;
    v[P::Index::modRate]   = 2.7f;
    v[P::Index::modDepth]  = 29.0f;
    v[P::Index::drive]     = 83.0f;
    v[P::Index::duck]      = 17.0f;
    v[P::Index::mix]       = 72.0f;
    v[P::Index::send]      = 1.0f;
    v[P::Index::laneGain]  = 64.0f;
    v[P::Index::hold]      = 1.0f;
    v[P::Index::chop]      = 1.0f;
    v[P::Index::fx]        = 1.0f;
    v[P::Index::fxType]    = 1.0f;
    v[P::Index::fxAmount]  = 12.0f;

    v[P::Index::link]          = 0.0f;   // the only one whose default is on
    v[P::Index::laneLevel]     = -7.5f;
    v[P::Index::laneTime]      = 431.0f;
    v[P::Index::laneCharacter] = 1.0f;
    v[P::Index::laneStereo]    = 2.0f;
    v[P::Index::laneLowCut]    = 219.0f;
    v[P::Index::laneHighCut]   = 5100.0f;
    v[P::Index::laneModRate]   = 4.3f;
    v[P::Index::laneModDepth]  = 56.0f;
    v[P::Index::laneFx]        = 1.0f;
    v[P::Index::laneFxType]    = 2.0f;
    v[P::Index::laneFxAmount]  = 88.0f;

    // Id 32. Past a rack slot's lanes and wired like every row under it --
    // SlotOverflow's whole point is that it still reaches here. Set to the
    // opposite of its default, as `link` above is, because a bool that
    // defaults on is one a forgotten assignment would still read correctly.
    v[P::Index::fxLink]        = 0.0f;

    dsp.setParams (v.data(), (int) v.size());
    const auto& p = dsp.getCore().getParams();

    check (p.timeMs          == 913.0f,  "TIME reaches the core");
    check (p.noteChoice      == 3,       "NOTE reaches the core");
    check (p.feedbackPct     == 61.0f,   "FEEDBACK reaches the core");
    check (p.characterChoice == 2,       "CHARACTER reaches the core");
    check (p.stereoChoice    == 1,       "STEREO reaches the core");
    check (p.lowCutHz        == 137.0f,  "LOW CUT reaches the core");
    check (p.highCutHz       == 7300.0f, "HIGH CUT reaches the core");
    check (p.modRateHz       == 2.7f,    "MOD RATE reaches the core");
    check (p.modDepthPct     == 29.0f,   "MOD DEPTH reaches the core");
    check (p.drivePct        == 83.0f,   "DRIVE reaches the core");
    check (p.duckDb          == 17.0f,   "DUCK reaches the core");
    check (p.mixPct          == 72.0f,   "MIX reaches the core");
    check (p.sendHeld,                   "SEND reaches the core");
    check (p.laneGain        == 64.0f,   "LANE GAIN reaches the core");
    check (p.hold,                       "HOLD reaches the core");
    check (p.chop,                       "CHOP reaches the core");
    check (p.fx,                         "FX reaches the core");
    check (p.fxTypeChoice    == 1,       "FX TYPE reaches the core");
    check (p.fxAmountPct     == 12.0f,   "FX AMOUNT reaches the core");

    check (! p.link,                            "LINK reaches the core");
    check (p.laneLevelDb         == -7.5f,      "LANE LEVEL reaches the core");
    check (p.laneTimeMs          == 431.0f,     "LANE TIME reaches the core");
    check (p.laneCharacterChoice == 1,          "LANE CHARACTER reaches the core");
    check (p.laneStereoChoice    == 2,          "LANE STEREO reaches the core");
    check (p.laneLowCutHz        == 219.0f,     "LANE LOW CUT reaches the core");
    check (p.laneHighCutHz       == 5100.0f,    "LANE HIGH CUT reaches the core");
    check (p.laneModRateHz       == 4.3f,       "LANE MOD RATE reaches the core");
    check (p.laneModDepthPct     == 56.0f,      "LANE MOD DEPTH reaches the core");
    check (p.laneFx,                            "LANE FX reaches the core");
    check (p.laneFxTypeChoice    == 2,          "LANE FX TYPE reaches the core");
    check (p.laneFxAmountPct     == 88.0f,      "LANE FX AMOUNT reaches the core");
    check (! p.fxLink,                          "FX LINK reaches the core");

    // The lane's twelve are a mirror of the main delay's rows and the pairs
    // sit next to each other in this struct, which is exactly the shape a
    // copy-paste reads the wrong lane into. Every pair above is set to a
    // different number for that reason; these are the two that would still
    // pass if a lane row were wired to the main's.
    check (p.timeMs != p.laneTimeMs, "TIME and LANE TIME are not the same lane");
    check (p.lowCutHz != p.laneLowCutHz, "LOW CUT and LANE LOW CUT are not the same lane");
}

/** SYNC is in the schema and switched off at the source.

    The slot and NOTE's order are permanent from this release, but no host
    tempo reaches a ModuleDsp yet -- that is docs/delay/12's plumbing and its
    own pull request. Until it lands the DSP must not act on SYNC, whatever a
    session, a preset or an automation lane says. The flag is the only thing
    the change that enables it has to touch. */
void testSyncShipsDisabled()
{
    check (! P::kSyncIsEnabled, "SYNC ships disabled until the tempo plumbing lands");

    P::DwellDsp dsp;
    dsp.prepare (48000.0, 512, 2);

    auto v = defaults();
    v[P::Index::sync] = 1.0f;
    dsp.setParams (v.data(), (int) v.size());

    check (! dsp.getCore().getParams().sync,
           "a session asking for SYNC does not switch it on while it is disabled");
}

/** Zero, everywhere, including at the settings that tempt a delay to report
    its own time. docs/delay/11 §2 and §4 (k). */
void testLatencyIsAlwaysZero()
{
    P::DwellDsp dsp;
    auto v = defaults();

    check (dsp.latencyForParams (v.data(), (int) v.size()) == 0, "zero latency at the defaults");

    for (const auto ms : { 1.0f, 17.5f, 375.0f, 1000.0f, P::kMaxTimeMs })
    {
        v[P::Index::time] = ms;

        for (int ch = 0; ch < 3; ++ch)
        {
            v[P::Index::character] = (float) ch;

            for (const auto drive : { 0.0f, 100.0f })
            {
                v[P::Index::drive] = drive;

                for (const auto on : { 0.0f, 1.0f })
                {
                    v[P::Index::fx]   = on;
                    v[P::Index::hold] = on;

                    check (dsp.latencyForParams (v.data(), (int) v.size()) == 0,
                           "zero latency at TIME " + std::to_string ((int) ms)
                               + " ms, character " + std::to_string (ch));
                }
            }
        }
    }

    // The delay time is never latency: at the maximum, a module that reported
    // it would be asking the host for 2000 ms of compensation.
    v[P::Index::time] = P::kMaxTimeMs;
    v[P::Index::mix]  = 100.0f;
    check (dsp.latencyForParams (v.data(), (int) v.size()) == 0,
           "the wet delay time is not reported as latency");
}

/** Stage 1 passes audio through untouched, sample for sample.

    Not a placeholder assertion: docs/delay/10 §9 requires the dry path to be
    **bit-exact** unity for every MIX at or below 50 %, and the stage-2 null
    test is this comparison with a loop running behind it. */
void testStageOneIsBitExact()
{
    P::DwellDsp dsp;
    dsp.prepare (48000.0, 512, 2);

    auto v = defaults();
    dsp.setParams (v.data(), (int) v.size());

    std::vector<float> left (512), right (512), leftIn (512), rightIn (512);

    for (int i = 0; i < 512; ++i)
    {
        leftIn[(size_t) i]  = std::sin (0.017f * (float) i) * 0.7f;
        rightIn[(size_t) i] = std::cos (0.011f * (float) i) * 0.5f;
        left[(size_t) i]    = leftIn[(size_t) i];
        right[(size_t) i]   = rightIn[(size_t) i];
    }

    float* channels[] { left.data(), right.data() };
    dsp.process (channels, 2, 512);

    bool exact = true;
    for (int i = 0; i < 512; ++i)
        exact = exact && left[(size_t) i] == leftIn[(size_t) i]
                      && right[(size_t) i] == rightIn[(size_t) i];

    check (exact, "the pass-through core is bit-exact, sample for sample");
}

/** The ring is sized from the fixed maximum and not from a parameter, at every
    rate the suite supports -- which is what keeps the allocation in prepare()
    and off the audio thread (docs/delay/10 §10). */
void testTheRingIsSizedFromTheFixedMaximum()
{
    for (const auto rate : { 44100.0, 48000.0, 88200.0, 96000.0, 176400.0, 192000.0 })
    {
        P::DspCore core;
        core.prepare (rate, 512, 2);

        const auto expected = (int) std::ceil ((double) P::kMaxTimeMs * 0.001 * rate);
        check (core.maxDelaySamples() == expected,
               "the maximum delay is " + std::to_string (expected) + " samples at "
                   + std::to_string ((int) rate) + " Hz");
    }

    check (P::kMaxTimeMs == 2000.0f, "the maximum delay is 2000 ms");
}

/** A short array must not be read past its end. The rack hands a module its
    own parameter count, but a chain edited under an older schema can hand over
    fewer, and reading one lane past the array is the kind of fault that passes
    every test until it does not. */
void testAShortParameterArrayIsIgnored()
{
    P::DwellDsp dsp;
    dsp.prepare (48000.0, 512, 2);

    auto v = defaults();
    v[P::Index::mix] = 90.0f;
    dsp.setParams (v.data(), (int) v.size());

    auto shorter = defaults();
    dsp.setParams (shorter.data(), (int) P::Index::count - 1);

    check (dsp.getCore().getParams().mixPct == 90.0f,
           "a short parameter array leaves the last good set in place");
}

} // namespace

//==============================================================================
int main()
{
    testSchemaIsWhatItWillAlwaysBe();
    testChoiceListsKeepTheirOrder();
    testEveryParameterIsWiredToItsOwnValue();
    testSyncShipsDisabled();
    testLatencyIsAlwaysZero();
    testStageOneIsBitExact();
    testTheRingIsSizedFromTheFixedMaximum();
    testAShortParameterArrayIsIgnored();

    std::printf ("%d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
