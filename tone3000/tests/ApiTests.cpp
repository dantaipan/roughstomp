#include "rs_t3k/Api.h"

#include <catch2/catch_test_macros.hpp>

using namespace rs::t3k;

namespace
{
/** Stands in for TONE3000: answers each request through handler, and
    remembers what was asked. */
struct FakeHttp final : public Http
{
    std::function<HttpResponse (const HttpRequest&)> handler;
    std::vector<HttpRequest> requests;

    HttpResponse fetch (const HttpRequest& r) override
    {
        requests.push_back (r);
        return handler (r);
    }

    HttpResponse download (const HttpRequest& r, const juce::File& file, std::function<bool()>) override
    {
        auto response = fetch (r);
        if (response.ok())
            file.replaceWithText (response.body);
        return response;
    }

    int count (const juce::String& urlPart) const
    {
        int n = 0;
        for (const auto& r : requests)
            n += r.url.contains (urlPart) ? 1 : 0;
        return n;
    }
};

HttpResponse json (int status, const juce::String& body) { return { status, body, {} }; }

juce::String tokenResponse (const juce::String& access, const juce::String& refresh)
{
    return R"({"access_token":")" + access + R"(","refresh_token":")" + refresh + R"(","expires_in":3600,"token_type":"bearer"})";
}

struct Fixture
{
    juce::TemporaryFile sessionFile { ".json" };
    std::shared_ptr<TokenStore> store = std::make_shared<TokenStore> (sessionFile.getFile());
    std::shared_ptr<FakeHttp> http = std::make_shared<FakeHttp>();
    juce::int64 nowMs = 1'000'000'000;
    Api api { "t3k_pub_test", store, http, [this] { return nowMs; } };

    void signIn (const juce::String& access, const juce::String& refresh, juce::int64 expiresInMs)
    {
        store->save ({ access, refresh, nowMs + expiresInMs });
    }
};
} // namespace

TEST_CASE ("A valid token is used as it is", "[t3k][api]")
{
    Fixture f;
    f.signIn ("A1", "R1", 30 * 60 * 1000);
    f.http->handler = [] (const HttpRequest& r) { return json (200, r.url.endsWith ("/user") ? R"({"username":"dan"})" : "{}"); };

    User u;
    juce::String error;
    REQUIRE (f.api.user (u, error));
    CHECK (u.username == "dan");
    REQUIRE (f.http->requests.size() == 1);
    CHECK (f.http->requests[0].headers["Authorization"] == "Bearer A1");
}

TEST_CASE ("Tokens are renewed shortly before they expire, and the rotated refresh token is kept", "[t3k][api]")
{
    Fixture f;
    f.signIn ("A1", "R1", 30 * 1000);   // inside the minute's margin
    f.http->handler = [] (const HttpRequest& r)
    {
        if (r.url.endsWith ("/oauth/token"))
        {
            CHECK (r.body.contains ("grant_type=refresh_token"));
            CHECK (r.body.contains ("refresh_token=R1"));
            return json (200, tokenResponse ("A2", "R2"));
        }
        CHECK (r.headers["Authorization"] == "Bearer A2");
        return json (200, R"({"username":"dan"})");
    };

    User u;
    juce::String error;
    REQUIRE (f.api.user (u, error));
    CHECK (f.http->count ("/oauth/token") == 1);
    const auto saved = f.store->load();
    CHECK (saved.access == "A2");
    CHECK (saved.refresh == "R2");
    CHECK (saved.expiresAtMs == f.nowMs + 3600 * 1000);
}

TEST_CASE ("A 401 renews once and retries once", "[t3k][api]")
{
    Fixture f;
    f.signIn ("A1", "R1", 30 * 60 * 1000);
    f.http->handler = [] (const HttpRequest& r)
    {
        if (r.url.endsWith ("/oauth/token"))
            return json (200, tokenResponse ("A2", "R2"));
        return r.headers["Authorization"] == "Bearer A2" ? json (200, R"({"username":"dan"})") : json (401, R"({"error":"unauthorized"})");
    };

    User u;
    juce::String error;
    REQUIRE (f.api.user (u, error));
    CHECK (f.http->count ("/oauth/token") == 1);
    CHECK (f.http->count ("/user") == 2);
}

TEST_CASE ("When another process has renewed already, its tokens are used instead of spending ours", "[t3k][api]")
{
    Fixture f;
    f.signIn ("A1", "R1", 30 * 60 * 1000);
    f.http->handler = [&f] (const HttpRequest& r)
    {
        if (r.url.endsWith ("/oauth/token"))
            return json (400, R"({"error":"invalid_grant"})");   // R1 is spent: must not be used
        if (r.headers["Authorization"] == "Bearer A1")
        {
            // Meanwhile, RoughStomp in another host renewed the session
            f.store->save ({ "B1", "S1", f.nowMs + 3600 * 1000 });
            return json (401, "{}");
        }
        return json (200, R"({"username":"dan"})");
    };

    User u;
    juce::String error;
    REQUIRE (f.api.user (u, error));
    CHECK (f.http->count ("/oauth/token") == 0);
    CHECK (f.store->load().refresh == "S1");
}

