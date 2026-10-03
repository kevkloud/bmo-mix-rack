// BMO Opto: what its switches do to the audio at the moment they move.
//
// OptoDspTests holds what the module does once it has settled; this file
// holds the transitions -- Mode, Link or Color changing while a signal
// passes -- which none of those tests can see, because each of them builds a
// fresh core, sets it once and measures the result.

#include "modules/opto/dsp/DspCore.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <functional>
#include <string>
#include <vector>

using namespace bmo::opto;

namespace
{

constexpr double kPi = 3.14159265358979323846;
constexpr double kSampleRate = 48000.0;
constexpr int    kBlock = 512;
constexpr double kProgrammeAmp = 0.17782794;   // -18 dBFS RMS

int failures = 0, checks = 0;

void check (bool condition, const std::string& what)
{
    ++checks;

    if (! condition)
    {
        std::printf ("FAIL  %s\n", what.c_str());
        ++failures;
    }
}

DspCore::Params params (Mode mode, float crushPercent, bool link = true, bool color = false, float levelDb = 0.0f)
{
    DspCore::Params p;
    p.mode = mode;
    p.crushPercent = crushPercent;
    p.link = link;
    p.color = color;
    p.levelDb = levelDb;
    return p;
}

std::vector<float> sine (double hz, double seconds, double amplitude, double phase = 0.0)
{
    std::vector<float> out ((size_t) (seconds * kSampleRate));

    for (size_t i = 0; i < out.size(); ++i)
        out[i] = (float) (amplitude * std::sin (2.0 * kPi * hz * (double) i / kSampleRate + phase));

    return out;
}

/** Render an L/R pair through one core, the parameters chosen per block from
    the block's first sample, as a host delivers them. Returns { L, R }. */
std::pair<std::vector<float>, std::vector<float>> render (std::vector<float> l, std::vector<float> r,
                                                          const std::function<DspCore::Params (size_t)>& paramsAt,
                                                          int numChannels = 2)
{
    DspCore core;
    core.setParams (paramsAt (0));
    core.prepare (kSampleRate, kBlock, numChannels);

    for (size_t start = 0; start < l.size(); start += kBlock)
    {
        const auto n = (int) std::min<size_t> (kBlock, l.size() - start);
        core.setParams (paramsAt (start));
        float* channels[2] { l.data() + start, r.data() + start };
        core.process (channels, numChannels, n);
    }

    return { l, r };
}

double largestStep (const std::vector<float>& y, size_t from, size_t to)
{
    double m = 0.0;
    for (size_t i = std::max<size_t> (from, 1); i < to && i < y.size(); ++i)
        m = std::max (m, (double) std::abs (y[i] - y[i - 1]));
    return m;
}

double rmsDb (const std::vector<float>& v, size_t from, size_t count)
{
    double sum = 0.0;
    for (size_t i = from; i < from + count && i < v.size(); ++i) sum += (double) v[i] * v[i];
    return 10.0 * std::log10 (std::max (sum / (double) count, 1.0e-20));
}

size_t blockAt (double seconds) { return (size_t) (seconds * kSampleRate) / kBlock * kBlock; }

/** One cycle of the 220 Hz programme, in samples. */
const size_t kCycle = (size_t) std::llround (kSampleRate / 220.0);

//==============================================================================
/** Leaving a mode and coming back must find it where a core that never left
    would be.

    The cell that Mode was not using used to be frozen, holding whatever it
    held when it was last switched away from. A loud passage, a switch away
    just after it and a switch back three seconds later brought the cell back
    still holding that passage: the output sat 11.1 dB (Tele) and 6.7 dB
    (Stressed) below a run that had stayed in the mode, and stayed low for
    more than three seconds. */
void testAModeRoundTripComesBackWhereItWouldHaveBeen()
{
    for (const auto mode : { Mode::La2a, Mode::Distressor })
    {
        const auto name  = std::string (mode == Mode::La2a ? "Tele" : "Stressed");
        const auto other = mode == Mode::La2a ? Mode::Distressor : Mode::La2a;

        // The programme, 18 dB hotter for 100 ms at 2 s.
        auto x = sine (220.0, 9.0, kProgrammeAmp);
        for (auto i = (size_t) (2.0 * kSampleRate); i < (size_t) (2.1 * kSampleRate); ++i)
            x[i] *= 7.943282f;

        const auto away = blockAt (2.1), back = blockAt (5.1);

        const auto stayed  = render (x, x, [&] (size_t) { return params (mode, 100.0f); }, 1).first;
        const auto tripped = render (x, x, [&] (size_t s) { return params (s >= away && s < back ? other : mode, 100.0f); }, 1).first;

        for (const auto afterSec : { 0.1, 0.5, 1.0 })
        {
            const auto at = back + (size_t) (afterSec * kSampleRate);
            const auto low = rmsDb (tripped, at, kCycle) - rmsDb (stayed, at, kCycle);

            check (std::abs (low) < 0.5,
                   name + ": " + std::to_string (afterSec) + " s after coming back, the output is within 0.5 dB of a run that never left ("
                     + std::to_string (low) + " dB)");
        }
    }
}

} // namespace

//==============================================================================
int main()
{
    testAModeRoundTripComesBackWhereItWouldHaveBeen();

    std::printf ("%d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
