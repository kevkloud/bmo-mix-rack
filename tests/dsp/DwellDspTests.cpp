/*
    BMO Dwell's DSP, stage 1: the schema and the wiring, before there is any
    delay to measure.

    docs/delay/11-integration-and-test-plan.md §4 lists the suites this file
    grows into -- time accuracy, feedback decay, the mix law, the FX stage.
    None of them can be written yet. What can be written, and is worth writing
    first, is everything that is *permanent*: ids 0-25 in their frozen order,
    the three choice lists in their frozen index order, and the mapping from
    spec index to named value in DwellDsp::setParams.

    **The table settled at twenty-seven on 2026-09-22: two engines, one
    voicing.** The lane runs the main delay's character, stereo, cuts,
    modulation and drive rather than mirroring them, which deleted `link` and
    six `lane_` rows and renumbered everything after them.

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

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <new>
#include <string>
#include <vector>

namespace P = bmo::dwell;

namespace
{

int failures = 0, checks = 0;

/** Armed only around a `setParams` + `process` pair. `11` §4k: both rings come
    from `prepare()` and **nothing** is allocated on the audio thread. */
bool allocationGuardArmed = false;
int  allocationsWhileArmed = 0;

} // namespace

//==============================================================================
// Replacing the global allocator is the only way to assert (k)'s claim from
// inside a JUCE-free test: a delay that allocated its ring, its FX scratch or a
// temporary from process() would sound perfect and still drop out under load,
// which is exactly the class of fault a listening pass cannot catch.
void* operator new (std::size_t size)
{
    if (allocationGuardArmed)
        ++allocationsWhileArmed;

    if (auto* p = std::malloc (size == 0 ? 1 : size))
        return p;

    throw std::bad_alloc();
}

void* operator new[] (std::size_t size)                  { return operator new (size); }
void operator delete (void* p) noexcept                  { std::free (p); }
void operator delete[] (void* p) noexcept                { std::free (p); }
void operator delete (void* p, std::size_t) noexcept     { std::free (p); }
void operator delete[] (void* p, std::size_t) noexcept   { std::free (p); }

namespace
{

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
    // The lane, ids 20-25: what it declares for itself once it runs the main
    // delay's voicing. `link` and the six `lane_` voicing rows that stood here
    // went on 2026-09-22; see modules/dwell/params.h.
    { 20, "lane_level",    -24.0f,    24.0f,     0.0f,  0 },
    { 21, "lane_time",       1.0f,  2000.0f,   250.0f,  0 },
    { 22, "lane_note",      0.0f,    15.0f,     6.0f, 16 },
    { 23, "lane_fx",         0.0f,     1.0f,     0.0f,  0 },
    { 24, "lane_fx_type",    0.0f,     2.0f,     0.0f,  3 },
    { 25, "lane_fx_amount",  0.0f,   100.0f,    35.0f,  0 },
    // Id 26, the last row, and the one bool in this schema that defaults
    // **on**: a stage nobody has asked to differ follows the main delay.
    { 26, "fx_link",         0.0f,     1.0f,     1.0f,  0 },
};

void testSchemaIsWhatItWillAlwaysBe()
{
    const auto& specs = P::specs();

    check (specs.size() == 27, "twenty-seven parameters, ids 0-26");
    check (specs.size() == (size_t) P::Index::count, "the Index enum matches specs()");
    check ((int) P::Index::count == 27, "Index::count is 27");

    // The same shape tests/plugin/DwellTests.cpp asserts, minus the one thing
    // this suite cannot see: it is JUCE-free and does not link the rack, so
    // `kParamsPerSlot` is not in scope here. That assertion lives over there;
    // what is checked here is the count and the last row, which is what would
    // have to move for it to start failing.
    check (std::string (specs[26].id) == P::kFxLink, "fx_link closes the table at id 26");

    if (specs.size() != 26)
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
    check (std::string (specs[(size_t) P::Index::laneLevel].id) == P::kLaneLevel, "Index::laneLevel is lane_level");
    check (std::string (specs[(size_t) P::Index::fxLink].id)    == P::kFxLink,
           "Index::fxLink is fx_link, and it is the last one");
}

/** Choice lists are stored by index, so the order is as permanent as the ids.

    NOTE is the one that is not "least to most": its index is the automation
    lane, so it ascends in duration and all sixteen ship at once. The other
    two run least to most intervention so that index 0 is the neutral value a
    corrupt state lands on.

    The lane reuses the FX list rather than declaring its own, so checking the
    main delay's checks both -- and the assertion at the end is what catches
    them being split into two lists that can drift apart. It has nothing to
    reuse for CHARACTER and STEREO: from 2026-09-22 there is one of each
    parameter and the lane runs it. */
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

    // The lane's FX types come off the main delay's list rather than a list of
    // its own, which is the only way the two cannot drift apart.
    const auto& laneFxType = specs[(size_t) P::Index::laneFxType];

    check (laneFxType.choices == fxType.choices, "the lane's FX types are the main delay's");
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

    v[P::Index::laneLevel]     = -7.5f;
    v[P::Index::laneTime]      = 431.0f;
    v[P::Index::laneFx]        = 1.0f;
    v[P::Index::laneFxType]    = 2.0f;
    v[P::Index::laneFxAmount]  = 88.0f;

    // Set to the opposite of its default, because a bool that defaults on is
    // one a forgotten assignment would still read correctly.
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

    check (p.laneLevelDb         == -7.5f,      "LANE LEVEL reaches the core");
    check (p.laneTimeMs          == 431.0f,     "LANE TIME reaches the core");
    check (p.laneFx,                            "LANE FX reaches the core");
    check (p.laneFxTypeChoice    == 2,          "LANE FX TYPE reaches the core");
    check (p.laneFxAmountPct     == 88.0f,      "LANE FX AMOUNT reaches the core");
    check (! p.fxLink,                          "FX LINK reaches the core");

    // **The one pair left that a copy-paste could cross.** The lane's twelve
    // mirrored rows sat next to the main delay's in this struct until
    // 2026-09-22 and were exactly the shape a duplicated line reads the wrong
    // lane into; five of them are gone and only the two times are still a
    // pair. Set to different numbers above for that reason.
    check (p.timeMs != p.laneTimeMs, "TIME and LANE TIME are not the same lane");
    check (p.fxTypeChoice != p.laneFxTypeChoice, "FX TYPE and LANE FX TYPE are not the same lane");
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

/** At the defaults, with the loop running behind it, the block comes out
    exactly as it went in.

    docs/delay/10 §9 requires the dry path to be **bit-exact** unity for every
    MIX at or below 50 %, and the defaults sit at 35 % with TIME at 375 ms, so
    no repeat reaches a 512-sample block. `testTheDryNullIsBitExact` is the
    same claim walked over the hinge; this one stays because it is the claim as
    a freshly inserted instance meets it. */
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

//==============================================================================
// Stage 2a: the delay engine itself. docs/delay/11 §4 i, k and c.
//==============================================================================

/** A deterministic noise source, so every render in this file is reproducible
    and two renders of "the same input" really are the same input. */
struct Noise
{
    unsigned int state = 22695477u;

    float next() noexcept
    {
        state = state * 1103515245u + 12345u;
        return (float) ((double) (state >> 8) / 8388608.0 - 1.0);
    }
};

/** Two planar channels, and the pointer pair a `ModuleDsp` wants. */
struct Block
{
    explicit Block (int n) : left ((size_t) n, 0.0f), right ((size_t) n, 0.0f) {}

    float* const* channels() noexcept
    {
        ptrs[0] = left.data();
        ptrs[1] = right.data();
        return ptrs;
    }

    std::vector<float> left, right;
    float* ptrs[2] {};
};

/** Renders `block` through `dsp` in chunks of `chunk`, in place. */
void renderInChunks (P::DwellDsp& dsp, Block& block, int numSamples, int chunk)
{
    for (int offset = 0; offset < numSamples; )
    {
        const auto count = std::min (chunk, numSamples - offset);
        float* channels[] { block.left.data() + offset, block.right.data() + offset };
        dsp.process (channels, 2, count);
        offset += count;
    }
}

double rms (const std::vector<float>& v, int from, int count)
{
    auto sum = 0.0;

    for (int i = 0; i < count; ++i)
    {
        const auto x = (double) v[(size_t) (from + i)];
        sum += x * x;
    }

    return std::sqrt (sum / std::max (count, 1));
}

/** **The dry null is bit-exact, not small** (docs/delay/10 §9, `11` §4i).

    At or below 50 % MIX the dry is not multiplied at all, so with TIME set
    beyond the render no repeat arrives and the output must equal the input
    sample for sample. The assertion is a worst-case difference of exactly
    0.0 -- a -120 dB figure would pass a build that multiplied by 1.0f and
    hid a rounding error, which is the build this rule exists to forbid. */
