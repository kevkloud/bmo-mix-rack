// Renders a WAV through any VST3 as a host would, with no delay
// compensation, so another tuner's true latency and correction can be scored
// against BMO Tune RT's by the same code (tools/common/Stimulus.h,
// bmo-tune-ref score):
//
//   bmo-tune-hostrender <plugin.vst3> --types
//   bmo-tune-hostrender <plugin.vst3> [--type <name part>] --params
//   bmo-tune-hostrender <plugin.vst3> [--type <name part>] in.wav out.wav
//                       [--set "<parameter name>=<text>"] ...
//                       [--setn "<parameter name>=<0..1>"] ... [--block N]
//                       [--preroll seconds]
//
// --type picks one plugin out of a shell (WaveShell holds hundreds). --set
// goes through the plugin's own text parsing, so values are typed as its UI
// shows them; --setn sets the normalised value, for parameters whose text
// the plugin does not parse (Auto-Tune's choices). Each is echoed back as
// the plugin then shows it. The output is the plugin's left channel from sample 0 of the
// input, uncompensated: sample 0 out is what the plugin produced while
// sample 0 went in. What the plugin *reports* is printed alongside, so the
// two can be compared.
//
// Loading someone else's plugin needs its licence on this machine; a plugin
// that is not licensed may load and output silence, so check the render.

#include "tools/tune/common/Wav.h"

#include <juce_audio_processors/juce_audio_processors.h>
#include <iostream>

namespace wav = bmo::tune::wav;

namespace
{
    std::unique_ptr<juce::AudioPluginInstance> load (juce::VST3PluginFormat& format, const juce::String& path,
                                                     const juce::String& typePart, double fs, int block)
    {
        juce::OwnedArray<juce::PluginDescription> found;
        format.findAllTypesForFile (found, path);

        for (auto* d : found)
        {
            if (typePart.isNotEmpty() && ! d->name.containsIgnoreCase (typePart))
                continue;

            juce::String error;
            auto instance = format.createInstanceFromDescription (*d, fs, block, error);
            if (instance == nullptr)
                std::cerr << "could not instantiate " << d->name << ": " << error << '\n';
            return instance;
        }

        std::cerr << "no plugin" << (typePart.isNotEmpty() ? " matching \"" + typePart + "\"" : juce::String())
                  << " in " << path << '\n';
        return nullptr;
    }

    juce::AudioProcessorParameter* find (juce::AudioPluginInstance& p, const juce::String& name)
    {
        for (auto* param : p.getParameters())
            if (param->getName (128).trim().equalsIgnoreCase (name.trim()))
                return param;
        return nullptr;
    }
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    if (argc < 3)
    {
        std::cerr << "usage: bmo-tune-hostrender <plugin.vst3> --types | [--type name] --params | [--type name] in.wav out.wav [--set \"Name=text\"]... [--block N]\n";
        return 2;
    }

    const juce::String path { juce::CharPointer_UTF8 (argv[1]) };
    juce::VST3PluginFormat format;

    juce::String typePart, inPath, outPath;
    juce::StringArray sets;
    bool listTypes = false, listParams = false;
    int block = 128;
    double preroll = 0.0;

    for (int i = 2; i < argc; ++i)
    {
        const juce::String a { juce::CharPointer_UTF8 (argv[i]) };
        if (a == "--types")                       listTypes = true;
        else if (a == "--params")                 listParams = true;
        else if (a == "--type" && i + 1 < argc)   typePart = juce::CharPointer_UTF8 (argv[++i]);
        else if (a == "--set" && i + 1 < argc)    sets.add (juce::CharPointer_UTF8 (argv[++i]));
        else if (a == "--setn" && i + 1 < argc)   sets.add ("#" + juce::String (juce::CharPointer_UTF8 (argv[++i])));
        else if (a == "--block" && i + 1 < argc)  block = juce::String (argv[++i]).getIntValue();
        else if (a == "--preroll" && i + 1 < argc) preroll = juce::String (argv[++i]).getDoubleValue();
        else if (inPath.isEmpty())                inPath = a;
        else                                      outPath = a;
    }

