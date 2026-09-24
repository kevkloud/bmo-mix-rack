/*
    Tail-length reporting, across the whole suite at once.

    `ModuleDsp::tailSecondsForParams` is a new virtual on every module's
    vtable, defaulted to 0.0 so that no module file had to change. A default is
    a promise made on eight modules' behalf by a file none of them can see, so
    this suite goes and checks it on all eight rather than trusting it: walk
    the rack registry, and for every module that is not BMO Linger assert the
    figure is EXACTLY zero -- at the schema defaults, at every parameter's
    minimum and maximum one at a time, and with the whole schema pinned to each
    end at once.

    **The assertions are absolute, never a comparison between two runs of the
    same code.** tests/dsp/OptoDspTests.cpp is the reason the house writes them
    this way: a relative test there passed for an entire release while both of
    the things it was comparing were broken. So BMO Linger's figures below are
    written-down seconds, arrived at from the formula in
    docs/reverb/10-dsp-spec.md 5 by hand, and the rack's total is a written-down
    sum of two written-down parts rather than "the sum of whatever the slots
    say".

    **The rack clamps its total at thirty seconds too**, as of 2026-09-21, so
    the product has one rule instead of two. The sum is still what a chain
    under the ceiling reports -- the 8.581 s two-slot figure is asserted after
    the clamp as well as before it -- and what the clamp stops is the case the
    slot limit does not: eight BMO Lingers, each honestly reporting its own
    thirty, rendering four minutes of silence onto the end of every offline
    bounce.

    The other half of the proof is that the zero is not vacuous -- a default
    that is never overridden would pass every assertion in the first half. BMO
    Linger is asserted NON-zero at its own defaults, so if the override were
    dropped or the new virtual silently hidden by a differing signature, the
    reverb section goes red rather than the suite going quiet.
*/

#include "TestUtil.h"
#include "core/product/SingleModuleProcessor.h"
#include "core/rack/RackProcessor.h"
#include "products/rack/Product.h"
#include "products/rack/Registry.h"
#include "modules/reverb/params.h"

using namespace test;
using bmo::RackProcessor;