TEST_CASE ("Too many requests: waits as asked, then tries once more", "[t3k][api]")
{
    Fixture f;
    f.signIn ("A1", "R1", 30 * 60 * 1000);
    int calls = 0;
    f.http->handler = [&calls] (const HttpRequest&)
    {
        if (++calls == 1)
        {
            HttpResponse busy { 429, R"({"error":"rate_limited"})", {} };
            busy.headers.set ("Retry-After", "0");
            return busy;
        }
        return json (200, R"({"username":"dan"})");
    };

    User u;
    juce::String error;
    REQUIRE (f.api.user (u, error));
    CHECK (calls == 2);
}

TEST_CASE ("A refused refresh token signs out", "[t3k][api]")
{
    Fixture f;
    f.signIn ("A1", "R1", 0);
    f.http->handler = [] (const HttpRequest& r)
    {
        if (r.url.endsWith ("/oauth/token"))
            return json (400, R"({"error":"invalid_grant"})");
        return json (401, "{}");
    };

    User u;
    juce::String error;
    CHECK_FALSE (f.api.user (u, error));
    CHECK_FALSE (f.api.isSignedIn());
    CHECK (error.isNotEmpty());
}

TEST_CASE ("Signed out, nothing goes over the network", "[t3k][api]")
{
    Fixture f;
    f.http->handler = [] (const HttpRequest&) { return json (200, "{}"); };
    User u;
    juce::String error;
    CHECK_FALSE (f.api.user (u, error));
    CHECK (f.http->requests.empty());
}

TEST_CASE ("Models: A2 first, A1 when there's no A2", "[t3k][api]")
{
    Fixture f;
    f.signIn ("A1", "R1", 30 * 60 * 1000);
    f.http->handler = [] (const HttpRequest& r)
    {
        if (r.url.contains ("architecture=2"))
            return json (200, R"({"data":[],"page":1,"total":0,"total_pages":0})");
        return json (200, R"({"data":[{"id":5,"tone_id":3,"name":"Old","architecture_version":"1","model_url":"u"}],"page":1,"total":1,"total_pages":1})");
    };

    std::vector<Model> models;
    juce::String error;
    REQUIRE (f.api.models (3, models, error));
    REQUIRE (models.size() == 1);
    CHECK (models[0].architecture == "1");
    CHECK (f.http->count ("architecture=2") == 1);
    CHECK (f.http->count ("/models?") == 2);
}

TEST_CASE ("Device sign-in: the poll answers, and tokens saved when approved", "[t3k][api]")
{
    Fixture f;
    juce::String next = "authorization_pending";
    f.http->handler = [&next] (const HttpRequest& r)
    {
        if (r.url.endsWith ("/oauth/device_authorization"))
        {
            CHECK (r.body.contains ("client_id=t3k_pub_test"));
            CHECK_FALSE (r.body.contains ("redirect_uri"));   // the device flow refuses these
            return json (200, R"({"device_code":"D","user_code":"ABCD-EFGH","verification_uri":"https://www.tone3000.com/activate",)"
                              R"("verification_uri_complete":"https://www.tone3000.com/activate?user_code=ABCD-EFGH","expires_in":600,"interval":5})");
        }
        if (next == "ok")
            return json (200, tokenResponse ("A1", "R1"));
        return json (400, R"({"error":")" + next + R"("})");
    };

    Api::DeviceCode code;
    juce::String error;
    REQUIRE (f.api.startDeviceSignIn (code, error));
    CHECK (code.userCode == "ABCD-EFGH");
    CHECK (code.interval == 5);

    CHECK (f.api.pollDeviceSignIn (code, error) == Api::Poll::pending);
    next = "slow_down";
    CHECK (f.api.pollDeviceSignIn (code, error) == Api::Poll::slowDown);
    next = "access_denied";
    CHECK (f.api.pollDeviceSignIn (code, error) == Api::Poll::denied);
    next = "expired_token";
    CHECK (f.api.pollDeviceSignIn (code, error) == Api::Poll::expired);
    CHECK_FALSE (f.api.isSignedIn());

    next = "ok";
    CHECK (f.api.pollDeviceSignIn (code, error) == Api::Poll::signedIn);
    CHECK (f.store->load().refresh == "R1");
}

TEST_CASE ("Downloads land whole or not at all", "[t3k][api]")
{
    Fixture f;
    f.signIn ("A1", "R1", 30 * 60 * 1000);
    juce::TemporaryFile out (".nam");
    const auto target = out.getFile();
    Model m;
    m.id = 1;
    m.url = "https://www.tone3000.com/api/v1/models/1/download";

    f.http->handler = [] (const HttpRequest& r)
    {
        CHECK (r.headers["Authorization"] == "Bearer A1");
        return json (200, R"({"version":"0.7.0"})");
    };
    juce::String error;
    REQUIRE (f.api.download (m, target, error));
    CHECK (target.loadFileAsString() == R"({"version":"0.7.0"})");
    CHECK_FALSE (target.getSiblingFile (target.getFileName() + ".part").exists());

    target.deleteFile();
    f.http->handler = [] (const HttpRequest&) { return json (404, R"({"error":"Not found"})"); };
    CHECK_FALSE (f.api.download (m, target, error));
    CHECK_FALSE (target.exists());
    CHECK (f.api.lastStatus() == 404);

    // The connection drops part-way: nothing left behind, and it says why
    f.http->handler = [] (const HttpRequest&) { return HttpResponse {}; };
    CHECK_FALSE (f.api.download (m, target, error));
    CHECK_FALSE (target.exists());
    CHECK (error.contains ("internet connection"));
}
