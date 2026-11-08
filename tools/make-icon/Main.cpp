// rs-make-icon: draw RoughStomp's app icon and write a macOS .iconset folder.
//
//   rs-make-icon <folder.iconset>
//
// The icon: a flat amber pedal (two knobs in a dark window, an LED, a
// footswitch) on near-black, in the style and colours of RoughTrack's icon.
// Drawn as vectors at every size, so the small ones stay crisp.

#include <juce_graphics/juce_graphics.h>

#include <iostream>

namespace
{
// RoughTrack's icon palette (resources/icon/icon-macos.svg there): flat amber
// shapes on near-black, so the two icons sit together in the Dock
const juce::Colour tile      { 0xff121214 };
const juce::Colour amber     { 0xfff2c14e };
const juce::Colour amberMid  { 0xffc59e42 };
const juce::Colour amberDim  { 0xff8d7234 };
const juce::Colour window    { 0xff514324 };
const juce::Colour windowDim { 0xff3a321e };

void knob (juce::Graphics& g, juce::Point<float> centre, float radius, float angle)
{
    g.setColour (tile);
    g.fillEllipse (juce::Rectangle<float> (radius * 2.0f, radius * 2.0f).withCentre (centre));
    g.setColour (windowDim);
    g.fillEllipse (juce::Rectangle<float> (radius * 1.5f, radius * 1.5f).withCentre (centre));

    juce::Path pointer;
    pointer.addLineSegment ({ centre.getPointOnCircumference (radius * 0.2f, angle), centre.getPointOnCircumference (radius * 0.72f, angle) }, 0.0f);
    g.setColour (amber);
    g.strokePath (pointer, juce::PathStrokeType (radius * 0.3f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

/** The icon on a 1024-unit canvas, flat like RoughTrack's. */
void draw (juce::Graphics& g)
{
    // macOS icon grid: an 824-unit rounded square with a soft shadow below
    const auto square = juce::Rectangle<float> (100.0f, 100.0f, 824.0f, 824.0f);
    for (int i = 6; i > 0; --i)
    {
        g.setColour (juce::Colours::black.withAlpha (0.05f));
        g.fillRoundedRectangle (square.translated (0.0f, 12.0f).expanded (static_cast<float> (i) * 3.0f), 185.0f + static_cast<float> (i) * 3.0f);
    }
    g.setColour (tile);
    g.fillRoundedRectangle (square, 185.0f);

    // The pedal, corner screws as on RoughTrack's cassette
    const auto pedal = juce::Rectangle<float> (312.0f, 196.0f, 400.0f, 632.0f);
    g.setColour (amber);
    g.fillRoundedRectangle (pedal, 28.0f);
    g.setColour (amberDim);
    for (auto corner : { pedal.getTopLeft(), pedal.getTopRight(), pedal.getBottomLeft(), pedal.getBottomRight() })
        g.fillEllipse (juce::Rectangle<float> (24.0f, 24.0f).withCentre (corner + juce::Point<float> (corner.x < 512.0f ? 30.0f : -30.0f,
                                                                                                       corner.y < 512.0f ? 30.0f : -30.0f)));

    // A recessed window with two knobs, turned up
    g.setColour (window);
    g.fillRoundedRectangle ({ 356.0f, 252.0f, 312.0f, 200.0f }, 14.0f);
    knob (g, { 442.0f, 352.0f }, 62.0f, juce::degreesToRadians (35.0f));
    knob (g, { 582.0f, 352.0f }, 62.0f, juce::degreesToRadians (-30.0f));

    // The LED
    g.setColour (tile);
    g.fillEllipse (juce::Rectangle<float> (30.0f, 30.0f).withCentre ({ 512.0f, 520.0f }));

    // The footswitch: a dark collar and a two-tone cap
    const juce::Point<float> foot { 512.0f, 680.0f };
    g.setColour (tile);
    g.fillEllipse (juce::Rectangle<float> (196.0f, 196.0f).withCentre (foot));
    g.setColour (amberMid);
    g.fillEllipse (juce::Rectangle<float> (136.0f, 136.0f).withCentre (foot));
    g.setColour (amber);
    g.fillEllipse (juce::Rectangle<float> (84.0f, 84.0f).withCentre (foot));
}
} // namespace

int main (int argc, char* argv[])
{
    if (argc < 2)
    {
        std::cout << "Usage: rs-make-icon <folder.iconset>\n";
        return 1;
    }

    const juce::File folder (juce::File::getCurrentWorkingDirectory().getChildFile (argv[1]));
    folder.createDirectory();

    // Every size macOS's iconutil expects
    const std::pair<const char*, int> sizes[] {
        { "icon_16x16.png", 16 },    { "icon_16x16@2x.png", 32 },
        { "icon_32x32.png", 32 },    { "icon_32x32@2x.png", 64 },
        { "icon_128x128.png", 128 }, { "icon_128x128@2x.png", 256 },
        { "icon_256x256.png", 256 }, { "icon_256x256@2x.png", 512 },
        { "icon_512x512.png", 512 }, { "icon_512x512@2x.png", 1024 },
    };

    for (const auto& [name, size] : sizes)
    {
        juce::Image image (juce::Image::ARGB, size, size, true);
        {
            juce::Graphics g (image);
            g.addTransform (juce::AffineTransform::scale (static_cast<float> (size) / 1024.0f));
            draw (g);
        }

        const auto file = folder.getChildFile (name);
        file.deleteFile();
        juce::FileOutputStream out (file);
        if (! out.openedOk() || ! juce::PNGImageFormat().writeImageToStream (image, out))
        {
            std::cerr << "rs-make-icon: can't write " << file.getFullPathName() << "\n";
            return 1;
        }
    }

    std::cout << "Wrote " << folder.getFullPathName() << "\n";
    return 0;
}
