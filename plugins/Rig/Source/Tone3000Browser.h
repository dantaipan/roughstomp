#pragma once

#include "RigProcessor.h"

#include <rs_t3k/Service.h>

/** TONE3000's logos (BinaryData). */
namespace tone3000Logos
{
juce::Image full();      // the TONE3000 wordmark: seen first
juce::Image compact();   // the T3K mark: only after the full one
} // namespace tone3000Logos

/** Choosing a tone from TONE3000 for one slot (Amp, Power amp: NAM captures;
    Cab: IRs). Shown over the rig's board, as TONE3000's integration
    guidelines describe:

      splash (signed out) -> sign-in (a code to enter at tone3000.com)
      -> library: Favorites / Created / Downloaded / Trending / Latest,
         and Browse TONE3000 (its own picker, in the browser)
      -> a tone: its models, and Load

    Loading goes through RigProcessor::loadTone3000, the same loading as a
    local file. */
class Tone3000Browser final : public juce::Component
{
public:
    Tone3000Browser (RigProcessor&, rs::t3k::Service&);
    ~Tone3000Browser() override;

    /** Opens for a slot: the library (or the splash when signed out). */
    void openFor (rig::FileSlot);

    /** Opens on a tone (the one a slot came from), to switch models. */
    void openTone (rig::FileSlot, int toneId);

    /** Shows one of the lists (favorited, created, downloaded, trending, latest). */
    void showList (const juce::String& which);

    std::function<void()> onClose;

    void paint (juce::Graphics&) override;
    void resized() override;

    // The screens (defined in the .cpp)
    class Splash;
    class SignIn;
    class Library;
    class Detail;

private:
    enum class Screen { splash, signIn, library, detail };
    void show (Screen);
    void close();
    void signedIn();
    void browseCatalog();
    void showTone (int toneId);
    void refreshHeader();

    RigProcessor& rigProcessor;
    rs::t3k::Service& service;
    rig::FileSlot slot = rig::FileSlot::amp;
    juce::String format = "nam";   // what this slot loads: "nam" or "ir"
    Screen screen = Screen::splash;

    // The header (library and tone screens): the logo, who's signed in, and
    // the way to the whole catalog
    juce::Image logo, avatar;
    juce::String username;
    juce::TextButton browseButton { "Browse TONE3000" }, signOutButton { "Sign out" }, closeButton { "Close" };
    juce::Label status;   // browsing in the browser, errors

    std::unique_ptr<Splash> splash;
    std::unique_ptr<SignIn> signIn;
    std::unique_ptr<Library> library;
    std::unique_ptr<Detail> detail;

    juce::Rectangle<int> headerArea;
    bool headerVisible = false;

    JUCE_DECLARE_WEAK_REFERENCEABLE (Tone3000Browser)
};
