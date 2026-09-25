#include "PresetBar.h"

namespace bmo::ui
{

namespace
{
    enum MenuId
    {
        saveAs = 1000,
        importPreset,
        exportPreset,
        revealFolder,
        deleteCurrent,
        toggleDark,
        surfaceSimple,
        surfaceHouse,
        surfaceBrushed,
        surfacePowder,
        factoryBase = 2000,
        userBase    = 3000
    };
}

//==============================================================================
PresetBar::PresetBar (PresetManager& p) : presets (p)
{
    for (auto* b : { &previous, &next, &name })
    {
        b->setTriggeredOnMouseDown (false);
        b->setColour (juce::ComboBox::outlineColourId, juce::Colours::transparentBlack);
        addAndMakeVisible (b);
    }

    previous.onClick = [this] { presets.step (-1); timerCallback(); };
    next    .onClick = [this] { presets.step ( 1); timerCallback(); };
    name    .onClick = [this] { showMenu(); };

    startTimerHz (6);
    timerCallback();
}

void PresetBar::timerCallback()
{
    auto text = presets.getCurrentName();

    if (presets.isEdited())
        text += " *";

    if (text == shown)
        return;

    shown = text;
    name.setButtonText (text);
}

//==============================================================================
void PresetBar::showMenu()
{
    juce::PopupMenu menu;
    menu.setLookAndFeel (&getLookAndFeel());

    const auto& factory = presets.getFactory();
    const auto users = presets.getUserNames();
    const auto current = presets.getCurrentName();

    juce::PopupMenu factoryMenu;
    for (int i = 0; i < (int) factory.size(); ++i)
        factoryMenu.addItem (factoryBase + i, factory[(size_t) i].name,
                             true, current == factory[(size_t) i].name);

    menu.addSubMenu ("Presets", factoryMenu);

    if (! users.isEmpty())
    {
        juce::PopupMenu userMenu;
        for (int i = 0; i < users.size(); ++i)
            userMenu.addItem (userBase + i, users[i], true, current == users[i]);

        menu.addSubMenu ("Yours", userMenu);
    }

    menu.addSeparator();
    menu.addItem (saveAs, "Save as...");
    menu.addItem (importPreset, "Import...");
    menu.addItem (exportPreset, "Export...");
    menu.addSeparator();
    menu.addItem (revealFolder, "Show preset folder");
    menu.addItem (deleteCurrent, "Delete \"" + current + "\"",
                  users.contains (current));

    // Appearance lives here rather than on a button of its own. There is no
    // room on a 160 px panel for a control that is used twice a year, and this
    // menu is already where everything that is about the plugin rather than
    // about the sound has ended up.
    menu.addSeparator();
    menu.addItem (toggleDark, "Dark mode", true, isDarkMode());

    // The surface sits with the appearance for the same reason: it is about
    // the plugin, not the sound, and it is machine-wide. Simple is today's
    // look and the default; Textured takes the line's own finish unless one
    // is picked for everything.
    {
        const auto textured = surface() == Surface::textured;
        const auto finish = finishChoice();

        juce::PopupMenu surfaceMenu;
        surfaceMenu.addItem (surfaceSimple, "Simple", true, ! textured);
        surfaceMenu.addSeparator();
        surfaceMenu.addItem (surfaceHouse,   "Textured", true, textured && finish == FinishChoice::house);
        surfaceMenu.addItem (surfaceBrushed, "Textured, brushed everywhere", true,
                             textured && finish == FinishChoice::brushed);
        surfaceMenu.addItem (surfacePowder,  "Textured, powder everywhere", true,
                             textured && finish == FinishChoice::powder);

        menu.addSubMenu ("Surface", surfaceMenu);
    }

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (name),
                        [this, users] (int choice)
    {
        if (choice == 0)
            return;

        if (choice >= userBase)          presets.loadUser (users[choice - userBase]);
        else if (choice >= factoryBase)  presets.loadFactory (choice - factoryBase);
        else switch (choice)
        {
            case saveAs:        promptForName();  break;
            case importPreset:  chooseImport();   break;
            case exportPreset:  chooseExport();   break;
            case revealFolder:  presets.directory().revealToUser(); break;
            case deleteCurrent: presets.deleteUser (presets.getCurrentName());
                                presets.loadFactory (0);
                                break;
            case toggleDark:    setDarkMode (! isDarkMode());
                                if (auto* top = getTopLevelComponent())
                                    top->repaint();
                                break;
            case surfaceSimple:
            case surfaceHouse:
            case surfaceBrushed:
            case surfacePowder:
                setSurface (choice == surfaceSimple ? Surface::simple : Surface::textured,
                            choice == surfaceBrushed ? FinishChoice::brushed
                          : choice == surfacePowder  ? FinishChoice::powder
                                                     : FinishChoice::house);
                if (auto* top = getTopLevelComponent())
                    top->repaint();
                break;
            default: break;
        }

        timerCallback();
    });
}

//==============================================================================
void PresetBar::promptForName()
{
    auto* window = new juce::AlertWindow ("Save preset",
                                          "Name this preset.",
                                          juce::MessageBoxIconType::NoIcon);

    window->addTextEditor ("name", presets.getCurrentName(), {});
    window->addButton ("Save",   1, juce::KeyPress (juce::KeyPress::returnKey));
    window->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));

    window->enterModalState (true, juce::ModalCallbackFunction::create (
        [this, window] (int result)
        {
            if (result == 1)
            {
                presets.saveUser (window->getTextEditorContents ("name"));
                timerCallback();
            }

            delete window;
        }), false);
}

void PresetBar::chooseImport()
{
    chooser = std::make_unique<juce::FileChooser> (
        "Import a preset", presets.directory(), "*" + presets.extension());

    chooser->launchAsync (juce::FileBrowserComponent::openMode
                            | juce::FileBrowserComponent::canSelectFiles,
                          [this] (const juce::FileChooser& fc)
    {
        if (fc.getResult() != juce::File {})
            presets.importFrom (fc.getResult());

        timerCallback();
    });
}

void PresetBar::chooseExport()
{
    chooser = std::make_unique<juce::FileChooser> (
        "Export this preset",
        presets.directory().getChildFile (presets.getCurrentName()
                                                 + presets.extension()),
        "*" + presets.extension());

    chooser->launchAsync (juce::FileBrowserComponent::saveMode
                            | juce::FileBrowserComponent::warnAboutOverwriting,
                          [this] (const juce::FileChooser& fc)
    {
        if (fc.getResult() != juce::File {})
            presets.exportTo (fc.getResult());
    });
}

//==============================================================================
void PresetBar::paint (juce::Graphics& g)
{
    const auto& t = tokens();

    g.setColour (t.plateEdge);
    g.fillRect (getLocalBounds());

    g.setColour (t.hairline.withAlpha (0.6f));
    g.drawHorizontalLine (getHeight() - 1, 0.0f, (float) getWidth());
}

void PresetBar::resized()
{
    auto area = getLocalBounds().reduced (6, 3);
    constexpr int arrow = 20;

    previous.setBounds (area.removeFromLeft (arrow));
    next    .setBounds (area.removeFromRight (arrow));
    name    .setBounds (area.reduced (3, 0));
}

} // namespace bmo::ui
