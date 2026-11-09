// rs-t3k: TONE3000 from the command line, through the same library the
// plugin uses (tone3000/, rs::t3k). For development and checking the API.
//
//   rs-t3k login                   device-code sign-in (saves the session)
//   rs-t3k user                    who's signed in
//   rs-t3k get <path?query>        any GET under /api/v1, printed as JSON
//   rs-t3k list <which>            favorited|created|downloaded|trending|latest
//   rs-t3k models <toneId>         the models RoughStomp would offer
//   rs-t3k download <toneId> <modelId>   into the downloads folder, as the plugin does
//   rs-t3k browse [nam|ir] [toneId]      TONE3000's picker (load_tone with a tone id)
//   rs-t3k logout
//   rs-t3k expire                  mark the session expired (to test renewing it)
//
// The session lives where the plugin keeps it:
// ~/Library/Application Support/RoughStomp/TONE3000/session.json

#include <rs_t3k/Config.h>
#include <rs_t3k/Service.h>

#include <iostream>

using namespace rs::t3k;

namespace
{
std::ostream& operator<< (std::ostream& o, const juce::String& s) { return o << s.toStdString(); }

/** Runs the message loop until done() is called (the Service answers there). */
struct Wait
{
    bool finished = false;
    int result = 0;
    void done (int r) { result = r; finished = true; }
    int run()
    {
        while (! finished)
            juce::MessageManager::getInstance()->runDispatchLoopUntil (50);
        return result;
    }
};

void printTone (const Tone& t)
{
    std::cout << t.id << "  [" << formatName (t.format) << ", " << gearName (t.gear) << "]  " << t.title
              << "  by " << t.creator.name() << "  (A1 " << t.a1Models << ", A2 " << t.a2Models << ", IR " << t.irs << ")\n";
}
} // namespace

int main (int argc, char* argv[])
{
    juce::ScopedJuceInitialiser_GUI juceInit;   // the Service answers on the message thread
    juce::StringArray args;
    for (int i = 1; i < argc; ++i)
        args.add (argv[i]);
    const auto cmd = args[0];

    Service service;
    if (! service.isAvailable())
    {
        std::cout << "No client ID: configure with -DROUGHSTOMP_T3K_CLIENT_ID=... or set T3K_CLIENT_ID\n";
        return 1;
    }

    Wait wait;
    if (cmd == "login")
    {
        service.startSignIn ([] (const Api::DeviceCode& c)
                             {
                                 std::cout << "Code: " << c.userCode << "\nEnter it at " << c.verificationUri << std::endl;
                                 juce::URL (c.verificationUriComplete).launchInDefaultBrowser();
                             },
                             [&] (bool ok, const juce::String& error)
                             {
                                 std::cout << (ok ? juce::String ("Signed in.") : error) << "\n";
                                 wait.done (ok ? 0 : 1);
                             });
        return wait.run();
    }
    if (cmd == "expire")
    {
        TokenStore store (config::sessionFile());
        auto t = store.load();
        t.expiresAtMs = 0;
        store.save (t);
        std::cout << "Marked expired.\n";
        return 0;
    }
    if (cmd == "logout")
    {
        service.signOut();
        std::cout << "Signed out.\n";
        return 0;
    }
    if (cmd == "user")
    {
        service.user ([&] (bool ok, const User& u, const juce::String& error)
                      {
                          std::cout << (ok ? u.name() + " (" + u.url + ")" : error) << "\n";
                          wait.done (ok ? 0 : 1);
                      });
        return wait.run();
    }
    if (cmd == "list" && args.size() > 1)
    {
        service.tones (args[1], 1, [&] (bool ok, const Page<Tone>& p, const juce::String& error)
                       {
                           if (! ok)
                               std::cout << error << "\n";
                           for (const auto& t : p.items)
                               printTone (t);
                           std::cout << "page " << p.page << " of " << p.totalPages << ", " << p.total << " tones\n";
                           wait.done (ok ? 0 : 1);
                       });
        return wait.run();
    }
    if (cmd == "models" && args.size() > 1)
    {
        service.models (args[1].getIntValue(), [&] (bool ok, const std::vector<Model>& models, const juce::String& error)
                        {
                            if (! ok)
                                std::cout << error << "\n";
                            for (const auto& m : models)
                                std::cout << m.id << "  arch " << (m.architecture.isEmpty() ? juce::String ("-") : m.architecture)
                                          << "  " << m.name << "\n";
                            wait.done (ok ? 0 : 1);
                        });
        return wait.run();
    }
    if (cmd == "download" && args.size() > 2)
    {
        service.tone (args[1].getIntValue(), [&] (bool ok, const Tone& tone, const juce::String& error)
        {
            if (! ok)
            {
                std::cout << error << " (HTTP " << service.lastStatus() << ")\n";
                return wait.done (1);
            }
            service.model (args[2].getIntValue(), [&, tone] (bool okModel, const Model& m, const juce::String& e2)
            {
                if (! okModel)
                {
                    std::cout << e2 << "\n";
                    return wait.done (1);
                }
                service.download (tone, m, [&] (const juce::File& f, const juce::String& e3)
                {
                    std::cout << (f.existsAsFile() ? f.getFullPathName() + " (" + juce::String (f.getSize()) + " bytes)" : e3) << "\n";
                    wait.done (f.existsAsFile() ? 0 : 1);
                });
            });
        });
        return wait.run();
    }
    if (cmd == "browse")
    {
        service.browse (args.size() > 1 ? args[1] : juce::String ("nam"), args.size() > 2 ? args[2].getIntValue() : 0,
                        [&] (int toneId, const juce::String& error)
                        {
                            std::cout << (error.isNotEmpty() ? error : "tone_id " + juce::String (toneId)) << "\n";
                            wait.done (error.isEmpty() ? 0 : 1);
                        });
        return wait.run();
    }
    if (cmd == "get" && args.size() > 1)
    {
        Api api (config::clientId(), std::make_shared<TokenStore> (config::sessionFile()), std::make_shared<JuceHttp>());
        const auto r = api.get (args[1].startsWith ("/") ? args[1] : "/" + args[1]);
        std::cout << "HTTP " << r.status << "\n" << juce::JSON::toString (r.json()) << "\n";
        return r.ok() ? 0 : 1;
    }

    std::cout << "Usage: rs-t3k login | user | get <path> | list <which> | models <toneId> | "
                 "download <toneId> <modelId> | browse [nam|ir] [toneId] | logout\n";
    return 1;
}
