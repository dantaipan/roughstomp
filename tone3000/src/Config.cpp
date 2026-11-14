#include "rs_t3k/Config.h"

#include <mutex>

namespace rs::t3k::config
{
namespace
{
juce::File applicationSupport()
{
    auto base = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory);
   #if JUCE_MAC
    base = base.getChildFile ("Application Support");   // JUCE's "app data" on macOS is ~/Library itself
   #endif
    return base;
}
} // namespace

void moveIntoSharedFolder (const juce::File& old, const juce::File& shared)
{
    if (! old.isDirectory())
        return;

    juce::InterProcessLock lock (sessionLockName());
    const juce::InterProcessLock::ScopedLockType locked (lock);
    if (! shared.createDirectory())
        return;

    const auto oldSession = old.getChildFile ("session.json");
    const auto sharedSession = shared.getChildFile ("session.json");
    if (oldSession.existsAsFile())
    {
        if (sharedSession.existsAsFile())
            oldSession.deleteFile();
        else
            oldSession.moveFileTo (sharedSession);
    }

    const auto oldDownloads = old.getChildFile ("Downloads");
    const auto sharedDownloads = shared.getChildFile ("Downloads");
    if (! oldDownloads.isDirectory() || oldDownloads.isSymbolicLink() || ! sharedDownloads.createDirectory())
        return;

    // The whole list first: moving while iterating would skip files
    juce::Array<juce::File> files;
    for (const auto& entry : juce::RangedDirectoryIterator (oldDownloads, true, "*", juce::File::findFiles))
        files.add (entry.getFile());

    bool allThere = true;
    for (const auto& from : files)
    {
        const auto to = sharedDownloads.getChildFile (from.getRelativePathFrom (oldDownloads));
        if (to.exists())
            continue;   // the other app has it already
        allThere = to.getParentDirectory().createDirectory() && from.moveFileTo (to) && allThere;
    }

    // Anything that couldn't move stays where saved paths expect it
    if (allThere && oldDownloads.deleteRecursively())
        sharedDownloads.createSymbolicLink (oldDownloads, false);
}

juce::String baseUrl() { return "https://www.tone3000.com/api/v1"; }

juce::String clientId()
{
    const auto env = juce::SystemStats::getEnvironmentVariable ("T3K_CLIENT_ID", {});
    return env.isNotEmpty() ? env : juce::String (ROUGHSTOMP_T3K_CLIENT_ID);
}

juce::String sessionLockName() { return "RoughStompTONE3000Session"; }

juce::File folder()
{
    const auto shared = applicationSupport().getChildFile ("TONE3000 Shared");

    // Once per process, on first use: that's when the session store is made,
    // before anything holds the session's lock. (JUCE's lock is a POSIX record
    // lock, which belongs to the whole process: taking it a second time here
    // while held elsewhere in the process would release it when done.)
    static std::once_flag moved;
    std::call_once (moved, [&] { moveIntoSharedFolder (applicationSupport().getChildFile ("RoughStomp").getChildFile ("TONE3000"), shared); });
    return shared;
}

juce::File sessionFile()     { return folder().getChildFile ("session.json"); }
juce::File downloadsFolder() { return folder().getChildFile ("Downloads"); }
} // namespace rs::t3k::config
