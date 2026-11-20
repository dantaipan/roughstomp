#include <catch2/catch_session.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "Presets.h"
#include "RigProcessor.h"

using Catch::Matchers::WithinAbs;

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter();

namespace
{
/** The process's MIDI mappings, kept for the whole run and never saved: the
    tests must not touch the user's own. */
juce::SharedResourcePointer<MidiMappings>* testMappings = nullptr;

MidiMappings& mappings() { return **testMappings; }

void clearMappings()
{
    while (! mappings().get().empty())
        mappings().forget (mappings().get().front().paramId);
}

void pump (int ms) { juce::MessageManager::getInstance()->runDispatchLoopUntil (ms); }

std::unique_ptr<RigProcessor> makeRig()
{
    std::unique_ptr<RigProcessor> rig (dynamic_cast<RigProcessor*> (createPluginFilter()));
    rig->setPlayConfigDetails (1, 2, 48000.0, 512);
    rig->prepareToPlay (48000.0, 512);
    return rig;
}

void send (RigProcessor& rig, std::initializer_list<juce::MidiMessage> messages)
{
    juce::AudioBuffer<float> buffer (2, 512);
    buffer.clear();
    juce::MidiBuffer midi;
    int t = 0;
    for (const auto& m : messages)
        midi.addEvent (m, t++);
    rig.processBlock (buffer, midi);
}

juce::MidiMessage cc (int number, int value) { return juce::MidiMessage::controllerEvent (1, number, value); }

float valueOf (RigProcessor& rig, const juce::String& id) { return rig.apvts.getParameter (id)->getValue(); }
float plainOf (RigProcessor& rig, const juce::String& id) { return rig.apvts.getRawParameterValue (id)->load(); }
} // namespace

int main (int argc, char* argv[])
{
    juce::ScopedJuceInitialiser_GUI juce;
    juce::SharedResourcePointer<MidiMappings> held;
    held->setRemembered (false);
    testMappings = &held;
    return Catch::Session().run (argc, argv);
}

//==============================================================================
TEST_CASE ("A mapped CC moves its control", "[midi]")
{
    clearMappings();
    auto rig = makeRig();
    mappings().assign (11, "output");
    pump (20);   // the mapping reaches the audio thread's table

    send (*rig, { cc (11, 127) });
    REQUIRE_THAT (valueOf (*rig, "output"), WithinAbs (1.0, 1.0e-6));
    REQUIRE_THAT (plainOf (*rig, "output"), WithinAbs (12.0, 1.0e-4));   // what the audio code reads, at once
    send (*rig, { cc (11, 0) });
    REQUIRE_THAT (valueOf (*rig, "output"), WithinAbs (0.0, 1.0e-6));

    // Unmapped CCs and bank select move nothing
    const float before = valueOf (*rig, "input");
    send (*rig, { cc (12, 100), cc (0, 100), cc (32, 100) });
    REQUIRE (juce::approximatelyEqual (valueOf (*rig, "input"), before));
}

TEST_CASE ("A switch follows the CC, or flips on each press", "[midi]")
{
    clearMappings();
    auto rig = makeRig();

    // Following: 64 and up is on
    mappings().assign (64, "gateOn");
    pump (20);
    send (*rig, { cc (64, 127) });
    REQUIRE (valueOf (*rig, "gateOn") > 0.5f);
    send (*rig, { cc (64, 0) });
    REQUIRE (valueOf (*rig, "gateOn") < 0.5f);

    // Flipping: a momentary footswitch's press flips it, the release doesn't
    mappings().assign (65, "pre1_on", true);
    pump (20);
    REQUIRE (valueOf (*rig, "pre1_on") > 0.5f);
    send (*rig, { cc (65, 127), cc (65, 0) });
    REQUIRE (valueOf (*rig, "pre1_on") < 0.5f);
    send (*rig, { cc (65, 127) });
    send (*rig, { cc (65, 0) });
    REQUIRE (valueOf (*rig, "pre1_on") > 0.5f);
}

TEST_CASE ("A program change loads that preset in the menu's order", "[midi]")
{
    auto rig = makeRig();
    const auto entries = rig->getPresets().list();
    REQUIRE (entries.size() > 2);

    send (*rig, { juce::MidiMessage::programChange (1, 2) });
    pump (150);   // the processor's timer picks it up
    REQUIRE (rig->getPresetName() == entries[2].name);

    // Past the end: nothing changes
    send (*rig, { juce::MidiMessage::programChange (1, 127) });
    pump (150);
    REQUIRE (rig->getPresetName() == entries[2].name);
}

