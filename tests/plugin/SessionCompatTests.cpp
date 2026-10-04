/*
    Sessions saved by a shipped release restore in this build.

    The schema tests pin ids, order, ranges and defaults, and every suite
    round-trips this build's own state. Neither loads a state that a shipped
    build wrote. This does: SessionCompatFixtures_0_2_5.h holds the blobs
    release 0.2.5 wrote from getStateInformation -- every product it shipped,
    every parameter away from its default, two rack chains that between them
    hold every module its rack could host, and every product again at its
    defaults -- together with the values 0.2.5 itself read back from them.
    SESSION-COMPAT.md says how they were made and how to add a release.

    Each one is restored here three ways -- on the message thread, from a
    host thread while the message thread runs, and from this build's own
    re-save of the first -- and in each:
      - every parameter 0.2.5 had reads back the value 0.2.5 read, found by
        id: exactly for a bool, a choice or a stepped float, within one part
        in a million for a continuous one -- and sits at the position it had
        in 0.2.5's specs(), which a restore by id would not notice;
      - every parameter 0.2.5 did not have is at this build's default;
      - a rack holds the same modules in the same slots, each lane 0.2.5 had
        carries 0.2.5's value on it, and a module's parameters past the 32nd
        lane restore with the rest; the expanded view restores where saved.
    The defaults fixtures are what keep a default that has since moved honest:
    an old session saved the old default explicitly, and must get it back.

    A difference this build has from 0.2.5 on purpose goes in kKnownDifferences
    with its figures, never by loosening a comparison: the suite stays green and
    the difference stays printed on every run.
*/

#include "TestUtil.h"
#include "core/rack/RackProcessor.h"
#include "core/state/PresetManager.h"
#include "products/rack/Product.h"
#include "products/eq/Product.h"
#include "products/sat/Product.h"
#include "products/util/Product.h"
#include "products/opto/Product.h"
#include "products/dim/Product.h"
#include "products/deq/Product.h"
#include "products/vcomp/Product.h"

#if BMO_SESSION_COMPAT_TUNE
 #include "products/tune/Product.h"
#endif

#include <atomic>
#include <cmath>
#include <iterator>
#include <set>
#include <string>
#include <thread>
#include <vector>

namespace session_compat
{
    /** One parameter as a release saved it and read it back. */
    struct Value
    {
        const char* id;
        double value;       // real units
        bool stepped;       // bool, choice or stepped float: compared exactly
        const char* text;   // as that release displayed it
    };

    /** One module's share of a state: the product's own, or one rack slot. */
    struct Slot
    {
        const char* module;
        bool expanded;
        const Value* values;
        int count;
    };

    struct Fixture
    {
        const char* name;
        const char* product;    // the current product that restores it
        bool rack;
        const char* const* state;   // base64 lines, null-terminated
        const Slot* slots;
        int numSlots;
    };
}

#include "SessionCompatFixtures_0_2_5.h"

using namespace test;
using namespace session_compat;
using bmo::RackProcessor;
using bmo::SingleModuleProcessor;

namespace
{
    //== Known differences =====================================================
    // A value this build restores differently from the release that saved it,
    // on purpose and with the owner's knowledge. Each entry names the fixture,
    // the slot (-1 for a single-module product), the parameter, the value the
    // release read back and the value this build reads. It is printed on every
    // run; an entry that stops occurring fails, so the table cannot go stale.
    struct KnownDifference
    {
        const char* release;
        const char* fixture;
        int slot;
        const char* id;
        double released;
        double now;
        const char* why;
    };

    const std::vector<KnownDifference> kKnownDifferences {
        // None: release 0.2.5 restores unchanged.
    };

    std::set<const KnownDifference*> knownSeen;

    int assertions = 0;

    void expect (bool condition, const juce::String& what)
    {
        ++assertions;
        check (condition, what);
    }

    juce::MemoryBlock decode (const char* const* lines)
    {
        juce::String text;
        for (auto* l = lines; *l != nullptr; ++l)
            text << *l;

        juce::MemoryOutputStream out;
        const auto ok = juce::Base64::convertFromBase64 (out, text);
        expect (ok, "a fixture's base64 decodes");
        return out.getMemoryBlock();
    }

    std::unique_ptr<SingleModuleProcessor> createProduct (const juce::String& product)
    {
        using namespace bmo::products;

        if (product == "eq")      return createEq();
        if (product == "sat")     return createSat();
        if (product == "util")    return createUtil();
        if (product == "opto")    return createOpto();
        if (product == "dim")     return createDim();
        if (product == "deq")     return createDeq();
        if (product == "ltvcomp") return createVcomp();
       #if BMO_SESSION_COMPAT_TUNE
        if (product == "tune")    return createTune();
       #endif
        return nullptr;
    }

    bool same (double actual, double expected, bool stepped)
    {
        return stepped ? actual == expected
                       : std::abs (actual - expected) <= 1.0e-6 * std::max (1.0, std::abs (expected));
    }

    juce::String figure (double v) { return juce::String (v, 9); }

