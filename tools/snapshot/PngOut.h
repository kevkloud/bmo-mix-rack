#pragma once

#include <juce_graphics/juce_graphics.h>

/** Writing a render to disk, in one place so a test can exercise the same code
    the tool runs.

    It exists because of a bug that cost a debugging round trip on AURORA on
    2026-09-21. `juce::File::createOutputStream` opens an *existing* file and
    seeks to the END, so re-rendering over a path appended a second PNG to the
    first instead of replacing it. Every decoder reads the first image and
    ignores the trailing bytes, so the file silently grew -- one render went
    68,858 bytes to 137,716, another 168 kB to 850 kB -- while every viewer, and
    every human reviewing the render, went on seeing the picture from before the
    change. Three consecutive renders after a real layout change showed the
    pre-change panel.

    That is worse than having no render loop at all, because it reports that a
    fix did not work when it did. It also quietly voids the guarantee the review
    loop is built on: "byte-identical or it was not a refactor" means nothing if
    the bytes are a previous render.

    `tools/tune/snapshot` never had the bug -- it calls `deleteFile()` first --
    so that is the house fix and this matches it. The truncate is kept as well:
    `deleteFile` can fail on a file another process holds open, and if it ever
    does, the append comes straight back. Belt and braces on a fault that is
    invisible when it happens.
*/
namespace bmo::snapshot
{

inline bool writePng (const juce::File& out, const juce::Image& image)
{
    out.deleteFile();

    std::unique_ptr<juce::FileOutputStream> stream (out.createOutputStream());

    if (stream == nullptr)
        return false;

    stream->setPosition (0);
    stream->truncate();

    juce::PNGImageFormat png;
    return png.writeImageToStream (image, *stream);
}

} // namespace bmo::snapshot
