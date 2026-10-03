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
    still holding that passage: the output sat 11.3 dB (Tele) and 6.7 dB
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

//==============================================================================
/** The bound the repository holds a switch to: the largest sample-to-sample
    step in the quarter second after it, against the steady signal's own
    largest step before it and once it has settled again. Worst of four
    starting phases an eighth of a cycle apart, so a switch cannot hide by
    landing where the two sides happen to agree. L carries the programme at
    -18 dBFS RMS and R the same signal `rightScale` times as large; the
    figure is read on `channel`. The switch lands on a block boundary, as a
    host's does, after three seconds for the cells to settle. */
double switchStepRatio (const DspCore::Params& a, const DspCore::Params& b, int channel = 0,
                        double rightScale = 1.0, double amplitude = kProgrammeAmp)
{
    const auto sw = blockAt (3.0);
    double worst = 0.0;

    for (int k = 0; k < 4; ++k)
    {
        const auto l = sine (220.0, 5.0, amplitude, k * kPi / 4.0);
        auto r = l;
        for (auto& v : r) v = (float) (v * rightScale);

        const auto out = render (l, r, [&] (size_t s) { return s >= sw ? b : a; });
        const auto& y  = channel == 0 ? out.first : out.second;

        const auto before = largestStep (y, sw - (size_t) (0.15 * kSampleRate), sw);
        const auto after  = largestStep (y, (size_t) (4.7 * kSampleRate), y.size());
        const auto during = largestStep (y, sw, sw + (size_t) (0.25 * kSampleRate));

        worst = std::max (worst, during / std::max (before, after));
    }

    return worst;
}

/** Mode is two different circuits at two different levels, and moving
    between them in one sample stepped the output by 45 to 156 times the
    signal's own largest step. */
void testModeCrossesOverWithoutAStep()
{
    for (const auto crush : { 60.0f, 100.0f })
    {
        const auto toStressed = switchStepRatio (params (Mode::La2a, crush), params (Mode::Distressor, crush));
        const auto toTele     = switchStepRatio (params (Mode::Distressor, crush), params (Mode::La2a, crush));

        check (toStressed < 1.5, "Tele -> Stressed at crush " + std::to_string ((int) crush)
                                   + " steps by " + std::to_string (toStressed) + " times the signal's own step (under 1.5)");
        check (toTele < 1.5,     "Stressed -> Tele at crush " + std::to_string ((int) crush)
                                   + " steps by " + std::to_string (toTele) + " times the signal's own step (under 1.5)");
    }
}

/** Link decides whose cell sets the quieter channel's gain, and the two
    answers are far apart: with L at -18 and R at -30 dBFS RMS, unlinking
    stepped R by 61 to 85 times its own largest step and left it 14.7 dB
    (Tele) and 20.6 dB (Stressed) louder in one sample; linking stepped it by
    18 to 22 times. */
void testLinkCrossesOverWithoutAStep()
{
    constexpr double quieter = 0.2512;   // R 12 dB under L

    for (const auto mode : { Mode::La2a, Mode::Distressor })
    {
        const auto name = std::string (mode == Mode::La2a ? "Tele" : "Stressed");

        const auto unlink = switchStepRatio (params (mode, 100.0f, true), params (mode, 100.0f, false), 1, quieter);
        const auto link   = switchStepRatio (params (mode, 100.0f, false), params (mode, 100.0f, true), 1, quieter);

        check (unlink < 1.5, name + ": Link off steps the quieter channel by " + std::to_string (unlink)
                               + " times its own step (under 1.5)");
        check (link < 1.5,   name + ": Link on steps the quieter channel by " + std::to_string (link)
                               + " times its own step (under 1.5)");

        // And unlinking does not move the quieter channel's level at once:
        // its own cell starts from what the shared one was holding.
        const auto sw = blockAt (3.0);
        const auto l  = sine (220.0, 5.0, kProgrammeAmp);
        auto r = l;
        for (auto& v : r) v = (float) (v * quieter);

        const auto out  = render (l, r, [&] (size_t s) { return params (mode, 100.0f, s < sw); }).second;
        const auto jump = rmsDb (out, sw, kCycle) - rmsDb (out, sw - (size_t) (0.05 * kSampleRate), kCycle);

        check (std::abs (jump) < 1.0,
               name + ": the quieter channel's level across Link off moves by " + std::to_string (jump) + " dB in the first cycle (under 1)");
    }
}

/** Color is a soft clip, which is nothing at all on a quiet signal and a
    different waveform on a loud one. Measured where it bends: a tone peaking
    at -6 dBFS with 6 dB of LEVEL, nothing being reduced. */
void testColorCrossesOverWithoutAStep()
{
    const auto on  = switchStepRatio (params (Mode::Distressor, 0.0f, true, false, 6.0f),
                                      params (Mode::Distressor, 0.0f, true, true, 6.0f), 0, 1.0, 0.5);
    const auto off = switchStepRatio (params (Mode::Distressor, 0.0f, true, true, 6.0f),
                                      params (Mode::Distressor, 0.0f, true, false, 6.0f), 0, 1.0, 0.5);

    check (on < 1.5,  "Color on steps by " + std::to_string (on) + " times the signal's own step (under 1.5)");
    check (off < 1.5, "Color off steps by " + std::to_string (off) + " times the signal's own step (under 1.5)");
}

} // namespace

//==============================================================================
int main()
{
    testAModeRoundTripComesBackWhereItWouldHaveBeen();
    testModeCrossesOverWithoutAStep();
    testLinkCrossesOverWithoutAStep();
    testColorCrossesOverWithoutAStep();

    std::printf ("%d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
