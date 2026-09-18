#include "DspCore.h"
#include <algorithm>
#include <cstring>

namespace bmo::eq
{

namespace
{
    float dbToGain (float db) noexcept { return std::pow (10.0f, db * 0.05f); }

    float toLog2Hz (float hz) noexcept  { return std::log2 (std::max (hz, 1.0f)); }
    float fromLog2Hz (float l) noexcept { return std::exp2 (l); }

    /** Bit-exact float comparison, deliberately. Smoother::tick snaps to its
        target once inside epsilon, so a settled parameter compares identical
        and the coefficient recomputation can be skipped entirely. Written with
        < rather than == to say that this is intended, not an oversight. */
    constexpr bool exactly (float a, float b) noexcept
    {
        return ! (a < b) && ! (b < a);
    }

    //==========================================================================
    // Calibration.
    //
    // The published figure for the unit is not more than 0.07 % from 50 Hz to
    // 10 kHz at +20 dBu out, which is its nominal operating level -- these are
    // clean amplifiers until they are pushed, and the colour is something you
    // drive them into rather than something they do at rest. Taking 0 dBFS as
    // roughly +22 dBu, unity here should be gently coloured and the Input
    // control is what takes it further, exactly as winding up the mic gain and
    // pulling the output fader does on the hardware.
    //
    // The amplifier bias is small on purpose. tanh with a large offset makes
    // enormous second harmonic long before its knee, which sounds like a fuzz
    // box rather than a console: an earlier calibration here was reading 10 %
    // at 1 kHz.
    //==========================================================================

    constexpr float kInputIronDrive  = 0.50f;
    constexpr float kOutputIronDrive = 0.70f;
    constexpr float kPreampDrive     = 0.10f;
    constexpr float kOutputAmpDrive  = 0.09f;
    constexpr float kAmpAsymmetry    = 0.06f;

