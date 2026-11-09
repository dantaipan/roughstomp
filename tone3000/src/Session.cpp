#include "rs_t3k/Session.h"

#if JUCE_MAC || JUCE_LINUX
 #include <sys/stat.h>
#endif

namespace rs::t3k
{
Tokens Tokens::fromResponse (const juce::var& v, juce::int64 nowMs, const Tokens& previous)
{
    Tokens t;
    t.access = v["access_token"].toString();
    const auto newRefresh = v["refresh_token"].toString();
    t.refresh = newRefresh.isNotEmpty() ? newRefresh : previous.refresh;
    const auto expiresIn = v.hasProperty ("expires_in") ? static_cast<juce::int64> (v["expires_in"]) : 3600;
    t.expiresAtMs = nowMs + expiresIn * 1000;
    return t;
}

TokenStore::TokenStore (juce::File f) : file (std::move (f)) {}

Tokens TokenStore::load() const
{
    Tokens t;
    if (! file.existsAsFile())
        return t;
    const auto v = juce::JSON::parse (file);
    t.access = v["access_token"].toString();
    t.refresh = v["refresh_token"].toString();
    t.expiresAtMs = v.hasProperty ("expires_at") ? static_cast<juce::int64> (v["expires_at"]) : 0;
    return t;
}

void TokenStore::save (const Tokens& t)
{
    auto* o = new juce::DynamicObject();
    o->setProperty ("access_token", t.access);
    o->setProperty ("refresh_token", t.refresh);
    o->setProperty ("expires_at", t.expiresAtMs);

    file.getParentDirectory().createDirectory();
    // Written beside it, then moved over: a reader never sees half a file
    const auto temp = file.getSiblingFile (file.getFileName() + ".tmp");
    temp.replaceWithText (juce::JSON::toString (juce::var (o)));
   #if JUCE_MAC || JUCE_LINUX
    ::chmod (temp.getFullPathName().toRawUTF8(), S_IRUSR | S_IWUSR);   // 0600: the user only
   #endif
    temp.moveFileTo (file);
}

void TokenStore::clear()
{
    file.deleteFile();
}
} // namespace rs::t3k
