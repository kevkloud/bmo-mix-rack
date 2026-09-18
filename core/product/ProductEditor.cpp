#include "ProductEditor.h"

namespace bmo
{

ProductEditor::ProductEditor (SingleModuleProcessor& p)
    : juce::AudioProcessorEditor (&p), proc (p),
      header (p.getInfo().name, p.getModule().accent, p.getModule().lineOf()),
      presetBar (p.getPresets()),
      designWidth (p.getModule().widthFor (p.isExpanded()))
{
    ui::pollTheme();
    lookAndFeel.refreshColours();
    setLookAndFeel (&lookAndFeel);

    panel = proc.getModule().createPanel (proc.makeContext());

    if (proc.getModule().isExpandable())
        header.setExpandable ([this] { return proc.isExpanded(); },
                              [this]
                              {
                                  proc.setExpanded (! proc.isExpanded());
                                  applyView();
                              });

    plate.addAndMakeVisible (header);
    plate.addAndMakeVisible (presetBar);
    plate.addAndMakeVisible (*panel);
    addAndMakeVisible (plate);

    setResizable (true, true);
    applyView();

    // The theme file is watched, not loaded once: editing it with the plugin
    // open recolours the panel.
    startTimer (1000);
}

ProductEditor::~ProductEditor()
{
    setLookAndFeel (nullptr);
}

void ProductEditor::applyView()
{
    // The scale the user had, before the width changes under it. First time
    // through there is no window yet, and that is 1.
    const auto scale = getWidth() > 0 ? (float) getWidth() / (float) designWidth : 1.0f;

    designWidth = proc.getModule().widthFor (proc.isExpanded());

    plate.setBounds (0, 0, designWidth, kDesignHeight);
    header.setBounds (0, 0, designWidth, ui::ProductHeader::kHeight);
    presetBar.setBounds (0, ui::ProductHeader::kHeight, designWidth, 24);
    panel->setBounds (0, ui::ProductHeader::kHeight + 24, designWidth, ui::ModulePanel::kContentHeight);

    getConstrainer()->setFixedAspectRatio ((double) designWidth / (double) kDesignHeight);
    setResizeLimits (designWidth * 2 / 3, kDesignHeight * 2 / 3,
                     designWidth * 2,     kDesignHeight * 2);
    setSize (juce::roundToInt ((float) designWidth * scale), juce::roundToInt ((float) kDesignHeight * scale));
    resized();
    header.refreshExpand();
}

void ProductEditor::timerCallback()
{
    if (ui::pollTheme())
    {
        lookAndFeel.refreshColours();
        repaint();
    }

    // A session restored while the window is open can change the view without
    // the panel asking. Once a second is soon enough for that.
    if (proc.getModule().widthFor (proc.isExpanded()) != designWidth)
        applyView();
}

void ProductEditor::resized()
{
    // One uniform scale, so everything keeps its proportions.
    plate.setTransform (juce::AffineTransform::scale ((float) getWidth() / (float) designWidth));
}

} // namespace bmo
