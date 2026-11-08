// rs-host-render: render a WAV file through a built plugin (VST3 or AU),
// configured through its saved state the way a DAW session would restore it.
//
//   rs-host-render <plugin> <in.wav> <out.wav> [settings...] [--block=N] [--rate=HZ]
//
// Settings edit the plugin's state before it is restored:
//   id=value        a parameter, in its own units (e.g. input=6, pre1_k1=0.7)
//   Rig.key=value   an attribute of the <Rig> node (e.g. Rig.ampFile=/path.nam)
//
// The output is stereo 32-bit float, latency-compensated. Prints the plugin's
// latency and the output's peak and RMS level.

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_audio_processors/juce_audio_processors.h>

#include <cmath>
#include <iostream>

namespace
{
int fail (const juce::String& message)
{
    std::cerr << "rs-host-render: " << message << "\n";
    return 1;
}

juce::String option (const juce::StringArray& args, const juce::String& name, const juce::String& fallback)
{
    for (const auto& a : args)
        if (a.startsWith ("--" + name + "="))
            return a.fromFirstOccurrenceOf ("=", false, false);
    return fallback;
}

/** Sets a parameter in an APVTS-style state (<PARAM id=".." value=".."/>,
    plain units). False if the state has no such parameter. */
bool setParameter (juce::XmlElement& state, const juce::String& id, float plain)
{
    for (auto* e : state.getChildWithTagNameIterator ("PARAM"))
        if (e->getStringAttribute ("id") == id)
        {
            e->setAttribute ("value", plain);
            return true;
        }
    return false;
}

/** A JUCE plugin's own state, as hosted. For a VST3 the host wraps it: base64
    in <VST3PluginState><IComponent>, sometimes followed by JUCE's private
    data (bypass, program), which has to be kept. */
struct HostedState
{
    std::unique_ptr<juce::XmlElement> outer;   // the host's wrapper, or null
    std::unique_ptr<juce::XmlElement> plugin;  // the plugin's own XML
    juce::MemoryBlock trailer;                 // anything after the plugin's XML

    static HostedState read (const juce::MemoryBlock& data)
    {
        HostedState h;
        auto xml = juce::AudioProcessor::getXmlFromBinary (data.getData(), static_cast<int> (data.getSize()));
        if (xml == nullptr)
            return h;

        if (! xml->hasTagName ("VST3PluginState"))
        {
            h.plugin = std::move (xml);
            return h;
        }

        juce::MemoryBlock inner;
        if (auto* component = xml->getChildByName ("IComponent"); component == nullptr || ! inner.fromBase64Encoding (component->getAllSubText()))
            return h;

        // JUCE's binary XML: "VC2!", a 4-byte little-endian size, then the text
        if (inner.getSize() < 8)
            return h;
        const auto size = static_cast<size_t> (juce::ByteOrder::littleEndianInt (static_cast<const char*> (inner.getData()) + 4));
        const auto used = juce::jmin (inner.getSize(), 8 + size);

        h.plugin = juce::AudioProcessor::getXmlFromBinary (inner.getData(), static_cast<int> (used));
        h.trailer = juce::MemoryBlock (static_cast<const char*> (inner.getData()) + used, inner.getSize() - used);
        h.outer = std::move (xml);
        return h;
    }

    juce::MemoryBlock write() const
    {
        juce::MemoryBlock pluginData;
        juce::AudioProcessor::copyXmlToBinary (*plugin, pluginData);

        juce::MemoryBlock out;
        if (outer == nullptr)
            return pluginData;

        pluginData.append (trailer.getData(), trailer.getSize());
        auto wrapper = std::make_unique<juce::XmlElement> (*outer);
        auto* component = wrapper->getChildByName ("IComponent");
        component->deleteAllTextElements();
        component->addTextElement (pluginData.toBase64Encoding());
        juce::AudioProcessor::copyXmlToBinary (*wrapper, out);
        return out;
    }
};
} // namespace

