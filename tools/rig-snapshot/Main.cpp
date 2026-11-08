// rs-rig-snapshot: save a picture of the RoughStomp rig's editor.
//
//   rs-rig-snapshot <out.png> [id=value] [Rig.key=value] [--scale=2] [--tone=HZ]
//                   [--preset=NAME] [--save-preset=NAME] [--swap=pre1,loop3]
//                   [--toggle=PARAM] [--rate=HZ] [--async-amp=FILE.nam]
//
// Settings work as in rs-host-render: id=value sets a parameter in its own
// units, Rig.key=value an attribute of the <Rig> node (e.g. Rig.loop1=delay).
// --tone plays a sine at that pitch through the rig with the tuner on and
// prints what the tuner reads (and the output level, which should be muted).
// --preset loads a preset (after the settings), --save-preset saves the rig as
// a user preset; either prints the preset state afterwards. --toggle plays a
// tone, flips a switch parameter halfway (e.g. ampOn) and prints how big the
// worst jump at the switch is next to the steady signal (a click shows up as
// well above 1).

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
    double toneHz = 0.0, rate = 48000.0;
    juce::String toggleParam, asyncAmp;
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
        if (a.startsWith ("--async-amp="))
        {
            asyncAmp = a.fromFirstOccurrenceOf ("=", false, false);
            continue;
        }
        if (a.startsWith ("--toggle="))
        {
            toggleParam = a.fromFirstOccurrenceOf ("=", false, false);
            continue;
        }
        if (a.startsWith ("--rate="))
        {
            rate = a.fromFirstOccurrenceOf ("=", false, false).getDoubleValue();
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

    processor->setPlayConfigDetails (1, 2, rate, 512);
    juce::AudioProcessor::copyXmlToBinary (*state, data);
    processor->setStateInformation (data.getData(), static_cast<int> (data.getSize()));
    processor->prepareToPlay (rate, 512);

    if (asyncAmp.isNotEmpty())
    {
        auto* rig = dynamic_cast<RigProcessor*> (processor.get());
        const juce::File file (asyncAmp);
        auto ms = [] { return juce::Time::getMillisecondCounterHiRes(); };

        auto t0 = ms();
        rig->loadAmp (rig::AmpStage::amp, file);
        std::cout << "blocking load: the call took " << juce::String (ms() - t0, 1) << " ms\n";
        rig->clearAmp (rig::AmpStage::amp);

        // A first load straight away superseded by a second (same file here):
        // only the second should land, and the calls should return at once
        int doneCount = 0;
        juce::String lastError = "?";
        t0 = ms();
        rig->loadAmpAsync (rig::AmpStage::amp, file, RigProcessor::AmpLoad::keepOld, [&] (const juce::String& e) { ++doneCount; lastError = e; });
        rig->loadAmpAsync (rig::AmpStage::amp, file, RigProcessor::AmpLoad::keepOld, [&] (const juce::String& e) { ++doneCount; lastError = e; });
        std::cout << "background load: the calls took " << juce::String (ms() - t0, 2) << " ms; panel says \""
                  << rig->getAmpProblem (rig::AmpStage::amp) << "\" for " << rig->getLoadingAmpFile (rig::AmpStage::amp).getFileName() << "\n";
        while (rig->getLoadingAmpFile (rig::AmpStage::amp) != juce::File() && ms() - t0 < 20000)
            juce::MessageManager::getInstance()->runDispatchLoopUntil (20);
        juce::MessageManager::getInstance()->runDispatchLoopUntil (300);   // let the first (stale) one report too, if it would
        std::cout << "  loaded after " << juce::String (ms() - t0, 0) << " ms: " << rig->getAmpFile (rig::AmpStage::amp).getFileName()
                  << ", callbacks run: " << doneCount << ", error: \"" << lastError << "\", latency " << rig->getLatencySamples() << "\n";

        // A bad file keeps the old amp
        rig->loadAmpAsync (rig::AmpStage::amp, juce::File ("/nonexistent.nam"), RigProcessor::AmpLoad::keepOld, [&] (const juce::String& e) { lastError = e; });
        while (rig->getLoadingAmpFile (rig::AmpStage::amp) != juce::File())
            juce::MessageManager::getInstance()->runDispatchLoopUntil (20);
        std::cout << "bad file: error \"" << lastError << "\", still playing " << rig->getAmpFile (rig::AmpStage::amp).getFileName() << "\n";
    }

    if (toggleParam.isNotEmpty())
    {
        auto* param = processor->getParameters()[0];
        for (auto* p : processor->getParameters())
            if (auto* withId = dynamic_cast<juce::AudioProcessorParameterWithID*> (p); withId != nullptr && withId->paramID == toggleParam)
                param = p;

        juce::AudioBuffer<float> buffer (2, 512);
        juce::MidiBuffer midi;
        std::vector<float> out;
        double phase = 0.0;
        const int blocks = static_cast<int> (rate * 2.0 / 512);
        for (int b = 0; b < blocks; ++b)
        {
            if (b == blocks / 2)
                param->setValueNotifyingHost (param->getValue() > 0.5f ? 0.0f : 1.0f);
            for (int i = 0; i < 512; ++i)
            {
                buffer.setSample (0, i, static_cast<float> (0.2 * std::sin (phase)));
                buffer.setSample (1, i, 0.0f);
                phase += 2.0 * juce::MathConstants<double>::pi * 220.0 / rate;
            }
            processor->processBlock (buffer, midi);
            out.insert (out.end(), buffer.getReadPointer (0), buffer.getReadPointer (0) + 512);
        }

        auto jerk = [&] (size_t from, size_t to)
        {
            double worst = 0.0;
            for (size_t i = std::max<size_t> (from, 2); i < to; ++i)
                worst = std::max (worst, std::abs (static_cast<double> (out[i]) - 2.0 * out[i - 1] + out[i - 2]));
            return worst;
        };
        const size_t half = static_cast<size_t> (blocks / 2) * 512, q = static_cast<size_t> (rate / 4);
        const double steady = std::max (jerk (half - q, half), jerk (half + q, half + 2 * q));
        std::cout << toggleParam << " switched: worst jump " << jerk (half, half + static_cast<size_t> (rate * 0.05)) / steady
                  << "x the steady signal's\n";
    }

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