namespace
{

constexpr double kRate  = 48000.0;
constexpr int    kBlock = 512;

/** The only module in the suite that reports a tail. Everything else in the
    registry takes `ModuleDsp`'s zero default, and the walk below asserts that
    this list is exactly one long -- so a module added later is checked for a
    zero, or its author has to come here and say why not. */
constexpr auto kRingingModule = "reverb";

//== BMO Linger's stated figures ==============================================
//
// T_tail = preDelay + T_mid * max(1, r_lo, r_hi) + t_ER,max + 0.05 s, clamped
// to 30 s (docs/reverb/10-dsp-spec.md 5). t_ER,max is **the selected type's
// own ER span** since 2026-09-24: its latest tap, by the Size law and held to
// the type's window clamp -- `erSpanMsAt (table, size)`, which is the law the
// ER engine plays by. Every setting below is TYPE Room, whose table's latest
// tap is 98.078 ms at the 12 m reference, and whose window reaches its 100 ms
// clamp at 12 m, so **Room's span is 98.078 ms at every SIZE from 12 m up**.
// (`measure_reverb tail` prints the span per type and size.)
//
// Until 2026-09-24 t_ER,max was the placeholder table's last tap, 79.1 ms at
// 12 m scaling with SIZE and never clamped; the old figures are in the
// comments beside the new ones.
//
// Each row is the arithmetic written out, then the answer. They are settings
// and seconds, not expressions evaluated by the same code under test.

/** The schema defaults: Room, 12 m, no pre-delay, 1.8 s decay, damping 1.20
    low and 0.40 high.
    0 + 1.8 * 1.20 + 98.078 ms + 0.05 = 2.16 + 0.098078 + 0.05
    (was 2.2891, with the placeholder's 79.1 ms) */
constexpr double kDefaultTail = 2.308078;

/** A deliberately unround setting, so the test cannot pass on a coincidence:
    125 ms pre-delay, 4 s decay, damping 1.50 low and 0.50 high, 24 m. At 24 m
    Room's ER is held at its clamp, so its span is still 98.078 ms:
    0.125 + 4.0 * 1.50 + 98.078 ms + 0.05 = 0.125 + 6.0 + 0.098078 + 0.05
    (was 6.3332, with the placeholder's 158.2 ms at 24 m) */
constexpr double kLongTail = 6.273078;

/** Both damping multipliers under unity. The formula floors the multiplier at
    1, so a dark room is not reported SHORTER than its own mid-band decay:
    0 + 3.0 * 1.00 + 98.078 ms + 0.05
    (was 3.1291) */
constexpr double kDarkTail = 3.148078;

/** The ceiling, and **it is now the rack's as well as the module's**. 250 ms +
    20 s decay at a 2.0 multiplier + Room's 98.078 ms of early reflections
    (held to its clamp at 80 m) + 0.05 is 40.398 s of honest arithmetic (it
    was 40.827 with the placeholder's unclamped 527 ms), and the host is told 30 -- and a rack of eight of those
    is told 30 too, rather than four minutes. Written out here as the seconds
    it is, and checked against `bmo::kMaxTailSeconds` at the foot of the rack
    section so this suite cannot quietly disagree with the code. */
constexpr double kClampedTail = 30.0;

/** Two occupied slots, in series: the rack adds them.
    A MAXIMUM would report 6.273078 here, so the two answers cannot be confused,
    and it is **under the rack's ceiling**, so the clamp must leave it alone. */
constexpr double kSummedTail = kDefaultTail + kLongTail;    // 8.581156 (was 8.6223)

//== Building things ==========================================================
std::unique_ptr<bmo::SingleModuleProcessor> makeProduct (const bmo::ModuleDef& def)
{
    bmo::ProductInfo info { def.name, { def.name, ".bmotest" }, 1, 1 };
    return std::make_unique<bmo::SingleModuleProcessor> (def, info);
}

/** BMO Linger's five tail parameters. Everything else stays at its default,
    because nothing else is in the formula -- which is itself worth a test, and
    gets one below. */
struct Setting
{
    float preDelayMs, decaySeconds, dampLo, dampHi, sizeM;
};

constexpr Setting kDefaults { 0.0f,   1.8f, 1.20f, 0.40f, 12.0f };
constexpr Setting kLong     { 125.0f, 4.0f, 1.50f, 0.50f, 24.0f };
constexpr Setting kDark     { 0.0f,   3.0f, 0.20f, 0.20f, 12.0f };
constexpr Setting kWorst    { 250.0f, 20.0f, 2.00f, 2.00f, 80.0f };

void apply (bmo::ParamSet& params, const Setting& s)
{
    namespace P = bmo::reverb;

    params.setReal (P::kPreDelay, s.preDelayMs);
    params.setReal (P::kDecay,    s.decaySeconds);
    params.setReal (P::kDampLo,   s.dampLo);
    params.setReal (P::kDampHi,   s.dampHi);
    params.setReal (P::kSize,     s.sizeM);
}

const bmo::ModuleDef& moduleNamed (const char* id)
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