void testTheDryNullIsBitExact()
{
    for (const auto mix : { 0.0f, 25.0f, 50.0f })
    {
        for (const auto feedback : { 0.0f, 35.0f, 97.0f })
        {
            P::DwellDsp dsp;
            dsp.prepare (48000.0, 512, 2);

            auto v = defaults();
            v[P::Index::mix]       = mix;
            v[P::Index::feedback]  = feedback;
            v[P::Index::time]      = 1000.0f;   // 48 000 samples: no repeat inside
            v[P::Index::laneLevel] = 24.0f;     // the lane is summed at full travel
            dsp.setParams (v.data(), (int) v.size());

            constexpr int n = 4096;
            Block block { n };
            Noise noise;

            for (int i = 0; i < n; ++i)
            {
                block.left[(size_t) i]  = 0.6f * noise.next();
                block.right[(size_t) i] = 0.6f * noise.next();
            }

            const auto inLeft = block.left, inRight = block.right;

            renderInChunks (dsp, block, n, 512);

            auto worst = 0.0f;

            for (int i = 0; i < n; ++i)
            {
                worst = std::max (worst, std::abs (block.left[(size_t) i]  - inLeft[(size_t) i]));
                worst = std::max (worst, std::abs (block.right[(size_t) i] - inRight[(size_t) i]));
            }

            check (worst == 0.0f,
                   "the dry null is exactly 0.0 at MIX " + std::to_string ((int) mix)
                       + " %, FEEDBACK " + std::to_string ((int) feedback) + " %");
        }
    }
}

/** **`P_c` is swept, not written down** (docs/delay/10 §3).

    Clean's reference chain is the 20 Hz LOW CUT rail, the 18 kHz HIGH CUT cap
    and the 10 Hz blocker, with a sinc that is a pure delay at whole samples,
    so the peak lands just under unity and in the low hundreds of Hz. Both
    figures are asserted: a build that returned a constant would pass the first
    and fail the second, and a build that swept the wrong grid would fail
    both. Every engine sweeps its own -- the lane is checked with the main.

    **The figure moves with the sample rate, and that is the point.** Measured
    on AURORA: 0.99959 at 44.1 kHz down to 0.99880 at 192 kHz, because the
    18 kHz cap prewarps to a very different shape when it sits at 0.41 f_s
    rather than at 0.09 f_s, and the low-band droop it leaves behind moves with
    it. 10 §3's "expected clean 0.999" covers the span. A single hardcoded
    constant would be wrong at five of these six rates -- by enough that
    `testUnityLandsAtNinetySevenPercent` fails, since 0.0006 of error in `P_c`
    is a dB of drift over that test's 190 laps. */
void testTheReferenceLoopPeakIsSwept()
{
    for (const auto rate : { 44100.0, 48000.0, 88200.0, 96000.0, 176400.0, 192000.0 })
    {
        P::DspCore core;
        core.prepare (rate, 512, 2);

        auto p = P::DspCore::Params {};
        core.setParams (p);

        const auto pc = core.getMainEngine().referenceLoopPeak();
        const auto hz = core.getMainEngine().referencePeakHz();
        const auto lanePc = core.getLaneEngine().referenceLoopPeak();

        check (std::abs (pc - 0.9992) < 0.0012,
               "clean's P_c is just under unity at " + std::to_string ((int) rate) + " Hz (swept "
                   + std::to_string (pc) + ")");

        check (hz > 300.0 && hz < 1200.0,
               "clean's loop peak sits in the low band at " + std::to_string ((int) rate)
                   + " Hz (" + std::to_string ((int) hz) + " Hz)");

        check (std::abs (pc - lanePc) < 0.0015,
               "both engines sweep their own P_c to the same clean figure at "
                   + std::to_string ((int) rate) + " Hz");
    }
}

/** **Unity lands at FEEDBACK 97.0 %** (docs/delay/10 §3, `11` §4c, §4e6).

    §3's law puts the loop's peak magnitude at `1.05 . 0.97^1.6 = 1.0001`, and
    unity is a claim about **the loudest band**, not about every band -- so the
    tone is placed at the frequency the engine's own sweep found the peak at,
    which is the only frequency the claim is about. A burst is used rather than
    a sustained input because at unity a sustained input accumulates without
    bound and would reach the safety clip, which would be measuring the clip
    instead.

    **Tolerance 0.3 dB over 190 laps, and it is a sharp assertion rather than a
    generous one.** Measured on AURORA: **-0.05 dB**, the law's own +0.09 dB
    from 1.0001 less the burst's off-peak shoulders, which decay at their own
    slightly lower per-lap gain. What the tolerance rejects is a `P_c` that is
    wrong by more than about 0.0002 -- so the 0.0006 between 48 kHz's swept
    figure and 192 kHz's, i.e. exactly the error a single hardcoded constant
    would make, lands a decibel outside it. */
void testUnityLandsAtNinetySevenPercent()
{
    constexpr auto rate = 48000.0;
    constexpr auto timeMs = 100.0f;
    const auto lap = (int) (rate * 0.001 * (double) timeMs);   // 4800, a whole number

    P::DwellDsp dsp;
    dsp.prepare (rate, 512, 2);

    auto v = defaults();
    v[P::Index::time]     = timeMs;
    v[P::Index::feedback] = 97.0f;
    v[P::Index::mix]      = 100.0f;   // wet only, so the dry never lands in a window
    dsp.setParams (v.data(), (int) v.size());

    const auto fPeak = dsp.getCore().getMainEngine().referencePeakHz();

    constexpr int laps = 200;
    const auto n = lap * (laps + 2);
    Block block { n };

    // A Hann-windowed burst, half a lap long, at 0.002: small enough that the
    // safety clip is linear to a part in a million.
    const auto burst = lap / 2;

    for (int i = 0; i < burst; ++i)
    {
        const auto w = 0.5 - 0.5 * std::cos (2.0 * P::kPiD * (double) i / (double) burst);
        const auto s = (float) (0.002 * w * std::sin (2.0 * P::kPiD * fPeak * (double) i / rate));
        block.left[(size_t) i]  = s;
        block.right[(size_t) i] = s;
    }

    renderInChunks (dsp, block, n, 512);

    const auto early = rms (block.left, 10 * lap, lap);
    const auto late  = rms (block.left, 200 * lap, lap);

    check (early > 1.0e-6, "the loop is still ringing ten laps in");

    const auto drift = 20.0 * std::log10 (std::max (late, 1.0e-30) / std::max (early, 1.0e-30));

    check (std::abs (drift) <= 0.3,
           "at FEEDBACK 97 % the loudest band neither grows nor decays: "
               + std::to_string (drift) + " dB over 190 laps");

    // The other half of the claim: below 97 % it decays and above it the clip
    // -- not the arithmetic -- is what keeps it bounded.
    {
        P::DwellDsp quiet;
        quiet.prepare (rate, 512, 2);
        auto q = v;
        q[P::Index::feedback] = 80.0f;
        quiet.setParams (q.data(), (int) q.size());

        Block b { n };

        for (int i = 0; i < burst; ++i)
        {
            const auto w = 0.5 - 0.5 * std::cos (2.0 * P::kPiD * (double) i / (double) burst);
            const auto s = (float) (0.002 * w * std::sin (2.0 * P::kPiD * fPeak * (double) i / rate));
            b.left[(size_t) i]  = s;
            b.right[(size_t) i] = s;
        }

        renderInChunks (quiet, b, n, 512);

        const auto decayEarly = rms (b.left, 10 * lap, lap);
        const auto decayLate  = rms (b.left, 60 * lap, lap);

        check (decayLate < decayEarly * 0.001,
               "at FEEDBACK 80 % the tail is well down fifty laps later");
    }

    {
        P::DwellDsp loud;
        loud.prepare (rate, 512, 2);
        auto l = v;
        l[P::Index::feedback] = 100.0f;
        loud.setParams (l.data(), (int) l.size());

        const auto m = lap * 400;
        Block b { m };
        Noise noise;
        for (int i = 0; i < lap; ++i)
            b.left[(size_t) i] = b.right[(size_t) i] = 0.5f * noise.next();

        renderInChunks (loud, b, m, 512);

        auto peak = 0.0f;
        auto finite = true;

        for (int i = m - 4 * lap; i < m; ++i)
        {
            peak = std::max (peak, std::abs (b.left[(size_t) i]));
            finite = finite && std::isfinite (b.left[(size_t) i]) && std::isfinite (b.right[(size_t) i]);
        }

        check (finite && peak < 2.0f,
               "at FEEDBACK 100 % the safety clip bounds the loop rather than it diverging (peak "
                   + std::to_string (peak) + ")");
    }
}

