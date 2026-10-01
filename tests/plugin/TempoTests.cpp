/*
    The host's tempo, handed down to every module once per block.

    `ModuleDsp::setTempo` is a new virtual on every module's vtable, defaulted
    to do nothing, and both processors now read the host's playhead once per
    block and pass what they find through `ModuleEngine::process`. Nothing in
    the suite consumes it yet, so this file is the only thing that can tell
    whether the plumbing works -- and, as important, whether it cost the
    modules that ignore it anything at all.

    Three parts:

    - **The contract, through the standalone processor.** A probe module
      records every call it is handed, in order. The tempo has to arrive
      exactly once per block, after `setParams` and before `process`, carrying
      the host's figure exactly, and the three ways a host can fail to give one
      -- no playhead, no position, no tempo -- all have to arrive as the same
      "no tempo": bpm 0.0, valid false, playing false. A stopped transport is
      not one of them: it keeps its tempo and says it is stopped.

    - **The rack.** Every slot is handed the same three values in the same
      block, read once from the host, so two synced modules cannot disagree;
      and after a chain edit, which rebuilds every slot's DSP, each new DSP
      is handed the tempo before its first process.

    - **Nothing else moved.** Every registered module, and a rack holding all
      of them, is rendered with no playhead and again with a playhead at
      120 bpm, and the two outputs are compared byte for byte. **This one is a
      comparison between two runs, which the house usually refuses** (see
      tests/dsp/OptoDspTests.cpp and tests/plugin/BusTests.cpp), and it is
      allowed here for a specific reason: the claim under test IS "the same
      with and without", and the absolute numbers are already pinned by
      bus_tests' goldens, which this change has to leave green untouched. A
      determinism control -- the same render twice with no playhead -- comes
      first, so a module that is not repeatable cannot pass the comparison by
      accident or fail it for a reason that is not the tempo.
*/

#include "TestUtil.h"
#include "core/product/SingleModuleProcessor.h"
#include "core/rack/RackProcessor.h"
#include "products/rack/Product.h"
#include "products/rack/Registry.h"

#if BMO_TEMPO_TESTS_TUNE
 #include "modules/tune/Module.h"
#endif

#include <algorithm>
#include <cmath>
#include <cstring>
#include <functional>
#include <limits>

using namespace test;
using bmo::RackProcessor;

namespace
{

constexpr double kRate   = 48000.0;
constexpr int    kBlock  = 512;
constexpr int    kBlocks = 16;
constexpr int    kLength = kBlock * kBlocks;

/** Every assertion in this file goes through here, so the suite can say how
    many it made: a count that drops is a test that stopped running. */
int checksMade = 0;

void expect (bool condition, const juce::String& what)
{
    ++checksMade;
    check (condition, what);
}

//== A host's playhead ========================================================
/** Answers whatever the test last told it to. `position` empty is a host
    with a playhead that has no position to give. */
struct FakePlayHead final : juce::AudioPlayHead
{
    juce::Optional<PositionInfo> position;

    juce::Optional<PositionInfo> getPosition() const override { return position; }

    void set (juce::Optional<double> bpm, bool playing)
    {
        PositionInfo info;
        info.setBpm (bpm);
        info.setIsPlaying (playing);
        position = info;
    }
};

//== A module that writes down what it is handed ==============================
enum class Call { params, tempo, process };

struct Event
{
    int    probe;          // which probe instance, in construction order
    Call   call;
    double bpm     = 0.0;
    bool   valid   = false;
    bool   playing = false;
};

/** One log for every probe alive, in the order the calls happened: the order
    across two rack slots is part of what is asserted. */
std::vector<Event> events;
int probesMade = 0;

struct ProbeDsp final : bmo::ModuleDsp
{
    const int id = probesMade++;

    void prepare (double, int, int) override {}
    void reset() override {}
    void setParams (const float*, int) override { events.push_back ({ id, Call::params }); }

    void setTempo (double bpm, bool valid, bool playing) noexcept override
    {
        events.push_back ({ id, Call::tempo, bpm, valid, playing });
    }

