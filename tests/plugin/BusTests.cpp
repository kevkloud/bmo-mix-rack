/*
    Bus layouts, and the proof that widening them changed nothing.

    Mono-in -> stereo-out is new. Before it, both processors demanded that the
    input channel set equal the output channel set, so a module could only ever
    see 1 -> 1 or 2 -> 2. Accepting 1 -> 2 makes every shipped module
    instantiable in a layout that could not previously exist, and the obvious
    implementation -- clear the channels the input does not cover -- would hand
    each of them a stereo pair with a silent right channel. That is a hard-left
    signal, not a mono one. BMO Dimension would image it (its own guard only
    catches numChannels < 2; see modules/dim/dsp/DspCore.h and the +1.17 dB it
    records), and every other module would put its output on the left alone.

    So the input is DUPLICATED, not cleared: on a 1 -> 2 layout channel 0 is
    copied into channel 1 before the engine runs. Every module then sees a
    correlated stereo pair, which is exactly what it already sees today when a
    host feeds the same signal to both inputs of a stereo instance -- a path
    that has shipped since 1.0. Nothing in any module changes, and a module
    that wants to decorrelate (a reverb tail) has the whole mono signal in both
    channels to do it from.

    That claim is what the golden tables below pin down. They are ABSOLUTE
    numbers, not a before/after comparison inside one run: OptoDspTests learned
    the hard way that a relative test passes happily while both sides are
    broken. Every number here was captured by running this file's --print mode
    against the build at 8fed835, BEFORE either processor was touched, and then
    pasted in. A module whose mono->mono or stereo->stereo output moves by more
    than a whisker fails against a constant written down by code that predates
    the change.

    Case C ("dup") is the pre-existing stereo->stereo path fed the same signal
    on both inputs. Case D is the NEW mono->stereo layout, and it is asserted
    against case C's constants: duplication means the two must agree, so the
    new path is pinned to a number the old code produced.

    Regenerate the tables with:  bus_tests --print
*/

#include "TestUtil.h"
#include "core/product/SingleModuleProcessor.h"
#include "core/rack/RackProcessor.h"
#include "products/rack/Product.h"
#include "products/rack/Registry.h"

#include <cstring>
#include <iomanip>

using namespace test;
using bmo::RackProcessor;

