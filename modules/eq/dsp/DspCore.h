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
/** A one-pole smoother ticked every sample that lands on its target.

    For a value applied straight to the audio, where Smoother's once-per-
    sub-block step shows: Mix's first step of a 0 -> 100 move was 3.56 % of
    the way at 44.1 kHz, held for 32 samples. A float32 one-pole does not
    land on its own (see TrimSmoother); this one lands, exactly, once within
    `landWithin` of the target or when a sample makes no progress, and then
    returns the target without working. Snapped or landed, the value is the
    target itself, bit for bit.
*/
class LandingSmoother
{
public:
    explicit LandingSmoother (float landWithinValue) noexcept : landWithin (landWithinValue) {}

    void prepare (double sampleRate, double timeMs) noexcept
    {
        const auto tau = std::max (timeMs, 0.01) * 0.001;
        coeff = (float) (1.0 - std::exp (-1.0 / (std::max (sampleRate, 1.0) * tau)));
    }

    void snap (float v) noexcept        { current = target = v; }
    void setTarget (float t) noexcept   { target = t; }
    float value() const noexcept        { return current; }
    bool isSettled() const noexcept     { return ! (current < target) && ! (target < current); }

    /** One sample on. */
    float next() noexcept
    {
        if (isSettled())
            return current;

        const auto moved = current + coeff * (target - current);

        if (std::abs (target - moved) < landWithin || ! (moved < current || current < moved))
            current = target;
        else
            current = moved;

        return current;
    }

private:
    float coeff = 1.0f, current = 0.0f, target = 0.0f;
    float landWithin;
};

//==============================================================================
/** A trim: a level in dB, smoothed every sample, handed out as a gain.

    The trims were smoothed once per 32-sample sub-block, and linearly in
    gain. Both measured: Output -24 -> +24 dB rose 19.3 dB in its first
    sample, and a block shorter than 32 samples still ticked the smoother
    once, so the same move ran 32 times faster at a host block size of 1.
    Ticked per sample, the move takes the same time at any block size;
    smoothed in dB, a full-range move rises at most 0.05 dB in a sample at
    44.1 kHz rather than front-loading it, so no sample of it steps the
    signal by more than its own slope does at 100 Hz. Settled, the gain is
    dbToGain of the parameter's own value, the figure it always was.

    It has to land, and a one-pole in float32 does not on its own: near
    24 dB one float step is 1.9e-6 dB, and once the pole's increment is
    under half of that it rounds away, leaving the level stalled short of
    the target for ever (8.4e-4 dB at 44.1 kHz, 1.8e-3 at 96 kHz, 3.7e-3 at
    192 kHz). So it lands on the target, exactly, when it is within
    kLandDb or when a sample makes no progress, whichever comes first:
    within 220 ms of any move across the full range. That last step is
    1e-3 dB at 44.1 and 48 kHz, at most 1.8e-3 at 96 kHz and 3.7e-3 at
    192 kHz -- a gain change of at most 4.2e-4, under a seventh of a 100 Hz
    tone's own largest step at 192 kHz (3.3e-3 of its amplitude). Landed,
    next() does no work.
*/
class TrimSmoother
{
public:
    static constexpr float kLandDb = 1.0e-3f;

    void prepare (double sampleRate, double timeMs) noexcept
    {
        const auto tau = std::max (timeMs, 0.01) * 0.001;
        coeff = (float) (1.0 - std::exp (-1.0 / (std::max (sampleRate, 1.0) * tau)));
    }

    void snap (float targetDb) noexcept
    {
        currentDb = targetDb;
        setTarget (targetDb);
        gain = targetGain;
    }

    void setTarget (float targetDb) noexcept
    {
        if (! (targetDb < this->targetDb) && ! (this->targetDb < targetDb))
            return;

        this->targetDb = targetDb;
        targetGain = toGain (targetDb);
    }

    /** At the target exactly: next() returns dbToGain (target) and does no work. */
    bool isSettled() const noexcept { return ! (currentDb < targetDb) && ! (targetDb < currentDb); }