    void process (float* const*, int, int) override { events.push_back ({ id, Call::process }); }
    int latencyForParams (const float*, int) const override { return 0; }
};

const bmo::ModuleDef& probeModule()
{
    static const bmo::ParamSpecs specs { bmo::ParamSpec::floatParam ("unused", "Unused", 0.0f, 1.0f, 0.0f, 1.0f) };
    static const std::vector<bmo::FactoryPreset> presets { { "Init", {} } };

    static const bmo::ModuleDef def {
        "probe", "Probe", 1, 160, juce::Colours::grey, specs, presets,
        [] { return std::make_unique<ProbeDsp>(); },
        {} };       // no panel: nothing here opens an editor

    return def;
}

//== Building and running things ==============================================
std::unique_ptr<bmo::SingleModuleProcessor> makeProduct (const bmo::ModuleDef& def)
{
    bmo::ProductInfo info { def.name, { def.name, ".bmotest" }, 1, 1 };
    auto proc = std::make_unique<bmo::SingleModuleProcessor> (def, info);
    proc->setPlayConfigDetails (2, 2, kRate, kBlock);
    proc->prepareToPlay (kRate, kBlock);
    return proc;
}

/** One block of silence through `proc`, with the log cleared first, so what
    is in `events` afterwards is that block and nothing else -- prepare's own
    setParams included. */
void oneBlock (juce::AudioProcessor& proc)
{
    juce::AudioBuffer<float> buffer (2, kBlock);
    buffer.clear();
    juce::MidiBuffer midi;

    events.clear();
    proc.processBlock (buffer, midi);
}

/** The tempo events in the log, in order. */
std::vector<Event> tempoEvents()
{
    std::vector<Event> out;

    for (const auto& e : events)
        if (e.call == Call::tempo)
            out.push_back (e);

    return out;
}

/** Each probe's own calls in this block were exactly params, tempo, process,
    in that order, once each -- whatever the other probes' calls are
    interleaved with. */
void expectOrder (const juce::String& where)
{
    std::vector<int> seen;

    for (const auto& e : events)
        if (std::find (seen.begin(), seen.end(), e.probe) == seen.end())
            seen.push_back (e.probe);

    expect (! seen.empty(), where + ": a probe was called at all");

    for (const auto id : seen)
    {
        std::vector<Call> calls;

        for (const auto& e : events)
            if (e.probe == id)
                calls.push_back (e.call);

        expect (calls == std::vector<Call> { Call::params, Call::tempo, Call::process },
                where + ": probe " + juce::String (id)
                    + " was called params, tempo, process, once each and in that order ("
                    + juce::String ((int) calls.size()) + " calls)");
    }
}

/** The one tempo the block carried, for a processor holding one probe. */
void expectTempo (double bpm, bool valid, bool playing, const juce::String& where)
{
    const auto t = tempoEvents();
    expect (t.size() == 1, where + ": exactly one tempo in the block, got " + juce::String ((int) t.size()));

    if (t.empty())
        return;

    // Exactly: the figure is the host's, passed down, and nothing on the way
    // has any business doing arithmetic on it.
    expect (t.front().bpm == bpm,
            where + ": bpm " + juce::String (bpm, 6) + ", got " + juce::String (t.front().bpm, 6));
    expect (t.front().valid == valid, where + ": valid is " + (valid ? "true" : "false"));
    expect (t.front().playing == playing, where + ": playing is " + (playing ? "true" : "false"));
}

//== Rendering, for the comparison ============================================
const std::vector<float>& signal()
{
    static const auto s = pink (kLength);
    return s;
}

/** What a host can do to the playhead during a render. */
enum class Host { none, steady, moving };

/** Both channels of `proc`'s output over kLength samples, laid end to end.

    `Host::moving` changes the playhead every block -- tempo, transport, and a
    block with no tempo at all -- so a module whose default were not really a
    no-op would have every chance to show it. */
std::vector<float> render (juce::AudioProcessor& proc, Host host)
{
    FakePlayHead playHead;
    playHead.set (120.0, true);
    proc.setPlayHead (host == Host::none ? nullptr : &playHead);

    proc.setPlayConfigDetails (2, 2, kRate, kBlock);
    proc.prepareToPlay (kRate, kBlock);

    const auto& in = signal();
    std::vector<float> out ((size_t) kLength * 2);

    juce::AudioBuffer<float> buffer (2, kBlock);
    juce::MidiBuffer midi;

    for (int b = 0; b < kBlocks; ++b)
    {
        if (host == Host::moving)
        {
            switch (b % 4)
            {
                case 0:  playHead.set (120.0, true);  break;
                case 1:  playHead.set (97.5, false);  break;
                case 2:  playHead.set ({}, true);     break;
                default: playHead.position = {};      break;
            }
        }

        const auto offset = (size_t) b * kBlock;

        for (int ch = 0; ch < 2; ++ch)
            buffer.copyFrom (ch, 0, in.data() + offset, kBlock);

        proc.processBlock (buffer, midi);

        for (int ch = 0; ch < 2; ++ch)
            std::memcpy (out.data() + (size_t) ch * kLength + offset,
                         buffer.getReadPointer (ch), sizeof (float) * kBlock);
    }

    // The playhead is about to go out of scope; a processor must not be left
    // holding it.
    proc.setPlayHead (nullptr);
    return out;
}

bool identical (const std::vector<float>& a, const std::vector<float>& b)
{
    return a.size() == b.size() && std::memcmp (a.data(), b.data(), sizeof (float) * a.size()) == 0;
}

/** Everything at 0.63 of its normalised range, as bus_tests' swept setting:
    switches on, choices high, knobs off-centre, so a module is doing
    something rather than sitting at a transparent default. */
void sweep (bmo::ParamSet& params)
{
    for (int i = 0; i < params.size(); ++i)
        params.param (i).setValueNotifyingHost (0.63f);
}

} // namespace

