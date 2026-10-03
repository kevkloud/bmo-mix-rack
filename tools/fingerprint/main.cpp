// Runs fixed audio through every built VST3, as a host would, and prints a hash
// of what came out:
//
//   audio_fingerprint <build dir or .vst3> [more ...] [dump=<dir>]
//   audio_fingerprint diff <a.f32> <b.f32>
//
// It exists because two builds of the same source are not always built the same
// way. A pull-request and a main build skip link-time optimisation and a
// release tag keeps it (the BMO_LTO option in the root CMakeLists.txt), so the
// plugins someone listens to from a main run are not bit-for-bit the plugins a
// tag ships. Whether they make the same audio is a question with a number for
// an answer, and this prints the number: same source, same platform, same
// lines here means the same samples out, whatever the optimiser did.
//
// What it is not: a test of whether the audio is right. tests/ does that. The
// hashes are not written down anywhere to be compared against, because every
// DSP change moves them on purpose. Compare two runs of ONE commit on ONE
// platform. Across platforms the lines differ and that means nothing -- the
// maths libraries differ, and nothing here asks them to agree.
//
// Every render is made twice, each in a fresh instance, and the two must agree
// before the hash is worth anything: a plugin whose output depended on thread
// timing would differ between two builds for a reason that has nothing to do
// with how either was built. A render that does not repeat is printed as
// UNSTABLE and the tool exits 1.
//
// "dump=<dir>" also writes each render as raw 32-bit floats, channel after
// channel, so that when two builds disagree "diff" can say by how much rather
// than only that they do. Point it outside the repository: raw audio is still
// audio, and no audio is committed here (AGENTS.md).

#include <juce_audio_processors/juce_audio_processors.h>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <vector>

namespace
{
    struct Config
    {
        double rate;
        int block;
    };

    // Two rates and two block sizes, one of them not a power of two, so a path
    // that only runs on a ragged block or at 44.1 kHz is in the fingerprint.
    constexpr Config configs[] = { { 48000.0, 512 }, { 44100.0, 100 } };

    constexpr double stimulusSeconds = 7.0;

    /** xorshift32. The stimulus and the parameter positions come from integer
        state and exact float steps only, with no call into a maths library, so
        the audio going in cannot be what differs between two builds. Its hash
        is printed anyway, as the check that it did not. */
    struct Rng
    {
        uint32_t state;

        uint32_t next() noexcept
        {
            state ^= state << 13;
            state ^= state >> 17;
            state ^= state << 5;
            return state;
        }

        /** [0, 1) on a 24-bit grid, which a float holds exactly. */
        float unit() noexcept     { return (float) (next() >> 8) * (1.0f / 16777216.0f); }

        /** [-1, 1) on the same grid. */
        float bipolar() noexcept  { return (float) (next() >> 8) * (1.0f / 8388608.0f) - 1.0f; }
    };

    /** A triangle from a 32-bit phase: 2|x| - 1 with x the phase as a signed
        fraction. No sine, so no maths library. */
    float triangle (uint32_t phase) noexcept
    {
        const auto x = (float) (int32_t) phase * (1.0f / 2147483648.0f);
        return 2.0f * (x < 0.0f ? -x : x) - 1.0f;
    }

    /** Seven seconds meant to wake every kind of module: an impulse for a
        reverb and a filter, noise at three levels 12 dB apart for anything
        with a threshold, low, mid and high tones for an equaliser and a
        saturator, a tone with hiss standing over it for a de-esser, and two
        seconds of silence for the tails. Left and right differ throughout, for
        the modules that care about width. Gains are powers of two. */
    juce::AudioBuffer<float> makeStimulus (double rate)
    {
        const auto at = [rate] (double seconds) { return (int) (seconds * rate); };

        juce::AudioBuffer<float> s (2, at (stimulusSeconds));
        s.clear();

        auto* l = s.getWritePointer (0);
        auto* r = s.getWritePointer (1);

        l[at (0.25)] = 0.5f;
        r[at (0.25) + 7] = 0.5f;

        Rng left { 0x9e3779b9u }, right { 0x7f4a7c15u };

        const auto noise = [&] (double from, double to, float gain)
        {
            for (int i = at (from); i < at (to); ++i)
            {
                l[i] += gain * left.bipolar();
                r[i] += gain * right.bipolar();
            }
        };

        noise (0.75, 1.50, 0.03125f);
        noise (1.50, 2.25, 0.125f);
        noise (2.25, 3.00, 0.5f);

        const auto step = [rate] (double hz) { return (uint32_t) (hz / rate * 4294967296.0); };
        const uint32_t steps[] = { step (110.0), step (1000.0), step (7000.0) };
        uint32_t phases[] = { 0u, 0u, 0u };

        float lastL = 0.0f, lastR = 0.0f;

        for (int i = at (3.25); i < at (5.0); ++i)
        {
            const auto low  = triangle (phases[0]);
            const auto mid  = triangle (phases[1]);
            const auto high = triangle (phases[2]);

            for (int k = 0; k < 3; ++k)
                phases[k] += steps[k];

            const auto hissing = i >= at (4.25);
            const auto level = hissing ? 0.5f : 1.0f;

            l[i] += level * (0.25f * low + 0.125f * mid + 0.0625f * high);
            r[i] += level * (0.125f * low + 0.25f * mid + 0.0625f * high);

            if (hissing)
            {
                // Noise minus its own last sample: a crude high-pass, which is
                // all "hiss" has to mean here.
                const auto nl = left.bipolar(), nr = right.bipolar();
                l[i] += 0.25f * (nl - lastL);
                r[i] += 0.25f * (nr - lastR);
                lastL = nl;
                lastR = nr;
            }
        }

        return s;
    }