    bool sameSettings (const EqSettings& a, const EqSettings& b) noexcept
    {
        return a.midHiQ == b.midHiQ
            && exactly (a.hfFreqHz,  b.hfFreqHz)  && exactly (a.hfGainDb,  b.hfGainDb)
            && exactly (a.midFreqHz, b.midFreqHz) && exactly (a.midGainDb, b.midGainDb)
            && exactly (a.lfFreqHz,  b.lfFreqHz)  && exactly (a.lfGainDb,  b.lfGainDb)
            && exactly (a.hpfFreqHz, b.hpfFreqHz) && exactly (a.lpfFreqHz, b.lpfFreqHz);
    }
}

//==============================================================================
void DspCore::prepare (double newSampleRate, int maxBlockSize, int numChannels,
                       int oversampleFactor)
{
    sampleRate  = newSampleRate > 0.0 ? newSampleRate : 44100.0;
    maxBlock    = std::max (maxBlockSize, 1);
    maxChannels = std::clamp (numChannels, 1, (int) networks.size());

    // Sized for the highest factor, so a change of oversampling at run time
    // never has to allocate on the audio thread.
    dryDelay.assign ((size_t) ((Oversampler::kMaxLatency + 2) * (int) networks.size()), 0.0f);
    dryStride = Oversampler::kMaxLatency + 2;

    const auto controlRate = sampleRate / (double) kSubBlock;

    for (auto* s : { &hfFreqSm, &midFreqSm, &lfFreqSm })
        s->prepare (controlRate, 25.0);

    for (auto* s : { &hfGainSm, &midGainSm, &lfGainSm,
                     &inputGainSm, &outputLevelSm, &mixSm, &autoGainSm })
        s->prepare (controlRate, 20.0);

    applyOversampling (oversampleFactor);

    settingsValid = false;
    reset();
}

//==============================================================================
void DspCore::applyOversampling (int factor)
{
    factor = factor >= 8 ? 8 : factor >= 4 ? 4 : factor >= 2 ? 2 : 1;

    for (auto& o : oversamplers)
        o.setFactor (factor);

    currentFactor  = factor;
    latencySamples = Oversampler::latencyForFactor (factor);
    effectiveRate  = sampleRate * (double) factor;
    dryLength      = latencySamples + 1;

    // None of these allocate; they only recompute coefficients, so this is
    // safe to call from the audio thread when the factor changes.
    for (size_t ch = 0; ch < networks.size(); ++ch)
    {
        networks[ch].prepare (effectiveRate);

        inputTransformer [ch].prepare (effectiveRate);
        outputTransformer[ch].prepare (effectiveRate);
        preamp           [ch].prepare (effectiveRate);
        outputAmp        [ch].prepare (effectiveRate);

        // Calibration. The transformers are driven so a full-scale tone at the
        // bottom of the band sits near the knee; because the emphasis tilts
        // 26 dB across 25 Hz to 500 Hz, a tone at 1 kHz then sits roughly a
        // decade lower in distortion, which is what Marinair measured for the
        // line transformer in these units. The class-A stages are gentler but
        // markedly asymmetric, and supply most of the second harmonic.
        inputTransformer [ch].setDrive (kInputIronDrive);
        outputTransformer[ch].setDrive (kOutputIronDrive);   // the output iron works hardest

        preamp   [ch].setDrive (kPreampDrive);
        outputAmp[ch].setDrive (kOutputAmpDrive);
        preamp   [ch].setAsymmetry (kAmpAsymmetry);
        outputAmp[ch].setAsymmetry (kAmpAsymmetry);
    }

    settingsValid = false;
}

void DspCore::reset() noexcept
{
    for (auto& n : networks)           n.reset();
    for (auto& o : oversamplers)       o.reset();
    for (auto& t : inputTransformer)   t.reset();
    for (auto& t : outputTransformer)  t.reset();
    for (auto& a : preamp)             a.reset();
    for (auto& a : outputAmp)          a.reset();

    std::fill (dryDelay.begin(), dryDelay.end(), 0.0f);
    dryWrite = 0;
}

//==============================================================================
void DspCore::setParams (const Params& p) noexcept
{
    params = p;

    const auto hfHz  = highShelfFreqHz (p.hfFreqIndex);
    const auto midHz = midFreqHz (p.midFreqIndex);
    const auto lfHz  = lowShelfFreqHz (p.lfFreqIndex);

    hfFreqSm .setTarget (toLog2Hz (hfHz));
    midFreqSm.setTarget (toLog2Hz (midHz));
    lfFreqSm .setTarget (toLog2Hz (lfHz));

    hfGainSm .setTarget (p.hfGainDb);
    midGainSm.setTarget (p.midGainDb);
    lfGainSm .setTarget (p.lfGainDb);

    inputGainSm  .setTarget (dbToGain (p.inputGainDb));
    outputLevelSm.setTarget (dbToGain (p.outputLevelDb));
    mixSm        .setTarget (std::clamp (p.mixPercent, 0.0f, 100.0f) * 0.01f);

    if (! settingsValid)
    {
        hfFreqSm .snap (toLog2Hz (hfHz));
        midFreqSm.snap (toLog2Hz (midHz));
        lfFreqSm .snap (toLog2Hz (lfHz));
        hfGainSm .snap (p.hfGainDb);
        midGainSm.snap (p.midGainDb);
        lfGainSm .snap (p.lfGainDb);
        inputGainSm  .snap (dbToGain (p.inputGainDb));
        outputLevelSm.snap (dbToGain (p.outputLevelDb));
        mixSm        .snap (std::clamp (p.mixPercent, 0.0f, 100.0f) * 0.01f);
        autoGainSm   .snap (1.0f);
    }
}

//==============================================================================
void DspCore::updateCoefficients (int activeChannels) noexcept
{
    EqSettings s;
    s.hfFreqHz  = fromLog2Hz (hfFreqSm.tick());
    s.midFreqHz = fromLog2Hz (midFreqSm.tick());
    s.lfFreqHz  = fromLog2Hz (lfFreqSm.tick());
    s.hfGainDb  = hfGainSm.tick();
    s.midGainDb = midGainSm.tick();
    s.lfGainDb  = lfGainSm.tick();
    s.midHiQ    = params.midHiQ;
    s.hpfFreqHz = hpfFreqHz (params.hpfIndex);
    s.lpfFreqHz = lpfFreqHz (params.lpfIndex);

    const auto settingsChanged = ! (settingsValid && sameSettings (s, currentSettings));

    if (! settingsChanged && params.autoGain == autoGainApplied)
        return;

    if (settingsChanged)
    {
        for (int ch = 0; ch < activeChannels; ++ch)
            networks[(size_t) ch].setSettings (s);

        currentSettings = s;
        settingsValid   = true;
    }

    // Auto Gain is re-decided on its own change as well as on a band's. Until
    // 0.2.4 its target was only ever set after a settings change, so flipping
    // the switch with nothing else moving did nothing at all, and the next
    // knob move then jumped the level by the whole compensation at once.
    autoGainApplied = params.autoGain;
    autoGainSm.setTarget (params.autoGain ? (float) (1.0 / networks[0].broadbandGain()) : 1.0f);
}

//==============================================================================
void DspCore::process (float* const* channels, int numChannels, int numSamples) noexcept
{
    const auto activeChannels = std::clamp (numChannels, 0, maxChannels);

    if (activeChannels == 0 || numSamples <= 0)
        return;

    if (params.oversampling != currentFactor)
        applyOversampling (params.oversampling);

    const auto polarity = params.phaseInvert ? -1.0f : 1.0f;
    const auto factor   = currentFactor;

    for (int start = 0; start < numSamples; start += kSubBlock)
    {
        const auto n = std::min (kSubBlock, numSamples - start);

        updateCoefficients (activeChannels);

        const auto inGain   = inputGainSm.tick();
        const auto outGain  = outputLevelSm.tick() * autoGainSm.tick();
        const auto wet      = mixSm.tick();
        const auto dryLevel = 1.0f - wet;

        // Samples outermost so the shared dry-delay cursor advances once per
        // frame rather than once per channel.
        for (int i = 0; i < n; ++i)
        {
            const auto readIndex = (dryWrite + 1) % dryLength;

            for (int ch = 0; ch < activeChannels; ++ch)
            {
                auto* data = channels[ch] + start;
                auto* dry  = dryDelay.data() + (size_t) ch * (size_t) dryStride;

                const auto input = data[i];

                dry[(size_t) dryWrite] = input;
                const auto delayed = dry[(size_t) readIndex];

                float buffer[Oversampler::kMaxFactor] {};
                oversamplers[(size_t) ch].upsample (input * inGain, buffer);

                for (int j = 0; j < factor; ++j)
                {
                    auto v = buffer[j];

                    v = inputTransformer[(size_t) ch].process (v);
                    v = preamp[(size_t) ch].process (v);

                    // EQ In takes only the equaliser out of circuit; the gain
                    // stages and their iron stay in, as on the hardware.
                    if (params.eqIn)
                        v = networks[(size_t) ch].processSample (v);

                    v = outputAmp[(size_t) ch].process (v);
                    v = outputTransformer[(size_t) ch].process (v);

                    buffer[j] = v;
                }

                const auto processed = oversamplers[(size_t) ch].downsample (buffer) * outGain;

                // Polarity flips the blend, not the wet path alone. Until 0.2.4
                // it was applied before the iron while the dry ring held the
                // un-flipped input, so at Mix 50 % a flat EQ cancelled itself
                // and at Mix 0 the switch did nothing -- the fault the
                // Saturator fixed in its own DspCore, and the same fix.
                data[i] = (processed * wet + delayed * dryLevel) * polarity;
            }

            dryWrite = (dryWrite + 1) % dryLength;
        }
    }
}

} // namespace bmo::eq
