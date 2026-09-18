// Renders BMO Tune RT's editor to a PNG without a display, so a layout change
// can be looked at rather than described. BMO Mix Rack's tools/snapshot, for
// one product:
//
//   bmo-tune-snapshot out.png [width height] [param=value ...]
//
// Parameters by their ids, choices by name or index: key=Bb, scale=Minor,
// note_d=0; Retune Speed by its milliseconds, retune_ms=12. "appearance=dark|light" renders the other palette
// for this process only -- it neither writes nor reads the machine-wide
// preference, so it cannot flip the look of plugins that happen to be open.
//
// An unknown parameter, or a value that is neither a number nor one of the
// parameter's own choices, is fatal: a render that quietly answered a
// different question than the one asked is worse than none (the rack learned
// this once already -- see its tools/snapshot).

#include "products/tune/Product.h"
#include "modules/tune/panel/TunePanel.h"

#include <juce_gui_basics/juce_gui_basics.h>
#include <iostream>
#include <optional>

namespace
{
    std::optional<float> realValueFor (const bmo::ParamSet& params, int index, const juce::String& text)
    {
        const auto& spec = params.spec (index);

        // Choices named by numbers (Retune Speed's "12 ms") take the number,
        // never an index: retune_ms=12 is 12 ms, not step 12 (1.2 ms).
        if (spec.numChoices() > 0 && juce::CharacterFunctions::isDigit (spec.choices[0][0]))
        {
            if (text.containsOnly ("0123456789.-+"))
                for (int i = 0; i < spec.numChoices(); ++i)
                    if (std::abs (juce::String (spec.choices[(size_t) i]).getDoubleValue() - text.getDoubleValue()) < 1.0e-6)
                        return (float) i;
        }
        else if (text.containsOnly ("0123456789.-+"))
            return text.getFloatValue();

        for (int i = 0; i < spec.numChoices(); ++i)
            if (text.equalsIgnoreCase (juce::String (spec.choices[(size_t) i])))
                return (float) i;

        return {};
    }

    bmo::tune::TunePanel* findPanel (juce::Component& root)
    {
        for (auto* child : root.getChildren())
        {
            if (auto* panel = dynamic_cast<bmo::tune::TunePanel*> (child))
                return panel;

            if (auto* found = findPanel (*child))
                return found;
        }

        return nullptr;
    }
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    if (argc < 2)
    {
        std::cerr << "usage: bmo-tune-snapshot out.png [width height] [param=value ...]\n";
        return 2;
    }

    auto processor = bmo::products::createTune();
    const juce::File out = juce::File::getCurrentWorkingDirectory().getChildFile (argv[1]);

    processor->prepareToPlay (48000.0, 512);

    int first = 2, width = 0, height = 0;

    if (argc > 3 && juce::String (argv[2]).containsOnly ("0123456789")
                 && juce::String (argv[3]).containsOnly ("0123456789"))
    {
        width  = std::atoi (argv[2]);
        height = std::atoi (argv[3]);
        first  = 4;
    }

    // Parameters first, editor second: attachments read the current value in
    // their constructors, and there is no message loop here to deliver a
    // change made afterwards.
    auto& params = processor->getEngine().params();

    for (int i = first; i < argc; ++i)
    {
        const juce::String arg { argv[i] };
        const auto split = arg.indexOfChar ('=');

        if (split < 0)
        {
            std::cerr << "expected param=value, got " << arg << '\n';
            return 2;
        }

        const auto key = arg.substring (0, split);
        const auto value = arg.substring (split + 1);

        if (key == "appearance")
        {
            if (value.equalsIgnoreCase ("dark"))       bmo::ui::overrideAppearance (true);
            else if (value.equalsIgnoreCase ("light")) bmo::ui::overrideAppearance (false);
            else
            {
                std::cerr << "appearance is dark or light, got " << value << '\n';
                return 2;
            }

            continue;
        }

        const auto index = params.indexOf (key.toRawUTF8());

        if (index < 0)
        {
            std::cerr << "unknown parameter: " << key << '\n';
            return 2;
        }

        const auto real = realValueFor (params, index, value);

        if (! real.has_value())
        {
            std::cerr << "not a value for " << key << ": " << value << '\n';
            return 2;
        }

        params.setReal (index, *real);
    }

    std::unique_ptr<juce::AudioProcessorEditor> editor (processor->createEditorAndMakeActive());

    if (editor == nullptr)
    {
        std::cerr << "no editor\n";
        return 1;
    }

    if (auto* panel = findPanel (*editor))
        panel->syncNow();

    if (width > 0 && height > 0)
        editor->setSize (width, height);

    for (int i = 0; i < 8; ++i)
    {
        juce::Thread::sleep (40);
        juce::Timer::callPendingTimersSynchronously();
    }

    const auto image = editor->createComponentSnapshot (editor->getLocalBounds(), false, 2.0f);

    juce::PNGImageFormat png;
    out.deleteFile();
    std::unique_ptr<juce::FileOutputStream> stream (out.createOutputStream());

    if (stream == nullptr || ! png.writeImageToStream (image, *stream))
    {
        std::cerr << "could not write " << out.getFullPathName() << '\n';
        return 1;
    }

    std::cout << "wrote " << out.getFullPathName()
              << " (" << image.getWidth() << "x" << image.getHeight() << ")\n";

    processor->editorBeingDeleted (editor.get());
    editor.reset();
    return 0;
}
