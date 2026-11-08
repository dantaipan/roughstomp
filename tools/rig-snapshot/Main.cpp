// rs-rig-snapshot: save a picture of the RoughStomp rig's editor.
//
//   rs-rig-snapshot <out.png> [id=value] [Rig.key=value] [--scale=2]
//
// Settings work as in rs-host-render: id=value sets a parameter in its own
// units, Rig.key=value an attribute of the <Rig> node (e.g. Rig.loop1=delay).

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
    for (int i = 2; i < argc; ++i)
    {
        const juce::String a (argv[i]);
        if (a.startsWith ("--scale="))
        {
            scale = a.fromFirstOccurrenceOf ("=", false, false).getFloatValue();
            continue;
        }

        const auto key = a.upToFirstOccurrenceOf ("=", false, false);
        const auto value = a.fromFirstOccurrenceOf ("=", false, false);
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
