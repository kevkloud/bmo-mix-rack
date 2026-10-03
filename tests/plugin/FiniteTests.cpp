/*
    One bad sample, and what it may cost.

    A NaN or an infinity in a module's input used to stay in its state for
    good: a filter's memory, a detector's envelope, a delay line -- each of
    them feeds a non-finite value back into itself, and from then on every
    sample out was non-finite until the module was reset. QA measured it on
    2026-10-03 in BMO Opto (48,000 of 48,000 samples two seconds later, with an
    infinity crossing the stereo link to the other side), BMO CEQ, BMO
    Saturator at every oversampling factor, BMO FET and BMO Linger's early
    reflections. In a rack, one bad sample from the host or from any slot
    therefore silenced everything after it.

    The guard is ONE place, `ModuleEngine::process`, which both processors and
    every rack slot go through (core/AGENTS.md says what it guarantees). So the
    test is one place too, and it walks the registry rather than a list: a
    module added later is covered without anyone remembering to add it.

    - **Every module, through the processor a host drives**, at its defaults,
      fed a steady 220 Hz sine at -18 dBFS RMS on both channels. One NaN, and
      separately one +Inf, replaces one sample of the left channel in one
      block. Asserted: no non-finite sample ever leaves the processor, the
      meters a panel reads stay finite, and from `kSettleSeconds` after the
      bad sample the output is within `kToleranceDb` of the same render with
      no bad sample in it, and still carries the signal. The input guard
      hands the module a zero in place of the bad sample, so what is left of
      the injection is the module's own response to a one-sample gap; the
      figures each module actually reaches are printed beside the bound.

    - **A module that blows up by itself.** No registered module produces a
      non-finite sample from finite input at its defaults, so the output half
      of the guard is driven by a probe that passes its input through and, on
      a given block, latches into putting out NaN until it is reset -- which
      is what a filter that has gone unstable looks like from outside. The
      listener gets exactly one block of silence and then the input again,
      and the probe is reset exactly once.

    - **The rack.** A chain of real modules with the bad sample entering at
      the rack's input; and the same chain with the blowing-up probe in the
      middle of it, so the bad samples are produced mid-chain and the modules
      after the probe are the ones at risk.

    The bounds are absolute -- a figure in dBFS against a fixed stimulus --
    and the comparison render is the same code with the same input less one
    sample, which is the only honest reference for "recovered": the house's
    usual objection to comparing two runs (tests/dsp/OptoDspTests.cpp) is that
    both can be broken the same way, and here one of them has no bad sample in
    it to be broken by.
*/

#include "TestUtil.h"
#include "core/product/SingleModuleProcessor.h"
#include "core/rack/RackProcessor.h"
#include "products/rack/Product.h"
#include "products/rack/Registry.h"

#if BMO_FINITE_TESTS_TUNE
 #include "modules/tune/Module.h"
#endif

#include <algorithm>
#include <cmath>
#include <cstring>
#include <functional>
#include <iomanip>
#include <iterator>
#include <limits>

using namespace test;
using bmo::RackProcessor;

