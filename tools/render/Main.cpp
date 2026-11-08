// rs-render: process a WAV file through an fx-core effect.
//
//   rs-render <effect> <in.wav> <out.wav> [name=value ...] [--block=N] [--24bit] [--bpm=N]
//   rs-render --list
//
// <effect> is any pedal in fx::registry (overdrive, delay, reverb, ...), with
// its controls by id in their own units (e.g. time=380 feedback=30), or
// "gain" (db=...). Controls not given stay at the pedal's defaults.
//
// The output is latency-compensated (lines up sample-for-sample with the
// input), so you can A/B or null-test it against the input or another render.
// Writes 32-bit float WAV by default so nothing clips.

#include <juce_audio_formats/juce_audio_formats.h>

#include "fx/Pedal.h"
#include "fx/effects/Gain.h"

#include <functional>
#include <iostream>
#include <map>
#include <memory>

namespace
{
using Params = std::map<juce::String, float>;

struct EffectInfo
{
    const char* params; // help text
    std::function<std::unique_ptr<fx::Effect> (const Params&)> create;
};

float param (const Params& p, const char* name, float fallback)
{
    const auto it = p.find (name);
    return it != p.end() ? it->second : fallback;
}

// Effects that aren't pedals (pedals come from fx::registry).
const std::map<juce::String, EffectInfo>& registry()
{
    static const std::map<juce::String, EffectInfo> effects {
        { "gain", { "db=<-100..24> (default 0)",
                    [] (const Params& p)
                    {
                        auto g = std::make_unique<fx::Gain>();
                        g->setGainDecibels (param (p, "db", 0.0f));
                        return g;
                    } } },
    };
    return effects;
}

int usage()
{
    std::cerr << "Usage: rs-render <effect> <in.wav> <out.wav> [name=value ...] [--block=N] [--24bit] [--bpm=N]\n"
                 "       rs-render --list\n";
    return 1;
}

int listEffects()
{
    for (const auto* info : fx::registry::all())
    {
        std::cout << "  " << info->id << "  ";
        for (const auto& p : info->params)
        {
            std::cout << " " << p.id << "=<" << p.minValue << ".." << p.maxValue;
            if (! p.unit.empty())
                std::cout << " " << p.unit;
            std::cout << ", " << p.defaultValue << ">";
        }
        std::cout << "\n";
    }
    for (const auto& [name, info] : registry())
        std::cout << "  " << name << "   " << info.params << "\n";
    return 0;
}

int fail (const juce::String& message)
{
    std::cerr << "rs-render: " << message << "\n";
    return 1;
}
} // namespace