/** **Sample-rate invariance** (`11` §4k).

    Every coefficient in the loop depends only on `f_c / f_s` and the ring is
    sized from a time, so the same settings must give the same delay in
    *seconds* and the same decay per lap at every rate the suite supports. The
    reference is 48 kHz; the tolerance on the echo's arrival is 0.05 ms. */
void testSampleRateInvariance()
{
    struct Measured { double echoSeconds = 0.0, lapDb = 0.0, pc = 0.0; };

    const auto measure = [] (double rate)
    {
        P::DwellDsp dsp;
        dsp.prepare (rate, 512, 2);

        auto v = defaults();
        v[P::Index::time]     = 100.0f;
        v[P::Index::feedback] = 60.0f;
        v[P::Index::mix]      = 100.0f;
        dsp.setParams (v.data(), (int) v.size());

        const auto n = (int) (rate * 0.5);
        Block block { n };

        const auto burst = (int) (rate * 0.02);

        for (int i = 0; i < burst; ++i)
        {
            const auto w = 0.5 - 0.5 * std::cos (2.0 * P::kPiD * (double) i / (double) burst);
            const auto s = (float) (0.25 * w * std::sin (2.0 * P::kPiD * 1000.0 * (double) i / rate));
            block.left[(size_t) i]  = s;
            block.right[(size_t) i] = s;
        }

        renderInChunks (dsp, block, n, 512);

        auto bestIndex = 0;
        auto best = 0.0f;

        for (int i = (int) (rate * 0.05); i < (int) (rate * 0.15); ++i)
        {
            if (std::abs (block.left[(size_t) i]) > best)
            {
                best = std::abs (block.left[(size_t) i]);
                bestIndex = i;
            }
        }

        const auto window = (int) (rate * 0.03);
        const auto first  = rms (block.left, (int) (rate * 0.10), window);
        const auto second = rms (block.left, (int) (rate * 0.20), window);

        Measured m;
        m.echoSeconds = (double) bestIndex / rate;
        m.lapDb = 20.0 * std::log10 (std::max (second, 1.0e-30) / std::max (first, 1.0e-30));
        m.pc = dsp.getCore().getMainEngine().referenceLoopPeak();
        return m;
    };

    const auto reference = measure (48000.0);

    for (const auto rate : { 44100.0, 48000.0, 88200.0, 96000.0, 176400.0, 192000.0 })
    {
        const auto m = measure (rate);
        const auto name = std::to_string ((int) rate) + " Hz";

        check (std::abs (m.echoSeconds - reference.echoSeconds) < 5.0e-5,
               "the echo arrives at the same time at " + name);

        check (std::abs (m.lapDb - reference.lapDb) < 0.1,
               "the decay per lap matches 48 kHz within 0.1 dB at " + name);

        check (std::abs (m.pc - reference.pc) < 0.002,
               "P_c matches 48 kHz at " + name);
    }
}

/** **Block-size invariance** (`11` §4k): 1, 32, 64, 512, 1023 and a random
    schedule must give the same audio as one long call, to -120 dB. A delay
    whose state advanced per block rather than per sample passes every other
    test in this file and fails this one. */
void testBlockSizeInvariance()
{
    constexpr auto rate = 48000.0;
    constexpr int n = 48000;

    const auto render = [] (int chunk, bool randomise)
    {
        P::DwellDsp dsp;
        dsp.prepare (rate, 2048, 2);

        auto v = defaults();
        v[P::Index::time]     = 37.0f;
        v[P::Index::feedback] = 70.0f;
        v[P::Index::mix]      = 50.0f;
        dsp.setParams (v.data(), (int) v.size());

        Block block { n };
        Noise noise;

        for (int i = 0; i < n; ++i)
        {
            block.left[(size_t) i]  = 0.3f * noise.next();
            block.right[(size_t) i] = 0.3f * noise.next();
        }

        if (! randomise)
        {
            renderInChunks (dsp, block, n, chunk);
        }
        else
        {
            Noise sizes;

            for (int offset = 0; offset < n; )
            {
                const auto want = 1 + (int) (std::abs ((double) sizes.next()) * 700.0);
                const auto count = std::min (want, n - offset);
                float* channels[] { block.left.data() + offset, block.right.data() + offset };
                dsp.process (channels, 2, count);
                offset += count;
            }
        }

        return block.left;
    };

    const auto reference = render (n, false);

    for (const auto chunk : { 1, 32, 64, 512, 1023 })
    {
        const auto got = render (chunk, false);
        auto worst = 0.0f;

        for (int i = 0; i < n; ++i)
            worst = std::max (worst, std::abs (got[(size_t) i] - reference[(size_t) i]));

        check (worst < 1.0e-6f,
               "blocks of " + std::to_string (chunk) + " give the same audio as one call");
    }

    const auto got = render (0, true);
    auto worst = 0.0f;

    for (int i = 0; i < n; ++i)
        worst = std::max (worst, std::abs (got[(size_t) i] - reference[(size_t) i]));

    check (worst < 1.0e-6f, "a random block schedule gives the same audio as one call");
}

/** **Silence, denormals and NaN** (`11` §4k).

    Silence in must give **exact zeros** out, not a decaying tail of
    subnormals: a one-pole and a feedback ring both approach zero without
    reaching it, and on x86 a single subnormal operand costs a hundred cycles,
    so "quiet" and "zero" are a CPU spike apart. And nothing non-finite may
    ever leave, whatever arrives -- a NaN that reached a feedback ring would
    circulate for the life of the instance. */
void testSilenceDenormalsAndNaN()
{
    constexpr auto rate = 48000.0;

    {
        P::DwellDsp dsp;
        dsp.prepare (rate, 512, 2);

        auto v = defaults();
        v[P::Index::time]     = 20.0f;
        v[P::Index::feedback] = 50.0f;
        v[P::Index::mix]      = 100.0f;
        dsp.setParams (v.data(), (int) v.size());

        const auto n = (int) (rate * 4.0);
        Block block { n };
        Noise noise;

        for (int i = 0; i < (int) rate / 2; ++i)
        {
            block.left[(size_t) i]  = 0.5f * noise.next();
            block.right[(size_t) i] = 0.5f * noise.next();
        }

        renderInChunks (dsp, block, n, 512);

        auto zeroed = true;

        for (int i = n - 4096; i < n; ++i)
            zeroed = zeroed && block.left[(size_t) i] == 0.0f && block.right[(size_t) i] == 0.0f;

        check (zeroed, "silence in gives exact zeros out once the tail has run down");
    }

    {
        P::DwellDsp dsp;
        dsp.prepare (rate, 512, 2);

        auto v = defaults();
        v[P::Index::time]     = 30.0f;
        v[P::Index::feedback] = 90.0f;
        v[P::Index::mix]      = 100.0f;
        dsp.setParams (v.data(), (int) v.size());

        const auto n = (int) (rate * 2.0);
        Block block { n };
        Noise noise;

        for (int i = 0; i < n; ++i)
        {
            block.left[(size_t) i]  = 0.4f * noise.next();
            block.right[(size_t) i] = 0.4f * noise.next();
        }

        // A NaN, an infinity and a denormal, on and off a block boundary.
        block.left[100]   = std::numeric_limits<float>::quiet_NaN();
        block.right[512]  = std::numeric_limits<float>::infinity();
        block.left[1023]  = -std::numeric_limits<float>::infinity();
        block.right[4097] = 1.0e-42f;

        renderInChunks (dsp, block, n, 512);

        auto finite = true;

        for (int i = 0; i < n; ++i)
            finite = finite && std::isfinite (block.left[(size_t) i])
                            && std::isfinite (block.right[(size_t) i]);

        check (finite, "nothing non-finite ever leaves, whatever arrives");

        check (rms (block.left, n - 8192, 8192) > 1.0e-5,
               "the loop is still working after a NaN rather than stuck at zero");
    }
}

/** **Nothing is allocated on the audio thread** (`11` §4k).

    Both rings, both sweep grids and the wet scratch come from `prepare()`.
    The guard is armed around a `setParams` + `process` pair rather than
    `process` alone because `setParams` runs on the audio thread too
    (`ModuleEngine::process` calls it once a block), and it is where §3's
    re-sweep happens on a TIME move. */
