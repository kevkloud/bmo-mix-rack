#include "modules/reverb/dsp/ErTable.h"
#include "modules/reverb/dsp/TapTables.h"

#include <algorithm>
#include <cmath>

// **STAND-IN. This file is replaced wholesale by the table generator.**
//
// It exists so that the engine half of M2 has something to link and play
// while the generator half is being written in parallel: every type gets the
// same table, built from the placeholder `kReferenceTaps` for its core taps
// and an evenly spread, unjittered infill for the rest. Both channels carry
// the same set, so the stand-in is mono and makes no claim about gamma,
// combing, flamming or lateral fraction. Nothing here is a design decision,
// and no test may pin a number that comes from it.

namespace bmo::reverb
{
namespace
{
    ErTable makeStandIn()
    {
        ErTable t {};

        constexpr float kWindowMs = 90.0f;
        constexpr int   kInfill   = kErMaxTaps - kErCoreTaps;

        ErChannel ch {};

        int n = 0;
        int core = 0;
        int infill = 0;

        // Merge the core taps and the infill grid in time order.
        while (n < kErMaxTaps)
        {
            const float coreTime   = core < kErCoreTaps ? kReferenceTaps[core].timeMs : 1.0e9f;
            const float infillTime = infill < kInfill
                                       ? 5.0f + (kWindowMs - 5.0f) * ((float) infill + 0.5f) / (float) kInfill
                                       : 1.0e9f;

            if (coreTime <= infillTime)
            {
                const auto& r = kReferenceTaps[core++];
                ch.taps[n++] = { r.timeMs, r.gain, 0.0f, r.pan, std::min (3, (int) (r.timeMs / 25.0f)) };
            }
            else
            {
                const float theta = ((float) infill + 1.0f) / (float) kInfill;
                const float gain  = 0.5f * 7.3f / infillTime;
                ch.taps[n++] = { infillTime, gain, theta, 0.0f, std::min (3, (int) (infillTime / 25.0f)) };
                ++infill;
            }
        }

        ch.numTaps = kErMaxTaps;

        for (auto& v : t.variation)
            v = { ch, ch };

        t.combDelayMs   = 0.7f;
        t.combGain      = 0.7f;
        t.windowMs      = kWindowMs;
        t.windowClampMs = 100.0f;
        t.bandCutoffHz[0] = 12000.0f;
        t.bandCutoffHz[1] = 8000.0f;
        t.bandCutoffHz[2] = 5000.0f;
        t.bandCutoffHz[3] = 3000.0f;
        t.beta = 0.70f;
        t.seed = 0u;
        return t;
    }
}

const ErTable& erTableFor (int) noexcept
{
    static const ErTable standIn = makeStandIn();
    return standIn;
}

float erBandCutoffHzAt (const ErTable& table, int band, float sizeM) noexcept
{
    constexpr float kKappa = 0.2f;
    const auto b = band < 0 ? 0 : (band >= kErBands ? kErBands - 1 : band);
    const auto s = sizeM > 0.01f ? sizeM : 0.01f;
    return table.bandCutoffHz[b] * std::pow (kReferenceSizeM / s, kKappa);
}

} // namespace bmo::reverb
