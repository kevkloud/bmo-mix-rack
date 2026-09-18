#pragma once

//==============================================================================
// WAV in and out for the measurement harnesses.
//
// Lifted verbatim out of tools/measure/opto/main.cpp, which is where it was
// written. **The five existing harnesses -- eq, sat, opto, dim, deq -- each
// still hold their own copy of this**, byte-identical or nearly so, which is
// the kind of repeated encoding of one fact the root AGENTS.md is repeatedly
// rude about. They were not migrated in the change that made this file:
// moving five working harnesses to prove a point about tidiness is a different
// change from adding a sixth. Move each one when it is next opened for its own
// reasons, and delete its copy then.
//
// Nothing in here knows about any module.
//==============================================================================

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace bmo::measure
{

/** Minimal WAV reader: PCM 16/24/32 and IEEE float 32, any channel count,
    deinterleaved (not summed -- Link needs real per-channel content). */
inline bool readWav (const std::string& path, std::vector<std::vector<float>>& channels, double& rate)
{
    std::ifstream file (path, std::ios::binary);

    if (! file)
        return false;

    const std::vector<char> bytes { std::istreambuf_iterator<char> (file),
                                    std::istreambuf_iterator<char>() };

    if (bytes.size() < 44 || std::memcmp (bytes.data(), "RIFF", 4) != 0
                          || std::memcmp (bytes.data() + 8, "WAVE", 4) != 0)
        return false;

    const auto u16 = [&bytes] (size_t at) { return (uint32_t) (uint8_t) bytes[at]
                                                 | ((uint32_t) (uint8_t) bytes[at + 1] << 8); };
    const auto u32 = [&u16] (size_t at)   { return u16 (at) | (u16 (at + 2) << 16); };

    uint32_t format = 1, numChannels = 1, bits = 16;
    size_t at = 12;

    while (at + 8 <= bytes.size())
    {
        const std::string id (bytes.data() + at, 4);
        const auto size = (size_t) u32 (at + 4);
        const auto body = at + 8;

        if (id == "fmt " && body + 16 <= bytes.size())
        {
            format      = u16 (body);
            numChannels = std::max (1u, u16 (body + 2));
            rate        = (double) u32 (body + 4);
            bits        = u16 (body + 14);
        }
        else if (id == "data")
        {
            const auto bytesPerSample = bits / 8;

            if (bytesPerSample == 0)
                return false;

            const auto frames = std::min (size, bytes.size() - body) / (bytesPerSample * numChannels);
            channels.assign (numChannels, std::vector<float> (frames, 0.0f));

            for (size_t f = 0; f < frames; ++f)
            {
                for (uint32_t c = 0; c < numChannels; ++c)
                {
                    const auto p = body + (f * numChannels + c) * bytesPerSample;
                    double v = 0.0;

                    if (format == 3 && bits == 32)
                    {
                        float bits32 = 0.0f;
                        std::memcpy (&bits32, bytes.data() + p, 4);
                        v = bits32;
                    }
                    else if (bits == 16)
                    {
                        v = (double) (int16_t) (uint16_t) u16 (p) / 32768.0;
                    }
                    else if (bits == 24)
                    {
                        auto raw = (int32_t) (u16 (p) | ((uint32_t) (uint8_t) bytes[p + 2] << 16));
                        if (raw & 0x800000) raw -= 0x1000000;
                        v = (double) raw / 8388608.0;
                    }
                    else if (bits == 32)
                    {
                        v = (double) (int32_t) u32 (p) / 2147483648.0;
                    }
                    else
                    {
                        return false;
                    }

                    channels[c][f] = (float) v;
                }
            }

            return ! channels.empty() && ! channels.front().empty();
        }

        at = body + size + (size & 1);
    }

    return false;
}

/** 24-bit PCM out, any channel count, interleaved. Creates the destination's
    parent directory if it doesn't exist yet, so `release`'s default output
    directory (and any --out path a caller points at a fresh folder) doesn't
    silently fail to write. */
inline bool writeWav (const std::string& path, const std::vector<std::vector<float>>& channels, double rate)
{
    if (channels.empty())
        return false;

    const std::filesystem::path p (path);

    if (p.has_parent_path())
        std::filesystem::create_directories (p.parent_path());

    std::ofstream file (path, std::ios::binary);

    if (! file)
        return false;

    const auto numChannels = (uint32_t) channels.size();
    const auto frames = channels.front().size();
    const uint32_t dataBytes = (uint32_t) (frames * numChannels * 3);
    const uint32_t byteRate  = (uint32_t) rate * numChannels * 3;

    const auto u32 = [&file] (uint32_t v) { file.put ((char) (v & 0xff)); file.put ((char) ((v >> 8) & 0xff));
                                            file.put ((char) ((v >> 16) & 0xff)); file.put ((char) ((v >> 24) & 0xff)); };
    const auto u16 = [&file] (uint16_t v) { file.put ((char) (v & 0xff)); file.put ((char) ((v >> 8) & 0xff)); };

    file.write ("RIFF", 4); u32 (36 + dataBytes); file.write ("WAVE", 4);
    file.write ("fmt ", 4); u32 (16); u16 (1); u16 ((uint16_t) numChannels); u32 ((uint32_t) rate);
    u32 (byteRate); u16 ((uint16_t) (numChannels * 3)); u16 (24);
    file.write ("data", 4); u32 (dataBytes);

    for (size_t f = 0; f < frames; ++f)
    {
        for (uint32_t c = 0; c < numChannels; ++c)
        {
            const auto clamped = std::max (-1.0f, std::min (1.0f, channels[c][f]));
            const auto value = (int32_t) std::lround ((double) clamped * 8388607.0);
            file.put ((char) (value & 0xff));
            file.put ((char) ((value >> 8) & 0xff));
            file.put ((char) ((value >> 16) & 0xff));
        }
    }

    return true;
}

} // namespace bmo::measure