void testProcessAllocatesNothing()
{
    P::DwellDsp dsp;
    dsp.prepare (48000.0, 512, 2);

    auto v = defaults();
    dsp.setParams (v.data(), (int) v.size());

    Block block { 512 };
    Noise noise;

    for (int i = 0; i < 512; ++i)
    {
        block.left[(size_t) i]  = 0.3f * noise.next();
        block.right[(size_t) i] = 0.3f * noise.next();
    }

    // The guard has to be shown to work before it is trusted: a replaced
    // operator new that the linker quietly ignored would make this test a
    // permanent, silent pass.
    allocationsWhileArmed = 0;
    allocationGuardArmed = true;
    {
        std::vector<float> deliberate ((size_t) 4096, 0.0f);
        check (allocationsWhileArmed > 0 && deliberate.size() == 4096,
               "the allocation guard counts a deliberate allocation");
    }

    allocationsWhileArmed = 0;

    for (int pass = 0; pass < 8; ++pass)
    {
        // A TIME move, a CHARACTER move and a MIX move across the hinge --
        // every path that re-sweeps or re-smooths, none of which may allocate.
        v[P::Index::time]      = 20.0f + 200.0f * (float) pass;
        v[P::Index::laneTime]  = 500.0f - 40.0f * (float) pass;
        v[P::Index::character] = (float) (pass % 3);
        v[P::Index::mix]       = 10.0f * (float) pass;
        v[P::Index::feedback]  = 12.5f * (float) pass;
        dsp.setParams (v.data(), (int) v.size());
        dsp.process (block.channels(), 2, 512);
    }

    allocationGuardArmed = false;

    // Those eight passes also walked TIME and LANE TIME across §2's crossfade
    // on every block boundary, which is the one path here that runs two taps
    // at once. Nothing non-finite may come out of it.
    {
        auto finite = true;

        for (int i = 0; i < 512; ++i)
            finite = finite && std::isfinite (block.left[(size_t) i])
                            && std::isfinite (block.right[(size_t) i]);

        check (finite, "a TIME move mid-block leaves the output finite");
    }

    check (allocationsWhileArmed == 0,
           "process() and setParams() allocate nothing ("
               + std::to_string (allocationsWhileArmed) + " allocations)");
}

//==============================================================================
// Stage 2b: the three characters, the in-loop tone controls, the shaper and
// the modulation. docs/delay/11 §4 b, c, d, f and k, per character.
//==============================================================================

const char* characterName (int c)
{
    return c == 0 ? "clean" : (c == 1 ? "tape" : "bucket-brigade");
}

/** A Hann-windowed DFT at one frequency, normalised so that a pure sine of
    amplitude A reads A.

    Broadband RMS cannot carry the unity assertion below, because unity is a
    claim about **the loudest band** and nothing else (docs/delay/10 §3): on
    tape and bucket-brigade every other band is decaying by design, so an RMS
    window would measure the colour draining out of a burst rather than
    whether the peak held. */
double magnitudeAt (const std::vector<float>& v, int from, int count, double freqHz, double rate)
{
    auto re = 0.0, im = 0.0, weight = 0.0;

    for (int i = 0; i < count; ++i)
    {
        const auto w = 0.5 - 0.5 * std::cos (2.0 * P::kPiD * (double) i / (double) count);
        const auto x = (double) v[(size_t) (from + i)] * w;
        const auto phase = 2.0 * P::kPiD * freqHz * (double) i / rate;

        re += x * std::cos (phase);
        im -= x * std::sin (phase);
        weight += w;
    }

    return 2.0 * std::sqrt (re * re + im * im) / std::max (weight, 1.0);
}

/** A DC meter: three cascaded 1 Hz one-poles, read over the last second.

    A plain mean cannot make this measurement. Under broadband excitation the
    mean of N samples of the programme itself is around `rms / sqrt(N)`, which
    at these levels is a thousand times the -80 dBFS the assertion is about --
    so a mean would be measuring the noise and calling it offset. Three poles
    put a 33 Hz component (the lowest comb tooth in these renders) 90 dB down
    while passing DC at unity, which is what makes the number mean what it
    says. */
double dcLevel (const std::vector<float>& v, double rate)
{
    P::TptOnePole a, b, c;
    a.setCutoff (1.0, rate);
    b.setCutoff (1.0, rate);
    c.setCutoff (1.0, rate);

    const auto n = (int) v.size();
    const auto from = std::max (0, n - (int) rate);
    auto worst = 0.0;

    for (int i = 0; i < n; ++i)
    {
        const auto y = c.lowPass (b.lowPass (a.lowPass ((double) v[(size_t) i])));

        if (i >= from)
            worst = std::max (worst, std::abs (y));
    }

    return worst;
}

/** The defaults with `time`, `feedback`, `mix` and `character` set. */
std::vector<float> settings (int character, float timeMs, float feedback, float mix)
{
    auto v = defaults();
    v[P::Index::time]      = timeMs;
    v[P::Index::feedback]  = feedback;
    v[P::Index::mix]       = mix;
    v[P::Index::character] = (float) character;
    return v;
}

/** **`P_c` is swept per character, and each one lands where 10 §3 expects.**

    Clean's reference chain is the 20 Hz LOW CUT rail, the 18 kHz HIGH CUT cap
    and the 10 Hz blocker with a sinc that is a pure delay at whole samples, so
    its peak sits just under unity. **Tape's is above it**, deliberately: the
    +2 dB shelf at 55 Hz is the only in-loop stage whose magnitude exceeds 1,
    its asymptote is cut away by the blocker and the 20 Hz rail, and what
    survives is the +0.45 dB near 63 Hz that §3's normalisation divides out.
    Bucket-brigade's Butterworth pair is monotonic with |H| <= 1, so its peak
    is just under unity and **moves with TIME**, because its corner comes from
    a clock that does.

    All three are asserted against the spec's own expected figures rather than
    against whatever this build happens to produce, which is the only way this
    test can fail usefully. */
void testTheLoopPeakIsSweptPerCharacter()
{
    for (const auto rate : { 44100.0, 48000.0, 96000.0, 192000.0 })
    {
        for (int c = 0; c < 3; ++c)
        {
            P::DwellDsp dsp;
            dsp.prepare (rate, 512, 2);

            auto v = settings (c, 375.0f, 35.0f, 35.0f);
            dsp.setParams (v.data(), (int) v.size());

            const auto& engine = dsp.getCore().getMainEngine();
            const auto pc = engine.referenceLoopPeak();
            const auto hz = engine.referencePeakHz();

            const auto where = std::string (characterName (c)) + " at "
                             + std::to_string ((int) rate) + " Hz (P_c "
                             + std::to_string (pc) + " at " + std::to_string ((int) hz) + " Hz)";

            if (rate == 48000.0)
                std::printf ("      P_c %-15s at 48 kHz: %.5f, peak %7.2f Hz\n",
                             characterName (c), pc, hz);

            if (c == P::kClean)
            {
                check (std::abs (pc - 0.9992) < 0.0012, "clean's P_c is just under unity, " + where);
                check (hz > 300.0 && hz < 1200.0, "clean's peak sits in the low band, " + where);
            }
            else if (c == P::kTape)
            {
                // 10 §4's figure for the pole convention, which is the one
                // `kHeadBumpHz` takes: 1.054 at 63 Hz.
                check (std::abs (pc - 1.054) < 0.006, "tape's P_c is the head bump, " + where);
                check (hz > 45.0 && hz < 90.0, "tape's peak sits at the head bump, " + where);
            }
            else
            {
                check (pc > 0.985 && pc < 0.9995, "bucket-brigade's P_c is just under unity, " + where);
                check (hz > 80.0 && hz < 900.0, "bucket-brigade's peak sits below its clock, " + where);
            }
        }
    }

    // And it moves with TIME on bucket-brigade alone, because only its mode
    // filters are derived from one (10 §4's `f_clk = N / 2T`).
    P::DwellDsp dsp;
    dsp.prepare (48000.0, 512, 2);

    const auto peakAt = [&dsp] (int character, float timeMs)
    {
        auto v = settings (character, timeMs, 35.0f, 35.0f);
        dsp.setParams (v.data(), (int) v.size());
        return dsp.getCore().getMainEngine().referenceLoopPeak();
    };

    check (std::abs (peakAt (P::kTape, 120.0f) - peakAt (P::kTape, 1200.0f)) < 1.0e-6,
           "tape's P_c does not move with TIME");

    check (std::abs (peakAt (P::kBucketBrigade, 120.0f) - peakAt (P::kBucketBrigade, 1200.0f)) > 1.0e-4,
           "bucket-brigade's P_c moves with TIME, because its clock does");
}

