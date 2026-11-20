#include "AmpStrip.h"

#include "MidiMapView.h"
#include "Tone3000Browser.h"

#include <rs_t3k/Config.h>

namespace
{
namespace fam = rs::ui::family;

constexpr int onWidth = 74;        // "Amp A"
constexpr int cabOnWidth = 56;     // "Cab"
constexpr int knobWidth = 50;
constexpr int knobHeight = 58;
constexpr int buttonsHeight = 22;
constexpr int fileRowHeight = 40;  // a name over its buttons
constexpr int toneButtonWidth = 48;
} // namespace

//==============================================================================
FileRow::FileRow (const juce::String& loadTooltip)
{
    name.setFont (fam::font (13.0f));
    name.setMinimumHorizontalScale (1.0f);   // long names end in "..."
    name.setBorderSize ({});
    name.addMouseListener (this, false);     // a TONE3000 file's name opens its tone
    load.setTooltip (loadTooltip);
    tone3000.setTooltip ("Choose from TONE3000's library");
    clear.setTooltip ("Clear");
    load.onClick = [this] { if (onLoad) onLoad(); };
    tone3000.onClick = [this] { if (onTone3000) onTone3000(); };
    clear.onClick = [this] { if (onClear) onClear(); };
    for (auto* c : std::initializer_list<juce::Component*> { &name, &load, &clear })
        addAndMakeVisible (c);
    addChildComponent (tone3000);
}

void FileRow::setTone3000Available (bool available)
{
    tone3000.setVisible (available);
    resized();
}

