/*
    BMO Dimension as a host sees it. The schema is new in 1.0, and from here on
    it is frozen the same way the others are.

    Note the order: `width` is index 0 and `detune` is index 3, which is
    reach-for-first rather than signal order. The DSP runs detune first and the
    panel lays out that way; this list is what a host's automation lane shows
    and what a saved session is keyed by, and it is the only one of the three
    that can never change.
*/

#include "TestUtil.h"
#include "modules/dim/presets/FactoryPresets.h"
#include "products/dim/Product.h"

using namespace test;
namespace P = bmo::dim;

namespace
{
    const Expected kSchema[]
    {
        { P::kWidth,       "Dimension",      0.0f,  200.0f, 100.0f, 0 },
        { P::kShuffle,     "Bloom",          1.0f,    3.0f,   1.0f, 0 },
        { P::kShuffleFreq, "Below",        350.0f, 1400.0f, 700.0f, 0 },
        { P::kDetune,      "Detune",         0.0f,   25.0f,  10.0f, 0 },
        { P::kDetuneOn,    "Generate",       0.0f,    1.0f,   0.0f, 2 },
        { P::kDiffuse,     "Drift",          0.0f,  100.0f,   0.0f, 0 },
        { P::kRate,        "Drift Rate",     0.05f,   5.0f,   0.40f, 0 },
        { P::kDepth,       "Drift Depth",    0.0f,  100.0f,  50.0f, 0 },
        { P::kRotation,    "Turn",         -45.0f,   45.0f,   0.0f, 0 },
        { P::kAsymmetry,   "Tilt",        -100.0f,  100.0f,   0.0f, 0 },
    };
}

int main()
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    using bmo::products::createDim;

    {
        auto proc = createDim();
        checkSchema (*proc, kSchema);
        check (P::specs().size() == (size_t) P::Index::count, "the Index enum matches specs()");
    }

    //== Displayed values ======================================================
    {
        auto proc = createDim();

        setValue (*proc, P::kWidth, 150.0f);
        check (param (*proc, P::kWidth).getCurrentValueAsText() == "150 %",
               "Dimension reads as a percentage");

        setValue (*proc, P::kDiffuse, 40.0f);
        check (param (*proc, P::kDiffuse).getCurrentValueAsText() == "40 %",
               "Drift reads as a percentage");

        // BELOW prints its value on the panel, so it says what unit it is in.
        setValue (*proc, P::kShuffleFreq, 700.0f);
        check (param (*proc, P::kShuffleFreq).getCurrentValueAsText() == "700 Hz",
               "Below reads in hertz");

        setValue (*proc, P::kAsymmetry, -25.0f);
        check (param (*proc, P::kAsymmetry).getCurrentValueAsText() == "-25 %",
               "Tilt reads as a signed percentage");
    }

    //== Latency ==============================================================
    // Zero in every configuration, including with the detune stage running --
    // the mid path is a wire and the detune voices only add to the side signal,
    // so nothing the host gets back is a delayed copy of what it sent. See
    // DimDsp::latencyForParams.
    {
        auto proc = createDim();
        proc->setPlayConfigDetails (2, 2, 48000.0, 512);
        proc->prepareToPlay (48000.0, 512);
        check (proc->getLatencySamples() == 0, "dimension has no latency with detune off");

        setValue (*proc, P::kDetuneOn, 1.0f);
        setValue (*proc, P::kDetune, 25.0f);
        proc->prepareToPlay (48000.0, 512);
        check (proc->getLatencySamples() == 0, "and none with it on at full depth");
    }

    //== State round-trip ======================================================
    {
        juce::MemoryBlock state;

        const bmo::Setting settings[] {
            { P::kWidth,       165.0f },
            { P::kShuffle,       2.2f },
            { P::kShuffleFreq, 520.0f },
            { P::kDetune,       17.5f },
            { P::kDetuneOn,      1.0f },
            { P::kDiffuse,      65.0f },
            { P::kRate,          1.25f },
            { P::kDepth,        80.0f },
            { P::kRotation,    -12.5f },
            { P::kAsymmetry,    35.0f },
        };

        {
            auto proc = createDim();
            for (const auto& s : settings) setValue (*proc, s.id, s.value);
            proc->getStateInformation (state);
        }

        auto restored = createDim();
        restored->setStateInformation (state.getData(), (int) state.getSize());

        for (const auto& s : settings)
            checkClose (getValue (*restored, s.id), s.value, 0.01,
                        juce::String ("state round-trip of '") + s.id + "'");
    }

    //== Factory presets ======================================================
    {
        check (! P::factory().empty(), "there are factory presets");
        check (juce::String (P::factory().front().name) == "Init", "Init is first");
        check (P::factory().front().settings.empty(), "Init is every default");

        // RATE and DEPTH have no controls, so a preset that moves them leaves
        // a value on the instance the panel cannot show or put back.
        for (const auto& preset : P::factory())
            for (const auto& s : preset.settings)
                check (juce::String (s.id) != P::kRate && juce::String (s.id) != P::kDepth,
                       juce::String ("factory preset '") + preset.name
                           + "' does not set " + s.id + ", which has no control");
    }

    //== A preset must not change how loud the track is ======================
    // The rule every other module's test already holds (modules/AGENTS.md,
    // step 6); this file had no such block until the 0.2.4 review, and Wide
    // Vocal measured +3.5 dB peak over Init on a mono vocal with nothing
    // downstream to catch it. Measured as stereo power, because the module
    // manufactures side content from a mono source: the left channel alone
    // would read the width, and the mono sum cannot move by construction, so
    // the sum of both channels' power is the figure that says whether a
    // preset is louder. Print every figure with BMO_PRINT_PRESET_LEVELS.
    {
        auto proc = createDim();
        proc->setPlayConfigDetails (2, 2, 48000.0, 512);
        proc->prepareToPlay (48000.0, 512);

        const auto source = voice (512 * 300);
        const auto sourceDb = rmsDb (source);
        const auto& factory = proc->getPresets().getFactory();
        const bool print = std::getenv ("BMO_PRINT_PRESET_LEVELS") != nullptr;

        const auto stereoPowerDb = [&] (const std::vector<float>& in, int block, int skip)
        {
            juce::AudioBuffer<float> buffer (2, block);
            juce::MidiBuffer midi;
            const auto blocks = (int) in.size() / block;
            double sum = 0.0;
            int counted = 0;

            for (int b = 0; b < blocks; ++b)
            {
                for (int ch = 0; ch < 2; ++ch)
                    buffer.copyFrom (ch, 0, in.data() + (size_t) (b * block), block);

                proc->processBlock (buffer, midi);

                if (b < skip)
                    continue;

                for (int ch = 0; ch < 2; ++ch)
                {
                    const auto* read = buffer.getReadPointer (ch);
                    for (int i = 0; i < block; ++i)
                        sum += (double) read[i] * read[i];
                }

                counted += 2 * block;
            }

            return juce::Decibels::gainToDecibels (std::sqrt (sum / (double) counted));
        };

        for (int index = 1; index < (int) factory.size(); ++index)
        {
            proc->getPresets().loadFactory (index);
            proc->reset();

            const auto outDb = stereoPowerDb (source, 512, 20);

            if (print)
                std::cout << factory[(size_t) index].name << ": " << (outDb - sourceDb) << " dB\n";

            checkClose (outDb - sourceDb, 0.0, 2.5,
                        juce::String ("preset '") + factory[(size_t) index].name
                            + "' comes out near the level it went in, as stereo power");
        }
    }

    return finish ("BMO Dimension");
}
