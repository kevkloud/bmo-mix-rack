// Renders a product's editor to a PNG without a display, so a layout change
// can be reviewed in a pull request rather than described in one.
//
//   snapshot <eq|sat|util|opto|dim|deq|ltvcomp|rack> out.png [width height] [param=value ...]
//
// For the rack, "chain=util,eq,sat,opto" sets the modules and "N.id=value"
// sets a parameter of the module in slot N (1-based), e.g. 2.mid_gain=4.
//
// "view=compact|expanded" picks the width of a module that has two (BMO
// DEQ), standalone; "N.view=..." does the same for rack slot N. Standalone
// opens expanded and a rack compact, so these render the other one.
//
// "appearance=dark|light" renders the other palette. Set for this process
// only: it neither writes nor reads the machine-wide preference, so it cannot
// flip the look of plugins that happen to be open.
//
// "theme=<file.json>" overlays a palette, the same flat token -> hex file an
// editor watches, so a candidate colour can be rendered without writing the
// machine-wide Themes/Default.json -- a file the user owns and every open
// plugin is polling. Read before "appearance", whatever order they are given
// in. A missing file is fatal, not ignored: a theme that did not load renders
// the built-in palette, which looks like a good render of the wrong thing.
//
// "signal=<dBFS>" runs a 1 kHz tone through the processor before capturing,
// so a metering panel renders with its meters reading something instead of at
// rest. Needed for any module whose meters are the thing being reviewed.
//
// "ui.<key>=<value>" sets panel state that has no parameter behind it. BMO
// Opto takes "ui.meter=IN|GR|OUT", which is the only way to render its VU in
// anything but OUT. Offered to every panel; refused by all of them is fatal.

#include "products/deq/Product.h"
#include "products/dim/Product.h"
#include "products/eq/Product.h"
#include "products/opto/Product.h"
#include "products/vcomp/Product.h"
#include "products/sat/Product.h"
#include "products/util/Product.h"
#include "products/rack/Product.h"

#include "core/ui/ModulePanel.h"

#include <juce_gui_basics/juce_gui_basics.h>
#include <iostream>
#include <optional>
#include <utility>
#include <vector>

namespace
{
    std::unique_ptr<juce::AudioProcessor> create (const juce::String& product)
    {
        using namespace bmo::products;

        if (product == "eq")   return createEq();
        if (product == "sat")  return createSat();
        if (product == "util") return createUtil();
        if (product == "opto") return createOpto();
        if (product == "dim")  return createDim();
        if (product == "deq")  return createDeq();
        if (product == "ltvcomp") return createVcomp();
        if (product == "rack") return createRack();
        return nullptr;
    }

    /** The real value `text` asks for, or nothing if it names neither a number
        nor one of the parameter's own choices.

        A choice may be given by name -- `mode=Stressed` as well as `mode=1` --
        because that is what anyone reading params.h will type. Before this,
        every non-numeric value went through getFloatValue() and came out 0.0,
        so a choice name, or a typo, silently set the parameter to its *first*
        value and rendered a panel that looked entirely plausible and was of
        the wrong thing. That cost a debugging round trip; a snapshot that
        quietly answers a different question than the one asked is worse than
        one that refuses. */
    std::optional<float> realValueFor (const bmo::ParamSet& params, int index,
                                       const juce::String& text)
    {
        if (text.containsOnly ("0123456789.-+"))
            return text.getFloatValue();

        const auto& spec = params.spec (index);

        for (int i = 0; i < spec.numChoices(); ++i)
            if (text.equalsIgnoreCase (juce::String (spec.choices[(size_t) i])))
                return (float) i;

        return {};
    }

    bool set (juce::AudioProcessor& processor, const juce::String& id, const juce::String& text)
    {
        if (auto* rack = dynamic_cast<bmo::RackProcessor*> (&processor))
        {
            if (id == "chain")
            {
                rack->clearChain();

                for (const auto& m : juce::StringArray::fromTokens (text, ",", {}))
                    if (auto* def = rack->findModule (m.trim()))
                        rack->addModule (*def);
                    else
                        return false;

                return true;
            }

            const auto dot = id.indexOfChar ('.');

            if (dot < 0)
                return false;

            const auto slot = id.substring (0, dot).getIntValue() - 1;
            auto* engine = rack->getEngineAt (slot);

            if (engine == nullptr)
                return false;

            const auto param = id.substring (dot + 1);

            if (param == "view")
            {
                if (! rack->getModuleAt (slot)->isExpandable() || (text != "compact" && text != "expanded"))
                    return false;

                rack->setSlotExpanded (slot, text == "expanded");
                return true;
            }

            const auto i = engine->params().indexOf (param.toRawUTF8());

            if (i < 0)
                return false;

            const auto value = realValueFor (engine->params(), i, text);

            if (! value.has_value())
            {
                std::cerr << "not a value for " << param << ": " << text << '\n';
                return false;
            }

            engine->params().setReal (i, *value);
            return true;
        }

        if (auto* single = dynamic_cast<bmo::SingleModuleProcessor*> (&processor))
        {
            if (id == "view")
            {
                if (! single->getModule().isExpandable() || (text != "compact" && text != "expanded"))
                    return false;

                single->setExpanded (text == "expanded");
                return true;
            }

            const auto i = single->getEngine().params().indexOf (id.toRawUTF8());

            if (i < 0)
                return false;

            const auto value = realValueFor (single->getEngine().params(), i, text);

            if (! value.has_value())
            {
                std::cerr << "not a value for " << id << ": " << text << '\n';
                return false;
            }

            single->getEngine().params().setReal (i, *value);
            return true;
        }

        return false;
    }

