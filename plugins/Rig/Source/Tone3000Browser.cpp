#include "Tone3000Browser.h"

#include "Tone3000Assets.h"
#include "rs_ui/Family.h"

namespace
{
namespace fam = rs::ui::family;
using rs::t3k::Tone;
using rs::t3k::Model;

constexpr int pad = 16;
constexpr int headerHeight = 44;

juce::String slotName (rig::FileSlot s)
{
    return s == rig::FileSlot::amp ? "Amp" : s == rig::FileSlot::power ? "Power amp" : "Cab";
}

juce::String whatLoads (const juce::String& format)
{
    return format == "ir" ? "IRs" : "NAM captures";
}

juce::String licenceName (const juce::String& l)
{
    if (l.isEmpty())  return {};
    if (l == "t3k")   return "TONE3000 licence";
    return l.toUpperCase().replaceCharacter ('-', ' ');
}

/** Draws img filling area (cropped to keep its shape), or a placeholder. */
void drawPicture (juce::Graphics& g, const juce::Image& img, juce::Rectangle<float> area, float corner)
{
    juce::Graphics::ScopedSaveState save (g);
    juce::Path clip;
    clip.addRoundedRectangle (area, corner);
    g.reduceClipRegion (clip);
    if (! img.isValid())
    {
        g.setColour (fam::recess);
        g.fillRect (area);
        g.setColour (fam::dimText.withAlpha (0.5f));
        g.setFont (fam::font (11.0f));
        g.drawText ("TONE3000", area, juce::Justification::centred);
        return;
    }
    const float scale = juce::jmax (area.getWidth() / static_cast<float> (img.getWidth()),
                                    area.getHeight() / static_cast<float> (img.getHeight()));
    const auto w = static_cast<float> (img.getWidth()) * scale, h = static_cast<float> (img.getHeight()) * scale;
    g.drawImage (img, juce::Rectangle<float> (w, h).withCentre (area.getCentre()));
}

void styleLabel (juce::Label& l, float size, juce::Colour c, bool bold = false)
{
    l.setFont (fam::font (size, bold));
    l.setColour (juce::Label::textColourId, c);
    l.setJustificationType (juce::Justification::topLeft);
    l.setMinimumHorizontalScale (1.0f);
}
} // namespace

namespace tone3000Logos
{
juce::Image full()
{
    return juce::ImageCache::getFromMemory (Tone3000Assets::tone3000logo_png, Tone3000Assets::tone3000logo_pngSize);
}

juce::Image compact()
{
    return juce::ImageCache::getFromMemory (Tone3000Assets::t3klogo_png, Tone3000Assets::t3klogo_pngSize);
}
} // namespace tone3000Logos

//==============================================================================
/** Before signing in: who TONE3000 are, then Continue. */
class Tone3000Browser::Splash final : public juce::Component
{
public:
    Splash()
    {
        styleLabel (text, 15.0f, fam::cream);
        text.setJustificationType (juce::Justification::centredTop);
        text.setText ("RoughStomp has partnered with TONE3000 to give you access to a massive library of "
                      "Neural Amp Modeler (NAM) captures and IRs of real analog gear, created by a global "
                      "community of musicians.", juce::dontSendNotification);
        addAndMakeVisible (text);
        addAndMakeVisible (continueButton);
        addAndMakeVisible (notNow);
        continueButton.onClick = [this] { if (onContinue) onContinue(); };
        notNow.onClick = [this] { if (onCancel) onCancel(); };
    }

    std::function<void()> onContinue, onCancel;

    void paint (juce::Graphics& g) override
    {
        const auto wordmark = tone3000Logos::full();
        const float h = 48.0f, w = h * static_cast<float> (wordmark.getWidth()) / static_cast<float> (wordmark.getHeight());
        g.drawImage (wordmark, juce::Rectangle<float> (w, h).withCentre ({ static_cast<float> (getWidth()) * 0.5f,
                                                                          static_cast<float> (getHeight()) * 0.3f }));
    }

