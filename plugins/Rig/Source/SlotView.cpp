#include "SlotView.h"

#include "CapturePedal.h"
#include "Tone3000Browser.h"
#include "fx/GraphicEq.h"
#include <rs_t3k/Config.h>
#include "rs_ui/Family.h"

namespace
{
constexpr int emptyItemId = 1;   // menu ids: 1 = empty, 2.. = registry order, then NAM Capture
constexpr int typeBoxHeight = 24;
constexpr int footHeight = 50;
constexpr int knobHeight = 66, smallKnobHeight = 56;   // two to a row, or three
constexpr int choiceHeight = 22;
constexpr int faderLabelHeight = 22;   // two staggered rows
constexpr int levelGap = 6;            // between the bands and the level fader

// The slider view
constexpr int stripHeight = 6;          // the pedal's colour, across the top
constexpr int nameHeight = 22;
constexpr int sliderRowHeight = 30;     // name and value, then the slider
constexpr int onHeight = 26;
} // namespace

rs::ui::PedalColours SlotView::coloursFor (const fx::PedalInfo* info)
{
    rs::ui::PedalColours c;   // red, the Overdrive's colour
    if (info == nullptr)
        return c;

    auto paint = [&c] (juce::uint32 body, juce::uint32 dark)
    {
        c.body = juce::Colour (body);
        c.bodyDark = juce::Colour (dark);
    };
    const std::string_view id = info->id;
    if (id == "boost")       paint (0xff2f6db5, 0xff1f4c80);
    else if (id == "eq")     paint (0xff7d848b, 0xff50565c);
    else if (id == "chorus") paint (0xff2a9d8f, 0xff1b6a60);
    else if (id == "delay")  paint (0xff4f8a3c, 0xff325a26);
    else if (id == "reverb") paint (0xff6a4fa3, 0xff45326e);
    else if (id == "tremolo") paint (0xffb08a1e, 0xff7a5f12);
    else if (id == "vibrato") paint (0xffa13d74, 0xff6e2550);
    else if (id == "flanger") paint (0xff2f7f9e, 0xff1f566b);
    else if (id == "phaser")  paint (0xffd0661f, 0xff97470f);
    else if (id == "spring")  paint (0xff6b4a2f, 0xff47301d);
    else if (id == "compressor") paint (0xff4d6f74, 0xff30484c);
    else if (id == "wah")     paint (0xff3a3a3a, 0xff1c1c1c);
    else if (id == "fuzz")       paint (0xff8e1b3a, 0xff5e1026);
    else if (id == "bigfuzz")    paint (0xff6b7a2e, 0xff47511e);
    else if (id == "cleandrive") paint (0xffb07a4a, 0xff7a5232);
    else if (id == "bluesdrive") paint (0xff3b4f94, 0xff263466);
    else if (id == "octave")     paint (0xffb0486a, 0xff7a2f48);
    else if (id == "nam")        paint (0xff5a5d63, 0xff35373b);   // graphite: a box for any pedal
    return c;
}

SlotView::SlotView (RigProcessor& p, rig::Section s, int slot)
    : rigProcessor (p), section (s), index (slot)
{
    typeBox.addItem ("Empty", emptyItemId);
    const auto all = fx::registry::all();
    for (size_t i = 0; i < all.size(); ++i)
        typeBox.addItem (juce::String (std::string (all[i]->name)), static_cast<int> (i) + 2);
    const int captureItemId = static_cast<int> (all.size()) + 2;
    typeBox.addSeparator();
    typeBox.addItem (juce::String (std::string (CapturePedal::staticInfo().name)), captureItemId);

    typeBox.setJustificationType (juce::Justification::centred);
    typeBox.setTooltip ("Pedal in this slot");
    typeBox.onChange = [this]
    {
        const int id = typeBox.getSelectedId();
        const auto pedals = fx::registry::all();
        const auto i = static_cast<size_t> (id - 2);
        juce::String pedalId = id >= 2 && i < pedals.size() ? juce::String (std::string (pedals[i]->id)) : juce::String();
        if (id == static_cast<int> (pedals.size()) + 2)
            pedalId = juce::String (std::string (CapturePedal::staticInfo().id));
        rigProcessor.setSlotPedal (section, index, pedalId);
        refresh();
    };
    addAndMakeVisible (typeBox);

    sliders = view->getStyle() == ViewSettings::Style::sliders;
    view->addChangeListener (this);
    refresh();
}

