#include "rs_t3k/Config.h"

#include <catch2/catch_test_macros.hpp>

using namespace rs::t3k;

namespace
{
/** RoughStomp's old folder and the shared one, in a temporary folder. */
struct Folders
{
    juce::TemporaryFile root;
    juce::File old = root.getFile().getChildFile ("RoughStomp").getChildFile ("TONE3000");
    juce::File shared = root.getFile().getChildFile ("TONE3000 Shared");

    Folders() { old.getChildFile ("Downloads").createDirectory(); }
    ~Folders() { root.getFile().deleteRecursively(); }

    static void write (const juce::File& f, const juce::String& text)
    {
        f.getParentDirectory().createDirectory();
        f.replaceWithText (text);
    }
};
} // namespace

TEST_CASE ("The old folder moves into the shared one, and its Downloads path still works", "[t3k][config]")
{
    Folders f;
    Folders::write (f.old.getChildFile ("session.json"), "old session");
    Folders::write (f.old.getChildFile ("Downloads/111/1 - A.nam"), "a");
    Folders::write (f.old.getChildFile ("Downloads/222/2 - B.wav"), "b");

    config::moveIntoSharedFolder (f.old, f.shared);

    CHECK (f.shared.getChildFile ("session.json").loadFileAsString() == "old session");
    CHECK_FALSE (f.old.getChildFile ("session.json").exists());   // moved, not copied
    CHECK (f.shared.getChildFile ("Downloads/111/1 - A.nam").loadFileAsString() == "a");
    CHECK (f.shared.getChildFile ("Downloads/222/2 - B.wav").loadFileAsString() == "b");

    // Saved sessions and presets point into the old folder
    CHECK (f.old.getChildFile ("Downloads").isSymbolicLink());
    CHECK (f.old.getChildFile ("Downloads/111/1 - A.nam").loadFileAsString() == "a");

    // Again: nothing left to do, nothing changes
    config::moveIntoSharedFolder (f.old, f.shared);
    CHECK (f.shared.getChildFile ("session.json").loadFileAsString() == "old session");
    CHECK (f.old.getChildFile ("Downloads/222/2 - B.wav").loadFileAsString() == "b");
}

TEST_CASE ("A session already in the shared folder is kept, and its downloads win", "[t3k][config]")
{
    Folders f;
    Folders::write (f.old.getChildFile ("session.json"), "old session");
    Folders::write (f.old.getChildFile ("Downloads/111/1 - A.nam"), "a");
    Folders::write (f.old.getChildFile ("Downloads/222/2 - B.wav"), "old b");
    Folders::write (f.shared.getChildFile ("session.json"), "shared session");
    Folders::write (f.shared.getChildFile ("Downloads/222/2 - B.wav"), "shared b");

    config::moveIntoSharedFolder (f.old, f.shared);

    CHECK (f.shared.getChildFile ("session.json").loadFileAsString() == "shared session");
    CHECK_FALSE (f.old.getChildFile ("session.json").exists());
    CHECK (f.shared.getChildFile ("Downloads/111/1 - A.nam").loadFileAsString() == "a");
    CHECK (f.shared.getChildFile ("Downloads/222/2 - B.wav").loadFileAsString() == "shared b");
    CHECK (f.old.getChildFile ("Downloads").isSymbolicLink());
}

TEST_CASE ("No old folder: nothing happens", "[t3k][config]")
{
    Folders f;
    f.old.getParentDirectory().deleteRecursively();
    config::moveIntoSharedFolder (f.old, f.shared);
    CHECK_FALSE (f.shared.exists());
}