    void resized() override
    {
        auto b = getLocalBounds().withSizeKeepingCentre (juce::jmin (560, getWidth() - 2 * pad), 200);
        b.setY (getHeight() * 3 / 10 + 50);
        text.setBounds (b.removeFromTop (80));
        b.removeFromTop (16);
        auto buttons = b.removeFromTop (30).withSizeKeepingCentre (250, 30);
        continueButton.setBounds (buttons.removeFromLeft (150));
        buttons.removeFromLeft (10);
        notNow.setBounds (buttons);
    }

private:
    juce::Label text;
    juce::TextButton continueButton { "Continue" }, notNow { "Not now" };
};

//==============================================================================
/** Device-code sign-in: a code to enter at tone3000.com/activate. */
class Tone3000Browser::SignIn final : public juce::Component
{
public:
    explicit SignIn (rs::t3k::Service& s) : service (s)
    {
        styleLabel (heading, 20.0f, fam::cream, true);
        heading.setText ("Sign in to TONE3000", juce::dontSendNotification);
        styleLabel (instructions, 14.0f, fam::dimText);
        instructions.setText ("Open TONE3000 in your browser, sign in, and check it shows this code:",
                              juce::dontSendNotification);
        styleLabel (code, 40.0f, fam::amber, true);
        styleLabel (status, 13.0f, fam::dimText);
        for (auto* l : { &heading, &instructions, &code, &status })
        {
            l->setJustificationType (juce::Justification::centred);
            addAndMakeVisible (l);
        }
        addAndMakeVisible (openButton);
        addAndMakeVisible (cancelButton);
        addChildComponent (newCodeButton);
        openButton.onClick = [this] { if (link.isNotEmpty()) juce::URL (link).launchInDefaultBrowser(); };
        cancelButton.onClick = [this] { service.cancelSignIn(); if (onCancel) onCancel(); };
        newCodeButton.onClick = [this] { start(); };
    }

    std::function<void()> onSignedIn, onCancel;

    void start()
    {
        code.setText ("....-....", juce::dontSendNotification);
        status.setText ("Getting a code...", juce::dontSendNotification);
        openButton.setEnabled (false);
        newCodeButton.setVisible (false);

        juce::Component::SafePointer<SignIn> safe (this);
        service.startSignIn ([safe] (const rs::t3k::Api::DeviceCode& c)
                             {
                                 if (safe == nullptr)
                                     return;
                                 safe->code.setText (c.userCode, juce::dontSendNotification);
                                 safe->link = c.verificationUriComplete.isNotEmpty() ? c.verificationUriComplete : c.verificationUri;
                                 safe->status.setText ("Waiting for you to approve it on TONE3000...", juce::dontSendNotification);
                                 safe->openButton.setEnabled (true);
                             },
                             [safe] (bool ok, const juce::String& error)
                             {
                                 if (safe == nullptr)
                                     return;
                                 if (ok)
                                 {
                                     if (safe->onSignedIn)
                                         safe->onSignedIn();
                                     return;
                                 }
                                 safe->status.setText (error, juce::dontSendNotification);
                                 safe->newCodeButton.setVisible (true);
                                 safe->openButton.setEnabled (false);
                             });
    }

    void resized() override
    {
        auto b = getLocalBounds().withSizeKeepingCentre (juce::jmin (520, getWidth() - 2 * pad), 300);
        heading.setBounds (b.removeFromTop (34));
        b.removeFromTop (8);
        instructions.setBounds (b.removeFromTop (24));
        b.removeFromTop (10);
        code.setBounds (b.removeFromTop (56));
        b.removeFromTop (10);
        auto buttons = b.removeFromTop (30).withSizeKeepingCentre (330, 30);
        openButton.setBounds (buttons.removeFromLeft (160));
        buttons.removeFromLeft (10);
        cancelButton.setBounds (buttons);
        b.removeFromTop (14);
        status.setBounds (b.removeFromTop (22));
        b.removeFromTop (8);
        newCodeButton.setBounds (b.removeFromTop (28).withSizeKeepingCentre (140, 28));
    }

private:
    rs::t3k::Service& service;
    juce::Label heading, instructions, code, status;
    juce::TextButton openButton { "Open TONE3000" }, cancelButton { "Cancel" }, newCodeButton { "Get a new code" };
    juce::String link;
};