    /** Every ModulePanel under `root`: one for a product, one per slot for the
        rack. */
    void collectPanels (juce::Component& root, std::vector<bmo::ui::ModulePanel*>& out)
    {
        for (auto* child : root.getChildren())
        {
            if (auto* panel = dynamic_cast<bmo::ui::ModulePanel*> (child))
                out.push_back (panel);

            collectPanels (*child, out);
        }
    }

    /** Offers `key=value` to every panel and reports whether any took it.

        Offered to all of them rather than addressed to one, because in a rack
        only the module that has the state knows the key. Accepted by none is
        an error, not a no-op: the whole reason this exists is that a render
        which quietly ignores the mode it was asked for is a picture of the
        wrong thing that nothing downstream can tell apart from the right one. */
    bool setUiState (juce::AudioProcessorEditor& editor,
                     const juce::String& key, const juce::String& value)
    {
        std::vector<bmo::ui::ModulePanel*> panels;
        collectPanels (editor, panels);

        bool accepted = false;

        for (auto* panel : panels)
            accepted |= panel->setUiState (key, value);

        return accepted;
    }
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    if (argc < 3)
    {
        std::cerr << "usage: snapshot <eq|sat|util|opto|dim|deq|ltvcomp|rack> out.png [width height] [param=value ...]\n";
        return 2;
    }

    auto processor = create (argv[1]);

    if (processor == nullptr)
    {
        std::cerr << "unknown product: " << argv[1] << '\n';
        return 2;
    }

    const juce::File out = juce::File::getCurrentWorkingDirectory().getChildFile (argv[2]);

    // `rate=<Hz>` has to be read before the flag loop, because prepareToPlay
    // happens here and a panel that draws something rate-dependent reads the
    // rate from the prepared processor. Scanned out of argument order for the
    // same reason `theme=` is: the thing it configures is set up before the
    // loop that would otherwise have handled it.
    //
    // It exists because BMO DEQ's response curve is designed at the running
    // rate, and without this there is no way to render the difference and so
    // no way to check the curve follows it. Default 48 k, which is what every
    // baseline in testing-notes/ui-pass-render-loop.md was taken at.
    auto rate = 48000.0;

    for (int i = 3; i < argc; ++i)
    {
        const juce::String arg (argv[i]);

        if (arg.startsWith ("rate="))
        {
            rate = arg.fromFirstOccurrenceOf ("=", false, false).getDoubleValue();

            if (rate < 8000.0 || rate > 768000.0)
            {
                std::cerr << "rate is 8000..768000 Hz, got " << arg.fromFirstOccurrenceOf ("=", false, false) << '\n';
                return 2;
            }
        }
    }

    processor->prepareToPlay (rate, 512);

    // Parameters first, editor second. Attachments read the current value in
    // their constructors, synchronously; setting parameters afterwards relies
    // on the message queue, which is not running here.
    int first = 3;
    int width = 0, height = 0;

    if (argc > 4 && juce::String (argv[3]).containsOnly ("0123456789")
                 && juce::String (argv[4]).containsOnly ("0123456789"))
    {
        width  = std::atoi (argv[3]);
        height = std::atoi (argv[4]);
        first  = 5;
    }

    // UI state is held back: it lives on the panel, which does not exist until
    // the editor does.
    std::vector<std::pair<juce::String, juce::String>> uiState;
    std::optional<float> signalDb;

