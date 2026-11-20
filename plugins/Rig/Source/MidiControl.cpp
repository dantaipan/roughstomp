#include "MidiControl.h"

namespace
{
constexpr const char* settingsKey = "midiMappings";
} // namespace

//==============================================================================
MidiMappings::MidiMappings()
{
    juce::PropertiesFile::Options options;   // the view preference's file
    options.applicationName = "RoughStomp";
    options.filenameSuffix = "settings";
    options.folderName = "RoughStomp";
    options.osxLibrarySubFolder = "Application Support";
    file = std::make_unique<juce::PropertiesFile> (options);

    if (auto xml = file->getXmlValue (settingsKey))
        for (const auto* e : xml->getChildWithTagNameIterator ("Map"))
        {
            const int cc = e->getIntAttribute ("cc", -1);
            const auto id = e->getStringAttribute ("param");
            if (isMappable (cc) && id.isNotEmpty())
                mappings.push_back ({ cc, id, e->getBoolAttribute ("toggle") });
        }
}

std::optional<MidiMappings::Mapping> MidiMappings::forParameter (const juce::String& paramId) const
{
    for (const auto& m : mappings)
        if (m.paramId == paramId)
            return m;
    return std::nullopt;
}

void MidiMappings::assign (int cc, const juce::String& paramId, bool toggle)
{
    if (! isMappable (cc) || paramId.isEmpty())
        return;
    std::erase_if (mappings, [&] (const Mapping& m) { return m.cc == cc || m.paramId == paramId; });
    mappings.push_back ({ cc, paramId, toggle });
    save();
}

void MidiMappings::forget (const juce::String& paramId)
{
    if (std::erase_if (mappings, [&] (const Mapping& m) { return m.paramId == paramId; }) > 0)
        save();
}

void MidiMappings::setToggle (const juce::String& paramId, bool toggle)
{
    for (auto& m : mappings)
        if (m.paramId == paramId && m.toggle != toggle)
        {
            m.toggle = toggle;
            save();
        }
}

void MidiMappings::save()
{
    juce::XmlElement xml ("MidiMappings");
    for (const auto& m : mappings)
    {
        auto* e = xml.createNewChildElement ("Map");
        e->setAttribute ("cc", m.cc);
        e->setAttribute ("param", m.paramId);
        if (m.toggle)
            e->setAttribute ("toggle", true);
    }
    // Another RoughStomp may have changed other settings since: reload, then save
    if (remembered)
    {
        file->reload();
        file->setValue (settingsKey, &xml);
        file->saveIfNeeded();
    }
    sendChangeMessage();
}

//==============================================================================
MidiControl::MidiControl (juce::AudioProcessorValueTreeState& state) : apvts (state)
{
    lastValue.fill (0);
    rebuild();
    mappings->addChangeListener (this);
}

MidiControl::~MidiControl()
{
    mappings->removeChangeListener (this);
}

void MidiControl::changeListenerCallback (juce::ChangeBroadcaster*)
{
    rebuild();
}

void MidiControl::rebuild()
{
    for (auto& t : targets)
        t.store (nullptr);
    for (const auto& m : mappings->get())
        if (auto* p = apvts.getParameter (m.paramId); p != nullptr && MidiMappings::isMappable (m.cc))
        {
            const auto i = static_cast<size_t> (m.cc);
            toggles[i].store (m.toggle);
            targets[i].store (p);
        }
}

void MidiControl::process (const juce::MidiBuffer& midi) noexcept
{
    for (const auto metadata : midi)
    {
        const auto message = metadata.getMessage();

        if (message.isProgramChange())
        {
            program.store (message.getProgramChangeNumber());
            continue;
        }
        if (! message.isController())
            continue;

        const int cc = message.getControllerNumber();
        const int value = message.getControllerValue();
        if (! MidiMappings::isMappable (cc))
            continue;
        const auto i = static_cast<size_t> (cc);
        const int previous = std::exchange (lastValue[i], value);

        if (listening.load())
        {
            // The first CC to arrive is the one; note which side of 64 it goes
            int expected = -1;
            if (learnedCc.compare_exchange_strong (expected, cc) || expected == cc)
                learnedValues.fetch_or (value >= 64 ? 2 : 1);
            continue;
        }

        auto* p = targets[i].load();
        if (p == nullptr)
            continue;

        float normalised = static_cast<float> (value) / 127.0f;
        if (toggles[i].load())
        {
            if (value < 64 || previous >= 64)
                continue;   // only a press flips it
            normalised = p->getValue() >= 0.5f ? 0.0f : 1.0f;
        }
        if (! juce::approximatelyEqual (p->getValue(), p->convertTo0to1 (p->convertFrom0to1 (normalised))))
            p->setValueNotifyingHost (normalised);
    }
}

void MidiControl::learn (const juce::String& paramId)
{
    listening.store (false);
    learning = paramId;
    learnedCc.store (-1);
    learnedValues.store (0);
    learnedAt = 0.0;
    listening.store (paramId.isNotEmpty());
}

bool MidiControl::poll()
{
    if (learning.isEmpty())
        return false;
    const int cc = learnedCc.load();
    if (cc < 0)
        return false;

    const double now = juce::Time::getMillisecondCounterHiRes();
    if (learnedAt <= 0.0)
        learnedAt = now;
    if (now - learnedAt < 400.0)
        return false;

    // A switch flips on each press if the footswitch is momentary (a press
    // and a release came in); otherwise it follows the controller
    const auto* p = apvts.getParameter (learning);
    const bool isSwitch = p != nullptr && p->isBoolean();
    mappings->assign (cc, learning, isSwitch && learnedValues.load() == 3);
    learn ({});
    return true;
}