    //== Every shipped module reports exactly zero ============================
    //
    // Exactly: `== 0.0`, not "close to". A module with no tail has no rounding
    // to do, and a figure that is nearly zero means somebody computed
    // something, which is a different bug from the one a tolerance would hide.
    {
        int ringing = 0;

        for (const auto* def : bmo::products::registry())
        {
            if (juce::String (def->id) == kRingingModule)
            {
                ++ringing;
                continue;
            }

            const juce::String who { def->id };
            auto proc = makeProduct (*def);
            auto& params = proc->getEngine().params();

            // (1) At the defaults, through the engine...
            check (proc->getEngine().tailSeconds() == 0.0,
                   who + " reports no tail at its defaults");

            // ...and through the host-facing getter, which is a cache and so
            // has to have been refreshed by something. prepareToPlay is the
            // path a host always takes.
            proc->setPlayConfigDetails (2, 2, kRate, kBlock);
            proc->prepareToPlay (kRate, kBlock);
            check (proc->getTailLengthSeconds() == 0.0,
                   who + " tells the host no tail at its defaults");

            // (2) Every parameter to each end of its range, one at a time, the
            // rest left at their defaults. One at a time because a whole-schema
            // sweep can have two settings cancel: BMO Sat's oversampling row at
            // its maximum with its drive at the minimum is a quiet plugin, and
            // a bug that keyed off one of them would hide behind the other.
            for (int i = 0; i < params.size(); ++i)
            {
                const auto& spec = def->specs[(size_t) i];
                const auto restore = params.getReal (i);

                for (const auto corner : { spec.min, spec.max })
                {
                    params.setReal (i, corner);
                    check (proc->getEngine().tailSeconds() == 0.0,
                           who + " reports no tail with " + spec.id + " at "
                               + juce::String (corner));
                }

                params.setReal (i, restore);
            }

            // (3) And the whole schema pinned to one end at once, which is the
            // corner a one-at-a-time walk cannot reach.
            for (const auto low : { true, false })
            {
                for (int i = 0; i < params.size(); ++i)
                    params.setReal (i, low ? def->specs[(size_t) i].min
                                           : def->specs[(size_t) i].max);

                proc->prepareToPlay (kRate, kBlock);

                check (proc->getEngine().tailSeconds() == 0.0,
                       who + (low ? " reports no tail with every parameter at its minimum"
                                  : " reports no tail with every parameter at its maximum"));
                check (proc->getTailLengthSeconds() == 0.0,
                       who + (low ? " tells the host no tail at the bottom of its schema"
                                  : " tells the host no tail at the top of its schema"));
            }
        }

        // The exception list is one long, and it is the reverb. A module added
        // to the registry later is walked by the loop above whether or not
        // anybody remembers this file exists.
        check (ringing == 1,
               "exactly one registered module is exempt from the zero, got "
                   + juce::String (ringing));
        check (bmo::products::registry().size() == 10,
               "the registry still holds ten modules, got "
                   + juce::String ((int) bmo::products::registry().size()));
    }

    //== BMO Linger's own figures =============================================
    {
        auto proc = makeProduct (moduleNamed ("reverb"));
        auto& params = proc->getEngine().params();
        proc->setPlayConfigDetails (2, 2, kRate, kBlock);

        const auto tailAt = [&] (const Setting& s)
        {
            apply (params, s);
            proc->prepareToPlay (kRate, kBlock);
            return proc->getTailLengthSeconds();
        };

        // The one assertion that makes every zero above mean something: the
        // default is overridden, so the zeros are answers and not silence.
        check (proc->getEngine().tailSeconds() > 0.0,
               "BMO Linger reports a tail at all");

        checkClose (tailAt (kDefaults), kDefaultTail, 1.0e-4,
                    "BMO Linger's tail at the schema defaults");
        checkClose (tailAt (kLong), kLongTail, 1.0e-4,
                    "BMO Linger's tail at 125 ms / 4 s / 1.50x / 24 m");
        checkClose (tailAt (kDark), kDarkTail, 1.0e-4,
                    "damping under unity is floored at 1.00x, not allowed to shorten the tail");
        checkClose (tailAt (kWorst), kClampedTail, 1.0e-4,
                    "40.8 s of arithmetic is reported as the 30 s ceiling");

        // The clamp is a ceiling and not a fixed answer: a setting just under
        // it has to still be reported as itself.
        //   0 + 20.0 * 1.00 + 98.078 ms + 0.05 = 20.148078 (was 20.1291)
        checkClose (tailAt ({ 0.0f, 20.0f, 1.00f, 1.00f, 12.0f }), 20.148078, 1.0e-4,
                    "20.1 s is under the ceiling and is reported in full");

        // Nothing outside the formula moves it. MIX at zero is a bypassed
        // reverb by ear and a ringing one by contract -- the host still has to
        // pull the tail through, because MIX is automatable and can come back.
        {
            apply (params, kDefaults);
            params.setReal (bmo::reverb::kMix, 0.0f);
            params.setReal (bmo::reverb::kErLevel, -40.0f);
            params.setReal (bmo::reverb::kVerbLevel, -40.0f);
            params.setReal (bmo::reverb::kOutput, -24.0f);
            proc->prepareToPlay (kRate, kBlock);

            checkClose (proc->getTailLengthSeconds(), kDefaultTail, 1.0e-4,
                        "the levels and the mix are not in the tail formula");
        }
    }