    uint64_t fnv1a (const void* data, size_t bytes, uint64_t hash = 0xcbf29ce484222325ull) noexcept
    {
        const auto* p = static_cast<const unsigned char*> (data);

        for (size_t i = 0; i < bytes; ++i)
            hash = (hash ^ p[i]) * 0x100000001b3ull;

        return hash;
    }

    uint64_t hashOf (const juce::AudioBuffer<float>& b)
    {
        auto hash = fnv1a (nullptr, 0);

        for (int ch = 0; ch < b.getNumChannels(); ++ch)
            hash = fnv1a (b.getReadPointer (ch), sizeof (float) * (size_t) b.getNumSamples(), hash);

        return hash;
    }

    juce::String hex (uint64_t v)
    {
        return juce::String::toHexString ((juce::int64) v).paddedLeft ('0', 16);
    }

    /** What a render starts from. `chain` is for the rack only, whose modules
        are session state rather than parameters; `seed` 0 leaves every
        parameter at its default. */
    struct State
    {
        juce::String name;
        juce::StringArray chain;
        uint32_t seed = 0;
    };

    // The rack's module ids, as products/rack/Registry.cpp registers them, in
    // two chains because a rack has eight slots and there are eleven modules. An
    // id the rack does not know is dropped by the rack itself, so a module
    // added later is simply not covered until it is listed here -- and the
    // rack's own line then says "thru" or stays the same, which is the cue.
    const juce::StringArray chainA { "eq", "sat", "opto", "dim", "deq", "vcomp", "util" };
    const juce::StringArray chainB { "deesser", "fetcomp", "dwell", "reverb", "eq", "util" };

    std::vector<State> statesFor (const juce::PluginDescription& desc)
    {
        if (desc.name.containsIgnoreCase ("Rack"))
            return { { "empty", {}, 0 },
                     { "chainA", chainA, 0 }, { "chainA+seed1", chainA, 0x1234567u },
                     { "chainB", chainB, 0 }, { "chainB+seed2", chainB, 0x89abcdeu } };

        return { { "default", {}, 0 },
                 { "seed1", {}, 0x1234567u },
                 { "seed2", {}, 0x89abcdeu },
                 { "seed3", {}, 0x2468aceu } };
    }

    /** Lets whatever the plugin queued for the message thread run. A module
        that settles a restored state there has then settled before the first
        block, every time, rather than whenever a timer happened to fire. */
    void settle()
    {
        juce::MessageManager::getInstance()->runDispatchLoopUntil (60);
    }

    /** A rack session holding `ids` and nothing else, in the form JUCE's VST3
        host hands a plugin its saved state: the rack's own RACK/SLOT document
        (core/rack/RackProcessor.cpp), inside the host's IComponent chunk. A
        slot with no PARAMS child opens at its module's defaults. */
    void loadChain (juce::AudioPluginInstance& rack, const juce::StringArray& ids)
    {
        juce::XmlElement session ("RACK");
        int index = 0;

        for (auto& id : ids)
        {
            auto* slot = session.createNewChildElement ("SLOT");
            slot->setAttribute ("index", index++);
            slot->setAttribute ("module", id);
        }

        juce::MemoryBlock inner;
        juce::AudioProcessor::copyXmlToBinary (session, inner);

        juce::XmlElement host ("VST3PluginState");
        host.createNewChildElement ("IComponent")->addTextElement (inner.toBase64Encoding());

        juce::MemoryBlock outer;
        juce::AudioProcessor::copyXmlToBinary (host, outer);
        rack.setStateInformation (outer.getData(), (int) outer.getSize());
    }

