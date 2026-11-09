#pragma once

#include <juce_core/juce_core.h>

namespace rs::t3k
{
/** Where things are, and who we are to TONE3000. */
namespace config
{
    /** The API's base URL (v1). */
    juce::String baseUrl();

    /** The publishable key (client ID) the build was configured with; the
        T3K_CLIENT_ID environment variable overrides it (for development).
        Empty means TONE3000 isn't available in this build. */
    juce::String clientId();

    /** ~/Library/Application Support/RoughStomp/TONE3000: the session file
        and the downloaded models live here, shared by every RoughStomp in
        every host (and by the standalone app). */
    juce::File folder();

    juce::File sessionFile();
    juce::File downloadsFolder();
} // namespace config
} // namespace rs::t3k