SlotView::~SlotView()
{
    view->removeChangeListener (this);
}

void SlotView::changeListenerCallback (juce::ChangeBroadcaster*)
{
    const bool nowSliders = view->getStyle() == ViewSettings::Style::sliders;
    if (nowSliders != sliders)
    {
        sliders = nowSliders;
        rebuild();
    }
}

void SlotView::refresh()
{
    const auto* info = rigProcessor.getSlotInfo (section, index);
    if (! built || info != shown)
    {
        shown = info;
        rebuild();
    }

    if (led != nullptr && footswitch != nullptr)
        led->setOn (footswitch->getToggleState());
    for (auto& r : rows)   // the slider view's values (automation, presets)
        if (r.value != nullptr && r.slider != nullptr && r.slider->onValueChange)
            r.slider->onValueChange();
    if (isCapture())
        refreshCapture();
}

bool SlotView::isCapture() const noexcept
{
    return shown == &CapturePedal::staticInfo();
}

void SlotView::refreshCapture()
{
    if (captureName == nullptr)
        return;
    const auto file = rigProcessor.getSlotCaptureFile (section, index);
    const auto problem = rigProcessor.getSlotCaptureProblem (section, index);
    const auto source = rigProcessor.getTone3000Source (rig::FileSlot::pedal (section, index));
    const bool has = file != juce::File();
    const bool t3k = has && source.valid();

    namespace fam = rs::ui::family;
    const auto nameText = t3k ? source.toneTitle : has ? file.getFileNameWithoutExtension() : juce::String ("No capture loaded");
    const auto infoText = problem.isNotEmpty() ? problem : t3k ? source.modelName : juce::String();
    if (captureName->getText() != nameText)
        captureName->setText (nameText, juce::dontSendNotification);
    captureName->setTooltip (t3k ? source.toneTitle + " by " + source.creator + " (TONE3000): click to switch models"
                                 : has ? file.getFullPathName() : juce::String());
    captureName->setMouseCursor (t3k ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
    const auto ink = sliders ? fam::cream : colours.ink;
    captureName->setColour (juce::Label::textColourId, has ? ink : ink.withAlpha (0.6f));
    if (captureInfo->getText() != infoText)
        captureInfo->setText (infoText, juce::dontSendNotification);
    const bool busy = problem.startsWith ("Loading") || problem.startsWith ("Downloading");
    captureInfo->setColour (juce::Label::textColourId, problem.isEmpty() || busy ? ink.withAlpha (0.7f) : fam::amber);
    clearButton->setEnabled (has);
    if (t3k != captureFromTone3000)
    {
        captureFromTone3000 = t3k;
        repaint();   // the T3K mark
    }
}

void SlotView::mouseUp (const juce::MouseEvent& e)
{
    if (e.eventComponent == captureName.get() && e.mouseWasClicked() && captureFromTone3000 && onShowCaptureTone)
        onShowCaptureTone();
}

void SlotView::rebuild()
{
    built = true;
    knobs.clear();
    choices.clear();
    faders.clear();
    rows.clear();
    footswitch.reset();
    led.reset();
    onAttachment.reset();
    onButton.reset();
    captureName.reset();
    captureInfo.reset();
    loadButton.reset();
    tone3000Button.reset();
    clearButton.reset();
    captureFromTone3000 = false;
    colours = coloursFor (shown);
    pedalLook = std::make_unique<rs::ui::PedalLookAndFeel> (colours);

    int selected = emptyItemId;
    const auto all = fx::registry::all();
    for (size_t i = 0; i < all.size(); ++i)
        if (all[i] == shown)
            selected = static_cast<int> (i) + 2;
    if (isCapture())
        selected = static_cast<int> (all.size()) + 2;
    typeBox.setSelectedId (selected, juce::dontSendNotification);

    if (isCapture())
    {
        // The capture's name (or its TONE3000 tone), the model or a problem,
        // and Load / TONE3000 / clear
        captureName = std::make_unique<juce::Label>();
        captureInfo = std::make_unique<juce::Label>();
        captureName->setFont (rs::ui::family::font (13.0f, true));
        captureInfo->setFont (rs::ui::family::font (11.0f));
        for (auto* l : { captureName.get(), captureInfo.get() })
        {
            l->setBorderSize ({});
            l->setJustificationType (juce::Justification::centredLeft);
            l->setMinimumHorizontalScale (1.0f);
            addAndMakeVisible (*l);
        }
        captureName->addMouseListener (this, false);
        loadButton = std::make_unique<juce::TextButton> ("Load");
        tone3000Button = std::make_unique<juce::TextButton> ("TONE3000");
        clearButton = std::make_unique<juce::TextButton> (juce::String::fromUTF8 ("\xc3\x97"));
        loadButton->setTooltip ("Load a .nam capture");
        tone3000Button->setTooltip ("Choose a capture from TONE3000's library");
        clearButton->setTooltip ("Clear");
        loadButton->onClick = [this] { if (onLoadCapture) onLoadCapture(); };
        tone3000Button->onClick = [this] { if (onBrowseCapture) onBrowseCapture(); };
        clearButton->onClick = [this] { if (onClearCapture) onClearCapture(); };
        for (auto* b : { loadButton.get(), tone3000Button.get(), clearButton.get() })
            addAndMakeVisible (*b);
        tone3000Button->setVisible (rs::t3k::config::clientId().isNotEmpty());
        refreshCapture();
    }

    if (shown != nullptr)
    {
        auto& apvts = rigProcessor.apvts;
        auto* popupParent = findParentComponentOfClass<juce::AudioProcessorEditor>();

        for (int k = 0; k < rig::knobsPerSlot && k < static_cast<int> (shown->params.size()); ++k)
        {
            const auto& info = shown->params[static_cast<size_t> (k)];
            const auto id = rig::slotKnobID (section, index, k);
            const auto name = juce::String (std::string (info.name)).toUpperCase();

            if (info.kind == fx::ParamInfo::Kind::continuous && isGraphicEq())
            {
                // Faders, so the EQ's shape can be seen at a glance
                Fader f;
                f.slider = std::make_unique<juce::Slider> (juce::Slider::LinearVertical, juce::Slider::NoTextBox);
                f.slider->setPopupDisplayEnabled (true, true, popupParent);
                f.attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (apvts, id.getParamID(), *f.slider);
                f.slider->setDoubleClickReturnValue (true, rig::toNormalised (info, info.defaultValue));
                f.label = juce::String (std::string (info.name)).toUpperCase();
                f.info = &info;
                if (! sliders)
                    f.slider->setLookAndFeel (pedalLook.get());   // the slider view keeps the family look
                addAndMakeVisible (*f.slider);
                faders.push_back (std::move (f));
            }
            else if (info.kind == fx::ParamInfo::Kind::continuous && sliders)
            {
                // A row: name and value, then a slider
                Row r;
                r.name = std::make_unique<juce::Label> (juce::String(), juce::String (std::string (info.name)));
                r.value = std::make_unique<juce::Label>();
                for (auto* l : { r.name.get(), r.value.get() })
                {
                    l->setFont (rs::ui::family::font (12.0f));
                    l->setBorderSize ({});
                    l->setInterceptsMouseClicks (false, false);
                    addAndMakeVisible (*l);
                }
                r.name->setColour (juce::Label::textColourId, rs::ui::family::cream);
                r.value->setColour (juce::Label::textColourId, rs::ui::family::dimText);
                r.value->setJustificationType (juce::Justification::centredRight);

                r.slider = std::make_unique<juce::Slider> (juce::Slider::LinearHorizontal, juce::Slider::NoTextBox);
                r.attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (apvts, id.getParamID(), *r.slider);
                r.slider->setDoubleClickReturnValue (true, rig::toNormalised (info, info.defaultValue));
                r.slider->setTooltip (juce::String (std::string (info.name)));
                auto* slider = r.slider.get();
                auto* value = r.value.get();
                r.slider->onValueChange = [slider, value, &info]
                {
                    value->setText (rig::valueText (info, rig::toPlain (info, static_cast<float> (slider->getValue()))),
                                    juce::dontSendNotification);
                };
                r.slider->onValueChange();
                addAndMakeVisible (*r.slider);
                rows.push_back (std::move (r));
            }
            else if (info.kind == fx::ParamInfo::Kind::continuous)
            {
                auto knob = std::make_unique<rs::ui::PedalKnob> (apvts, id, name, true);
                knob->slider.setDoubleClickReturnValue (true, rig::toNormalised (info, info.defaultValue));
                knob->setPopupParent (popupParent);
                knob->setLookAndFeel (pedalLook.get());
                addAndMakeVisible (*knob);
                knobs.push_back (std::move (knob));
            }
            else
            {
                // Switch-like controls (modes, on/off) get a menu
                Choice c;
                c.box = std::make_unique<juce::ComboBox>();
                const int count = static_cast<int> (info.maxValue - info.minValue) + 1;
                for (int v = 0; v < count; ++v)
                    c.box->addItem (rig::valueText (info, info.minValue + static_cast<float> (v)), v + 1);
                c.box->setJustificationType (juce::Justification::centred);
                c.box->setTooltip (juce::String (std::string (info.name)));

                auto* box = c.box.get();
                auto* param = apvts.getParameter (id.getParamID());
                c.attachment = std::make_unique<juce::ParameterAttachment> (*param, [box, info] (float normalised)
                {
                    box->setSelectedId (static_cast<int> (rig::toPlain (info, normalised) - info.minValue) + 1,
                                        juce::dontSendNotification);
                });

                auto* attachment = c.attachment.get();
                box->onChange = [box, attachment, info]
                {
                    const float plain = info.minValue + static_cast<float> (box->getSelectedId() - 1);
                    attachment->setValueAsCompleteGesture (rig::toNormalised (info, plain));
                };
                attachment->sendInitialUpdate();
                box->setLookAndFeel (sliders ? static_cast<juce::LookAndFeel*> (&sliderLook.get()) : pedalLook.get());
                addAndMakeVisible (*box);
                choices.push_back (std::move (c));
            }
        }

        if (sliders)
        {
            onButton = std::make_unique<juce::TextButton> ("On");
            onButton->setClickingTogglesState (true);
            onButton->setTooltip ("On / off");
            onButton->setLookAndFeel (&sliderLook.get());
            onAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (apvts, rig::slotOnID (section, index).getParamID(), *onButton);
            addAndMakeVisible (*onButton);
        }
        else
        {
            footswitch = std::make_unique<rs::ui::Footswitch> (apvts, rig::slotOnID (section, index));
            footswitch->setTooltip ("On / off");
            addAndMakeVisible (*footswitch);

            led = std::make_unique<rs::ui::Led> (colours);
            addAndMakeVisible (*led);
            led->setOn (footswitch->getToggleState());
        }
    }

    resized();
    repaint();
}

void SlotView::mouseDrag (const juce::MouseEvent& e)
{
    if (shown == nullptr || e.getDistanceFromDragStart() < 6)
        return;
    if (auto* container = juce::DragAndDropContainer::findParentDragContainerFor (this); container != nullptr && ! container->isDragAndDropActive())
    {
        const juce::ScaledImage dragImage (createComponentSnapshot (getLocalBounds()).convertedToFormat (juce::Image::ARGB));
        container->startDragging ("slot:" + rig::sectionKey (section) + ":" + juce::String (index), this, dragImage, true);
    }
}

bool SlotView::isInterestedInDragSource (const SourceDetails& d)
{
    return d.description.toString().startsWith ("slot:") && d.sourceComponent.get() != this;
}

void SlotView::itemDragEnter (const SourceDetails&) { dropHighlight = true; repaint(); }
void SlotView::itemDragExit (const SourceDetails&)  { dropHighlight = false; repaint(); }

void SlotView::itemDropped (const SourceDetails& d)
{
    dropHighlight = false;
    const auto parts = juce::StringArray::fromTokens (d.description.toString(), ":", {});
    if (parts.size() == 3)
    {
        const auto fromSection = parts[1] == rig::sectionKey (rig::Section::pre) ? rig::Section::pre : rig::Section::loop;
        rigProcessor.swapSlots (fromSection, parts[2].getIntValue(), section, index);
    }
    // Both slots rebuild on their next refresh; do this one now
    refresh();
    repaint();
}

void SlotView::paint (juce::Graphics& g)
{
    auto body = getLocalBounds().toFloat().withTrimmedTop (static_cast<float> (typeBoxHeight + 4)).reduced (2.0f);

    if (dropHighlight)
    {
        g.setColour (juce::Colours::white.withAlpha (0.08f));
        g.fillRoundedRectangle (body.expanded (2.0f), 14.0f);
    }

    if (shown == nullptr)
    {
        juce::Path outline;
        outline.addRoundedRectangle (body, 12.0f);
        // As RoughTrack's "+ Track" placeholder
        const float dashes[] { 5.0f, 4.0f };
        juce::PathStrokeType (1.0f).createDashedStroke (outline, outline, dashes, 2);
        g.setColour (dropHighlight ? rs::ui::family::cream : rs::ui::family::dimText.withAlpha (0.6f));
        g.fillPath (outline);
        g.setFont (rs::ui::family::font (13.0f));
        g.drawText ("Empty", body, juce::Justification::centred);
        return;
    }

    if (sliders)
        paintSliders (g, body);
    else
        paintPedal (g, body);

    if (captureFromTone3000 && ! captureMarkArea.isEmpty())
    {
        const auto mark = tone3000Logos::compact();
        const float h = static_cast<float> (captureMarkArea.getHeight());
        g.drawImage (mark, captureMarkArea.toFloat().withWidth (h * static_cast<float> (mark.getWidth()) / static_cast<float> (mark.getHeight())),
                     juce::RectanglePlacement::xLeft | juce::RectanglePlacement::yMid);
    }
}

void SlotView::paintPedal (juce::Graphics& g, juce::Rectangle<float> body)
{
    g.setGradientFill (juce::ColourGradient (colours.body, body.getTopLeft(), colours.bodyDark, body.getBottomRight(), false));
    g.fillRoundedRectangle (body, 12.0f);
    g.setColour (dropHighlight ? juce::Colours::white.withAlpha (0.8f) : juce::Colours::black.withAlpha (0.5f));
    g.drawRoundedRectangle (body, 12.0f, dropHighlight ? 2.5f : 1.5f);

    if (! faders.empty())
        paintEqMarkings (g);

    // Pedal name above the footswitch
    g.setColour (colours.ink);
    g.setFont (rs::ui::pedalFont (13.0f));
    g.drawText (juce::String (std::string (shown->name)).toUpperCase(),
                body.withTrimmedBottom (static_cast<float> (footHeight)).removeFromBottom (18.0f),
                juce::Justification::centred);
}

void SlotView::paintSliders (juce::Graphics& g, juce::Rectangle<float> body)
{
    namespace fam = rs::ui::family;
    // A plain box in the family colours, the pedal's colour as a strip across
    // the top so pedals stay easy to tell apart
    g.setColour (fam::strip);
    g.fillRoundedRectangle (body, 6.0f);
    {
        juce::Graphics::ScopedSaveState save (g);
        juce::Path clip;
        clip.addRoundedRectangle (body, 6.0f);
        g.reduceClipRegion (clip);
        g.setColour (colours.body);
        g.fillRect (body.withHeight (static_cast<float> (stripHeight)));
    }
    g.setColour (dropHighlight ? juce::Colours::white.withAlpha (0.8f) : juce::Colours::black.withAlpha (0.6f));
    g.drawRoundedRectangle (body.reduced (0.5f), 6.0f, dropHighlight ? 2.0f : 1.0f);

    g.setColour (fam::cream);
    g.setFont (fam::font (14.0f));
    g.drawText (juce::String (std::string (shown->name)),
                body.withTrimmedTop (static_cast<float> (stripHeight)).removeFromTop (static_cast<float> (nameHeight)).reduced (10.0f, 0.0f),
                juce::Justification::centredLeft);

    if (! faders.empty())
        paintEqMarkings (g);
}

void SlotView::paintEqMarkings (juce::Graphics& g)
{
    const int numBands = std::min (fx::graphicEq::numBands, static_cast<int> (faders.size()));
    if (numBands < 2)
        return;

    // Labels under each fader, alternating between two rows so each has room
    const auto ink = sliders ? rs::ui::family::dimText : colours.ink;
    g.setColour (ink);
    g.setFont (sliders ? rs::ui::family::font (9.5f) : rs::ui::pedalFont (9.5f));
    const int rowHeight = labelRow.getHeight() / 2;
    for (size_t i = 0; i < faders.size(); ++i)
    {
        const auto b = faders[i].slider->getBounds();
        const int y = labelRow.getY() + (i % 2 == 0 ? 0 : rowHeight);
        g.drawFittedText (faders[i].label, juce::Rectangle<int> (b.getCentreX() - b.getWidth(), y, b.getWidth() * 2, rowHeight),
                          juce::Justification::centred, 1, 0.8f);
    }

    // A faint 0 dB line across the bands: the caps themselves show the curve
    const auto* first = faders.front().slider.get();
    const auto& info = *faders.front().info;
    const auto y0 = static_cast<double> (first->getY()) + first->getPositionOfValue (rig::toNormalised (info, 0.0f));
    const float spacing = static_cast<float> (faders[1].slider->getBounds().getCentreX() - faders[0].slider->getBounds().getCentreX());
    const float left = static_cast<float> (faders[0].slider->getBounds().getCentreX()) - spacing * 0.5f;
    const float right = static_cast<float> (faders[static_cast<size_t> (numBands - 1)].slider->getBounds().getCentreX()) + spacing * 0.5f;
    g.setColour (ink.withAlpha (0.25f));
    g.drawHorizontalLine (juce::roundToInt (y0), left, right);
}

void SlotView::resized()
{
    auto b = getLocalBounds();
    typeBox.setBounds (b.removeFromTop (typeBoxHeight).reduced (2, 0));
    b.removeFromTop (4);
    if (sliders)
        layoutSliders (b);
    else
        layoutPedal (b);
}

juce::Rectangle<int> SlotView::layoutCapture (juce::Rectangle<int> b)
{
    captureMarkArea = {};
    if (! isCapture() || captureName == nullptr)
        return b;
    captureName->setBounds (b.removeFromTop (18));
    auto info = b.removeFromTop (15);
    if (captureFromTone3000)
    {
        captureMarkArea = info.removeFromLeft (30).withSizeKeepingCentre (30, 10);
        info.removeFromLeft (3);
    }
    captureInfo->setBounds (info);
    b.removeFromTop (4);
    auto buttons = b.removeFromTop (22).expanded (sliders ? 6 : 0, 0);   // the slider view's margins are wider
    clearButton->setBounds (buttons.removeFromRight (22));
    buttons.removeFromRight (4);
    if (tone3000Button->isVisible())
    {
        loadButton->setBounds (buttons.removeFromLeft (42));
        buttons.removeFromLeft (4);
        tone3000Button->setBounds (buttons);
    }
    else
        loadButton->setBounds (buttons);
    b.removeFromTop (6);
    return b;
}

void SlotView::layoutSliders (juce::Rectangle<int> b)
{
    b.reduce (2, 2);   // the body, as painted
    b.removeFromTop (stripHeight + nameHeight);
    b.reduce (10, 4);
    b = layoutCapture (b);

    if (onButton != nullptr)
        onButton->setBounds (b.removeFromBottom (onHeight).withSizeKeepingCentre (70, onHeight));
    b.removeFromBottom (8);

    auto menus = b.removeFromBottom (choiceHeight * static_cast<int> (choices.size()));
    for (auto& c : choices)
        c.box->setBounds (menus.removeFromTop (choiceHeight).reduced (0, 1));
    b.removeFromBottom (4);

    // Rows shrink to fit pedals with many controls
    const int rowH = rows.empty() ? sliderRowHeight
                                  : juce::jmin (sliderRowHeight, (b.getHeight() - (faders.empty() ? 0 : 60)) / static_cast<int> (rows.size()));
    for (auto& r : rows)
    {
        auto row = b.removeFromTop (rowH);
        auto text = row.removeFromTop (14);
        r.value->setBounds (text.removeFromRight (56));
        r.name->setBounds (text);
        r.slider->setBounds (row.withTrimmedLeft (-4).withTrimmedRight (-4));
    }

    // The EQ's faders, as in the pedal view
    if (! faders.empty())
    {
        labelRow = b.removeFromBottom (faderLabelHeight);
        const int n = static_cast<int> (faders.size());
        const int gap = isGraphicEq() && n > fx::graphicEq::numBands ? levelGap : 0;
        const int w = (b.getWidth() - gap) / n;
        auto row = b.withSizeKeepingCentre (w * n + gap, b.getHeight());
        for (int i = 0; i < n; ++i)
        {
            if (i == fx::graphicEq::numBands)
                row.removeFromLeft (gap);
            faders[static_cast<size_t> (i)].slider->setBounds (row.removeFromLeft (w));
        }
    }
}

void SlotView::layoutPedal (juce::Rectangle<int> b)
{
    b.reduce (8, 8);
    if (isCapture())
        b = layoutCapture (b.withTrimmedTop (2));

    auto foot = b.removeFromBottom (footHeight);
    if (footswitch != nullptr)
        footswitch->setBounds (foot.withSizeKeepingCentre (foot.getHeight(), foot.getHeight()));
    if (led != nullptr)
        led->setBounds (foot.removeFromLeft (30).withSizeKeepingCentre (30, 30));
    b.removeFromBottom (20);   // pedal name

    // Menus in the pedal's own order, top to bottom
    auto menus = b.removeFromBottom (choiceHeight * static_cast<int> (choices.size()));
    for (auto& c : choices)
        c.box->setBounds (menus.removeFromTop (choiceHeight).reduced (4, 0));

    // Faders side by side, labels underneath
    if (! faders.empty())
    {
        labelRow = b.removeFromBottom (faderLabelHeight);
        const int n = static_cast<int> (faders.size());
        const int gap = isGraphicEq() && n > fx::graphicEq::numBands ? levelGap : 0;
        const int w = (b.getWidth() - gap) / n;
        auto row = b.withSizeKeepingCentre (w * n + gap, b.getHeight());
        for (int i = 0; i < n; ++i)
        {
            if (i == fx::graphicEq::numBands)
                row.removeFromLeft (gap);
            faders[static_cast<size_t> (i)].slider->setBounds (row.removeFromLeft (w));
        }
    }

    // Knobs two to a row, or three (smaller) for pedals with many; a short
    // last row is centred
    const size_t cols = knobs.size() > 4 || isCapture() ? 3 : 2;
    const int h = cols == 3 ? smallKnobHeight : knobHeight;
    const int w = b.getWidth() / static_cast<int> (cols);
    for (size_t i = 0; i < knobs.size(); i += cols)
    {
        auto row = b.removeFromTop (h);
        const auto inRow = std::min (cols, knobs.size() - i);
        row = row.withSizeKeepingCentre (w * static_cast<int> (inRow), h);
        for (size_t k = 0; k < inRow; ++k)
            knobs[i + k]->setBounds (row.removeFromLeft (w));
    }
}
