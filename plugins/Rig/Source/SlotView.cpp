#include "SlotView.h"

#include "fx/GraphicEq.h"

namespace
{
constexpr int emptyItemId = 1;   // menu ids: 1 = empty, 2.. = registry order
constexpr int typeBoxHeight = 24;
constexpr int footHeight = 50;
constexpr int knobHeight = 66, smallKnobHeight = 56;   // two to a row, or three
constexpr int choiceHeight = 22;
constexpr int faderLabelHeight = 22;   // two staggered rows
constexpr int levelGap = 6;            // between the bands and the level fader
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
    return c;
}

SlotView::SlotView (RigProcessor& p, rig::Section s, int slot)
    : rigProcessor (p), section (s), index (slot)
{
    typeBox.addItem ("Empty", emptyItemId);
    const auto all = fx::registry::all();
    for (size_t i = 0; i < all.size(); ++i)
        typeBox.addItem (juce::String (std::string (all[i]->name)), static_cast<int> (i) + 2);

    typeBox.setJustificationType (juce::Justification::centred);
    typeBox.setTooltip ("Pedal in this slot");
    typeBox.onChange = [this]
    {
        const int id = typeBox.getSelectedId();
        const auto pedals = fx::registry::all();
        const auto i = static_cast<size_t> (id - 2);
        const juce::String pedalId = id >= 2 && i < pedals.size() ? juce::String (std::string (pedals[i]->id)) : juce::String();
        rigProcessor.setSlotPedal (section, index, pedalId);
        refresh();
    };
    addAndMakeVisible (typeBox);

    refresh();
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
}

void SlotView::rebuild()
{
    built = true;
    knobs.clear();
    choices.clear();
    faders.clear();
    footswitch.reset();
    led.reset();
    colours = coloursFor (shown);

    int selected = emptyItemId;
    const auto all = fx::registry::all();
    for (size_t i = 0; i < all.size(); ++i)
        if (all[i] == shown)
            selected = static_cast<int> (i) + 2;
    typeBox.setSelectedId (selected, juce::dontSendNotification);

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
                f.slider->onValueChange = [this] { repaint(); };
                f.label = juce::String (std::string (info.name)).toUpperCase();
                f.info = &info;
                addAndMakeVisible (*f.slider);
                faders.push_back (std::move (f));
            }
            else if (info.kind == fx::ParamInfo::Kind::continuous)
            {
                auto knob = std::make_unique<rs::ui::PedalKnob> (apvts, id, name, true);
                knob->slider.setDoubleClickReturnValue (true, rig::toNormalised (info, info.defaultValue));
                knob->setPopupParent (popupParent);
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
                addAndMakeVisible (*box);
                choices.push_back (std::move (c));
            }
        }

        footswitch = std::make_unique<rs::ui::Footswitch> (apvts, rig::slotOnID (section, index));
        footswitch->setTooltip ("On / off");
        addAndMakeVisible (*footswitch);

        led = std::make_unique<rs::ui::Led> (colours);
        addAndMakeVisible (*led);
        led->setOn (footswitch->getToggleState());
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
        const float dashes[] { 6.0f, 5.0f };
        juce::PathStrokeType (1.5f).createDashedStroke (outline, outline, dashes, 2);
        g.setColour (juce::Colours::white.withAlpha (dropHighlight ? 0.6f : 0.18f));
        g.fillPath (outline);
        g.setFont (rs::ui::pedalFont (12.0f, false));
        g.drawText ("empty", body, juce::Justification::centred);
        return;
    }

    g.setGradientFill (juce::ColourGradient (colours.body, body.getTopLeft(), colours.bodyDark, body.getBottomRight(), false));
    g.fillRoundedRectangle (body, 12.0f);
    g.setColour (dropHighlight ? juce::Colours::white.withAlpha (0.8f) : juce::Colours::black.withAlpha (0.5f));
    g.drawRoundedRectangle (body, 12.0f, dropHighlight ? 2.5f : 1.5f);

    if (! faders.empty())
        paintEqCurve (g);

    // Pedal name above the footswitch
    g.setColour (colours.ink);
    g.setFont (rs::ui::pedalFont (13.0f));
    g.drawText (juce::String (std::string (shown->name)).toUpperCase(),
                body.withTrimmedBottom (static_cast<float> (footHeight)).removeFromBottom (18.0f),
                juce::Justification::centred);
}