    // `theme=` is read in its own pass, before anything else, because
    // `appearance=` loads the theme as part of choosing the palette. Left in
    // argument order, "appearance=dark theme=candidate.json" would read the
    // machine's theme and only pick the candidate up on the editor's next
    // poll, while "theme=candidate.json appearance=dark" worked -- an ordering
    // that happens to matter is exactly the kind of thing nobody discovers
    // until a render is quietly wrong.
    for (int i = first; i < argc; ++i)
    {
        const juce::String arg { argv[i] };

        if (! arg.startsWith ("theme="))
            continue;

        const auto path = arg.substring (6);
        const juce::File file { juce::File::getCurrentWorkingDirectory().getChildFile (path) };

        // Fatal for the same reason an unknown ui. key is: a theme that did not
        // load renders the built-in palette, which looks like a perfectly good
        // render of the wrong thing.
        if (! file.existsAsFile())
        {
            std::cerr << "theme file not found: " << file.getFullPathName() << '\n';
            return 2;
        }

        bmo::ui::overrideThemeFile (file);
    }

    for (int i = first; i < argc; ++i)
    {
        const juce::String arg { argv[i] };
        const auto split = arg.indexOfChar ('=');

        if (split < 0)
            continue;

        const auto key = arg.substring (0, split);
        const auto value = arg.substring (split + 1);

        // Appearance is not a panel's state and not a parameter: it is the
        // whole palette. Set for this process only -- rendering the dark set
        // must not flip every plugin open on the machine, which is what
        // ui::setDarkMode would do.
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

        if (key == "theme")
            continue;               // taken in the pass above

        if (key.startsWith ("ui."))
        {
            uiState.emplace_back (key.substring (3), value);
            continue;
        }

        // Not a parameter either: how loud a tone to run through the processor
        // so the meters have something to read. See the capture loop below.
        if (key == "signal")
        {
            signalDb = value.getFloatValue();
            continue;
        }

        // Already applied, above prepareToPlay. Swallowed here so it does not
        // come back as "unknown parameter", which is fatal by design.
        if (key == "rate")
            continue;

        if (! set (*processor, key, value))
            std::cerr << "unknown parameter: " << key << '\n';
    }

    std::unique_ptr<juce::AudioProcessorEditor> editor (processor->createEditorAndMakeActive());

    if (editor == nullptr)
    {
        std::cerr << "no editor\n";
        return 1;
    }

    // Fatal rather than a warning, unlike an unknown parameter above. A render
    // that silently ignored the mode it was asked for would be a picture of
    // the wrong thing, and nothing downstream could tell it from the right one.
    for (const auto& [key, value] : uiState)
        if (! setUiState (*editor, key, value))
        {
            std::cerr << "no panel here takes ui." << key << "=" << value << '\n';
            processor->editorBeingDeleted (editor.get());
            editor.reset();
            return 2;
        }

    if (width > 0 && height > 0)
        editor->setSize (width, height);

    // Parts of the panel refresh on timers, so let those fire before capturing.
    //
    // A meter refreshes on a timer *from a source the audio thread feeds*, so
    // for a metering panel that is only half of it: with no audio ever
    // processed every meter in the suite renders at rest, and a render at rest
    // cannot show what a meter does. BMO Vcomp's GR bar is the case that
    // forced this -- it grows leftward from zero, which is a decision worth
    // reviewing and is invisible in an empty well.
    //
    // `signal=<dBFS>` runs a 1 kHz tone through the processor between the
    // timer ticks, so the meters are reading something real by the time the
    // snapshot is taken. Steady rather than programme-like on purpose: a still
    // image of a moving meter is a picture of one arbitrary instant, and a
    // tone at least makes that instant reproducible.
    //
    // "Reproducible" took a second pass to earn, and this is the part to
    // understand before shortening the loop again. A steady tone fixes the
    // *level* a meter is fed; it does not fix where the needle has got to on
    // its way there. `DynamicsMeter::timerCallback` integrates per tick --
    //
    //     displayed += 0.28f * (level - displayed)
    //
    // -- at 30 Hz, so after n ticks it is at 1 - 0.72^n of the truth. The loop
    // below settles it by sleeping, and `Thread::sleep` is a floor rather than
    // a period: when the scheduler overran, one extra 33.3 ms tick fired and
    // the needle landed 2% further along. Ten renders of BMO Opto at a fixed
    // `signal=-18` came back as *three* distinct images, on AURORA, 2026-09-14.
    //
    // Only Opto showed it, because a bar meter quantises to a whole pixel and
    // swallows 2% while a needle's angle is continuous and drawn anti-aliased.
    // That is the trap: the fault was suite-wide and visible on one panel.
    //
    // It matters because `tools/inspect hash` is how this project settles
    // whether a refactor moved anything -- "byte-identical or it was not a
    // refactor" -- and that test was unsound for the one panel in the suite
    // with a needle, in both directions: a spurious difference on a pure
    // refactor, and a real one-pixel regression dismissible as the known
    // flakiness.
    //
    // So the settle now runs long enough to *converge* rather than long enough
    // to look settled. At 24 ticks the residual is 0.72^24 = 2.4e-4 of full
    // scale, which moves the needle tip by well under a tenth of a pixel, so
    // whether a 25th fires cannot change the render. Renders without a signal
    // were always deterministic -- every meter sits at rest -- and stay on the
    // short loop, which is what keeps a colour render cheap.
    //
    // That was not the whole of it, and the premise is why. Convergence
    // assumes the meter is fed a constant, and it is not: `Meter` publishes the
    // RMS of the *last block*, and 512 samples of 1 kHz at 48 k is 10.67
    // cycles, so every block's RMS differs a little with where the tone's
    // phase fell. The needle does not settle on a value, it tracks a sequence,
    // and where it ends depends on exactly which ticks read which blocks. The
    // DEQ session found `snapshot opto` alternating between two images at
    // signal=-18 on 2026-09-15; eight renders here on 2026-09-17 gave the
    // baseline six times and a third image twice, AURORA.
    //
    // So the block now holds a whole number of cycles, which makes every
    // block's RMS the same number and gives the convergence argument above the
    // constant it assumed. 480 samples at 48 k and 441 at 44.1 k are ten
    // cycles; 96 k takes five. Ticking the timers by hand instead was tried and
    // does not work from here: the meters inherit juce::Timer privately.
    //
    // And 24 ticks was too few for a *hash*, whatever it does for the eye.
    // One extra tick at 0.72^24 moves the needle tip about 0.02 px, which no
    // one can see and which changes anti-aliased bytes all the same. 64 leaves
    // 0.72^64 = 7e-10 of the swing, where an extra tick rounds away. It costs
    // about 2.6 s a metered render; unmetered ones keep the 8-tick loop.
    const auto settleTicks = signalDb.has_value() ? 64 : 8;