    struct Render
    {
        juce::String error;
        juce::AudioBuffer<float> out;
        int ins = 0, outs = 0, latency = 0;
    };

    Render render (juce::VST3PluginFormat& format, const juce::PluginDescription& desc,
                   Config config, const State& state, const juce::AudioBuffer<float>& stimulus)
    {
        Render result;

        auto plugin = format.createInstanceFromDescription (desc, config.rate, config.block, result.error);

        if (plugin == nullptr)
            return result;

        plugin->setPlayConfigDetails (2, 2, config.rate, config.block);
        plugin->prepareToPlay (config.rate, config.block);

        result.ins = plugin->getTotalNumInputChannels();
        result.outs = plugin->getTotalNumOutputChannels();

        if (result.ins < 1 || result.outs < 1)
        {
            result.error = "no audio in or no audio out";
            return result;
        }

        if (! state.chain.isEmpty())
            loadChain (*plugin, state.chain);

        settle();

        if (state.seed != 0)
        {
            Rng rng { state.seed };

            // The host's own bypass is left alone: a render of a bypassed
            // plugin is a render of the stimulus.
            for (auto* p : plugin->getParameters())
            {
                const auto value = rng.unit();

                if (p != plugin->getBypassParameter())
                    p->setValue (value);
            }

            settle();
        }

        const auto total = stimulus.getNumSamples();
        const auto channels = juce::jmax (result.ins, result.outs);

        result.out.setSize (result.outs, total);
        juce::AudioBuffer<float> io (channels, config.block);
        juce::MidiBuffer midi;

        for (int at = 0; at < total; at += config.block)
        {
            const auto n = juce::jmin (config.block, total - at);
            io.setSize (channels, n, false, false, true);
            io.clear();

            for (int ch = 0; ch < juce::jmin (result.ins, 2); ++ch)
                io.copyFrom (ch, 0, stimulus, ch, at, n);

            plugin->processBlock (io, midi);
            midi.clear();

            for (int ch = 0; ch < result.outs; ++ch)
                result.out.copyFrom (ch, at, io, ch, 0, n);
        }

        result.latency = plugin->getLatencySamples();
        plugin->releaseResources();
        return result;
    }

    bool same (const juce::AudioBuffer<float>& a, const juce::AudioBuffer<float>& b)
    {
        if (a.getNumChannels() != b.getNumChannels() || a.getNumSamples() != b.getNumSamples())
            return false;

        for (int ch = 0; ch < a.getNumChannels(); ++ch)
            if (std::memcmp (a.getReadPointer (ch), b.getReadPointer (ch),
                             sizeof (float) * (size_t) a.getNumSamples()) != 0)
                return false;

        return true;
    }

    /** True when the plugin passed the stimulus through untouched, on the
        channels it was fed. Printed, because a hash of the input is a line
        that proves nothing about the plugin. */
    bool isThru (const juce::AudioBuffer<float>& out, const juce::AudioBuffer<float>& stimulus)
    {
        const auto channels = juce::jmin (out.getNumChannels(), stimulus.getNumChannels());

        for (int ch = 0; ch < channels; ++ch)
            if (std::memcmp (out.getReadPointer (ch), stimulus.getReadPointer (ch),
                             sizeof (float) * (size_t) stimulus.getNumSamples()) != 0)
                return false;

        return true;
    }

    juce::String decibels (float linear)
    {
        if (linear <= 0.0f)
            return "-inf";

        return juce::String (20.0 * std::log10 ((double) linear), 2);
    }

    void dump (const juce::File& dir, const juce::String& name, const juce::AudioBuffer<float>& b)
    {
        dir.createDirectory();
        auto file = dir.getChildFile (juce::File::createLegalFileName (name) + ".f32");
        file.deleteFile();

        if (auto stream = file.createOutputStream())
            for (int ch = 0; ch < b.getNumChannels(); ++ch)
                stream->write (b.getReadPointer (ch), sizeof (float) * (size_t) b.getNumSamples());
    }