namespace
{

constexpr double kRate    = 48000.0;
constexpr int    kBlock   = 512;
constexpr int    kBlocks  = 288;                     // 3.072 s
constexpr int    kLength  = kBlock * kBlocks;

/** Where the bad sample goes: block 48 (0.512 s, after every module's
    smoothers have settled from prepare), sample 100 of it, left channel. */
constexpr int kBadBlock   = 48;
constexpr int kBadOffset  = 100;
constexpr int kBadChannel = 0;
constexpr int kBadAt      = kBadBlock * kBlock + kBadOffset;

/** How close a module has to be to the clean render, and from when.

    **-60 dBFS is 42 dB under the signal**, which is -18 dBFS RMS (-15 dBFS
    peak). It is tight enough to fail every way this has gone wrong: a module
    still non-finite fails it outright, one that fell silent and stayed there
    (LTV Comp did, on +Inf, before the guard) is 15 dBFS out, and one that had
    recovered by being reset would differ by its whole output for as long as
    its own attack took. It is loose enough to leave a module its own correct
    response to the one-sample gap it was handed.

    **A quarter of a second** is the settle for a module and **half a second**
    for the rack. Measured on ICE QUEEN, 2026-10-03, max |difference| from the
    settle on: every module but two is at or under -130 dBFS by 0.25 s and
    most are bit-identical; BMO Tune RT holds a constant -74 dBFS from the gap
    on, which does not decay but is 14 dB inside the bound; BMO Dwell is
    below. The rack chain below is at -45 dBFS 0.25 s after a bad sample at
    its input -- BMO FET, after BMO Saturator, clamps on the click the gap
    makes and releases over about 0.3 s; without the Saturator ahead of it the
    chain is under -107 dBFS by 0.05 s -- and at -33 after a block silenced
    mid-chain; both are under -126 by 0.5 s. The settles are not padded
    beyond the next round figure, so a module that starts recovering more
    slowly shows up here. */
constexpr double kToleranceDb       = -60.0;
constexpr double kSettleSeconds     = 0.25;
constexpr double kRackSettleSeconds = 0.5;

/** The modules that may take longer, each with its reason. **BMO Dwell is a
    delay with feedback**: the gap it was handed comes back at every repeat,
    375 ms apart at its defaults and 14 to 17 dB down each time -- the
    largest difference from 0.25, 0.5, 1 and 1.5 s after it is -21, -38, -54
    and -68 dBFS -- which is the delay doing its job, not a failure to
    recover. A name here that is not a registered module fails the suite. */
struct SlowerSettle
{
    const char* id;
    double      seconds;
};

constexpr SlowerSettle kSlowerSettles[] { { "dwell", 1.5 } };

double settleFor (const char* id)
{
    for (const auto& s : kSlowerSettles)
        if (juce::String (s.id) == id)
            return s.seconds;

    return kSettleSeconds;
}

/** A recovered output is not a silent one: the last half second must carry
    the signal, which is -18 dBFS RMS going in. */
constexpr double kAliveDb = -40.0;

int checksMade = 0;

void expect (bool condition, const juce::String& what)
{
    ++checksMade;
    check (condition, what);
}

const float kNaN = std::numeric_limits<float>::quiet_NaN();
const float kInf = std::numeric_limits<float>::infinity();

//== The stimulus =============================================================
const std::vector<float>& sine()
{
    static const auto s = []
    {
        std::vector<float> v ((size_t) kLength);
        const auto amplitude = std::sqrt (2.0) * std::pow (10.0, -18.0 / 20.0);

        for (int i = 0; i < kLength; ++i)
            v[(size_t) i] = (float) (amplitude * std::sin (2.0 * juce::MathConstants<double>::pi * 220.0 * i / kRate));

        return v;
    }();

    return s;
}

/** What one render found. `out` is both channels end to end. */
struct Render
{
    std::vector<float> out;
    int  nonFinite     = 0;
    bool metersFinite  = true;
};

/** Runs the sine through `proc` in host-sized blocks, with `bad` written over
    one sample of the left channel (or nothing, when `bad` is zero). `meters`
    reads whatever meters the processor publishes after each block. */
Render render (juce::AudioProcessor& proc, float bad, const std::function<bool()>& meters)
{
    proc.setPlayConfigDetails (2, 2, kRate, kBlock);
    proc.prepareToPlay (kRate, kBlock);

    Render r;
    r.out.resize ((size_t) kLength * 2);

    juce::AudioBuffer<float> buffer (2, kBlock);
    juce::MidiBuffer midi;

    for (int b = 0; b < kBlocks; ++b)
    {
        const auto offset = (size_t) b * kBlock;

        for (int ch = 0; ch < 2; ++ch)
            buffer.copyFrom (ch, 0, sine().data() + offset, kBlock);

        if (b == kBadBlock && bad != 0.0f)
            buffer.setSample (kBadChannel, kBadOffset, bad);

        proc.processBlock (buffer, midi);

        for (int ch = 0; ch < 2; ++ch)
        {
            const auto* read = buffer.getReadPointer (ch);

            for (int i = 0; i < kBlock; ++i)
                if (! std::isfinite (read[i]))
                    ++r.nonFinite;

            std::memcpy (r.out.data() + (size_t) ch * kLength + offset, read, sizeof (float) * kBlock);
        }

        if (meters && ! meters())
            r.metersFinite = false;
    }

    return r;
}

/** The largest difference between two renders from `from` on, in dBFS. A
    non-finite difference is reported as +inf, never as a small number. */
double maxDiffDb (const Render& a, const Render& b, int from)
{
    double worst = 0.0;

    for (int ch = 0; ch < 2; ++ch)
        for (int i = from; i < kLength; ++i)
        {
            const auto d = std::abs ((double) a.out[(size_t) (ch * kLength + i)]
                                     - (double) b.out[(size_t) (ch * kLength + i)]);

            if (! std::isfinite (d))
                return std::numeric_limits<double>::infinity();

            worst = std::max (worst, d);
        }

    return worst > 0.0 ? 20.0 * std::log10 (worst) : -400.0;
}

/** RMS of the last half second, both channels, in dBFS. */
double tailRmsDb (const Render& r)
{
    const auto from = kLength - (int) (kRate * 0.5);
    double sum = 0.0;

    for (int ch = 0; ch < 2; ++ch)
        for (int i = from; i < kLength; ++i)
        {
            const auto v = (double) r.out[(size_t) (ch * kLength + i)];
            sum += v * v;
        }

    const auto rms = std::sqrt (sum / (2.0 * (kLength - from)));
    return std::isfinite (rms) && rms > 0.0 ? 20.0 * std::log10 (rms) : (std::isfinite (rms) ? -400.0 : 400.0);
}

int samplesAfter (double seconds) { return kBadAt + (int) std::lround (seconds * kRate); }

juce::String db (double v)
{
    return std::isfinite (v) ? juce::String (v, 1) : juce::String ("non-finite");
}

/** The figures the bound was chosen from, printed for every case. */
void printFigures (const juce::String& who, const char* what, const Render& clean, const Render& hit)
{
    std::cout << std::left << std::setw (28) << (who + " " + what).toStdString()
              << " non-finite " << std::setw (7) << hit.nonFinite
              << " diff after 0.05 s " << std::setw (11) << db (maxDiffDb (clean, hit, samplesAfter (0.05))).toStdString()
              << " 0.25 s " << std::setw (11) << db (maxDiffDb (clean, hit, samplesAfter (0.25))).toStdString()
              << " 0.5 s "  << std::setw (11) << db (maxDiffDb (clean, hit, samplesAfter (0.5))).toStdString()
              << " 1 s "    << std::setw (11) << db (maxDiffDb (clean, hit, samplesAfter (1.0))).toStdString()
              << " 1.5 s "  << std::setw (11) << db (maxDiffDb (clean, hit, samplesAfter (1.5))).toStdString()
              << " 2 s "    << std::setw (11) << db (maxDiffDb (clean, hit, samplesAfter (2.0))).toStdString()
              << " tail rms " << db (tailRmsDb (hit)).toStdString() << " dBFS\n";
}

/** The three assertions every injected render is held to. */
void expectRecovered (const juce::String& who, const char* what, const Render& clean, const Render& hit,
                      double settleSeconds, double toleranceDb)
{
    const juce::String where { who + ", one " + what };

    printFigures (who, what, clean, hit);

    expect (clean.nonFinite == 0, who + ": the render with no bad sample is finite throughout");
    expect (hit.nonFinite == 0,
            where + ": no non-finite sample leaves the processor, got " + juce::String (hit.nonFinite));
    expect (hit.metersFinite, where + ": every meter a panel reads stays finite");

    const auto diff = maxDiffDb (clean, hit, samplesAfter (settleSeconds));
    expect (diff <= toleranceDb,
            where + ": from " + juce::String (settleSeconds) + " s after it the output is within "
                + juce::String (toleranceDb) + " dBFS of the clean render, got " + db (diff));

    const auto alive = tailRmsDb (hit);
    expect (alive >= kAliveDb && alive < 100.0,
            where + ": the last half second carries signal, " + db (alive) + " dBFS RMS");
}

//== Building things ==========================================================
std::unique_ptr<bmo::SingleModuleProcessor> makeProduct (const bmo::ModuleDef& def)
{
    bmo::ProductInfo info { def.name, { def.name, ".bmotest" }, 1, 1 };
    return std::make_unique<bmo::SingleModuleProcessor> (def, info);
}

bool engineMetersFinite (const bmo::ModuleEngine& e)
{
    return std::isfinite (e.meter().maxPeak()) && std::isfinite (e.meter().maxRms())
        && std::isfinite (e.inputMeter().maxPeak()) && std::isfinite (e.inputMeter().maxRms())
        && std::isfinite (e.gainReduction().get());
}

//== A module that blows up by itself ==========================================
/** Passes its input through until its `blowUpAt`-th block, where it latches:
    from sample 300 of that block on, and on every block after, it puts out
    `bad` on both channels -- until it is reset, which is the only thing that
    clears the latch. That is a filter gone unstable, seen from outside. */
int   blowUpAt = -1;
float blowUpWith = 0.0f;
int   probeResets = 0;

struct BlowUpDsp final : bmo::ModuleDsp
{
    int  blocks = 0;
    bool blown  = false;

