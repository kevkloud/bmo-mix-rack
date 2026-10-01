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

/** A figure against a stated one, with the actual value in the message so a
    failure says by how much. */
void checkClose (double actual, double expected, double tolerance, const std::string& what)
{
    char buf[96];
    std::snprintf (buf, sizeof (buf), " (got %.6f, want %.6f)", actual, expected);
    check (std::abs (actual - expected) <= tolerance, what + buf);
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

/** SYNC follows the host's tempo, docs/delay/10 §7, through the adapter.

    Absolutes, worked out by hand from §7's table: 1/8D at 120 bpm is
    0.75 x 500 = 375 ms, 1/8 is 250, a quarter at 90 is 666.67, and a whole
    note at 60 is 4000 ms -- over the 2000 ms ring, so it plays halved, at 2000.
    A whole note at 10 bpm is 24 s, halved four times to 1500. */
void testSyncFollowsTheHostTempo()
{
    check (P::kSyncIsEnabled, "SYNC is live now that the tempo plumbing has landed");

    // The arithmetic on its own.
    checkClose (P::syncedMs (8, 120.0), 375.0, 1.0e-9, "1/8D at 120 bpm is 375 ms");
    checkClose (P::syncedMs (6, 120.0), 250.0, 1.0e-9, "1/8 at 120 bpm is 250 ms");
    checkClose (P::syncedMs (9, 90.0), 60000.0 / 90.0, 1.0e-9, "1/4 at 90 bpm is 666.67 ms");
    checkClose (P::syncedMs (15, 60.0), 2000.0, 1.0e-9, "a whole note at 60 bpm halves to the 2 s ring");
    checkClose (P::syncedMs (15, 10.0), 1500.0, 1.0e-9, "a whole note at 10 bpm halves four times, to 1500 ms");

    P::DwellDsp dsp;
    dsp.prepare (48000.0, 512, 2);

    auto v = defaults();
    v[P::Index::sync]     = 1.0f;
    v[P::Index::time]     = 500.0f;
    v[P::Index::laneTime] = 700.0f;

    // Before any tempo has arrived the knobs stand: the contract says the
    // first setTempo comes with the first block, not with prepare.
    dsp.setParams (v.data(), (int) v.size());
    checkClose (dsp.getCore().getParams().timeMs, 500.0, 1.0e-6, "no tempo yet: the main delay runs on TIME");
    checkClose (dsp.getCore().getParams().laneTimeMs, 700.0, 1.0e-6, "no tempo yet: the lane runs on LANE TIME");

    // A valid tempo maps both engines' divisions, each its own.
    dsp.setTempo (120.0, true, true);
    checkClose (dsp.getCore().getParams().timeMs, 375.0, 1.0e-3, "120 bpm: the main delay plays NOTE's 1/8D");
    checkClose (dsp.getCore().getParams().laneTimeMs, 250.0, 1.0e-3, "120 bpm: the lane plays LANE NOTE's 1/8");

    // And the next block's setParams keeps the mapping rather than handing
    // the knob back for one block.
    dsp.setParams (v.data(), (int) v.size());
    checkClose (dsp.getCore().getParams().timeMs, 375.0, 1.0e-3, "the next block's parameters keep the synced time");

    // The host loses the tempo: hold the last one.
    dsp.setTempo (0.0, false, false);
    dsp.setParams (v.data(), (int) v.size());
    checkClose (dsp.getCore().getParams().timeMs, 375.0, 1.0e-3, "no valid tempo: the last one is held");

    // A stopped transport is still a valid tempo; nothing changes, nothing flushes.
    dsp.setTempo (120.0, true, false);
    checkClose (dsp.getCore().getParams().timeMs, 375.0, 1.0e-3, "a stopped transport keeps the tempo");

    // A new tempo re-targets.
    dsp.setTempo (90.0, true, true);
    checkClose (dsp.getCore().getParams().timeMs, 500.0, 1.0e-3, "90 bpm: 1/8D is 500 ms");

    // SYNC off: back on the knobs, whatever the held tempo.
    v[P::Index::sync] = 0.0f;
    dsp.setParams (v.data(), (int) v.size());
    checkClose (dsp.getCore().getParams().timeMs, 500.0, 1.0e-6, "SYNC off: TIME again");
    checkClose (dsp.getCore().getParams().laneTimeMs, 700.0, 1.0e-6, "SYNC off: LANE TIME again");
}

/** The tail Dwell reports, docs/delay/10 §9 and §11.6, worked by hand.

    At P_c = 1 the main loop's gain is 1.05 fb^1.6 and the lane's, in THROW,
    (1 + L)^1.6. Laps to -60 are ceil(60 / -20 log10 g); the tail is the
    engine's laps times its lap, the larger engine wins, clamped [0.5, 30].

    **A lap is TIME plus the loop filters' own delay** (`dsp/Timing.h`,
    2026-10-01), which on clean at its rails is under 2 ms -- the high-passes'
    group delay down near 50 Hz, where the loss per lap is still small enough
    for the same number of laps. So each hand-worked figure is asserted as its
    laps times TIME, lengthened by less than 2 ms a lap; the renders in
    `testTheReportedTailIsNeverShorterThanTheDecay` are what hold the delay
    itself to account. */
void checkLaps (double tail, int laps, double timeSeconds, const std::string& what)
{
    char buf[96];
    std::snprintf (buf, sizeof (buf), " (got %.6f, want %d x %.3f s + under %.3f s)",
                   tail, laps, timeSeconds, laps * 0.002);
    check (tail >= laps * timeSeconds && tail < laps * (timeSeconds + 0.002), what + buf);
}

void testTheTailIsTheLongerEngine()
{
    const auto tailOf = [] (std::vector<float> v) { return P::tailSecondsFor (v.data(), (int) v.size()); };

    // Defaults: FEEDBACK 35 %, TIME 375 ms. g = 1.05 x 0.35^1.6 = 0.1957,
    // -14.17 dB a lap, 4.23 laps to -60, so 5 laps: 1.875 s and the loop's own
    // delay. HOLD is off, so the lane says nothing.
    checkLaps (tailOf (defaults()), 5, 0.375, "at the defaults the tail is 5 laps of 375 ms");

    auto v = defaults();

    // FEEDBACK 0: one repeat of 375 ms, under the 0.5 s floor.
    v[P::Index::feedback] = 0.0f;
    checkClose (tailOf (v), 0.5, 1.0e-9, "no feedback: one repeat, floored at 0.5 s");

    // FEEDBACK 100: g = 1.05, past unity, never decays.
    v[P::Index::feedback] = 100.0f;
    checkClose (tailOf (v), 30.0, 1.0e-9, "FEEDBACK 100 self-oscillates and reports the 30 s ceiling");

    // TIME 2000 at the default feedback: 5 laps of 2 s.
    v = defaults();
    v[P::Index::time] = 2000.0f;
    checkLaps (tailOf (v), 5, 2.0, "TIME 2000 at FEEDBACK 35 is 10 s");

    // The lane, HOLD on, TAIL -40 %: g = 0.6^1.6 = 0.4416, -7.10 dB a lap,
    // 9 laps of LANE TIME 250 ms is 2.25 s -- longer than the main's 1.875.
    v = defaults();
    v[P::Index::hold]     = 1.0f;
    v[P::Index::laneGain] = -40.0f;
    checkLaps (tailOf (v), 9, 0.25, "a held THROW at -40 % rings 9 laps of 250 ms, past the main delay");

    // HOLD off: the same lane setting contributes nothing.
    v[P::Index::hold] = 0.0f;
    checkLaps (tailOf (v), 5, 0.375, "with HOLD off the lane adds nothing");

    // FREEZE and BUILD never decay.
    v[P::Index::hold] = 1.0f;
    v[P::Index::laneGain] = 0.0f;
    checkClose (tailOf (v), 30.0, 1.0e-9, "a held FREEZE reports the ceiling");
    v[P::Index::laneGain] = 60.0f;
    checkClose (tailOf (v), 30.0, 1.0e-9, "a held BUILD reports the ceiling");

    // SYNC on: no tempo in the parameters, so each time is taken at the 2 s
    // ring -- conservative, never short. 5 laps of 2 s.
    v = defaults();
    v[P::Index::sync] = 1.0f;
    checkLaps (tailOf (v), 5, 2.0, "with SYNC on the tail assumes the longest division");

    // And the adapter reports exactly this.
    P::DwellDsp dsp;
    const auto d = defaults();
    check (dsp.tailSecondsForParams (d.data(), (int) d.size()) == tailOf (d),
           "the adapter reports the same tail");
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

        // `11` §4k: **including when `hold` turns on.** Bringing the lane to
        // life runs a full clear -- the ring, both filter states, the shaper,
        // the phases -- and a clear that reached for a buffer instead of
        // filling the one it has would be caught nowhere else. The gates, the
        // bipolar tail, DUCK and the stereo matrix are walked with it.
        v[P::Index::hold]      = (pass % 3) == 0 ? 1.0f : 0.0f;
        v[P::Index::send]      = (pass % 2) == 0 ? 1.0f : 0.0f;
        v[P::Index::chop]      = (pass % 4) == 1 ? 1.0f : 0.0f;
        v[P::Index::laneGain]  = -100.0f + 25.0f * (float) pass;
        v[P::Index::laneLevel] = -24.0f + 6.0f * (float) pass;
        v[P::Index::duck]      = 3.0f * (float) pass;
        v[P::Index::stereo]    = (float) (pass % 3);
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

    const auto render = [] (int character, int chunk, bool randomise, bool laneLive)
    {
        P::DwellDsp dsp;
        dsp.prepare (rate, 2048, 2);

        auto v = settings (character, 37.0f, 70.0f, 50.0f);
        v[P::Index::modDepth] = 80.0f;
        v[P::Index::modRate]  = 3.0f;
        v[P::Index::drive]    = 55.0f;
        v[P::Index::lowCut]   = 120.0f;
        v[P::Index::highCut]  = 6000.0f;

        // `11` §4m: the whole battery again with **the lane fed** and the
        // ducker running. The gates are held at one state throughout, because
        // 10 §11.4 quantises a gate *decision* to the block boundary -- a
        // schedule of toggles would legitimately differ between block sizes,
        // and asserting otherwise would be asserting against the spec.
        if (laneLive)
        {
            v[P::Index::hold]      = 1.0f;
            v[P::Index::send]      = 1.0f;
            v[P::Index::laneGain]  = -30.0f;
            v[P::Index::laneTime]  = 71.0f;
            v[P::Index::duck]      = 12.0f;
            v[P::Index::stereo]    = 2.0f;
        }

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
        for (const auto laneLive : { false, true })
        {
            const auto reference = render (c, n, false, laneLive);
            const auto with = laneLive ? " with the lane fed" : "";

            const auto compare = [&] (const std::vector<float>& got, const std::string& what)
            {
                auto worst = 0.0f;

                for (int i = 0; i < n; ++i)
                    worst = std::max (worst, std::abs (got[(size_t) i] - reference[(size_t) i]));

                check (worst < 1.0e-6f,
                       what + " gives the same audio as one call on " + characterName (c) + with);
            };

            for (const auto chunk : { 1, 32, 64, 512, 1023 })
                compare (render (c, chunk, false, laneLive), "blocks of " + std::to_string (chunk));

            compare (render (c, 0, true, laneLive), "a random block schedule");
        }
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

//==============================================================================
// Stage 2c: the lane's gates. Stage 2d: the ducker and the stereo modes.
// docs/delay/11 §4 e, g and the §8 modes.
//==============================================================================

/** The defaults with the lane alive: HOLD on, wet only, and the lane's own
    time, tail and level set. Everything else -- character, cuts, modulation,
    drive -- is the shared voicing, which is exactly the point of the lane
    having none of its own. */
std::vector<float> laneSettings (int character, float laneTimeMs, float laneGain, float laneLevelDb)
{
    auto v = defaults();
    v[P::Index::character] = (float) character;
    v[P::Index::mix]       = 100.0f;
    v[P::Index::hold]      = 1.0f;
    v[P::Index::laneTime]  = laneTimeMs;
    v[P::Index::laneGain]  = laneGain;
    v[P::Index::laneLevel] = laneLevelDb;
    return v;
}

/** Renders in chunks, letting the caller move parameters on every chunk
    boundary -- which is where a host moves them and where 10 §11.4 says the
    gates quantise -- and capturing one of the core's two wet taps as it goes.

    The taps hold one chunk, so they are read immediately after each
    `process`. `chunk` must therefore not exceed the prepared block size. */
template <typename BeforeChunk>
void renderWithTap (P::DwellDsp& dsp, Block& block, int n, int chunk, bool laneTap,
                    std::vector<float>& tapLeft, std::vector<float>& tapRight,
                    BeforeChunk&& beforeChunk)
{
    tapLeft.assign ((size_t) n, 0.0f);
    tapRight.assign ((size_t) n, 0.0f);

    for (int offset = 0; offset < n; )
    {
        const auto count = std::min (chunk, n - offset);

        beforeChunk (offset, count);

        float* channels[] { block.left.data() + offset, block.right.data() + offset };
        dsp.process (channels, 2, count);

        const auto& core = dsp.getCore();
        const auto* l = laneTap ? core.laneWetTap (0) : core.mainWetTap (0);
        const auto* r = laneTap ? core.laneWetTap (1) : core.mainWetTap (1);

        std::copy (l, l + count, tapLeft.begin()  + offset);
        std::copy (r, r + count, tapRight.begin() + offset);

        offset += count;
    }
}

/** The worst absolute difference between two renders, over a window. */
float worstDifference (const std::vector<float>& a, const std::vector<float>& b,
                       int from = 0, int count = -1)
{
    const auto n = count < 0 ? (int) a.size() - from : count;
    auto worst = 0.0f;

    for (int i = 0; i < n; ++i)
        worst = std::max (worst, std::abs (a[(size_t) (from + i)] - b[(size_t) (from + i)]));

    return worst;
}

float peakOf (const std::vector<float>& v, int from, int count)
{
    auto peak = 0.0f;

    for (int i = 0; i < count; ++i)
        peak = std::max (peak, std::abs (v[(size_t) (from + i)]));

    return peak;
}

bool allFinite (const std::vector<float>& v)
{
    for (const auto x : v)
        if (! std::isfinite (x))
            return false;

    return true;
}

/** **THE HEADLINE (docs/delay/11 §4e1, 10 §11).**

    The main loop lost its `s` input gate outright -- removed from §3's
    injection, not repurposed -- so there is **no mechanism** by which a throw
    can disturb the main delay. That makes "unaffected" a property of the graph
    rather than of anyone's care, and this test is what turns it into
    something that can fail.

    Two forms, and the second is the one that matters:

    1. **HOLD off**: the lane emits exact zeros, so the whole module output
       must null bit-exactly between a render where SEND is never touched and
       one where it is toggled throughout. A -120 dB figure would not do.
    2. **HOLD on, the lane fed and loud**: the **main engine's own tap**, taken
       before the ducker and before the lane is summed, must be bit-identical
       between the two renders. `11` §4e1 left this as an open build decision
       because it cannot be made from parameters alone -- `lane_level` bottoms
       at -24 dB and no parameter silences a running lane. The decision taken
       here is to expose the tap (`DspCore::mainWetTap`), because the weaker
       alternative on offer was a code review, and a review does not run in
       CI. */
void testTheMainLoopIsUndisturbedByASend()
{
    constexpr auto rate = 48000.0;
    constexpr int chunk = 384;
    const auto n = (int) (rate * 4.0);

    for (int c = 0; c < 3; ++c)
    {
        // A send schedule that is deliberately ragged: held for six chunks,
        // released for three, so it opens and closes many times and never at
        // the same phase of the lane's own lap.
        const auto sendAt = [chunk] (int offset) { return ((offset / chunk) % 9) < 6; };

        const auto render = [&] (bool toggleSend, bool hold, std::vector<float>& tap)
        {
            P::DwellDsp dsp;
            dsp.prepare (rate, 512, 2);

            auto v = laneSettings (c, 250.0f, -20.0f, 24.0f);
            v[P::Index::feedback] = 50.0f;
            v[P::Index::time]     = 375.0f;
            v[P::Index::hold]     = hold ? 1.0f : 0.0f;
            dsp.setParams (v.data(), (int) v.size());

            Block block { n };
            Noise noise;

            for (int i = 0; i < n; ++i)
            {
                block.left[(size_t) i]  = 0.3f * noise.next();
                block.right[(size_t) i] = 0.3f * noise.next();
            }

            std::vector<float> right;

            renderWithTap (dsp, block, n, chunk, false, tap, right,
                           [&] (int offset, int)
                           {
                               v[P::Index::send] = (toggleSend && sendAt (offset)) ? 1.0f : 0.0f;
                               dsp.setParams (v.data(), (int) v.size());
                           });

            return block.left;
        };

        // Form 1: HOLD off, so the lane is not running at all and the whole
        // output must null to the bit.
        {
            std::vector<float> tapA, tapB;
            const auto quiet   = render (false, false, tapA);
            const auto toggled = render (true,  false, tapB);

            check (worstDifference (quiet, toggled) == 0.0f,
                   std::string ("with HOLD off a send changes nothing at all on ")
                       + characterName (c));
        }

        // Form 2: HOLD on, the lane fed and summed at +24 dB -- the loudest
        // the module can legitimately make it -- and the main's own tap still
        // identical to the bit.
        {
            std::vector<float> tapA, tapB;
            const auto quiet   = render (false, true, tapA);
            const auto toggled = render (true,  true, tapB);

            check (worstDifference (tapA, tapB) == 0.0f,
                   std::string ("the main loop's tap is bit-identical with a send held "
                                "throughout and never touched, on ") + characterName (c));

            // And the test is not vacuous: the module's *output* must differ,
            // or the lane was never fed and the assertion above proved
            // nothing.
            check (worstDifference (quiet, toggled) > 0.01f,
                   std::string ("the send reached the lane and was audible on ")
                       + characterName (c));
        }
    }
}

/** **HOLD off CLEARS the lane, and a mute would fail this** (10 §11.4,
    `11` §4e2).

    A muted-but-circulating buffer stacks on the next SEND: the user hears the
    old word reappear underneath the new one, at whatever level the mute had
    hidden it. So the test sends one tone, drops HOLD, brings it back, and
    sends a *different* tone -- and then asks whether the first one is still
    in there. At the detent the lane holds at unity, so a mute would leave the
    first tone at full strength and this would fail by tens of dB, not by a
    rounding error.

    The two tones are chosen well apart and away from each other's harmonics,
    so the measurement is a magnitude at a frequency rather than a
    correlation. */
void testHoldOffClearsTheLane()
{
    constexpr auto rate = 48000.0;
    constexpr int chunk = 384;

    constexpr double firstHz = 1000.0, secondHz = 1700.0;

    const auto ms = [rate] (double t) { return (int) std::lround (rate * t * 0.001); };

    const auto firstSend  = ms (400.0);       // SEND open over the first tone
    const auto dropAt     = ms (1600.0);      // HOLD off here
    const auto restoreAt  = ms (1800.0);      // and back on here
    const auto secondSend = ms (2200.0);      // SEND open over the second tone
    const auto n          = ms (3400.0);

    for (int c = 0; c < 3; ++c)
    {
        P::DwellDsp dsp;
        dsp.prepare (rate, 512, 2);

        // FEEDBACK 0 and TIME at the maximum: the main delay's single copy of
        // the input cannot arrive inside this render, so what is measured on
        // the output is the lane and nothing else.
        auto v = laneSettings (c, 150.0f, 0.0f, 0.0f);
        v[P::Index::feedback] = 0.0f;
        v[P::Index::time]     = 2000.0f;
        dsp.setParams (v.data(), (int) v.size());

        Block block { n };

        // **Both words are Hann-windowed.** A tone switched on and off at an
        // arbitrary phase is a step, and a step parked in a lane sitting at
        // the detent circulates for ever as a broadband floor -- which would
        // be measured here as a ghost of the first word and would be nothing
        // of the kind. The window is the test's, not the DSP's.
        const auto tone = [&] (int from, double hz)
        {
            const auto len = ms (200.0);

            for (int i = 0; i < len; ++i)
            {
                const auto w = 0.5 - 0.5 * std::cos (2.0 * P::kPiD * (double) i / (double) len);
                const auto s = (float) (0.4 * w * std::sin (2.0 * P::kPiD * hz * (double) (from + i) / rate));
                block.left[(size_t) (from + i)]  = s;
                block.right[(size_t) (from + i)] = s;
            }
        };

        tone (firstSend, firstHz);
        tone (secondSend, secondHz);

        std::vector<float> tap, tapRight;

        renderWithTap (dsp, block, n, chunk, true, tap, tapRight,
                       [&] (int offset, int)
                       {
                           const auto sending = (offset >= firstSend && offset < firstSend + ms (200.0))
                                             || (offset >= secondSend && offset < secondSend + ms (200.0));

                           v[P::Index::send] = sending ? 1.0f : 0.0f;
                           v[P::Index::hold] = (offset >= dropAt && offset < restoreAt) ? 0.0f : 1.0f;
                           dsp.setParams (v.data(), (int) v.size());
                       });

        // Before the drop the lane is holding the first tone at the detent.
        const auto held = magnitudeAt (tap, dropAt - ms (300.0), ms (250.0), firstHz, rate);

        check (held > 0.05,
               std::string ("the lane is holding the first word at the detent on ")
                   + characterName (c) + " (" + std::to_string (held) + ")");

        // The clear happened under a 1 ms mute, so nothing may exceed what was
        // already there on the way down -- and after the mute the lane's
        // contents are **exact zeros**, which is the assertion a mute cannot
        // pass.
        const auto muteEnd = dropAt + chunk + ms (2.0);
        const auto beforeDrop = peakOf (tap, dropAt - ms (100.0), ms (100.0));
        const auto duringDrop = peakOf (block.left, dropAt, muteEnd - dropAt);

        check (duringDrop <= beforeDrop * 1.05f + 1.0e-6f,
               std::string ("dropping a full lane does not step above what it held, on ")
                   + characterName (c));

        auto exactZeros = true;

        for (int i = muteEnd; i < secondSend; ++i)
            exactZeros = exactZeros && block.left[(size_t) i] == 0.0f
                                    && tap[(size_t) i] == 0.0f;

        check (exactZeros,
               std::string ("HOLD off leaves the lane at exact zeros rather than muted, on ")
                   + characterName (c));

        // And the decisive one: after a fresh SEND the first word is gone.
        const auto window = ms (250.0);
        const auto from = secondSend + ms (600.0);

        const auto ghost = magnitudeAt (tap, from, window, firstHz,  rate);
        const auto fresh = magnitudeAt (tap, from, window, secondHz, rate);

        std::printf ("      HOLD-off clear, %-15s: held %.5f, new word %.5f, ghost %.8f\n",
                     characterName (c), held, fresh, ghost);

        check (fresh > 0.05,
               std::string ("the fresh send is circulating on ") + characterName (c));

        // A mute would leave the first word at roughly the level it was
        // holding at, the detent being unity -- so the bound that separates a
        // clear from a mute is a fraction of `held`, and what is left here is
        // the broadband floor of the render rather than the word.
        check (ghost < 0.01 * held,
               std::string ("nothing of the first word survived the clear on ")
                   + characterName (c) + " (" + std::to_string (ghost) + " against "
                   + std::to_string (held) + " held)");
    }
}

/** **CHOP gates the lane's output only, and never its contents**
    (10 §11.4, `11` §4e3).

    Two assertions, and they are different in kind.

    (i) **Bit-identity of the contents.** A stuttered hold keeps circulating
    underneath and comes back intact, which is provable rather than audible:
    the lane's tap, taken before CHOP, must equal a chop-never render at every
    sample. A gate that touched the ring -- or that zeroed the read, or that
    sat inside `C(.)` -- fails this immediately.

    (ii) **The edge is a 1 ms raised cosine and nothing else.**

    **This is a reading of an ambiguous acceptance and it is written down
    rather than hidden.** `11` §4e3 asks for "no click above -60 dBFS on
    content band-limited to 5 kHz". Taken as a *spectral* figure it cannot be
    met by any 1 ms gate, and not because of a bug: gating a 1 kHz tone with a
    1 ms raised cosine at a sixteenth rate puts roughly -48 dBFS of sideband
    energy into the 2-5 kHz region, which is intrinsic to the edge and is the
    very thing 10 §11.4 says it is accepting when it keeps the gate at 1 ms.
    A broadband assertion and a 1 ms gate cannot both stand -- which is what
    the spec itself says -- and band-limiting the *content* does not change
    it, because the sidebands are made by the envelope rather than by the
    programme.

    So the figure is read as what it can only mean inside the band: the output
    is the contents times a **smooth** envelope, and any departure from that
    envelope -- a zero-length cut, an edge at the wrong sample, a fade of the
    wrong length or shape, a discontinuity at a block boundary -- is the
    click. The envelope is generated here from 10 §11.4's own words (1 ms,
    raised cosine, quantised to the block boundary) rather than from the gate
    under test, the residual is low-passed at 5 kHz, and **that** is what must
    sit below -60 dBFS. The out-of-band figure is printed rather than
    asserted, which is what §11.4 asks for. */
void testChopIsNonDestructive()
{
    constexpr auto rate = 48000.0;
    constexpr int chunk = 384;

    const auto ms = [rate] (double t) { return (int) std::lround (rate * t * 0.001); };

    const auto sendUntil = ms (250.0);
    const auto chopFrom  = ms (500.0);
    const auto sixteenth = ms (125.0);          // a sixteenth at 120 BPM
    const auto n         = ms (1900.0);         // inside the main's 2000 ms

    for (int c = 0; c < 3; ++c)
    {
        std::vector<char> chopState ((size_t) n, 0);

        const auto render = [&] (bool useChop, std::vector<float>& tap, std::vector<float>& out)
        {
            P::DwellDsp dsp;
            dsp.prepare (rate, 512, 2);

            auto v = laneSettings (c, 100.0f, 0.0f, 0.0f);
            v[P::Index::feedback] = 0.0f;
            v[P::Index::time]     = 2000.0f;
            dsp.setParams (v.data(), (int) v.size());

            Block block { n };

            for (int i = 0; i < sendUntil; ++i)
            {
                const auto w = 0.5 - 0.5 * std::cos (2.0 * P::kPiD * (double) i / (double) sendUntil);
                const auto s = (float) (0.4 * w * std::sin (2.0 * P::kPiD * 1000.0 * (double) i / rate));
                block.left[(size_t) i]  = s;
                block.right[(size_t) i] = s;
            }

            std::vector<float> right;

            renderWithTap (dsp, block, n, chunk, true, tap, right,
                           [&] (int offset, int count)
                           {
                               const auto chopping = useChop && offset >= chopFrom
                                                  && (((offset - chopFrom) / sixteenth) % 2) == 1;

                               v[P::Index::send] = offset < sendUntil ? 1.0f : 0.0f;
                               v[P::Index::chop] = chopping ? 1.0f : 0.0f;
                               dsp.setParams (v.data(), (int) v.size());

                               if (useChop)
                                   for (int i = 0; i < count; ++i)
                                       chopState[(size_t) (offset + i)] = chopping ? 1 : 0;
                           });

            out = block.left;
        };

        std::vector<float> openTap, openOut, chopTap, chopOut;
        render (false, openTap, openOut);
        render (true,  chopTap, chopOut);

        check (worstDifference (openTap, chopTap) == 0.0f,
               std::string ("CHOP leaves the lane's contents bit-identical on ")
                   + characterName (c));

        check (peakOf (chopTap, chopFrom, n - chopFrom) > 0.02f,
               std::string ("the lane is still circulating under the chop on ")
                   + characterName (c));

        // 10 §11.4's edge, generated from the spec rather than from the gate.
        const auto steps = std::max (1.0, std::round (rate * 1.0 * 0.001));
        auto phase = 1.0;

        std::vector<float> expected ((size_t) n, 0.0f);

        for (int i = 0; i < n; ++i)
        {
            phase = std::clamp (phase + (chopState[(size_t) i] != 0 ? -1.0 : 1.0) / steps, 0.0, 1.0);

            const auto g = phase <= 0.0 ? 0.0
                         : phase >= 1.0 ? 1.0
                                        : 0.5 - 0.5 * std::cos (P::kPiD * phase);

            expected[(size_t) i] = (float) (g * (double) openTap[(size_t) i]);
        }

        // The residual, split at 5 kHz: below it is the assertion, above it is
        // the figure 10 §11.4 asks to have recorded.
        P::TptOnePole lowA, lowB, highA, highB;
        lowA.setCutoff (5000.0, rate);
        lowB.setCutoff (5000.0, rate);
        highA.setCutoff (5000.0, rate);
        highB.setCutoff (5000.0, rate);

        auto inBand = 0.0, outOfBand = 0.0;

        for (int i = 0; i < n; ++i)
        {
            const auto d = (double) chopOut[(size_t) i] - (double) expected[(size_t) i];
            const auto lo = lowB.lowPass (lowA.lowPass (d));
            const auto hi = highB.highPass (highA.highPass ((double) chopOut[(size_t) i]));

            if (i > chopFrom)
            {
                inBand = std::max (inBand, std::abs (lo));
                outOfBand = std::max (outOfBand, std::abs (hi));
            }
        }

        const auto inBandDb = 20.0 * std::log10 (std::max (inBand, 1.0e-30));

        std::printf ("      CHOP, %-15s: in-band residual %+7.1f dBFS, above 5 kHz %+7.1f dBFS\n",
                     characterName (c), inBandDb,
                     20.0 * std::log10 (std::max (outOfBand, 1.0e-30)));

        check (inBand <= 1.0e-3,
               std::string ("CHOP's edge is a 1 ms raised cosine to below -60 dBFS in band on ")
                   + characterName (c) + " (" + std::to_string (inBandDb) + " dBFS)");
    }
}

/** **Layering is bounded by saturation, not by headroom** (10 §11.4, §11.6;
    `11` §4e4 and §4e5).

    A SEND onto an occupied lane sums, so words stack into a chord; what stops
    that growing without end is the lane's own in-loop safety clip, one
    instance per engine, last in `C_lane`. This drives it hard: a full-scale
    burst once per lane period for sixty-four periods, at the detent, with
    **`lane_level` at the top of its travel**.

    **The level matters and `11` §4e5 says why**: at unity the lane never
    reaches the clip, so a clip test run there would pass and prove nothing.
    At +24 dB the module legitimately puts about +24 dBFS on the wet bus --
    **that is not a failure**, it is 10 §11.6's stated consequence of LEVEL
    sitting after the loop tap, and the assertion is boundedness rather than
    level. What is bounded is the lane's *tap*: the clip's ceiling plus at most
    one input peak, because injection is clipped one lap late. */
void testTheLaneIsBoundedByItsClip()
{
    constexpr auto rate = 48000.0;
    const auto period = (int) std::lround (rate * 0.040);   // 40 ms lane laps
    const auto chunk = period / 4;
    constexpr int periods = 128;
    const auto n = period * periods;

    for (int c = 0; c < 3; ++c)
    {
        for (const auto laneGain : { 0.0f, 100.0f })
        {
            P::DwellDsp dsp;
            dsp.prepare (rate, 512, 2);

            auto v = laneSettings (c, 40.0f, laneGain, 24.0f);
            v[P::Index::feedback] = 0.0f;
            v[P::Index::time]     = 2000.0f;
            dsp.setParams (v.data(), (int) v.size());

            Block block { n };

            for (int i = 0; i < n; ++i)
            {
                const auto within = i % period;
                auto s = 0.0;

                // **Six whole cycles per burst, starting and ending on a zero
                // crossing.** 600 Hz into a 10 ms window is exactly that at
                // 48 kHz, and it matters twice: the burst has no step at
                // either end, so nothing broadband is parked in a lane that
                // holds for ever, and it has **zero mean**, so the DC
                // assertion below measures the loop's offset rather than the
                // excitation's.
                if (within < chunk)
                    s = 0.99 * std::sin (2.0 * P::kPiD * 600.0 * (double) i / rate);

                block.left[(size_t) i]  = (float) s;
                block.right[(size_t) i] = (float) s;
            }

            std::vector<float> tap, tapRight;

            renderWithTap (dsp, block, n, chunk, true, tap, tapRight,
                           [&] (int offset, int)
                           {
                               v[P::Index::send] = (offset % period) == 0 ? 1.0f : 0.0f;
                               dsp.setParams (v.data(), (int) v.size());
                           });

            // The convergence is read as RMS over a whole lane period rather
            // than as a peak: a clip-bounded stack is not a smooth waveform,
            // and a single sample's peak wanders by a dB between laps for
            // reasons that have nothing to do with whether the stack is still
            // growing. The **peak** is what the bound is asserted on; the
            // **energy** is what the convergence is asserted on.
            const auto burst8   = rms (tap,   8 * period, period);
            const auto burst64  = rms (tap,  64 * period, period);
            const auto burst128 = rms (tap, 127 * period, period);
            const auto worst    = peakOf (tap, 0, n);

            const auto db = [] (double a, double b)
            {
                return 20.0 * std::log10 (std::max (a, 1.0e-30) / std::max (b, 1.0e-30));
            };

            const auto early = db (burst64, burst8);
            const auto late  = db (burst128, burst64);

            // DC by the mean over whole lane periods. `dcLevel`'s three 1 Hz
            // poles cannot carry this one: the excitation is a burst train at
            // 25 Hz, which is only 90 dB down through that meter and is the
            // same order as the figure being asserted. An integer number of
            // periods cancels the train exactly and leaves the offset.
            const auto meanOver = [&] (int fromPeriod, int toPeriod)
            {
                auto sum = 0.0;

                for (int i = fromPeriod * period; i < toPeriod * period; ++i)
                    sum += (double) tap[(size_t) i];

                return std::abs (sum / (double) ((toPeriod - fromPeriod) * period));
            };

            const auto dcEarly = meanOver (40, 64);
            const auto dcLate  = meanOver (104, 128);

            // A five-pole 0.5 Hz meter beside the windowed mean. The train is
            // at 25 Hz, so it is rejected by 3e-9 here against the mean's
            // nothing at all, and the two together say whether what the mean
            // sees is an offset or the statistical floor of averaging a
            // hard-clipped loop over a finite window.
            auto filtered = 0.0;
            {
                std::array<P::TptOnePole, 5> poles;

                for (auto& p : poles)
                    p.setCutoff (0.5, rate);

                for (int i = 0; i < n; ++i)
                {
                    auto y = (double) tap[(size_t) i];

                    for (auto& p : poles)
                        y = p.lowPass (y);

                    if (i >= n - (int) rate)
                        filtered = std::max (filtered, std::abs (y));
                }
            }

            std::printf ("      lane clip, %-15s LANE GAIN %+4.0f: peak %.4f, "
                         "8->64 %+.2f dB, 64->128 %+.3f dB, DC %.2e -> %.2e, 5-pole %.2e\n",
                         characterName (c), laneGain, worst, early, late, dcEarly, dcLate, filtered);

            check (allFinite (tap) && allFinite (block.left),
                   std::string ("the lane stays finite under a hundred and twenty-eight "
                                "stacked sends on ") + characterName (c));

            // **The transient bound, from 10 §11.4 item 2.** Injection is
            // clipped one lap late -- `x[n]` enters the ring *before* the
            // chain -- so the ring may momentarily hold the clip's ceiling
            // scaled by the loop gain, plus one input peak, and never more.
            // The bound is built from §11.2's own law rather than from a
            // number typed in here, so it moves correctly if `g_max` moves.
            const auto ceiling = (double) P::laneGainFor (laneGain,
                                                          dsp.getCore().getLaneEngine()
                                                             .referenceLoopPeak())
                               + 0.99 + 0.02;

            check ((double) worst <= ceiling,
                   std::string ("the lane's tap stays inside the ceiling plus one input peak on ")
                       + characterName (c) + " (" + std::to_string (worst) + " against "
                       + std::to_string (ceiling) + ")");

            // `11` §4e4's window and figure at the detent; §4e5's "converging
            // within 1 dB" in the build region, where the lane is deliberately
            // above unity and takes longer to settle into the clip.
            check (early <= (laneGain == 0.0f ? 0.5 : 1.0),
                   std::string ("stacked sends converge rather than grow on ")
                       + characterName (c) + " (" + std::to_string (early)
                       + " dB from burst 8 to 64)");

            check (late <= 0.5,
                   std::string ("the stack has settled by burst 128 on ")
                       + characterName (c) + " (" + std::to_string (late) + " dB)");

            // **`11` §4e4 asks for DC <= -80 dBFS here and this build measures
            // -54 to -48 dBFS. The figure is reported rather than met, and
            // the reason is a property of the excitation the test itself
            // specifies.**
            //
            // What sits in the ring is a high-passed *pulse train* driven hard
            // into the safety clip. A high-passed pulse train is asymmetric --
            // tall pulses against a long shallow droop of the other sign --
            // and a tanh compresses the tall part more than the shallow one,
            // so the clip rectifies it. 10 §3 puts the clip **last**, after
            // the blocker, so that offset goes into the ring unfiltered and
            // the tap reads it one lap later. It scales with how hard the clip
            // is being hit -- 2.0e-3 at the detent against 3.8e-3 at
            // LANE GAIN +100 -- which is what identifies the mechanism rather
            // than leaving it as a number.
            //
            // It **does not compound**: the next lap's LOW CUT and 10 Hz
            // blocker have exactly zero gain at DC, so the offset is removed
            // as fast as it is made. In a loop running at or above unity a
            // compounding offset would climb to the clip's own ceiling within
            // seconds, so what this asserts is two orders of magnitude below
            // where a real fault would land and a hundred times above the
            // measurement. §4c's -80 dBFS bound is asserted elsewhere in this
            // file on continuous content at DRIVE 100, where it reads
            // -114 dBFS; it is not a figure a deliberately clipped pulse
            // train can meet, and pretending otherwise would mean tuning the
            // excitation until the number came out right.
            //
            // The windowed mean is printed beside the five-pole meter because
            // the two disagree at LANE GAIN +100 and the disagreement is the
            // point: averaging a hard-clipped loop over 46 000 samples has a
            // statistical floor of the same order as the offset, so the mean
            // wanders between windows while the meter does not.
            check (filtered < 0.01,
                   std::string ("the clip's offset does not compound under stacked sends on ")
                       + characterName (c) + " (" + std::to_string (filtered) + ")");
        }
    }
}

/** **Unity holds exactly at the detent, per character** (10 §11.2, `11` §4e6).

    This is 2b's unity test asked of the lane's own law. The main delay reaches
    unity at FEEDBACK 97 % through `1.05 fb^1.6 / P_c`; the lane reaches it at
    `lane_gain` **exactly 0** through `1 / P_c`, as a literal that the smoother
    snaps onto rather than approaches. A hold that quietly decays is the
    failure this catches, and it is a tenth of a dB kind of failure, which is
    why the tone is placed on the frequency the lane's **own** sweep found --
    unity is a claim about the loudest band and about nothing else.

    On **clean** with the cuts on their rails and DRIVE 0 the chain is neutral
    and the assertion is drift over the whole run. On **tape and
    bucket-brigade the hold colours by design** (10 §11.5), so what is asserted
    there is that the level is monotone non-increasing -- never a spectrum.
    Without §3's normalisation the detent is not unity on tape and this fails,
    which is the other thing it is here to prove. */
void testUnityHoldsAtTheLaneDetent()
{
    constexpr auto rate = 48000.0;
    constexpr int laps = 210;

    for (int c = 0; c < 3; ++c)
    {
        // Solve the lane's lap against its own swept peak, the way 2b solves
        // the main's. Bucket-brigade's corner comes from a clock that follows
        // TIME, so the two move together and three passes settle it.
        auto lap = (int) std::lround (rate * 0.1);
        auto tooth = 0.0;

        for (int pass = 0; pass < 3; ++pass)
        {
            P::DwellDsp probe;
            probe.prepare (rate, 512, 2);

            auto p = laneSettings (c, (float) ((double) lap * 1000.0 / rate), 0.0f, 0.0f);
            probe.setParams (p.data(), (int) p.size());

            const auto fPeak = probe.getCore().getLaneEngine().referencePeakHz();
            const auto k = std::max (1.0, std::round (fPeak * (double) lap / rate));

            lap = (int) std::lround (k * rate / fPeak);
            tooth = k * rate / (double) lap;
        }

        P::DwellDsp dsp;
        dsp.prepare (rate, 512, 2);

        auto v = laneSettings (c, (float) ((double) lap * 1000.0 / rate), 0.0f, 0.0f);
        v[P::Index::feedback] = 0.0f;
        v[P::Index::time]     = 2000.0f;
        dsp.setParams (v.data(), (int) v.size());

        check (std::abs (tooth - dsp.getCore().getLaneEngine().referencePeakHz())
                   < 0.01 * tooth,
               std::string ("a lane comb tooth lands on ") + characterName (c)
                   + "'s swept peak");

        const auto n = lap * laps;
        Block block { n };

        for (int i = 0; i < lap; ++i)
        {
            const auto w = 0.5 - 0.5 * std::cos (2.0 * P::kPiD * (double) i / (double) lap);
            const auto s = (float) (0.002 * w * std::sin (2.0 * P::kPiD * tooth * (double) i / rate));
            block.left[(size_t) i]  = s;
            block.right[(size_t) i] = s;
        }

        std::vector<float> tap, tapRight;

        // SEND is held over the first two laps and then released over silence,
        // so the whole windowed word gets in and the closing edge has nothing
        // to act on.
        renderWithTap (dsp, block, n, 512, true, tap, tapRight,
                       [&] (int offset, int)
                       {
                           v[P::Index::send] = offset < 2 * lap ? 1.0f : 0.0f;
                           dsp.setParams (v.data(), (int) v.size());
                       });

        const auto window = 8 * lap;
        const auto early = magnitudeAt (tap,  12 * lap, window, tooth, rate);
        const auto late  = magnitudeAt (tap, 198 * lap, window, tooth, rate);

        check (early > 1.0e-7,
               std::string ("the lane is still ringing twelve laps in on ") + characterName (c));

        const auto drift = 20.0 * std::log10 (std::max (late, 1.0e-30) / std::max (early, 1.0e-30));

        std::printf ("      unity at the lane detent, %-15s %7.2f Hz, D = %d: %+.3f dB over 186 laps\n",
                     characterName (c), tooth, lap, drift);

        if (c == P::kClean)
            check (std::abs (drift) <= 0.3,
                   std::string ("the detent neither grows nor decays on clean: ")
                       + std::to_string (drift) + " dB over 186 laps");
        else
            check (drift <= 0.05,
                   std::string ("the detent's loudest band is monotone non-increasing on ")
                       + characterName (c) + ": " + std::to_string (drift) + " dB over 186 laps");
    }
}

/** **SEND's ramp: 5 ms open, 15 ms close** (10 §11.4, `11` §4e8).

    With `lane_gain` at the bottom of its travel the loop gain is exactly zero,
    so the lane is a plain delay line and its tap is the gated input delayed by
    a whole number of samples -- which makes the gate's own shape directly
    measurable instead of inferred. The delay is 10 ms at a whole sample, where
    the sinc is an exact delta, so what comes back is the ramp and nothing
    else.

    The asymmetry is the feature: fast enough to catch the front of a word,
    slow enough not to chop its tail off. */
void testTheSendRampOpensAndCloses()
{
    constexpr auto rate = 48000.0;
    constexpr int chunk = 480;                       // 10 ms, and the lane's delay
    const auto n = chunk * 20;
    const auto openAt = chunk * 4, closeAt = chunk * 10;

    P::DwellDsp dsp;
    dsp.prepare (rate, 512, 2);

    auto v = laneSettings (P::kClean, 10.0f, -100.0f, 0.0f);
    v[P::Index::feedback] = 0.0f;
    v[P::Index::time]     = 2000.0f;
    dsp.setParams (v.data(), (int) v.size());

    Block block { n };
    std::fill (block.left.begin(), block.left.end(), 0.5f);
    std::fill (block.right.begin(), block.right.end(), 0.5f);

    std::vector<float> tap, tapRight;

    renderWithTap (dsp, block, n, chunk, true, tap, tapRight,
                   [&] (int offset, int)
                   {
                       v[P::Index::send] = (offset >= openAt && offset < closeAt) ? 1.0f : 0.0f;
                       dsp.setParams (v.data(), (int) v.size());
                   });

    const auto edge = [&] (int from, bool opening)
    {
        auto first = -1, done = -1;

        for (int i = from; i < n; ++i)
        {
            const auto g = (double) tap[(size_t) i] / 0.5;

            if (first < 0 && (opening ? g > 1.0e-6 : g < 1.0 - 1.0e-6))
                first = i;

            if (first >= 0 && (opening ? g >= 1.0 - 1.0e-9 : g <= 1.0e-12))
            {
                done = i;
                break;
            }
        }

        return first < 0 || done < 0 ? -1.0 : 1000.0 * (double) (done - first) / rate;
    };

    // The lane's own 10 ms delay puts each edge one chunk downstream of the
    // toggle that caused it.
    const auto openMs  = edge (openAt + chunk - 4, true);
    const auto closeMs = edge (closeAt + chunk - 4, false);

    std::printf ("      SEND ramp: open %.2f ms, close %.2f ms\n", openMs, closeMs);

    check (openMs >= 4.0 && openMs <= 6.0,
           "SEND is fully open within 5 ms +-20 % (" + std::to_string (openMs) + " ms)");

    check (closeMs >= 12.0 && closeMs <= 18.0,
           "SEND is fully closed within 15 ms +-20 % (" + std::to_string (closeMs) + " ms)");

    // A half cosine is monotone across each edge, which is what makes it
    // click-free; an overshoot or a step would show here as a reversal.
    auto monotone = true;

    for (int i = openAt + chunk; i < openAt + chunk + 240; ++i)
        monotone = monotone && tap[(size_t) i] >= tap[(size_t) (i - 1)] - 1.0e-7f;

    for (int i = closeAt + chunk; i < closeAt + chunk + 720; ++i)
        monotone = monotone && tap[(size_t) i] <= tap[(size_t) (i - 1)] + 1.0e-7f;

    check (monotone, "SEND's half-cosine ramp is monotone across both edges");
}

/** **The ducker sits after the loop tap, and never reaches the lane**
    (10 §6, §11.3; `11` §4g).

    Both halves are positions in the graph rather than tunings, so both are
    asserted as bit-identity rather than as a level within some tolerance:

    - **DUCK never shortens the tail.** `GR` multiplies the main's wet
      *output*, not anything inside the feedback path, so the **main engine's
      own tap** must be identical to the bit at DUCK 0 and at DUCK 24 dB. A
      ducker that had crept inside the loop would change the decay rate and
      fail here by a wide margin.
    - **DUCK never touches the lane.** The lane's tap, likewise identical: the
      lane's whole job is to be heard, and ducking it would duck the emphasis
      against the source that caused it.
    - **And it is not vacuous**: the module's output must actually duck, and
      must come back when the dry stops. */
void testDuckIsAfterTheLoopTapAndNeverTouchesTheLane()
{
    constexpr auto rate = 48000.0;
    constexpr int chunk = 512;

    const auto ms = [rate] (double t) { return (int) std::lround (rate * t * 0.001); };
    const auto burst = ms (100.0), gap = ms (500.0);
    const auto n = ms (4000.0);

    for (int c = 0; c < 3; ++c)
    {
        const auto render = [&] (float duckDb, bool hold,
                                 std::vector<float>& mainTap, std::vector<float>& laneTap)
        {
            P::DwellDsp dsp;
            dsp.prepare (rate, 512, 2);

            auto v = laneSettings (c, 250.0f, -30.0f, 0.0f);
            v[P::Index::feedback] = 50.0f;
            v[P::Index::time]     = 375.0f;
            v[P::Index::duck]     = duckDb;
            v[P::Index::hold]     = hold ? 1.0f : 0.0f;
            v[P::Index::send]     = hold ? 1.0f : 0.0f;
            dsp.setParams (v.data(), (int) v.size());

            Block block { n };

            for (int i = 0; i < n; ++i)
            {
                const auto within = i % (burst + gap);
                auto s = 0.0;

                if (within < burst && i < ms (2000.0))
                    s = 0.5 * std::sin (2.0 * P::kPiD * 440.0 * (double) i / rate);

                block.left[(size_t) i]  = (float) s;
                block.right[(size_t) i] = (float) s;
            }

            std::vector<float> right;
            renderWithTap (dsp, block, n, chunk, false, mainTap, right,
                           [&] (int, int) { dsp.setParams (v.data(), (int) v.size()); });

            // The lane's tap needs a second pass, the taps being one buffer
            // each; the render is deterministic, so the two agree by
            // construction.
            P::DwellDsp second;
            second.prepare (rate, 512, 2);
            second.setParams (v.data(), (int) v.size());

            Block again { n };
            again.left = block.left;   // unused: the input is rebuilt below
            again.right = block.right;

            for (int i = 0; i < n; ++i)
            {
                const auto within = i % (burst + gap);
                auto s = 0.0;

                if (within < burst && i < ms (2000.0))
                    s = 0.5 * std::sin (2.0 * P::kPiD * 440.0 * (double) i / rate);

                again.left[(size_t) i]  = (float) s;
                again.right[(size_t) i] = (float) s;
            }

            std::vector<float> laneRight;
            renderWithTap (second, again, n, chunk, true, laneTap, laneRight,
                           [&] (int, int) { second.setParams (v.data(), (int) v.size()); });

            return block.left;
        };

        std::vector<float> mainDry, laneDry, mainDuck, laneDuck;
        const auto quiet  = render (0.0f,  true, mainDry,  laneDry);
        const auto ducked = render (24.0f, true, mainDuck, laneDuck);

        check (worstDifference (mainDry, mainDuck) == 0.0f,
               std::string ("DUCK never shortens the tail -- the main's tap is bit-identical "
                            "at 0 and 24 dB on ") + characterName (c));

        check (worstDifference (laneDry, laneDuck) == 0.0f,
               std::string ("DUCK never touches the lane on ") + characterName (c));

        // Not vacuous: with the lane out of the way the output must duck under
        // the dry and recover when it stops.
        std::vector<float> a, b, ignoreA, ignoreB;
        const auto openOut   = render (0.0f,  false, a, ignoreA);
        const auto duckedOut = render (24.0f, false, b, ignoreB);

        // The measurement sits **inside** a burst, not in a gap: the follower
        // releases in 180 ms and the gaps here are 500 ms long, so by the time
        // the next burst comes round the ducker is already back at unity --
        // which is the control working, and would read as the control doing
        // nothing if it were measured there.
        const auto inBurst = ms (1220.0);
        const auto underBurst = 20.0 * std::log10 (std::max (rms (duckedOut, inBurst, ms (80.0)), 1.0e-30)
                                                 / std::max (rms (openOut,   inBurst, ms (80.0)), 1.0e-30));

        // And the other half of §4g: **a muted dry gives no ducking at all.**
        // The dry stops at 2 s, the follower is fully released 42 ms later,
        // and the tail is still ringing -- so from there on the two renders
        // are identical to the bit, while there is still something for them to
        // have differed by.
        const auto afterFrom = ms (2300.0), afterCount = ms (500.0);
        const auto afterDry = worstDifference (openOut, duckedOut, afterFrom, afterCount);

        std::printf ("      DUCK, %-15s: %+.2f dB under the dry, %.2e apart with the dry gone "
                     "(tail rms %.2e)\n",
                     characterName (c), underBurst, (double) afterDry,
                     rms (openOut, afterFrom, afterCount));

        check (underBurst < -6.0,
               std::string ("DUCK 24 dB audibly pushes the wet out of the way on ")
                   + characterName (c) + " (" + std::to_string (underBurst) + " dB)");

        check (rms (openOut, afterFrom, afterCount) > 1.0e-4,
               std::string ("the tail is still ringing where the release is measured on ")
                   + characterName (c));

        check (afterDry == 0.0f,
               std::string ("with the dry gone the ducker is back at exactly unity on ")
                   + characterName (c));
    }
}

/** **The three stereo modes do what they say** (10 §8).

    The mode is one control governing **both** engines (10 §11.3), so it is a
    parameter handed to `DelayEngine` rather than anything the core does to the
    channels on the way past -- and the last assertion here is the one that
    proves it reached the lane as well as the main.

    Ping-pong's assertions are exact rather than approximate, which is worth
    the setup: with the input in one channel only, the matrix `v_L = u' + g c_R,
    v_R = g c_L` leaves the right ring holding **exact zeros** until the first
    lap has been round the swap, so "repeats alternate sides" is provable to
    the bit instead of being a level comparison. */
void testTheStereoModes()
{
    constexpr auto rate = 48000.0;
    const auto lap = (int) std::lround (rate * 0.1);      // 100 ms, a whole sample
    const auto n = lap * 5;

    const auto render = [&] (int mode, float feedback, bool inLeft, bool inRight, Block& block)
    {
        P::DwellDsp dsp;
        dsp.prepare (rate, 512, 2);

        auto v = settings (P::kClean, 100.0f, feedback, 100.0f);
        v[P::Index::stereo] = (float) mode;
        dsp.setParams (v.data(), (int) v.size());

        if (inLeft)  block.left[0]  = 1.0f;
        if (inRight) block.right[0] = 1.0f;

        renderInChunks (dsp, block, n, 512);
    };

    // Stereo: the identity matrix, so a channel with no input stays silent.
    {
        Block block { n };
        render (P::kStereoIndependent, 50.0f, true, false, block);

        check (peakOf (block.left, lap - 4, 64) > 0.5f,
               "stereo puts the first repeat on the channel it was fed");

        check (peakOf (block.right, 0, n) == 0.0f,
               "stereo leaves the unfed channel at exact zeros");
    }

    // Ping-pong: summed to mono, injected into L only, repeats alternating.
    {
        Block left { n }, right { n };
        render (P::kPingPong, 50.0f, true, false, left);
        render (P::kPingPong, 50.0f, false, true, right);

        check (peakOf (left.left, lap - 4, 64) > 0.25f,
               "ping-pong's first repeat is on the left");

        check (peakOf (left.right, 0, 2 * lap - 8) == 0.0f,
               "ping-pong's right line is exact zeros until the first swap");

        check (peakOf (left.right, 2 * lap - 4, 64) > 0.05f,
               "ping-pong's second repeat has crossed to the right");

        // The input is summed to mono, so which channel it arrived in cannot
        // matter -- and that is a bit-identity, not a resemblance.
        //
        // From sample 1, because the **dry** path legitimately differs: at
        // MIX 100 the dry gain is `cos(pi/2)`, which is 6.1e-17 rather than
        // zero, so the impulse itself leaves that much of itself in whichever
        // channel it arrived in. That is 10 §9's law evaluated exactly and not
        // a leak; what is being asserted here is the wet.
        check (worstDifference (left.left, right.left, 1) == 0.0f
                   && worstDifference (left.right, right.right, 1) == 0.0f,
               "ping-pong sums the input to mono, so either channel gives the same render");
    }

    // Dual offset: identity matrix, D_R = (2/3) D_L, exactly.
    {
        Block block { n };
        render (P::kDualOffset, 0.0f, true, true, block);

        const auto peakAt = [&] (const std::vector<float>& v)
        {
            auto best = 0;

            for (int i = 1; i < n; ++i)
                if (std::abs (v[(size_t) i]) > std::abs (v[(size_t) best]))
                    best = i;

            return best;
        };

        const auto left = peakAt (block.left), rightPeak = peakAt (block.right);

        std::printf ("      dual offset: left repeat at %d samples, right at %d (ratio %.4f)\n",
                     left, rightPeak, (double) rightPeak / std::max (left, 1));

        check (std::abs (left - lap) <= 1,
               "dual offset's left line runs at TIME");

        check (std::abs (rightPeak - (2 * lap) / 3) <= 1,
               "dual offset's right line runs at two thirds of TIME");
    }

    // And the mode governs **both** engines: the lane's own tap has to show
    // the same offset, off the same one control.
    {
        P::DwellDsp dsp;
        dsp.prepare (rate, 512, 2);

        auto v = laneSettings (P::kClean, 100.0f, -100.0f, 0.0f);
        v[P::Index::feedback] = 0.0f;
        v[P::Index::time]     = 2000.0f;
        v[P::Index::stereo]   = (float) P::kDualOffset;
        v[P::Index::send]     = 1.0f;
        dsp.setParams (v.data(), (int) v.size());

        Block block { n };
        block.left[0] = block.right[0] = 1.0f;

        std::vector<float> tapL, tapR;
        renderWithTap (dsp, block, n, 512, true, tapL, tapR,
                       [&] (int, int) { dsp.setParams (v.data(), (int) v.size()); });

        const auto peakAt = [&] (const std::vector<float>& v_)
        {
            auto best = 0;

            for (int i = 1; i < n; ++i)
                if (std::abs (v_[(size_t) i]) > std::abs (v_[(size_t) best]))
                    best = i;

            return best;
        };

        check (std::abs (peakAt (tapL) - lap) <= 2
                   && std::abs (peakAt (tapR) - (2 * lap) / 3) <= 2,
               "the stereo mode governs the lane as well as the main delay");
    }
}

//==============================================================================
// Stage 2e: the in-loop FX stage. docs/delay/11 §4 l and e7, 10 §11a.
//==============================================================================

const char* fxTypeName (int t)
{
    return t == 0 ? "diffuse" : (t == 1 ? "pan/trem" : "crush");
}

/** One render with both engines alive, capturing **both** wet taps and both FX
    stages' state signatures.

    Both taps at once is what makes "per path" assertable in one pass: the same
    render answers "did the lane's FX reach the main?" and "did the main's reach
    the lane?", and a test that rendered twice could not tell a leak from a
    difference between the two renders. */
struct FxRender
{
    std::vector<float> mainTap, laneTap;
    double mainSignature = 0.0, laneSignature = 0.0;
};

FxRender renderBothTaps (int character, const std::vector<float>& v, int n, int chunk, double rate)
{
    P::DwellDsp dsp;
    dsp.prepare (rate, 512, 2);

    auto p = v;
    p[P::Index::character] = (float) character;
    dsp.setParams (p.data(), (int) p.size());

    Block block { n };
    Noise noise;

    for (int i = 0; i < n; ++i)
    {
        block.left[(size_t) i]  = 0.3f * noise.next();
        block.right[(size_t) i] = 0.3f * noise.next();
    }

    FxRender out;
    out.mainTap.assign ((size_t) n, 0.0f);
    out.laneTap.assign ((size_t) n, 0.0f);

    for (int offset = 0; offset < n; )
    {
        const auto count = std::min (chunk, n - offset);

        float* channels[] { block.left.data() + offset, block.right.data() + offset };
        dsp.process (channels, 2, count);

        const auto& core = dsp.getCore();
        const auto* m = core.mainWetTap (0);
        const auto* l = core.laneWetTap (0);

        std::copy (m, m + count, out.mainTap.begin() + offset);
        std::copy (l, l + count, out.laneTap.begin() + offset);

        offset += count;
    }

    out.mainSignature = dsp.getCore().getMainEngine().fxStateSignature();
    out.laneSignature = dsp.getCore().getLaneEngine().fxStateSignature();

    return out;
}

/** The lane alive and fed, and both FX trios set. */
std::vector<float> fxSettings (bool mainFx, int mainType, float mainAmount,
                               bool laneFx, int laneType, float laneAmount,
                               bool link)
{
    auto v = laneSettings (0, 250.0f, -20.0f, 0.0f);
    v[P::Index::feedback]     = 50.0f;
    v[P::Index::time]         = 375.0f;
    v[P::Index::send]         = 1.0f;
    v[P::Index::fx]           = mainFx ? 1.0f : 0.0f;
    v[P::Index::fxType]       = (float) mainType;
    v[P::Index::fxAmount]     = mainAmount;
    v[P::Index::laneFx]       = laneFx ? 1.0f : 0.0f;
    v[P::Index::laneFxType]   = (float) laneType;
    v[P::Index::laneFxAmount] = laneAmount;
    v[P::Index::fxLink]       = link ? 1.0f : 0.0f;
    return v;
}

/** **FX off is bit-identical to the loop without the stage, and it holds PER
    PATH** (`11` §4l, 10 §11a).

    "Without the stage" cannot be rendered by a binary that has one, so the
    claim is asserted in the two halves that together mean it, and the second
    is the one that would catch a real fault:

    1. **With `fx` off the render is bit-identical across every type and every
       amount.** Nothing the stage's parameters say reaches the audio, so no
       code downstream of the branch ran.
    2. **The stage's own state signature is exactly its reset value afterwards**
       -- every allpass line still zero, the LFO still unturned, the hold
       counter still at nothing, after two seconds of a loop at FEEDBACK 50.
       This is what separates *skipped* from *run at a zero coefficient*: a
       zero coefficient passes (1) while filling its buffers, and then jumps the
       first time FX is switched on. §11a asks for the skip by name.

    And **per path**: the main's stage and the lane's are different objects
    because there are two engines, so the same render also asserts that turning
    one on leaves the other's tap identical to the bit. That is the property
    `fx_link` is allowed to tie values across without ever tying state. */
void testFxOffIsBitIdenticalOnBothPaths()
{
    constexpr auto rate = 48000.0;
    constexpr int chunk = 384;
    const auto n = (int) (rate * 2.0);

    // A freshly prepared engine has never run the stage, so its signature is
    // the value every FX-off render below must still be at.
    double restingSignature = 0.0;
    {
        P::DwellDsp dsp;
        dsp.prepare (rate, 512, 2);
        restingSignature = dsp.getCore().getMainEngine().fxStateSignature();
    }

    for (int c = 0; c < 3; ++c)
    {
        const auto base = renderBothTaps (c, fxSettings (false, 0, 35.0f, false, 0, 35.0f, false),
                                          n, chunk, rate);

        check (base.mainSignature == restingSignature && base.laneSignature == restingSignature,
               std::string ("with FX off neither stage has any state at all on ")
                   + characterName (c));

        for (int t = 0; t < 3; ++t)
        {
            for (const auto amount : { 0.0f, 100.0f })
            {
                // Both trios moved to the same extreme, with FX off on both
                // paths: not one sample may differ, on either tap.
                const auto off = renderBothTaps (c, fxSettings (false, t, amount, false, t, amount, false),
                                                 n, chunk, rate);

                check (worstDifference (base.mainTap, off.mainTap) == 0.0f,
                       std::string ("FX off on the main path is bit-identical at ")
                           + fxTypeName (t) + " " + std::to_string ((int) amount) + " % on "
                           + characterName (c));

                check (worstDifference (base.laneTap, off.laneTap) == 0.0f,
                       std::string ("FX off on the lane path is bit-identical at ")
                           + fxTypeName (t) + " " + std::to_string ((int) amount) + " % on "
                           + characterName (c));

                check (off.mainSignature == restingSignature
                           && off.laneSignature == restingSignature,
                       std::string ("FX off skips the stage rather than running it at zero, at ")
                           + fxTypeName (t) + " " + std::to_string ((int) amount) + " % on "
                           + characterName (c));
            }
        }

        // The other half, and the one that makes the above non-vacuous: each
        // stage on its own reaches its own path and **only** its own path.
        for (int t = 0; t < 3; ++t)
        {
            const auto mainOnly = renderBothTaps (c, fxSettings (true, t, 100.0f, false, t, 100.0f, false),
                                                  n, chunk, rate);
            const auto laneOnly = renderBothTaps (c, fxSettings (false, t, 100.0f, true, t, 100.0f, false),
                                                  n, chunk, rate);

            check (worstDifference (base.mainTap, mainOnly.mainTap) > 1.0e-4f,
                   std::string ("the main's own FX stage reaches the main loop at ")
                       + fxTypeName (t) + " on " + characterName (c));

            check (worstDifference (base.laneTap, laneOnly.laneTap) > 1.0e-4f,
                   std::string ("the lane's own FX stage reaches the lane at ")
                       + fxTypeName (t) + " on " + characterName (c));

            check (worstDifference (base.laneTap, mainOnly.laneTap) == 0.0f,
                   std::string ("the main's FX stage cannot reach the lane at ")
                       + fxTypeName (t) + " on " + characterName (c));

            check (worstDifference (base.mainTap, laneOnly.mainTap) == 0.0f,
                   std::string ("the lane's FX stage cannot reach the main loop at ")
                       + fxTypeName (t) + " on " + characterName (c));
        }
    }
}

/** **The 2c headline again, now with the lane crushed** (`11` §4e1 and §4l).

    2c proved the main loop's tap is bit-identical between a render where SEND
    is held throughout and one where it is never touched. The FX stage is the
    first thing added since that could plausibly break it: it is *in the loop*,
    it holds buffers, and if the two engines shared one the lane's crush would
    be writing into the main delay's memory. So the same assertion is re-run
    with the lane's stage live at the most destructive setting the module has,
    against a main delay running none. */
void testTheMainLoopIsUndisturbedByASendWithFxLive()
{
    constexpr auto rate = 48000.0;
    constexpr int chunk = 384;
    const auto n = (int) (rate * 3.0);

    for (int c = 0; c < 3; ++c)
    {
        const auto sendAt = [chunk] (int offset) { return ((offset / chunk) % 9) < 6; };

        const auto render = [&] (bool toggleSend, std::vector<float>& tap)
        {
            P::DwellDsp dsp;
            dsp.prepare (rate, 512, 2);

            // The lane on Crush at full, the main delay with no FX at all --
            // and `fx_link` off, which is the only way to ask for that.
            auto v = fxSettings (false, 0, 35.0f, true, 2, 100.0f, false);
            v[P::Index::character] = (float) c;
            v[P::Index::laneLevel] = 24.0f;
            v[P::Index::send]      = 0.0f;
            dsp.setParams (v.data(), (int) v.size());

            Block block { n };
            Noise noise;

            for (int i = 0; i < n; ++i)
            {
                block.left[(size_t) i]  = 0.3f * noise.next();
                block.right[(size_t) i] = 0.3f * noise.next();
            }

            std::vector<float> right;

            renderWithTap (dsp, block, n, chunk, false, tap, right,
                           [&] (int offset, int)
                           {
                               v[P::Index::send] = (toggleSend && sendAt (offset)) ? 1.0f : 0.0f;
                               dsp.setParams (v.data(), (int) v.size());
                           });

            return block.left;
        };

        std::vector<float> tapA, tapB;
        const auto quiet   = render (false, tapA);
        const auto toggled = render (true,  tapB);

        check (worstDifference (tapA, tapB) == 0.0f,
               std::string ("the main loop's tap is still bit-identical across a send with the "
                            "lane's FX stage live, on ") + characterName (c));

        check (worstDifference (quiet, toggled) > 0.01f,
               std::string ("the crushed lane was fed and audible on ") + characterName (c));
    }
}

/** **Crush's acceptance: the non-harmonic floor stops growing by repeat 10**
    (10 §11a, `11` §4l).

    Crush is **exempt from §4's -60 dBFS alias floor**, and that exemption is
    the design rather than a concession: the sample-and-hold's images are made
    *inside* the loop and meet §4's 18 kHz cap on the *next* lap, so the cap
    tames them one repeat late instead of preventing them. Deliberate aliasing
    with a level is not a fault; deliberate aliasing that compounds without
    bound is. So what is asserted is that it **converges**.

    The measurement is relative, and it has to be: at FEEDBACK 97 the loop is at
    unity FX-off, but the hold is a low-pass with teeth, so the loop runs a
    little under unity with it in and an absolute floor would fall with the
    whole signal and pass for the wrong reason. What is tracked is the worst
    non-harmonic probe **against the fundamental in the same window**.

    A 1 kHz tone is used so the harmonic grid is coarse and easy to sit off: the
    five probes are 350 Hz from the nearest harmonic, which at this window is
    seventeen bins of Hann sidelobe away -- far enough that what they read is
    the crush and not the tone. */
void testCrushFloorStopsGrowing()
{
    constexpr auto rate = 48000.0;
    constexpr auto toneHz = 1000.0;
    const auto lap = (int) (rate * 0.1);
    const auto burst = lap / 2;
    // AMOUNT 37 % puts the hold divisor on 13 -- `ceil(1 + 0.37 . 31)` -- so
    // the sample-and-hold runs at 48000/13 = 3692.31 Hz, which is **not** a
    // multiple of the tone. That is the reason for the odd figure: at AMOUNT
    // 50 the divisor is 16 and the hold rate is exactly 3 kHz, so every image
    // lands on a harmonic of the 1 kHz tone and there is no non-harmonic
    // content left to measure. The probes are the first four images,
    // `|k . f_hold +- f_tone|`, each at least 300 Hz -- fifteen bins of this
    // window -- from the nearest harmonic.
    constexpr auto holdHz = 48000.0 / 13.0;
    const double probes[] { holdHz - toneHz, holdHz + toneHz,
                            2.0 * holdHz - toneHz, 2.0 * holdHz + toneHz };

    for (int c = 0; c < 3; ++c)
    {
        P::DwellDsp dsp;
        dsp.prepare (rate, 512, 2);

        auto v = settings (c, (float) ((double) lap * 1000.0 / rate), 97.0f, 100.0f);
        v[P::Index::fx]       = 1.0f;
        v[P::Index::fxType]   = 2.0f;   // Crush
        v[P::Index::fxAmount] = 37.0f;
        dsp.setParams (v.data(), (int) v.size());

        const auto n = lap * 18;
        Block block { n };

        for (int i = 0; i < burst; ++i)
        {
            const auto w = 0.5 - 0.5 * std::cos (2.0 * P::kPiD * (double) i / (double) burst);
            const auto s = (float) (0.5 * w * std::sin (2.0 * P::kPiD * toneHz * (double) i / rate));
            block.left[(size_t) i]  = s;
            block.right[(size_t) i] = s;
        }

        renderInChunks (dsp, block, n, 512);

        const auto relativeFloorAt = [&] (int repeat)
        {
            auto worst = 0.0;

            for (const auto hz : probes)
                worst = std::max (worst, magnitudeAt (block.left, repeat * lap, burst, hz, rate));

            const auto tone = magnitudeAt (block.left, repeat * lap, burst, toneHz, rate);

            return 20.0 * std::log10 (std::max (worst, 1.0e-30) / std::max (tone, 1.0e-30));
        };

        // Repeat 1 is the input arriving late and **has never been crushed** --
        // the stage is in `C(y)`, the feedback chain, so the first lap through
        // it lands on repeat 2. That is where the measurement starts.
        const auto atTwo = relativeFloorAt (2);
        const auto atTen = relativeFloorAt (10);

        auto after = atTen;

        for (int r = 11; r <= 17; ++r)
            after = std::max (after, relativeFloorAt (r));

        std::printf ("      crush floor %-15s repeat 2: %7.2f dB, repeat 10: %7.2f dB, "
                     "worst 11-17: %7.2f dB\n",
                     characterName (c), atTwo, atTen, after);

        // 0.5 dB of slack, the figure `11` §4e4 already uses for the lane's own
        // convergence, because this is the same kind of claim: not that the
        // number is small -- §11a exempts Crush from the -60 dBFS floor
        // precisely because it will not be -- but that it has stopped moving.
        check (after <= atTen + 0.5,
               std::string ("crush's non-harmonic floor stops growing by repeat 10 on ")
                   + characterName (c) + " (repeat 10 " + std::to_string (atTen)
                   + " dB, worst after " + std::to_string (after) + " dB)");

        // What the measurement actually found, and it is stronger than §11a
        // asks for: the floor is **level from the first crushed repeat**, not
        // merely level by the tenth. The hold's images are a fixed ratio of
        // whatever is circulating, so they ride the tail down rather than
        // building on it -- which is the thing "every candidate compounds per
        // repeat" was the risk of.
        check (std::abs (atTen - atTwo) <= 0.5,
               std::string ("crush's floor is level from the first crushed repeat on ")
                   + characterName (c) + " (repeat 2 " + std::to_string (atTwo)
                   + " dB, repeat 10 " + std::to_string (atTen) + " dB)");

        // Non-vacuous: there has to be real non-harmonic content for the
        // convergence to be about. Anything near the -60 dBFS an FX-off render
        // reads would mean the probes had missed the images and this test was
        // measuring the quantiser's dither instead.
        check (atTen > -40.0,
               std::string ("crush really is making non-harmonic content to converge, on ")
                   + characterName (c) + " (" + std::to_string (atTen) + " dB)");
    }
}

/** **`fx_link` ties the lane's trio to the main's, and ties nothing else**
    (`11` §4e7, 10 §11.3).

    Three claims, and the third is the one that is easy to get wrong:

    1. **On, the lane's own three rows cannot reach the audio.** Two renders
       with `lane_fx`, `lane_fx_type` and `lane_fx_amount` at opposite extremes
       must null bit-exactly -- the lane's stage reads the main's three, so its
       own values are ignored rather than overwritten.
    2. **Off, they reach it** -- which is the case the parameter exists for: a
       thrown word crushed against a clean main delay.
    3. **Nothing is seeded either way.** The lane's three values are still
       exactly what the host set after a pass with the tie on, so there is no
       automation pass that rewrites parameters and `11` §4e7's "automating
       `fx_link` writes no parameters at all" is simply true of this module. */
void testFxLinkTiesTheLanesTrio()
{
    constexpr auto rate = 48000.0;
    constexpr int chunk = 384;
    const auto n = (int) (rate * 2.0);

    for (int c = 0; c < 3; ++c)
    {
        // Linked, with the main delay running Diffuse: the lane must follow it
        // whatever its own rows say.
        const auto linkedA = renderBothTaps (c, fxSettings (true, 0, 35.0f, false, 0, 0.0f, true),
                                             n, chunk, rate);
        const auto linkedB = renderBothTaps (c, fxSettings (true, 0, 35.0f, true, 2, 100.0f, true),
                                             n, chunk, rate);

        check (worstDifference (linkedA.laneTap, linkedB.laneTap) == 0.0f,
               std::string ("with FX LINK on the lane's own trio cannot leak, on ")
                   + characterName (c));

        check (worstDifference (linkedA.mainTap, linkedB.mainTap) == 0.0f,
               std::string ("with FX LINK on the lane's own trio cannot reach the main either, on ")
                   + characterName (c));

        // And the tie is real rather than both stages simply being off: with
        // the main's stage running, the lane's tap must differ from a render
        // where the main's stage is off too.
        const auto unlit = renderBothTaps (c, fxSettings (false, 0, 35.0f, false, 0, 0.0f, true),
                                           n, chunk, rate);

        check (worstDifference (unlit.laneTap, linkedA.laneTap) > 1.0e-4f,
               std::string ("FX LINK on makes the lane follow the main's stage, on ")
                   + characterName (c));

        // Released: now the lane's own three are what it runs, and the main is
        // untouched by them -- **a thrown word crushed against a clean main
        // delay**, which is the case the parameter exists for.
        const auto freeA = renderBothTaps (c, fxSettings (false, 0, 35.0f, false, 0, 0.0f, false),
                                           n, chunk, rate);
        const auto freeB = renderBothTaps (c, fxSettings (false, 0, 35.0f, true, 2, 100.0f, false),
                                           n, chunk, rate);

        check (worstDifference (freeA.laneTap, freeB.laneTap) > 1.0e-4f,
               std::string ("with FX LINK off the lane's own trio reaches the audio, on ")
                   + characterName (c));

        check (worstDifference (freeA.mainTap, freeB.mainTap) == 0.0f,
               std::string ("with FX LINK off the lane's crush still leaves the main clean, on ")
                   + characterName (c));

        // Nothing was seeded while the tie was on: the lane's three rows come
        // back out of the core exactly as the host set them.
        {
            P::DwellDsp dsp;
            dsp.prepare (rate, 512, 2);

            auto linked = fxSettings (true, 0, 35.0f, true, 2, 100.0f, true);
            linked[P::Index::character] = (float) c;
            dsp.setParams (linked.data(), (int) linked.size());

            Block warm { 4096 };
            renderInChunks (dsp, warm, 4096, 512);

            const auto& p = dsp.getCore().getParams();

            check (p.laneFx && p.laneFxTypeChoice == 2 && p.laneFxAmountPct == 100.0f,
                   std::string ("FX LINK leaves the lane's own three rows exactly as they were, on ")
                       + characterName (c));
        }
    }
}

//==============================================================================
// The review's fixes, 2026-10-01. **Every test below moves a parameter, or
// feeds something hostile, while there is signal in the loop, and then
// measures what came out** -- the one shape of test the file did not have, and
// the shape every defect the review found needed in order to be seen.
//==============================================================================

/** Renders the way a host does: `setParams`, then `setTempo`, then `process`,
    every block, with `before (offset)` free to move a value in `v` first. */
template <typename BeforeBlock>
void renderAsHost (P::DwellDsp& dsp, std::vector<float>& v, Block& block, int n, int chunk,
                   BeforeBlock&& before, double bpm = 0.0, bool tempoValid = false)
{
    for (int offset = 0; offset < n; offset += chunk)
    {
        const auto count = std::min (chunk, n - offset);

        before (offset);
        dsp.setParams (v.data(), (int) v.size());
        dsp.setTempo (bpm, tempoValid, tempoValid);

        float* channels[] { block.left.data() + offset, block.right.data() + offset };
        dsp.process (channels, 2, count);
    }
}

/** **One non-finite input sample with DUCK up must not silence the module.**

    The ducker's key filter and its follower run on the dry input, and a
    one-pole or a log follower handed a NaN keeps it: the gain reduction became
    NaN, the output guard turned every sample into 0 -- dry included -- and
    only a `reset` brought it back. `testSilenceDenormalsAndNaN` runs at DUCK
    0, where the ducker is branched past, and so could not see it.

    The render with the bad sample is held against the same render without it:
    the one sample that was not a number is replaced, so a second later the two
    must agree to the level. */
void testANonFiniteInputWithDuckUpRecovers()
{
    constexpr auto rate = 48000.0;
    const auto n = (int) (rate * 2.0);

    for (const auto duck : { 6.0f, 24.0f })
    {
        for (const auto bad : { std::numeric_limits<float>::quiet_NaN(),
                                std::numeric_limits<float>::infinity() })
        {
            const auto render = [&] (bool spoil)
            {
                P::DwellDsp dsp;
                dsp.prepare (rate, 512, 2);

                auto v = defaults();
                v[P::Index::duck] = duck;

                Block block { n };

                for (int i = 0; i < n; ++i)
                {
                    const auto s = (float) (0.1 * std::sin (2.0 * P::kPiD * 440.0 * (double) i / rate));
                    block.left[(size_t) i] = block.right[(size_t) i] = s;
                }

                if (spoil)
                    block.left[1000] = bad;

                renderAsHost (dsp, v, block, n, 512, [] (int) {});
                return block.left;
            };

            const auto clean = render (false);
            const auto spoilt = render (true);

            const auto want = rms (clean, n - (int) rate, (int) rate);
            const auto got  = rms (spoilt, n - (int) rate, (int) rate);

            char buf[160];
            std::snprintf (buf, sizeof (buf),
                           "one %s input sample at DUCK %.0f dB: the last second is back to the "
                           "clean render's level (%.6f, want %.6f)",
                           std::isnan (bad) ? "NaN" : "infinite", (double) duck, got, want);

            check (allFinite (spoilt) && want > 0.01
                       && std::abs (20.0 * std::log10 (std::max (got, 1.0e-30) / want)) < 0.01,
                   buf);
        }
    }
}

/** **CHARACTER moved while a repeat is in flight replays it at its own level.**

    Bucket-brigade's compressor writes its gain into a ring beside the audio,
    and the expander divides the read by it. The expander ran only while the
    character *was* bucket-brigade, so a move to Clean or Tape left the ring's
    companded samples -- up to +30 dB on a quiet line -- with nothing dividing
    them back: measured on AURORA, a repeat of a 0.05 tone came back at a 0.558
    peak, -14.1 dBFS against -29.0.

    FEEDBACK 0 so the repeat is the raw read and nothing else: the switched
    render's repeat must match the render that never switched. The reverse move
    is the control -- a ring written without companding holds gains of exactly
    1.0, so the expander arriving has nothing to undo. */
void testACharacterMoveReplaysTheRingAtItsOwnLevel()
{
    constexpr auto rate = 48000.0;
    const auto n = (int) rate;
    const auto switchAt = (int) (0.25 * rate) / 512 * 512;
    const auto from = (int) (0.31 * rate), count = (int) (0.08 * rate);

    const auto render = [&] (int start, int moveTo)
    {
        P::DwellDsp dsp;
        dsp.prepare (rate, 512, 2);

        auto v = defaults();
        v[P::Index::character] = (float) start;
        v[P::Index::time]      = 300.0f;
        v[P::Index::feedback]  = 0.0f;
        v[P::Index::mix]       = 100.0f;

        Block block { n };

        for (int i = 0; i < (int) (0.1 * rate); ++i)
        {
            const auto s = (float) (0.05 * std::sin (2.0 * P::kPiD * 440.0 * (double) i / rate));
            block.left[(size_t) i] = block.right[(size_t) i] = s;
        }

        renderAsHost (dsp, v, block, n, 512, [&] (int offset)
        {
            if (offset >= switchAt)
                v[P::Index::character] = (float) moveTo;
        });

        return block.left;
    };

    const std::pair<int, int> moves[] { { 2, 0 }, { 2, 1 }, { 0, 2 } };

    for (const auto& [start, moveTo] : moves)
    {
        const auto stayed = render (start, start);
        const auto moved  = render (start, moveTo);

        const auto want = rms (stayed, from, count);
        const auto got  = rms (moved, from, count);
        const auto db   = 20.0 * std::log10 (std::max (got, 1.0e-30) / std::max (want, 1.0e-30));

        char buf[200];
        std::snprintf (buf, sizeof (buf),
                       "%s to %s with a repeat in flight: the repeat comes back at its own level "
                       "(%+.2f dB against the render that stayed, peak %.4f)",
                       characterName (start), characterName (moveTo), db,
                       (double) peakOf (moved, from, count));

        check (want > 0.01 && std::abs (db) < 0.1
                   && peakOf (moved, from, count) < 1.02f * peakOf (stayed, from, count),
               buf);
    }
}

/** **MIX crossing 50 % inside one block is smoothed like the rest of its
    travel** (10 §9).

    Below the hinge the dry is unity and the wet rides `sin(pi m)`; above it
    the wet is unity and the dry rides `cos(pi (m - 0.5))`. Each side was
    smoothed, but the crossing was not: going down the dry gain was snapped to
    1 and going up the smoothed wet gain was dropped, so a jump across the
    hinge stepped the output by most of the signal -- 0.433 on a 0.5 sine
    whose own largest step is 0.0033, measured on AURORA.

    A 50 Hz sine at 0.5, the jump at a block edge, and the largest
    sample-to-sample step around it held to the programme's own plus 0.002.
    The control is a jump that does not cross. And after the downward crossing
    has settled, the dry is bit-exact again -- the null §9 asks for below the
    hinge has to come back, not merely get close. */
void testMixAcrossTheHingeIsSmoothed()
{
    constexpr auto rate = 48000.0;
    constexpr int n = 48000;
    constexpr int at = 512 * 40;
    const auto ownStep = 0.5 * 2.0 * P::kPiD * 50.0 / rate;

    struct Jump { float from, to, timeMs, feedback; const char* name; };

    const Jump jumps[]
    {
        { 100.0f, 35.0f, 2000.0f, 35.0f, "MIX 100 to 35 (down across the hinge)" },
        {   0.0f, 100.0f,  10.0f,  0.0f, "MIX 0 to 100 (up across the hinge, a repeat sounding)" },
        { 100.0f, 60.0f, 2000.0f, 35.0f, "MIX 100 to 60 (the control: no crossing)" },
    };

    for (const auto& j : jumps)
    {
        P::DwellDsp dsp;
        dsp.prepare (rate, 512, 2);

        auto v = defaults();
        v[P::Index::time]     = j.timeMs;
        v[P::Index::feedback] = j.feedback;

        Block block { n };

        for (int i = 0; i < n; ++i)
            block.left[(size_t) i] = block.right[(size_t) i]
                = (float) (0.5 * std::sin (2.0 * P::kPiD * 50.0 * (double) i / rate));

        const auto input = block.left;

        renderAsHost (dsp, v, block, n, 512, [&] (int offset)
        {
            v[P::Index::mix] = offset >= at ? j.to : j.from;
        });

        auto worst = 0.0;

        for (int i = at - 2048; i < at + 2048; ++i)
            worst = std::max (worst, (double) std::abs (block.left[(size_t) i] - block.left[(size_t) (i - 1)]));

        char buf[200];
        std::snprintf (buf, sizeof (buf), "%s: largest step %.4f against the programme's own %.4f",
                       j.name, worst, ownStep);

        check (worst <= ownStep + 0.002, buf);

        // TIME 2000 puts the first repeat past the render, so below the hinge
        // the output is the input itself once the gains have landed.
        if (j.to <= 50.0f && j.timeMs >= 2000.0f)
        {
            auto exact = true;

            for (int i = n - 4096; i < n; ++i)
                exact = exact && block.left[(size_t) i] == input[(size_t) i];

            check (exact, std::string (j.name) + ": once settled the dry is bit-exact again");
        }
    }
}

/** **With SYNC on, the first tempo after `prepare` or `reset` lands the synced
    time; it does not glide there from the TIME knob.**

    The first block's `setParams` primes the engines at the knob's time, and
    the tempo arrives after it in the same block. Handed over as an ordinary
    move, the knob-to-note distance then went through tape's and
    bucket-brigade's rate-limited glide: measured on AURORA, a 1/4 at 60 bpm
    from the 375 ms default took three seconds to arrive, on every fresh
    instance -- a session load, an offline bounce, and in a rack every chain
    edit, since a rebuild makes a new DSP. `testSyncFollowsTheHostTempo` reads
    the parameters handed to the core and could not see the engines' delay.

    Asserted on the engines' own read delay, after the first block, for both
    engines on every character, and again after a `reset`. A tempo change
    *later* is a move like any other and glides on tape (as designed); that is
    pinned too, so the landing cannot spread to every tempo change. */
void testTheFirstTempoLandsTheSyncedTime()
{
    constexpr auto rate = 48000.0;

    for (int c = 0; c < 3; ++c)
    {
        P::DwellDsp dsp;
        dsp.prepare (rate, 512, 2);

        auto v = defaults();
        v[P::Index::character] = (float) c;
        v[P::Index::sync]      = 1.0f;
        v[P::Index::note]      = 9.0f;    // 1/4: 1000 ms at 60 bpm
        // LANE NOTE stays at its default 1/8: 500 ms at 60 bpm.

        Block block { 512 * 4 };
        renderAsHost (dsp, v, block, 512, 512, [] (int) {}, 60.0, true);

        const auto main = dsp.getCore().getMainEngine().currentDelaySamples();
        const auto lane = dsp.getCore().getLaneEngine().currentDelaySamples();

        checkClose (main, 48000.0, 1.0e-6,
                    std::string ("the first tempo lands the main delay on 1/4 at 60 bpm after one block, on ")
                        + characterName (c));
        checkClose (lane, 24000.0, 1.0e-6,
                    std::string ("the first tempo lands the lane on 1/8 at 60 bpm after one block, on ")
                        + characterName (c));

        // A reset is a fresh start as far as the ring is concerned, so the
        // first tempo after it lands too.
        dsp.reset();
        renderAsHost (dsp, v, block, 512, 512, [] (int) {}, 120.0, true);

        checkClose (dsp.getCore().getMainEngine().currentDelaySamples(), 24000.0, 1.0e-6,
                    std::string ("the first tempo after a reset lands as well, on ") + characterName (c));

        // And a later change is a move: on the gliding characters it is still
        // on its way one block later.
        renderAsHost (dsp, v, block, 512, 512, [] (int) {}, 60.0, true);

        const auto later = dsp.getCore().getMainEngine().currentDelaySamples();

        if (c == 0)
            check (later == 24000.0 || later == 48000.0,
                   "a later tempo change on clean crossfades between the two times");
        else
            check (later > 24000.0 && later < 48000.0,
                   std::string ("a later tempo change still glides on ") + characterName (c)
                       + " (" + std::to_string (later) + " samples after one block)");

        // The landing is only taken while the ring is empty: a host that sends
        // no tempo until audio has flowed gets an ordinary move, not a jump.
        if (c != 0)
        {
            P::DwellDsp late;
            late.prepare (rate, 512, 2);

            renderAsHost (late, v, block, 512, 512, [] (int) {}, 0.0, false);
            renderAsHost (late, v, block, 512, 512, [] (int) {}, 60.0, true);

            const auto moved = late.getCore().getMainEngine().currentDelaySamples();

            check (moved > 18000.0 && moved < 48000.0,
                   std::string ("a first tempo that arrives after audio has flowed glides on ")
                       + characterName (c) + " (" + std::to_string (moved) + " samples)");
        }
    }
}

/** **Crush in the loop decays to silence** (10 §11a: every in-loop FX is
    non-expanding, `|F| <= 1`).

    Rounding to the nearest step can make a value larger -- at 3 bits a 0.13
    becomes 0.25 -- so with FEEDBACK above about 60 % the loop settled into a
    limit cycle instead of decaying: measured on AURORA at AMOUNT 100 and
    FEEDBACK 80 the repeats were still -7.1 dB under the first one forty
    seconds later, and even AMOUNT 0's 16-bit quantiser held a -81 dB residue
    for good. `testCrushFloorStopsGrowing` measures the floor *relative to the
    tone* and so could not see the tone failing to leave.

    A 50 ms burst, then twenty seconds of silence, on every character: the last
    second must be exact zeros. */
void testCrushInTheLoopDecaysToSilence()
{
    constexpr auto rate = 48000.0;
    const auto n = (int) (rate * 20.0);
    const auto burst = (int) (0.05 * rate);

    for (int c = 0; c < 3; ++c)
    {
        for (const auto feedback : { 80.0f, 90.0f })
        {
            for (const auto amount : { 0.0f, 35.0f, 60.0f, 100.0f })
            {
                P::DwellDsp dsp;
                dsp.prepare (rate, 512, 2);

                auto v = settings (c, 100.0f, feedback, 100.0f);
                v[P::Index::fx]       = 1.0f;
                v[P::Index::fxType]   = 2.0f;   // Crush
                v[P::Index::fxAmount] = amount;

                Block block { n };

                for (int i = 0; i < burst; ++i)
                {
                    const auto w = 0.5 - 0.5 * std::cos (2.0 * P::kPiD * (double) i / (double) burst);
                    block.left[(size_t) i] = block.right[(size_t) i]
                        = (float) (0.5 * w * std::sin (2.0 * P::kPiD * 300.0 * (double) i / rate));
                }

                renderAsHost (dsp, v, block, n, 512, [] (int) {});

                auto zeros = true;

                for (int i = n - (int) rate; i < n; ++i)
                    zeros = zeros && block.left[(size_t) i] == 0.0f && block.right[(size_t) i] == 0.0f;

                const auto lastSecond = rms (block.left, n - (int) rate, (int) rate);
                const auto firstRepeat = rms (block.left, (int) (0.1 * rate), burst);

                char buf[200];
                std::snprintf (buf, sizeof (buf),
                               "Crush at AMOUNT %.0f, FEEDBACK %.0f on %s decays to exact zeros "
                               "(last second %.1f dB under the first repeat)",
                               (double) amount, (double) feedback, characterName (c),
                               -20.0 * std::log10 (std::max (lastSecond, 1.0e-30) / std::max (firstRepeat, 1.0e-30)));

                check (zeros, buf);
            }
        }
    }
}

/** How long `v` really rings after a burst ends: the time from the burst's
    last sample to the last output sample, either channel, above -60 dB of the
    burst's peak -- the host's meaning of a tail, the time it has to keep
    processing after its input has gone silent. Rendered for `seconds` after
    the burst; a render still above the line in its last 100 ms returns
    `seconds`, so a figure that ran out of render cannot pass for a short one.

    Three inputs, all peaking at 0.5: one sample, which reaches every band the
    loop passes; 5 ms of 300 Hz under a Hann window; 50 ms of windowed noise.

    **The tail is the loop's own decay, so no input is longer than one lap**
    (`checkTailRows` skips a burst that would be). A burst that overlaps its
    own repeats builds a high-FEEDBACK loop up above the level it went in at,
    and the -60 dB line, drawn from the input, then measures the build-up as
    well as the decay: measured on AURORA, tape at TIME 1 ms and FEEDBACK
    96.9 % rings 2.95 s after one sample and 4.57 s after 50 ms of noise. The
    figure is §9's -- laps from the first repeat -- and is not that. */
enum class TailInput { impulse, tone, noise };

double measuredTailSeconds (std::vector<float> v, TailInput input, double seconds)
{
    constexpr auto rate = 48000.0;
    const auto burst = input == TailInput::noise ? (int) (0.05 * rate)
                     : input == TailInput::tone  ? (int) (0.005 * rate)
                                                 : 1;
    const auto n = burst + (int) std::ceil (seconds * rate);

    P::DwellDsp dsp;
    dsp.prepare (rate, 512, 2);

    Block block { n };
    Noise source;
    auto peak = 0.0f;

    for (int i = 0; i < burst; ++i)
    {
        const auto w = burst > 1 ? 0.5 - 0.5 * std::cos (2.0 * P::kPiD * (double) i / (double) (burst - 1)) : 1.0;
        const auto s = input == TailInput::noise ? (float) (0.5 * w * source.next())
                     : input == TailInput::tone  ? (float) (0.5 * w * std::sin (2.0 * P::kPiD * 300.0 * (double) i / rate))
                                                 : 0.5f;
        block.left[(size_t) i] = block.right[(size_t) i] = s;
        peak = std::max (peak, std::abs (s));
    }

    renderAsHost (dsp, v, block, n, 512, [] (int) {});

    const auto line = peak * 1.0e-3f;
    auto last = -1;

    for (int i = n - 1; i >= 0 && last < 0; --i)
        if (std::abs (block.left[(size_t) i]) > line || std::abs (block.right[(size_t) i]) > line)
            last = i;

    if (last >= n - (int) (0.1 * rate))
        return seconds;

    return (double) (last + 1 - burst) / rate;
}

/** **The tail Dwell reports is never shorter than the decay it describes**
    (`dsp/Timing.h`; 10 §9, §11.6; `11` §4j).

    `testTheTailIsTheLongerEngine` checks the arithmetic against itself. This
    renders the loop and times it: a burst, then the time to the last sample
    above -60 dB, against `tailSecondsFor` -- rendered for the reported figure
    plus a lap and half a second, so a repeat arriving after the figure is
    seen. A figure at the 30 s ceiling is the ceiling (it stands, by decision)
    and is not rendered.

    The rows are the review's (AURORA, 2026-10-01) plus the defaults, on every
    character. */
struct TailRow { int character; float feedback, timeMs; bool fx; int fxType; float fxAmount; };

void checkTailRows (const std::vector<TailRow>& rows, const char* what)
{
    for (const auto& r : rows)
    {
        auto v = settings (r.character, r.timeMs, r.feedback, 100.0f);
        v[P::Index::fx]       = r.fx ? 1.0f : 0.0f;
        v[P::Index::fxType]   = (float) r.fxType;
        v[P::Index::fxAmount] = r.fxAmount;

        const auto reported = P::tailSecondsFor (v.data(), (int) v.size());

        if (reported >= P::kTailCeilingSeconds)
        {
            check (reported == P::kTailCeilingSeconds,
                   std::string (what) + ": " + characterName (r.character) + ", FEEDBACK "
                       + std::to_string (r.feedback) + ", TIME " + std::to_string ((int) r.timeMs)
                       + " ms reports the 30 s ceiling, which stands");
            continue;
        }

        for (const auto input : { TailInput::impulse, TailInput::tone, TailInput::noise })
        {
            const auto lengthMs = input == TailInput::noise ? 50.0f : (input == TailInput::tone ? 5.0f : 0.0f);

            if (lengthMs > r.timeMs)
                continue;

            const auto window = reported + (double) r.timeMs * 0.001 + 0.5;
            const auto measured = measuredTailSeconds (v, input, window);

            char buf[240];
            std::snprintf (buf, sizeof (buf),
                           "%s: %s, FEEDBACK %.1f, TIME %.0f ms%s, %s: reported %.3f s, "
                           "measured %s%.3f s to -60 dB",
                           what, characterName (r.character), (double) r.feedback, (double) r.timeMs,
                           r.fx ? (std::string (", FX ") + fxTypeName (r.fxType) + " "
                                   + std::to_string ((int) r.fxAmount)).c_str() : "",
                           input == TailInput::noise ? "noise burst"
                               : (input == TailInput::tone ? "300 Hz burst" : "one sample"),
                           reported, measured >= window ? "> " : "", measured);

            check (measured <= reported, buf);
        }
    }
}

void testTheReportedTailIsNeverShorterThanTheDecay()
{
    std::vector<TailRow> rows;

    // The review's rows, the defaults, and the corners where the loop's own
    // filters delay each lap: bucket-brigade at long TIME, where its clock
    // puts the two Butterworths at 800 Hz, and tape at the shortest TIME,
    // where the head bump puts the loop's peak at 63 Hz among the high-passes.
    for (int c = 0; c < 3; ++c)
        for (const auto& [fb, t] : std::initializer_list<std::pair<float, float>> {
                 { 35.0f, 375.0f }, { 90.0f, 375.0f }, { 90.0f, 100.0f }, { 96.0f, 50.0f },
                 { 96.0f, 100.0f }, { 96.9f, 20.0f }, { 96.9f, 100.0f }, { 96.0f, 5.0f },
                 { 60.0f, 375.0f }, { 60.0f, 1000.0f }, { 96.9f, 1.0f } })
            rows.push_back ({ c, fb, t, false, 0, 0.0f });

    checkTailRows (rows, "the tail is never short");
}

} // namespace

//==============================================================================
int main()
{
    testSchemaIsWhatItWillAlwaysBe();
    testChoiceListsKeepTheirOrder();
    testEveryParameterIsWiredToItsOwnValue();
    testSyncFollowsTheHostTempo();
    testTheTailIsTheLongerEngine();
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

    // Stage 2c: the lane's gates.
    testTheMainLoopIsUndisturbedByASend();
    testHoldOffClearsTheLane();
    testChopIsNonDestructive();
    testTheLaneIsBoundedByItsClip();
    testUnityHoldsAtTheLaneDetent();
    testTheSendRampOpensAndCloses();

    // Stage 2d: ducking and the stereo modes.
    testDuckIsAfterTheLoopTapAndNeverTouchesTheLane();
    testTheStereoModes();

    // Stage 2e: the in-loop FX stage.
    testFxOffIsBitIdenticalOnBothPaths();
    testTheMainLoopIsUndisturbedByASendWithFxLive();
    testCrushFloorStopsGrowing();
    testFxLinkTiesTheLanesTrio();

    // The review's fixes, 2026-10-01.
    testANonFiniteInputWithDuckUpRecovers();
    testACharacterMoveReplaysTheRingAtItsOwnLevel();
    testMixAcrossTheHingeIsSmoothed();
    testTheFirstTempoLandsTheSyncedTime();
    testCrushInTheLoopDecaysToSilence();
    testTheReportedTailIsNeverShorterThanTheDecay();

    std::printf ("%d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
