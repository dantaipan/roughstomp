#pragma once

#include <juce_core/juce_core.h>

#include <functional>

namespace rs::t3k
{
struct HttpRequest
{
    juce::String url;
    bool post = false;
    juce::String body;          // for POST: form-encoded
    juce::StringPairArray headers;
};

struct HttpResponse
{
    int status = 0;             // 0: no connection
    juce::String body;
    juce::StringPairArray headers;

    juce::var json() const { return juce::JSON::parse (body); }
    bool ok() const noexcept { return status >= 200 && status < 300; }
};

/** The network, behind an interface so the tests can stand in for TONE3000. */
class Http
{
public:
    virtual ~Http() = default;

    /** Blocking: call it off the message thread. */
    virtual HttpResponse fetch (const HttpRequest&) = 0;

    /** Blocking: streams the body into file (overwriting it); keepGoing is
        asked as it goes, and false stops early (the response then says 0). */
    virtual HttpResponse download (const HttpRequest&, const juce::File&, std::function<bool()> keepGoing) = 0;
};

/** The real network, through juce::URL. */
class JuceHttp final : public Http
{
public:
    HttpResponse fetch (const HttpRequest&) override;
    HttpResponse download (const HttpRequest&, const juce::File&, std::function<bool()> keepGoing) override;
};

/** key=value&... with both sides escaped. */
juce::String formEncode (const juce::StringPairArray&);
} // namespace rs::t3k