//==============================================================================
namespace
{
/** A tone in a list: picture, title, gear and format, creator. */
class ToneCard final : public juce::Component
{
public:
    ToneCard (const Tone& t, rs::t3k::Service& service) : tone (t)
    {
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
        juce::Component::SafePointer<ToneCard> safe (this);
        service.image (tone.image(), [safe] (const juce::Image& img)
        {
            if (safe != nullptr)
            {
                safe->picture = img;
                safe->repaint();
            }
        });
    }

    std::function<void (const Tone&)> onClick;

    void paint (juce::Graphics& g) override
    {
        auto b = getLocalBounds().toFloat().reduced (1.0f);
        g.setColour (isMouseOver() ? fam::widget : fam::strip);
        g.fillRoundedRectangle (b, 6.0f);
        g.setColour (fam::outline.withAlpha (0.6f));
        g.drawRoundedRectangle (b, 6.0f, 1.0f);

        auto inner = b.reduced (6.0f);
        drawPicture (g, picture, inner.removeFromLeft (inner.getHeight()), 4.0f);
        inner.removeFromLeft (10.0f);

        g.setColour (fam::cream);
        g.setFont (fam::font (13.5f, true));
        g.drawFittedText (tone.title, inner.removeFromTop (36.0f).toNearestInt(), juce::Justification::topLeft, 2, 1.0f);
        g.setColour (fam::dimText);
        g.setFont (fam::font (12.0f));
        g.drawText (rs::t3k::gearName (tone.gear) + "  \xc2\xb7  " + rs::t3k::formatName (tone.format),
                    inner.removeFromTop (17.0f), juce::Justification::topLeft, true);
        g.drawText ("by " + tone.creator.name(), inner.removeFromTop (17.0f), juce::Justification::topLeft, true);
    }

    void mouseEnter (const juce::MouseEvent&) override { repaint(); }
    void mouseExit (const juce::MouseEvent&) override  { repaint(); }
    void mouseUp (const juce::MouseEvent& e) override
    {
        if (e.mouseWasClicked() && onClick)
            onClick (tone);
    }

private:
    Tone tone;
    juce::Image picture;
};

/** The last card in a short list: more on TONE3000. */
class MoreCard final : public juce::Component
{
public:
    explicit MoreCard (juce::String t) : text (std::move (t)) { setMouseCursor (juce::MouseCursor::PointingHandCursor); }
    std::function<void()> onClick;

    void paint (juce::Graphics& g) override
    {
        auto b = getLocalBounds().toFloat().reduced (1.0f);
        g.setColour (isMouseOver() ? fam::widget : fam::recess);
        g.fillRoundedRectangle (b, 6.0f);
        g.setColour (fam::amber.withAlpha (0.7f));
        g.drawRoundedRectangle (b, 6.0f, 1.0f);
        g.setColour (fam::amber);
        g.setFont (fam::font (14.0f, true));
        g.drawFittedText (text, b.reduced (10.0f).toNearestInt(), juce::Justification::centred, 2);
    }

    void mouseEnter (const juce::MouseEvent&) override { repaint(); }
    void mouseExit (const juce::MouseEvent&) override  { repaint(); }
    void mouseUp (const juce::MouseEvent& e) override { if (e.mouseWasClicked() && onClick) onClick(); }

private:
    juce::String text;
};
} // namespace

//==============================================================================
/** The lists: your favorites, uploads and downloads, and what's trending or new. */
class Tone3000Browser::Library final : public juce::Component
{
public:
    Library (rs::t3k::Service& s) : service (s)
    {
        const std::array<std::pair<const char*, const char*>, 5> kinds { {
            { "favorited", "Favorites" }, { "created", "Created" }, { "downloaded", "Downloaded" },
            { "trending", "Trending" }, { "latest", "Latest" } } };
        for (const auto& [id, name] : kinds)
        {
            auto* b = tabs.add (new juce::TextButton (name));
            b->setClickingTogglesState (false);
            b->setColour (juce::TextButton::buttonOnColourId, fam::amber.withAlpha (0.35f));
            b->onClick = [this, list = juce::String (id)] { showList (list); };
            ids.add (id);
            addAndMakeVisible (b);
        }
        viewport.setViewedComponent (&grid, false);
        viewport.setScrollBarsShown (true, false);
        addAndMakeVisible (viewport);
        styleLabel (message, 14.0f, fam::dimText);
        message.setJustificationType (juce::Justification::centred);
        addChildComponent (message);
    }