void FileRow::show (const juce::File& file, const juce::String& problem, const rig::Tone3000Source& source,
                    const juce::String& emptyText)
{
    const bool has = file != juce::File();
    const bool t3k = has && source.valid() && problem.isEmpty();
    if (t3k != fromTone3000)
    {
        fromTone3000 = t3k;
        name.setMouseCursor (t3k ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
        resized();
        repaint();
    }

    // A problem replaces the name (in amber); a TONE3000 file shows as its tone
    const auto text = problem.isNotEmpty() ? problem
                    : t3k ? source.toneTitle
                    : has ? file.getFileNameWithoutExtension() : emptyText;
    if (name.getText() != text)
        name.setText (text, juce::dontSendNotification);
    const bool busy = problem.startsWith ("Loading") || problem.startsWith ("Downloading");
    name.setColour (juce::Label::textColourId, problem.isNotEmpty() ? (busy ? fam::dimText : fam::amber)
                                               : has ? fam::cream : fam::dimText);
    name.setTooltip (problem.isNotEmpty() ? (has ? file.getFullPathName() + ": " : juce::String()) + problem
                   : t3k ? source.toneTitle + ", " + source.modelName + ", by " + source.creator + " (TONE3000): click to switch models"
                   : has ? file.getFullPathName() : juce::String());
    clear.setEnabled (has);
}

void FileRow::mouseUp (const juce::MouseEvent& e)
{
    if (e.eventComponent == &name && e.mouseWasClicked() && fromTone3000 && onShowTone3000)
        onShowTone3000();
}

void FileRow::paint (juce::Graphics& g)
{
    if (fromTone3000 && ! markArea.isEmpty())
    {
        const auto mark = tone3000Logos::compact();
        const float h = static_cast<float> (markArea.getHeight());
        g.drawImage (mark, markArea.toFloat().withWidth (h * static_cast<float> (mark.getWidth()) / static_cast<float> (mark.getHeight())),
                     juce::RectanglePlacement::xLeft | juce::RectanglePlacement::yMid);
    }
}

void FileRow::resized()
{
    // The name over the buttons
    auto b = getLocalBounds();
    auto buttons = b.removeFromBottom (buttonsHeight);
    b.removeFromBottom (2);
    markArea = {};
    if (fromTone3000)
    {
        markArea = b.removeFromLeft (30).withSizeKeepingCentre (30, 11);
        b.removeFromLeft (4);
    }
    name.setBounds (b);

    load.setBounds (buttons.removeFromLeft (64));
    buttons.removeFromLeft (4);
    if (tone3000.isVisible())
    {
        tone3000.setBounds (buttons.removeFromLeft (82));
        buttons.removeFromLeft (4);
    }
    clear.setBounds (buttons.removeFromLeft (24));
}

//==============================================================================
AmpStrip::AmpStrip (RigProcessor& p, rig::AmpStage s)
    : rigProcessor (p), stage (s),
      onAttachment (p.apvts, (s == rig::AmpStage::amp ? ParamIDs::ampOn : ParamIDs::powerOn).getParamID(), on),
      cabOnAttachment (p.apvts, (s == rig::AmpStage::amp ? ParamIDs::cabOn : ParamIDs::cabBOn).getParamID(), cabOn),
      gain  (p.apvts, s == rig::AmpStage::amp ? ParamIDs::ampGain : ParamIDs::ampBGain, "Gain"),
      level (p.apvts, s == rig::AmpStage::amp ? ParamIDs::ampLevel : ParamIDs::ampBLevel, "Level"),
      pan   (p.apvts, s == rig::AmpStage::amp ? ParamIDs::ampPan : ParamIDs::ampBPan, "Pan")
{
    on.setButtonText (s == rig::AmpStage::amp ? "Amp A" : "Amp B");
    on.setTooltip (s == rig::AmpStage::amp ? "Amp A on (off: the guitar straight into Cab A, or only Amp B when it plays)"
                                           : "Amp B on (off: only Amp A plays)");
    cabOn.setTooltip ("Cab on (off for captures that already include a cab)");
    tone.setTooltip ("Tone: Bass, Middle and Treble after the amp, low and high cut after the cab");
    tone.setClickingTogglesState (false);
    tone.onClick = [this] { showTone(); };
    for (auto* c : std::initializer_list<juce::Component*> { &on, &amp, &gain, &level, &pan, &tone, &cabOn, &cab })
        addAndMakeVisible (c);
    const bool t3k = rs::t3k::config::clientId().isNotEmpty();
    amp.setTone3000Available (t3k);
    cab.setTone3000Available (t3k);
    pan.slider.setDoubleClickReturnValue (true, 0.5);   // centre
    const bool isA = s == rig::AmpStage::amp;
    midimap::tag (on,    isA ? ParamIDs::ampOn : ParamIDs::powerOn);
    midimap::tag (cabOn, isA ? ParamIDs::cabOn : ParamIDs::cabBOn);
    midimap::tag (gain,  isA ? ParamIDs::ampGain : ParamIDs::ampBGain);
    midimap::tag (level, isA ? ParamIDs::ampLevel : ParamIDs::ampBLevel);
    midimap::tag (pan,   isA ? ParamIDs::ampPan : ParamIDs::ampBPan);
    refresh();
}

void AmpStrip::setPopupParent (juce::Component* parent)
{
    popupParent = parent;
    for (auto* k : { &gain, &level, &pan })
        k->setPopupParent (parent);
}

void AmpStrip::showTone()
{
    auto* parent = popupParent != nullptr ? popupParent : getParentComponent();
    auto panel = std::make_unique<TonePanel> (rigProcessor, stage, parent);
    auto& look = *panel->callOutLook;
    auto& box = juce::CallOutBox::launchAsynchronously (std::move (panel), parent->getLocalArea (this, tone.getBounds()), parent);
    box.setLookAndFeel (&look);
}

void AmpStrip::refresh()
{
    const auto loading = rigProcessor.getLoadingAmpFile (stage);
    const auto ampFile = loading != juce::File() ? loading : rigProcessor.getAmpFile (stage);
    const bool isA = stage == rig::AmpStage::amp;
    amp.show (ampFile, rigProcessor.getAmpProblem (stage), rigProcessor.getTone3000Source (rig::fileSlot (stage)),
              isA ? "No capture loaded" : "Empty: load a second amp");

    const auto which = isA ? rig::Cab::a : rig::Cab::b;
    cab.show (rigProcessor.getCabinetFile (which), rigProcessor.getCabinetProblem (which),
              rigProcessor.getTone3000Source (rig::fileSlot (which)),
              isA ? "No IR loaded" : "Same as A");

    tone.setToggleState (! TonePanel::isFlat (rigProcessor, stage), juce::dontSendNotification);
}

void AmpStrip::paint (juce::Graphics& g)
{
    // A faint divider between the amp and its cab
    g.setColour (juce::Colours::black.withAlpha (0.35f));
    g.fillRect (dividerX, 8, 1, getHeight() - 16);
}

void AmpStrip::resized()
{
    // Everything centred in the strip: switches, files, and the knobs with their labels
    auto b = getLocalBounds();
    auto rowOf = [] (juce::Rectangle<int> r) { return r.withSizeKeepingCentre (r.getWidth(), fileRowHeight); };
    on.setBounds (b.removeFromLeft (onWidth).withSizeKeepingCentre (onWidth, 22));
    b.removeFromLeft (4);

    // The cab takes the right-hand part: a divider, its On, its name and buttons
    auto cabArea = b.removeFromRight (juce::jmin (252, b.getWidth() / 2));
    dividerX = cabArea.getX();
    cabArea.removeFromLeft (8);
    cabOn.setBounds (cabArea.removeFromLeft (cabOnWidth).withSizeKeepingCentre (cabOnWidth, 22));
    cab.setBounds (rowOf (cabArea.withTrimmedRight (4)));

    // Gain / Level / Pan and Tone just left of the divider, the capture before them
    tone.setBounds (b.removeFromRight (toneButtonWidth + 8).withTrimmedRight (8).withSizeKeepingCentre (toneButtonWidth, buttonsHeight));
    auto knobs = b.removeFromRight (knobWidth * 3);
    knobs = knobs.withSizeKeepingCentre (knobs.getWidth(), juce::jmin (knobHeight, knobs.getHeight()))
                 .translated (0, 2);   // the knob leaves a little room under its label: centre what's drawn
    for (auto* k : { &gain, &level, &pan })
        k->setBounds (knobs.removeFromLeft (knobWidth));
    b.removeFromRight (6);
    amp.setBounds (rowOf (b));
}

//==============================================================================
namespace
{
constexpr int panelKnobWidth = 58;
constexpr int panelTitleHeight = 24;
constexpr int panelGap = 18;   // between the amp's knobs and the cab's
} // namespace

void CallOutLook::drawCallOutBoxBackground (juce::CallOutBox& box, juce::Graphics& g, const juce::Path& path, juce::Image& cachedImage)
{
    if (cachedImage.isNull())
    {
        cachedImage = { juce::Image::ARGB, box.getWidth(), box.getHeight(), true };
        juce::Graphics g2 (cachedImage);
        juce::DropShadow (juce::Colours::black.withAlpha (0.6f), 10, { 0, 3 }).drawForPath (g2, path);
    }
    g.drawImageAt (cachedImage, 0, 0);
    g.setColour (fam::strip);
    g.fillPath (path);
    g.setColour (fam::outline);
    g.strokePath (path, juce::PathStrokeType (1.5f));
}

TonePanel::TonePanel (RigProcessor& p, rig::AmpStage s, juce::Component* popupParent)
    : rigProcessor (p), stage (s),
      ids (s == rig::AmpStage::amp ? ParamIDs::toneA : ParamIDs::toneB),
      bass    (p.apvts, ids.bass,    "Bass"),
      middle  (p.apvts, ids.middle,  "Middle"),
      treble  (p.apvts, ids.treble,  "Treble"),
      lowCut  (p.apvts, ids.lowCut,  "Low cut"),
      highCut (p.apvts, ids.highCut, "High cut")
{
    for (auto* k : { &bass, &middle, &treble, &lowCut, &highCut })
    {
        // Double-click: back to flat (or off)
        const auto* param = p.apvts.getParameter (k == &bass ? ids.bass.getParamID() : k == &middle ? ids.middle.getParamID()
                                                : k == &treble ? ids.treble.getParamID() : k == &lowCut ? ids.lowCut.getParamID()
                                                : ids.highCut.getParamID());
        k->slider.setDoubleClickReturnValue (true, param->convertFrom0to1 (param->getDefaultValue()));
        k->setPopupParent (popupParent);
        addAndMakeVisible (k);
    }
    reset.setTooltip ("Back to flat, with no cuts");
    reset.onClick = [this]
    {
        for (const auto* id : { &ids.bass, &ids.middle, &ids.treble, &ids.lowCut, &ids.highCut })
            if (auto* param = rigProcessor.apvts.getParameter (id->getParamID()))
            {
                param->beginChangeGesture();
                param->setValueNotifyingHost (param->getDefaultValue());
                param->endChangeGesture();
            }
    };
    addAndMakeVisible (reset);
    setSize (panelKnobWidth * 5 + panelGap + 16, panelTitleHeight + 66 + 8);
}

bool TonePanel::isFlat (RigProcessor& p, rig::AmpStage s)
{
    const auto& t = s == rig::AmpStage::amp ? ParamIDs::toneA : ParamIDs::toneB;
    for (const auto* id : { &t.bass, &t.middle, &t.treble, &t.lowCut, &t.highCut })
        if (const auto* param = p.apvts.getParameter (id->getParamID());
            param != nullptr && std::abs (param->getValue() - param->getDefaultValue()) > 1.0e-4f)
            return false;
    return true;
}

void TonePanel::paint (juce::Graphics& g)
{
    const bool isA = stage == rig::AmpStage::amp;
    auto title = getLocalBounds().reduced (8, 0).removeFromTop (panelTitleHeight);
    g.setFont (fam::font (13.0f));
    g.setColour (fam::cream);
    g.drawText (isA ? "Amp A" : "Amp B", title.withWidth (panelKnobWidth * 3).reduced (4, 0), juce::Justification::centredLeft);
    g.drawText (isA ? "Cab A" : "Cab B", title.withTrimmedLeft (panelKnobWidth * 3 + panelGap).reduced (4, 0), juce::Justification::centredLeft);

    g.setColour (juce::Colours::black.withAlpha (0.35f));
    g.fillRect (dividerX, panelTitleHeight + 4, 1, getHeight() - panelTitleHeight - 12);
}

void TonePanel::resized()
{
    auto b = getLocalBounds().reduced (8, 0);
    auto title = b.removeFromTop (panelTitleHeight);
    reset.setBounds (title.removeFromRight (56).withSizeKeepingCentre (56, 20).translated (0, 2));
    b.removeFromBottom (8);
    for (auto* k : { &bass, &middle, &treble })
        k->setBounds (b.removeFromLeft (panelKnobWidth));
    dividerX = b.getX() + panelGap / 2;
    b.removeFromLeft (panelGap);
    for (auto* k : { &lowCut, &highCut })
        k->setBounds (b.removeFromLeft (panelKnobWidth));
}
