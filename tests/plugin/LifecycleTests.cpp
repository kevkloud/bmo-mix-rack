/*
    The calls a host may make before it has prepared anything, and twice.

    Both processors' `releaseResources()` call the engine's `reset()`
    unconditionally, and a host is entitled to release a plugin it never
    prepared -- a scan, a load that is cancelled, a project opened and closed
    without playing. On a sibling branch one module's `reset()` went into an
    unbounded loop when its buffers had never been sized: a hang, and nothing
    caught it, because every test prepares first.

    So every module in the registry, and BMO Tune RT, is driven through:

    - **the DSP alone**: made by the module's own factory, `reset()` with no
      `prepare()`; then `prepare()`, `reset()`, `reset()`;
    - **the engine, as the standalone processor makes it**: `latency()` and
      `tailSeconds()` (the processor asks for the tail as it is
      constructed), the engine's `reset()`, and `releaseResources()` twice
      before `prepareToPlay()` and twice after it;
    - **a rack slot**: the module alone in a rack that is released before it
      is ever prepared, then prepared and released twice.

    After each sequence the processor is prepared and run for a block, which
    must come back finite: returning is not enough if what it returned to is
    broken.

    **A hang fails fast instead of stalling ctest.** Every call runs on a
    worker thread under a watchdog of `kWatchdogMs`; if it has not returned
    the suite prints which call and exits at once, non-zero, leaving the
    stuck thread behind -- there is no way to stop it, and no reason to try.
*/

#include "TestUtil.h"
#include "core/product/SingleModuleProcessor.h"
#include "core/rack/RackProcessor.h"
#include "products/rack/Product.h"
#include "products/rack/Registry.h"

#if BMO_LIFECYCLE_TESTS_TUNE
 #include "modules/tune/Module.h"
#endif

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <functional>
#include <thread>

using namespace test;
using bmo::RackProcessor;

namespace
{

constexpr double kRate  = 48000.0;
constexpr int    kBlock = 512;

/** Every call here returns in microseconds; prepare() of the heaviest module
    takes milliseconds. Five seconds is three orders of magnitude of room on a
    loaded machine and still fails a hang long before ctest's own timeout. */
constexpr int kWatchdogMs = 5000;

int checksMade = 0;

void expect (bool condition, const juce::String& what)
{
    ++checksMade;
    check (condition, what);
}

/** Runs `call` on a worker thread and waits up to `kWatchdogMs` for it. A call
    that does not return ends the process here, non-zero, with `what` named. */
void mustReturn (const juce::String& what, const std::function<void()>& call)
{
    juce::WaitableEvent done;

    std::thread worker ([&]
    {
        call();
        done.signal();
    });

    if (! done.wait (kWatchdogMs))
    {
        std::cout << "FAIL: " << what << " did not return" << std::endl;
        std::cerr << "FAIL: " << what << " did not return" << std::endl;
        std::_Exit (1);
    }

    worker.join();
    ++checksMade;
}

std::unique_ptr<bmo::SingleModuleProcessor> makeProduct (const bmo::ModuleDef& def)
{
    bmo::ProductInfo info { def.name, { def.name, ".bmotest" }, 1, 1 };
    return std::make_unique<bmo::SingleModuleProcessor> (def, info);
}

/** One block of a -18 dBFS sine through `proc`, prepared first; whether every
    sample came back finite. */
bool runsAfter (juce::AudioProcessor& proc)
{
    proc.setPlayConfigDetails (2, 2, kRate, kBlock);
    proc.prepareToPlay (kRate, kBlock);

    juce::AudioBuffer<float> buffer (2, kBlock);
    juce::MidiBuffer midi;

    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < kBlock; ++i)
            buffer.setSample (ch, i, 0.177f * (float) std::sin (0.0288 * i));

    proc.processBlock (buffer, midi);

    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < kBlock; ++i)
            if (! std::isfinite (buffer.getSample (ch, i)))
                return false;

    return true;
}

} // namespace

//==============================================================================
int main()
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    std::vector<const bmo::ModuleDef*> modules (bmo::products::registry().begin(),
                                                bmo::products::registry().end());

   #if BMO_LIFECYCLE_TESTS_TUNE
    // BMO Tune RT is its own product on the same SingleModuleProcessor, not in
    // the rack's registry, so it is added by hand and gets no rack case.
    modules.push_back (&bmo::tune::module());
   #endif

    int walked = 0;

    for (const auto* def : modules)
    {
        const juce::String id { def->id };
        const auto inRack = std::find (bmo::products::registry().begin(), bmo::products::registry().end(), def)
                                != bmo::products::registry().end();

        //== The DSP alone =====================================================
        {
            auto dsp = def->createDsp();

            mustReturn (id + " reset() before prepare()", [&] { dsp->reset(); });
            mustReturn (id + " reset() twice before prepare()", [&] { dsp->reset(); });

            mustReturn (id + " prepare() after an unprepared reset()", [&]
            {
                dsp->prepare (kRate, kBlock, 2);
            });

            mustReturn (id + " reset() after prepare()", [&] { dsp->reset(); });
            mustReturn (id + " reset() twice after prepare()", [&] { dsp->reset(); });
        }

        //== The engine, in the standalone processor ===========================
        {
            // Made here, on the message thread. Construction asks the engine
            // for its tail before any prepare, which is the tailSeconds()
            // call below, made again under the watchdog.
            auto proc = makeProduct (*def);

            auto& engine = proc->getEngine();

            mustReturn (id + " latency() before prepare()",     [&] { (void) engine.latency(); });
            mustReturn (id + " tailSeconds() before prepare()", [&] { (void) engine.tailSeconds(); });
            mustReturn (id + " engine reset() before prepare()", [&] { engine.reset(); });
            mustReturn (id + " releaseResources() before prepareToPlay()", [&] { proc->releaseResources(); });
            mustReturn (id + " releaseResources() twice before prepareToPlay()", [&] { proc->releaseResources(); });

            expect (runsAfter (*proc), id + ": prepared after an unprepared release, a block comes back finite");

            mustReturn (id + " releaseResources() after prepareToPlay()", [&] { proc->releaseResources(); });
            mustReturn (id + " releaseResources() twice after prepareToPlay()", [&] { proc->releaseResources(); });

            expect (runsAfter (*proc), id + ": prepared again after two releases, a block comes back finite");
        }

        //== A rack slot =======================================================
        if (inRack)
        {
            // Made here, on the message thread: rebuild() says it must be.
            auto rack = bmo::products::createRack();
            rack->addModule (*def);

            mustReturn (id + " in a rack, releaseResources() before prepareToPlay()", [&] { rack->releaseResources(); });

            expect (runsAfter (*rack), id + " in a rack: prepared after an unprepared release, a block comes back finite");

            mustReturn (id + " in a rack, releaseResources() after prepareToPlay()", [&] { rack->releaseResources(); });
            mustReturn (id + " in a rack, releaseResources() twice after prepareToPlay()", [&] { rack->releaseResources(); });

            expect (runsAfter (*rack), id + " in a rack: prepared again after two releases, a block comes back finite");
        }

        ++walked;
    }

    expect (walked == (int) modules.size() && walked >= 11,
            "every registered module was walked, " + juce::String (walked));

    std::cout << checksMade << " checks made.\n";
    return finish ("lifecycle");
}
