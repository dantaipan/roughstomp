#pragma once

#include "Api.h"

#include <juce_events/juce_events.h>
#include <juce_graphics/juce_graphics.h>

#include <map>

namespace rs::t3k
{
/** TONE3000 for the GUI: the Api's calls run on a background thread and
    answer on the message thread. One per process, shared by every plugin
    instance (use it through juce::SharedResourcePointer<Service>).

    Callbacks may arrive after whoever asked has gone: capture a
    SafePointer/WeakReference and check it. */
class Service
{
public:
    Service();
    ~Service();

    /** False when the build has no client ID. */
    bool isAvailable() const;
    bool isSignedIn() const;
    void signOut();

    //==========================================================================
    /** Device-code sign-in: onCode with the code to show (call again for a
        new code after it expires), then onDone (ok, or why not; cancelling
        gives neither). */
    void startSignIn (std::function<void (const Api::DeviceCode&)> onCode,
                      std::function<void (bool ok, const juce::String& error)> onDone);
    void cancelSignIn();

    /** TONE3000's own picker in the browser (prompt=select_tone, or
        load_tone with replaceToneId), coming back through a redirect to this
        machine. done gets the chosen tone (0: closed without choosing) or why
        it failed. It also signs in. */
    void browse (const juce::String& format, int replaceToneId,
                 std::function<void (int toneId, const juce::String& error)> done);
    void cancelBrowse();
    bool isBrowsing() const noexcept { return browsing.load(); }

    //==========================================================================
    template <typename T>
    using Reply = std::function<void (bool ok, const T&, const juce::String& error)>;

    void user (Reply<User>);
    /** favorited, created, downloaded, trending or latest. Kept for the
        session (fresh = ask again). */
    void tones (const juce::String& which, int page, Reply<Page<Tone>>, bool fresh = false, const juce::String& gear = {});
    void tone (int id, Reply<Tone>);
    void models (int toneId, Reply<std::vector<Model>>);
    void model (int id, Reply<Model>);

    /** Downloads the model (if it isn't already) into the downloads folder,
        then done with the file, or an empty file and why. */
    void download (const Tone&, const Model&, std::function<void (const juce::File&, const juce::String& error)> done);

    /** An earlier download of this model, if it's still there. */
    static juce::File findDownload (int modelId);
    static juce::File downloadFileFor (const Tone&, const Model&);

    /** An image (tone pictures, avatars), kept in memory; done with an
        invalid image if it couldn't be loaded. */
    void image (const juce::String& url, std::function<void (const juce::Image&)> done);

    /** Why the last call failed with this status: 403/404 mean the tone
        is private or gone; 0 means no connection. */
    static bool isUnavailable (int status) noexcept { return status == 403 || status == 404; }
    int lastStatus() const noexcept { return api->lastStatus(); }

private:
    void run (std::function<void()> job);

    std::shared_ptr<TokenStore> store;
    std::shared_ptr<Api> api;
    juce::ThreadPool pool { juce::ThreadPoolOptions{}.withThreadName ("TONE3000").withNumberOfThreads (2) };

    std::atomic<int> signInGeneration { 0 }, browseGeneration { 0 };
    std::atomic<bool> browsing { false };

    std::mutex cacheLock;
    std::map<juce::String, Page<Tone>> toneLists;
    std::map<juce::String, juce::Image> images;
};
} // namespace rs::t3k