/** **Bucket-brigade's filters are a clock, not a curve** (10 §4).

    `f_clk = N / 2T` with N = 4096 and `f_c = 0.6 f_clk / 2`, clamped to
    [800 Hz, 16 kHz]. 01's datasheet pair is the 205 ms row: f_clk near 10 kHz,
    corner near 3 kHz. Clean and tape have no clock and report none. */
void testTheBucketBrigadeClockFollowsTime()
{
    P::DwellDsp dsp;
    dsp.prepare (48000.0, 512, 2);

    const auto corner = [&dsp] (int character, float timeMs)
    {
        auto v = settings (character, timeMs, 35.0f, 35.0f);
        dsp.setParams (v.data(), (int) v.size());
        return dsp.getCore().getMainEngine().modeCutoffHz();
    };

    const auto datasheet = corner (P::kBucketBrigade, 205.0f);

    check (std::abs (datasheet - 2997.0) < 5.0,
           "at 205 ms the corner is 01's 3 kHz (" + std::to_string (datasheet) + " Hz)");

    check (corner (P::kBucketBrigade, 1.0f) == 16000.0, "the corner clamps at 16 kHz on the short end");
    check (corner (P::kBucketBrigade, 2000.0f) == 800.0, "the corner clamps at 800 Hz on the long end");

    check (corner (P::kBucketBrigade, 500.0f) < corner (P::kBucketBrigade, 200.0f),
           "the corner falls as TIME lengthens, which is the darkening");

    check (corner (P::kClean, 205.0f) == 0.0 && corner (P::kTape, 205.0f) == 0.0,
           "clean and tape have no clock to report");
}

/** **THE HEADLINE: unity lands at FEEDBACK 97.0 % on all three characters.**

    §3's law puts the loop's peak magnitude at `1.05 . 0.97^1.6 = 1.0001` once
    the character's own `P_c` has been divided out, and unity is a claim about
    **the loudest band**: a non-flat loop cannot hold every band and stay
    bounded. So the tone is placed at the frequency the engine's own sweep
    found its peak at -- 63 Hz on tape, a few hundred on clean and
    bucket-brigade -- and measured there rather than by RMS.

    This is the test the whole `P_c` normalisation exists for. Without it tape
    reaches unity nearer 84 %, and the lane's centre detent -- which the panel
    will label as a hold -- would not hold. A build that hardcoded one constant
    for all three fails this twice over.

    A burst rather than a sustained input, because at unity a sustained input
    accumulates without bound and would reach the safety clip, which would be
    measuring the clip instead.

    **TIME is nudged by a sample or two per character, and that is a property
    of the measurement rather than of the delay.** A feedback ring is a comb:
    the only frequencies it can sustain are multiples of `f_s / D`, so a tone
    placed at the swept peak but *between* two teeth decays at the gain of the
    nearer tooth instead of at the peak's. On tape the nearest tooth to 63.4 Hz
    at a round 100 ms sits 3.4 Hz low, which is -0.003 dB a lap -- half a
    decibel over this render, and nothing at all to do with `P_c`. So `D` is
    chosen so that a tooth lands on the peak, after which the two agree to a
    hundredth of a decibel. */
void testUnityLandsAtNinetySevenPercentOnEveryCharacter()
{
    constexpr auto rate = 48000.0;
    constexpr int laps = 210;

    for (int c = 0; c < 3; ++c)
    {
        // Find a lap length whose comb has a tooth on this character's peak.
        // Bucket-brigade's peak moves with TIME, so the two are solved
        // together -- three passes is ample, and the residual is asserted.
        auto lap = (int) std::lround (rate * 0.1);
        auto tooth = 0.0;

        for (int pass = 0; pass < 3; ++pass)
        {
            P::DwellDsp probe;
            probe.prepare (rate, 512, 2);

            auto p = settings (c, (float) ((double) lap * 1000.0 / rate), 97.0f, 100.0f);
            probe.setParams (p.data(), (int) p.size());

            const auto fPeak = probe.getCore().getMainEngine().referencePeakHz();
            const auto k = std::max (1.0, std::round (fPeak * (double) lap / rate));

            lap = (int) std::lround (k * rate / fPeak);
            tooth = k * rate / (double) lap;
        }

        P::DwellDsp dsp;
        dsp.prepare (rate, 512, 2);

        auto v = settings (c, (float) ((double) lap * 1000.0 / rate), 97.0f, 100.0f);   // wet only
        dsp.setParams (v.data(), (int) v.size());

        const auto fPeak = dsp.getCore().getMainEngine().referencePeakHz();

        check (std::abs (tooth - fPeak) < 0.01 * fPeak,
               std::string ("a comb tooth lands on ") + characterName (c) + "'s swept peak ("
                   + std::to_string (tooth) + " Hz against " + std::to_string (fPeak) + " Hz)");

        const auto n = lap * laps;
        Block block { n };

        // One lap of Hann-windowed tone on that tooth, at 0.002: small enough
        // that the safety clip is linear to a part in a million.
        for (int i = 0; i < lap; ++i)
        {
            const auto w = 0.5 - 0.5 * std::cos (2.0 * P::kPiD * (double) i / (double) lap);
            const auto s = (float) (0.002 * w * std::sin (2.0 * P::kPiD * tooth * (double) i / rate));
            block.left[(size_t) i]  = s;
            block.right[(size_t) i] = s;
        }

        renderInChunks (dsp, block, n, 512);

        const auto window = 8 * lap;
        const auto early = magnitudeAt (block.left,  12 * lap, window, tooth, rate);
        const auto late  = magnitudeAt (block.left, 198 * lap, window, tooth, rate);

        check (early > 1.0e-6,
               std::string ("the loop is still ringing twelve laps in on ") + characterName (c));

        const auto drift = 20.0 * std::log10 (std::max (late, 1.0e-30) / std::max (early, 1.0e-30));

        std::printf ("      unity at FEEDBACK 97 %%, %-15s %7.2f Hz, D = %d: %+.3f dB over 186 laps\n",
                     characterName (c), tooth, lap, drift);

        check (std::abs (drift) <= 0.3,
               std::string ("at FEEDBACK 97 % the loudest band neither grows nor decays on ")
                   + characterName (c) + ": " + std::to_string (drift) + " dB over 186 laps");
    }
}

/** **The compander is unity through a transient, and it discriminates.**

    10 §4 makes the delayed-gain construction a stability requirement: the
    compressor's gain is written to a control ring beside the audio and read at
    the same fractional position, and the expander applies its exact
    reciprocal, so the pair is unity at every instant including through a
    transient. It also quotes 0.184 dB per dB of envelope step for the
    re-detecting alternative -- **modelled, not measured**, and flagged in the
    spec as needing a bench before it is quoted.

    This is that bench. Two assertions and one measurement:

    1. Through the engine at FEEDBACK 0 the wet output is one delayed copy of
       the input and the compander is the only thing between them, so a 20 dB
       envelope step must come back out unchanged -- not close, unchanged.
    2. In the loop at FEEDBACK 90, a transient's per-lap peak must not grow.
    3. A re-detecting pair built from **the same detector class at the same
       5 / 50 ms ballistics** runs beside it and its net gain is reported. The
       spec's figure is confirmed or refuted by that number, not by this
       comment. */
