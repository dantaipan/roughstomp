#pragma once

#include <juce_core/juce_core.h>

namespace rs::t3k
{
/** An access token and the refresh token that renews it. */
struct Tokens
{
    juce::String access, refresh;
    juce::int64 expiresAtMs = 0;   // ms since 1970

    bool signedIn() const noexcept { return refresh.isNotEmpty() || access.isNotEmpty(); }

    /** Renew a minute early, so a request doesn't go out with a token that
        expires on the way. */
    bool needsRefresh (juce::int64 nowMs) const noexcept { return nowMs > expiresAtMs - 60000; }

    /** From a /oauth/token response. The refresh token rotates: a response
        without one keeps the previous. */
    static Tokens fromResponse (const juce::var&, juce::int64 nowMs, const Tokens& previous);
    static Tokens fromResponse (const juce::var& v, juce::int64 nowMs) { return fromResponse (v, nowMs, Tokens()); }
};

/** The tokens, in a file only the user can read. One file for every
    RoughStomp: other hosts and the standalone app share the sign-in. Refresh
    tokens rotate, so whoever refreshes must hold the lock() and re-read the
    file first: another process may have refreshed already. */
class TokenStore
{
public:
    explicit TokenStore (juce::File);

    Tokens load() const;
    void save (const Tokens&);
    void clear();

    /** Across processes, for the read-refresh-write sequence. */
    juce::InterProcessLock& lock() noexcept { return processLock; }

    const juce::File& getFile() const noexcept { return file; }

private:
    juce::File file;
    juce::InterProcessLock processLock { "RoughStompTONE3000Session" };
};
} // namespace rs::t3k