//==============================================================================
int main()
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    //== The standalone processor =============================================
    {
        auto proc = makeProduct (probeModule());
        FakePlayHead playHead;

        // No playhead at all: a standalone app, an offline harness.
        oneBlock (*proc);
        expectOrder ("no playhead");
        expectTempo (0.0, false, false, "no playhead");

        // A host at 120, playing.
        proc->setPlayHead (&playHead);
        playHead.set (120.0, true);
        oneBlock (*proc);
        expectOrder ("120 bpm playing");
        expectTempo (120.0, true, true, "120 bpm playing");

        // Several blocks: once per block, every block, not once per prepare.
        {
            juce::AudioBuffer<float> buffer (2, kBlock);
            buffer.clear();
            juce::MidiBuffer midi;

            events.clear();

            for (int b = 0; b < 5; ++b)
                proc->processBlock (buffer, midi);

            expect (tempoEvents().size() == 5,
                    "five blocks carry five tempos, got " + juce::String ((int) tempoEvents().size()));
            expect (events.size() == 15, "five blocks are fifteen calls: params, tempo, process each");

            for (size_t i = 0; i + 2 < events.size(); i += 3)
                expect (events[i].call == Call::params && events[i + 1].call == Call::tempo
                            && events[i + 2].call == Call::process,
                        "block " + juce::String ((int) i / 3) + " is params, tempo, process");
        }

        // An unround tempo, which has to arrive as itself.
        playHead.set (97.5, true);
        oneBlock (*proc);
        expectTempo (97.5, true, true, "97.5 bpm playing");

        // A change between blocks is seen on the very next block.
        playHead.set (140.0, true);
        oneBlock (*proc);
        expectTempo (140.0, true, true, "a tempo change, on the next block");

        // A stopped transport keeps its tempo and says it is stopped. This is
        // NOT one of the invalid cases: a module synced to 120 is still synced
        // to 120 while the transport is parked.
        playHead.set (120.0, false);
        oneBlock (*proc);
        expectOrder ("stopped");
        expectTempo (120.0, true, false, "stopped transport, tempo known");

        // A playhead with no position to give.
        playHead.position = {};
        oneBlock (*proc);
        expectOrder ("no position");
        expectTempo (0.0, false, false, "playhead with no position");

        // A position with no tempo in it -- and a running transport, which is
        // still reported as not playing: invalid is one case, down to the last
        // value, so a module cannot be made to tell the three apart.
        playHead.set ({}, true);
        oneBlock (*proc);
        expectOrder ("no bpm");
        expectTempo (0.0, false, false, "position with no bpm, transport running");

        // A tempo that is not a tempo is the same "no tempo".
        for (const auto nonsense : { 0.0, -120.0,
                                     std::numeric_limits<double>::quiet_NaN(),
                                     std::numeric_limits<double>::infinity() })
        {
            playHead.set (nonsense, true);
            oneBlock (*proc);
            expectTempo (0.0, false, false, "a host bpm of " + juce::String (nonsense));
        }

        // And back again: a valid tempo after an invalid one is not held off.
        playHead.set (120.0, true);
        oneBlock (*proc);
        expectTempo (120.0, true, true, "valid again after invalid");

        // A host that takes its playhead away: nothing stale survives it.
        proc->setPlayHead (nullptr);
        oneBlock (*proc);
        expectTempo (0.0, false, false, "playhead removed");
    }

    //== The rack =============================================================
    {
        const auto& probe = probeModule();
        FakePlayHead playHead;

        // Empty: no engine to hand anything to, and the audio passes through.
        {
            auto rack = bmo::products::createRack();
            rack->setPlayHead (&playHead);
            playHead.set (120.0, true);
            rack->setPlayConfigDetails (2, 2, kRate, kBlock);
            rack->prepareToPlay (kRate, kBlock);

            juce::AudioBuffer<float> buffer (2, kBlock);
            for (int ch = 0; ch < 2; ++ch)
                buffer.copyFrom (ch, 0, signal().data(), kBlock);

            juce::MidiBuffer midi;
            events.clear();
            rack->processBlock (buffer, midi);

            expect (events.empty(), "an empty rack calls nothing");
            expect (std::memcmp (buffer.getReadPointer (0), signal().data(), sizeof (float) * kBlock) == 0,
                    "an empty rack with a playhead passes its input through untouched");

            rack->setPlayHead (nullptr);
        }

        // Two probes side by side: the same values, in the same block.
        {
            auto rack = bmo::products::createRack();
            rack->addModule (probe);
            rack->addModule (probe);
            rack->setPlayHead (&playHead);
            rack->setPlayConfigDetails (2, 2, kRate, kBlock);
            rack->prepareToPlay (kRate, kBlock);

            const auto expectAgree = [&] (double bpm, bool valid, bool playing, const juce::String& where)
            {
                oneBlock (*rack);
                expectOrder (where);

                const auto t = tempoEvents();
                expect (t.size() == 2, where + ": two slots, two tempos, got " + juce::String ((int) t.size()));

                if (t.size() != 2)
                    return;

                expect (t[0].probe != t[1].probe, where + ": from two different slots");
                expect (t[0].bpm == t[1].bpm && t[0].valid == t[1].valid && t[0].playing == t[1].playing,
                        where + ": both slots were handed the same values");

                for (const auto& e : t)
                {
                    expect (e.bpm == bpm, where + ": bpm " + juce::String (bpm) + ", got " + juce::String (e.bpm));
                    expect (e.valid == valid, where + ": valid");
                    expect (e.playing == playing, where + ": playing");
                }
            };

            playHead.set (120.0, true);
            expectAgree (120.0, true, true, "rack, 120 bpm playing");

            playHead.set (97.5, false);
            expectAgree (97.5, true, false, "rack, 97.5 bpm stopped");

            playHead.set ({}, true);
            expectAgree (0.0, false, false, "rack, no bpm");

            rack->setPlayHead (nullptr);
            expectAgree (0.0, false, false, "rack, no playhead");
        }

        // Mid-chain, among real modules: util, probe, eq, probe. The real
        // modules in between change nothing about what the probes are handed.
        {
            const auto& registry = bmo::products::registry();
            const auto find = [&] (const char* id) -> const bmo::ModuleDef&
            {
                for (const auto* def : registry)
                    if (juce::String (def->id) == id)
                        return *def;

                jassertfalse;
                return *registry.front();
            };

            auto rack = bmo::products::createRack();
            rack->addModule (find ("util"));
            rack->addModule (probe);
            rack->addModule (find ("eq"));
            rack->addModule (probe);
            rack->setPlayHead (&playHead);
            rack->setPlayConfigDetails (2, 2, kRate, kBlock);
            rack->prepareToPlay (kRate, kBlock);

            playHead.set (133.0, true);
            oneBlock (*rack);
            expectOrder ("mid-chain");

            const auto t = tempoEvents();
            expect (t.size() == 2, "mid-chain: the two probes, and only they, record a tempo");

            for (const auto& e : t)
                expect (e.bpm == 133.0 && e.valid && e.playing,
                        "mid-chain: probe " + juce::String (e.probe) + " was handed 133 bpm, playing");

            rack->setPlayHead (nullptr);
        }

        // A chain edit mid-session. `rebuild` gives EVERY slot a new engine
        // and a new DSP, the slots the edit did not touch included, so
        // whatever an old DSP remembered is gone -- which ModuleDsp::setTempo
        // warns a module about. What the plumbing owes it is that the new DSP
        // is handed the tempo before its first process, not one block later.
        {
            const auto& util = [&]() -> const bmo::ModuleDef&
            {
                for (const auto* def : bmo::products::registry())
                    if (juce::String (def->id) == "util")
                        return *def;

                jassertfalse;
                return *bmo::products::registry().front();
            }();

            auto rack = bmo::products::createRack();
            rack->addModule (probe);
            rack->addModule (probe);
            rack->setPlayHead (&playHead);
            rack->setPlayConfigDetails (2, 2, kRate, kBlock);
            rack->prepareToPlay (kRate, kBlock);

            playHead.set (120.0, true);
            oneBlock (*rack);

            /** After an edit, every probe that was alive before it is silent,
                and every probe made by it -- `expected` of them -- was handed
                exactly one tempo, with these values, before its first
                process. The log is cleared BEFORE the edit, so the new DSPs'
                whole lives are in it, prepare's setParams included. */
            const auto expectFreshAndHanded = [&] (const std::function<void()>& edit, int expected,
                                                   double bpm, bool valid, bool playing,
                                                   const juce::String& where)
            {
                const auto firstNew = probesMade;

                events.clear();
                edit();

                juce::AudioBuffer<float> buffer (2, kBlock);
                buffer.clear();
                juce::MidiBuffer midi;
                rack->processBlock (buffer, midi);

                expect (probesMade - firstNew == expected,
                        where + ": the edit made " + juce::String (expected) + " new probe DSPs, made "
                            + juce::String (probesMade - firstNew));

                for (const auto& e : events)
                    expect (e.probe >= firstNew,
                            where + ": no call reaches probe " + juce::String (e.probe)
                                + ", which the rebuild destroyed");

                for (int id = firstNew; id < probesMade; ++id)
                {
                    int tempos = 0;
                    bool processedBeforeTempo = false;
                    bool processed = false;

                    for (const auto& e : events)
                    {
                        if (e.probe != id)
                            continue;

                        if (e.call == Call::process)
                        {
                            processedBeforeTempo = processedBeforeTempo || tempos == 0;
                            processed = true;
                        }

                        if (e.call == Call::tempo)
                        {
                            ++tempos;
                            expect (e.bpm == bpm && e.valid == valid && e.playing == playing,
                                    where + ": new probe " + juce::String (id) + " was handed bpm "
                                        + juce::String (bpm) + (valid ? ", valid" : ", invalid")
                                        + (playing ? ", playing" : ", stopped"));
                        }
                    }

                    expect (processed, where + ": new probe " + juce::String (id) + " processed the block");
                    expect (tempos == 1, where + ": new probe " + juce::String (id)
                                             + " was handed one tempo, got " + juce::String (tempos));
                    expect (! processedBeforeTempo,
                            where + ": new probe " + juce::String (id) + " had its tempo before its first process");
                }
            };

            // Add a third probe: all three slots are new engines, the two the
            // edit did not touch as well as the one it added.
            expectFreshAndHanded ([&] { rack->addModule (probe); }, 3,
                                  120.0, true, true, "adding a module");

            // Swap the middle one for BMO Util, at a new tempo: the probes
            // either side of it are rebuilt again and hear the new figure.
            playHead.set (97.5, false);
            expectFreshAndHanded ([&] { rack->setModule (1, util); }, 2,
                                  97.5, true, false, "swapping a module");

            // Move one, with the host's tempo gone: the new DSPs are handed
            // "no tempo" before they process, never nothing and never the
            // figure the destroyed DSPs last heard.
            playHead.set ({}, true);
            expectFreshAndHanded ([&] { rack->moveModule (0, 2); }, 2,
                                  0.0, false, false, "moving a module, tempo invalid");

            rack->setPlayHead (nullptr);
        }
    }

    //== Nothing else moved: byte for byte, with and without a playhead =======
    //
    // **Today this is true by construction**: no module overrides `setTempo`,
    // so every one of them inherits the no-op and cannot hear the playhead.
    // The check is here for the day that stops being true. BMO Dwell will be
    // the first module that syncs to the tempo, and its output SHOULD change
    // with one -- so it, and every consumer after it, is carved out of this
    // comparison deliberately, by id, in `tempoConsumers` below, and carries
    // its own tempo tests in its own suite. A module that starts hearing the
    // tempo without being added there fails here, which is the point: the
    // list is the record of which modules are allowed to.
    //
    // The walk is over the registry as it stands, never a count written down
    // here, so adding a module is checked automatically and never breaks this
    // file for a reason that has nothing to do with the tempo.
    {
        // Ids of modules that consume the tempo, and so are exempt from the
        // byte-identity below. Empty until BMO Dwell; add "delay" (or whatever
        // id it ships under) here in the same change that overrides setTempo.
        const std::vector<juce::String> tempoConsumers {};

        const auto& registry = bmo::products::registry();

        const auto consumesTempo = [&] (const bmo::ModuleDef& def)
        {
            return std::find (tempoConsumers.begin(), tempoConsumers.end(), juce::String (def.id))
                       != tempoConsumers.end();
        };

        // A name on the carve-out list that is not a registered module is a
        // typo or a leftover, and would exempt nothing while looking as if it
        // did.
        for (const auto& id : tempoConsumers)
            expect (std::any_of (registry.begin(), registry.end(),
                                 [&] (const bmo::ModuleDef* d) { return id == d->id; }),
                    "the tempo carve-out names a registered module: " + id);

        const auto compareStandalone = [&] (const bmo::ModuleDef& def)
        {
            for (const auto swept : { false, true })
            {
                const juce::String who { juce::String (def.id) + (swept ? " (swept)" : " (defaults)") };

                const auto run = [&] (Host host)
                {
                    auto proc = makeProduct (def);

                    if (swept)
                        sweep (proc->getEngine().params());

                    return render (*proc, host);
                };

                const auto quiet  = run (Host::none);
                const auto again  = run (Host::none);
                const auto steady = run (Host::steady);
                const auto moving = run (Host::moving);

                expect (identical (quiet, again), who + " renders the same twice with no playhead");
                expect (identical (quiet, steady), who + " is byte-identical with a playhead at 120 bpm playing");
                expect (identical (quiet, moving), who + " is byte-identical with a playhead that changes every block");
            }
        };

        int compared = 0;

        for (const auto* def : registry)
        {
            if (consumesTempo (*def))
                continue;

            compareStandalone (*def);
            ++compared;
        }

        expect (compared + (int) tempoConsumers.size() == (int) registry.size(),
                "every registered module was compared or carved out by name");

       #if BMO_TEMPO_TESTS_TUNE
        // BMO Tune RT runs on the same SingleModuleProcessor but is not in the
        // rack's registry, so the walk above never reaches it. It is linked in
        // here only when the build has it (BMO_BUILD_TUNE), as the other Tune
        // suites are.
        compareStandalone (bmo::tune::module());
       #endif

        // And in racks, in registry order, eight slots at a time -- the
        // registry holds more modules than a rack has slots, so it takes two
        // racks to put every module in one.
        std::vector<const bmo::ModuleDef*> rackable;

        for (const auto* def : registry)
            if (! consumesTempo (*def))
                rackable.push_back (def);

        int racked = 0;

        for (size_t first = 0; first < rackable.size(); first += (size_t) RackProcessor::kSlots)
        {
            for (const auto swept : { false, true })
            {
                int loaded = 0;

                const auto run = [&] (Host host)
                {
                    auto rack = bmo::products::createRack();

                    for (auto i = first; i < rackable.size() && rack->addModule (*rackable[i]); ++i) {}

                    loaded = rack->getNumModules();

                    if (swept)
                        for (int i = 0; i < loaded; ++i)
                            sweep (rack->getEngineAt (i)->params());

                    return render (*rack, host);
                };

                const juce::String who { "the rack from module " + juce::String ((int) first)
                                             + (swept ? " (swept)" : " (defaults)") };
                const auto quiet = run (Host::none);

                expect (identical (quiet, run (Host::none)),   who + " renders the same twice with no playhead");
                expect (identical (quiet, run (Host::steady)), who + " is byte-identical with a playhead at 120 bpm");
                expect (identical (quiet, run (Host::moving)), who + " is byte-identical with a moving playhead");

                if (! swept)
                    racked += loaded;
            }
        }

        expect (racked == (int) rackable.size(),
                "every module not carved out was run in a rack, got " + juce::String (racked));
    }

    std::cout << checksMade << " checks made.\n";
    return finish ("tempo");
}