int main (int argc, char* argv[])
{
    juce::ScopedJuceInitialiser_GUI juceInit;   // plugin hosting needs a message manager

    juce::StringArray args;
    for (int i = 1; i < argc; ++i)
        args.add (argv[i]);

    juce::StringArray positional;
    for (const auto& a : args)
        if (! a.startsWith ("--") && ! a.containsChar ('='))
            positional.add (a);

    if (positional.size() < 3)
    {
        std::cout << "Usage: rs-host-render <plugin.vst3|.component> <in.wav> <out.wav> [id=value] [Rig.key=value] [--block=N] [--rate=HZ]\n";
        return 1;
    }

    const juce::File pluginFile (juce::File::getCurrentWorkingDirectory().getChildFile (positional[0]));
    const juce::File inFile (juce::File::getCurrentWorkingDirectory().getChildFile (positional[1]));
    const juce::File outFile (juce::File::getCurrentWorkingDirectory().getChildFile (positional[2]));
    const int block = option (args, "block", "512").getIntValue();

    // Read the input
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (inFile));
    if (reader == nullptr)
        return fail ("can't read " + inFile.getFullPathName());

    const double rate = option (args, "rate", juce::String (reader->sampleRate)).getDoubleValue();
    const int length = static_cast<int> (reader->lengthInSamples);
    juce::AudioBuffer<float> input (1, length);
    reader->read (&input, 0, length, 0, true, false);   // first channel only: guitar is mono

    // Load the plugin
    juce::AudioPluginFormatManager pluginFormats;
    juce::addDefaultFormatsToManager (pluginFormats);

    juce::OwnedArray<juce::PluginDescription> types;
    for (auto* format : pluginFormats.getFormats())
        format->findAllTypesForFile (types, pluginFile.getFullPathName());
    if (types.isEmpty())
        return fail ("no plugin found in " + pluginFile.getFullPathName());

    juce::String error;
    auto plugin = pluginFormats.createPluginInstance (*types[0], rate, block, error);
    if (plugin == nullptr)
        return fail ("couldn't load the plugin: " + error);

    // Mono in, stereo out where the plugin allows it
    auto layout = plugin->getBusesLayout();
    if (! layout.inputBuses.isEmpty())  layout.inputBuses.getReference (0)  = juce::AudioChannelSet::mono();
    if (! layout.outputBuses.isEmpty()) layout.outputBuses.getReference (0) = juce::AudioChannelSet::stereo();
    plugin->setBusesLayout (layout);

    // Edit its state, then restore it like a session would
    juce::MemoryBlock stateData;
    plugin->getStateInformation (stateData);
    auto hosted = HostedState::read (stateData);
    if (hosted.plugin == nullptr)
        return fail ("couldn't read the plugin's state (only JUCE plugins with XML state are supported)");
    auto& state = hosted.plugin;

    for (const auto& a : args)
    {
        if (a.startsWith ("--") || ! a.containsChar ('='))
            continue;

        const auto key = a.upToFirstOccurrenceOf ("=", false, false);
        const auto value = a.fromFirstOccurrenceOf ("=", false, false);

        if (key.startsWith ("Rig."))
        {
            auto* rigNode = state->getChildByName ("Rig");
            if (rigNode == nullptr)
                rigNode = state->createNewChildElement ("Rig");
            rigNode->setAttribute (key.fromFirstOccurrenceOf (".", false, false), value);
        }
        else if (! setParameter (*state, key, value.getFloatValue()))
            return fail ("unknown parameter '" + key + "'");
    }

    const auto edited = hosted.write();
    plugin->setStateInformation (edited.getData(), static_cast<int> (edited.getSize()));

    plugin->prepareToPlay (rate, block);
    const int latency = plugin->getLatencySamples();

    // Process, with extra silence at the end to flush the latency
    const int numChannels = juce::jmax (plugin->getTotalNumInputChannels(), plugin->getTotalNumOutputChannels(), 2);
    const int total = length + latency;
    juce::AudioBuffer<float> output (2, length);
    juce::AudioBuffer<float> buffer (numChannels, block);
    juce::MidiBuffer midi;

    for (int pos = 0; pos < total; pos += block)
    {
        const int n = juce::jmin (block, total - pos);
        buffer.setSize (numChannels, n, false, false, true);
        buffer.clear();
        if (pos < length)
            buffer.copyFrom (0, 0, input, 0, pos, juce::jmin (n, length - pos));

        plugin->processBlock (buffer, midi);

        // Drop the first `latency` samples
        for (int i = 0; i < n; ++i)
        {
            const int outPos = pos + i - latency;
            if (outPos < 0 || outPos >= length)
                continue;
            for (int c = 0; c < 2; ++c)
                output.setSample (c, outPos, buffer.getSample (juce::jmin (c, plugin->getTotalNumOutputChannels() - 1), i));
        }
    }

    plugin->releaseResources();

    // Report
    float peak = 0.0f;
    double sumSquares = 0.0;
    for (int c = 0; c < 2; ++c)
        for (int i = 0; i < length; ++i)
        {
            const float s = output.getSample (c, i);
            peak = juce::jmax (peak, std::abs (s));
            sumSquares += static_cast<double> (s) * s;
        }
    const double rms = std::sqrt (sumSquares / (2.0 * juce::jmax (1, length)));
    std::cout << "latency " << latency << " samples, peak " << juce::Decibels::gainToDecibels (peak)
              << " dBFS, RMS " << juce::Decibels::gainToDecibels (static_cast<float> (rms)) << " dBFS\n";

    // Write
    outFile.deleteFile();
    std::unique_ptr<juce::OutputStream> stream (outFile.createOutputStream());
    if (stream == nullptr)
        return fail ("can't write " + outFile.getFullPathName());

    using Format = juce::AudioFormatWriterOptions::SampleFormat;
    const auto options = juce::AudioFormatWriterOptions{}
                             .withSampleRate (rate)
                             .withNumChannels (2)
                             .withBitsPerSample (32)
                             .withSampleFormat (Format::floatingPoint);

    juce::WavAudioFormat wav;
    auto writer = wav.createWriterFor (stream, options);
    if (writer == nullptr)
        return fail ("can't create a WAV writer for " + outFile.getFullPathName());
    writer->writeFromAudioSampleBuffer (output, 0, length);
    return 0;
}