TEST_CASE ("MIDI Learn maps the next CC, telling momentary footswitches apart", "[midi]")
{
    clearMappings();
    auto rig = makeRig();
    auto& midi = rig->getMidi();

    // A knob: the learning CC moves nothing; it's mapped once it settles
    const float gain = valueOf (*rig, "ampGain");
    midi.learn ("ampGain");
    send (*rig, { cc (20, 30), cc (20, 31) });
    REQUIRE (juce::approximatelyEqual (valueOf (*rig, "ampGain"), gain));
    pump (600);
    REQUIRE (midi.getLearning().isEmpty());
    auto m = mappings().forParameter ("ampGain");
    REQUIRE (m.has_value());
    REQUIRE (m->cc == 20);
    REQUIRE_FALSE (m->toggle);

    // A momentary footswitch (press and release) flips its switch...
    midi.learn ("pre2_on");
    send (*rig, { cc (21, 127) });
    send (*rig, { cc (21, 0) });
    pump (600);
    m = mappings().forParameter ("pre2_on");
    REQUIRE (m.has_value());
    REQUIRE (m->toggle);

    // ...a latching one (just one message) follows it
    midi.learn ("pre3_on");
    send (*rig, { cc (22, 127) });
    pump (600);
    m = mappings().forParameter ("pre3_on");
    REQUIRE (m.has_value());
    REQUIRE_FALSE (m->toggle);

    // Learning a CC another control had moves it over
    midi.learn ("output");
    send (*rig, { cc (20, 64) });
    pump (600);
    REQUIRE (mappings().forParameter ("output")->cc == 20);
    REQUIRE_FALSE (mappings().forParameter ("ampGain").has_value());
}

//==============================================================================
TEST_CASE ("Amp tone: flat by default, kept by factory presets, flat in old sessions", "[tone]")
{
    auto rig = makeRig();
    for (const auto* id : { "ampBass", "ampMiddle", "ampTreble", "ampBBass", "ampBMiddle", "ampBTreble" })
        REQUIRE_THAT (plainOf (*rig, id), WithinAbs (0.0, 1.0e-6));
    REQUIRE_THAT (plainOf (*rig, "cabLowCut"), WithinAbs (fx::CutFilters::lowCutMinHz, 1.0e-3));
    REQUIRE_THAT (plainOf (*rig, "cabHighCut"), WithinAbs (fx::CutFilters::highCutMaxHz, 1.0e-2));
    REQUIRE (rig->apvts.getParameter ("cabLowCut")->getCurrentValueAsText() == "Off");
    REQUIRE (rig->apvts.getParameter ("cabHighCut")->getCurrentValueAsText() == "Off");

    // Factory presets keep the amps' tone and cuts (with the captures they suit)
    auto set = [&] (const char* id, float plain)
    {
        auto* p = rig->apvts.getParameter (id);
        p->setValueNotifyingHost (p->convertTo0to1 (plain));
    };
    set ("ampBass", 5.0f);
    set ("cabBHighCut", 6000.0f);
    set ("ampGain", 7.0f);
    rig->getPresets().load (rig->getPresets().list().front());
    REQUIRE_THAT (plainOf (*rig, "ampBass"), WithinAbs (5.0, 0.05));
    REQUIRE_THAT (plainOf (*rig, "cabBHighCut"), WithinAbs (6000.0, 1.0));
    REQUIRE_THAT (plainOf (*rig, "ampGain"), WithinAbs (0.0, 1.0e-4));   // the rest goes to the preset's

    // A session saved before these existed comes back flat
    auto xml = rig->captureState();
    for (auto* child = xml->getFirstChildElement(); child != nullptr;)
    {
        auto* next = child->getNextElement();
        if (ParamIDs::isAmpTone (child->getStringAttribute ("id")))
            xml->removeChildElement (child, true);
        child = next;
    }
    rig->restoreState (*xml, RigProcessor::Files::keepCurrent);
    REQUIRE_THAT (plainOf (*rig, "ampBass"), WithinAbs (0.0, 1.0e-6));
    REQUIRE_THAT (plainOf (*rig, "cabBHighCut"), WithinAbs (fx::CutFilters::highCutMaxHz, 1.0e-2));
}
