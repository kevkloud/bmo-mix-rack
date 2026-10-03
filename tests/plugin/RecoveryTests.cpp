/*
    How long each module takes to come back from one very loud sample that
    the guard lets through.

    **These are RECORDED figures, not targets.** The guard in
    `ModuleEngine::process` zeroes a sample that is not finite or is at or
    over `bmo::finite::kCeiling` (2^32, +192.7 dBFS). Under the ceiling a
    sample is the module's to answer, however loud, because legal settings
    make signals that loud between rack slots and a guard must not punch holes
    in them. That was the owner's decision on 2026-10-03, with the reason:
    the guard stops samples that are not audio; the long hold after a huge
    one is the compressors' own recovery, and that is being fixed in those
    modules, separately.

    So this file writes the recovery down rather than judging it. For one
    sample at +60, +72 and +96 dBFS and at 4e9 (+192.0 dBFS, just under the
    ceiling), in the left channel of a steady 220 Hz sine at -18 dBFS RMS,
    each module's recovery time is measured: from the bad sample to the last
    sample further than -60 dBFS from the same render without it (the bound
    finite_tests uses), confirmed by `kStaySeconds` inside the bound after it,
    and given up at `kCapSeconds`. Each is asserted against the figure
    recorded below plus `kMarginFactor` and `kMarginSeconds`.

    **A module whose recovery improves should have its row tightened** to the
    new measured figure, in the same change; one that gets slower fails here,
    which is the point. Run `recovery_tests --print` for the table as
    measured, in the form the rows below are written in. A module added to
    the registry without a row fails until it has one.

    BMO Tune RT is the one module whose difference never comes inside
    -60 dBFS, at any level: it settles at about -48 dBFS and stays there,
    without decaying, for the rest of the render, while its output level is
    unchanged (the same holds after a NaN in finite_tests, at -74 dBFS). Its
    row records "never" (-1) and the level the difference settles at, which
    is asserted instead.
*/

#include "TestUtil.h"
#include "core/product/SingleModuleProcessor.h"
#include "products/rack/Registry.h"

#if BMO_RECOVERY_TESTS_TUNE
 #include "modules/tune/Module.h"
#endif

#include <algorithm>
#include <cmath>
#include <cstring>
#include <iomanip>

using namespace test;

namespace
{

constexpr double kRate  = 48000.0;
constexpr int    kBlock = 512;

constexpr int    kBadBlock  = 48;
constexpr int    kBadOffset = 100;
constexpr int    kBadAt     = kBadBlock * kBlock + kBadOffset;

constexpr double kToleranceDb  = -60.0;
constexpr double kStaySeconds  = 10.0;
constexpr double kCapSeconds   = 90.0;

/** The bound is the recorded figure times this plus that: a quarter again
    for a platform whose maths lands a little differently, and a quarter of
    a second so that a module recorded at zero is not held to it exactly. */
constexpr double kMarginFactor  = 1.25;
constexpr double kMarginSeconds = 0.25;

/** For a module that never comes inside the bound: how far over the recorded
    settled difference it may be, in dB. */
constexpr double kSettledMarginDb = 3.0;

/** The four levels, as samples. */
constexpr float kLevels[] { 1000.0f, 3981.0717f, 63095.734f, 4.0e9f };
constexpr const char* kLevelNames[] { "+60 dBFS", "+72 dBFS", "+96 dBFS", "4e9" };
constexpr int kNumLevels = 4;

/** One module's row: seconds to recover at each level, -1 for never by the
    cap; and, for a module that never recovers, the dBFS its difference
    settles at over the last `kStaySeconds`. */
struct Recorded
{
    const char* id;
    double seconds[kNumLevels];
    double settledDb;
};

// Measured on ICE QUEEN, 2026-10-03, at 3d62316, with `recovery_tests --print`.
// Seconds from the bad sample to the last sample outside -60 dBFS of the
// clean render. Opto and FET are the compressors whose own recovery this is.
//
// Opto's row was re-measured on 2026-10-04 on the merge with main through #40,
// where a spike no longer charges the cell: 2.35, 7.67, 18.22 and 61.64 s
// became the figures below, and the bound was tightened with them, as this
// test's header asks when a module's recovery improves.
constexpr Recorded kRecorded[] {
    //                 +60      +72      +96      4e9       settled
    { "util",    { 0.00,    0.00,    0.00,    0.00  }, 0.0 },
    { "eq",      { 0.27,    0.32,    0.41,    0.76  }, 0.0 },
    { "sat",     { 0.04,    0.05,    0.09,    0.03  }, 0.0 },
    { "opto",    { 0.19,    0.27,    0.44,    1.10  }, 0.0 },
    { "dim",     { 0.00,    0.00,    0.00,    0.00  }, 0.0 },
    { "deq",     { 0.00,    0.00,    0.00,    0.00  }, 0.0 },
    { "ltvcomp", { 0.43,    0.44,    0.45,    0.50  }, 0.0 },
    { "deesser", { 0.00,    0.00,    0.00,    0.00  }, 0.0 },
    { "fetcomp", { 1.17,    1.28,    2.93,    12.97 }, 0.0 },
    { "dwell",   { 1.88,    1.88,    1.90,    1.90  }, 0.0 },
    { "reverb",  { 0.06,    0.06,    0.06,    0.06  }, 0.0 },
    { "tune",    { -1,      -1,      -1,      -1    }, -47.9 },
};

int checksMade = 0;

void expect (bool condition, const juce::String& what)
{
    ++checksMade;
    check (condition, what);
}

float sineAt (long long i)
{
    static const auto amplitude = std::sqrt (2.0) * std::pow (10.0, -18.0 / 20.0);
    return (float) (amplitude * std::sin (2.0 * juce::MathConstants<double>::pi * 220.0 * (double) i / kRate));
}

std::unique_ptr<bmo::SingleModuleProcessor> makeProduct (const bmo::ModuleDef& def)
{
    bmo::ProductInfo info { def.name, { def.name, ".bmotest" }, 1, 1 };
    auto proc = std::make_unique<bmo::SingleModuleProcessor> (def, info);
    proc->setPlayConfigDetails (2, 2, kRate, kBlock);
    proc->prepareToPlay (kRate, kBlock);
    return proc;
}

/** The clean render of one module, extended a block at a time as a
    measurement asks for more of it, so every level shares it. */
struct CleanRender
{
    explicit CleanRender (const bmo::ModuleDef& def) : proc (makeProduct (def)) {}