    const auto blockSize = [rate]
    {
        for (int cycles = 20; cycles > 0; --cycles)
        {
            const auto samples = cycles * rate / 1000.0;

            if (samples <= 512.0 && samples == std::floor (samples))
                return (int) samples;
        }

        return 512;
    }();

    juce::AudioBuffer<float> block (juce::jmax (2, processor->getTotalNumOutputChannels()), blockSize);
    juce::MidiBuffer midi;
    double phase = 0.0;

    // A constant tone is still not a constant reading from a compressor: an
    // opto cell is still moving 240 ms in, which is all the settle loop lasts,
    // so gain reduction was a ramp and the render caught it wherever the
    // ticks happened to leave it -- BMO Opto at crush=60 came back as two
    // images in twelve. The DSP gets ten seconds of the same tone first, with
    // no timers and no sleeping, so the loop below meters a steady state.
    const auto fillTone = [&]
    {
        const auto amplitude = juce::Decibels::decibelsToGain (*signalDb);

        for (int n = 0; n < block.getNumSamples(); ++n)
        {
            const auto v = (float) (amplitude * std::sin (phase));
            // The tone is 1 kHz at whatever rate the processor was
            // prepared at, not at a hardcoded 48 k -- otherwise `rate=`
            // silently moves the test tone as well as the rate, and a
            // meter reading would be answering a different question from
            // the one asked.
            phase += 2.0 * juce::MathConstants<double>::pi * 1000.0 / rate;

            for (int ch = 0; ch < block.getNumChannels(); ++ch)
                block.setSample (ch, n, v);
        }
    };

    if (signalDb.has_value())
    {
        const auto prerollBlocks = (int) std::ceil (10.0 * rate / blockSize);

        for (int i = 0; i < prerollBlocks; ++i)
        {
            fillTone();
            processor->processBlock (block, midi);
        }
    }

    for (int i = 0; i < settleTicks; ++i)
    {
        if (signalDb.has_value())
        {
            fillTone();
            processor->processBlock (block, midi);
        }

        juce::Thread::sleep (40);
        juce::Timer::callPendingTimersSynchronously();
    }

    const auto image = editor->createComponentSnapshot (editor->getLocalBounds(), false, 2.0f);

    juce::PNGImageFormat png;
    std::unique_ptr<juce::FileOutputStream> stream (out.createOutputStream());

    if (stream == nullptr || ! png.writeImageToStream (image, *stream))
    {
        std::cerr << "could not write " << out.getFullPathName() << '\n';
        return 1;
    }

    std::cout << "wrote " << out.getFullPathName()
              << " (" << image.getWidth() << "x" << image.getHeight() << ")\n";

    // The editor goes before its processor.
    processor->editorBeingDeleted (editor.get());
    editor.reset();
    return 0;
}
