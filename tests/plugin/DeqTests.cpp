/*
    BMO DEQ as a host sees it: 159 parameters, frozen from the first release
    the same way every other module's are. (158 of Frosty's allocation, and
    AUTO appended after them the same day.)

    The order is Frosty's allocation of 2026-09-11 and is the thing to protect.
    A rack slot has 32 host lanes, so what sits in the first 32 is what can be
    automated in a rack: output, bands 1-6 by frequency, gain, Q, threshold and
    range, and the module's in/out. Everything after that is held off the grid
    (core/rack/SlotOverflow.h) and is a host parameter only standalone.

    The table below is written out whole -- generated once from that written
    allocation, not from params.h, so it can disagree with params.h. A test
    that read its expectations back out of the code under test could only
    ever agree with it.
*/

#include "TestUtil.h"
#include "modules/deq/dsp/DeqDsp.h"
#include "modules/deq/presets/FactoryPresets.h"
#include "products/deq/Product.h"

using namespace test;
namespace P = bmo::deq;

namespace
{
    const Expected kSchema[]
    {
        // 0: output
        { "out", "Output", -24.0f, 24.0f, 0.0f, 0 },
        // 1..30: bands 1-6 by frequency, gain, Q, threshold, range -- the rack's lanes
        { "b1_freq", "Band 1 Freq", 20.0f, 20000.0f, 30.0f, 0 },
        { "b1_gain", "Band 1 Gain", -24.0f, 24.0f, 0.0f, 0 },
        { "b1_q", "Band 1 Q", 0.1f, 40.0f, 0.71f, 0 },
        { "b1_thr", "Band 1 Thresh", -60.0f, 0.0f, -24.0f, 0 },
        { "b1_range", "Band 1 Range", -24.0f, 24.0f, -6.0f, 0 },
        { "b2_freq", "Band 2 Freq", 20.0f, 20000.0f, 80.0f, 0 },
        { "b2_gain", "Band 2 Gain", -24.0f, 24.0f, 0.0f, 0 },
        { "b2_q", "Band 2 Q", 0.1f, 40.0f, 0.71f, 0 },
        { "b2_thr", "Band 2 Thresh", -60.0f, 0.0f, -24.0f, 0 },
        { "b2_range", "Band 2 Range", -24.0f, 24.0f, -6.0f, 0 },
        { "b3_freq", "Band 3 Freq", 20.0f, 20000.0f, 160.0f, 0 },
        { "b3_gain", "Band 3 Gain", -24.0f, 24.0f, 0.0f, 0 },
        { "b3_q", "Band 3 Q", 0.1f, 40.0f, 0.71f, 0 },
        { "b3_thr", "Band 3 Thresh", -60.0f, 0.0f, -24.0f, 0 },
        { "b3_range", "Band 3 Range", -24.0f, 24.0f, -6.0f, 0 },
        { "b4_freq", "Band 4 Freq", 20.0f, 20000.0f, 300.0f, 0 },
        { "b4_gain", "Band 4 Gain", -24.0f, 24.0f, 0.0f, 0 },
        { "b4_q", "Band 4 Q", 0.1f, 40.0f, 0.71f, 0 },
        { "b4_thr", "Band 4 Thresh", -60.0f, 0.0f, -24.0f, 0 },
        { "b4_range", "Band 4 Range", -24.0f, 24.0f, -6.0f, 0 },
        { "b5_freq", "Band 5 Freq", 20.0f, 20000.0f, 500.0f, 0 },
        { "b5_gain", "Band 5 Gain", -24.0f, 24.0f, 0.0f, 0 },
        { "b5_q", "Band 5 Q", 0.1f, 40.0f, 0.71f, 0 },
        { "b5_thr", "Band 5 Thresh", -60.0f, 0.0f, -24.0f, 0 },
        { "b5_range", "Band 5 Range", -24.0f, 24.0f, -6.0f, 0 },
        { "b6_freq", "Band 6 Freq", 20.0f, 20000.0f, 800.0f, 0 },
        { "b6_gain", "Band 6 Gain", -24.0f, 24.0f, 0.0f, 0 },
        { "b6_q", "Band 6 Q", 0.1f, 40.0f, 0.71f, 0 },
        { "b6_thr", "Band 6 Thresh", -60.0f, 0.0f, -24.0f, 0 },
        { "b6_range", "Band 6 Range", -24.0f, 24.0f, -6.0f, 0 },
        // 31: the module's in/out, the 32nd and last lane
        { "active", "DEQ", 0.0f, 1.0f, 1.0f, 2 },
        // 32..79: the rest of bands 1-6, off the rack's grid
        { "b1_on", "Band 1 On", 0.0f, 1.0f, 0.0f, 2 },
        { "b1_shape", "Band 1 Shape", 0.0f, 4.0f, 3.0f, 5 },
        { "b1_place", "Band 1 Place", 0.0f, 2.0f, 0.0f, 3 },
        { "b1_dyn", "Band 1 Dyn", 0.0f, 1.0f, 0.0f, 2 },
        { "b1_dir", "Band 1 Dir", 0.0f, 1.0f, 0.0f, 2 },
        { "b1_ratio", "Band 1 Ratio", 1.0f, 20.0f, 2.0f, 0 },
        { "b1_atk", "Band 1 Attack", 0.1f, 200.0f, 5.0f, 0 },
        { "b1_rel", "Band 1 Release", 5.0f, 2000.0f, 120.0f, 0 },
        { "b2_on", "Band 2 On", 0.0f, 1.0f, 0.0f, 2 },
        { "b2_shape", "Band 2 Shape", 0.0f, 4.0f, 1.0f, 5 },
        { "b2_place", "Band 2 Place", 0.0f, 2.0f, 0.0f, 3 },
        { "b2_dyn", "Band 2 Dyn", 0.0f, 1.0f, 0.0f, 2 },
        { "b2_dir", "Band 2 Dir", 0.0f, 1.0f, 0.0f, 2 },
        { "b2_ratio", "Band 2 Ratio", 1.0f, 20.0f, 2.0f, 0 },
        { "b2_atk", "Band 2 Attack", 0.1f, 200.0f, 5.0f, 0 },
        { "b2_rel", "Band 2 Release", 5.0f, 2000.0f, 120.0f, 0 },
        { "b3_on", "Band 3 On", 0.0f, 1.0f, 0.0f, 2 },
        { "b3_shape", "Band 3 Shape", 0.0f, 4.0f, 0.0f, 5 },
        { "b3_place", "Band 3 Place", 0.0f, 2.0f, 0.0f, 3 },
        { "b3_dyn", "Band 3 Dyn", 0.0f, 1.0f, 0.0f, 2 },
        { "b3_dir", "Band 3 Dir", 0.0f, 1.0f, 0.0f, 2 },
        { "b3_ratio", "Band 3 Ratio", 1.0f, 20.0f, 2.0f, 0 },
        { "b3_atk", "Band 3 Attack", 0.1f, 200.0f, 5.0f, 0 },
        { "b3_rel", "Band 3 Release", 5.0f, 2000.0f, 120.0f, 0 },
        { "b4_on", "Band 4 On", 0.0f, 1.0f, 0.0f, 2 },
        { "b4_shape", "Band 4 Shape", 0.0f, 4.0f, 0.0f, 5 },
        { "b4_place", "Band 4 Place", 0.0f, 2.0f, 0.0f, 3 },
        { "b4_dyn", "Band 4 Dyn", 0.0f, 1.0f, 0.0f, 2 },
        { "b4_dir", "Band 4 Dir", 0.0f, 1.0f, 0.0f, 2 },
        { "b4_ratio", "Band 4 Ratio", 1.0f, 20.0f, 2.0f, 0 },
        { "b4_atk", "Band 4 Attack", 0.1f, 200.0f, 5.0f, 0 },
        { "b4_rel", "Band 4 Release", 5.0f, 2000.0f, 120.0f, 0 },
        { "b5_on", "Band 5 On", 0.0f, 1.0f, 0.0f, 2 },
        { "b5_shape", "Band 5 Shape", 0.0f, 4.0f, 0.0f, 5 },
        { "b5_place", "Band 5 Place", 0.0f, 2.0f, 0.0f, 3 },
        { "b5_dyn", "Band 5 Dyn", 0.0f, 1.0f, 0.0f, 2 },
        { "b5_dir", "Band 5 Dir", 0.0f, 1.0f, 0.0f, 2 },
        { "b5_ratio", "Band 5 Ratio", 1.0f, 20.0f, 2.0f, 0 },
        { "b5_atk", "Band 5 Attack", 0.1f, 200.0f, 5.0f, 0 },
        { "b5_rel", "Band 5 Release", 5.0f, 2000.0f, 120.0f, 0 },
        { "b6_on", "Band 6 On", 0.0f, 1.0f, 0.0f, 2 },
        { "b6_shape", "Band 6 Shape", 0.0f, 4.0f, 0.0f, 5 },
        { "b6_place", "Band 6 Place", 0.0f, 2.0f, 0.0f, 3 },
        { "b6_dyn", "Band 6 Dyn", 0.0f, 1.0f, 0.0f, 2 },
        { "b6_dir", "Band 6 Dir", 0.0f, 1.0f, 0.0f, 2 },
        { "b6_ratio", "Band 6 Ratio", 1.0f, 20.0f, 2.0f, 0 },
        { "b6_atk", "Band 6 Attack", 0.1f, 200.0f, 5.0f, 0 },
        { "b6_rel", "Band 6 Release", 5.0f, 2000.0f, 120.0f, 0 },
        // 80..157: bands 7-12, whole
        { "b7_on", "Band 7 On", 0.0f, 1.0f, 0.0f, 2 },
        { "b7_shape", "Band 7 Shape", 0.0f, 4.0f, 0.0f, 5 },
        { "b7_freq", "Band 7 Freq", 20.0f, 20000.0f, 1300.0f, 0 },
        { "b7_gain", "Band 7 Gain", -24.0f, 24.0f, 0.0f, 0 },
        { "b7_q", "Band 7 Q", 0.1f, 40.0f, 0.71f, 0 },
        { "b7_place", "Band 7 Place", 0.0f, 2.0f, 0.0f, 3 },
        { "b7_dyn", "Band 7 Dyn", 0.0f, 1.0f, 0.0f, 2 },
        { "b7_dir", "Band 7 Dir", 0.0f, 1.0f, 0.0f, 2 },
        { "b7_thr", "Band 7 Thresh", -60.0f, 0.0f, -24.0f, 0 },
        { "b7_range", "Band 7 Range", -24.0f, 24.0f, -6.0f, 0 },
        { "b7_ratio", "Band 7 Ratio", 1.0f, 20.0f, 2.0f, 0 },
        { "b7_atk", "Band 7 Attack", 0.1f, 200.0f, 5.0f, 0 },
        { "b7_rel", "Band 7 Release", 5.0f, 2000.0f, 120.0f, 0 },
        { "b8_on", "Band 8 On", 0.0f, 1.0f, 0.0f, 2 },
        { "b8_shape", "Band 8 Shape", 0.0f, 4.0f, 0.0f, 5 },
        { "b8_freq", "Band 8 Freq", 20.0f, 20000.0f, 2000.0f, 0 },
        { "b8_gain", "Band 8 Gain", -24.0f, 24.0f, 0.0f, 0 },
        { "b8_q", "Band 8 Q", 0.1f, 40.0f, 0.71f, 0 },
        { "b8_place", "Band 8 Place", 0.0f, 2.0f, 0.0f, 3 },
        { "b8_dyn", "Band 8 Dyn", 0.0f, 1.0f, 0.0f, 2 },
        { "b8_dir", "Band 8 Dir", 0.0f, 1.0f, 0.0f, 2 },
        { "b8_thr", "Band 8 Thresh", -60.0f, 0.0f, -24.0f, 0 },
        { "b8_range", "Band 8 Range", -24.0f, 24.0f, -6.0f, 0 },
        { "b8_ratio", "Band 8 Ratio", 1.0f, 20.0f, 2.0f, 0 },
        { "b8_atk", "Band 8 Attack", 0.1f, 200.0f, 5.0f, 0 },
        { "b8_rel", "Band 8 Release", 5.0f, 2000.0f, 120.0f, 0 },
        { "b9_on", "Band 9 On", 0.0f, 1.0f, 0.0f, 2 },
        { "b9_shape", "Band 9 Shape", 0.0f, 4.0f, 0.0f, 5 },
        { "b9_freq", "Band 9 Freq", 20.0f, 20000.0f, 3200.0f, 0 },
        { "b9_gain", "Band 9 Gain", -24.0f, 24.0f, 0.0f, 0 },
        { "b9_q", "Band 9 Q", 0.1f, 40.0f, 0.71f, 0 },
        { "b9_place", "Band 9 Place", 0.0f, 2.0f, 0.0f, 3 },
        { "b9_dyn", "Band 9 Dyn", 0.0f, 1.0f, 0.0f, 2 },
        { "b9_dir", "Band 9 Dir", 0.0f, 1.0f, 0.0f, 2 },
        { "b9_thr", "Band 9 Thresh", -60.0f, 0.0f, -24.0f, 0 },
        { "b9_range", "Band 9 Range", -24.0f, 24.0f, -6.0f, 0 },
        { "b9_ratio", "Band 9 Ratio", 1.0f, 20.0f, 2.0f, 0 },
        { "b9_atk", "Band 9 Attack", 0.1f, 200.0f, 5.0f, 0 },
        { "b9_rel", "Band 9 Release", 5.0f, 2000.0f, 120.0f, 0 },
        { "b10_on", "Band 10 On", 0.0f, 1.0f, 0.0f, 2 },
        { "b10_shape", "Band 10 Shape", 0.0f, 4.0f, 0.0f, 5 },
        { "b10_freq", "Band 10 Freq", 20.0f, 20000.0f, 5000.0f, 0 },
        { "b10_gain", "Band 10 Gain", -24.0f, 24.0f, 0.0f, 0 },
        { "b10_q", "Band 10 Q", 0.1f, 40.0f, 0.71f, 0 },
        { "b10_place", "Band 10 Place", 0.0f, 2.0f, 0.0f, 3 },
        { "b10_dyn", "Band 10 Dyn", 0.0f, 1.0f, 0.0f, 2 },
        { "b10_dir", "Band 10 Dir", 0.0f, 1.0f, 0.0f, 2 },
        { "b10_thr", "Band 10 Thresh", -60.0f, 0.0f, -24.0f, 0 },
        { "b10_range", "Band 10 Range", -24.0f, 24.0f, -6.0f, 0 },
        { "b10_ratio", "Band 10 Ratio", 1.0f, 20.0f, 2.0f, 0 },
        { "b10_atk", "Band 10 Attack", 0.1f, 200.0f, 5.0f, 0 },
        { "b10_rel", "Band 10 Release", 5.0f, 2000.0f, 120.0f, 0 },
        { "b11_on", "Band 11 On", 0.0f, 1.0f, 0.0f, 2 },
        { "b11_shape", "Band 11 Shape", 0.0f, 4.0f, 2.0f, 5 },
        { "b11_freq", "Band 11 Freq", 20.0f, 20000.0f, 10000.0f, 0 },
        { "b11_gain", "Band 11 Gain", -24.0f, 24.0f, 0.0f, 0 },
        { "b11_q", "Band 11 Q", 0.1f, 40.0f, 0.71f, 0 },
        { "b11_place", "Band 11 Place", 0.0f, 2.0f, 0.0f, 3 },
        { "b11_dyn", "Band 11 Dyn", 0.0f, 1.0f, 0.0f, 2 },
        { "b11_dir", "Band 11 Dir", 0.0f, 1.0f, 0.0f, 2 },
        { "b11_thr", "Band 11 Thresh", -60.0f, 0.0f, -24.0f, 0 },
        { "b11_range", "Band 11 Range", -24.0f, 24.0f, -6.0f, 0 },
        { "b11_ratio", "Band 11 Ratio", 1.0f, 20.0f, 2.0f, 0 },
        { "b11_atk", "Band 11 Attack", 0.1f, 200.0f, 5.0f, 0 },
        { "b11_rel", "Band 11 Release", 5.0f, 2000.0f, 120.0f, 0 },
        { "b12_on", "Band 12 On", 0.0f, 1.0f, 0.0f, 2 },
        { "b12_shape", "Band 12 Shape", 0.0f, 4.0f, 4.0f, 5 },
        { "b12_freq", "Band 12 Freq", 20.0f, 20000.0f, 18000.0f, 0 },
        { "b12_gain", "Band 12 Gain", -24.0f, 24.0f, 0.0f, 0 },
        { "b12_q", "Band 12 Q", 0.1f, 40.0f, 0.71f, 0 },
        { "b12_place", "Band 12 Place", 0.0f, 2.0f, 0.0f, 3 },
        { "b12_dyn", "Band 12 Dyn", 0.0f, 1.0f, 0.0f, 2 },
        { "b12_dir", "Band 12 Dir", 0.0f, 1.0f, 0.0f, 2 },
        { "b12_thr", "Band 12 Thresh", -60.0f, 0.0f, -24.0f, 0 },
        { "b12_range", "Band 12 Range", -24.0f, 24.0f, -6.0f, 0 },
        { "b12_ratio", "Band 12 Ratio", 1.0f, 20.0f, 2.0f, 0 },
        { "b12_atk", "Band 12 Attack", 0.1f, 200.0f, 5.0f, 0 },
        { "b12_rel", "Band 12 Release", 5.0f, 2000.0f, 120.0f, 0 },
        // 158: AUTO, appended 2026-09-11 (before any release)
        { "auto_gain", "Auto Gain", 0.0f, 1.0f, 0.0f, 2 },
    };