    const KnownDifference* findKnown (const char* release, const Fixture& f, int slot, const Value& value, double actual)
    {
        for (const auto& k : kKnownDifferences)
            if (juce::String (k.release) == release && juce::String (k.fixture) == f.name
                 && k.slot == slot && juce::String (k.id) == value.id
                 && same (actual, k.now, value.stepped) && same (k.released, value.value, value.stepped))
                return &k;

        return nullptr;
    }

    /** Every parameter of one module against what the release read back. */
    void checkModule (const char* release, const Fixture& f, const juce::String& pass, int slotIndex,
                      const Slot& slot, const bmo::ParamSet& params)
    {
        const juce::String where = juce::String (release) + " " + f.name + " [" + pass + "]"
                                     + (f.rack ? " slot " + juce::String (slotIndex + 1) : juce::String())
                                     + " " + slot.module + "/";

        std::set<std::string> saved;

        for (int v = 0; v < slot.count; ++v)
        {
            const auto& value = slot.values[v];
            saved.insert (value.id);

            const auto i = params.indexOf (value.id);

            if (i < 0)
            {
                expect (false, where + value.id + ": in the saved session, gone from this build");
                continue;
            }

            // The values are in the release's spec order. A host keys
            // automation by position, a rack lane most of all, so a parameter
            // that has moved breaks a session even though its value restores.
            expect (i == v, where + value.id + ": parameter " + juce::String (v + 1) + " in the release, "
                              + juce::String (i + 1) + " in this build");

            const auto actual = (double) params.getReal (i);

            if (same (actual, value.value, value.stepped))
            {
                ++assertions;
                continue;
            }

            const auto* known = findKnown (release, f, f.rack ? slotIndex : -1, value, actual);

            if (known != nullptr)
            {
                ++assertions;
                knownSeen.insert (known);
                std::cout << "KNOWN DIFFERENCE: " << where << value.id << " saved " << figure (value.value)
                          << " (" << value.text << "), restores " << figure (actual) << " -- " << known->why << '\n';
                continue;
            }

            expect (false, where + value.id + ": expected " + figure (value.value) + " (" + value.text + "), got "
                             + figure (actual) + " (" + params.param (i).getCurrentValueAsText() + ")");
        }

        // A parameter the release did not have is at this build's default.
        for (int i = 0; i < params.size(); ++i)
        {
            if (saved.count (params.spec (i).id) != 0)
                continue;

            auto& p = params.param (i);
            const auto def = (double) p.convertFrom0to1 (p.getDefaultValue());
            const auto actual = (double) params.getReal (i);

            expect (actual == def, where + params.spec (i).id + ": not in the saved session, so it should be at its default "
                                     + figure (def) + ", got " + figure (actual));
        }
    }

    void checkSingle (const char* release, const Fixture& f, const juce::String& pass, SingleModuleProcessor& p)
    {
        const auto& slot = f.slots[0];

        expect (juce::String (p.getEngine().def().id) == slot.module,
                juce::String (release) + " " + f.name + " [" + pass + "]: the product's module is "
                    + p.getEngine().def().id + ", the session's is " + slot.module);
        expect (p.isExpanded() == slot.expanded,
                juce::String (release) + " " + f.name + " [" + pass + "]: the view restores "
                    + (slot.expanded ? "expanded" : "compact"));

        checkModule (release, f, pass, 0, slot, p.getEngine().params());
    }

    void checkRack (const char* release, const Fixture& f, const juce::String& pass, RackProcessor& rack)
    {
        const juce::String where = juce::String (release) + " " + f.name + " [" + pass + "]";

        expect (rack.getNumModules() == f.numSlots,
                where + ": " + juce::String (f.numSlots) + " modules saved, " + juce::String (rack.getNumModules()) + " restored");

        for (int s = 0; s < juce::jmin (f.numSlots, rack.getNumModules()); ++s)
        {
            const auto& slot = f.slots[s];
            const auto* def = rack.getModuleAt (s);
            auto* engine = rack.getEngineAt (s);

            if (def == nullptr || engine == nullptr || juce::String (def->id) != slot.module)
            {
                expect (false, where + " slot " + juce::String (s + 1) + ": expected " + slot.module + ", holds "
                                 + (def != nullptr ? juce::String (def->id) : juce::String ("nothing")));
                continue;
            }

            ++assertions;
            expect (rack.isSlotExpanded (s) == slot.expanded,
                    where + " slot " + juce::String (s + 1) + ": the view restores " + (slot.expanded ? "expanded" : "compact"));

            const auto& params = engine->params();
            checkModule (release, f, pass, s, slot, params);

            // The host's lanes, by position: a lane the release had carries the
            // value the release had on it, which is what an automation lane
            // recorded against that release goes on meaning. A lane past the
            // release's parameters reads whatever this build's module does.
            for (int i = 0; i < juce::jmin (params.size(), RackProcessor::kParamsPerSlot); ++i)
            {
                auto& lane = rack.getSlotParameter (s, i);
                const auto onLane = (double) lane.convertFrom0to1 (lane.getValue());
                const auto what = where + " slot " + juce::String (s + 1) + " lane " + juce::String (i + 1);

                if (i >= slot.count)
                {
                    expect (onLane == (double) params.getReal (i),
                            what + " reads " + figure (onLane) + ", its module " + figure (params.getReal (i)));
                    continue;
                }

                const auto& value = slot.values[i];
                const auto* known = findKnown (release, f, s, value, onLane);

                expect (same (onLane, value.value, value.stepped) || known != nullptr,
                        what + " (" + value.id + " in the release): expected " + figure (value.value)
                            + " (" + value.text + "), got " + figure (onLane) + " (" + lane.getCurrentValueAsText() + ")");
            }
        }

        for (int s = f.numSlots; s < RackProcessor::kSlots; ++s)
            expect (rack.getModuleAt (s) == nullptr, where + " slot " + juce::String (s + 1) + " stays empty");
    }