void SlotView::paintEqCurve (juce::Graphics& g)
{
    const int numBands = std::min (fx::graphicEq::numBands, static_cast<int> (faders.size()));
    if (numBands < 2)
        return;

    // Labels under each fader, alternating between two rows so each has room
    g.setColour (colours.ink);
    g.setFont (rs::ui::pedalFont (9.5f));
    const int rowHeight = labelRow.getHeight() / 2;
    for (size_t i = 0; i < faders.size(); ++i)
    {
        const auto b = faders[i].slider->getBounds();
        const int y = labelRow.getY() + (i % 2 == 0 ? 0 : rowHeight);
        g.drawFittedText (faders[i].label, juce::Rectangle<int> (b.getCentreX() - b.getWidth(), y, b.getWidth() * 2, rowHeight),
                          juce::Justification::centred, 1, 0.8f);
    }

    // Where a fader puts a given dB, so the curve lines up with the caps
    const auto* first = faders.front().slider.get();
    const auto& info = *faders.front().info;
    auto yFor = [&] (double dB)
    {
        const auto clamped = static_cast<float> (juce::jlimit (static_cast<double> (info.minValue) - 3.0,
                                                               static_cast<double> (info.maxValue) + 3.0, dB));
        const double pos = first->getPositionOfValue (rig::toNormalised (info, juce::jlimit (info.minValue, info.maxValue, clamped)));
        // Beyond the range: extend the scale past the ends
        const double perDb = (first->getPositionOfValue (1.0) - first->getPositionOfValue (0.0)) / (info.maxValue - info.minValue);
        const double extra = (clamped - juce::jlimit (info.minValue, info.maxValue, clamped)) * perDb;
        return static_cast<float> (first->getY() + pos + extra);
    };

    // Octave bands: frequency maps to x on a log scale through the faders' centres
    const float x0 = static_cast<float> (faders[0].slider->getBounds().getCentreX());
    const float spacing = static_cast<float> (faders[1].slider->getBounds().getCentreX()) - x0;
    const float left = x0 - spacing * 0.5f;
    const float right = static_cast<float> (faders[static_cast<size_t> (numBands - 1)].slider->getBounds().getCentreX()) + spacing * 0.5f;

    std::array<float, fx::graphicEq::numBands> gains {};
    for (int b = 0; b < numBands; ++b)
    {
        const auto& f = faders[static_cast<size_t> (b)];
        gains[static_cast<size_t> (b)] = rig::toPlain (*f.info, static_cast<float> (f.slider->getValue()));
    }

    // 0 dB line
    g.setColour (colours.ink.withAlpha (0.25f));
    g.drawHorizontalLine (juce::roundToInt (yFor (0.0)), left, right);

    juce::Path curve;
    bool started = false;
    for (float x = left; x <= right; x += 1.0f)
    {
        const double octaves = (x - x0) / spacing;
        const double hz = fx::graphicEq::bandsHz[0] * std::exp2 (octaves);
        const float y = yFor (fx::graphicEq::responseDb (gains, hz));
        if (started)
            curve.lineTo (x, y);
        else
            curve.startNewSubPath (x, y);
        started = true;
    }
    g.setColour (colours.ink.withAlpha (0.85f));
    g.strokePath (curve, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

void SlotView::resized()
{
    auto b = getLocalBounds();
    typeBox.setBounds (b.removeFromTop (typeBoxHeight).reduced (2, 0));
    b.removeFromTop (4);
    b.reduce (8, 8);

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
    const size_t cols = knobs.size() > 4 ? 3 : 2;
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