    if (listTypes)
    {
        juce::OwnedArray<juce::PluginDescription> found;
        format.findAllTypesForFile (found, path);
        for (auto* d : found)
            std::cout << d->name << "  (" << d->manufacturerName << ", " << d->version << ")\n";
        return found.isEmpty() ? 1 : 0;
    }

    wav::Channels in;
    double fs = 48000.0;
    if (! listParams && ! wav::read (inPath.toStdString(), in, fs))
    {
        std::cerr << "cannot read " << inPath << '\n';
        return 1;
    }

    auto plugin = load (format, path, typePart, fs, block);
    if (plugin == nullptr)
        return 1;

    std::cout << "plugin: " << plugin->getName() << "\n";

    if (listParams)
    {
        const auto& params = plugin->getParameters();
        for (int i = 0; i < params.size(); ++i)
            std::cout << i << "\t" << params[i]->getName (128) << "\t= " << params[i]->getCurrentValueAsText()
                      << "\t(steps " << params[i]->getNumSteps() << ")\n";
        return 0;
    }

    plugin->setPlayConfigDetails (2, 2, fs, block);
    plugin->setNonRealtime (false);
    plugin->prepareToPlay (fs, block);

    // Printed back as the plugin reads it: some plugins parse text for their
    // continuous parameters only, and a choice set by text can land on its
    // default without complaint -- --setn (a normalised 0-1 value) is the
    // way round that, and the echo is how to tell.
    for (const auto& entry : sets)
    {
        const auto normalised = entry.startsWith ("#");
        const auto s = normalised ? entry.substring (1) : entry;
        const auto name = s.upToFirstOccurrenceOf ("=", false, false);
        const auto text = s.fromFirstOccurrenceOf ("=", false, false);
        auto* p = find (*plugin, name);
        if (p == nullptr)
        {
            std::cerr << "no parameter named \"" << name << "\" (see --params)\n";
            return 2;
        }
        p->setValueNotifyingHost (normalised ? text.getFloatValue() : p->getValueForText (text));
        std::cout << "set " << name << " = " << p->getCurrentValueAsText() << "\n";
    }

    // No pre-roll unless asked for: the plugin starts fresh at sample 0, as
    // Ableton's export starts it. Silence before the file is not neutral --
    // BMO's detector evaluates on a grid of samples counted from the start,
    // so any pre-roll moves every evaluation and changes the output (the
    // first version ran 0.5 s of it and differed from Ableton by -28 dB).
    const auto& x = in.front();
    juce::AudioBuffer<float> buffer (2, block);
    juce::MidiBuffer midi;

    for (int k = 0; k < (int) (preroll * fs) / block; ++k)
    {
        buffer.clear();
        plugin->processBlock (buffer, midi);
    }

    std::vector<float> out (x.size(), 0.0f);
    for (size_t at = 0; at < x.size(); at += (size_t) block)
    {
        const auto n = (int) std::min<size_t> ((size_t) block, x.size() - at);
        buffer.setSize (2, n, false, false, true);
        for (int ch = 0; ch < 2; ++ch)
            std::copy (x.begin() + (long) at, x.begin() + (long) at + n, buffer.getWritePointer (ch));
        plugin->processBlock (buffer, midi);
        std::copy (buffer.getReadPointer (0), buffer.getReadPointer (0) + n, out.begin() + (long) at);
    }

    const auto reported = plugin->getLatencySamples();
    std::cout << "reported latency: " << reported << " samples (" << 1000.0 * reported / fs << " ms) at "
              << fs << " Hz, block " << block << "\n";

    double peak = 0.0;
    for (auto v : out)
        peak = std::max (peak, (double) std::abs (v));
    std::cout << "output peak: " << (peak > 0.0 ? 20.0 * std::log10 (peak) : -999.0) << " dBFS\n";

    plugin->releaseResources();
    return wav::writeMono (outPath.toStdString(), out, fs) ? 0 : 1;
}