void testTheCompanderIsUnityThroughATransient()
{
    constexpr auto rate = 48000.0;

    // 1. The delayed-gain pair, straight through.
    {
        P::DwellDsp dsp;
        dsp.prepare (rate, 512, 2);

        auto v = settings (P::kBucketBrigade, 50.0f, 0.0f, 100.0f);
        dsp.setParams (v.data(), (int) v.size());

        const auto delay = (int) (rate * 0.05);      // 2400, a whole number
        const auto n = (int) (rate * 1.0);
        Block block { n };

        // |x| is constant within each half, so what the detector sees is a
        // clean 20 dB envelope step and not a waveform ripple.
        for (int i = 0; i < n; ++i)
        {
            const auto a = i < n / 2 ? 0.01f : 0.1f;
            const auto s = (i & 1) ? a : -a;
            block.left[(size_t) i]  = s;
            block.right[(size_t) i] = s;
        }

        const auto in = block.left;
        renderInChunks (dsp, block, n, 512);

        auto worst = 0.0;

        for (int i = delay + 1; i < n; ++i)
            worst = std::max (worst, std::abs ((double) block.left[(size_t) i]
                                               - (double) in[(size_t) (i - delay)]));

        check (worst < 1.0e-6,
               "the compander returns a 20 dB step unchanged: worst error "
                   + std::to_string (worst));
    }

    // 2. The same transient in the loop: no gain per lap.
    {
        P::DwellDsp dsp;
        dsp.prepare (rate, 512, 2);

        auto v = settings (P::kBucketBrigade, 100.0f, 90.0f, 100.0f);
        dsp.setParams (v.data(), (int) v.size());

        const auto lap = (int) (rate * 0.1);
        const auto n = lap * 24;
        Block block { n };

        // A quiet bed with a 20 dB step in it: the envelope jump is what a
        // re-detecting pair would overshoot on, once per lap, for ever.
        for (int i = 0; i < lap; ++i)
        {
            const auto a = i < lap / 2 ? 0.02 : 0.2;
            const auto s = (float) (a * std::sin (2.0 * P::kPiD * 700.0 * (double) i / rate));
            block.left[(size_t) i]  = s;
            block.right[(size_t) i] = s;
        }

        renderInChunks (dsp, block, n, 512);

        auto previous = 0.0f;
        auto grew = false;

        for (int k = 2; k < 22; ++k)
        {
            auto peak = 0.0f;

            for (int i = k * lap; i < (k + 1) * lap; ++i)
                peak = std::max (peak, std::abs (block.left[(size_t) i]));

            if (k > 2 && peak > previous * 1.001f)
                grew = true;

            previous = peak;
        }

        check (! grew, "a transient's per-lap peak never grows with the delayed-gain compander");
    }

    // 3. The bench: what a re-detecting pair actually does.
    {
        P::LevelDetectorDb compressor, expander;
        compressor.prepare (rate, P::DelayEngine::kCompanderAttackMs, P::DelayEngine::kCompanderReleaseMs);
        expander  .prepare (rate, P::DelayEngine::kCompanderAttackMs, P::DelayEngine::kCompanderReleaseMs);

        const auto n = (int) (rate * 1.0);
        const auto stepAt = n / 2;

        auto overshootDb = 0.0;
        auto settledDb = 0.0;

        for (int i = 0; i < n; ++i)
        {
            const auto a = i < stepAt ? 0.01 : 0.1;          // a 20 dB step

            const auto e = compressor.tick (P::LevelDetectorDb::levelDb (a));
            const auto compressorGain = P::companderGainFor (e);

            // The re-detecting half: a second detector of the same ballistics
            // on the compressed signal, expanding 2:1. In steady state it is
            // the compressor's exact inverse; through the step it is not.
            const auto f = expander.tick (P::LevelDetectorDb::levelDb (a * compressorGain));
            const auto expanderGain = std::pow (10.0, f / 20.0);

            const auto netDb = 20.0 * std::log10 (compressorGain * expanderGain);

            if (i == stepAt - 1)
                settledDb = netDb;

            if (i >= stepAt)
                overshootDb = std::max (overshootDb, netDb);
        }

        std::printf ("      re-detecting compander: %+.2f dB on a 20 dB step (%.3f dB per dB)\n",
                     overshootDb, overshootDb / 20.0);

        check (std::abs (settledDb) < 0.01,
               "the re-detecting pair is unity in steady state, which is why the fault hides");

        check (overshootDb > 3.0,
               "a re-detecting pair overshoots by more than 3 dB on a 20 dB step ("
                   + std::to_string (overshootDb)
                   + " dB), so the assertion above discriminates rather than passing on anything");
    }
}

/** **The alias floor: <= -60 dBFS after 10 repeats at maximum DRIVE**
    (10 §4's acceptance, `11` §4f), measured FX-off.

    A 9 kHz tone at 48 kHz: the third harmonic lands at 27 kHz, above Nyquist,
    and folds to 21 kHz, where nothing legitimate can appear. The burst is half
    a lap long so repeats do not overlap, and repeat 10 is gated on its own.

    Measured at an input of 0.25 (-12 dBFS), which is **stated rather than
    assumed**: 10 §4 sets the acceptance without setting the level it is
    measured at, and the figure moves with it, because the in-loop safety clip
    -- a plain `tanh`, on regardless of DRIVE and *not* anti-aliased -- is a
    second source of folded content at high circulating levels.

    Clean carries the assertion, because it is the only character with a 9 kHz
    band left by repeat 10; tape and bucket-brigade remove it in the loop,
    which is what their mode filters are for. That is asserted too, so this
    cannot pass vacuously. */
void testTheAliasFloor()
{
    constexpr auto rate = 48000.0;
    constexpr auto toneHz = 9000.0;
    constexpr auto imageHz = 21000.0;
    const auto lap = (int) (rate * 0.1);
    const auto burst = lap / 2;

    for (int c = 0; c < 3; ++c)
    {
        P::DwellDsp dsp;
        dsp.prepare (rate, 512, 2);

        auto v = settings (c, 100.0f, 97.0f, 100.0f);
        v[P::Index::drive] = 100.0f;
        v[P::Index::fx]    = 0.0f;
        dsp.setParams (v.data(), (int) v.size());

        const auto n = lap * 13;
        Block block { n };

        for (int i = 0; i < burst; ++i)
        {
            const auto w = 0.5 - 0.5 * std::cos (2.0 * P::kPiD * (double) i / (double) burst);
            const auto s = (float) (0.25 * w * std::sin (2.0 * P::kPiD * toneHz * (double) i / rate));
            block.left[(size_t) i]  = s;
            block.right[(size_t) i] = s;
        }

        renderInChunks (dsp, block, n, 512);

        const auto image = magnitudeAt (block.left, 10 * lap, burst, imageHz, rate);
        const auto tone  = magnitudeAt (block.left, 10 * lap, burst, toneHz, rate);

        const auto imageDb = 20.0 * std::log10 (std::max (image, 1.0e-30));
        const auto toneDb  = 20.0 * std::log10 (std::max (tone, 1.0e-30));

        std::printf ("      alias floor %-15s repeat 10: image %8.1f dBFS, fundamental %8.1f dBFS\n",
                     characterName (c), imageDb, toneDb);

        check (imageDb <= -60.0,
               std::string ("the folded image is at or below -60 dBFS after 10 repeats at max DRIVE on ")
                   + characterName (c) + " (" + std::to_string (imageDb) + " dBFS)");

        if (c == P::kClean)
            check (toneDb > -60.0,
                   "clean still carries the 9 kHz band at repeat 10, so the floor above is a real "
                   "measurement (" + std::to_string (toneDb) + " dBFS)");
    }
}

/** **§2's glide** (`11` §4b).

    Tape and bucket-brigade bend pitch: a rate-limited exponential, tau 120 ms,
    capped at 0.25 samples/sample so that `rho = 1 - dD/dn` stays inside
    [0.75, 1.25]. Clean crossfades instead and 2a already runs it.

    The cap is asserted **from the delay itself** rather than inferred from the
    audio, because that is the quantity §2 bounds. The click assertion is the
    weaker one it can honestly be: under a glide the waveform's own slope rises
    with the pitch, up to 1.25x, so what is checked is that the largest
    sample-to-sample step through the move stays inside that plus a margin. A
    discontinuity would be orders out, not a quarter. */
void testTheGlideIsRateLimited()
{
    constexpr auto rate = 48000.0;
    constexpr int chunk = 64;

    for (int c = 1; c < 3; ++c)
    {
        P::DwellDsp dsp;
        dsp.prepare (rate, 512, 2);

        auto v = settings (c, 300.0f, 50.0f, 100.0f);
        dsp.setParams (v.data(), (int) v.size());

        const auto& engine = dsp.getCore().getMainEngine();

        check (std::abs (engine.currentDelaySamples() - 0.3 * rate) < 1.0,
               std::string ("the opening push snaps rather than gliding on ") + characterName (c));

        const auto n = (int) (rate * 8.0);
        Block block { n };

        for (int i = 0; i < n; ++i)
        {
            const auto s = (float) (0.3 * std::sin (2.0 * P::kPiD * 1000.0 * (double) i / rate));
            block.left[(size_t) i]  = s;
            block.right[(size_t) i] = s;
        }

        // A 300 -> 150 ms step, deliberately off a block boundary.
        const auto stepAt = (int) (rate * 2.0) + 37;

        auto worstRate = 0.0;
        auto previousDelay = engine.currentDelaySamples();
        auto stepped = false;

        for (int offset = 0; offset < n; )
        {
            if (! stepped && offset >= stepAt)
            {
                v[P::Index::time] = 150.0f;
                dsp.setParams (v.data(), (int) v.size());
                stepped = true;
            }

            const auto count = std::min (chunk, n - offset);
            float* channels[] { block.left.data() + offset, block.right.data() + offset };
            dsp.process (channels, 2, count);
            offset += count;

            const auto now = engine.currentDelaySamples();
            worstRate = std::max (worstRate, std::abs (now - previousDelay) / (double) count);
            previousDelay = now;
        }

        check (worstRate <= 0.25 + 1.0e-9,
               std::string ("the glide is rate-limited to 0.25 samples/sample on ")
                   + characterName (c) + " (" + std::to_string (worstRate) + ")");

        check (std::abs (engine.currentDelaySamples() - 0.15 * rate) < 1.0,
               std::string ("the glide arrives at the new time on ") + characterName (c));

        const auto slope = [&block] (int from, int count)
        {
            auto worst = 0.0f;

            for (int i = from + 1; i < from + count; ++i)
                worst = std::max (worst, std::abs (block.left[(size_t) i] - block.left[(size_t) (i - 1)]));

            return worst;
        };

        const auto steady = slope ((int) (rate * 1.0), (int) (rate * 0.5));
        const auto across = slope (stepAt - 2400, 9600);

        check (across <= steady * 1.5f,
               std::string ("the time step gives no discontinuity on ") + characterName (c)
                   + " (" + std::to_string (across) + " against " + std::to_string (steady) + ")");

        auto finite = true;

        for (int i = 0; i < n; ++i)
            finite = finite && std::isfinite (block.left[(size_t) i]);

        check (finite, std::string ("nothing non-finite comes out of a time move on ") + characterName (c));
    }
}

