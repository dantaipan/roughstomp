#include "Presets.h"

#include "RigProcessor.h"

namespace
{
using Values = std::vector<std::pair<const char*, float>>;   // control id -> value in its own units

struct SlotSetting
{
    int slot;
    const char* pedal;
    Values values;
};

struct FactoryPreset
{
    const char* name;
    std::vector<SlotSetting> pre, loop;
    Values rig;   // rig parameters by id (plain units)
};

// Starting points. Everything not listed is at its default; the amp and cab
// stay whatever is loaded.
const std::vector<FactoryPreset>& factoryPresets()
{
    static const std::vector<FactoryPreset> presets {
        { "Clean", {}, {
              { 0, "compressor", { { "sustain", 3.0f }, { "blend", 70.0f } } },
              { 1, "reverb", { { "decay", 1.6f }, { "mix", 18.0f }, { "mode", 1.0f } } } }, {} },
        { "Crunch", {
              { 0, "overdrive", { { "drive", 4.0f }, { "tone", 5.5f } } } }, {
              { 0, "reverb", { { "decay", 1.0f }, { "mix", 15.0f }, { "mode", 0.0f } } } },
          { { "ampGain", 3.0f } } },
        { "Lead", {
              { 0, "compressor", { { "sustain", 4.0f } } },
              { 1, "overdrive", { { "drive", 6.5f }, { "tone", 6.0f }, { "level", 2.0f } } } }, {
              { 0, "delay", { { "time", 380.0f }, { "feedback", 30.0f }, { "mix", 22.0f }, { "tone", 4500.0f } } },
              { 1, "reverb", { { "decay", 2.2f }, { "mix", 20.0f }, { "mode", 1.0f } } } },
          { { "ampGain", 4.0f } } },
        { "Ambient Swells", {}, {
              { 0, "chorus", { { "rate", 0.4f }, { "depth", 60.0f }, { "mix", 40.0f } } },
              { 1, "delay", { { "time", 520.0f }, { "feedback", 50.0f }, { "mix", 35.0f }, { "mode", 1.0f }, { "tone", 3500.0f } } },
              { 2, "reverb", { { "decay", 5.0f }, { "size", 80.0f }, { "mix", 40.0f }, { "mode", 2.0f } } } }, {} },
        { "Surf", {}, {
              { 0, "tremolo", { { "rate", 6.0f }, { "depth", 45.0f } } },
              { 1, "spring", { { "decay", 2.5f }, { "dwell", 7.0f }, { "mix", 45.0f } } } }, {} },
        { "Funk", {
              { 0, "compressor", { { "sustain", 6.0f }, { "attack", 5.0f } } },
              { 1, "wah", { { "mode", 1.0f }, { "sensitivity", 6.0f }, { "position", 10.0f }, { "resonance", 6.0f } } } }, {
              { 0, "phaser", { { "rate", 0.4f }, { "depth", 50.0f }, { "mix", 35.0f } } } }, {} },
        { "Blues", {
              { 0, "bluesdrive", { { "gain", 5.0f }, { "tone", 5.5f } } } }, {
              { 0, "reverb", { { "decay", 1.4f }, { "mix", 16.0f }, { "mode", 0.0f } } } },
          { { "ampGain", 3.0f } } },
        { "Slapback", {
              { 0, "bluesdrive", { { "gain", 2.5f }, { "tone", 6.0f } } } }, {
              { 0, "delay", { { "time", 110.0f }, { "feedback", 12.0f }, { "mix", 40.0f }, { "mode", 1.0f }, { "tone", 7000.0f } } },
              { 1, "spring", { { "decay", 1.5f }, { "mix", 15.0f } } } }, {} },
        { "Empty Board", {}, {}, {} },
    };
    return presets;
}

bool sameName (const juce::String& a, const juce::String& b) { return a.compareIgnoreCase (b) == 0; }
} // namespace

PresetManager::PresetManager (RigProcessor& p) : processor (p)
{
    baseline = fingerprint();
}

juce::File PresetManager::getFolder()
{
    auto base = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory);
   #if JUCE_MAC
    base = base.getChildFile ("Application Support");   // JUCE's "app data" on macOS is ~/Library itself
   #endif
    return base.getChildFile ("RoughStomp").getChildFile ("Presets");
}

std::vector<PresetManager::Entry> PresetManager::list() const
{
    std::vector<Entry> entries;
    const auto& factory = factoryPresets();
    for (size_t i = 0; i < factory.size(); ++i)
        entries.push_back ({ factory[i].name, true, static_cast<int> (i), {} });

    auto files = getFolder().findChildFiles (juce::File::findFiles, false, juce::String ("*") + extension);
    std::sort (files.begin(), files.end(), [] (const juce::File& a, const juce::File& b)
    {
        return a.getFileNameWithoutExtension().compareNatural (b.getFileNameWithoutExtension()) < 0;
    });
    for (const auto& f : files)
        entries.push_back ({ f.getFileNameWithoutExtension(), false, -1, f });
    return entries;
}

