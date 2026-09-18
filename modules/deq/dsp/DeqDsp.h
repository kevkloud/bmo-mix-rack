#pragma once

#include "core/dsp/ModuleDsp.h"
#include "modules/deq/dsp/AutoGain.h"
#include "modules/deq/dsp/DspCore.h"
#include "modules/deq/params.h"
#include <algorithm>
#include <cmath>

namespace bmo::deq
{

/** BMO DEQ as the suite drives it: a params.h value array in, DspCore out.

    Everything the engine is not told stays at its own default: the knee is
    fixed at 6 dB and detection is peak (Frosty's lean band set, 2026-09-11),
    and bands are in series (spec/decisions.md). A mid or side band acts on
    that channel alone -- there is no continuous M/S blend.

    Three things the engine does not have:

    - **DEQ** (`active`) switches every band off. Bands fade in the engine
      rather than being cut, so switching it is click-free, and the latency
      it reports does not change: there is none either way.
    - **AUTO** (`auto_gain`) trims the output by the reciprocal of the static
      curve's broadband level (AutoGain.h). Worked out here, off the audio
      loop, and only when a static setting has moved; it rides the output's
      own smoothing, so switching it or dragging a band glides.
    - **Output** is a trim after the bands, smoothed like BMO Util's gain.

    A shelf's Q is capped at kShelfMaxQ here (params.h, effectiveQ), so
    automation or an old session asking for a resonant shelf gets the widest
    one the design is good for.
*/
class DeqDsp final : public ModuleDsp
{
public:
    void prepare (double sampleRate, int maxBlockSize, int numChannels) override
    {
        core.prepare (sampleRate, maxBlockSize, numChannels);
        grid = DesignGrid::make (sampleRate);
        const auto tau = 0.005;
        smoothCoeff = 1.0 - std::exp (-1.0 / (std::max (sampleRate, 1.0) * tau));
        autoDirty = true;
        reset();
    }

    void reset() override
    {
        core.reset();
        gainNow = gainTarget;
    }

    void setParams (const float* v, int count) override
    {
        if (count < kCount)
            return;

        const auto active = v[kActive] > 0.5f;

        for (int b = 0; b < kBands; ++b)
        {
            auto at = [v, b] (Control c) { return v[indexOf (b, c)]; };
            auto& band = settings.bands[(size_t) b];

            const auto shapeChoice = (int) std::lround (at (Control::shape));

            const BandSettings was = band;
            band.enabled     = active && at (Control::on) > 0.5f;
            band.shape       = shapeFor (shapeChoice);
            band.frequencyHz = at (Control::freq);
            band.q           = effectiveQ (shapeChoice, at (Control::q));
            band.gainDb      = at (Control::gain);
            band.placement   = placementFor ((int) std::lround (at (Control::place)));
            band.msAmount    = 1.0;

            if (band.enabled != was.enabled || band.shape != was.shape || band.frequencyHz != was.frequencyHz
                || band.q != was.q || band.gainDb != was.gainDb || band.placement != was.placement)
                autoDirty = true;

            auto& d = band.dynamics;
            d.enabled     = at (Control::dyn) > 0.5f;
            d.direction   = std::lround (at (Control::dir)) == 1 ? Direction::below : Direction::above;
            d.thresholdDb = at (Control::thr);
            d.rangeDb     = at (Control::range);
            d.ratio       = at (Control::ratio);
            d.attackMs    = at (Control::attack);
            d.releaseMs   = at (Control::release);
            d.kneeDb      = 6.0;
            d.rms         = false;
        }

        core.setSettings (settings);

        const auto autoOn = v[kAutoGain] > 0.5f;

        if (autoOn && autoDirty && grid.sampleRate > 0.0)
        {
            // Clamped, so a curve that is nearly all cut (a pair of steep
            // filters closing on each other) cannot ask for 40 dB of makeup.
            const auto mean = staticBroadbandGain (settings, grid);
            autoGain = std::clamp (1.0 / std::max (mean, 1.0e-6), kAutoMin, kAutoMax);
            autoDirty = false;
        }

        gainTarget = std::pow (10.0, (double) v[kOutput] / 20.0) * (autoOn ? autoGain : 1.0);

        if (! primed)
        {
            gainNow = gainTarget;
            primed = true;
        }
    }

    void process (float* const* channels, int numChannels, int numSamples) override
    {
        core.process (channels, numChannels, numSamples);

        const auto used = std::min (numChannels, 2);

        for (int i = 0; i < numSamples; ++i)
        {
            gainNow += smoothCoeff * (gainTarget - gainNow);

            for (int ch = 0; ch < used; ++ch)
                channels[ch][i] = (float) ((double) channels[ch][i] * gainNow);
        }
    }

    int latencyForParams (const float*, int) const override { return DspCore::latencySamples(); }

    float currentGainReductionDb() const noexcept override { return (float) core.currentGainReductionDb(); }

    /** A band index, or -1. Momentary: the panel holds it, nothing saves it. */
    void setSolo (int band) noexcept override { core.setSolo (band); }

    /** Post-EQ, which is what the panel draws. The engine carries a pre tap on
        the same terms if a second curve is ever wanted. */
    AnalyserTap* analyser() noexcept override { return &core.postTap(); }

    /** For the panel's curve and the tests: the engine as it stands. */
    const DspCore& engine() const noexcept { return core; }

    /** What AUTO last worked out, as a linear gain; 1 until it is switched on. */
    double autoGainNow() const noexcept { return autoGain; }

    /** AUTO's limits: +-18 dB. */
    static constexpr double kAutoMin = 0.125, kAutoMax = 8.0;

    static Shape shapeFor (int choice) noexcept
    {
        switch (choice)
        {
            case 1:  return Shape::lowShelf;
            case 2:  return Shape::highShelf;
            case 3:  return Shape::lowCut;
            case 4:  return Shape::highCut;
            default: return Shape::bell;
        }
    }

    static Placement placementFor (int choice) noexcept
    {
        return choice == 1 ? Placement::mid : choice == 2 ? Placement::side : Placement::stereo;
    }

private:
    DspCore core;
    Settings settings;
    DesignGrid grid;
    double smoothCoeff = 1.0, gainTarget = 1.0, gainNow = 1.0, autoGain = 1.0;
    bool primed = false, autoDirty = true;
};

inline std::unique_ptr<ModuleDsp> createDsp() { return std::make_unique<DeqDsp>(); }

} // namespace bmo::deq
