#include "rs_t3k/Types.h"

#include <catch2/catch_test_macros.hpp>

using namespace rs::t3k;

namespace
{
juce::var load (const char* name)
{
    return juce::JSON::parse (juce::File (T3K_TEST_DATA).getChildFile (name));
}
} // namespace

TEST_CASE ("Tone pages parse, nulls and missing fields included", "[t3k]")
{
    const auto page = parseTones (load ("tones-page.json"));
    REQUIRE (page.items.size() == 2);
    CHECK (page.page == 1);
    CHECK (page.totalPages == 3);
    CHECK (page.total == 5);
    CHECK (page.hasMore());

    const auto& amp = page.items[0];
    CHECK (amp.id == 101);
    CHECK (amp.title == "Clean Amp");
    CHECK (amp.format == "nam");
    CHECK_FALSE (amp.isIr());
    CHECK (amp.image() == "https://assets.tone3000.com/clean.jpg");
    CHECK (amp.a2Models == 3);
    CHECK (amp.creator.name() == "maker");   // display_name null: the username

    const auto& room = page.items[1];
    CHECK (room.isIr());
    CHECK (room.image().isEmpty());
    CHECK (room.creator.name() == "Room Person");
    CHECK (room.creator.avatarUrl.isEmpty());
}

TEST_CASE ("Unpaginated lists are one page", "[t3k]")
{
    const auto page = parseTones (load ("trending.json"));
    REQUIRE (page.items.size() == 1);
    CHECK (page.page == 1);
    CHECK (page.totalPages == 1);
    CHECK_FALSE (page.hasMore());
}

TEST_CASE ("Models parse", "[t3k]")
{
    const auto page = parseModels (load ("models.json"));
    REQUIRE (page.items.size() == 2);
    CHECK (page.items[0].id == 9001);
    CHECK (page.items[0].toneId == 101);
    CHECK (page.items[0].architecture == "2");
    CHECK (page.items[0].url.endsWith ("/9001/download"));
    CHECK (page.items[1].size.isEmpty());
}

TEST_CASE ("Gear and format names", "[t3k]")
{
    CHECK (gearName ("amp-cab") == "Amp & Cab");
    CHECK (gearName ("space") == "Space");
    CHECK (gearName ("full-rig") == "Full Rig");
    CHECK (formatName ("nam") == "NAM");
    CHECK (formatName ("ir") == "IR");
}