namespace
{

constexpr double kRate   = 48000.0;
constexpr int    kBlock  = 512;
constexpr int    kBlocks = 8;
constexpr int    kLength = kBlock * kBlocks;

/** What a host would leave in a channel the input bus does not cover: not
    silence. If a processor forgets to write channel 1 on a 1 -> 2 layout this
    walks straight into the measurement. */
constexpr float kGarbage = 7.0f;

//== Signals ==================================================================
/** A sine plus seeded noise at -18 dBFS RMS -- the level the house measures
    at. `seed` picks an unrelated waveform, so L and R are decorrelated. */
std::vector<float> signalFor (int seed)
{
    juce::Random random (seed);
    std::vector<float> out ((size_t) kLength);

    const auto hz = 110.0 * (double) seed;

    for (int i = 0; i < kLength; ++i)
    {
        const auto t = (double) i / kRate;
        out[(size_t) i] = (float) (0.7 * std::sin (juce::MathConstants<double>::twoPi * hz * t)
                                 + 0.3 * ((double) random.nextFloat() * 2.0 - 1.0));
    }

    double sum = 0.0;
    for (auto v : out) sum += (double) v * v;

    const auto scale = (float) (juce::Decibels::decibelsToGain (-18.0)
                                  / std::sqrt (sum / (double) kLength));
    for (auto& v : out) v *= scale;

    return out;
}

const std::vector<float>& signalA() { static const auto s = signalFor (1); return s; }
const std::vector<float>& signalB() { static const auto s = signalFor (3); return s; }

//== Layouts ==================================================================
juce::AudioProcessor::BusesLayout layoutOf (const juce::AudioChannelSet& in,
                                            const juce::AudioChannelSet& out)
{
    juce::AudioProcessor::BusesLayout l;
    l.inputBuses.add (in);
    l.outputBuses.add (out);
    return l;
}

const auto kMono   = juce::AudioChannelSet::mono();
const auto kStereo = juce::AudioChannelSet::stereo();

//== Rendering ================================================================
/** RMS and peak of each output channel, in dB and in linear units. */
struct Render
{
    int    channels = 0;
    double rms[2]   { 0.0, 0.0 };
    double peak[2]  { 0.0, 0.0 };
    bool   ran      = false;
};

/** Runs `proc` in the given layout over kLength samples.

    `numIn == 1` feeds `a` into channel 0 and, when the layout is wider than
    the input, leaves kGarbage in channel 1 for the processor to deal with.
    `numIn == 2` feeds `a` and `b`. */
Render render (juce::AudioProcessor& proc, int numIn, int numOut,
               const std::vector<float>& a, const std::vector<float>& b)
{
    Render r;
    r.channels = numOut;

    if (! proc.setBusesLayout (layoutOf (numIn == 1 ? kMono : kStereo,
                                         numOut == 1 ? kMono : kStereo)))
        return r;

    proc.setRateAndBufferSizeDetails (kRate, kBlock);
    proc.prepareToPlay (kRate, kBlock);

    juce::AudioBuffer<float> buffer (juce::jmax (numIn, numOut), kBlock);
    juce::MidiBuffer midi;

    double sum[2] { 0.0, 0.0 };

    for (int blockIndex = 0; blockIndex < kBlocks; ++blockIndex)
    {
        const auto offset = (size_t) (blockIndex * kBlock);

        buffer.copyFrom (0, 0, a.data() + offset, kBlock);

        if (buffer.getNumChannels() > 1)
        {
            if (numIn == 2)
                buffer.copyFrom (1, 0, b.data() + offset, kBlock);
            else
                juce::FloatVectorOperations::fill (buffer.getWritePointer (1), kGarbage, kBlock);
        }

        proc.processBlock (buffer, midi);

        for (int ch = 0; ch < numOut; ++ch)
        {
            const auto* read = buffer.getReadPointer (ch);

            for (int i = 0; i < kBlock; ++i)
            {
                sum[ch] += (double) read[i] * read[i];
                r.peak[ch] = juce::jmax (r.peak[ch], (double) std::abs (read[i]));
            }
        }
    }

    for (int ch = 0; ch < numOut; ++ch)
        r.rms[ch] = juce::Decibels::gainToDecibels (std::sqrt (sum[(size_t) ch] / (double) kLength));

    r.ran = true;
    return r;
}

//== The golden tables ========================================================
/** One module's measured output in the three layouts that the old code could
    produce. All values absolute: dB for RMS, linear for peak. */
struct Golden
{
    const char* id;
    double monoRms,  monoPeak;          // A: 1 -> 1
    double stereoRmsL, stereoPeakL,     // B: 2 -> 2, decorrelated L/R
           stereoRmsR, stereoPeakR;
    double dupRmsL,  dupPeakL,          // C: 2 -> 2, the same signal on both
           dupRmsR,  dupPeakR;
};

/** Tolerances, **sized for two toolchains rather than one**.

    These were 1e-6 dB and 1e-7 linear, on the reasoning that the DSP is
    untouched and the render path identical, so the numbers should match to the
    last bit. That is true within a toolchain and false across two. The goldens
    were captured on Windows under MSVC; macOS under clang contracts FMAs
    differently, links a different libm and vectorises differently, so the last
    bits are not the same arithmetic.

    macOS CI failed on `eq` and `sat` -- **modules whose DSP this branch never
    touched** -- while Windows passed on the identical goldens. That is what
    identifies the difference as the platform rather than the code.

    The observed divergence is **under 5e-5 dB**: every printed digit of every
    failing row matched, so the delta sits below the sixth significant figure.
    1e-3 dB is twenty times that bound and still a thousand times tighter than
    any real regression -- a module that actually moved would move by tenths of
    a dB, not thousandths.

    This is a genuine widening, not a test bent to fit, and the line it must not
    cross is a tolerance wide enough to hide a change. If a future failure
    reports a delta anywhere near these numbers, that is a regression and not
    float noise. The message prints the delta so that is visible rather than
    inferred. */
constexpr double kRmsTol  = 1.0e-3;   // dB
constexpr double kPeakTol = 1.0e-5;   // linear

void checkGolden (double actual, double expected, double absTol, const juce::String& what)
{
    // **The delta goes in the message.** This printed only the two values at
    // default precision, so the cross-platform failure read
    // "expected -18.1086, got -18.1086" -- identical text, with no way to tell
    // a last-bit difference from a real one without a repro on that platform.
    // A failure message that cannot separate those two is not worth reading.
    const auto delta = std::abs (actual - expected);

    checkClose (actual, expected, juce::jmax (absTol, 1.0e-9 * std::abs (expected)),
                what + " (delta " + juce::String (delta, 9) + ")");
}

//== Parameter settings =======================================================
/** Two settings per module: everything at its default, and everything at 0.63
    of its normalised range -- switches on, choices high, knobs off-centre, so
    the module is actually doing something in both mono and stereo. */
enum class Setting { defaults, swept };

void applySetting (bmo::ParamSet& params, Setting setting)
{
    for (int i = 0; i < params.size(); ++i)
    {
        auto& p = params.param (i);
        p.setValueNotifyingHost (setting == Setting::defaults ? p.getDefaultValue() : 0.63f);
    }
}

std::unique_ptr<bmo::SingleModuleProcessor> makeProduct (const bmo::ModuleDef& def, Setting setting)
{
    // The bus contract is the processor's, not the product's: what goes in
    // ProductInfo here only names presets, and no preset is touched.
    bmo::ProductInfo info { def.name, { def.name, ".bmotest" }, 1, 1 };
    auto proc = std::make_unique<bmo::SingleModuleProcessor> (def, info);
    applySetting (proc->getEngine().params(), setting);
    return proc;
}

/** The rack under test: every registered module in one chain, registry order. */
std::unique_ptr<RackProcessor> makeFullRack (Setting setting)
{
    auto rack = bmo::products::createRack();

    for (const auto* def : bmo::products::registry())
        rack->addModule (*def);

    for (int i = 0; i < (int) bmo::products::registry().size(); ++i)
        if (auto* engine = rack->getEngineAt (i))
            applySetting (engine->params(), setting);

    return rack;
}

//== Printing (regeneration) ==================================================
void printRow (const char* id, const Render& mono, const Render& stereo, const Render& dup)
{
    std::cout << std::setprecision (12)
              << "    { \"" << id << "\",\n"
              << "      " << mono.rms[0] << ", " << mono.peak[0] << ",\n"
              << "      " << stereo.rms[0] << ", " << stereo.peak[0] << ", "
                          << stereo.rms[1] << ", " << stereo.peak[1] << ",\n"
              << "      " << dup.rms[0] << ", " << dup.peak[0] << ", "
                          << dup.rms[1] << ", " << dup.peak[1] << " },\n";
}

void printTable (Setting setting)
{
    std::cout << (setting == Setting::defaults ? "const Golden kDefaults[]\n{\n"
                                               : "const Golden kSwept[]\n{\n");

    for (const auto* def : bmo::products::registry())
    {
        const auto mono   = render (*makeProduct (*def, setting), 1, 1, signalA(), signalB());
        const auto stereo = render (*makeProduct (*def, setting), 2, 2, signalA(), signalB());
        const auto dup    = render (*makeProduct (*def, setting), 2, 2, signalA(), signalA());
        printRow (def->id, mono, stereo, dup);
    }

    {
        const auto mono   = render (*makeFullRack (setting), 1, 1, signalA(), signalB());
        const auto stereo = render (*makeFullRack (setting), 2, 2, signalA(), signalB());
        const auto dup    = render (*makeFullRack (setting), 2, 2, signalA(), signalA());
        printRow ("rack", mono, stereo, dup);
    }

    std::cout << "};\n\n";
}

//== Captured from the build at 8fed835, before either processor was touched ==
const Golden kDefaults[]
{
    { "util",
      -18.0000001899, 0.237879320979,
      -18.000000186, 0.237879306078, -17.9999997416, 0.239933893085,
      -18.0000001899, 0.237879320979, -18.0000001899, 0.237879320979 },
    { "eq",
      -18.1085793005, 0.246073037386,
      -18.1085793005, 0.246073037386, -18.0554981077, 0.247819900513,
      -18.1085793005, 0.246073037386, -18.1085793005, 0.246073037386 },
    { "sat",
      -16.8591902532, 0.417550802231,
      -16.8591902532, 0.417550802231, -16.3599694613, 0.442101210356,
      -16.8591902532, 0.417550802231, -16.8591902532, 0.417550802231 },
    { "opto",
      -18.1622967448, 0.240351647139,
      -18.1782759707, 0.240072011948, -18.0459880442, 0.246936783195,
      -18.1622967448, 0.240351647139, -18.1622967448, 0.240351647139 },
    { "dim",
      -18.0000001899, 0.237879320979,
      -18.0000001857, 0.237879306078, -17.9999997416, 0.239933893085,
      -18.0000001899, 0.237879320979, -18.0000001899, 0.237879320979 },
    { "deq",
      -18.0000001899, 0.237879320979,
      -18.0000001899, 0.237879320979, -17.9999997391, 0.239933893085,
      -18.0000001899, 0.237879320979, -18.0000001899, 0.237879320979 },
    { "ltvcomp",
      -18.0000001899, 0.237879320979,
      -18.0000001899, 0.237879320979, -17.9999997391, 0.239933893085,
      -18.0000001899, 0.237879320979, -18.0000001899, 0.237879320979 },
    // BMO Defang, captured on AURORA on 2026-09-22 when it merged into this
    // branch. Not from 8fed835: the module did not exist then, so its row is
    // the only way the widened layouts can be pinned for it at all.
    { "deesser",
      -18.0078987379, 0.237040400505,
      -18.0000001899, 0.237879320979, -17.9999997391, 0.239933893085,
      -18.0078987379, 0.237040400505, -18.0078987379, 0.237040400505 },
    // BMO FET, captured on AURORA on 2026-09-22 when it merged into this
    // branch. The one row here that is not near -18: BMO FET's placeholder is
    // not a wire -- it compresses at its defaults, so it lands about 2.6 dB
    // down, and it is the only new module so far whose arrival is visible in
    // its own row.
    { "fetcomp",
      -20.6458693267, 0.178372368217,
      -20.7609701573, 0.175070211291, -20.7124848796, 0.176500663161,
      -20.6458693267, 0.178372368217, -20.6458693267, 0.178372368217 },
    // BMO Linger. **A wire, and that is the whole of what this row says
    // today**: its DSP is a marked placeholder, so the numbers are the
    // unaltered input and they are LTV Comp's, BMO DEQ's and the wire's alike.
    // They will move when the engine lands, and the row is here so that the
    // commit which moves them has to say so rather than quietly adding one.
    //
    // **The rack rows below did not move for BMO FET or for BMO Linger, and
    // the reason is the slot count, not the DSP.** `RackProcessor::kSlots` is
    // 8 and `makeFullRack` adds the registry in order, so the chain stops at
    // BMO Defang; the ninth and tenth registered modules never enter it. They
    // did move when Defang joined, because Defang took the eighth slot. An
    // earlier version of this note read the unchanged rack rows as proof that
    // BMO Linger's placeholder passes signal through -- it is not, and no row
    // in this file tests that. Every row above is byte-identical to 8fed835
    // except the three captured since, which is what says nothing else moved.
    { "reverb",
      -18.0000001899, 0.237879320979,
      -18.0000001899, 0.237879320979, -17.9999997391, 0.239933893085,
      -18.0000001899, 0.237879320979, -18.0000001899, 0.237879320979 },
    { "rack",
      -17.4712562736, 0.361956000328,
      -17.5538728123, 0.361777067184, -16.9709734739, 0.381855756044,
      -17.4712562736, 0.361956000328, -17.4712562736, 0.361956000328 },
};

// BMO DEQ's swept row is loud on purpose and is not a fault: 0.63 turns all 24
// bands on and boosts each of them, and 24 stacked boosts is +90 dB. The
// filters are stable -- it is plain gain, not a runaway -- and the number is
// deterministic, which is all a fingerprint has to be.
const Golden kSwept[]
{
    { "util",
      -11.7600009264, 0.487929016352,
      -17.4087591686, 0.290588617325, -14.7933936813, 0.392687320709,
      -14.3753664302, 0.361067473888, -11.7600009264, 0.487929016352 },
    { "eq",
      -15.5938930361, 0.453235358,
      -15.5938930361, 0.453235358, -7.20944735724, 0.902541100979,
      -15.5938930361, 0.453235358, -15.5938930361, 0.453235358 },
    { "sat",
      -7.60426052625, 1.1754732132,
      -8.30132334629, 1.07108569145, -6.68785712242, 1.168405056,
      -7.60426052625, 1.1754732132, -7.60426052625, 1.1754732132 },
    { "opto",
      -18.4816694325, 0.428239285946,
      -19.6395485658, 0.423439323902, -19.6015834201, 0.435511857271,
      -18.4816694325, 0.428239285946, -18.4816694325, 0.428239285946 },
    { "dim",
      -18.0000001899, 0.237879320979,
      -12.0777680029, 0.615875780582, -15.4762951629, 0.465581327677,
      -23.1036245406, 0.199254766107, -12.8562159113, 0.526588916779 },
    { "deq",
      89.7000479803, 67366,
      84.7421622782, 53060.8125, 84.7421621754, 53060.8398438,
      89.7000479803, 67366, 89.7000479803, 67366 },
    { "ltvcomp",
      -7.16919915094, 0.988553106785,
      -10.8123103775, 0.853308975697, -9.8115626611, 0.988553166389,
      -7.16919915094, 0.988553106785, -7.16919915094, 0.988553106785 },
    { "deesser",
      -18.1231178049, 0.234146103263,
      -18.0548834769, 0.236055493355, -18.0545154892, 0.239385798573,
      -18.1231178049, 0.234146103263, -18.1231178049, 0.234146103263 },
    { "fetcomp",
      -14.1968014623, 0.385867774487,
      -14.3174488279, 0.38244971633, -14.2261054655, 0.383085817099,
      -14.1968014623, 0.385867774487, -14.1968014623, 0.385867774487 },
    // Identical to its defaults row, and it should be: at 0.63 of normalised
    // every one of BMO Linger's thirty parameters is somewhere else, and a
    // placeholder does not care. This is the row that will move furthest.
    { "reverb",
      -18.0000001899, 0.237879320979,
      -18.0000001899, 0.237879320979, -17.9999997391, 0.239933893085,
      -18.0000001899, 0.237879320979, -18.0000001899, 0.237879320979 },
    { "rack",
      -12.3127888787, 1.16622579098,
      -12.0186864023, 1.15730452538, -11.9707022298, 1.15666925907,
      -12.5249302605, 1.1564707756, -12.4651240329, 1.15827572346 },
};

const Golden* goldenFor (const Golden* table, size_t n, const char* id)
{
    for (size_t i = 0; i < n; ++i)
        if (std::strcmp (table[i].id, id) == 0)
            return &table[i];

    return nullptr;
}

//== The assertions ===========================================================
void checkFinite (const Render& r, const juce::String& where)
{
    for (int ch = 0; ch < r.channels; ++ch)
        check (std::isfinite (r.rms[ch]) && std::isfinite (r.peak[ch]),
               where + " channel " + juce::String (ch) + " is finite");
}

/** A: mono -> mono, unchanged. Absolute, against the pre-change constant. */
void checkMono (const Render& r, const Golden& g, const juce::String& where)
{
    check (r.ran, where + ": the mono layout is accepted");
    checkFinite (r, where);
    checkGolden (r.rms[0],  g.monoRms,  kRmsTol,  where + " mono RMS dB");
    checkGolden (r.peak[0], g.monoPeak, kPeakTol, where + " mono peak");
}

/** B: stereo -> stereo, unchanged. */
void checkStereo (const Render& r, const Golden& g, const juce::String& where)
{
    check (r.ran, where + ": the stereo layout is accepted");
    checkFinite (r, where);
    checkGolden (r.rms[0],  g.stereoRmsL,  kRmsTol,  where + " stereo RMS dB L");
    checkGolden (r.peak[0], g.stereoPeakL, kPeakTol, where + " stereo peak L");
    checkGolden (r.rms[1],  g.stereoRmsR,  kRmsTol,  where + " stereo RMS dB R");
    checkGolden (r.peak[1], g.stereoPeakR, kPeakTol, where + " stereo peak R");
}

/** C and D both: the duplicated-pair numbers. D (mono -> stereo) is asserted
    against the constants C produced under the old code. */
void checkDuplicated (const Render& r, const Golden& g, const juce::String& where)
{
    check (r.ran, where + ": the layout is accepted");
    checkFinite (r, where);
    checkGolden (r.rms[0],  g.dupRmsL,  kRmsTol,  where + " RMS dB L");
    checkGolden (r.peak[0], g.dupPeakL, kPeakTol, where + " peak L");
    checkGolden (r.rms[1],  g.dupRmsR,  kRmsTol,  where + " RMS dB R");
    checkGolden (r.peak[1], g.dupPeakR, kPeakTol, where + " peak R");
}

/** Every layout a product accepts, written out. A product that gains or loses
    one fails here. */
void checkAcceptedLayouts (juce::AudioProcessor& proc, const juce::String& name)
{
    check (proc.checkBusesLayoutSupported (layoutOf (kMono, kMono)),
           name + " accepts mono -> mono");
    check (proc.checkBusesLayoutSupported (layoutOf (kStereo, kStereo)),
           name + " accepts stereo -> stereo");
    check (proc.checkBusesLayoutSupported (layoutOf (kMono, kStereo)),
           name + " accepts mono -> stereo");

    check (! proc.checkBusesLayoutSupported (layoutOf (kStereo, kMono)),
           name + " refuses stereo -> mono");
    check (! proc.checkBusesLayoutSupported (layoutOf (kStereo, juce::AudioChannelSet::quadraphonic())),
           name + " refuses a quadraphonic output");
    check (! proc.checkBusesLayoutSupported (layoutOf (kMono, juce::AudioChannelSet::disabled())),
           name + " refuses a disabled output");
}

} // namespace

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    if (argc > 1 && std::strcmp (argv[1], "--print") == 0)
    {
        printTable (Setting::defaults);
        printTable (Setting::swept);
        return 0;
    }

    //== The accepted-layout table ============================================
    {
        for (const auto* def : bmo::products::registry())
            checkAcceptedLayouts (*makeProduct (*def, Setting::defaults), def->name);

        checkAcceptedLayouts (*bmo::products::createRack(), "the rack");
    }

    //== Nothing that already worked moved ====================================
    // Cases A, B and C, every module, both settings, against constants the
    // pre-change build printed.
    for (const auto setting : { Setting::defaults, Setting::swept })
    {
        const auto* table = setting == Setting::defaults ? kDefaults : kSwept;
        const auto  count = setting == Setting::defaults ? std::size (kDefaults) : std::size (kSwept);
        const juce::String tag { setting == Setting::defaults ? " (defaults)" : " (swept)" };

        const auto run = [&] (const char* id, auto&& make)
        {
            const auto* g = goldenFor (table, count, id);

            if (g == nullptr)
            {
                check (false, juce::String ("no golden row for ") + id);
                return;
            }

            const juce::String where { juce::String (id) + tag };

            checkMono        (render (*make(), 1, 1, signalA(), signalB()), *g, where);
            checkStereo      (render (*make(), 2, 2, signalA(), signalB()), *g, where);
            checkDuplicated  (render (*make(), 2, 2, signalA(), signalA()), *g,
                              where + " stereo-in duplicate");

            // D: the new layout, pinned to C's pre-change numbers.
            checkDuplicated  (render (*make(), 1, 2, signalA(), signalB()), *g,
                              where + " mono -> stereo");
        };

        for (const auto* def : bmo::products::registry())
            run (def->id, [&] { return makeProduct (*def, setting); });

        run ("rack", [&] { return makeFullRack (setting); });
    }

    //== The new layout, on its own terms =====================================
    // BMO Util at its defaults is a wire, so a mono -> stereo instance of it
    // has one correct output and it is written down here rather than measured:
    // both channels carry the input sample for sample.
    {
        auto proc = makeProduct (*bmo::products::registry().front(), Setting::defaults);
        check (juce::String (bmo::products::registry().front()->id) == "util",
               "the wire test is on BMO Util");

        check (proc->setBusesLayout (layoutOf (kMono, kStereo)),
               "a mono -> stereo layout can be set");
        check (proc->getTotalNumInputChannels() == 1 && proc->getTotalNumOutputChannels() == 2,
               "the processor reports 1 in, 2 out");

        proc->setRateAndBufferSizeDetails (kRate, kBlock);
        proc->prepareToPlay (kRate, kBlock);

        juce::AudioBuffer<float> buffer (2, kBlock);
        juce::MidiBuffer midi;

        buffer.copyFrom (0, 0, signalA().data(), kBlock);
        juce::FloatVectorOperations::fill (buffer.getWritePointer (1), kGarbage, kBlock);

        proc->processBlock (buffer, midi);

        double worstL = 0.0, worstR = 0.0;

        for (int i = 0; i < kBlock; ++i)
        {
            worstL = juce::jmax (worstL, (double) std::abs (buffer.getSample (0, i) - signalA()[(size_t) i]));
            worstR = juce::jmax (worstR, (double) std::abs (buffer.getSample (1, i) - signalA()[(size_t) i]));
        }

        check (worstL <= 1.0e-6, "mono -> stereo: the left output is the input, worst "
                                     + juce::String (worstL));
        check (worstR <= 1.0e-6, "mono -> stereo: the right output is the input too, worst "
                                     + juce::String (worstR));

        // The failure this whole design exists to prevent: a right channel
        // that is silent, or still holding what the host left there.
        check (buffer.getMagnitude (1, 0, kBlock) > 0.01f,
               "mono -> stereo: the right channel is not silent");
        check (buffer.getMagnitude (1, 0, kBlock) < 1.0f,
               "mono -> stereo: the right channel is not the host's leftovers");
    }

    //== An empty rack on the new layout is a wire too =========================
    {
        auto rack = bmo::products::createRack();
        check (rack->setBusesLayout (layoutOf (kMono, kStereo)),
               "the rack accepts a mono -> stereo layout");

        rack->setRateAndBufferSizeDetails (kRate, kBlock);
        rack->prepareToPlay (kRate, kBlock);

        juce::AudioBuffer<float> buffer (2, kBlock);
        juce::MidiBuffer midi;

        juce::FloatVectorOperations::fill (buffer.getWritePointer (0), 0.7f, kBlock);
        juce::FloatVectorOperations::fill (buffer.getWritePointer (1), kGarbage, kBlock);

        rack->processBlock (buffer, midi);

        checkClose (buffer.getSample (0, 100), 0.7, 1.0e-6, "an empty rack passes the left channel");
        checkClose (buffer.getSample (1, 100), 0.7, 1.0e-6, "an empty rack duplicates it into the right");
    }

    return finish ("bus");
}
