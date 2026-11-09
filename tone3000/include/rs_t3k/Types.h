#pragma once

#include <juce_core/juce_core.h>

#include <vector>

namespace rs::t3k
{
/** The signed-in user, or a tone's creator. */
struct User
{
    juce::String id, username, displayName, avatarUrl, url;

    /** The display name, or the username when there isn't one. */
    juce::String name() const { return displayName.isNotEmpty() ? displayName : username; }

    static User fromJson (const juce::var&);
};

/** A tone: a pack of one or more models (captures or IRs) of one piece of gear. */
struct Tone
{
    int id = 0;
    juce::String title, description, gear, format, license, url;
    juce::StringArray images;
    User creator;
    bool isPublic = true;
    int a1Models = 0, a2Models = 0, customModels = 0, irs = 0;

    juce::String image() const { return images.isEmpty() ? juce::String() : images[0]; }
    bool isIr() const { return format == "ir"; }

    static Tone fromJson (const juce::var&);
};

/** One model in a tone: what actually gets downloaded and loaded. */
struct Model
{
    int id = 0, toneId = 0;
    juce::String name, size, architecture, contentHash, url;

    static Model fromJson (const juce::var&);
};

template <typename T>
struct Page
{
    std::vector<T> items;
    int page = 1, totalPages = 1, total = 0;
    bool hasMore() const noexcept { return page < totalPages; }
};

/** A paginated response ({ data, page, total_pages, total }) or a plain
    { data } one (trending, latest), which is a single page. */
Page<Tone> parseTones (const juce::var&);
Page<Model> parseModels (const juce::var&);

/** "amp-cab" -> "Amp & Cab", "nam" -> "NAM": for showing gear and formats. */
juce::String gearName (const juce::String& gear);
juce::String formatName (const juce::String& format);
} // namespace rs::t3k