    std::function<void (const Tone&)> onTone;
    std::function<void()> onBrowse;

    void setFormat (const juce::String& f)
    {
        format = f;
        showList (which.isEmpty() ? "favorited" : which);
    }

    void reload() { showList (which.isEmpty() ? "favorited" : which, true); }
    void show (const juce::String& list) { showList (list); }

    void resized() override
    {
        auto b = getLocalBounds();
        auto row = b.removeFromTop (30);
        for (auto* t : tabs)
        {
            t->setBounds (row.removeFromLeft (110));
            row.removeFromLeft (6);
        }
        b.removeFromTop (10);
        viewport.setBounds (b);
        message.setBounds (b.withHeight (80));
        layoutGrid();
    }

private:
    void showList (const juce::String& w, bool fresh = false)
    {
        which = w;
        for (int i = 0; i < tabs.size(); ++i)
            tabs[i]->setToggleState (ids[i] == which, juce::dontSendNotification);
        cards.clear();
        more.reset();
        page = 0;
        hasMore = false;
        message.setVisible (false);
        grid.removeAllChildren();
        fetch (1, fresh);
    }

    void fetch (int p, bool fresh = false)
    {
        loading = true;
        if (p == 1)
            showMessage ("Loading...");
        const auto list = which;
        juce::Component::SafePointer<Library> safe (this);
        service.tones (list, p, [safe, list, p] (bool ok, const rs::t3k::Page<Tone>& result, const juce::String& error)
        {
            if (safe == nullptr || safe->which != list)
                return;
            safe->loading = false;
            if (! ok)
                return safe->showMessage (error);
            safe->page = p;
            safe->hasMore = result.hasMore();
            for (const auto& t : result.items)
                if (t.format == safe->format)   // the lists don't filter by format: this slot's only
                    safe->addCard (t);
            safe->layoutGrid();
        }, fresh);
    }

    void addCard (const Tone& t)
    {
        auto* c = cards.add (new ToneCard (t, service));
        c->onClick = [this] (const Tone& tone) { if (onTone) onTone (tone); };
        grid.addAndMakeVisible (c);
    }

    void showMessage (const juce::String& text)
    {
        message.setText (text, juce::dontSendNotification);
        message.setVisible (text.isNotEmpty());
    }

    void layoutGrid()
    {
        const bool shortList = which == "trending" || which == "latest";
        const bool wantMore = shortList || hasMore;
        if (wantMore && more == nullptr)
        {
            more = std::make_unique<MoreCard> (shortList ? "Discover more on TONE3000" : "Load more");
            more->onClick = [this, shortList]
            {
                if (shortList) { if (onBrowse) onBrowse(); }
                else if (! loading) fetch (page + 1);
            };
            grid.addAndMakeVisible (*more);
        }
        else if (! wantMore)
            more.reset();

        const int w = juce::jmax (100, viewport.getWidth() - viewport.getScrollBarThickness());
        const int columns = juce::jmax (1, w / 250), cardW = w / columns, cardH = 96;
        int i = 0;
        auto place = [&] (juce::Component& c)
        {
            c.setBounds ((i % columns) * cardW, (i / columns) * cardH, cardW - 6, cardH - 6);
            ++i;
        };
        for (auto* c : cards)
            place (*c);
        if (more != nullptr)
            place (*more);
        grid.setSize (w, ((i + columns - 1) / columns) * cardH);

        if (! loading && which.isNotEmpty())
        {
            if (cards.isEmpty())
            {
                const auto* tab = tabs[ids.indexOf (which)];
                const auto name = tab != nullptr ? tab->getButtonText() : which;
                showMessage (which == "favorited" || which == "created" || which == "downloaded"
                                 ? "No " + whatLoads (format) + " in your " + name + " yet. Find some with Browse TONE3000."
                                 : "Nothing here for this slot right now. Try Browse TONE3000.");
                if (more != nullptr)
                    more->setTopLeftPosition (0, 90);
            }
            else
                showMessage ({});
        }
    }