/** **Tape's character floor is modulation, never gain** (DECIDED, Frosty
    2026-09-23; not yet in 10 §5).

    Three claims, because a floor is the kind of addition that is easy to get
    subtly wrong:

    1. It **moves the read position** at MOD DEPTH 0, so tape at the defaults
       is not dead steady. Asserted against clean and bucket-brigade at the
       same settings, which must be exactly steady -- bucket-brigade gets no
       floor by decision, its signature being the clock darkening and the
       compander breathing rather than pitch movement.
    2. **Silence in is still exact zeros out.** The floor is modulation rather
       than an additive noise source, so a zeroed ring read at any fractional
       position is still zero, and there is no gate to get wrong.
    3. MOD DEPTH **adds on top of it** rather than replacing it. */
void testTheTapeCharacterFloor()
{
    constexpr auto rate = 48000.0;

    // 1 and 3: the floor is audible as movement, and the knob adds to it.
    const auto spread = [] (int character, float modDepth)
    {
        P::DwellDsp dsp;
        dsp.prepare (rate, 512, 2);

        auto v = settings (character, 300.0f, 0.0f, 100.0f);
        v[P::Index::modDepth] = modDepth;
        dsp.setParams (v.data(), (int) v.size());

        const auto n = (int) (rate * 4.0);
        Block block { n };

        for (int i = 0; i < n; ++i)
        {
            const auto s = (float) (0.3 * std::sin (2.0 * P::kPiD * 1000.0 * (double) i / rate));
            block.left[(size_t) i]  = s;
            block.right[(size_t) i] = s;
        }

        renderInChunks (dsp, block, n, 512);

        // A steady delay reproduces the tone in one bin; a wowing one spreads
        // it, so what is measured is how much energy has left 1 kHz.
        const auto from = (int) (rate * 1.0);
        const auto count = (int) (rate * 2.0);
        const auto bin = magnitudeAt (block.left, from, count, 1000.0, rate);
        const auto total = rms (block.left, from, count) * std::sqrt (2.0);

        return 1.0 - std::min (bin / std::max (total, 1.0e-12), 1.0);
    };

    const auto cleanFlat = spread (P::kClean, 0.0f);
    const auto bbdFlat   = spread (P::kBucketBrigade, 0.0f);
    const auto tapeFloor = spread (P::kTape, 0.0f);
    const auto tapeFull  = spread (P::kTape, 100.0f);

    std::printf ("      wow spread at MOD DEPTH 0: clean %.5f, bbd %.5f, tape %.5f; tape at 100 %%: %.5f\n",
                 cleanFlat, bbdFlat, tapeFloor, tapeFull);

    check (tapeFloor > cleanFlat + 1.0e-3,
           "tape at MOD DEPTH 0 is not dead steady -- the character floor is running");

    check (bbdFlat < cleanFlat + 1.0e-3,
           "bucket-brigade has no floor, by decision rather than by omission");

    check (tapeFull > tapeFloor,
           "MOD DEPTH adds on top of the floor rather than replacing it");

    // 2: silence is still silence, on every character, with the modulation at
    // full travel.
    for (int c = 0; c < 3; ++c)
    {
        P::DwellDsp dsp;
        dsp.prepare (rate, 512, 2);

        auto v = settings (c, 40.0f, 60.0f, 100.0f);
        v[P::Index::modDepth] = 100.0f;
        dsp.setParams (v.data(), (int) v.size());

        const auto n = (int) (rate * 5.0);
        Block block { n };
        Noise noise;

        for (int i = 0; i < (int) rate / 2; ++i)
        {
            block.left[(size_t) i]  = 0.5f * noise.next();
            block.right[(size_t) i] = 0.5f * noise.next();
        }

        renderInChunks (dsp, block, n, 512);

        auto zeroed = true;

        for (int i = n - 4096; i < n; ++i)
            zeroed = zeroed && block.left[(size_t) i] == 0.0f && block.right[(size_t) i] == 0.0f;

        check (zeroed, std::string ("silence in gives exact zeros out with the modulation running on ")
                           + characterName (c));
    }
}

/** **Sample-rate invariance, per character** (`11` §4k).

    Every coefficient in the loop depends only on `f_c / f_s` -- including
    bucket-brigade's, whose corner comes from TIME and N and is then prewarped
    like any other -- and the rings are sized from a time, so the same settings
    must give the same delay in *seconds* and the same decay per lap at every
    rate the suite supports. */
void testSampleRateInvariancePerCharacter()
{
    struct Measured { double echoSeconds = 0.0, lapDb = 0.0, pc = 0.0; };

    const auto measure = [] (double rate, int character)
    {
        P::DwellDsp dsp;
        dsp.prepare (rate, 512, 2);

        auto v = settings (character, 100.0f, 60.0f, 100.0f);
        dsp.setParams (v.data(), (int) v.size());

        const auto n = (int) (rate * 0.5);
        Block block { n };

        const auto burst = (int) (rate * 0.02);

        for (int i = 0; i < burst; ++i)
        {
            const auto w = 0.5 - 0.5 * std::cos (2.0 * P::kPiD * (double) i / (double) burst);
            const auto s = (float) (0.25 * w * std::sin (2.0 * P::kPiD * 500.0 * (double) i / rate));
            block.left[(size_t) i]  = s;
            block.right[(size_t) i] = s;
        }

        renderInChunks (dsp, block, n, 512);

        // The echo's arrival is taken as the **energy centroid** of the first
        // repeat rather than as its largest sample. A Hann-windowed burst is
        // flat across its top, so the largest sample hops a whole cycle of the
        // tone between one sample rate and the next -- which is a property of
        // `argmax` and not of the delay. The centroid moves with the group
        // delay, which is the thing this is asserting is invariant.
        auto weighted = 0.0, energy = 0.0;

        for (int i = (int) (rate * 0.05); i < (int) (rate * 0.15); ++i)
        {
            const auto e = (double) block.left[(size_t) i] * (double) block.left[(size_t) i];
            weighted += e * (double) i;
            energy += e;
        }

        const auto window = (int) (rate * 0.03);

        Measured m;
        m.echoSeconds = weighted / std::max (energy, 1.0e-30) / rate;
        m.lapDb = 20.0 * std::log10 (std::max (rms (block.left, (int) (rate * 0.20), window), 1.0e-30)
                                     / std::max (rms (block.left, (int) (rate * 0.10), window), 1.0e-30));
        m.pc = dsp.getCore().getMainEngine().referenceLoopPeak();
        return m;
    };

    for (int c = 0; c < 3; ++c)
    {
        const auto reference = measure (48000.0, c);

        for (const auto rate : { 44100.0, 88200.0, 96000.0, 176400.0, 192000.0 })
        {
            const auto m = measure (rate, c);
            const auto name = std::string (characterName (c)) + " at " + std::to_string ((int) rate) + " Hz";

            check (std::abs (m.echoSeconds - reference.echoSeconds) < 1.0e-4,
                   "the echo arrives at the same time, " + name);

            check (std::abs (m.lapDb - reference.lapDb) < 0.2,
                   "the decay per lap matches 48 kHz, " + name + " ("
                       + std::to_string (m.lapDb - reference.lapDb) + " dB)");

            check (std::abs (m.pc - reference.pc) < 0.002,
                   "P_c matches 48 kHz, " + name);
        }
    }
}

