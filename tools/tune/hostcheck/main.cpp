// Loads the built VST3 the way a host does, and checks what a host would see:
//
//   bmo-tune-hostcheck "<path to BMO Tune RT.vst3>"
//
// Not pluginval, which is not on this machine and is a download. This covers
// the part of it that a first build most needs: the binary loads, says who it
// is, carries the frozen parameter list, corrects pitch through the real
// wrapper, always reports 0 latency, and brings a saved state back. Pitch is
// measured with the offline ruler (tools/common/Analysis.h), never with the
// plugin's own detector.

#include "modules/tune/dsp/TuneCore.h"
#include "modules/tune/params.h"
#include "tests/dsp/tune/TestUtil.h"
#include "tools/tune/common/Analysis.h"
#include "tools/tune/common/Signals.h"

#include <juce_audio_processors/juce_audio_processors.h>
#include <iostream>

using namespace bmo::tune;
using namespace bmo::tune::test;

namespace
{
    constexpr double fs = 48000.0;
    constexpr int block = 512;

    std::unique_ptr<juce::AudioPluginInstance> load (juce::VST3PluginFormat& format, const juce::String& path)
    {
        juce::OwnedArray<juce::PluginDescription> found;
        format.findAllTypesForFile (found, path);

        if (found.isEmpty())
            return nullptr;

        juce::String error;
        auto instance = format.createInstanceFromDescription (*found[0], fs, block, error);

        if (instance == nullptr)
            std::cerr << "could not instantiate: " << error << '\n';

        return instance;
    }

    juce::AudioProcessorParameter* find (juce::AudioPluginInstance& p, const juce::String& name)
    {
        for (auto* param : p.getParameters())
            if (param->getName (64) == name)
                return param;

        return nullptr;
    }

    /** Sets a parameter to a real value through its normalised range, the
        way host automation arrives. */
    void set (juce::AudioProcessorParameter& param, const bmo::ParamSpec& spec, float real)
    {
        param.setValueNotifyingHost (spec.toNormalised (real));
    }

    /** Runs `in` through the plugin in host-sized stereo blocks. */
    std::vector<float> run (juce::AudioPluginInstance& p, const std::vector<float>& in)
    {
        std::vector<float> out (in.size());
        juce::AudioBuffer<float> buffer (2, block);
        juce::MidiBuffer midi;

        for (size_t at = 0; at < in.size(); at += block)
        {
            const auto n = (int) std::min<size_t> (block, in.size() - at);
            buffer.setSize (2, n, false, false, true);

            for (int ch = 0; ch < 2; ++ch)
                std::copy (in.begin() + (long) at, in.begin() + (long) at + n, buffer.getWritePointer (ch));

            p.processBlock (buffer, midi);
            std::copy (buffer.getReadPointer (0), buffer.getReadPointer (0) + n, out.begin() + (long) at);
        }

        return out;
    }
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    if (argc < 2)
    {
        std::cerr << "usage: bmo-tune-hostcheck <path to BMO Tune RT.vst3>\n";
        return 2;
    }

    juce::VST3PluginFormat format;
    auto plugin = load (format, juce::String (juce::CharPointer_UTF8 (argv[1])));
    check (plugin != nullptr, "the VST3 loads and instantiates");

    if (plugin == nullptr)
        return finish ("hostcheck");

    const auto desc = plugin->getPluginDescription();
    report ("plugin: " + desc.name.toStdString() + " by " + desc.manufacturerName.toStdString(), 0);
    check (desc.name == "BMO Tune RT", "it is called BMO Tune RT");
    check (desc.manufacturerName == "LT3 Audio", "by LT3 Audio");
    check (! plugin->acceptsMidi() && ! plugin->producesMidi(), "and neither takes nor makes MIDI");

    //== The frozen list, as the host sees it ===================================
    const auto& all = specs();
    int matched = 0;

    for (int i = 0; i < Index::count; ++i)
        matched += find (*plugin, all[(size_t) i].name) != nullptr ? 1 : 0;

    report ("parameters the host sees", (double) plugin->getParameters().size());
    check (matched == Index::count, "every one of the " + std::to_string (Index::count) + " parameters is there by its name ("
                                     + std::to_string (matched) + " found)");