int main (int argc, char* argv[])
{
    juce::StringArray args;
    for (int i = 1; i < argc; ++i)
        args.add (argv[i]);

    if (args.contains ("--list"))
        return listEffects();

    if (args.size() < 3)
        return usage();

    const auto* pedalInfo = fx::registry::find (args[0].toStdString());
    const auto effectIt = registry().find (args[0]);
    if (pedalInfo == nullptr && effectIt == registry().end())
        return fail ("unknown effect '" + args[0] + "' (try --list)");

    const auto cwd = juce::File::getCurrentWorkingDirectory();
    const auto inFile  = cwd.getChildFile (args[1]);
    const auto outFile = cwd.getChildFile (args[2]);

    // Options and effect parameters
    Params params;
    int blockSize = 512;
    bool write24 = false;
    double bpm = 120.0;

    for (int i = 3; i < args.size(); ++i)
    {
        const auto& a = args[i];

        if (a.startsWith ("--block="))
            blockSize = juce::jlimit (1, 65536, a.fromFirstOccurrenceOf ("=", false, false).getIntValue());
        else if (a == "--24bit")
            write24 = true;
        else if (a.startsWith ("--bpm="))
            bpm = a.fromFirstOccurrenceOf ("=", false, false).getDoubleValue();
        else if (a.contains ("="))
            params[a.upToFirstOccurrenceOf ("=", false, false).trim()] = a.fromFirstOccurrenceOf ("=", false, false).getFloatValue();
        else
            return fail ("don't understand '" + a + "'");
    }

    // Read the whole input file
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();

    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (inFile));
    if (reader == nullptr)
        return fail ("can't read " + inFile.getFullPathName());

    const int numChannels = static_cast<int> (reader->numChannels);
    const int numSamples  = static_cast<int> (reader->lengthInSamples);
    const double sampleRate = reader->sampleRate;

    // Create and prepare the effect
    std::unique_ptr<fx::Effect> effect;
    if (pedalInfo != nullptr)
    {
        auto pedal = fx::registry::create (pedalInfo->id);
        for (const auto& [name, value] : params)
        {
            const int index = pedalInfo->indexOf (name.toStdString());
            if (index < 0)
                return fail ("'" + args[0] + "' has no control '" + name + "' (try --list)");
            pedal->setParameter (index, value);
        }
        pedal->setTempo (bpm);
        effect = std::move (pedal);
    }
    else
        effect = effectIt->second.create (params);
    effect->prepare (sampleRate, blockSize, numChannels);
    const int latency = effect->getLatencySamples();

    // Pad the end by the latency so the tail isn't cut off
    juce::AudioBuffer<float> buffer (numChannels, numSamples + latency);
    buffer.clear();
    reader->read (&buffer, 0, numSamples, 0, true, true);

    // Process in host-sized blocks, the same way a DAW would
    std::vector<float*> ptrs (static_cast<size_t> (numChannels));
    const auto start = juce::Time::getMillisecondCounterHiRes();

    for (int pos = 0; pos < buffer.getNumSamples(); pos += blockSize)
    {
        const int n = std::min (blockSize, buffer.getNumSamples() - pos);
        for (int ch = 0; ch < numChannels; ++ch)
            ptrs[static_cast<size_t> (ch)] = buffer.getWritePointer (ch, pos);
        effect->process (ptrs.data(), numChannels, n);
    }

    const auto elapsedMs = juce::Time::getMillisecondCounterHiRes() - start;

    // Write, skipping the first `latency` samples so output lines up with input
    outFile.deleteFile();
    std::unique_ptr<juce::OutputStream> stream = std::make_unique<juce::FileOutputStream> (outFile);
    if (! static_cast<juce::FileOutputStream*> (stream.get())->openedOk())
        return fail ("can't write " + outFile.getFullPathName());

    using Format = juce::AudioFormatWriterOptions::SampleFormat;
    const auto options = juce::AudioFormatWriterOptions{}
                             .withSampleRate (sampleRate)
                             .withNumChannels (numChannels)
                             .withBitsPerSample (write24 ? 24 : 32)
                             .withSampleFormat (write24 ? Format::integral : Format::floatingPoint);

    juce::WavAudioFormat wav;
    auto writer = wav.createWriterFor (stream, options);
    if (writer == nullptr)
        return fail ("can't create a WAV writer for " + outFile.getFullPathName());

    writer->writeFromAudioSampleBuffer (buffer, latency, numSamples);
    writer.reset();

    // Report
    const float peak = buffer.getMagnitude (latency, numSamples);
    const double seconds = numSamples / sampleRate;

    std::cout << args[0] << ": " << inFile.getFileName() << " -> " << outFile.getFileName()
              << "  (" << juce::String (seconds, 2) << " s, " << numChannels << " ch, "
              << sampleRate << " Hz, latency " << latency << ")\n"
              << "  peak " << juce::String (juce::Decibels::gainToDecibels (peak), 2) << " dBFS"
              << (peak > 1.0f && write24 ? "  ** CLIPPED in 24-bit output **" : "") << "\n"
              << "  processed in " << juce::String (elapsedMs, 1) << " ms ("
              << juce::String (seconds * 1000.0 / juce::jmax (elapsedMs, 0.001), 0) << "x real time)\n";
    return 0;
}
