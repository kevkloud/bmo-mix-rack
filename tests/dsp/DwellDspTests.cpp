/*
    BMO Dwell's DSP, stage 1: the schema and the wiring, before there is any
    delay to measure.

    docs/delay/11-integration-and-test-plan.md §4 lists the suites this file
    grows into -- time accuracy, feedback decay, the mix law, the FX stage.
    None of them can be written yet. What can be written, and is worth writing
    first, is everything that is *permanent*: ids 0-19 in their frozen order,
    the four choice lists in their frozen index order, and the mapping from
    spec index to named value in DwellDsp::setParams.

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
    {  8, "voice",      0.0f,  100.0f,     0.0f,  0 },
    {  9, "mod_rate",   0.1f,    8.0f,     0.6f,  0 },
    { 10, "mod_depth",  0.0f,  100.0f,     0.0f,  0 },
    { 11, "drive",      0.0f,  100.0f,     0.0f,  0 },
    // DUCK defaults to 0 dB from 2026-09-21 -- ducking ships inert and opt-in
    // (DECIDED, Frosty; docs/delay/10 §6). The id, the slot and the 0-24 dB
    // range are unchanged; only the default moved.
    { 12, "duck",       0.0f,   24.0f,     0.0f,  0 },
    { 13, "mix",        0.0f,  100.0f,    35.0f,  0 },
    { 14, "throw",      0.0f,    1.0f,     0.0f,  0 },
    { 15, "throw_mode", 0.0f,    2.0f,     0.0f,  3 },
    { 16, "freeze",     0.0f,    1.0f,     0.0f,  0 },
    { 17, "fx",         0.0f,    1.0f,     0.0f,  0 },
    { 18, "fx_type",    0.0f,    6.0f,     0.0f,  7 },
    { 19, "fx_amount",  0.0f,  100.0f,    35.0f,  0 },
};

void testSchemaIsWhatItWillAlwaysBe()
{
    const auto& specs = P::specs();

    check (specs.size() == 20, "twenty parameters, ids 0-19");
    check (specs.size() == (size_t) P::Index::count, "the Index enum matches specs()");
    check ((int) P::Index::count == 20, "Index::count is 20");

    if (specs.size() != 20)
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
    check (std::string (specs[(size_t) P::Index::time].id)      == P::kTime,      "Index::time is time");
    check (std::string (specs[(size_t) P::Index::mix].id)       == P::kMix,       "Index::mix is mix");
    check (std::string (specs[(size_t) P::Index::throwHeld].id) == P::kThrow,     "Index::throwHeld is throw");
    check (std::string (specs[(size_t) P::Index::fxAmount].id)  == P::kFxAmount,  "Index::fxAmount is fx_amount");
}

/** Choice lists are stored by index, so the order is as permanent as the ids.

    NOTE is the one that is not "least to most": its index is the automation
    lane, so it ascends in duration and all sixteen ship at once. The other
    three run least to most intervention so that index 0 is the neutral value a
    corrupt state lands on. */
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

    // Send open is index 0, so THROW is inert on a fresh instance.
    const char* expectedThrow[] { "Send open", "Throw", "Build" };
    const auto& mode = specs[(size_t) P::Index::throwMode];
    check (mode.numChoices() == 3, "three throw modes");
    for (int i = 0; i < mode.numChoices() && i < 3; ++i)
        check (std::string (mode.choices[(size_t) i]) == expectedThrow[i],
               std::string ("throwMode index ") + std::to_string (i) + " is " + expectedThrow[i]);

    // The candidate list. Its contents may still change before ship; that
    // index 0 is a type and not "Off" may not -- `fx` owns off, and a corrupt
    // state landing on 0 has to give the gentlest type with the stage still
    // gated by a bool that defaults off.
    const auto& fxType = specs[(size_t) P::Index::fxType];
    check (fxType.numChoices() == 7, "seven FX candidates");
    check (fxType.def == 0.0f, "the default FX type is index 0");
    for (int i = 0; i < fxType.numChoices(); ++i)
        check (std::string (fxType.choices[(size_t) i]) != "Off",
               "no FX type is called Off -- fx owns off");
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
    v[P::Index::voice]     = 44.0f;
    v[P::Index::modRate]   = 2.7f;
    v[P::Index::modDepth]  = 29.0f;
    v[P::Index::drive]     = 83.0f;
    v[P::Index::duck]      = 17.0f;
    v[P::Index::mix]       = 72.0f;
    v[P::Index::throwHeld] = 1.0f;
    v[P::Index::throwMode] = 2.0f;
    v[P::Index::freeze]    = 1.0f;
    v[P::Index::fx]        = 1.0f;
    v[P::Index::fxType]    = 5.0f;
    v[P::Index::fxAmount]  = 12.0f;

    dsp.setParams (v.data(), (int) v.size());
    const auto& p = dsp.getCore().getParams();

    check (p.timeMs          == 913.0f,  "TIME reaches the core");
    check (p.noteChoice      == 3,       "NOTE reaches the core");
    check (p.feedbackPct     == 61.0f,   "FEEDBACK reaches the core");
    check (p.characterChoice == 2,       "CHARACTER reaches the core");
    check (p.stereoChoice    == 1,       "STEREO reaches the core");
    check (p.lowCutHz        == 137.0f,  "LOW CUT reaches the core");
    check (p.highCutHz       == 7300.0f, "HIGH CUT reaches the core");
    check (p.voicePct        == 44.0f,   "VOICE reaches the core");
    check (p.modRateHz       == 2.7f,    "MOD RATE reaches the core");
    check (p.modDepthPct     == 29.0f,   "MOD DEPTH reaches the core");
    check (p.drivePct        == 83.0f,   "DRIVE reaches the core");
    check (p.duckDb          == 17.0f,   "DUCK reaches the core");
    check (p.mixPct          == 72.0f,   "MIX reaches the core");
    check (p.throwHeld,                  "THROW reaches the core");
    check (p.throwModeChoice == 2,       "THROW MODE reaches the core");
    check (p.freeze,                     "FREEZE reaches the core");
    check (p.fx,                         "FX reaches the core");
    check (p.fxTypeChoice    == 5,       "FX TYPE reaches the core");
    check (p.fxAmountPct     == 12.0f,   "FX AMOUNT reaches the core");
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
                    v[P::Index::fx]     = on;
                    v[P::Index::freeze] = on;

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
