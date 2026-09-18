#include "modules/tune/dsp/ClassicEngine.h"
#include <algorithm>
#include <cmath>

namespace bmo::tune
{

void ClassicEngine::prepare (double sampleRate, double longestPeriod)
{
    fs = sampleRate;
    rest = contract::liveRestSamples (fs);

    // The deepest read: the rest lag, a period of swing above it and one more
    // of margin for a splice in flight, plus a fade's drift and the kernel.
    // Deep enough for the widest rest any note can ask for, plus a period of
    // swing above it, one more for a splice in flight, a fade and the kernel.
    const auto widestRest = std::max ((double) rest, contract::liveRest (fs, longestPeriod));
    const auto deepest = widestRest + 2.0 * longestPeriod + 2 * Sinc::kTaps + 64;
    int size = 1;
    while (size < (int) deepest)
        size <<= 1;

    ring.assign ((size_t) size, 0.0f);
    mask = size - 1;

    kernels.build (fs);

    reset();
}

void ClassicEngine::reset()
{
    std::fill (ring.begin(), ring.end(), 0.0f);
    write = 0;
    lag = rest;
    ratio = 1.0;
    lastPeriod = 0.0;
    fading = false;
    spliced = false;
    splices = 0;
}

double ClassicEngine::read (const Sinc& t, double lagBehindNewest) const noexcept
{
    const auto newest = write - 1;
    return t.read (ring.data(), mask, (double) newest - lagBehindNewest);
}

void ClassicEngine::startFade (double newLag, int length, bool equalPower) noexcept
{
    // A fade already running is abandoned where it is: its outgoing read is
    // replaced by the incoming one, which is what the listener was hearing
    // most of by then anyway.
    fadeLag = lag;
    lag = newLag;
    fading = true;
    fadeEqualPower = equalPower;
    fadeLength = std::max (1, length);
    fadePosition = 0;
    fadeDiffAcc = fadeRefAcc = 0.0;
}

float ClassicEngine::process (float input, double cents, double period, bool settled) noexcept
{
    ring[(size_t) write] = std::isfinite (input) ? input : 0.0f;
    write = (write + 1) & mask;
    spliced = false;

    ratio = std::exp2 (std::clamp (cents, -1200.0, 1200.0) / 1200.0);
    if (period > 1.0)
        lastPeriod = period;

    const auto step = 1.0 - ratio;
    lag += step;
    if (fading)
        fadeLag = std::max ((double) kFloor, fadeLag + step);

    const auto restNow = contract::liveRest (fs, lastPeriod);

    if (lastPeriod > 1.0 && ! fading)
    {
        const auto T = lastPeriod;
        const auto fade = std::max (16, (int) std::lround (0.5 * T));

        // The window. Its floor is raised by however far the outgoing read
        // will drift during a fade at the ratio in force, so a splice never
        // asks the kernel for a sample that has not arrived.
        const auto lo = kFloor + fade * std::max (0.0, ratio - 1.0);
        const auto hi = restNow + T;

        if (lag < lo || lag > hi)
        {
            // Whole periods only: the waveform one cycle away is the same
            // waveform. Usually one; more after a leap to a much higher note
            // shrank the window underneath the pointer.
            const auto k = lag < lo ? std::ceil ((lo - lag) / T) : -std::ceil ((lag - hi) / T);
            startFade (lag + k * T, fade, false);
            fadeWasSplice = true;
            spliced = true;
            ++splices;
        }
        else if (settled && std::abs (lag - restNow) > 0.5)
        {
            // Home, over 5 ms of uncorrelated material.
            startFade (restNow, std::max (16, (int) (0.005 * fs)), true);
        }
    }
    else if (lastPeriod <= 1.0 && settled && ! fading && std::abs (lag - restNow) > 0.5)
    {
        startFade (restNow, std::max (16, (int) (0.005 * fs)), true);
    }

    lag = std::max ((double) kFloor, lag);

    const auto& table = kernels.forRatio (ratio);
    auto y = read (table, lag);

    mismatch = 0.0;

    if (fading)
    {
        const auto outgoing = read (table, fadeLag);
        const auto incoming = y;
        const auto t = (double) (fadePosition + 1) / (double) (fadeLength + 1);

        // How badly this splice lands. The two reads are meant to be one
        // cycle apart on the same waveform, so across the fade they should
        // very nearly agree; whatever they do not agree by is the step the
        // listener hears. Accumulated over the fade and reported once, as an
        // RMS difference against the material's own RMS: about 0 for a jump
        // that lands in phase, and of order 1.4 for one that lands anywhere.
        //
        // This is the measurement the splice COUNT was standing in for and
        // should not have been. Frosty timestamped the pops he hears on
        // Failure (2026-09-12): seven of seven were splices, but only seven
        // of thirty-eight splices were audible at all, so a count cannot
        // tell a bad one from a silent one and driving it down did not drive
        // the pops down. testing-notes/tune-blind-2026-09-12.md.
        fadeDiffAcc += (incoming - outgoing) * (incoming - outgoing);
        fadeRefAcc += outgoing * outgoing + incoming * incoming;

        if (fadeEqualPower)
        {
            const auto a = std::cos (0.5 * kPi * t), b = std::sin (0.5 * kPi * t);
            y = a * outgoing + b * y;
        }
        else
        {
            y = (1.0 - t) * outgoing + t * y;
        }

        if (++fadePosition >= fadeLength)
        {
            fading = false;

            if (fadeWasSplice)
            {
                mismatch = fadeRefAcc > 1.0e-20 ? std::sqrt (2.0 * fadeDiffAcc / fadeRefAcc) : 0.0;
                worstMismatch = std::max (worstMismatch, mismatch);
                fadeWasSplice = false;
            }
        }
    }

    return std::isfinite (y) ? (float) y : 0.0f;
}

} // namespace bmo::tune