    rs::t3k::Service& service;
    juce::OwnedArray<juce::TextButton> tabs;
    juce::StringArray ids;
    juce::Viewport viewport;
    juce::Component grid;
    juce::OwnedArray<ToneCard> cards;
    std::unique_ptr<MoreCard> more;
    juce::Label message;
    juce::String format = "nam", which;
    int page = 0;
    bool hasMore = false, loading = false;
};

//==============================================================================
/** One tone: what it is, its models, and Load. */
class Tone3000Browser::Detail final : public juce::Component
{
public:
    Detail (RigProcessor& p, rs::t3k::Service& s) : rigProcessor (p), service (s)
    {
        styleLabel (title, 20.0f, fam::cream, true);
        styleLabel (creator, 14.0f, fam::dimText);
        styleLabel (meta, 13.0f, fam::dimText);
        styleLabel (modelLabel, 13.0f, fam::dimText);
        styleLabel (status, 13.0f, fam::amber);
        modelLabel.setText ("Model", juce::dontSendNotification);
        description.setMultiLine (true, true);
        description.setReadOnly (true);
        description.setScrollbarsShown (true);
        description.setCaretVisible (false);
        description.setFont (fam::font (13.0f));
        description.setColour (juce::TextEditor::backgroundColourId, fam::recess);
        description.setColour (juce::TextEditor::textColourId, fam::cream.withAlpha (0.85f));
        description.setColour (juce::TextEditor::outlineColourId, juce::Colours::transparentBlack);
        for (auto* c : std::initializer_list<juce::Component*> { &backButton, &title, &creator, &meta, &description,
                                                                &modelLabel, &models, &loadButton, &status, &pageLink })
            addAndMakeVisible (c);
        backButton.onClick = [this] { if (onBack) onBack(); };
        loadButton.onClick = [this] { load(); };
        pageLink.setFont (fam::font (13.0f), false, juce::Justification::centredLeft);
        pageLink.setColour (juce::HyperlinkButton::textColourId, fam::amber);
    }

    std::function<void()> onBack, onLoaded;

    void setTone (rig::FileSlot s, const juce::String& slotFormat, int toneId)
    {
        slot = s;
        format = slotFormat;
        tone = {};
        modelList.clear();
        picture = {};
        title.setText ("Loading...", juce::dontSendNotification);
        for (auto* l : { &creator, &meta, &status })
            l->setText ({}, juce::dontSendNotification);
        description.clear();
        models.clear (juce::dontSendNotification);
        loadButton.setEnabled (false);
        pageLink.setVisible (false);
        repaint();

        juce::Component::SafePointer<Detail> safe (this);
        service.tone (toneId, [safe, toneId] (bool ok, const Tone& t, const juce::String& error)
        {
            if (safe == nullptr)
                return;
            if (! ok)
            {
                safe->title.setText ("Tone unavailable", juce::dontSendNotification);
                safe->status.setText (rs::t3k::Service::isUnavailable (safe->service.lastStatus())
                                          ? juce::String ("This tone is private or has been removed.") : error,
                                      juce::dontSendNotification);
                return;
            }
            safe->showTone (t);
            safe->service.models (toneId, [safe] (bool okModels, const std::vector<Model>& list, const juce::String& e)
            {
                if (safe != nullptr)
                    safe->showModels (okModels, list, e);
            });
        });
    }

    void paint (juce::Graphics& g) override
    {
        drawPicture (g, picture, pictureArea.toFloat(), 6.0f);
    }

