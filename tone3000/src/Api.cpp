#include "rs_t3k/Api.h"

#include "rs_t3k/Config.h"

namespace rs::t3k
{
Api::Api (juce::String id, std::shared_ptr<TokenStore> s, std::shared_ptr<Http> h, Clock c)
    : clientId (std::move (id)), store (std::move (s)), http (std::move (h)),
      now (c ? std::move (c) : Clock ([] { return juce::Time::currentTimeMillis(); }))
{
}

bool Api::isSignedIn() const { return store->load().signedIn(); }

void Api::signOut()
{
    const std::scoped_lock lock (renewLock);
    const juce::InterProcessLock::ScopedLockType processLock (store->lock());
    store->clear();
}

juce::String Api::describe (const HttpResponse& r)
{
    if (r.status == 0)
        return "Couldn't reach TONE3000. Check your internet connection.";
    const auto v = r.json();
    for (const auto* key : { "error_description", "message", "error" })
        if (v[key].isString() && v[key].toString().isNotEmpty())
            return v[key].toString() + " (" + juce::String (r.status) + ")";
    return "TONE3000 said " + juce::String (r.status) + ".";
}

HttpResponse Api::postForm (const juce::String& path, const juce::StringPairArray& fields)
{
    HttpRequest r;
    r.url = config::baseUrl() + path;
    r.post = true;
    r.body = formEncode (fields);
    r.headers.set ("Content-Type", "application/x-www-form-urlencoded");
    r.headers.set ("Accept", "application/json");
    auto response = http->fetch (r);
    status = response.status;
    return response;
}

//==============================================================================
bool Api::startDeviceSignIn (DeviceCode& code, juce::String& error)
{
    juce::StringPairArray f;
    f.set ("client_id", clientId);
    const auto r = postForm ("/oauth/device_authorization", f);
    if (! r.ok())
    {
        error = describe (r);
        return false;
    }
    const auto v = r.json();
    code.deviceCode = v["device_code"].toString();
    code.userCode = v["user_code"].toString();
    code.verificationUri = v["verification_uri"].toString();
    code.verificationUriComplete = v["verification_uri_complete"].toString();
    code.expiresIn = v.hasProperty ("expires_in") ? static_cast<int> (v["expires_in"]) : 600;
    code.interval = juce::jmax (1, v.hasProperty ("interval") ? static_cast<int> (v["interval"]) : 5);
    if (code.deviceCode.isEmpty() || code.userCode.isEmpty())
    {
        error = "TONE3000 didn't send a sign-in code.";
        return false;
    }
    return true;
}

Api::Poll Api::pollDeviceSignIn (const DeviceCode& code, juce::String& error)
{
    juce::StringPairArray f;
    f.set ("grant_type", "urn:ietf:params:oauth:grant-type:device_code");
    f.set ("device_code", code.deviceCode);
    f.set ("client_id", clientId);
    const auto r = postForm ("/oauth/token", f);
    if (r.ok())
    {
        const std::scoped_lock lock (renewLock);
        const juce::InterProcessLock::ScopedLockType processLock (store->lock());
        store->save (Tokens::fromResponse (r.json(), now()));
        return Poll::signedIn;
    }
    const auto e = r.json()["error"].toString();
    if (e == "authorization_pending") return Poll::pending;
    if (e == "slow_down")             return Poll::slowDown;
    if (e == "access_denied")         { error = "Sign-in was cancelled."; return Poll::denied; }
    if (e == "expired_token")         { error = "The code expired."; return Poll::expired; }
    if (r.status == 0)                return Poll::pending;   // a dropped connection: try again next time
    error = describe (r);
    return Poll::failed;
}

bool Api::exchangeCode (const juce::String& code, const juce::String& verifier, const juce::String& redirectUri,
                        juce::String& error)
{
    juce::StringPairArray f;
    f.set ("grant_type", "authorization_code");
    f.set ("code", code);
    f.set ("code_verifier", verifier);
    f.set ("redirect_uri", redirectUri);
    f.set ("client_id", clientId);
    const auto r = postForm ("/oauth/token", f);
    if (! r.ok())
    {
        error = describe (r);
        return false;
    }
    const std::scoped_lock lock (renewLock);
    const juce::InterProcessLock::ScopedLockType processLock (store->lock());
    store->save (Tokens::fromResponse (r.json(), now()));
    return true;
}

//==============================================================================
bool Api::renew (bool force)
{
    const std::scoped_lock lock (renewLock);
    const juce::InterProcessLock::ScopedLockType processLock (store->lock());

    // Read it again: another process may have renewed it since (and the
    // refresh token we had is then spent)
    const auto current = store->load();
    if (! current.signedIn())
        return false;
    if (! current.needsRefresh (now()) && ! force)
        return true;
    if (current.refresh.isEmpty())
        return false;

    juce::StringPairArray f;
    f.set ("grant_type", "refresh_token");
    f.set ("refresh_token", current.refresh);
    f.set ("client_id", clientId);
    const auto r = postForm ("/oauth/token", f);
    if (r.ok())
    {
        store->save (Tokens::fromResponse (r.json(), now(), current));
        return true;
    }
    if (r.json()["error"].toString() == "invalid_grant")
        store->clear();   // the session is over: sign in again
    return false;
}

HttpResponse Api::authorised (HttpRequest r)
{
    if (! store->load().signedIn())
        return { 401, R"({"error":"not signed in"})", {} };

    renew (false);
    auto used = store->load();
    r.headers.set ("Authorization", "Bearer " + used.access);
    r.headers.set ("Accept", "application/json");
    auto response = http->fetch (r);

    // Too many requests (100 a minute by default): wait as long as it asks
    // (within reason) and try once more
    if (response.status == 429)
    {
        const auto retryAfter = response.headers.getValue ("Retry-After", response.headers.getValue ("retry-after", "2"));
        juce::Thread::sleep (juce::jlimit (0, 10, retryAfter.getIntValue()) * 1000);
        response = http->fetch (r);
    }

    if (response.status == 401)
    {
        // Renew unless another process already has, then once more
        const auto latest = store->load();
        if (latest.access == used.access ? renew (true) : latest.signedIn())
        {
            r.headers.set ("Authorization", "Bearer " + store->load().access);
            response = http->fetch (r);
        }
    }
    status = response.status;
    return response;
}

HttpResponse Api::get (const juce::String& pathOrUrl)
{
    HttpRequest r;
    r.url = pathOrUrl.startsWith ("http") ? pathOrUrl : config::baseUrl() + pathOrUrl;
    return authorised (r);
}

bool Api::user (User& u, juce::String& error)
{
    const auto r = get ("/user");
    if (! r.ok()) { error = describe (r); return false; }
    u = User::fromJson (r.json());
    return true;
}

bool Api::tones (const juce::String& which, int page, int pageSize, Page<Tone>& out, juce::String& error)
{
    auto path = "/tones/" + which;
    if (which != "trending" && which != "latest")
        path << "?page=" << page << "&page_size=" << juce::jlimit (1, 100, pageSize);
    const auto r = get (path);
    if (! r.ok()) { error = describe (r); return false; }
    out = parseTones (r.json());
    return true;
}

bool Api::tone (int id, Tone& t, juce::String& error)
{
    const auto r = get ("/tones/" + juce::String (id));
    if (! r.ok()) { error = describe (r); return false; }
    t = Tone::fromJson (r.json());
    return true;
}

bool Api::models (int toneId, std::vector<Model>& out, juce::String& error)
{
    // A2 first: without an architecture the API leaves A2 out, and most
    // captures are A2 only. Then A1 and custom for tones that have no A2.
    // (IRs come back either way.)
    for (const auto* arch : { "&architecture=2", "" })
    {
        const auto r = get ("/models?tone_id=" + juce::String (toneId) + "&page_size=300" + arch);
        if (! r.ok()) { error = describe (r); return false; }
        out = parseModels (r.json()).items;
        if (! out.empty())
            return true;
    }
    return true;   // a tone with nothing we can load: an empty list
}

bool Api::model (int id, Model& m, juce::String& error)
{
    const auto r = get ("/models/" + juce::String (id));
    if (! r.ok()) { error = describe (r); return false; }
    m = Model::fromJson (r.json());
    return true;
}

bool Api::download (const Model& m, const juce::File& file, juce::String& error, std::function<bool()> keepGoing)
{
    if (m.url.isEmpty())
    {
        error = "This model has no download.";
        return false;
    }

    auto attempt = [&]
    {
        renew (false);
        HttpRequest r;
        r.url = m.url;
        r.headers.set ("Authorization", "Bearer " + store->load().access);
        return r;
    };

    const auto part = file.getSiblingFile (file.getFileName() + ".part");
    auto response = http->download (attempt(), part, keepGoing);
    if (response.status == 401 && renew (true))
        response = http->download (attempt(), part, keepGoing);
    status = response.status;

    if (! response.ok())
    {
        part.deleteFile();
        error = response.status == 0 && keepGoing && ! keepGoing() ? juce::String ("Cancelled.") : describe (response);
        return false;
    }
    if (! part.moveFileTo (file))
    {
        part.deleteFile();
        error = "Couldn't save " + file.getFullPathName();
        return false;
    }
    return true;
}
} // namespace rs::t3k