    /** How far apart two dumps are, for the day two builds disagree: a last
        bit somewhere and a different sound are both "the hashes differ". */
    int diff (const juce::File& a, const juce::File& b)
    {
        juce::MemoryBlock x, y;

        if (! a.loadFileAsData (x) || ! b.loadFileAsData (y))
        {
            std::cerr << "could not read both files\n";
            return 2;
        }

        if (x.getSize() != y.getSize())
        {
            std::cout << "different lengths: " << x.getSize() << " and " << y.getSize() << " bytes\n";
            return 1;
        }

        const auto* p = static_cast<const float*> (x.getData());
        const auto* q = static_cast<const float*> (y.getData());
        const auto n = x.getSize() / sizeof (float);

        size_t differing = 0;
        float worst = 0.0f, peak = 0.0f;

        for (size_t i = 0; i < n; ++i)
        {
            const auto d = std::abs (p[i] - q[i]);
            differing += std::memcmp (p + i, q + i, sizeof (float)) != 0 ? 1u : 0u;
            worst = juce::jmax (worst, d);
            peak = juce::jmax (peak, std::abs (p[i]));
        }

        std::cout << differing << " of " << n << " samples differ; largest difference "
                  << decibels (worst) << " dBFS against a peak of " << decibels (peak) << " dBFS\n";
        return differing == 0 ? 0 : 1;
    }
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    juce::StringArray args;

    for (int i = 1; i < argc; ++i)
        args.add (juce::String (juce::CharPointer_UTF8 (argv[i])));

    const auto resolve = [] (const juce::String& path)
    {
        return juce::File::getCurrentWorkingDirectory().getChildFile (path);
    };

    if (args.size() == 3 && args[0] == "diff")
        return diff (resolve (args[1]), resolve (args[2]));

    juce::File dumpDir;
    juce::FileSearchPath places;

    for (auto& arg : args)
    {
        if (arg.startsWith ("dump="))
            dumpDir = resolve (arg.fromFirstOccurrenceOf ("=", false, false));
        else
            places.add (resolve (arg));
    }

    if (places.getNumPaths() == 0)
    {
        std::cerr << "usage: audio_fingerprint <build dir or .vst3> [more ...] [dump=<dir>]\n"
                     "       audio_fingerprint diff <a.f32> <b.f32>\n";
        return 2;
    }

    juce::VST3PluginFormat format;
    juce::OwnedArray<juce::PluginDescription> products;

    // A path that is itself a plugin is taken as one; anything else is a
    // folder to look through. A build tree holds each product once.
    for (int i = 0; i < places.getNumPaths(); ++i)
    {
        const auto place = places[i];

        if (place.hasFileExtension ("vst3"))
        {
            format.findAllTypesForFile (products, place.getFullPathName());
            continue;
        }

        for (auto& found : format.searchPathsForPlugins (juce::FileSearchPath (place.getFullPathName()), true, false))
            format.findAllTypesForFile (products, found);
    }

    if (products.isEmpty())
    {
        std::cerr << "no VST3 found\n";
        return 2;
    }

    std::sort (products.begin(), products.end(),
               [] (const juce::PluginDescription* a, const juce::PluginDescription* b) { return a->name < b->name; });

    bool allGood = true;
    auto overall = fnv1a (nullptr, 0);

    const auto print = [&overall] (const juce::String& line)
    {
        std::cout << line << '\n';
        overall = fnv1a (line.toRawUTF8(), line.getNumBytesAsUTF8(), overall);
    };

    for (auto config : configs)
    {
        const auto stimulus = makeStimulus (config.rate);
        const auto where = juce::String ((int) config.rate) + "/" + juce::String (config.block);

        print ("stimulus | " + where + " | " + hex (hashOf (stimulus)));

        for (auto* desc : products)
        {
            for (auto& state : statesFor (*desc))
            {
                const auto label = desc->name + " | " + where + " | " + state.name;
                auto first = render (format, *desc, config, state, stimulus);

                if (first.error.isNotEmpty())
                {
                    print (label + " | FAILED: " + first.error);
                    allGood = false;
                    continue;
                }

                const auto second = render (format, *desc, config, state, stimulus);
                const auto repeats = second.error.isEmpty() && same (first.out, second.out);
                allGood = allGood && repeats;

                if (dumpDir != juce::File())
                    dump (dumpDir, desc->name + " " + where.replaceCharacter ('/', '-') + " " + state.name, first.out);

                print (label
                       + " | in " + juce::String (first.ins) + " out " + juce::String (first.outs)
                       + " | latency " + juce::String (first.latency)
                       + " | peak " + decibels (first.out.getMagnitude (0, first.out.getNumSamples())) + " dBFS"
                       + (isThru (first.out, stimulus) ? " | thru" : "")
                       + " | " + (repeats ? hex (hashOf (first.out)) : juce::String ("UNSTABLE")));
            }
        }
    }

    std::cout << "fingerprint: " << hex (overall) << " (" << products.size() << " products)\n";
    return allGood ? 0 : 1;
}