    //== The rack adds its slots up ===========================================
    {
        const auto& reverb = moduleNamed ("reverb");
        const auto& util   = moduleNamed ("util");

        auto rack = bmo::products::createRack();
        rack->setPlayConfigDetails (2, 2, kRate, kBlock);

        // Empty: nothing to add up.
        rack->prepareToPlay (kRate, kBlock);
        check (rack->getTailLengthSeconds() == 0.0, "an empty rack has no tail");

        // One reverb, and the rack agrees with the standalone.
        rack->addModule (reverb);
        apply (rack->getEngineAt (0)->params(), kDefaults);
        rack->prepareToPlay (kRate, kBlock);
        checkClose (rack->getTailLengthSeconds(), kDefaultTail, 1.0e-4,
                    "one slot reports what the module reports");

        // A tailless module in between adds nothing.
        rack->addModule (util);
        rack->prepareToPlay (kRate, kBlock);
        checkClose (rack->getTailLengthSeconds(), kDefaultTail, 1.0e-4,
                    "BMO Util in the chain adds nothing to the tail");

        // Two occupied reverb slots in series. The sum, not the maximum: the
        // first is still feeding the second when the first has finished, so
        // the chain rings for longer than either of them.
        rack->addModule (reverb);
        apply (rack->getEngineAt (2)->params(), kLong);
        rack->prepareToPlay (kRate, kBlock);

        checkClose (rack->getTailLengthSeconds(), kSummedTail, 1.0e-4,
                    "two reverbs in series report the sum of their tails");
        check (rack->getTailLengthSeconds() > kLongTail + 1.0,
               "the rack is not reporting the longer of the two");

        // Removing one takes its share back off again.
        rack->removeModule (2);
        rack->prepareToPlay (kRate, kBlock);
        checkClose (rack->getTailLengthSeconds(), kDefaultTail, 1.0e-4,
                    "removing a reverb removes its tail from the total");

        // **And the rack clamps its own total at the same ceiling.** Until
        // 2026-09-21 this line asserted 60 s, on the argument that each
        // module's 30 is the honest ceiling on what IT rings for and the slots
        // are in series. That is still true of the arithmetic and is no longer
        // what a host is told: `addModule` counts slots and never looks for
        // duplicates, so eight BMO Lingers is a legal chain and eight honest
        // thirties is a four-minute tail -- which an offline bounce renders
        // onto the end of every export. Frosty approved the rack clamp.
        rack->clearChain();
        rack->addModule (reverb);
        rack->addModule (reverb);
        apply (rack->getEngineAt (0)->params(), kWorst);
        apply (rack->getEngineAt (1)->params(), kWorst);
        rack->prepareToPlay (kRate, kBlock);
        checkClose (rack->getTailLengthSeconds(), kClampedTail, 1.0e-4,
                    "two maxed reverbs are clamped to the 30 s ceiling, not summed to 60");

        // Eight of them, which is the chain the clamp was actually written
        // for: the slot limit is the only thing stopping this growing, and it
        // does not stop it at anything a host should be handed.
        rack->clearChain();

        for (int i = 0; i < 8; ++i)
        {
            check (rack->addModule (reverb), "slot " + juce::String (i) + " takes a reverb");
            apply (rack->getEngineAt (i)->params(), kWorst);
        }

        rack->prepareToPlay (kRate, kBlock);
        checkClose (rack->getTailLengthSeconds(), kClampedTail, 1.0e-4,
                    "a rack of eight maxed reverbs reports the ceiling, not four minutes");

        // **The clamp is a ceiling and not a fixed answer**, which is the half
        // of it a `return 30.0` would also pass. The two-slot sum is below the
        // ceiling and has to come back in full, unaltered by the change above.
        rack->clearChain();
        rack->addModule (reverb);
        rack->addModule (reverb);
        apply (rack->getEngineAt (0)->params(), kDefaults);
        apply (rack->getEngineAt (1)->params(), kLong);
        rack->prepareToPlay (kRate, kBlock);
        checkClose (rack->getTailLengthSeconds(), kSummedTail, 1.0e-4,
                    "8.581 s is under the ceiling and is still reported as the sum");

        // And the rack's ceiling is the module's ceiling, read from the one
        // place it is decided rather than from a second 30.0 written here.
        checkClose (kClampedTail, bmo::kMaxTailSeconds, 1.0e-12,
                    "the figure this suite writes down is the suite's own constant");
    }

    return finish ("tail");
}
