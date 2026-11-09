#include "rs_t3k/Config.h"

namespace rs::t3k::config
{
juce::String baseUrl() { return "https://www.tone3000.com/api/v1"; }

juce::String clientId()
{
    const auto env = juce::SystemStats::getEnvironmentVariable ("T3K_CLIENT_ID", {});
    return env.isNotEmpty() ? env : juce::String (ROUGHSTOMP_T3K_CLIENT_ID);
}

juce::File folder()
{
    auto base = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory);
   #if JUCE_MAC
    base = base.getChildFile ("Application Support");   // JUCE's "app data" on macOS is ~/Library itself
   #endif
    return base.getChildFile ("RoughStomp").getChildFile ("TONE3000");
}

juce::File sessionFile()     { return folder().getChildFile ("session.json"); }
juce::File downloadsFolder() { return folder().getChildFile ("Downloads"); }
} // namespace rs::t3k::config