    void resized() override
    {
        auto b = getLocalBounds();
        backButton.setBounds (b.removeFromTop (28).removeFromLeft (90));
        b.removeFromTop (12);

        pictureArea = b.removeFromLeft (220).removeFromTop (220);
        b.removeFromLeft (pad);

        title.setBounds (b.removeFromTop (30));
        creator.setBounds (b.removeFromTop (22));
        meta.setBounds (b.removeFromTop (22));
        pageLink.setBounds (b.removeFromTop (22).removeFromLeft (160));
        b.removeFromTop (8);

        auto bottom = b.removeFromBottom (96);
        description.setBounds (b);

        bottom.removeFromTop (12);
        auto row = bottom.removeFromTop (30);
        modelLabel.setBounds (row.removeFromLeft (52).withTrimmedTop (6));
        loadButton.setBounds (row.removeFromRight (110));
        row.removeFromRight (10);
        models.setBounds (row);
        bottom.removeFromTop (10);
        status.setBounds (bottom.removeFromTop (24));
    }

private:
    void showTone (const Tone& t)
    {
        tone = t;
        title.setText (t.title, juce::dontSendNotification);
        creator.setText ("by " + t.creator.name(), juce::dontSendNotification);
        juce::StringArray bits { rs::t3k::gearName (t.gear), rs::t3k::formatName (t.format), licenceName (t.license) };
        bits.removeEmptyStrings();
        meta.setText (bits.joinIntoString ("  \xc2\xb7  "), juce::dontSendNotification);
        description.setText (t.description, false);
        description.setCaretPosition (0);
        if (t.url.isNotEmpty())
        {
            pageLink.setButtonText ("Open on TONE3000");
            pageLink.setURL (juce::URL (t.url));
            pageLink.setVisible (true);
        }
        juce::Component::SafePointer<Detail> safe (this);
        service.image (t.image(), [safe] (const juce::Image& img)
        {
            if (safe != nullptr)
            {
                safe->picture = img;
                safe->repaint();
            }
        });
    }

    void showModels (bool ok, const std::vector<Model>& list, const juce::String& error)
    {
        if (! ok)
            return status.setText (error, juce::dontSendNotification);
        if (tone.format != format)
            return status.setText ("This is a " + rs::t3k::formatName (tone.format) + " tone: load it in the "
                                       + (tone.isIr() ? "Cab." : "Amp or Power amp."),
                                   juce::dontSendNotification);
        if (list.empty())
            return status.setText ("This tone has nothing RoughStomp can load.", juce::dontSendNotification);

        modelList = list;
        const auto current = rigProcessor.getTone3000Source (slot);
        int selected = 1;
        for (size_t i = 0; i < list.size(); ++i)
        {
            auto name = list[i].name.isNotEmpty() ? list[i].name : "Model " + juce::String (static_cast<int> (i) + 1);
            if (list[i].size.isNotEmpty() && list[i].size != "standard")
                name << "  (" << list[i].size << ")";
            models.addItem (name, static_cast<int> (i) + 1);
            if (list[i].id == current.modelId)
                selected = static_cast<int> (i) + 1;
        }
        models.setSelectedId (selected, juce::dontSendNotification);
        loadButton.setEnabled (true);
        status.setText (list.size() > 1 ? juce::String (static_cast<int> (list.size())) + " models in this tone" : juce::String(),
                        juce::dontSendNotification);
        status.setColour (juce::Label::textColourId, fam::dimText);
    }

    void load()
    {
        const int i = models.getSelectedId() - 1;
        if (i < 0 || i >= static_cast<int> (modelList.size()))
            return;
        loadButton.setEnabled (false);
        status.setColour (juce::Label::textColourId, fam::dimText);
        status.setText ("Downloading and loading...", juce::dontSendNotification);

        juce::Component::SafePointer<Detail> safe (this);
        rigProcessor.loadTone3000 (slot, tone, modelList[static_cast<size_t> (i)], [safe] (const juce::String& error)
        {
            if (safe == nullptr)
                return;
            safe->loadButton.setEnabled (true);
            if (error.isNotEmpty())
            {
                safe->status.setColour (juce::Label::textColourId, fam::amber);
                safe->status.setText (error, juce::dontSendNotification);
                return;
            }
            safe->status.setText ({}, juce::dontSendNotification);
            if (safe->onLoaded)
                safe->onLoaded();
        });
    }

    RigProcessor& rigProcessor;
    rs::t3k::Service& service;
    rig::FileSlot slot = rig::FileSlot::amp;
    juce::String format = "nam";
    Tone tone;
    std::vector<Model> modelList;
    juce::Image picture;
    juce::Rectangle<int> pictureArea;

