// rs-rig-snapshot: save a picture of the RoughStomp rig's editor.
//
//   rs-rig-snapshot <out.png> [id=value] [Rig.key=value] [--scale=2] [--tone=HZ]
//                   [--preset=NAME] [--save-preset=NAME] [--swap=pre1,loop3]
//
// Settings work as in rs-host-render: id=value sets a parameter in its own
// units, Rig.key=value an attribute of the <Rig> node (e.g. Rig.loop1=delay).
// --tone plays a sine at that pitch through the rig with the tuner on and
// prints what the tuner reads (and the output level, which should be muted).
// --preset loads a preset (after the settings), --save-preset saves the rig as
// a user preset; either prints the preset state afterwards.

#include "Presets.h"
#include "RigProcessor.h"

#include <iostream>

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter();

int main (int argc, char* argv[])
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    if (argc < 2)
    {
        std::cout << "Usage: rs-rig-snapshot <out.png> [id=value] [Rig.key=value] [--scale=2]\n";
        return 1;
    }

    std::unique_ptr<juce::AudioProcessor> processor (createPluginFilter());
    processor->setPlayConfigDetails (1, 2, 48000.0, 512);

    juce::MemoryBlock data;
    processor->getStateInformation (data);
    auto state = juce::AudioProcessor::getXmlFromBinary (data.getData(), static_cast<int> (data.getSize()));

    float scale = 1.0f;
    double toneHz = 0.0;
    juce::StringArray presetActions;   // "load:NAME" / "save:NAME", in order
    for (int i = 2; i < argc; ++i)
    {
        const juce::String a (argv[i]);
        if (a.startsWith ("--scale="))
        {
            scale = a.fromFirstOccurrenceOf ("=", false, false).getFloatValue();
            continue;
        }
        if (a.startsWith ("--preset=") || a.startsWith ("--save-preset="))
        {
            presetActions.add ((a.startsWith ("--preset=") ? "load:" : "save:") + a.fromFirstOccurrenceOf ("=", false, false));
            continue;
        }
        if (a.startsWith ("--swap="))
        {
            presetActions.add ("swap:" + a.fromFirstOccurrenceOf ("=", false, false));
            continue;
        }
        if (a.startsWith ("--tone="))
        {
            toneHz = a.fromFirstOccurrenceOf ("=", false, false).getDoubleValue();
            continue;
        }

        const auto key = a.upToFirstOccurrenceOf ("=", false, false);
        const auto value = a.fromFirstOccurrenceOf ("=", false, false);
        if (key.startsWith ("State."))   // an attribute of the state itself, e.g. State.uiScale=0.8
        {
            state->setAttribute (key.fromFirstOccurrenceOf (".", false, false), value);
            continue;
        }
        if (key.startsWith ("Rig."))
        {
            auto* rig = state->getChildByName ("Rig");
            if (rig == nullptr)
                rig = state->createNewChildElement ("Rig");
            rig->setAttribute (key.fromFirstOccurrenceOf (".", false, false), value);
            continue;
        }

        bool found = false;
        for (auto* e : state->getChildWithTagNameIterator ("PARAM"))
            if (e->getStringAttribute ("id") == key)
            {
                e->setAttribute ("value", value.getFloatValue());
                found = true;
            }
        if (! found)
        {
            std::cerr << "rs-rig-snapshot: unknown parameter '" << key << "'\n";
            return 1;
        }
    }

    juce::AudioProcessor::copyXmlToBinary (*state, data);
    processor->setStateInformation (data.getData(), static_cast<int> (data.getSize()));
    processor->prepareToPlay (48000.0, 512);

    if (auto* rig = dynamic_cast<RigProcessor*> (processor.get()); rig != nullptr && ! presetActions.isEmpty())
    {
        auto& presets = rig->getPresets();
        for (const auto& action : presetActions)
        {
            const auto name = action.fromFirstOccurrenceOf (":", false, false);
            juce::String error = "no preset called " + name;
            if (action.startsWith ("swap:"))
            {
                auto slotOf = [] (const juce::String& t)
                {
                    return std::pair { t.startsWith ("pre") ? rig::Section::pre : rig::Section::loop,
                                       t.retainCharacters ("0123456789").getIntValue() - 1 };
                };
                const auto [sa, a] = slotOf (name.upToFirstOccurrenceOf (",", false, false));
                const auto [sb, b] = slotOf (name.fromFirstOccurrenceOf (",", false, false));
                rig->swapSlots (sa, a, sb, b);
                error = {};
            }
            else if (action.startsWith ("save:"))
                error = presets.save (name);
            else
                for (const auto& e : presets.list())
                    if (e.name == name)
                        error = presets.load (e);
            if (error.isNotEmpty())
            {
                std::cerr << "rs-rig-snapshot: " << action << ": " << error << "\n";
                return 1;
            }
            std::cout << action << " -> preset \"" << presets.getCurrentName() << "\""
                      << (presets.isCurrentFactory() ? " (factory)" : "") << (presets.isModified() ? " modified" : "") << "\n";
        }
        juce::String slots;
        for (auto s : { rig::Section::pre, rig::Section::loop })
            for (int i = 0; i < rig::slotsPerSection; ++i)
                if (const auto* info = rig->getSlotInfo (s, i))
                {
                    slots << rig::sectionKey (s) << (i + 1) << "=" << juce::String (std::string (info->id)) << "(";
                    for (size_t k = 0; k < info->params.size() && k < rig::knobsPerSlot; ++k)
                        slots << rig::valueText (info->params[k], rig::toPlain (info->params[k],
                                     rig->apvts.getParameter (rig::slotKnobID (s, i, static_cast<int> (k)).getParamID())->getValue()))
                              << (k + 1 < info->params.size() ? ", " : "");
                    slots << ") ";
                }
        std::cout << "slots: " << slots << "\n";
        std::cout << "amp file: " << rig->getAmpFile (rig::AmpStage::amp).getFileName() << "\n";
    }

    if (toneHz > 0.0)
    {
        auto* rig = dynamic_cast<RigProcessor*> (processor.get());
        rig->apvts.getParameter (ParamIDs::tuner.getParamID())->setValueNotifyingHost (1.0f);

        juce::AudioBuffer<float> buffer (2, 512);
        juce::MidiBuffer midi;
        double phase = 0.0;
        float outPeak = 0.0f;
        for (int b = 0; b < 48000 / 512; ++b)
        {
            for (int i = 0; i < 512; ++i)
            {
                buffer.setSample (0, i, static_cast<float> (0.3 * std::sin (phase)));
                buffer.setSample (1, i, 0.0f);
                phase += 2.0 * juce::MathConstants<double>::pi * toneHz / 48000.0;
            }
            processor->processBlock (buffer, midi);
            if (b > 10)
                outPeak = juce::jmax (outPeak, buffer.getMagnitude (0, 512));
            if (b % 8 == 7)
            {
                const auto r = rig->readTuner();
                const auto n = fx::Note::fromHz (r.hz);
                std::cout << "tuner: " << r.hz << " Hz, confidence " << r.confidence << ", "
                          << std::string (n.name) << n.octave << " " << n.cents << " cents\n";
            }
        }
        std::cout << "output peak while tuning: " << juce::Decibels::gainToDecibels (outPeak) << " dB\n";
    }

    std::unique_ptr<juce::AudioProcessorEditor> editor (processor->createEditorAndMakeActive());
    const auto image = editor->createComponentSnapshot (editor->getLocalBounds(), true, scale);

    const juce::File out (juce::File::getCurrentWorkingDirectory().getChildFile (argv[1]));
    out.deleteFile();
    juce::FileOutputStream stream (out);
    if (! stream.openedOk() || ! juce::PNGImageFormat().writeImageToStream (image, stream))
    {
        std::cerr << "rs-rig-snapshot: can't write " << out.getFullPathName() << "\n";
        return 1;
    }

    processor->editorBeingDeleted (editor.get());
    editor.reset();
    processor->releaseResources();
    return 0;
}
