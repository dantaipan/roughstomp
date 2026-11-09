#include "rs_t3k/Types.h"

namespace rs::t3k
{
namespace
{
juce::String str (const juce::var& v) { return v.isVoid() || v.isUndefined() ? juce::String() : v.toString(); }
int num (const juce::var& v) { return v.isVoid() || v.isUndefined() ? 0 : static_cast<int> (v); }

template <typename T>
Page<T> parsePage (const juce::var& v)
{
    Page<T> p;
    if (const auto* data = v["data"].getArray())
        for (const auto& item : *data)
            p.items.push_back (T::fromJson (item));
    // trending and latest come without paging: one page
    p.page = v.hasProperty ("page") ? juce::jmax (1, num (v["page"])) : 1;
    p.totalPages = v.hasProperty ("total_pages") ? num (v["total_pages"]) : 1;
    p.total = v.hasProperty ("total") ? num (v["total"]) : static_cast<int> (p.items.size());
    return p;
}
} // namespace

User User::fromJson (const juce::var& v)
{
    return { str (v["id"]), str (v["username"]), str (v["display_name"]), str (v["avatar_url"]), str (v["url"]) };
}

Tone Tone::fromJson (const juce::var& v)
{
    Tone t;
    t.id = num (v["id"]);
    t.title = str (v["title"]);
    t.description = str (v["description"]);
    t.gear = str (v["gear"]);
    t.format = str (v["format"]);
    t.license = str (v["license"]);
    t.url = str (v["url"]);
    if (const auto* imgs = v["images"].getArray())
        for (const auto& i : *imgs)
            t.images.add (i.toString());
    t.creator = User::fromJson (v["user"]);
    t.isPublic = v.hasProperty ("is_public") ? static_cast<bool> (v["is_public"]) : true;
    t.a1Models = num (v["a1_models_count"]);
    t.a2Models = num (v["a2_models_count"]);
    t.customModels = num (v["custom_models_count"]);
    t.irs = num (v["irs_count"]);
    return t;
}

Model Model::fromJson (const juce::var& v)
{
    Model m;
    m.id = num (v["id"]);
    m.toneId = num (v["tone_id"]);
    m.name = str (v["name"]);
    m.size = str (v["size"]);
    m.architecture = str (v["architecture_version"]);
    m.contentHash = str (v["content_hash"]);
    m.url = str (v["model_url"]);
    return m;
}

Page<Tone> parseTones (const juce::var& v)   { return parsePage<Tone> (v); }
Page<Model> parseModels (const juce::var& v) { return parsePage<Model> (v); }

juce::String gearName (const juce::String& gear)
{
    if (gear == "amp")      return "Amp";
    if (gear == "amp-cab")  return "Amp & Cab";
    if (gear == "full-rig") return "Full Rig";
    if (gear == "pedal")    return "Pedal";
    if (gear == "outboard") return "Outboard";
    if (gear == "ir")       return "IR";
    if (gear.isEmpty())     return {};
    // Anything newer: "space" -> "Space"
    auto s = gear.replaceCharacter ('-', ' ');
    return s.substring (0, 1).toUpperCase() + s.substring (1);
}

juce::String formatName (const juce::String& format)
{
    if (format == "nam") return "NAM";
    if (format == "ir")  return "IR";
    return format.toUpperCase();
}
} // namespace rs::t3k
