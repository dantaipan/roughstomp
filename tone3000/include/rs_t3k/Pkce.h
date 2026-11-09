#pragma once

#include <juce_core/juce_core.h>

namespace rs::t3k
{
/** URL-safe base64 without padding (RFC 4648 section 5), as OAuth uses. */
juce::String base64Url (const void* data, size_t size);

/** bytes of cryptographically random data, URL-safe base64 encoded. */
juce::String randomToken (int bytes);

/** PKCE (RFC 7636): a random verifier and its S256 challenge. */
struct Pkce
{
    juce::String verifier, challenge;

    static Pkce create();
    static juce::String challengeFor (const juce::String& verifier);
};
} // namespace rs::t3k