    juce::TextButton backButton { "< Back" }, loadButton { "Load" };
    juce::Label title, creator, meta, modelLabel, status;
    juce::TextEditor description;
    juce::ComboBox models;
    juce::HyperlinkButton pageLink;
};

//==============================================================================
Tone3000Browser::Tone3000Browser (RigProcessor& p, rs::t3k::Service& s)
    : rigProcessor (p), service (s), logo (tone3000Logos::full())
{
    splash = std::make_unique<Splash>();
    signIn = std::make_unique<SignIn> (service);
    library = std::make_unique<Library> (service);
    detail = std::make_unique<Detail> (rigProcessor, service);
    for (auto* c : std::initializer_list<juce::Component*> { splash.get(), signIn.get(), library.get(), detail.get() })
        addChildComponent (c);

    splash->onContinue = [this] { show (Screen::signIn); };
    splash->onCancel = [this] { close(); };
    signIn->onSignedIn = [this] { signedIn(); };
    signIn->onCancel = [this] { show (Screen::splash); };
    library->onTone = [this] (const Tone& t) { showTone (t.id); };
    library->onBrowse = [this] { browseCatalog(); };
    detail->onBack = [this] { show (Screen::library); };
    detail->onLoaded = [this] { close(); };

    styleLabel (status, 13.0f, fam::dimText);
    status.setJustificationType (juce::Justification::centredRight);
    for (auto* c : std::initializer_list<juce::Component*> { &browseButton, &signOutButton, &closeButton, &status })
        addChildComponent (c);
    closeButton.setVisible (true);

    browseButton.onClick = [this] { browseCatalog(); };
    signOutButton.onClick = [this]
    {
        service.signOut();
        username = {};
        avatar = {};
        show (Screen::splash);
    };
    closeButton.onClick = [this] { close(); };
}

Tone3000Browser::~Tone3000Browser()
{
    service.cancelSignIn();
    if (service.isBrowsing())
        service.cancelBrowse();
}

void Tone3000Browser::openFor (rig::FileSlot s)
{
    slot = s;
    format = s == rig::FileSlot::cab ? "ir" : "nam";
    status.setText ({}, juce::dontSendNotification);
    if (service.isSignedIn())
    {
        library->setFormat (format);
        show (Screen::library);
    }
    else
        show (Screen::splash);
    setVisible (true);
    toFront (true);
}

void Tone3000Browser::openTone (rig::FileSlot s, int toneId)
{
    openFor (s);
    if (service.isSignedIn())
        showTone (toneId);
}

void Tone3000Browser::showList (const juce::String& which)
{
    if (! service.isSignedIn())
        return;
    library->show (which);
    show (Screen::library);
}

void Tone3000Browser::show (Screen s)
{
    screen = s;
    splash->setVisible (s == Screen::splash);
    signIn->setVisible (s == Screen::signIn);
    library->setVisible (s == Screen::library);
    detail->setVisible (s == Screen::detail);

    headerVisible = s == Screen::library || s == Screen::detail;
    browseButton.setVisible (headerVisible);
    signOutButton.setVisible (headerVisible);
    status.setVisible (headerVisible);

    if (s == Screen::signIn)
        signIn->start();
    if (headerVisible)
        refreshHeader();
    resized();
    repaint();
}

void Tone3000Browser::close()
{
    service.cancelSignIn();
    setVisible (false);
    if (onClose)
        onClose();
}

void Tone3000Browser::signedIn()
{
    rigProcessor.retryTone3000Downloads();   // anything a session was waiting for
    library->setFormat (format);
    show (Screen::library);
}

void Tone3000Browser::showTone (int toneId)
{
    detail->setTone (slot, format, toneId);
    show (Screen::detail);
}

void Tone3000Browser::refreshHeader()
{
    if (username.isNotEmpty())
        return;
    juce::Component::SafePointer<Tone3000Browser> safe (this);
    service.user ([safe] (bool ok, const rs::t3k::User& u, const juce::String&)
    {
        if (safe == nullptr || ! ok)
            return;
        safe->username = u.name();
        safe->repaint();
        safe->service.image (u.avatarUrl, [safe] (const juce::Image& img)
        {
            if (safe != nullptr)
            {
                safe->avatar = img;
                safe->repaint();
            }
        });
    });
}

