#pragma once

#include <algorithm>
#include <vector>

namespace bmo
{

/** The host's bypass, delayed by the latency the processor reports.

    A host that compensates a plugin's latency delays every other track by
    the figure the plugin reports, bypassed or not. A bypass that hands the
    input straight back is then early by that figure, and switching it in or
    out moves the track in time against everything else -- 80 samples with
    BMO EQ and BMO Saturator at 2x in a rack (QA, 2026-10-03).

    So both processors keep one of these: `push` from `processBlock`, every
    block, so the line always holds the most recent input; `delay` from
    `processBlockBypassed`, which feeds the line and hands back what went in
    `latency` samples ago. Because the line is fed while processing, the
    moment the host switches to bypass the output carries on from the right
    place in the input, aligned with what the processed path was producing;
    the samples the processed path had not yet delivered are delivered dry.
    A latency change while bypassed moves the read point at once, which is
    the figure the host is compensating for from then on.

    Sized in prepare and never again; the callback only reads and writes.
    `kCapacity` is a power of two well over any latency this suite can
    report -- the largest single module is BMO FET or BMO EQ at 8x, under a
    hundred samples, so eight of them in a rack stay under a thousand -- and a
    latency past it is clamped rather than read out of bounds. JUCE-free. */
class BypassDelay
{
public:
    static constexpr int kCapacity = 8192;

    void prepare (int numChannels)
    {
        lines.assign ((size_t) std::max (numChannels, 0), std::vector<float> ((size_t) kCapacity, 0.0f));
        writeIndex = 0;
    }

    /** processBlock's half: the input goes in, nothing comes out. */
    void push (const float* const* channels, int numChannels, int numSamples) noexcept
    {
        const auto n = std::min (numChannels, (int) lines.size());

        for (int ch = 0; ch < n; ++ch)
        {
            auto* line = lines[(size_t) ch].data();
            auto w = writeIndex;

            for (int i = 0; i < numSamples; ++i)
            {
                line[w] = channels[ch][i];
                w = (w + 1) & kMask;
            }
        }

        advance (numSamples);
    }

    /** processBlockBypassed's half: each sample goes in, and the one from
        `latency` samples before it comes out in its place. Channels past
        the prepared count are left as they are. */
    void delay (float* const* channels, int numChannels, int numSamples, int latency) noexcept
    {
        const auto n = std::min (numChannels, (int) lines.size());
        const auto d = std::clamp (latency, 0, kCapacity - 1);

        for (int ch = 0; ch < n; ++ch)
        {
            auto* line = lines[(size_t) ch].data();
            auto w = writeIndex;

            for (int i = 0; i < numSamples; ++i)
            {
                line[w] = channels[ch][i];
                channels[ch][i] = line[(w - d) & kMask];
                w = (w + 1) & kMask;
            }
        }

        advance (numSamples);
    }

private:
    static constexpr int kMask = kCapacity - 1;
    static_assert ((kCapacity & kMask) == 0, "the capacity is a power of two");

    void advance (int numSamples) noexcept
    {
        writeIndex = (writeIndex + std::max (numSamples, 0)) & kMask;
    }

    std::vector<std::vector<float>> lines;
    int writeIndex = 0;
};

} // namespace bmo
