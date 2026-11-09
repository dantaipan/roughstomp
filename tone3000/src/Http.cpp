#include "rs_t3k/Http.h"

namespace rs::t3k
{
namespace
{
juce::String headerText (const juce::StringPairArray& headers)
{
    juce::String s;
    for (const auto& k : headers.getAllKeys())
        s << k << ": " << headers[k] << "\r\n";
    return s;
}

std::unique_ptr<juce::InputStream> open (const HttpRequest& r, HttpResponse& out, int timeoutMs)
{
    auto url = juce::URL (r.url);
    if (r.post)
        url = url.withPOSTData (r.body);
    auto options = juce::URL::InputStreamOptions (r.post ? juce::URL::ParameterHandling::inPostData
                                                         : juce::URL::ParameterHandling::inAddress)
                       .withExtraHeaders (headerText (r.headers))
                       .withConnectionTimeoutMs (timeoutMs)
                       .withStatusCode (&out.status)
                       .withResponseHeaders (&out.headers);
    return url.createInputStream (options);
}
} // namespace

juce::String formEncode (const juce::StringPairArray& fields)
{
    juce::String body;
    for (const auto& key : fields.getAllKeys())
        body << (body.isEmpty() ? "" : "&") << juce::URL::addEscapeChars (key, true)
             << "=" << juce::URL::addEscapeChars (fields[key], true);
    return body;
}

HttpResponse JuceHttp::fetch (const HttpRequest& r)
{
    HttpResponse out;
    if (auto stream = open (r, out, 15000))
        out.body = stream->readEntireStreamAsString();
    return out;
}

HttpResponse JuceHttp::download (const HttpRequest& r, const juce::File& file, std::function<bool()> keepGoing)
{
    HttpResponse out;
    auto stream = open (r, out, 30000);
    if (stream == nullptr || ! out.ok())
    {
        if (stream != nullptr)
            out.body = stream->readEntireStreamAsString();
        return out;
    }

    file.getParentDirectory().createDirectory();
    juce::FileOutputStream o (file);
    if (! o.openedOk())
        return { 0, "Couldn't write " + file.getFullPathName(), {} };
    o.setPosition (0);
    o.truncate();

    juce::HeapBlock<char> buffer (65536);
    while (! stream->isExhausted())
    {
        if (keepGoing && ! keepGoing())
            return {};
        const auto n = stream->read (buffer, 65536);
        if (n < 0)
            return {};   // the connection dropped
        if (n == 0)
            break;
        if (! o.write (buffer, static_cast<size_t> (n)))
            return { 0, "Couldn't write " + file.getFullPathName(), {} };
    }
    o.flush();
    return out;
}
} // namespace rs::t3k