void Tone3000Browser::browseCatalog()
{
    if (service.isBrowsing())
    {
        service.cancelBrowse();
        browseButton.setButtonText ("Browse TONE3000");
        status.setText ({}, juce::dontSendNotification);
        return;
    }

    browseButton.setButtonText ("Cancel browsing");
    status.setColour (juce::Label::textColourId, fam::dimText);
    status.setText ("Choose a tone in your browser...", juce::dontSendNotification);

    juce::Component::SafePointer<Tone3000Browser> safe (this);
    service.browse (format, 0, [safe] (int toneId, const juce::String& error)
    {
        if (safe == nullptr)
            return;
        safe->browseButton.setButtonText ("Browse TONE3000");
        safe->status.setColour (juce::Label::textColourId, error.isNotEmpty() ? fam::amber : fam::dimText);
        safe->status.setText (error, juce::dontSendNotification);
        if (toneId > 0)
        {
            safe->toFront (true);
            safe->showTone (toneId);
        }
    });
}

void Tone3000Browser::paint (juce::Graphics& g)
{
    auto b = getLocalBounds().toFloat();
    g.setColour (fam::panel);
    g.fillRoundedRectangle (b, 8.0f);
    g.setColour (fam::outline);
    g.drawRoundedRectangle (b.reduced (0.5f), 8.0f, 1.0f);

    if (! headerVisible)
        return;

    auto h = headerArea;
    // The full logo, as TONE3000 asks, then which slot this is for
    const float logoH = 22.0f, logoW = logoH * static_cast<float> (logo.getWidth()) / static_cast<float> (logo.getHeight());
    const auto logoArea = juce::Rectangle<float> (static_cast<float> (h.getX()), static_cast<float> (h.getCentreY()) - logoH * 0.5f, logoW, logoH);
    g.drawImage (logo, logoArea);
    g.setColour (fam::dimText);
    g.setFont (fam::font (14.0f));
    g.drawText ("for the " + slotName (slot) + " (" + whatLoads (format) + ")",
                juce::Rectangle<float> (logoArea.getRight() + 14.0f, static_cast<float> (h.getY()), 260.0f, static_cast<float> (h.getHeight())),
                juce::Justification::centredLeft);

    // Who's signed in, left of Sign out
    if (username.isNotEmpty())
    {
        auto who = juce::Rectangle<int> (signOutButton.getX() - 210, h.getY(), 200, h.getHeight());
        auto pic = who.removeFromRight (26).withSizeKeepingCentre (26, 26).toFloat();
        if (avatar.isValid())
            drawPicture (g, avatar, pic, 13.0f);
        else
        {
            g.setColour (fam::widget);
            g.fillEllipse (pic);
            g.setColour (fam::cream);
            g.setFont (fam::font (13.0f, true));
            g.drawText (username.substring (0, 1).toUpperCase(), pic, juce::Justification::centred);
        }
        g.setColour (fam::cream);
        g.setFont (fam::font (14.0f));
        g.drawText (username, who.withTrimmedRight (8), juce::Justification::centredRight);
    }
}

void Tone3000Browser::resized()
{
    auto b = getLocalBounds().reduced (pad);
    closeButton.setBounds (b.getRight() - 80, b.getY(), 80, 28);

    headerArea = b.removeFromTop (headerHeight - 16);
    {
        auto h = headerArea.withTrimmedRight (90);
        signOutButton.setBounds (h.removeFromRight (90).withSizeKeepingCentre (90, 28));
        h.removeFromRight (230);   // who's signed in
        browseButton.setBounds (h.removeFromRight (150).withSizeKeepingCentre (150, 28));
        h.removeFromRight (10);
        status.setBounds (h.removeFromRight (260));
    }
    b.removeFromTop (16);

    const auto content = headerVisible ? b : getLocalBounds().reduced (pad).withTrimmedTop (30);
    for (auto* c : std::initializer_list<juce::Component*> { splash.get(), signIn.get(), library.get(), detail.get() })
        c->setBounds (content);
}
