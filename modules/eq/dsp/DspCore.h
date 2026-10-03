#pragma once

#include "EqNetwork.h"
#include "Saturation.h"
#include "core/dsp/Oversampler.h"
#include "core/dsp/SwitchFade.h"
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

    The switches are different: each changes the signal path rather than a
    coefficient, so each crosses over in EqNetwork::kSwitchFadeMs instead of
    stepping (core/dsp/SwitchFade.h). EQ In crosses between the network and
    a straight wire, Phase ramps the polarity through zero, Hi-Q glides the
    mid's Q geometrically from one width to the other, and the cuts cross
    inside the network. With nothing switching, every one of them is idle
    and the output is bit-identical to what it was before they existed.
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
        rate. Reported to the host so plugin delay compensation can undo it.
        After a change of oversampling it is the new factor's from the first
        process() on, while the audio dips through the change. */
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
    void updateCoefficients (int activeChannels, int numSamples) noexcept;
    void applyOversampling (int factor);
    void switchOversampling (int activeChannels, float inGain) noexcept;

    /** One host-rate sample through the oversampled chain, before the output
        gain. Shared by process() and the warm-up of a new oversampling path. */
    float runWet (size_t ch, float x, int factor, bool eqFading, float eqAmount) noexcept;

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
    // blend combs and a full bypass fails to null. The ring is one length for
    // every factor, read at the running path's latency, and long enough --
    // twice the longest latency -- to hold the whole span of the oversampling
    // filters, which is what a new path is run over when the factor changes.
    static constexpr int kDryRing = 2 * Oversampler::kMaxLatency + 2;
    std::vector<float> dryDelay;
    int dryWrite = 0, dryStride = 0, dryLatency = 0;

    // A change of oversampling waits at the bottom of this dip; see process().
    bmo::dsp::Dip oversamplingDip;
    int pendingFactor = 1;
    bool running = false;   // false until the first process() after prepare() or reset()

    Smoother hfFreqSm, midFreqSm, lfFreqSm;     // smoothed in log2(Hz)
    Smoother hfGainSm, midGainSm, lfGainSm;
    Smoother inputGainSm, outputLevelSm, mixSm, autoGainSm;

    // The switches' fades, at the host's rate. eqInMix is 0 out and 1 in;
    // polarity is the sign itself, ramped through zero; hiQAmount is 0 for
    // the normal width and 1 for Hi-Q, advanced a sub-block at a time.
    bmo::dsp::Ramp eqInMix, polarity, hiQAmount;

    Params   params;
    EqSettings currentSettings;
    bool     settingsValid = false;
    bool     autoGainApplied = false;   // the Auto Gain state the smoother's target was last set from
    bool     autoGainPrimed  = false;   // false until the first coefficient update after prepare() or reset()

    int maxBlock = 0, maxChannels = 0;
    int currentFactor = 0;
};

} // namespace bmo::eq
