#include "SlotView.h"

namespace
{
constexpr int emptyItemId = 1;   // menu ids: 1 = empty, 2.. = registry order
constexpr int typeBoxHeight = 24;
constexpr int footHeight = 54;
constexpr int knobHeight = 66;
constexpr int choiceHeight = 22;
} // namespace

rs::ui::PedalColours SlotView::coloursFor (const fx::PedalInfo* info)
{
    rs::ui::PedalColours c;   // red, the Overdrive's colour
    if (info != nullptr && info->id == "boost")
    {
        c.body     = juce::Colour (0xff2f6db5);
        c.bodyDark = juce::Colour (0xff1f4c80);
    }
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

            if (info.kind == fx::ParamInfo::Kind::continuous)
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

void SlotView::paint (juce::Graphics& g)
{
    auto body = getLocalBounds().toFloat().withTrimmedTop (static_cast<float> (typeBoxHeight + 4)).reduced (2.0f);

    if (shown == nullptr)
    {
        juce::Path outline;
        outline.addRoundedRectangle (body, 12.0f);
        const float dashes[] { 6.0f, 5.0f };
        juce::PathStrokeType (1.5f).createDashedStroke (outline, outline, dashes, 2);
        g.setColour (juce::Colours::white.withAlpha (0.18f));
        g.fillPath (outline);
        g.setFont (rs::ui::pedalFont (12.0f, false));
        g.drawText ("empty", body, juce::Justification::centred);
        return;
    }

    g.setGradientFill (juce::ColourGradient (colours.body, body.getTopLeft(), colours.bodyDark, body.getBottomRight(), false));
    g.fillRoundedRectangle (body, 12.0f);
    g.setColour (juce::Colours::black.withAlpha (0.5f));
    g.drawRoundedRectangle (body, 12.0f, 1.5f);

    // Pedal name above the footswitch
    g.setColour (colours.ink);
    g.setFont (rs::ui::pedalFont (13.0f));
    g.drawText (juce::String (std::string (shown->name)).toUpperCase(),
                body.withTrimmedBottom (static_cast<float> (footHeight)).removeFromBottom (18.0f),
                juce::Justification::centred);
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

    for (auto& c : choices)
        c.box->setBounds (b.removeFromBottom (choiceHeight).reduced (4, 0));

    // Knobs two to a row; an odd one out is centred
    const int cols = knobs.size() == 1 ? 1 : 2;
    const int w = b.getWidth() / 2;
    for (size_t i = 0; i < knobs.size(); i += static_cast<size_t> (cols))
    {
        auto row = b.removeFromTop (knobHeight);
        const bool single = cols == 1 || i + 1 == knobs.size();
        if (single)
            knobs[i]->setBounds (row.withSizeKeepingCentre (w, knobHeight));
        else
        {
            knobs[i]->setBounds (row.removeFromLeft (w));
            knobs[i + 1]->setBounds (row);
        }
    }
}
