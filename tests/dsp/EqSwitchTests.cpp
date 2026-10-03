// BMO EQ: what its switches do to the audio at the moment they move.
//
// EqDspTests holds what the module does once it has settled; this file holds
// the transitions -- a control changing while a signal passes, or after one
// has stopped -- which none of those tests can see, because each of them
// builds a fresh core, sets it once and measures the result.

#include "modules/eq/dsp/DspCore.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <iostream>
#include <string>
#include <vector>

using namespace bmo::eq;

namespace
{
    int failures = 0;
    constexpr double kPi = 3.14159265358979323846;

    void check (bool ok, const std::string& what)
    {
        if (! ok) { std::cerr << "FAIL: " << what << '\n'; ++failures; }
    }

    std::string rateName (double fs)
    {
        return std::to_string ((int) std::lround (fs / 100.0) / 10) + "."
             + std::to_string ((int) std::lround (fs / 100.0) % 10) + " kHz";
    }

    /** Stereo render through one core, with the parameters chosen per block
        from the block's first sample. Both channels carry the same signal. */
    std::vector<float> render (DspCore& core, std::vector<float> signal, int block,
                               const std::function<DspCore::Params (size_t)>& paramsAt)
    {
        std::vector<float> right = signal;

        for (size_t start = 0; start < signal.size(); start += (size_t) block)
        {
            const auto n = (int) std::min ((size_t) block, signal.size() - start);
            core.setParams (paramsAt (start));
            float* channels[2] { signal.data() + start, right.data() + start };
            core.process (channels, 2, n);
        }

        return signal;
    }
}

int main()
{
    //== 1. A cut switched back on does not replay what it heard before ======
    // Low Cut and High Cut used to keep their filter state while switched
    // off, frozen at whatever the signal was doing when they went off, and
    // switching one back on released it: a 100 Hz tone stopped, 0.75 s of
    // digital silence, Low Cut 360 back on, and the output peaked at -6.5 dBFS.
    // A filter that has never been used gives about 4e-11 there.
    {
        for (double fs : { 44100.0, 48000.0, 96000.0, 192000.0 })
            for (int highCut = 0; highCut < 2; ++highCut)
                for (int choice = 1; choice <= (highCut ? 5 : 4); ++choice)
                {
                    DspCore::Params on;
                    (highCut ? on.lpfIndex : on.hpfIndex) = choice;
                    DspCore::Params off = on;
                    (highCut ? off.lpfIndex : off.hpfIndex) = 0;

                    DspCore core;
                    core.prepare (fs, 512, 2, on.oversampling);
                    core.setParams (on);

                    // On with a tone, off while it plays, the tone stops, and
                    // the cut comes back on 0.75 s into the silence. The
                    // chain's own tail has died away to about 1e-10 by then,
                    // so anything near the bound is the filter's.
                    const auto offAt    = (size_t) (0.5 * fs) / 512 * 512 + 3 * 512;
                    const auto silentAt = (size_t) (0.75 * fs);
                    const auto onAt     = (size_t) (1.5 * fs) / 512 * 512;
                    const auto hz       = highCut ? 3000.0 : 100.0;

                    std::vector<float> x ((size_t) (2.0 * fs));
                    for (size_t i = 0; i < silentAt; ++i)
                        x[i] = (float) (0.5 * std::sin (2.0 * kPi * hz * (double) i / fs));

                    const auto y = render (core, x, 512, [&] (size_t s)
                                           { return s >= offAt && s < onAt ? off : on; });

                    double peak = 0.0;
                    for (size_t i = onAt; i < y.size(); ++i)
                        peak = std::max (peak, (double) std::abs (y[i]));

                    check (peak < 1.0e-6,
                           std::string (highCut ? "High Cut " : "Low Cut ")
                               + std::to_string ((int) (highCut ? lpfFreqHz (choice) : hpfFreqHz (choice)))
                               + " re-enabled in silence at " + rateName (fs)
                               + " must stay silent, peaked at " + std::to_string (peak));
                }
    }

    if (failures == 0)
        std::cout << "All EQ switch tests passed.\n";

    return failures == 0 ? 0 : 1;
}
