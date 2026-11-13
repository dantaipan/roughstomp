#include "ViewSettings.h"

ViewSettings::ViewSettings()
{
    juce::PropertiesFile::Options options;
    options.applicationName = "RoughStomp";
    options.filenameSuffix = "settings";
    options.folderName = "RoughStomp";
    options.osxLibrarySubFolder = "Application Support";
    file = std::make_unique<juce::PropertiesFile> (options);
    style = file->getValue ("view") == "sliders" ? Style::sliders : Style::pedals;
}

void ViewSettings::setStyle (Style s, bool remember)
{
    if (s == style)
        return;
    style = s;
    if (! remember)
    {
        sendChangeMessage();
        return;
    }
    // Another RoughStomp may have changed other settings since: reload, then save
    file->reload();
    file->setValue ("view", s == Style::sliders ? "sliders" : "pedals");
    file->saveIfNeeded();
    sendChangeMessage();
}
