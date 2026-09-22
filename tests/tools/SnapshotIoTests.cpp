// The render loop's own plumbing: writing a PNG over an existing one.
//
// This suite exists for a single fault, found on AURORA on 2026-09-21.
// `juce::File::createOutputStream` opens an existing file and seeks to the END,
// so `tools/snapshot` appended each new render to the previous one instead of
// replacing it. Decoders read the first image and ignore the trailing bytes, so
// the file silently grew while every viewer -- and every human reviewing the
// render -- went on seeing the picture from before the change. One file went
// 68,858 bytes to 137,716; another 168 kB to 850 kB. Three consecutive renders
// after a real layout change showed the pre-change panel.
//
// A render loop that reports "your fix did not work" when it did is worse than
// no render loop, and it voids the guarantee the review loop rests on:
// "byte-identical or it was not a refactor" means nothing when the bytes are a
// previous render.
//
// The house rule, from tests/dsp/OptoDspTests.cpp: **assert absolutes, not
// comparisons.** "The file did not grow" is exactly the relative test that trap
// teaches against -- it passes if both writes are wrong in the same way. So the
// assertions below pin the decoded image to the second render's *content*: a
// stated width, a stated height, and a stated pixel colour that differs between
// the two images written. A stale file fails on the pixel, not on the size.

#include "tools/snapshot/PngOut.h"

#include <juce_graphics/juce_graphics.h>
#include <iostream>

namespace
{

int failures = 0;

void check (bool ok, const juce::String& what)
{
    if (ok)
        return;

    std::cerr << "FAIL: " << what << '\n';
    ++failures;
}

/** A solid image of a stated colour, so a decoded pixel names which write won. */
juce::Image solid (int w, int h, juce::Colour c)
{
    juce::Image image (juce::Image::ARGB, w, h, true);
    juce::Graphics g (image);
    g.fillAll (c);
    return image;
}

juce::Image decode (const juce::File& f)
{
    juce::PNGImageFormat png;
    std::unique_ptr<juce::FileInputStream> in (f.createInputStream());

    if (in == nullptr)
        return {};

    return png.decodeImage (*in);
}

void writingOverARenderReplacesIt()
{
    const auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory)
                         .getChildFile ("bmo-snapshot-io-tests-"
                                        + juce::Uuid().toDashedString());
    dir.createDirectory();

    const auto out = dir.getChildFile ("render.png");
    out.deleteFile();

    // First render: 40x24, red.
    check (bmo::snapshot::writePng (out, solid (40, 24, juce::Colours::red)),
           "first write should succeed");

    const auto firstSize = out.getSize();
    check (firstSize > 0, "first write should produce bytes");

    // Second render over the same path: a different size AND a different
    // colour, so neither dimension alone can mask a stale file.
    check (bmo::snapshot::writePng (out, solid (64, 48, juce::Colours::blue)),
           "second write should succeed");

    const auto decoded = decode (out);

    // Absolutes: the second render's own stated geometry and colour.
    check (decoded.getWidth() == 64,
           "decoded width should be 64, the second render -- got "
               + juce::String (decoded.getWidth()));
    check (decoded.getHeight() == 48,
           "decoded height should be 48, the second render -- got "
               + juce::String (decoded.getHeight()));

    if (decoded.isValid() && decoded.getWidth() == 64 && decoded.getHeight() == 48)
    {
        const auto px = decoded.getPixelAt (32, 24);
        check (px.getBlue() > 200 && px.getRed() < 60,
               "centre pixel should be the second render's blue, not the first's red");
    }

    // The append bug's own signature is growth, but it is only measurable
    // between writes of the SAME image: two different images have two different
    // natural sizes, and comparing those proves nothing. (An earlier draft of
    // this test compared 40x24 red against 64x48 blue and failed on a correct
    // implementation, which is its own small lesson about relative assertions.)
    //
    // So: write the identical image again and pin the byte count. This is also
    // the property `tools/inspect hash` and "byte-identical or it was not a
    // refactor" actually depend on.
    const auto secondSize = out.getSize();

    check (bmo::snapshot::writePng (out, solid (64, 48, juce::Colours::blue)),
           "third write should succeed");
    check (out.getSize() == secondSize,
           "re-rendering the same image must give the same byte count, not append -- was "
               + juce::String (secondSize) + ", now " + juce::String (out.getSize()));

    check (firstSize > 0 && secondSize > 0, "both renders should produce bytes");

    out.deleteFile();
    dir.deleteRecursively();
}

void writingToAFreshPathWorks()
{
    const auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory)
                         .getChildFile ("bmo-snapshot-io-tests-fresh-"
                                        + juce::Uuid().toDashedString());
    dir.createDirectory();

    const auto out = dir.getChildFile ("new.png");
    out.deleteFile();

    check (bmo::snapshot::writePng (out, solid (16, 16, juce::Colours::green)),
           "writing to a path that does not exist should succeed");

    const auto decoded = decode (out);
    check (decoded.getWidth() == 16 && decoded.getHeight() == 16,
           "a fresh write should decode at its stated size");

    dir.deleteRecursively();
}

} // namespace

int main()
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    writingOverARenderReplacesIt();
    writingToAFreshPathWorks();

    if (failures > 0)
    {
        std::cerr << failures << " failure(s)\n";
        return 1;
    }

    std::cout << "snapshot io: all checks passed\n";
    return 0;
}