    const float* block (int b, int ch)
    {
        while ((int) blocks.size() <= b)
        {
            const auto n = (int) blocks.size();
            juce::AudioBuffer<float> buffer (2, kBlock);

            for (int c = 0; c < 2; ++c)
                for (int i = 0; i < kBlock; ++i)
                    buffer.setSample (c, i, sineAt ((long long) n * kBlock + i));

            proc->processBlock (buffer, midi);

            std::vector<float> both ((size_t) kBlock * 2);
            std::memcpy (both.data(), buffer.getReadPointer (0), sizeof (float) * kBlock);
            std::memcpy (both.data() + kBlock, buffer.getReadPointer (1), sizeof (float) * kBlock);
            blocks.push_back (std::move (both));
        }

        return blocks[(size_t) b].data() + (size_t) ch * kBlock;
    }

    std::unique_ptr<bmo::SingleModuleProcessor> proc;
    juce::MidiBuffer midi;
    std::vector<std::vector<float>> blocks;
};

struct Measured
{
    double seconds   = -1.0;      // -1: not back by the cap
    double settledDb = -400.0;    // the largest difference over the last kStaySeconds
    bool   allAudio  = true;      // nothing out was non-finite or at or over the ceiling
};

Measured measure (const bmo::ModuleDef& def, CleanRender& clean, float level)
{
    auto proc = makeProduct (def);
    juce::AudioBuffer<float> buffer (2, kBlock);
    juce::MidiBuffer midi;

    const auto bound      = std::pow (10.0, kToleranceDb / 20.0);
    const auto stayBlocks = (int) std::ceil (kStaySeconds * kRate / kBlock);
    const auto lastBlock  = (int) std::ceil ((kCapSeconds + kStaySeconds) * kRate / kBlock) + kBadBlock;

    long long lastOut = -1;         // the last sample index further than the bound
    int lastOutBlock = -1;
    Measured m;

    for (int b = 0; b <= lastBlock; ++b)
    {
        for (int c = 0; c < 2; ++c)
            for (int i = 0; i < kBlock; ++i)
                buffer.setSample (c, i, sineAt ((long long) b * kBlock + i));

        if (b == kBadBlock)
            buffer.setSample (0, kBadOffset, level);

        proc->processBlock (buffer, midi);

        for (int c = 0; c < 2; ++c)
        {
            const auto* ref = clean.block (b, c);
            const auto* out = buffer.getReadPointer (c);

            for (int i = 0; i < kBlock; ++i)
            {
                if (! std::isfinite (out[i]) || std::abs (out[i]) >= bmo::finite::kCeiling)
                    m.allAudio = false;

                if (std::abs ((double) out[i] - (double) ref[i]) > bound)
                {
                    lastOut = std::max (lastOut, (long long) b * kBlock + i);
                    lastOutBlock = b;
                }
            }
        }

        // Back, and stayed back for kStaySeconds: done.
        if (b > kBadBlock && b - std::max (lastOutBlock, kBadBlock) >= stayBlocks)
        {
            m.seconds = lastOut < kBadAt ? 0.0 : (double) (lastOut + 1 - kBadAt) / kRate;
            return m;
        }

        // The settled difference over the last stay window of a module that
        // has not come back.
        if (b > lastBlock - stayBlocks)
            for (int c = 0; c < 2; ++c)
                for (int i = 0; i < kBlock; ++i)
                {
                    const auto d = std::abs ((double) buffer.getSample (c, i) - (double) clean.block (b, c)[i]);
                    m.settledDb = std::max (m.settledDb, d > 0.0 ? 20.0 * std::log10 (d) : -400.0);
                }
    }

    return m;
}

const Recorded* rowFor (const char* id)
{
    for (const auto& r : kRecorded)
        if (juce::String (r.id) == id)
            return &r;

    return nullptr;
}

juce::String seconds (double s)
{
    return s < 0.0 ? juce::String ("never") : juce::String (s, 2) + " s";
}

} // namespace

