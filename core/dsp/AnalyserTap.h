#pragma once

#include <atomic>
#include <cstddef>
#include <vector>

namespace bmo
{

/** A read-only window onto the signal, for a panel that wants to draw it.

    The audio thread writes; a panel's timer reads the most recent samples. It
    is deliberately **not** a queue: a display wants "what is happening now",
    not every sample in order, so there is no consumer index and nothing for
    the audio thread to wait on or check. Writing is a copy and one relaxed
    store.

    **It cannot change the sound, and it cannot add latency.** A tap is taken
    on the way past: nothing downstream reads it, no DSP state is involved, and
    the module still reports the latency it did before. Block-size invariance
    is untouched for the same reason -- the write depends on no state that
    survives a block boundary.

    **Nothing runs when no panel is looking.** `setEnabled(false)` makes
    `write()` return immediately, and a closed editor is the normal state of a
    plugin in a finished session, so the cost has to be nothing rather than
    nearly nothing.

    A torn read is possible in principle -- the writer can wrap into samples
    the reader is copying -- and is accepted: the reader trails the write head
    by design, the cost of a tear is one frame of a display that redraws
    dozens of times a second, and the alternative is a lock on the audio
    thread. Do not "fix" this with one.
*/
class AnalyserTap
{
public:
    /** Allocates. Message thread, before the audio thread can run: the same
        contract as a DSP's prepare(). Capacity is rounded up to a power of two
        so the wrap is a mask rather than a division. */
    void prepare (int capacitySamples)
    {
        size_t size = 1;
        while (size < (size_t) (capacitySamples > 0 ? capacitySamples : 1))
            size <<= 1;

        buffer.assign (size, 0.0f);
        mask = size - 1;
        head.store (0, std::memory_order_relaxed);
    }

    /** True once a panel has asked for samples. False is the default, so a
        module that never opens an editor never pays anything. */
    bool isEnabled() const noexcept { return enabled.load (std::memory_order_relaxed); }
    void setEnabled (bool shouldBe) noexcept { enabled.store (shouldBe, std::memory_order_relaxed); }

    /** The mono sum of what was handed in, written at the head. Audio thread.
        Channels beyond the second are ignored, as everywhere else in the
        suite. */
    template <typename Sample>
    void write (const Sample* const* channels, int numChannels, int numSamples) noexcept
    {
        if (! isEnabled() || buffer.empty() || numSamples <= 0 || channels == nullptr || channels[0] == nullptr)
            return;

        const auto stereo = numChannels >= 2 && channels[1] != nullptr;
        auto at = head.load (std::memory_order_relaxed);

        for (int n = 0; n < numSamples; ++n)
        {
            const auto l = (float) channels[0][n];
            const auto r = stereo ? (float) channels[1][n] : l;
            buffer[at & mask] = 0.5f * (l + r);
            ++at;
        }

        head.store (at, std::memory_order_release);
    }

    /** The newest `count` samples, oldest first. Message thread. Returns how
        many were written, which is 0 until a panel has enabled the tap and the
        audio thread has run. */
    int read (float* destination, int count) const noexcept
    {
        if (destination == nullptr || count <= 0 || buffer.empty())
            return 0;

        const auto at = head.load (std::memory_order_acquire);
        const auto available = (size_t) count <= at ? (size_t) count : at;

        for (size_t i = 0; i < available; ++i)
            destination[i] = buffer[(at - available + i) & mask];

        return (int) available;
    }

    int capacity() const noexcept { return (int) buffer.size(); }

private:
    std::vector<float> buffer;
    size_t mask = 0;
    std::atomic<size_t> head { 0 };
    std::atomic<bool> enabled { false };
};

} // namespace bmo
