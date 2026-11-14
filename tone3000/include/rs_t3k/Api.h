#pragma once

#include "Http.h"
#include "Session.h"
#include "Types.h"

#include <functional>
#include <memory>

namespace rs::t3k
{
/** TONE3000's API, blocking (run it on a background thread). It signs
    requests with the stored tokens and renews them as needed: before they
    expire, and once more on a 401. */
class Api
{
public:
    using Clock = std::function<juce::int64()>;   // ms since 1970

    Api (juce::String clientId, std::shared_ptr<TokenStore>, std::shared_ptr<Http>, Clock = {});

    bool isSignedIn() const;
    void signOut();

    //==========================================================================
    // Device-code sign-in (RFC 8628)
    struct DeviceCode
    {
        juce::String deviceCode, userCode, verificationUri, verificationUriComplete;
        int expiresIn = 600, interval = 5;
    };
    bool startDeviceSignIn (DeviceCode&, juce::String& error);

    enum class Poll { pending, slowDown, signedIn, denied, expired, failed };
    /** One poll; on signedIn the tokens are saved. */
    Poll pollDeviceSignIn (const DeviceCode&, juce::String& error);

    /** The Select / Load Tone flows' code, for tokens (saved). */
    bool exchangeCode (const juce::String& code, const juce::String& verifier, const juce::String& redirectUri,
                       juce::String& error);

    //==========================================================================
    /** An authorised GET; path is under the base URL ("/user") or a full URL. */
    HttpResponse get (const juce::String& pathOrUrl);

    bool user (User&, juce::String& error);
    /** which: favorited, created, downloaded (paginated) or trending, latest.
        gear (e.g. "cab", "amp", "pedal") narrows every list but latest, which
        takes no filter; empty means everything. */
    bool tones (const juce::String& which, int page, int pageSize, Page<Tone>&, juce::String& error,
                const juce::String& gear = {});
    bool tone (int id, Tone&, juce::String& error);
    /** The models RoughStomp can load: A2 if the tone has any, else A1 and
        custom (what the API gives without an architecture); IRs as they are. */
    bool models (int toneId, std::vector<Model>&, juce::String& error);
    bool model (int id, Model&, juce::String& error);

    /** Downloads a model to file, through a temporary file renamed at the end,
        so a cut-off download never leaves a broken model behind. */
    bool download (const Model&, const juce::File&, juce::String& error, std::function<bool()> keepGoing = {});

    /** What failed last (HTTP status, 0 for no connection), for telling
        "not found" from "offline". */
    int lastStatus() const noexcept { return status.load(); }

private:
    /** Renews the tokens if they're about to expire (or if force, unless
        another process just did). False: signed out, or it couldn't. */
    bool renew (bool force);
    HttpResponse authorised (HttpRequest);
    HttpResponse postForm (const juce::String& path, const juce::StringPairArray&);
    static juce::String describe (const HttpResponse&);

    juce::String clientId;
    std::shared_ptr<TokenStore> store;
    std::shared_ptr<Http> http;
    Clock now;
    std::atomic<int> status { 0 };
    std::mutex renewLock;   // within this process (the store's lock is across processes)
};
} // namespace rs::t3k