//==============================================================================
int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    std::cout << std::unitbuf;

    const auto printing = argc > 1 && juce::String (argv[1]) == "--print";

    std::vector<const bmo::ModuleDef*> modules (bmo::products::registry().begin(),
                                                bmo::products::registry().end());
   #if BMO_RECOVERY_TESTS_TUNE
    modules.push_back (&bmo::tune::module());
   #endif

    for (const auto& r : kRecorded)
        expect (std::any_of (modules.begin(), modules.end(), [&] (const bmo::ModuleDef* d) { return juce::String (r.id) == d->id; }),
                juce::String ("a recorded row names a module: ") + r.id);

    std::cout << std::left << std::setw (10) << "module";
    for (const auto* name : kLevelNames)
        std::cout << std::setw (12) << name;
    std::cout << "settled\n";

    for (const auto* def : modules)
    {
        CleanRender clean (*def);
        Measured got[kNumLevels];

        for (int l = 0; l < kNumLevels; ++l)
            got[l] = measure (*def, clean, kLevels[l]);

        double settled = -400.0;

        std::cout << std::setw (10) << def->id;
        for (const auto& g : got)
        {
            std::cout << std::setw (12) << seconds (g.seconds).toStdString();
            if (g.seconds < 0.0)
                settled = std::max (settled, g.settledDb);
        }
        std::cout << (settled > -400.0 ? juce::String (settled, 1) + " dBFS" : juce::String ("-")).toStdString() << "\n";

        if (printing)
        {
            std::cout << "    { \"" << def->id << "\", { ";
            for (int l = 0; l < kNumLevels; ++l)
                std::cout << (got[l].seconds < 0.0 ? juce::String ("-1") : juce::String (got[l].seconds, 2)).toStdString()
                          << (l + 1 < kNumLevels ? ", " : " }, ");
            std::cout << (settled > -400.0 ? juce::String (settled, 1) : juce::String ("0.0")).toStdString() << " },\n";
            continue;
        }

        const auto* row = rowFor (def->id);
        expect (row != nullptr, juce::String (def->id) + " has a recorded row; run recovery_tests --print");

        for (int l = 0; l < kNumLevels; ++l)
        {
            const juce::String where { juce::String (def->id) + ", one sample at " + kLevelNames[l] };

            expect (got[l].allAudio, where + ": nothing out is non-finite or at or over the ceiling");

            if (row == nullptr)
                continue;

            const auto recorded = row->seconds[l];

            if (recorded < 0.0)
            {
                expect (got[l].seconds >= 0.0 || got[l].settledDb <= row->settledDb + kSettledMarginDb,
                        where + ": settles within " + juce::String (kSettledMarginDb) + " dB of the recorded "
                            + juce::String (row->settledDb, 1) + " dBFS, got " + juce::String (got[l].settledDb, 1));
                continue;
            }

            const auto bound = recorded * kMarginFactor + kMarginSeconds;
            expect (got[l].seconds >= 0.0 && got[l].seconds <= bound,
                    where + ": back within " + juce::String (kToleranceDb) + " dBFS of the clean render in "
                        + seconds (got[l].seconds) + ", recorded " + seconds (recorded) + ", bound "
                        + juce::String (bound, 2) + " s");
        }
    }

    std::cout << checksMade << " checks made.\n";
    return finish ("recovery");
}