    /** One sample on. */
    float next() noexcept
    {
        if (isSettled())
            return gain;

        const auto moved = currentDb + coeff * (targetDb - currentDb);

        if (std::abs (targetDb - moved) < kLandDb || ! (moved < currentDb || currentDb < moved))
        {
            currentDb = targetDb;
            gain = targetGain;
        }
        else
        {
            currentDb = moved;
            gain = toGain (moved);
        }

        return gain;
    }

private:
    static float toGain (float decibels) noexcept { return std::pow (10.0f, decibels * 0.05f); }

    float coeff = 1.0f, currentDb = 0.0f, targetDb = 0.0f;
    float gain = 1.0f, targetGain = 1.0f;
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

    /** How many samples, at the oversampled rate and summed over channels and
        paths, the oversampled chain has processed since construction. A count
        of the work rather than a clock: the switch tests bound what a single
        callback may do with it, deterministically. Not used by the audio. */
    unsigned long long oversampledSamplesProcessed() const noexcept { return wetSamplesProcessed; }

    static constexpr int kSubBlock = 32;

private:
    /** The oversampled chain at one factor, both channels.

        The signal chain of the original: input transformer, class-A preamp,
        the equaliser, class-A output amp, output transformer. The nonlinear
        stages sit inside the oversampled region because that is where they
        alias; the equaliser is linear but rides along, which also spares the
        1084's 16 kHz shelf the bilinear warping it would suffer at 48 kHz.

        There are two of these. One is live; the other runs only while the
        oversampling changes, at the new factor on the same live input, so
        that when the dip turns the new path is already mid-stream. */
    struct WetPath
    {
        std::array<EqNetwork, 2>        networks;
        std::array<TransformerStage, 2> inputTransformer, outputTransformer;
        std::array<ClassAStage, 2>      preamp, outputAmp;
        std::array<Oversampler, 2>      oversamplers;
        int factor = 1;

        /** Factor and rate, every stage cleared. Allocation-free. */
        void prepare (double hostRate, int newFactor) noexcept;
        void reset() noexcept;
    };

    void updateCoefficients (int activeChannels, int numSamples) noexcept;
    void applyOversampling (int factor);
    void beginWarming (int factor) noexcept;
    void switchOversampling() noexcept;

    WetPath& livePath() noexcept     { return paths[(size_t) live]; }
    WetPath& standbyPath() noexcept  { return paths[(size_t) (1 - live)]; }

    /** One host-rate sample through a path's oversampled chain, before the
        output gain. */
    float runWet (WetPath&, size_t ch, float x, bool eqFading, float eqAmount) noexcept;

    double sampleRate = 44100.0;
    double effectiveRate = 44100.0;
    int    latencySamples = 0;
    unsigned long long wetSamplesProcessed = 0;

    std::array<WetPath, 2> paths;
    int live = 0;

    // While true the standby path runs at pendingFactor alongside the live
    // one, and warmedSamples counts the host-rate samples it has heard.
    bool warming = false;
    int  warmedSamples = 0;

    // The dry path of the Mix control has to be delayed to match, or a partial
    // blend combs and a full bypass fails to null. The ring is one length for
    // every factor, long enough for the longest latency, and read at the live
    // path's latency, so a change of factor moves the read point rather than
    // resizing anything.
    static constexpr int kDryRing = Oversampler::kMaxLatency + 2;
    std::vector<float> dryDelay;
    int dryWrite = 0, dryStride = 0, dryLatency = 0;

    // A change of oversampling waits at the bottom of this dip while the
    // standby path warms up alongside; see process().
    bmo::dsp::Dip oversamplingDip;
    int pendingFactor = 1;
    bool running = false;   // false until the first process() after prepare() or reset()

    Smoother hfFreqSm, midFreqSm, lfFreqSm;     // smoothed in log2(Hz)
    Smoother hfGainSm, midGainSm, lfGainSm;
    // Per sample. Mix lands within 1e-5 of its fraction and Auto Gain within
    // 1e-5 of its linear gain: a last step of 1e-5 of the signal, or of the
    // dry/wet difference, is 100 dB under anything they move.
    LandingSmoother mixSm { 1.0e-5f }, autoGainSm { 1.0e-5f };
    TrimSmoother inputTrim, outputTrim;         // per sample, in dB

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

    // How far the stream is into the current control period; periods run
    // across process() calls. See process().
    int periodPos = 0;
};

} // namespace bmo::eq
