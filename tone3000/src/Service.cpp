#include "rs_t3k/Service.h"

#include "rs_t3k/Config.h"
#include "rs_t3k/Pkce.h"

namespace rs::t3k
{
Service::Service()
    : store (std::make_shared<TokenStore> (config::sessionFile())),
      api (std::make_shared<Api> (config::clientId(), store, std::make_shared<JuceHttp>()))
{
}

Service::~Service()
{
    ++signInGeneration;
    ++browseGeneration;
    pool.removeAllJobs (true, 10000);
}

bool Service::isAvailable() const { return config::clientId().isNotEmpty(); }
bool Service::isSignedIn() const  { return api->isSignedIn(); }

void Service::signOut()
{
    ++signInGeneration;
    api->signOut();
    const std::scoped_lock lock (cacheLock);
    toneLists.clear();
}

void Service::run (std::function<void()> job)
{
    pool.addJob (std::move (job));
}

//==============================================================================
void Service::startSignIn (std::function<void (const Api::DeviceCode&)> onCode,
                           std::function<void (bool, const juce::String&)> onDone)
{
    const int generation = ++signInGeneration;
    auto apiRef = api;
    auto* gen = &signInGeneration;   // the Service outlives its jobs (its destructor waits for them)

    run ([apiRef, gen, generation, onCode, onDone]
    {
        auto finish = [&] (bool ok, const juce::String& error)
        {
            if (gen->load() == generation)
                juce::MessageManager::callAsync ([onDone, ok, error] { onDone (ok, error); });
        };

        Api::DeviceCode code;
        juce::String error;
        if (! apiRef->startDeviceSignIn (code, error))
            return finish (false, error);
        juce::MessageManager::callAsync ([onCode, code] { onCode (code); });

        int interval = code.interval;
        const auto deadline = juce::Time::currentTimeMillis() + code.expiresIn * 1000;
        while (juce::Time::currentTimeMillis() < deadline)
        {
            // Sleep in short steps so cancelling is quick
            for (int waited = 0; waited < interval * 1000; waited += 100)
            {
                if (gen->load() != generation)
                    return;
                juce::Thread::sleep (100);
            }
            switch (apiRef->pollDeviceSignIn (code, error))
            {
                case Api::Poll::pending:  break;
                case Api::Poll::slowDown: interval += 5; break;
                case Api::Poll::signedIn: return finish (true, {});
                case Api::Poll::denied:
                case Api::Poll::expired:
                case Api::Poll::failed:   return finish (false, error);
            }
        }
        finish (false, "The code expired.");
    });
}

void Service::cancelSignIn() { ++signInGeneration; }

//==============================================================================
void Service::browse (const juce::String& format, int replaceToneId,
                      std::function<void (int, const juce::String&)> done)
{
    const int generation = ++browseGeneration;
    browsing = true;
    auto apiRef = api;
    auto* gen = &browseGeneration;
    auto* flag = &browsing;

    run ([apiRef, gen, flag, generation, format, replaceToneId, done]
    {
        auto finish = [&] (int toneId, const juce::String& error)
        {
            if (gen->load() != generation)
                return;
            flag->store (false);
            juce::MessageManager::callAsync ([done, toneId, error] { done (toneId, error); });
        };

        // A one-off listener on this machine for TONE3000's redirect back
        juce::StreamingSocket listener;
        if (! listener.createListener (0, "127.0.0.1"))
            return finish (0, "Couldn't open a connection for TONE3000 to come back to "
                              "(the host may not allow it). Try the RoughStomp app.");
        const auto redirect = "http://127.0.0.1:" + juce::String (listener.getBoundPort()) + "/callback";

        const auto pkce = Pkce::create();
        const auto state = randomToken (16);
        auto url = juce::URL (config::baseUrl() + "/oauth/authorize")
                       .withParameter ("client_id", config::clientId())
                       .withParameter ("redirect_uri", redirect)
                       .withParameter ("response_type", "code")
                       .withParameter ("code_challenge", pkce.challenge)
                       .withParameter ("code_challenge_method", "S256")
                       .withParameter ("state", state)
                       .withParameter ("format", format)
                       .withParameter ("menubar", "true");
        if (replaceToneId > 0)
            url = url.withParameter ("prompt", "load_tone").withParameter ("tone_id", juce::String (replaceToneId));
        else
            url = url.withParameter ("prompt", "select_tone").withParameter ("preview", "true");
        if (format == "nam")
            url = url.withParameter ("architecture", "2");
        juce::MessageManager::callAsync ([url] { url.launchInDefaultBrowser(); });

        // Up to 5 minutes for the user to choose; anything that isn't the
        // callback (a browser's favicon request, say) is answered and ignored
        const auto deadline = juce::Time::currentTimeMillis() + 5 * 60 * 1000;
        while (juce::Time::currentTimeMillis() < deadline)
        {
            if (gen->load() != generation)
                return;
            if (listener.waitUntilReady (true, 250) != 1)
                continue;
            std::unique_ptr<juce::StreamingSocket> conn (listener.waitForNextConnection());
            if (conn == nullptr)
                continue;

            char buffer[8192] {};
            if (conn->waitUntilReady (true, 3000) != 1)
                continue;
            const int n = conn->read (buffer, sizeof (buffer) - 1, false);
            const auto requestLine = juce::String::fromUTF8 (buffer, juce::jmax (0, n)).upToFirstOccurrenceOf ("\r\n", false, false);
            const auto target = requestLine.fromFirstOccurrenceOf (" ", false, false).upToLastOccurrenceOf (" ", false, false);

            auto reply = [&conn] (const juce::String& status, const juce::String& message)
            {
                const auto page = "<!doctype html><html><head><meta charset='utf-8'><title>RoughStomp</title></head>"
                                  "<body style='font-family:-apple-system,sans-serif;background:#1e1f22;color:#ddd;"
                                  "display:flex;align-items:center;justify-content:center;height:90vh'><h2>"
                                  + message + "</h2></body></html>";
                const auto text = "HTTP/1.1 " + status + "\r\nContent-Type: text/html; charset=utf-8\r\nContent-Length: "
                                  + juce::String (page.getNumBytesAsUTF8()) + "\r\nConnection: close\r\n\r\n" + page;
                conn->write (text.toRawUTF8(), static_cast<int> (text.getNumBytesAsUTF8()));
            };

            if (! target.startsWith ("/callback"))
            {
                reply ("404 Not Found", "");
                continue;
            }

            const juce::URL got ("http://127.0.0.1" + target);
            juce::StringPairArray q;
            for (int i = 0; i < got.getParameterNames().size(); ++i)
                q.set (got.getParameterNames()[i], got.getParameterValues()[i]);

            if (q["state"] != state)
            {
                reply ("400 Bad Request", "That wasn't the request RoughStomp made. Try again from RoughStomp.");
                return finish (0, "TONE3000's reply didn't match (state). Try again.");
            }
            if (q["error"].isNotEmpty())
            {
                reply ("200 OK", "TONE3000 couldn't complete that. You can return to RoughStomp.");
                return finish (0, q["error_description"].isNotEmpty() ? q["error_description"] : q["error"]);
            }

            reply ("200 OK", "You can return to RoughStomp.");

            // Closed without choosing still signs in, if it got that far
            juce::String error;
            if (q["code"].isNotEmpty() && ! apiRef->exchangeCode (q["code"], pkce.verifier, redirect, error))
                return finish (0, error);
            return finish (q["canceled"] == "true" ? 0 : q["tone_id"].getIntValue(), {});
        }
        finish (0, "TONE3000 didn't come back within 5 minutes.");
    });
}

void Service::cancelBrowse()
{
    ++browseGeneration;
    browsing = false;
}

//==============================================================================
namespace
{
template <typename T, typename Call>
void ask (juce::ThreadPool& pool, std::shared_ptr<Api> apiRef, Service::Reply<T> reply, Call call)
{
    pool.addJob ([apiRef, reply, call]
    {
        T value {};
        juce::String error;
        const bool ok = call (*apiRef, value, error);
        juce::MessageManager::callAsync ([reply, ok, value, error] { reply (ok, value, error); });
    });
}
} // namespace

void Service::user (Reply<User> reply)
{
    ask<User> (pool, api, std::move (reply), [] (Api& a, User& u, juce::String& e) { return a.user (u, e); });
}

void Service::tones (const juce::String& which, int page, Reply<Page<Tone>> reply, bool fresh)
{
    const auto key = which + "/" + juce::String (page);
    {
        const std::scoped_lock lock (cacheLock);
        if (fresh)
            toneLists.erase (key);
        else if (const auto it = toneLists.find (key); it != toneLists.end())
        {
            auto cached = it->second;
            juce::MessageManager::callAsync ([reply, cached] { reply (true, cached, {}); });
            return;
        }
    }

    auto* lock = &cacheLock;
    auto* lists = &toneLists;
    ask<Page<Tone>> (pool, api, [reply, key, lock, lists] (bool ok, const Page<Tone>& p, const juce::String& e)
    {
        if (ok)
        {
            const std::scoped_lock l (*lock);
            (*lists)[key] = p;
        }
        reply (ok, p, e);
    },
    [which, page] (Api& a, Page<Tone>& p, juce::String& e) { return a.tones (which, page, 24, p, e); });
}

void Service::tone (int id, Reply<Tone> reply)
{
    ask<Tone> (pool, api, std::move (reply), [id] (Api& a, Tone& t, juce::String& e) { return a.tone (id, t, e); });
}

void Service::models (int toneId, Reply<std::vector<Model>> reply)
{
    ask<std::vector<Model>> (pool, api, std::move (reply),
                             [toneId] (Api& a, std::vector<Model>& m, juce::String& e) { return a.models (toneId, m, e); });
}

void Service::model (int id, Reply<Model> reply)
{
    ask<Model> (pool, api, std::move (reply), [id] (Api& a, Model& m, juce::String& e) { return a.model (id, m, e); });
}

//==============================================================================
juce::File Service::downloadFileFor (const Tone& tone, const Model& model)
{
    const auto ext = tone.isIr() || (model.architecture.isEmpty() && model.contentHash.startsWith ("ir")) ? ".wav" : ".nam";
    // "Clean | Room" -> "Clean - Room" (the file system won't take "|")
    auto name = (model.name.isNotEmpty() ? model.name : tone.title).replace ("|", "-");
    name = juce::File::createLegalFileName (name);
    while (name.contains ("  "))
        name = name.replace ("  ", " ");
    name = name.trim().substring (0, 120);
    return config::downloadsFolder()
        .getChildFile (juce::String (tone.id > 0 ? tone.id : model.toneId))
        .getChildFile (juce::String (model.id) + " - " + name + ext);
}

juce::File Service::findDownload (int modelId)
{
    if (modelId <= 0)
        return {};
    const auto prefix = juce::String (modelId) + " - ";
    for (const auto& entry : juce::RangedDirectoryIterator (config::downloadsFolder(), true, "*", juce::File::findFiles))
        if (entry.getFile().getFileName().startsWith (prefix) && ! entry.getFile().getFileName().endsWith (".part"))
            return entry.getFile();
    return {};
}

void Service::download (const Tone& tone, const Model& model,
                        std::function<void (const juce::File&, const juce::String&)> done)
{
    if (const auto existing = findDownload (model.id); existing.existsAsFile())
    {
        juce::MessageManager::callAsync ([done, existing] { done (existing, {}); });
        return;
    }

    const auto file = downloadFileFor (tone, model);
    auto apiRef = api;
    run ([apiRef, model, file, done]
    {
        juce::String error;
        const bool ok = apiRef->download (model, file, error);
        juce::MessageManager::callAsync ([done, ok, file, error] { done (ok ? file : juce::File(), error); });
    });
}

void Service::image (const juce::String& url, std::function<void (const juce::Image&)> done)
{
    if (url.isEmpty())
    {
        juce::MessageManager::callAsync ([done] { done ({}); });
        return;
    }
    {
        const std::scoped_lock lock (cacheLock);
        if (const auto it = images.find (url); it != images.end())
        {
            auto img = it->second;
            juce::MessageManager::callAsync ([done, img] { done (img); });
            return;
        }
    }

    auto* lock = &cacheLock;
    auto* cache = &images;
    run ([url, done, lock, cache]
    {
        juce::MemoryBlock data;
        juce::Image img;
        if (juce::URL (url).readEntireBinaryStream (data))
            img = juce::ImageFileFormat::loadFrom (data.getData(), data.getSize());
        {
            const std::scoped_lock l (*lock);
            (*cache)[url] = img;   // a failure too: don't keep asking
        }
        juce::MessageManager::callAsync ([done, img] { done (img); });
    });
}
} // namespace rs::t3k
