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

    **Since 2026-09-23 the layout is opt-in per module** (Frosty's decision;
    `ModuleDef::acceptsMonoInput`), and BMO Linger is the only module that
    opts in. So case D runs for BMO Linger and for the rack, whose registry
    holds it; for every other module the layout must be refused, which puts
    each of them back on exactly the layouts it had before the conversion
    existed. Cases A, B and C are untouched by the opt-in, and so are their
    constants.

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

    // **The proportional term, and why it is 1e-5 rather than 1e-9.**
    //
    // Raising the absolute tolerances alone did not fix macOS, because for the
    // rows measured in tens of thousands -- BMO DEQ swept, whose peak is 67366
    // -- `jmax` picks the proportional term and it dominated: 1e-9 x 67366 is
    // 6.7e-05, tighter than the absolute figure it was meant to back up. The
    // measured divergence there is 0.0703 on 67366, a **relative** 1.04e-6, so
    // the proportional term was a thousand times too tight while the absolute
    // one was fine.
    //
    // 1e-5 relative is ten times the observed divergence and still a hundred
    // times tighter than the 1e-3 relative a real regression would have to
    // stay under to hide. The two terms now fail at comparable scales instead
    // of one quietly overriding the other.
    checkClose (actual, expected, juce::jmax (absTol, 1.0e-5 * std::abs (expected)),
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

//== A wire that opts in ======================================================
/** Passes its input through untouched and opts in to mono -> stereo, so the
    widening in SingleModuleProcessor can be checked sample for sample without
    depending on any shipped module either being a wire or opting in. */
struct WireDsp final : bmo::ModuleDsp
{
    void prepare (double, int, int) override {}
    void reset() override {}
    void setParams (const float*, int) override {}
    void process (float* const*, int, int) override {}
    int latencyForParams (const float*, int) const override { return 0; }
};

const bmo::ModuleDef& wireModule()
{
    static const bmo::ParamSpecs specs { bmo::ParamSpec::floatParam ("unused", "Unused", 0.0f, 1.0f, 0.0f, 1.0f) };
    static const std::vector<bmo::FactoryPreset> presets { { "Init", {} } };

    static const bmo::ModuleDef def {
        "wire", "Wire", 1, 160, juce::Colours::grey, specs, presets,
        [] { return std::make_unique<WireDsp>(); },
        {},         // no panel: nothing here opens an editor
        0,          // one width
        nullptr,    // BMO line
        nullptr,    // no ParamLink
        true };     // acceptsMonoInput

    return def;
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
    // BMO Saturator, recaptured on ICE QUEEN on 2026-10-03 with --print when
    // TONE's default went from 100 to 55, the owner's decision before the
    // 0.2.6 schema freeze. Against the row captured at 8fed835 the RMS
    // figures fell by 1.557 dB (mono, both stereo-in duplicate sides, stereo
    // L) and 1.319 dB (stereo R), and the peaks from 0.4176 to 0.2734 (0.4421
    // to 0.3055 on stereo R). Nothing else in the module moved; the rack's
    // defaults row below moved with it, and every other row printed within
    // tolerance.
    { "sat",
      -18.4162417982, 0.273352533579,
      -18.4162417982, 0.273352533579, -17.6791165191, 0.305534929037,
      -18.4162417982, 0.273352533579, -18.4162417982, 0.273352533579 },
    { "opto",
      -18.1622967448, 0.240351647139,
      -18.1782648998, 0.240072011948, -18.0459775451, 0.246936917305,
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
    // BMO Dwell, captured on AURORA on 2026-10-01 when it merged beside BMO
    // Linger. **Bit-identical to the input, and it is not because Dwell is a
    // wire.** The render is kLength = 4096 samples, 85 ms, and Dwell's default
    // TIME is 375 ms, so no repeat lands inside it; what is left is the dry,
    // which `docs/delay/10` requires to be bit-exact unity for MIX 0-50 %, and
    // MIX defaults to 35 %. This row is that requirement holding, not a delay
    // being measured. A first capture, not a before/after: Dwell did not exist
    // at 8fed835.
    { "dwell",
      -18.0000001899, 0.237879320979,
      -18.0000001899, 0.237879320979, -17.9999997391, 0.239933893085,
      -18.0000001899, 0.237879320979, -18.0000001899, 0.237879320979 },
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
      // Since 2026-09-24 the early reflections play, so BMO Linger is no
      // longer a wire. MIX defaults to 50 %, where the dry is at unity and the
      // ER cluster sits on top of it at ER -6 dB; the swept row lands at 63 %
      // MIX, where the dry is coming down. Captured on ICE QUEEN with --print.
      // Recaptured 2026-09-26, also on ICE QUEEN, when VARIATION's default
      // went from 2 to 4 and ER MODE lost Blend on Frosty's listening pass,
      // and again the same day when ER HI-CUT went to 12 dB/octave.
      // Recaptured on ICE QUEEN, 2026-10-02, when the tail landed (M3a): at
      // REVERB -6 dB the late network now plays under the ER, which moved
      // the RMS figures by up to 0.07 dB and the peaks by up to 0.0016 against
      // the M2 row. The swept row did not move, and should not have: its
      // PRE-DELAY is 157 ms, past the end of this suite's 85 ms of signal, so
      // its tail never starts.
      -18.2163922222, 0.241076186299,
      -17.9521665021, 0.248789131641, -18.2492466603, 0.243136674166,
      -17.8128481771, 0.259217143059, -18.6147132938, 0.237479582429 },
    // Recaptured with the BMO Saturator row above, for the same reason: the
    // Saturator is the third slot of this chain. RMS fell by 1.281 dB (mono,
    // stereo-in duplicate, mono -> stereo), 1.152 dB (stereo L) and 0.882 dB
    // (stereo R); peaks from 0.3620 to 0.2635, 0.3618 to 0.2670 and 0.3819
    // to 0.2934. Recaptured again on ICE QUEEN, 2026-10-03, when this work
    // was brought onto a main that already carried BMO Opto's new cell: the
    // Saturator's TONE is still the only cause (the row is unchanged through
    // every EQ commit), and the figures against main's row are -1.2808 dB
    // (mono, duplicate, mono -> stereo), -1.1536 (L) and -0.8833 (R).
    { "rack",
      -18.752053514, 0.263523042202,
      -18.7062172701, 0.267028808594, -17.8530324334, 0.293390482664,
      -18.752053514, 0.263523042202, -18.752053514, 0.263523042202 },
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
    // Regenerated on ICE QUEEN, 2026-10-03, for this row and the rack row
    // alone. 0.63 turns Auto Gain on, and its compensation for this setting is
    // +2.28 dB (the cuts at 160 Hz and 10 kHz take more of the band than the
    // boosts add). Auto Gain used to start from unity after prepare() and
    // glide there over about 60 ms of this 85 ms render; it now starts at its
    // figure (bdd8c37), so the render is louder: mono and L RMS -15.594 ->
    // -15.122 dB (+0.472), peak 0.45324 -> 0.46397; stereo R RMS -7.209 ->
    // -6.818 dB (+0.392), peak 0.90254 -> 0.94871. No other commit on the
    // branch moved this row, and the defaults rows are byte-identical.
    { "eq",
      -15.1218135793, 0.463965445757,
      -15.1218135793, 0.463965445757, -6.81764535567, 0.948705196381,
      -15.1218135793, 0.463965445757, -15.1218135793, 0.463965445757 },
    { "sat",
      -7.60426052625, 1.1754732132,
      -8.30132334629, 1.07108569145, -6.68785712242, 1.168405056,
      -7.60426052625, 1.1754732132, -7.60426052625, 1.1754732132 },
    // BMO Opto's row and the rack's were regenerated on 2026-10-03, when the
    // cells changed on purpose: the charge counts only a level the signal has
    // kept up, which let 0.15 dB more through on this noisy stimulus, and
    // Stressed (where 0.63 puts Mode) gained a quick attack stage that rides
    // the noise's crests and takes 1.77 dB off. Net 1.61 dB lower, peak 0.428
    // to 0.337. testing-notes/opto-spike-and-dip-2026-10-03.md has the work.
    { "opto",
      -20.0948769542, 0.336609631777,
      -21.2901275396, 0.277803987265, -21.1450327082, 0.417769670486,
      -20.0948769542, 0.336609631777, -20.0948769542, 0.336609631777 },
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
    // BMO Dwell at 0.63 of normalised. 0.745 dB under its defaults row, and
    // that is MIX at 63 % fading the dry -- the dry only starts to fall above
    // 50 % -- rather than any repeat: TIME at 0.63 of its log travel is still
    // far longer than the 85 ms render. A delay's repeats are not something
    // this file can see; DwellDspTests is where they are measured.
    { "dwell",
      -18.7454683134, 0.218314856291,
      -18.7454683134, 0.218314856291, -18.7454678563, 0.220200449228,
      -18.7454683134, 0.218314856291, -18.7454683134, 0.218314856291 },
    // Identical to its defaults row, and it should be: at 0.63 of normalised
    // every one of BMO Linger's thirty parameters is somewhere else, and a
    // placeholder does not care. This is the row that will move furthest.
    //
    // Regenerated on ICE QUEEN, 2026-10-02, for this row alone, twice. The
    // diffuser's normaliser now follows DENSITY in 0.25 % steps rather than
    // every block, and each update ramps over up to 2 ms rather than landing
    // at once. At 0.63 DENSITY sits just past the first stage, so the glide
    // from an instance's starting 50 % is where both show. Against the row
    // `main` carried, the peaks moved by up to 6.1e-5 on 0.069 (0.008 dB) and
    // the RMS figures by up to 0.0014 dB. Every other row printed within
    // tolerance.
    { "reverb",
      -29.3107797472, 0.0665621832013,
      -29.3036605172, 0.065762847662, -29.6342359134, 0.064994379878,
      -29.0903900723, 0.0687288194895, -29.5277643484, 0.0643955394626 },
    // Regenerated with the eq row above, for the same reason: BMO EQ is a slot
    // in this chain with Auto Gain on, so its first 60 ms now carry the +2.28
    // dB from the start, and the compressing slots after it turn that into
    // these moves: RMS -0.028 to -0.041 dB, peaks +0.0023 to +0.0183 (and
    // -0.0034 / -0.0027 on the duplicated pair). Before, from 6f6b8c3 on ICE
    // QUEEN:
    //   -12.3127826311, 1.16622364521,
    //   -12.0186861044, 1.15730381012, -11.9707005038, 1.15666902065,
    //   -12.5248895603, 1.1564694643, -12.4650833707, 1.15827429295
    // Recaptured on ICE QUEEN, 2026-10-03, when this work was brought onto a
    // main whose BMO Opto (slot four) had gained its quick attack stage. The
    // same Auto Gain commit is the only one that moves it, but the new cell
    // answers the louder start more strongly: against main's row, RMS -0.040
    // (mono), -0.077 / -0.074 (stereo L / R), -0.044 / -0.041 (duplicate and
    // mono -> stereo L / R) dB, and every peak up by 0.0028 to 0.0075.
    { "rack",
      -11.7195631752, 1.16198933125,
      -11.317069677, 1.15757536888, -11.2731589041, 1.15886342525,
      -11.854487306, 1.15360951424, -11.8121786505, 1.15331184864 },
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
    one fails here.

    Mono -> stereo is **opt-in per module** (`ModuleDef::acceptsMonoInput`,
    Frosty's decision on 2026-09-23), so it is the one row that differs by
    product: a module that did not opt in must refuse it, which is the layout
    set it had before the conversion existed. */
void checkAcceptedLayouts (juce::AudioProcessor& proc, const juce::String& name, bool monoToStereo)
{
    check (proc.checkBusesLayoutSupported (layoutOf (kMono, kMono)),
           name + " accepts mono -> mono");
    check (proc.checkBusesLayoutSupported (layoutOf (kStereo, kStereo)),
           name + " accepts stereo -> stereo");
    check (proc.checkBusesLayoutSupported (layoutOf (kMono, kStereo)) == monoToStereo,
           name + (monoToStereo ? " accepts mono -> stereo" : " refuses mono -> stereo -- it has not opted in"));

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
        bool anyAcceptsMono = false;

        for (const auto* def : bmo::products::registry())
        {
            // Written out rather than read off the def, which is the thing
            // under test: BMO Linger opts in and nothing else does.
            const auto optsIn = juce::String (def->id) == "reverb";

            check (def->acceptsMonoInput == optsIn,
                   juce::String (def->name) + (optsIn ? " opts in to mono -> stereo"
                                                      : " does not opt in to mono -> stereo"));

            checkAcceptedLayouts (*makeProduct (*def, Setting::defaults), def->name, optsIn);
            anyAcceptsMono = anyAcceptsMono || optsIn;
        }

        // The rack's layout is fixed before it holds a chain, so it offers
        // what any module it can host could use (RackProcessor.cpp).
        check (anyAcceptsMono, "at least one registered module opts in, so the rack offers mono -> stereo");
        checkAcceptedLayouts (*bmo::products::createRack(), "the rack", anyAcceptsMono);
    }

    //== Nothing that already worked moved ====================================
    // Cases A, B and C, every module, both settings, against constants the
    // pre-change build printed.
    for (const auto setting : { Setting::defaults, Setting::swept })
    {
        const auto* table = setting == Setting::defaults ? kDefaults : kSwept;
        const auto  count = setting == Setting::defaults ? std::size (kDefaults) : std::size (kSwept);
        const juce::String tag { setting == Setting::defaults ? " (defaults)" : " (swept)" };

        const auto run = [&] (const char* id, bool monoToStereo, auto&& make)
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

            // D: the new layout, pinned to C's pre-change numbers -- where it
            // is offered. A module that has not opted in must refuse it, so
            // the render never runs.
            if (monoToStereo)
                checkDuplicated (render (*make(), 1, 2, signalA(), signalB()), *g,
                                 where + " mono -> stereo");
            else
                check (! render (*make(), 1, 2, signalA(), signalB()).ran,
                       where + ": mono -> stereo is refused, so nothing renders");
        };

        for (const auto* def : bmo::products::registry())
            run (def->id, def->acceptsMonoInput, [&] { return makeProduct (*def, setting); });

        // The rack offers the layout whatever its chain holds -- its registry
        // has a module that opts in -- so D runs on it. (The full rack stops
        // at eight slots, before BMO Linger; the rack's answer does not care.)
        run ("rack", true, [&] { return makeFullRack (setting); });
    }

    //== The new layout, on its own terms =====================================
    // A wire that opts in has one correct output on a mono -> stereo layout,
    // and it is written down here rather than measured: both channels carry
    // the input sample for sample. This was BMO Util at its defaults until the
    // layout became opt-in; Util does not opt in, so the wire is a module of
    // this file's own that does, and the thing under test is still only
    // SingleModuleProcessor's widening.
    {
        auto proc = makeProduct (wireModule(), Setting::defaults);

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
