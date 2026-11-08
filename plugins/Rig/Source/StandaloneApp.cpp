// RoughStomp's standalone app: JUCE's own standalone application (see
// juce_audio_plugin_client_Standalone.cpp), with the same settings file so
// earlier settings carry over, and one difference. JUCE starts with the input
// muted "to avoid a feedback loop"; a guitar rig should start ready to play, so
// on first launch the input is unmuted unless it's a microphone (the Mac's
// built-in one could feed back through the speakers). After that the setting
// in Options is remembered as usual.

#include "rs_ui/Family.h"   // first: JUCE's wrapper headers below leave namespace juce open

// The same headers, in the same order, as JUCE's own standalone wrapper
#include <juce_core/system/juce_TargetPlatform.h>
#include <juce_audio_plugin_client/detail/juce_CheckSettingMacros.h>
#include <juce_audio_plugin_client/detail/juce_IncludeSystemHeaders.h>
#include <juce_audio_plugin_client/detail/juce_IncludeModuleHeaders.h>
#include <juce_audio_plugin_client/detail/juce_PluginUtilities.h>
#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_gui_extra/juce_gui_extra.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_audio_plugin_client/Standalone/juce_StandaloneFilterWindow.h>


namespace
{
class RigStandaloneApp final : public juce::JUCEApplication
{
public:
    RigStandaloneApp()
    {
        juce::PropertiesFile::Options options;
        options.applicationName     = juce::CharPointer_UTF8 (JucePlugin_Name);
        options.filenameSuffix      = ".settings";
        options.osxLibrarySubFolder = "Application Support";
        options.folderName          = "";
        appProperties.setStorageParameters (options);
    }

    const juce::String getApplicationName() override           { return juce::CharPointer_UTF8 (JucePlugin_Name); }
    const juce::String getApplicationVersion() override        { return JucePlugin_VersionString; }
    bool moreThanOneInstanceAllowed() override                 { return true; }
    void anotherInstanceStarted (const juce::String&) override {}

    void initialise (const juce::String&) override
    {
        auto* settings = appProperties.getUserSettings();
        const bool firstLaunch = ! settings->containsKey ("shouldMuteInput");

        mainWindow = std::make_unique<juce::StandaloneFilterWindow> (
            getApplicationName(), rs::ui::family::panel,
            std::make_unique<juce::StandalonePluginHolder> (settings, false, juce::String{}, nullptr,
                                                            juce::Array<juce::StandalonePluginHolder::PluginInOuts>{}, false));

        if (firstLaunch)
        {
            const auto input = mainWindow->getDeviceManager().getAudioDeviceSetup().inputDeviceName;
            mainWindow->pluginHolder->getMuteInputValue().setValue (input.containsIgnoreCase ("microphone"));
        }

        mainWindow->setVisible (true);
    }

    void shutdown() override
    {
        mainWindow = nullptr;
        appProperties.saveIfNeeded();
    }

    void systemRequestedQuit() override
    {
        if (mainWindow != nullptr)
            mainWindow->pluginHolder->savePluginState();

        if (juce::ModalComponentManager::getInstance()->cancelAllModalComponents())
        {
            juce::Timer::callAfterDelay (100, []
            {
                if (auto* app = juce::JUCEApplicationBase::getInstance())
                    app->systemRequestedQuit();
            });
        }
        else
            quit();
    }

private:
    juce::ApplicationProperties appProperties;
    std::unique_ptr<juce::StandaloneFilterWindow> mainWindow;
};
} // namespace

juce::JUCEApplicationBase* juce_CreateApplication();
juce::JUCEApplicationBase* juce_CreateApplication() { return new RigStandaloneApp(); }
