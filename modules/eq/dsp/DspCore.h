#pragma once

#include "EqNetwork.h"
#include "Saturation.h"
#include "core/dsp/Oversampler.h"
#include <array>
#include <vector>

namespace bmo::eq
{

/** One-pole parameter smoother.

    Snaps to the target once it is within epsilon, so a settled parameter
    compares exactly equal and the coefficient recomputation can be skipped.
*/
class Smoother
{
public:
    void prepare (double controlRateHz, double timeMs) noexcept
    {
        const auto tau = std::max (timeMs, 0.01) * 0.001;
        coeff = (float) (1.0 - std::exp (-1.0 / (std::max (controlRateHz, 1.0) * tau)));
    }

    void snap (float v) noexcept        { current = target = v; }
    void setTarget (float t) noexcept   { target = t; }
    float value() const noexcept        { return current; }

    float tick() noexcept
    {
        current += coeff * (target - current);

        if (std::abs (target - current) < 1.0e-5f)
            current = target;

        return current;
    }

private:
    float coeff = 1.0f, current = 0.0f, target = 0.0f;
};

//==============================================================================
/** Everything the plugin does to audio, with no dependency on JUCE's plugin
    layer or on a host. Takes plain values and raw buffers, so the measurement
    harness and the unit tests can drive the real signal path directly.

    Frequency and gain parameters are smoothed and the filter coefficients are
    recomputed at control rate (once per sub-block) rather than per sample. The
    TPT filters keep their state meaningful across a coefficient change, so a
    stepped selector glides between switch positions instead of clicking. If any
    residual artefact ever shows up, the fallback is to crossfade between two
    network instances over ~10 ms.
*/
class DspCore
{
public:
    struct Params
    {
        int hfFreqIndex  = 1;   // 10k / 12k / 16k
        int midFreqIndex = 2;
        int lfFreqIndex  = 1;
        int hpfIndex     = 0;   // 0 == off
        int lpfIndex     = 0;   // 0 == off

        float hfGainDb  = 0.0f;
        float midGainDb = 0.0f;
        float lfGainDb  = 0.0f;
        bool  midHiQ    = false;

        float inputGainDb   = 0.0f;
        float outputLevelDb = 0.0f;
        float mixPercent    = 100.0f;

        bool eqIn        = true;
        bool phaseInvert = false;
        bool autoGain    = false;

        int oversampling = 1;   // 1, 2, 4 or 8
    };

    void prepare (double sampleRate, int maxBlockSize, int numChannels, int oversampleFactor = 2);
    void reset() noexcept;

    /** Round-trip delay of the oversampling filters, in samples at the host's
        rate. Reported to the host so plugin delay compensation can undo it. */
    int getLatencySamples() const noexcept { return latencySamples; }

    /** The rate the equaliser actually runs at, which is the host rate times
        the oversampling factor. The curve display needs it so the drawn curve
        matches what the audio path does. */
    double getEqSampleRate() const noexcept { return effectiveRate; }

    /** Called once per block, before process(). Cheap: stores targets only. */
    void setParams (const Params&) noexcept;

    void process (float* const* channels, int numChannels, int numSamples) noexcept;

    static constexpr int kSubBlock = 32;

private:
    void updateCoefficients (int activeChannels) noexcept;
    void applyOversampling (int factor);

    double sampleRate = 44100.0;
    double effectiveRate = 44100.0;
    int    latencySamples = 0;

    std::array<EqNetwork, 2> networks;

    // The signal chain of the original: input transformer, class-A preamp, the
    // equaliser, class-A output amp, output transformer. The nonlinear stages
    // sit inside the oversampled region because that is where they alias; the
    // equaliser is linear but rides along, which also spares the 1084's 16 kHz
    // shelf the bilinear warping it would suffer at 48 kHz.
    std::array<TransformerStage, 2> inputTransformer, outputTransformer;
    std::array<ClassAStage, 2>      preamp, outputAmp;
    std::array<Oversampler, 2>      oversamplers;

    // The dry path of the Mix control has to be delayed to match, or a partial
    // blend combs and a full bypass fails to null.
    std::vector<float> dryDelay;
    int dryWrite = 0, dryLength = 1, dryStride = 0;

    Smoother hfFreqSm, midFreqSm, lfFreqSm;     // smoothed in log2(Hz)
    Smoother hfGainSm, midGainSm, lfGainSm;
    Smoother inputGainSm, outputLevelSm, mixSm, autoGainSm;

    Params   params;
    EqSettings currentSettings;
    bool     settingsValid = false;
    bool     autoGainApplied = false;   // the Auto Gain state the smoother's target was last set from

    int maxBlock = 0, maxChannels = 0;
    int currentFactor = 0;
};

} // namespace bmo::eq
