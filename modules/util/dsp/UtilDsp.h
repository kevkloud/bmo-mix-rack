#pragma once

#include "core/dsp/ModuleDsp.h"
#include "modules/util/params.h"
#include <algorithm>
#include <cmath>

namespace bmo::util
{

/** Gain, polarity, mono, width and pan, in that order. Zero latency, no
    oversampling, nothing nonlinear.

    Pan is a balance law rather than constant power: at centre both channels
    pass at unity, and turning towards one side only attenuates the other.
    A utility that took 3 dB off the middle by default would not be one.

    Width is mid/side: the side signal is scaled from nothing (mono) through
    unity to double. Mono sums to (L+R)/2 on both channels, which is the same
    as width 0 and is kept as its own switch because a switch is what you reach
    for when checking a mix.
*/
class UtilDsp final : public ModuleDsp
{
public:
    void prepare (double sampleRate, int, int) override
    {
        // ~5 ms one-pole on everything continuous, so an automated gain move
        // does not zipper.
        const auto tau = 0.005;
        smoothCoeff = (float) (1.0 - std::exp (-1.0 / (std::max (sampleRate, 1.0) * tau)));
        reset();
    }

    void reset() override
    {
        gainCur = gainTarget; widthCur = widthTarget;
        panLCur = panLTarget; panRCur = panRTarget;
        signLCur = signLTarget; signRCur = signRTarget; monoCur = monoTarget;
    }

    void setParams (const float* v, int count) override
    {
        if (count < Index::count)
            return;

        gainTarget  = std::pow (10.0f, v[gain] / 20.0f);
        widthTarget = v[width] * 0.01f;

        const auto p = std::clamp (v[pan] * 0.01f, -1.0f, 1.0f);
        panLTarget = std::min (1.0f, 1.0f - p);
        panRTarget = std::min (1.0f, 1.0f + p);

        // The three switches ride the same 5 ms one-pole as the knobs. A
        // polarity flip is a sign that passes through zero on the way, a 5 ms
        // fade out and back in; mono is a crossfade into the sum. Until 0.2.4
        // all three were applied per sample the instant the parameter changed,
        // which on a held bass note is a step of twice the sample.
        signLTarget = v[phaseL] > 0.5f ? -1.0f : 1.0f;
        signRTarget = v[phaseR] > 0.5f ? -1.0f : 1.0f;
        monoTarget  = v[mono] > 0.5f ? 1.0f : 0.0f;

        if (! primed)
        {
            reset();
            primed = true;
        }
    }

    void process (float* const* channels, int numChannels, int numSamples) override
    {
        if (numChannels < 1)
            return;

        auto* l = channels[0];
        auto* r = numChannels > 1 ? channels[1] : nullptr;

        for (int i = 0; i < numSamples; ++i)
        {
            gainCur  += smoothCoeff * (gainTarget  - gainCur);
            widthCur += smoothCoeff * (widthTarget - widthCur);
            panLCur  += smoothCoeff * (panLTarget  - panLCur);
            panRCur  += smoothCoeff * (panRTarget  - panRCur);
            signLCur += smoothCoeff * (signLTarget - signLCur);
            signRCur += smoothCoeff * (signRTarget - signRCur);
            monoCur  += smoothCoeff * (monoTarget  - monoCur);

            if (r == nullptr)
            {
                l[i] *= gainCur * signLCur;
                continue;
            }

            auto a = l[i] * gainCur * signLCur;
            auto b = r[i] * gainCur * signRCur;

            const auto sum = 0.5f * (a + b);
            a += monoCur * (sum - a);
            b += monoCur * (sum - b);

            const auto mid  = 0.5f * (a + b);
            const auto side = 0.5f * (a - b) * widthCur;

            l[i] = (mid + side) * panLCur;
            r[i] = (mid - side) * panRCur;
        }
    }

    int latencyForParams (const float*, int) const override { return 0; }

private:
    float smoothCoeff = 1.0f;
    float gainTarget = 1.0f, gainCur = 1.0f;
    float widthTarget = 1.0f, widthCur = 1.0f;
    float panLTarget = 1.0f, panLCur = 1.0f, panRTarget = 1.0f, panRCur = 1.0f;
    float signLTarget = 1.0f, signLCur = 1.0f, signRTarget = 1.0f, signRCur = 1.0f;
    float monoTarget = 0.0f, monoCur = 0.0f;
    bool  primed = false;
};

inline std::unique_ptr<ModuleDsp> createDsp() { return std::make_unique<UtilDsp>(); }

} // namespace bmo::util