juce::String PresetManager::load (const Entry& e)
{
    if (! e.factory)
    {
        auto xml = juce::XmlDocument::parse (e.file);
        if (xml == nullptr || ! xml->hasTagName ("RoughStompPreset") || xml->getFirstChildElement() == nullptr)
            return "It isn't a RoughStomp preset.";
        processor.restoreState (*xml->getFirstChildElement(), RigProcessor::Files::restoreInBackground);
        loaded (e.name, false);
        return {};
    }

    const auto& preset = factoryPresets()[static_cast<size_t> (e.factoryIndex)];
    auto& apvts = processor.apvts;

    // From a clean board: every parameter at its default, every slot empty
    for (auto* p : processor.getParameters())
        if (auto* withId = dynamic_cast<juce::AudioProcessorParameterWithID*> (p); withId != nullptr && withId->paramID != ParamIDs::tuner.getParamID())
            withId->setValueNotifyingHost (withId->getDefaultValue());
    for (auto s : { rig::Section::pre, rig::Section::loop })
        for (int i = 0; i < rig::slotsPerSection; ++i)
            processor.setSlotPedal (s, i, {});

    auto place = [&] (rig::Section s, const SlotSetting& setting)
    {
        processor.setSlotPedal (s, setting.slot, setting.pedal);   // starts on the pedal's defaults
        const auto* info = processor.getSlotInfo (s, setting.slot);
        if (info == nullptr)
            return;
        for (const auto& [id, value] : setting.values)
        {
            const int k = info->indexOf (id);
            if (k < 0 || k >= rig::knobsPerSlot)
                continue;
            if (auto* p = apvts.getParameter (rig::slotKnobID (s, setting.slot, k).getParamID()))
                p->setValueNotifyingHost (info->params[static_cast<size_t> (k)].toNormalised (value));
        }
    };
    for (const auto& s : preset.pre)
        place (rig::Section::pre, s);
    for (const auto& s : preset.loop)
        place (rig::Section::loop, s);
    for (const auto& [id, value] : preset.rig)
        if (auto* p = apvts.getParameter (id))
            p->setValueNotifyingHost (p->convertTo0to1 (value));

    loaded (preset.name, true);
    return {};
}

juce::String PresetManager::save (const juce::String& name)
{
    const auto clean = juce::File::createLegalFileName (name.trim());
    if (clean.isEmpty())
        return "Give the preset a name.";
    for (const auto& f : factoryPresets())
        if (sameName (clean, f.name))
            return "\"" + clean + "\" is a factory preset's name. Pick another.";

    const auto folder = getFolder();
    if (! folder.createDirectory())
        return "Couldn't create " + folder.getFullPathName();

    processor.setPresetName (clean, false);
    auto state = processor.captureState();
    state->removeAttribute ("uiScale");   // a preset shouldn't resize the window
    juce::XmlElement root ("RoughStompPreset");
    root.setAttribute ("version", 1);
    root.addChildElement (state.release());

    const auto file = folder.getChildFile (clean + extension);
    if (! root.writeTo (file))
        return "Couldn't write " + file.getFullPathName();

    loaded (clean, false);
    return {};
}

bool PresetManager::remove (const Entry& e)
{
    if (e.factory || ! e.file.deleteFile())
        return false;
    if (sameName (e.name, getCurrentName()) && ! currentIsFactory)
        processor.setPresetName ({}, false);
    return true;
}

void PresetManager::step (int delta)
{
    const auto entries = list();
    if (entries.empty())
        return;

    int current = -1;
    const auto name = getCurrentName();
    for (size_t i = 0; i < entries.size(); ++i)
        if (sameName (entries[i].name, name) && entries[i].factory == currentIsFactory)
            current = static_cast<int> (i);

    const int count = static_cast<int> (entries.size());
    const int next = current < 0 ? (delta > 0 ? 0 : count - 1) : ((current + delta) % count + count) % count;
    load (entries[static_cast<size_t> (next)]);
}

juce::String PresetManager::getCurrentName() const { return processor.getPresetName(); }
bool PresetManager::isCurrentFactory() const { return currentIsFactory; }

bool PresetManager::isModified()
{
    return processor.wasPresetModified() || fingerprint() != baseline;
}

void PresetManager::stateRestored()
{
    baseline = fingerprint();
    // Whether it's a factory preset isn't saved; go by the name
    currentIsFactory = false;
    for (const auto& f : factoryPresets())
        if (sameName (f.name, getCurrentName()))
            currentIsFactory = true;
}

void PresetManager::noteModifiedState()
{
    if (! processor.wasPresetModified() && fingerprint() != baseline)
        processor.setPresetName (getCurrentName(), true);
}

juce::String PresetManager::fingerprint()
{
    auto xml = processor.captureState();
    if (xml == nullptr)
        return {};
    if (auto* rigNode = xml->getChildByName ("Rig"))
    {
        rigNode->removeAttribute ("preset");
        rigNode->removeAttribute ("presetModified");
    }
    xml->removeAttribute ("uiScale");   // the window size isn't part of a sound
    // The tuner isn't part of a sound
    for (auto* e : xml->getChildWithTagNameIterator ("PARAM"))
        if (e->getStringAttribute ("id") == ParamIDs::tuner.getParamID())
        {
            xml->removeChildElement (e, true);
            break;
        }
    return xml->toString (juce::XmlElement::TextFormat().singleLine().withoutHeader());
}

void PresetManager::loaded (const juce::String& name, bool factory)
{
    processor.setPresetName (name, false);
    currentIsFactory = factory;
    baseline = fingerprint();
}