    void prepare (double, int, int) override {}
    void reset() override { blown = false; ++probeResets; }
    void setParams (const float*, int) override {}

    void process (float* const* channels, int numChannels, int numSamples) override
    {
        auto from = numSamples;

        if (blocks++ == blowUpAt)
        {
            blown = true;
            from = std::min (300, numSamples);
        }
        else if (blown)
        {
            from = 0;
        }

        for (int ch = 0; ch < numChannels; ++ch)
            for (int i = from; i < numSamples; ++i)
                channels[ch][i] = blowUpWith;
    }

    int latencyForParams (const float*, int) const override { return 0; }
};

const bmo::ModuleDef& blowUpModule()
{
    static const bmo::ParamSpecs specs { bmo::ParamSpec::floatParam ("unused", "Unused", 0.0f, 1.0f, 0.0f, 1.0f) };
    static const std::vector<bmo::FactoryPreset> presets { { "Init", {} } };

    static const bmo::ModuleDef def {
        "blowup", "Blow-up probe", 1, 160, juce::Colours::grey, specs, presets,
        [] { return std::make_unique<BlowUpDsp>(); },
        {} };       // no panel: nothing here opens an editor

    return def;
}

const bmo::ModuleDef& named (const char* id)
{
    for (const auto* def : bmo::products::registry())
        if (juce::String (def->id) == id)
            return *def;

    jassertfalse;
    return *bmo::products::registry().front();
}

} // namespace