/** **Block-size invariance, per character** (`11` §4k).

    Everything 2b added advances per sample -- the glide, both LFO phases, the
    wear noise, the compander's detector and the shaper's ADAA state -- and a
    build that advanced any of them per block would pass every other test in
    this file and fail this one. The wear noise is seeded rather than clocked,
    which is what makes the comparison meaningful instead of flaky. */
void testBlockSizeInvariancePerCharacter()
{
    constexpr auto rate = 48000.0;
    constexpr int n = 48000;

    const auto render = [] (int character, int chunk, bool randomise)
    {
        P::DwellDsp dsp;
        dsp.prepare (rate, 2048, 2);

        auto v = settings (character, 37.0f, 70.0f, 50.0f);
        v[P::Index::modDepth] = 80.0f;
        v[P::Index::modRate]  = 3.0f;
        v[P::Index::drive]    = 55.0f;
        v[P::Index::lowCut]   = 120.0f;
        v[P::Index::highCut]  = 6000.0f;
        dsp.setParams (v.data(), (int) v.size());

        Block block { n };
        Noise noise;

        for (int i = 0; i < n; ++i)
        {
            block.left[(size_t) i]  = 0.3f * noise.next();
            block.right[(size_t) i] = 0.3f * noise.next();
        }

        if (! randomise)
        {
            renderInChunks (dsp, block, n, chunk);
        }
        else
        {
            Noise sizes;

            for (int offset = 0; offset < n; )
            {
                const auto want = 1 + (int) (std::abs ((double) sizes.next()) * 700.0);
                const auto count = std::min (want, n - offset);
                float* channels[] { block.left.data() + offset, block.right.data() + offset };
                dsp.process (channels, 2, count);
                offset += count;
            }
        }

        return block.left;
    };

    for (int c = 0; c < 3; ++c)
    {
        const auto reference = render (c, n, false);

        const auto compare = [&reference, c] (const std::vector<float>& got, const std::string& what)
        {
            auto worst = 0.0f;

            for (int i = 0; i < n; ++i)
                worst = std::max (worst, std::abs (got[(size_t) i] - reference[(size_t) i]));

            check (worst < 1.0e-6f,
                   what + " gives the same audio as one call on " + characterName (c));
        };

        for (const auto chunk : { 1, 32, 64, 512, 1023 })
            compare (render (c, chunk, false), "blocks of " + std::to_string (chunk));

        compare (render (c, 0, true), "a random block schedule");
    }
}

/** **Denormals, NaN and the extremes, per character** (`11` §4k, §4d).

    Nothing non-finite may ever leave, whatever arrives -- a NaN that reached a
    feedback ring would circulate for the life of the instance -- and the loop
    has to still be bounded afterwards rather than diverging or stuck. Run at
    the extremes of everything 2b wired: both cuts at both ends, DRIVE at the
    top, MOD DEPTH at the top and FEEDBACK past unity, where the safety clip is
    the only thing holding the loop. */
void testRobustnessPerCharacter()
{
    constexpr auto rate = 48000.0;

    for (int c = 0; c < 3; ++c)
    {
        for (const auto corners : { 0, 1 })
        {
            P::DwellDsp dsp;
            dsp.prepare (rate, 512, 2);

            auto v = settings (c, 30.0f, 100.0f, 100.0f);
            v[P::Index::lowCut]   = corners ? 1000.0f : 20.0f;
            v[P::Index::highCut]  = corners ? 1000.0f : 20000.0f;
            v[P::Index::drive]    = 100.0f;
            v[P::Index::modDepth] = 100.0f;
            dsp.setParams (v.data(), (int) v.size());

            const auto n = (int) (rate * 3.0);
            Block block { n };

            // A tone rather than noise, and the reason is the DC assertion
            // below: white noise carries its own energy right down to DC, so
            // a render driven by it reads about -56 dBFS on any DC meter
            // narrow enough to be worth having -- the *input's* low band, not
            // an offset. A 537 Hz tone has nothing below 537 Hz to confuse it,
            // and 537 is deliberately not a tooth of this 30 ms comb.
            for (int i = 0; i < n; ++i)
            {
                const auto s = (float) (0.4 * std::sin (2.0 * P::kPiD * 537.0 * (double) i / rate));
                block.left[(size_t) i]  = s;
                block.right[(size_t) i] = s;
            }

            block.left[100]   = std::numeric_limits<float>::quiet_NaN();
            block.right[512]  = std::numeric_limits<float>::infinity();
            block.left[1023]  = -std::numeric_limits<float>::infinity();
            block.right[4097] = 1.0e-42f;

            renderInChunks (dsp, block, n, 512);

            auto finite = true;
            auto peak = 0.0f;

            for (int i = 0; i < n; ++i)
            {
                finite = finite && std::isfinite (block.left[(size_t) i])
                                && std::isfinite (block.right[(size_t) i]);
                peak = std::max (peak, std::abs (block.left[(size_t) i]));
            }

            const auto where = std::string (characterName (c))
                             + (corners ? " with the cuts closed" : " with the cuts open");

            check (finite, "nothing non-finite ever leaves, " + where);

            check (peak < 4.0f,
                   "the safety clip bounds the loop at FEEDBACK 100 %, " + where
                       + " (peak " + std::to_string (peak) + ")");

            // The shaper is asymmetric by design and sits **after** the 10 Hz
            // blocker (10 §4), so one pass of its offset does reach the ring
            // -- and is then cut by the blocker and the 20 Hz rail on the next
            // lap rather than compounding. This is the assertion that it does
            // not compound.
            const auto dc = dcLevel (block.left, rate);

            std::printf ("      DC %-32s %.2e (%.1f dBFS)\n", where.c_str(), dc,
                         20.0 * std::log10 (std::max (dc, 1.0e-30)));

            check (dc < 1.0e-4, "DC stays at or below -80 dBFS, " + where);
        }
    }
}

/** **The cuts and DRIVE can only shorten the tail** (10 §3, §4).

    §3 fixes `P_c` at the user stages' neutral limits on purpose and does not
    track them live, so every setting of LOW CUT, HIGH CUT and DRIVE is at or
    below the reference: a cut costs tail rather than re-normalising it. Tape's
    0.84 dB a lap at LOW CUT 200 Hz is the figure §3 quotes; what is asserted
    here is the direction, which is the part that must never invert. */
void testTheCutsOnlyShortenTheTail()
{
    constexpr auto rate = 48000.0;
    const auto lap = (int) (rate * 0.1);

    const auto tail = [lap] (int character, float lowCut, float highCut, float drive)
    {
        P::DwellDsp dsp;
        dsp.prepare (rate, 512, 2);

        auto v = settings (character, 100.0f, 90.0f, 100.0f);
        v[P::Index::lowCut]  = lowCut;
        v[P::Index::highCut] = highCut;
        v[P::Index::drive]   = drive;
        dsp.setParams (v.data(), (int) v.size());

        const auto n = lap * 30;
        Block block { n };
        Noise noise;

        for (int i = 0; i < lap / 2; ++i)
        {
            block.left[(size_t) i]  = 0.2f * noise.next();
            block.right[(size_t) i] = 0.2f * noise.next();
        }

        renderInChunks (dsp, block, n, 512);

        return rms (block.left, 20 * lap, lap);
    };

    for (int c = 0; c < 3; ++c)
    {
        const auto open = tail (c, 20.0f, 20000.0f, 0.0f);

        check (tail (c, 200.0f, 20000.0f, 0.0f) < open,
               std::string ("LOW CUT at 200 Hz shortens the tail on ") + characterName (c));

        check (tail (c, 20.0f, 2000.0f, 0.0f) < open,
               std::string ("HIGH CUT at 2 kHz shortens the tail on ") + characterName (c));

        check (tail (c, 20.0f, 20000.0f, 100.0f) < open,
               std::string ("DRIVE shortens the tail rather than lengthening it on ")
                   + characterName (c));
    }
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

    // Stage 2a.
    testTheDryNullIsBitExact();
    testTheReferenceLoopPeakIsSwept();
    testUnityLandsAtNinetySevenPercent();
    testSampleRateInvariance();
    testBlockSizeInvariance();
    testSilenceDenormalsAndNaN();
    testProcessAllocatesNothing();

    // Stage 2b.
    testTheLoopPeakIsSweptPerCharacter();
    testTheBucketBrigadeClockFollowsTime();
    testUnityLandsAtNinetySevenPercentOnEveryCharacter();
    testTheCompanderIsUnityThroughATransient();
    testTheAliasFloor();
    testTheGlideIsRateLimited();
    testTheTapeCharacterFloor();
    testSampleRateInvariancePerCharacter();
    testBlockSizeInvariancePerCharacter();
    testRobustnessPerCharacter();
    testTheCutsOnlyShortenTheTail();

    std::printf ("%d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