    /** Restores from a thread that is not the message thread, the way some
        hosts do, while the message thread keeps running. */
    void restoreOffThread (juce::AudioProcessor& proc, const juce::MemoryBlock& state)
    {
        std::atomic<bool> done { false };

        std::thread host ([&]
        {
            proc.setStateInformation (state.getData(), (int) state.getSize());
            done = true;
        });

        while (! done)
            juce::MessageManager::getInstance()->runDispatchLoopUntil (5);

        host.join();
        juce::MessageManager::getInstance()->runDispatchLoopUntil (50);
    }

    template <typename Make, typename Check>
    void runFixture (const char* release, const Fixture& f, Make make, Check checkOne)
    {
        const auto state = decode (f.state);

        // On the message thread, as a host loading a session does.
        auto first = make();
        first->setStateInformation (state.getData(), (int) state.getSize());
        checkOne (release, f, "message thread", *first);

        // From a host thread.
        auto second = make();
        restoreOffThread (*second, state);
        checkOne (release, f, "host thread", *second);

        // Saved again by this build and restored: the same values again.
        juce::MemoryBlock resaved;
        first->getStateInformation (resaved);
        auto third = make();
        third->setStateInformation (resaved.getData(), (int) resaved.getSize());
        checkOne (release, f, "re-saved", *third);
    }

    /** Defaults that have moved since the release, for the record: the
        defaults fixtures above already prove an old session keeps its own. */
    void noteMovedDefaults (const char* release, const Fixture& f, const bmo::ParamSet& params, int slotIndex)
    {
        const auto& slot = f.slots[slotIndex];

        for (int v = 0; v < slot.count; ++v)
        {
            const auto i = params.indexOf (slot.values[v].id);
            if (i < 0)
                continue;

            auto& p = params.param (i);
            const auto now = (double) p.convertFrom0to1 (p.getDefaultValue());

            if (! same (now, slot.values[v].value, slot.values[v].stepped))
                std::cout << "NOTE: " << slot.module << "/" << slot.values[v].id << " defaulted to "
                          << figure (slot.values[v].value) << " in " << release << ", defaults to "
                          << figure (now) << " now; a session that saved the old default keeps it\n";
        }
    }

    void runRelease (const char* release, const Fixture* const* fixtures, size_t count)
    {
        for (size_t n = 0; n < count; ++n)
        {
            const auto& f = *fixtures[n];

            if (f.rack)
            {
                runFixture (release, f, [] { return bmo::products::createRack(); },
                            [] (const char* r, const Fixture& fx, const juce::String& pass, RackProcessor& rack)
                            { checkRack (r, fx, pass, rack); });
                continue;
            }

            if (createProduct (f.product) == nullptr)
            {
                std::cout << "SKIPPED: " << release << " " << f.name << " -- this build does not have the product\n";
                continue;
            }

            runFixture (release, f, [&f] { return createProduct (f.product); },
                        [] (const char* r, const Fixture& fx, const juce::String& pass, SingleModuleProcessor& p)
                        { checkSingle (r, fx, pass, p); });

            if (juce::String (f.name).endsWith ("-defaults"))
                noteMovedDefaults (release, f, createProduct (f.product)->getEngine().params(), 0);
        }
    }
}

int main()
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    // Constructing a product looks for its preset folder and migrates old
    // ones; none of that may touch the machine's real folders.
    const auto sandbox = juce::File::getSpecialLocation (juce::File::tempDirectory)
                             .getChildFile ("bmo-session-compat-tests");
    sandbox.deleteRecursively();
    bmo::PresetManager::setDirectoryForTesting (sandbox);

    runRelease ("0.2.5", v0_2_5::kFixtures, std::size (v0_2_5::kFixtures));

    for (const auto& k : kKnownDifferences)
        check (knownSeen.count (&k) != 0,
               juce::String ("the known difference ") + k.release + " " + k.fixture + " " + k.id
                   + " no longer occurs: take it out of the table");

    sandbox.deleteRecursively();
    bmo::PresetManager::setDirectoryForTesting ({});

    std::cout << assertions << " assertions\n";
    return finish ("session compatibility");
}
