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

    /** ~/Library/Application Support/TONE3000 Shared: the session file and
        the downloaded models, shared by every app that uses TONE3000 through
        this library (RoughStomp in any host, its standalone app, RoughTrack),
        so signing in once signs in everywhere. Named for neither app, since
        many people have only one. On first use it takes over what RoughStomp
        up to 0.6.0 kept in ~/Library/Application Support/RoughStomp/TONE3000. */
    juce::File folder();

    juce::File sessionFile();
    juce::File downloadsFolder();

    /** The inter-process lock that guards the session file. Every app must use
        this same name (kept from RoughStomp's first version). */
    juce::String sessionLockName();

    /** What folder() does on first use, from RoughStomp's old folder: moves
        the sign-in and downloads into the shared folder, so signing in once
        works in every app.
        - The session is moved, never copied: refresh tokens rotate, so two
          copies would sign one app out. If the shared folder has one already,
          that one is kept and the old one deleted.
        - Downloads are merged (files already there are kept), then the old
          Downloads folder becomes a link to the shared one, because saved
          sessions and presets hold full paths into it. If any file can't
          move, the old folder stays as it is.
        Takes the session's lock, so nothing renews the tokens mid-move; call
        it only while this process doesn't hold that lock. */
    void moveIntoSharedFolder (const juce::File& old, const juce::File& shared);
} // namespace config
} // namespace rs::t3k