    auto* key = find (*plugin, "Key");
    check (key != nullptr && key->getNumSteps() == kNumKeySpellings, "Key has seventeen spellings");
    if (key != nullptr)
    {
        set (*key, all[(size_t) Index::key], 15.0f);
        check (key->getCurrentValueAsText() == "Bb", "and the host shows Bb as Bb, not A#: got "
                                                     + key->getCurrentValueAsText().toStdString());
        set (*key, all[(size_t) Index::key], 0.0f);
    }

    //== Correction through the real wrapper ====================================
    plugin->setPlayConfigDetails (2, 2, fs, block);
    plugin->prepareToPlay (fs, block);

    check (plugin->getLatencySamples() == 0, "Live, the default, reports 0 samples to the host");

    {
        // A voice 30 cents sharp of A3, into the defaults: chromatic, hard.
        const auto sharp = 220.0 * std::exp2 (30.0 / 1200.0);
        const auto in = signals::voice (signals::steady (sharp, 1.5, fs), fs).samples;
        const auto out = run (*plugin, in);

        const auto start = (size_t) (0.6 * fs), length = (size_t) (0.6 * fs);
        const auto before = analysis::measureHz (in, start, length, fs);
        const auto after = analysis::measureHz (out, start, length, fs);

        report ("in, cents from A3", analysis::cents (before, 220.0), "c");
        report ("out, cents from A3", analysis::cents (after, 220.0), "c");
        check (std::abs (analysis::cents (after, 220.0)) < 3.0, "a voice 30 cents sharp comes out on A3 through the VST3");

        bool finite = true;
        for (auto s : out)
            finite = finite && std::isfinite (s);
        check (finite, "and every sample out is finite");
    }

    //== Latency stays 0 =========================================================
    {
        // Live is the only contract: no setting may move what the host is
        // told. Range is the one that used to (through Studio), so move it
        // and let a change reach the plugin the way a VST3 host delivers one:
        // inside the next process call, then the message thread.
        if (auto* range = find (*plugin, "Pitch Range"))
        {
            set (*range, all[(size_t) Index::range], 3.0f);   // Bass
            run (*plugin, std::vector<float> ((size_t) block * 4, 0.0f));
            juce::MessageManager::getInstance()->runDispatchLoopUntil (300);
            check (plugin->getLatencySamples() == 0, "on the Bass range too, the host is told 0 samples");
            set (*range, all[(size_t) Index::range], 0.0f);
        }
    }

    //== A saved state comes back ===============================================
    {
        auto* scale = find (*plugin, "Scale");
        auto* retune = find (*plugin, "Retune Speed");
        check (scale != nullptr && retune != nullptr && key != nullptr, "Scale, Retune Speed and Key exist to save");

        if (scale != nullptr && retune != nullptr && key != nullptr)
        {
            set (*scale, all[(size_t) Index::scale], 2.0f);
            set (*retune, all[(size_t) Index::retuneMs], (float) retuneStepOfMs (12.0));
            set (*key, all[(size_t) Index::key], 15.0f);

            check (retune->getCurrentValueAsText() == "12 ms", "the host shows Retune Speed in ms: got "
                                                               + retune->getCurrentValueAsText().toStdString());
            check (retune->getNumSteps() == kNumRetuneSteps, "Retune Speed has 146 steps, 0.0 to 100 ms");

            juce::MemoryBlock state;
            plugin->getStateInformation (state);

            auto second = load (format, juce::String (juce::CharPointer_UTF8 (argv[1])));
            check (second != nullptr, "a second instance loads");

            if (second != nullptr)
            {
                second->setStateInformation (state.getData(), (int) state.getSize());

                const auto same = [&] (const char* name)
                {
                    auto* a = find (*plugin, name);
                    auto* b = find (*second, name);
                    return a != nullptr && b != nullptr && std::abs (a->getValue() - b->getValue()) < 1.0e-4f;
                };

                check (same ("Scale") && same ("Retune Speed") && same ("Key"),
                       "Minor, Retune 12 ms and Key Bb survive a save and reload");
            }
        }
    }

    plugin->releaseResources();
    return finish ("hostcheck");
}