//==============================================================================
int main()
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    std::cout << "Bound: within " << kToleranceDb << " dBFS of the clean render from "
              << kSettleSeconds << " s after the bad sample (" << kRackSettleSeconds
              << " s in a rack; longer only for a module in kSlowerSettles).\n";

    //== Every module, through the processor a host drives ====================
    {
        std::vector<const bmo::ModuleDef*> modules (bmo::products::registry().begin(),
                                                    bmo::products::registry().end());

        // A slower settle names a module that exists, or it exempts nothing
        // while looking as if it did.
        for (const auto& s : kSlowerSettles)
            expect (std::any_of (modules.begin(), modules.end(),
                                 [&] (const bmo::ModuleDef* d) { return juce::String (s.id) == d->id; }),
                    juce::String ("the slower settle names a registered module: ") + s.id);

       #if BMO_FINITE_TESTS_TUNE
        // BMO Tune RT runs on the same SingleModuleProcessor, and so behind the
        // same guard, but is not in the rack's registry.
        modules.push_back (&bmo::tune::module());
       #endif

        int walked = 0;

        for (const auto* def : modules)
        {
            const juce::String who { def->id };

            const auto run = [&] (float bad)
            {
                auto proc = makeProduct (*def);
                auto* engine = &proc->getEngine();
                return render (*proc, bad, [engine] { return engineMetersFinite (*engine); });
            };

            const auto clean = run (0.0f);
            expectRecovered (who, "NaN",  clean, run (kNaN), settleFor (def->id), kToleranceDb);
            expectRecovered (who, "+Inf", clean, run (kInf), settleFor (def->id), kToleranceDb);
            ++walked;
        }

        expect (walked == (int) modules.size() && walked >= 11,
                "every registered module was walked, " + juce::String (walked));
    }

    //== A module that blows up by itself: one block of silence ===============
    for (const auto bad : { kNaN, kInf })
    {
        const juce::String what { std::isnan (bad) ? "NaN" : "+Inf" };

        blowUpAt = kBadBlock;
        blowUpWith = bad;
        probeResets = 0;

        auto proc = makeProduct (blowUpModule());
        auto* engine = &proc->getEngine();
        const auto hit = render (*proc, 0.0f, [engine] { return engineMetersFinite (*engine); });

        expect (hit.nonFinite == 0, "blow-up probe (" + what + "): no non-finite sample leaves the processor, got "
                                        + juce::String (hit.nonFinite));
        expect (hit.metersFinite, "blow-up probe (" + what + "): every meter stays finite");
        expect (probeResets == 1, "blow-up probe (" + what + "): the module was reset exactly once, "
                                      + juce::String (probeResets));

        // Exactly what the listener gets: the input up to the block that blew
        // up, that whole block silent on both channels, and the input again
        // from the next block on, sample for sample.
        bool before = true, silent = true, after = true;

        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < kLength; ++i)
            {
                const auto o = hit.out[(size_t) (ch * kLength + i)];
                const auto block = i / kBlock;

                if (block < kBadBlock)       before = before && o == sine()[(size_t) i];
                else if (block == kBadBlock) silent = silent && o == 0.0f;
                else                         after  = after  && o == sine()[(size_t) i];
            }

        expect (before, "blow-up probe (" + what + "): every block before it is the input, unchanged");
        expect (silent, "blow-up probe (" + what + "): the block it blew up in is silent on both channels");
        expect (after,  "blow-up probe (" + what + "): every block after it is the input again, unchanged");

        blowUpAt = -1;
    }

    //== The rack ==============================================================
    {
        const char* chain[] { "util", "eq", "opto", "sat", "fetcomp", "reverb" };

        const auto makeRack = [&] (bool withProbe)
        {
            auto rack = bmo::products::createRack();

            for (size_t i = 0; i < std::size (chain); ++i)
            {
                // The probe goes in the middle: after util, eq and opto, before
                // sat, fetcomp and reverb, so three real modules are downstream
                // of whatever it produces.
                if (withProbe && i == 3)
                    rack->addModule (blowUpModule());

                rack->addModule (named (chain[i]));
            }

            return rack;
        };

        const auto rackMeters = [] (RackProcessor& rack)
        {
            return [&rack]
            {
                for (int s = 0; s < rack.getNumModules(); ++s)
                    if (! engineMetersFinite (*rack.getEngineAt (s)))
                        return false;

                return true;
            };
        };

        // At the input: the host hands the rack a bad sample.
        {
            const auto run = [&] (float bad)
            {
                auto rack = makeRack (false);
                return render (*rack, bad, rackMeters (*rack));
            };

            const auto clean = run (0.0f);
            expectRecovered ("rack (input)", "NaN",  clean, run (kNaN), kRackSettleSeconds, kToleranceDb);
            expectRecovered ("rack (input)", "+Inf", clean, run (kInf), kRackSettleSeconds, kToleranceDb);
        }

        // Mid-chain: the probe in slot 4 blows up by itself, on finite input.
        {
            blowUpAt = -1;
            const auto clean = [&] { auto rack = makeRack (true); return render (*rack, 0.0f, rackMeters (*rack)); }();

            for (const auto bad : { kNaN, kInf })
            {
                blowUpAt = kBadBlock;
                blowUpWith = bad;
                probeResets = 0;

                auto rack = makeRack (true);
                const auto hit = render (*rack, 0.0f, rackMeters (*rack));

                expect (probeResets == 1, "rack (mid-chain): the probe was reset exactly once, "
                                              + juce::String (probeResets));

                // The probe's block is silenced from its first sample, 100
                // before `kBadAt`, which the settle is measured from: 2 ms
                // against a settle of half a second.
                expectRecovered ("rack (mid-chain)", std::isnan (bad) ? "NaN" : "+Inf", clean, hit,
                                 kRackSettleSeconds, kToleranceDb);
            }

            blowUpAt = -1;
        }
    }

    std::cout << checksMade << " checks made.\n";
    return finish ("finite");
}