    std::vector<float> sine (double hz, double amplitude, int samples)
    {
        std::vector<float> out ((size_t) samples);
        for (int i = 0; i < samples; ++i)
            out[(size_t) i] = (float) (amplitude * std::sin (2.0 * juce::MathConstants<double>::pi * hz * i / 48000.0));
        return out;
    }

    /** Gain at `hz`, in dB: a sine through the processor against the same sine
        straight, both measured after a quarter second of settling. */
    double gainAt (bmo::SingleModuleProcessor& proc, double hz, double amplitude = 0.1)
    {
        const auto source = sine (hz, amplitude, 48000);
        return outputDb (proc, source, 512, 24) - rmsDb (source, 24 * 512);
    }

    std::unique_ptr<bmo::SingleModuleProcessor> prepared()
    {
        auto proc = bmo::products::createDeq();
        proc->setPlayConfigDetails (2, 2, 48000.0, 512);
        proc->prepareToPlay (48000.0, 512);
        return proc;
    }
}

int main()
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    using bmo::products::createDeq;

    //== The schema =============================================================
    {
        auto proc = createDeq();
        checkSchema (*proc, kSchema);
        check (P::specs().size() == (size_t) P::kCount, "specs() has kCount entries");

        // Every slot in the list is one band's one control, or output, DEQ or AUTO.
        std::vector<int> seen ((size_t) P::kCount, 0);
        ++seen[(size_t) P::kOutput];
        ++seen[(size_t) P::kActive];
        ++seen[(size_t) P::kAutoGain];
        for (int b = 0; b < P::kBands; ++b)
            for (int c = 0; c < P::kPerBand; ++c)
            {
                const auto i = P::indexOf (b, (P::Control) c);
                if (i >= 0 && i < P::kCount) ++seen[(size_t) i];
            }
        check (std::all_of (seen.begin(), seen.end(), [] (int n) { return n == 1; }),
               "indexOf puts every control of every band in exactly one place");
    }

    //== Displayed values =======================================================
    {
        auto proc = createDeq();

        setValue (*proc, "b3_freq", 2100.0f);
        check (param (*proc, "b3_freq").getCurrentValueAsText() == "2.10 kHz", "frequency reads in kHz");
        checkClose (param (*proc, "b3_freq").convertFrom0to1 (param (*proc, "b3_freq").getValueForText ("850 Hz")), 850.0, 0.5,
                    "and a typed \"850 Hz\" is 850 Hz");
        checkClose (param (*proc, "b3_freq").convertFrom0to1 (param (*proc, "b3_freq").getValueForText ("4.5k")), 4500.0, 1.0,
                    "and a typed \"4.5k\" is 4500 Hz");

        setValue (*proc, "b3_ratio", 3.0f);
        check (param (*proc, "b3_ratio").getCurrentValueAsText() == "3.0:1", "ratio reads as one");
        setValue (*proc, "b3_atk", 5.0f);
        check (param (*proc, "b3_atk").getCurrentValueAsText() == "5.0 ms", "attack reads in ms");
        setValue (*proc, "b3_shape", 1.0f);
        check (param (*proc, "b3_shape").getCurrentValueAsText() == "Low Shelf", "shape reads as its name");
        setValue (*proc, "b3_gain", -2.0f);
        check (param (*proc, "b3_gain").getCurrentValueAsText() == "-2.0 dB", "gain reads in dB");

        // The frequency knob is logarithmic: halfway round is the geometric
        // mean of 20 Hz and 20 kHz, not 10 kHz.
        checkClose (param (*proc, "b1_freq").convertFrom0to1 (0.5f), std::sqrt (20.0 * 20000.0), 0.5, "frequency travel is logarithmic");
    }

    //== Latency ================================================================
    // Zero, always -- the product claim. With every band on and dynamic.
    {
        auto proc = prepared();
        for (int b = 1; b <= P::kBands; ++b)
        {
            setValue (*proc, ("b" + juce::String (b) + "_on").toRawUTF8(), 1.0f);
            setValue (*proc, ("b" + juce::String (b) + "_dyn").toRawUTF8(), 1.0f);
            setValue (*proc, ("b" + juce::String (b) + "_gain").toRawUTF8(), 6.0f);
        }
        proc->prepareToPlay (48000.0, 512);
        check (proc->getLatencySamples() == 0, "deq reports no latency with every band on and dynamic");
    }

    //== What it does ===========================================================
    {
        auto proc = prepared();
        checkClose (gainAt (*proc, 1000.0), 0.0, 1.0e-4, "a fresh instance is a wire");

        setValue (*proc, "b3_on", 1.0f);
        setValue (*proc, "b3_freq", 1000.0f);
        setValue (*proc, "b3_gain", 6.0f);
        setValue (*proc, "b3_q", 1.0f);
        checkClose (gainAt (*proc, 1000.0), 6.0, 0.05, "a +6 dB bell at 1 kHz is +6 dB there");
        checkClose (gainAt (*proc, 100.0), 0.0, 0.3, "and leaves 100 Hz nearly alone");

        setValue (*proc, "out", -6.0f);
        checkClose (gainAt (*proc, 1000.0), 0.0, 0.05, "output trims after the bands");
        setValue (*proc, "out", 0.0f);

        setValue (*proc, "active", 0.0f);
        checkClose (gainAt (*proc, 1000.0), 0.0, 1.0e-3, "DEQ off is a wire again");
        setValue (*proc, "active", 1.0f);

        // Serial: two bands at the same frequency add in dB (spec/decisions.md).
        setValue (*proc, "b4_on", 1.0f);
        setValue (*proc, "b4_freq", 1000.0f);
        setValue (*proc, "b4_gain", 6.0f);
        setValue (*proc, "b4_q", 1.0f);
        checkClose (gainAt (*proc, 1000.0), 12.0, 0.1, "two +6 dB bells at one frequency make +12 dB");
    }

    //== Dynamics ===============================================================
    {
        auto proc = prepared();
        setValue (*proc, "b6_on", 1.0f);
        setValue (*proc, "b6_freq", 2000.0f);
        setValue (*proc, "b6_dyn", 1.0f);
        setValue (*proc, "b6_thr", -40.0f);
        setValue (*proc, "b6_range", -9.0f);
        setValue (*proc, "b6_ratio", 20.0f);

        checkClose (gainAt (*proc, 2000.0, 0.5), -9.0, 0.2, "a loud tone at a dynamic band is cut by its range");
        check (proc->getEngine().gainReduction().get() > 8.0f, "and the meter sees it");

        setValue (*proc, "b6_dir", 1.0f);   // below
        checkClose (gainAt (*proc, 2000.0, 0.5), 0.0, 0.2, "set to act below threshold, a loud tone is left alone");
    }

    //== Shelf Q ================================================================
    // A shelf runs at no more than kShelfMaxQ whatever its knob says; a bell
    // keeps the whole range. Measured at the shelf's resonant bump, where Q
    // shows most, against the same shelf asked for exactly the cap.
    {
        check (P::kShelfMaxQ == 2.0f, "a shelf's Q stops at 2");

        auto shelfAt = [] (float q, int shape)
        {
            auto proc = prepared();
            setValue (*proc, "b3_on", 1.0f);
            setValue (*proc, "b3_shape", (float) shape);
            setValue (*proc, "b3_freq", 1000.0f);
            setValue (*proc, "b3_gain", 9.0f);
            setValue (*proc, "b3_q", q);
            return std::pair<double, double> { gainAt (*proc, 700.0), gainAt (*proc, 1400.0) };
        };

        for (const auto shape : { 1, 2 })
        {
            const auto asked = shelfAt (8.0f, shape), capped = shelfAt (2.0f, shape);
            const auto name = juce::String (shape == 1 ? "low" : "high");
            checkClose (asked.first,  capped.first,  1.0e-3, name + " shelf at Q 8 is the Q 2 shelf, below f0");
            checkClose (asked.second, capped.second, 1.0e-3, name + " shelf at Q 8 is the Q 2 shelf, above f0");
        }

        const auto bell8 = shelfAt (8.0f, 0), bell2 = shelfAt (2.0f, 0);
        check (std::abs (bell8.first - bell2.first) > 1.0, "a bell's Q is not capped");

        check (P::effectiveQ (1, 30.0f) == 2.0f && P::effectiveQ (2, 1.5f) == 1.5f && P::effectiveQ (0, 30.0f) == 30.0f
                   && P::effectiveQ (3, 30.0f) == 30.0f,
               "effectiveQ caps shelves only, and only above the cap");
    }

    //== AUTO ===================================================================
    // Output compensation, BMO EQ's rule: the reciprocal of the static curve's
    // mean magnitude, 48 log points 20 Hz - 20 kHz. Dynamic movement is not
    // compensated.
    {
        auto proc = prepared();
        setValue (*proc, "b5_on", 1.0f);
        setValue (*proc, "b5_freq", 1000.0f);
        setValue (*proc, "b5_gain", 12.0f);
        setValue (*proc, "b5_q", 0.5f);

        const auto without = gainAt (*proc, 1000.0);
        setValue (*proc, "auto_gain", 1.0f);
        const auto with = gainAt (*proc, 1000.0);

        // The reference: the same mean, worked out here from the design.
        bmo::deq::Settings s;
        s.bands[0].enabled = true;
        s.bands[0].shape = bmo::deq::Shape::bell;
        s.bands[0].frequencyHz = 1000.0;
        s.bands[0].gainDb = 12.0;
        s.bands[0].q = 0.5;
        const auto mean = bmo::deq::staticBroadbandGain (s, bmo::deq::DesignGrid::make (48000.0));
        const auto expectDb = -20.0 * std::log10 (mean);

        check (expectDb < -3.0, "a wide +12 dB bell raises the broadband level by more than 3 dB");
        checkClose (with - without, expectDb, 0.02, "AUTO takes back the broadband rise");

        setValue (*proc, "auto_gain", 0.0f);
        checkClose (gainAt (*proc, 1000.0), without, 0.02, "and switching it off gives it back");

        // A flat curve needs nothing.
        auto flat = prepared();
        setValue (*flat, "auto_gain", 1.0f);
        checkClose (gainAt (*flat, 1000.0), 0.0, 1.0e-4, "AUTO on a wire is a wire");

        // A side band is not a centred source's level, so it moves nothing.
        auto side = prepared();
        setValue (*side, "b5_on", 1.0f);
        setValue (*side, "b5_place", 2.0f);
        setValue (*side, "b5_gain", 12.0f);
        setValue (*side, "auto_gain", 1.0f);
        checkClose (gainAt (*side, 1000.0), 0.0, 1.0e-3, "AUTO ignores a side band (a centred tone is untouched)");

        // Dynamics are left alone: a band that cuts a loud tone by its range
        // comes out the same with AUTO on as off, because a dynamic band at
        // 0 dB knob gain has a flat static curve.
        auto dyn = prepared();
        setValue (*dyn, "b6_on", 1.0f);
        setValue (*dyn, "b6_freq", 2000.0f);
        setValue (*dyn, "b6_dyn", 1.0f);
        setValue (*dyn, "b6_thr", -40.0f);
        setValue (*dyn, "b6_range", -9.0f);
        setValue (*dyn, "b6_ratio", 20.0f);
        setValue (*dyn, "auto_gain", 1.0f);
        checkClose (gainAt (*dyn, 2000.0, 0.5), -9.0, 0.2, "AUTO does not make up a dynamic band's cut");

        // A curve that is nearly all cut asks for more makeup than AUTO gives:
        // two 20 Hz high cuts leave almost nothing, and AUTO stops at +18 dB.
        auto shut = prepared();
        for (const auto* b : { "b1", "b2" })
        {
            setValue (*shut, (juce::String (b) + "_on").toRawUTF8(), 1.0f);
            setValue (*shut, (juce::String (b) + "_shape").toRawUTF8(), 4.0f);
            setValue (*shut, (juce::String (b) + "_freq").toRawUTF8(), 20.0f);
        }
        const auto shutOff = gainAt (*shut, 20.0);
        setValue (*shut, "auto_gain", 1.0f);
        checkClose (gainAt (*shut, 20.0) - shutOff, 20.0 * std::log10 (P::DeqDsp::kAutoMax), 0.05,
                    "AUTO's makeup is capped at +18 dB");
    }

    //== The view ===============================================================
    {
        auto proc = createDeq();
        check (proc->isExpanded(), "standalone opens full");
        check (P::module().designWidth == 320 && P::module().expandedWidth == 600, "compact 320, full 600");
    }

    //== Presets ================================================================
    {
        const auto& presets = P::factory();
        check (! presets.empty() && juce::String (presets[0].name) == "Init" && presets[0].settings.empty(),
               "Init is first, and is every default");
    }

    return finish ("BMO DEQ");
}
