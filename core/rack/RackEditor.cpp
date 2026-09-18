#include "RackEditor.h"

namespace bmo
{

//==============================================================================
RackEditor::SlotBar::SlotBar (RackEditor& o, int s) : owner (o), slot (s)
{
    for (auto* b : { &name, &left, &right, &remove })
    {
        b->setColour (juce::ComboBox::outlineColourId, juce::Colours::transparentBlack);
        addAndMakeVisible (b);
    }

    if (auto* def = owner.proc.getModuleAt (slot))
    {
        name.setButtonText (def->name);

        // Only a module with two widths gets the switch. Unlike the buttons
        // below it does not destroy this bar -- the bar is resized with its
        // panel -- so it acts inside the click.
        if (def->isExpandable())
        {
            expand = std::make_unique<ui::ExpandButton> ([this] { return owner.proc.isSlotExpanded (slot); });
            expand->onClick = [this]
            {
                owner.toggleSlotView (slot);
                expand->refresh();
            };
            addAndMakeVisible (*expand);
        }
    }

    // Every one of these destroys this bar, so the work is deferred to the
    // next message rather than done inside the click.
    auto& rack = owner.proc;
    const auto s0 = slot;

    name  .onClick = [this] { showMenu(); };
    left  .onClick = [&rack, s0] { juce::MessageManager::callAsync ([&rack, s0] { rack.moveModule (s0, s0 - 1); }); };
    right .onClick = [&rack, s0] { juce::MessageManager::callAsync ([&rack, s0] { rack.moveModule (s0, s0 + 1); }); };
    remove.onClick = [&rack, s0] { juce::MessageManager::callAsync ([&rack, s0] { rack.removeModule (s0); }); };

    left .setEnabled (slot > 0);
    right.setEnabled (slot + 1 < owner.proc.getNumModules());
}

void RackEditor::SlotBar::showMenu()
{
    auto& rack = owner.proc;
    const auto s0 = slot;

    owner.showModuleMenu (name, [&rack, s0] (const ModuleDef& def)
    {
        juce::MessageManager::callAsync ([&rack, s0, &def] { rack.setModule (s0, def); });
    });
}

void RackEditor::SlotBar::paint (juce::Graphics& g)
{
    const auto& t = ui::tokens();
    g.fillAll (t.plateEdge);

    // The module's accent along the top, as its standalone header has.
    if (auto* def = owner.proc.getModuleAt (slot))
    {
        g.setColour (def->accent);
        g.fillRect (getLocalBounds().removeFromTop (3));
    }

    g.setColour (t.hairline);
    g.fillRect (0, getHeight() - 1, getWidth(), 1);
    g.fillRect (getWidth() - 1, 0, 1, getHeight());
}

void RackEditor::SlotBar::resized()
{
    auto area = getLocalBounds().withTrimmedTop (3).reduced (4, 2);
    constexpr int button = 18;

    left  .setBounds (area.removeFromLeft (button));
    remove.setBounds (area.removeFromRight (button));
    right .setBounds (area.removeFromRight (button));

    if (expand != nullptr)
        expand->setBounds (area.removeFromRight (button));

    name  .setBounds (area.reduced (2, 0));
}

//==============================================================================
RackEditor::AddStrip::AddStrip (RackEditor& o) : owner (o)
{
    add.setColour (juce::ComboBox::outlineColourId, juce::Colours::transparentBlack);
    addAndMakeVisible (add);

    add.onClick = [this]
    {
        auto& rack = owner.proc;
        owner.showModuleMenu (add, [&rack] (const ModuleDef& def)
        {
            juce::MessageManager::callAsync ([&rack, &def] { rack.addModule (def); });
        });
    };
}

void RackEditor::AddStrip::paint (juce::Graphics& g)
{
    g.fillAll (ui::tokens().well);
}

void RackEditor::AddStrip::resized()
{
    add.setBounds (getLocalBounds().withTrimmedTop (3).reduced (6).removeFromTop (kSlotBar - 6));
    add.setEnabled (owner.proc.getNumModules() < RackProcessor::kSlots);
}

//==============================================================================
RackEditor::RackEditor (RackProcessor& p)
    : juce::AudioProcessorEditor (&p), proc (p),
      header (p.getInfo().name, ui::tokens().accent, ui::bmoLine()),
      presetBar (p.getPresets()),
      addStrip (*this)
{
    ui::pollTheme();
    lookAndFeel.refreshColours();
    setLookAndFeel (&lookAndFeel);

    plate.addAndMakeVisible (header);
    plate.addAndMakeVisible (presetBar);
    plate.addAndMakeVisible (addStrip);
    addAndMakeVisible (plate);

    proc.addRackListener (this);
    setResizable (true, true);

    rebuildViews();
    setSize (designWidth(), kDesignHeight);

    startTimer (1000);
}

RackEditor::~RackEditor()
{
    proc.removeRackListener (this);
    setLookAndFeel (nullptr);
}

//==============================================================================
void RackEditor::rackChainWillChange()
{
    // Panels point at engines that are about to go.
    views.clear();
}

void RackEditor::rackChainChanged()
{
    // Keep whatever scale the user had and re-size the window to the new
    // chain at that scale.
    const auto scale = (float) getWidth() / (float) plate.getWidth();

    rebuildViews();
    refit (scale);
}

void RackEditor::refit (float scale)
{
    const auto w = designWidth();
    getConstrainer()->setFixedAspectRatio ((double) w / (double) kDesignHeight);
    setResizeLimits (w * 2 / 3, kDesignHeight * 2 / 3, w * 2, kDesignHeight * 2);
    setSize (juce::roundToInt ((float) w * scale), juce::roundToInt ((float) kDesignHeight * scale));
    resized();
}

int RackEditor::slotWidth (int slot) const
{
    return proc.getModuleAt (slot)->widthFor (proc.isSlotExpanded (slot));
}

void RackEditor::toggleSlotView (int slot)
{
    // Nothing is torn down: the panel is resized in place and lays itself out
    // again, the modules to its right move over, and the window keeps the
    // scale the user had.
    const auto scale = (float) getWidth() / (float) plate.getWidth();
    proc.setSlotExpanded (slot, ! proc.isSlotExpanded (slot));
    layoutPlate();
    refit (scale);
}

void RackEditor::rebuildViews()
{
    views.clear();
    seams.clear();

    for (int s = 0; s < proc.getNumModules(); ++s)
    {
        SlotView view;
        view.bar   = std::make_unique<SlotBar> (*this, s);
        view.panel = proc.getModuleAt (s)->createPanel (proc.makeContext (s));

        plate.addAndMakeVisible (*view.bar);
        plate.addAndMakeVisible (*view.panel);
        views.push_back (std::move (view));
    }

    // One per boundary, so none after the last module: the add strip is filled
    // with `well` and separates itself. Added after the panels so they draw on
    // top of them rather than under.
    for (int s = 1; s < (int) views.size(); ++s)
    {
        seams.push_back (std::make_unique<Seam>());
        plate.addAndMakeVisible (*seams.back());
    }

    layoutPlate();

    const auto w = designWidth();
    getConstrainer()->setFixedAspectRatio ((double) w / (double) kDesignHeight);
    setResizeLimits (w * 2 / 3, kDesignHeight * 2 / 3, w * 2, kDesignHeight * 2);
}

int RackEditor::designWidth() const
{
    int w = 0;

    for (int s = 0; s < proc.getNumModules(); ++s)
        w += slotWidth (s);

    return juce::jmax (kMinWidth, w + kAddStrip);
}

void RackEditor::layoutPlate()
{
    const auto w = designWidth();
    plate.setBounds (0, 0, w, kDesignHeight);

    auto top = juce::Rectangle<int> (0, 0, w, kHeader);
    header.setBounds (top);

    // The rack's preset strip sits inside the header, right of the name.
    presetBar.setBounds (top.removeFromRight (juce::jmin (220, w / 2)).withTrimmedTop (3).reduced (0, 1));

    int x = 0;

    for (int s = 0; s < (int) views.size(); ++s)
    {
        const auto width = slotWidth (s);

        views[(size_t) s].bar  ->setBounds (x, kHeader, width, kSlotBar);
        views[(size_t) s].panel->setBounds (x, kHeader + kSlotBar, width, ui::ModulePanel::kContentHeight);

        // The seam belongs to the boundary on this slot's left, so slot 0 has
        // none and seams[s - 1] is the one between s - 1 and s. It starts below
        // the slot bar, which already carries its own edge.
        if (s > 0)
            seams[(size_t) (s - 1)]->setBounds (x, kHeader + kSlotBar, 1,
                                                ui::ModulePanel::kContentHeight);

        x += width;
    }

    addStrip.setBounds (x, kHeader, w - x, kDesignHeight - kHeader);
}

void RackEditor::resized()
{
    plate.setTransform (juce::AffineTransform::scale ((float) getWidth() / (float) plate.getWidth()));
}

void RackEditor::timerCallback()
{
    if (ui::pollTheme())
    {
        lookAndFeel.refreshColours();
        repaint();
    }
}

//==============================================================================
void RackEditor::showModuleMenu (juce::Component& target, std::function<void (const ModuleDef&)> chosen)
{
    juce::PopupMenu menu;
    menu.setLookAndFeel (&getLookAndFeel());

    const auto& registry = proc.getRegistry();

    for (int i = 0; i < (int) registry.size(); ++i)
        menu.addItem (i + 1, registry[(size_t) i]->name);

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (target),
                        [this, chosen] (int choice)
    {
        if (choice > 0)
            chosen (*proc.getRegistry()[(size_t) (choice - 1)]);
    });
}

} // namespace bmo
