/*
    The real panel, laid out at design size with no display: that every
    parameter is on it, and that everything on it fits.

      - Every parameter has a control on the panel, and it is shown. (The
        hide rule that Classic and Hybrid needed went with HYBRID on
        2026-09-11; it is on branch archive/hybrid-studio.)
      - Every switch label fits its switch, measured the way the suite's look
        and feel draws it.
      - Every value a box can show fits the box: all seventeen keys, every
        scale and range, every listed reference and a custom one.
      - No two visible controls overlap, and all of them are on the panel.

    The rack's tests/plugin/*LayoutTests.cpp are the model.
*/

#include "products/tune/Product.h"
#include "modules/tune/panel/TunePanel.h"
#include "tests/dsp/tune/TestUtil.h"

#include <juce_gui_basics/juce_gui_basics.h>

using namespace bmo::tune;
using namespace bmo::tune::test;

namespace
{
    TunePanel* findPanel (juce::Component& root)
    {
        for (auto* child : root.getChildren())
        {
            if (auto* panel = dynamic_cast<TunePanel*> (child))
                return panel;

            if (auto* found = findPanel (*child))
                return found;
        }

        return nullptr;
    }

    std::string nameOf (int index) { return specs()[(size_t) index].id; }
}

int main()
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    auto processor = bmo::products::createTune();
    processor->prepareToPlay (48000.0, 512);
    auto& params = processor->getEngine().params();

    std::unique_ptr<juce::AudioProcessorEditor> editor (processor->createEditorAndMakeActive());
    auto* panel = editor != nullptr ? findPanel (*editor) : nullptr;
    check (panel != nullptr, "the editor has a TunePanel");

    if (panel == nullptr)
        return finish ("panel");

    check (panel->getWidth() == TunePanel::kDesignWidth
           && panel->getHeight() == bmo::ui::ModulePanel::kContentHeight,
           "the panel is laid out at 360 x 688");

    //== Every parameter is on the panel =======================================
    panel->syncNow();

    for (int i = 0; i < Index::count; ++i)
    {
        const auto controls = panel->controlsFor (i);
        check (! controls.empty(), nameOf (i) + " has a control on the panel");

        for (auto* c : controls)
            check (c->isVisible(), nameOf (i) + "'s " + c->getName().toStdString() + " is shown");
    }

    //== Everything fits ========================================================
    for (auto* s : panel->switches())
    {
        const auto overflow = s->getButtonText().isEmpty() ? 0.0f : bmo::ui::BmoLookAndFeel::toggleLabelOverflow (*s);
        check (overflow <= 0.0f, "the " + s->getName().toStdString() + " switch's label fits: " + std::to_string (overflow) + " px over");
    }

    auto worstBox = [&] (int index, int choices)
    {
        float worst = -1.0e9f;
        std::string worstText;

        for (int c = 0; c < choices; ++c)
        {
            params.setReal (index, (float) c);
            panel->syncNow();

            for (const auto& b : panel->boxTexts())
                if (b.overflow > worst) { worst = b.overflow; worstText = b.id.toStdString() + " \"" + b.text.toStdString() + "\""; }
        }

        params.setReal (index, 0.0f);
        report ("widest text for " + nameOf (index) + ", px to spare", -worst);
        check (worst <= 0.0f, "every value of " + nameOf (index) + " fits its box; worst is " + worstText);
    };

    worstBox (Index::key, kNumKeySpellings);
    worstBox (Index::scale, 3);
    worstBox (Index::range, 5);

    for (const auto hz : { 440.0f, 432.0f, 415.0f, 444.0f, 467.3f, 380.0f })
    {
        params.setReal (Index::refA, hz);
        panel->syncNow();

        for (const auto& b : panel->boxTexts())
            if (b.id == "Ref A")
                check (b.overflow <= 0.0f, "Ref A \"" + b.text.toStdString() + "\" fits its box");
    }

    params.setReal (Index::refA, 440.0f);

    {
        // Retune Speed's readout, over all 146 steps.
        float worst = -1.0e9f;
        std::string worstText;
        for (int step = 0; step < kNumRetuneSteps; ++step)
        {
            params.setReal (Index::retuneMs, (float) step);
            panel->syncNow();
            const auto r = panel->retuneReadout();
            if (r.overflow > worst) { worst = r.overflow; worstText = r.text.toStdString(); }
        }

        params.setReal (Index::retuneMs, 57.0f);
        panel->syncNow();
        check (panel->retuneReadout().text == "12 ms", "the panel shows Retune Speed in ms: step 57 reads \"12 ms\"");
        params.setReal (Index::retuneMs, 4.0f);
        panel->syncNow();
        check (panel->retuneReadout().text == "0.4 ms", "and step 4 reads \"0.4 ms\"");

        params.setReal (Index::retuneMs, 0.0f);
        report ("widest Retune Speed readout, px to spare", -worst);
        check (worst <= 0.0f, "every Retune Speed value fits its readout; widest is \"" + worstText + "\"");
    }

    //== Nothing overlaps, and all of it is on the panel ========================
    std::vector<juce::Component*> visible;
    for (int i = 0; i < Index::count; ++i)
        for (auto* c : panel->controlsFor (i))
            if (c->isVisible() && std::find (visible.begin(), visible.end(), c) == visible.end())
                visible.push_back (c);

    for (auto* c : visible)
        check (panel->getLocalBounds().contains (c->getBounds()), c->getName().toStdString() + " is inside the panel");

    for (size_t a = 0; a < visible.size(); ++a)
        for (size_t b = a + 1; b < visible.size(); ++b)
            check (! visible[a]->getBounds().intersects (visible[b]->getBounds()),
                   visible[a]->getName().toStdString() + " and " + visible[b]->getName().toStdString() + " do not overlap");

    //== A 0.1 session still opens ===============================================
    // Saved state as the first build wrote it, retired parameters and all:
    // the ones that remain come back, the retired ones are ignored.
    {
        juce::XmlElement old ("PARAMS");
        old.setAttribute ("stateVersion", kStateVersion);
        const std::pair<const char*, double> saved[] = {
            { "retune", 22.5 }, { "key", 15.0 }, { "scale", 2.0 }, { "engine", 1.0 }, { "range", 3.0 },
            { "vibrato", 40.0 }, { "flex", 10.0 }, { "glide", 80.0 }, { "formant", 1.0 },
            { "formant_shift", -200.0 }, { "latency", 1.0 }, { "ref_a", 442.0 }, { "note_d", 0.0 } };

        for (const auto& [id, value] : saved)
        {
            auto* e = old.createNewChildElement ("PARAM");
            e->setAttribute ("id", id);
            e->setAttribute ("value", value);
        }

        juce::MemoryBlock blob;
        juce::AudioProcessor::copyXmlToBinary (old, blob);
        processor->setStateInformation (blob.getData(), (int) blob.getSize());

        check (params.getReal (Index::key) == 15.0f
               && params.getReal (Index::scale) == 2.0f && params.getReal (Index::range) == 3.0f
               && params.getReal (Index::vibrato) == 40.0f && params.getReal (Index::flex) == 10.0f
               && params.getReal (Index::refA) == 442.0f && params.getReal (Index::noteD) == 0.0f,
               "a 0.1 session's state loads: every remaining value comes back, the retired six are ignored");

        // 0.1's retune 22.5 was a knob position (about 2.6 ms). It must not
        // come back as 22.5 of anything: Retune Speed stays at its default.
        check (params.getReal (Index::retuneMs) == 0.0f,
               "and 0.1's unitless retune is not read as milliseconds: Retune Speed is at 0.0 ms");
    }

    processor->editorBeingDeleted (editor.get());
    editor.reset();
    return finish ("panel");
}
