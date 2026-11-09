#include "rs_t3k/Pkce.h"

#include <juce_cryptography/juce_cryptography.h>

#include <cstdlib>

namespace rs::t3k
{
juce::String base64Url (const void* data, size_t size)
{
    return juce::Base64::toBase64 (data, size)
        .replaceCharacter ('+', '-')
        .replaceCharacter ('/', '_')
        .trimCharactersAtEnd ("=");
}

juce::String randomToken (int bytes)
{
    juce::MemoryBlock b (static_cast<size_t> (juce::jmax (1, bytes)));
   #if JUCE_MAC || JUCE_IOS
    arc4random_buf (b.getData(), b.getSize());   // the system's CSPRNG
   #else
    juce::Random::getSystemRandom().fillBitsRandomly (b.getData(), b.getSize());
   #endif
    return base64Url (b.getData(), b.getSize());
}

juce::String Pkce::challengeFor (const juce::String& verifier)
{
    const auto digest = juce::SHA256 (verifier.toRawUTF8(), verifier.getNumBytesAsUTF8()).getRawData();
    return base64Url (digest.getData(), digest.getSize());
}

Pkce Pkce::create()
{
    Pkce p;
    p.verifier = randomToken (48);   // 64 characters: within RFC 7636's 43..128
    p.challenge = challengeFor (p.verifier);
    return p;
}
} // namespace rs::t3k
