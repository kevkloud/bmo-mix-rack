/*
    Offline measurement harness for BMO FET.

    Links the DSP core directly -- no plugin host, no GUI, no JUCE -- the same
    shape as tools/measure/opto and tools/measure/vcomp.

    **It is registered before it is useful, and that is deliberate.** The core
    it drives is still the placeholder in modules/fetcomp/dsp/DspCore.h, so
    there is no curve, no timing and no distortion to print yet. What it can
    answer honestly today are the two things the placeholder really does:

        measure_fetcomp latency          the reported delay at every factor,
                                         and the delay actually measured by
                                         running an impulse through the core
        measure_fetcomp positions        the attack/release position -> time
                                         law, as the value strings print it

    docs/1176-comp/11-integration-and-test-plan.md 3 lists the modes this grows
    when the DSP lands -- curve, timing, thd, alias, slam, allbuttons, bench,
    render, gen -- each taking a voicing, with WAVs going to
    packages/fetcomp-listening/ (gitignored). **Every recorded result names the
    machine it ran on (AURORA / ICE QUEEN) in testing-notes/.**
*/

#include "modules/fetcomp/dsp/DspCore.h"

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

using namespace bmo::fetcomp;

namespace
{

constexpr double kSampleRate = 48000.0;

/** Where an impulse comes back out, in samples: the index of the largest
    magnitude in the core's output. The reported latency has to equal this, or
    the module is lying to the host about its delay. */
int measuredDelay (int factor)
{
    DspCore core;
    core.prepare (kSampleRate, 512, 2);

    DspCore::Params p;
    p.oversampling = factor;
    core.setParams (p);
    core.reset();

    constexpr int kLength = 512;
    std::vector<float> left ((size_t) kLength, 0.0f), right ((size_t) kLength, 0.0f);
    left[0] = right[0] = 1.0f;

    float* channels[2] { left.data(), right.data() };
    core.process (channels, 2, kLength);

    int best = 0;
    float peak = 0.0f;

    for (int n = 0; n < kLength; ++n)
        if (std::abs (left[(size_t) n]) > peak)
        {
            peak = std::abs (left[(size_t) n]);
            best = n;
        }

    return peak > 0.0f ? best : -1;
}

int latency()
{
    std::printf ("%-8s %-10s %-10s\n", "factor", "reported", "measured");

    auto failures = 0;

    for (const auto factor : { 1, 2, 4 })
    {
        const auto reported = DspCore::latencyFor (factor);
        const auto measured = measuredDelay (factor);

        std::printf ("%-8d %-10d %-10d%s\n", factor, reported, measured,
                     reported == measured ? "" : "   MISMATCH");

        if (reported != measured)
            ++failures;
    }

    std::printf ("\n0 / 40 / 60 at Off / 2x / 4x, zero at the default, and the same\n"
                 "in both voicings -- docs/1176-comp/10-dsp-spec.md 11.\n");

    return failures == 0 ? 0 : 1;
}

int positions()
{
    std::printf ("%-10s %-14s %-14s\n", "position", "attack", "release");

    for (int i = 1; i <= 7; ++i)
    {
        const auto p = (float) i;
        std::printf ("%-10d %-14.1f %-14.1f\n", i,
                     (double) attackMicrosecondsFor (p),
                     (double) releaseMillisecondsFor (p));
    }

    std::printf ("\nMicroseconds and milliseconds. 7 is the fast end: the parameter is\n"
                 "the knob position and it runs backwards, like the hardware's does.\n");

    return 0;
}

} // namespace

int main (int argc, char** argv)
{
    const std::string mode { argc > 1 ? argv[1] : "latency" };

    if (mode == "latency")   return latency();
    if (mode == "positions") return positions();

    std::fprintf (stderr, "usage: measure_fetcomp <latency|positions>\n");
    return 2;
}
