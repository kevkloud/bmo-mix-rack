#include "DspCore.h"
#include <algorithm>
#include <cstring>

namespace bmo::eq
{

namespace
{
    int supportedFactor (int factor) noexcept
    {
        return factor >= 8 ? 8 : factor >= 4 ? 4 : factor >= 2 ? 2 : 1;
    }

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
            && exactly (a.midQ, b.midQ)
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
    dryDelay.assign ((size_t) (kDryRing * (int) networks.size()), 0.0f);
    dryStride = kDryRing;

    const auto controlRate = sampleRate / (double) kSubBlock;

    for (auto* s : { &hfFreqSm, &midFreqSm, &lfFreqSm })
        s->prepare (controlRate, 25.0);

    for (auto* s : { &hfGainSm, &midGainSm, &lfGainSm, &mixSm, &autoGainSm })
        s->prepare (controlRate, 20.0);

    // The trims keep the 20 ms they always had, now in real time per sample.
    inputTrim .prepare (sampleRate, 20.0);
    outputTrim.prepare (sampleRate, 20.0);

    for (auto* r : { &eqInMix, &polarity, &hiQAmount })
        r->prepare (sampleRate, EqNetwork::kSwitchFadeMs);

    // Taken as found, never faded in from whatever they held before.
    eqInMix  .snap (params.eqIn ? 1.0f : 0.0f);
    polarity .snap (params.phaseInvert ? -1.0f : 1.0f);
    hiQAmount.snap (params.midHiQ ? 1.0f : 0.0f);

    oversamplingDip.prepare (sampleRate, EqNetwork::kSwitchFadeMs);

    applyOversampling (oversampleFactor);
    pendingFactor = currentFactor;

    settingsValid = false;
    reset();
}

//==============================================================================
void DspCore::applyOversampling (int factor)
{
    factor = supportedFactor (factor);

    for (auto& o : oversamplers)
        o.setFactor (factor);

    currentFactor  = factor;
    latencySamples = Oversampler::latencyForFactor (factor);
    dryLatency     = latencySamples;
    effectiveRate  = sampleRate * (double) factor;

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

//==============================================================================
void DspCore::switchOversampling (int activeChannels, float inGain) noexcept
{
    // Called at the bottom of the dip, where the output is silent and every
    // stage can start over at the new rate unheard.
    applyOversampling (pendingFactor);

    // applyOversampling() leaves the settings to be recomputed and the
    // smoothers to be snapped on the next setParams(), which is right after
    // prepare() and wrong here: it threw Auto Gain back to unity and every
    // knob mid-move to its target. The settings in use are re-applied at the
    // new rate instead, and only Auto Gain, whose figure depends on the rate,
    // is re-read -- snapped, since nothing is audible to glide.
    for (auto& n : networks)
        n.setSettings (currentSettings);

    settingsValid = true;

    if (autoGainApplied)
        autoGainSm.snap ((float) (1.0 / networks[0].broadbandGain()));

    eqInMix.snap (params.eqIn ? 1.0f : 0.0f);

    // The new path starts with empty filters, so on its own it would sit
    // silent for its whole latency and then start abruptly, which the fade
    // up would turn into a step. Running it over the input it has missed --
    // the dry ring holds enough for twice the longest latency, which is the
    // whole span of the oversampling filters -- leaves it mid-stream, as if
    // it had always been running, and the fade up starts at once.
    for (int ch = 0; ch < activeChannels; ++ch)
    {
        const auto* dry = dryDelay.data() + (size_t) ch * (size_t) dryStride;

        for (int k = kDryRing - 1; k >= 1; --k)
            runWet ((size_t) ch, dry[(size_t) ((dryWrite + kDryRing - k) % kDryRing)] * inGain,
                    currentFactor, false, 1.0f);
    }
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

    // A dip in progress was hiding a change that no longer has anything to
    // hide; a change still wanted is made at once by the next process().
    oversamplingDip.reset();
    running = false;

    // Auto Gain starts at its figure for the settings on the next block.
    autoGainPrimed = false;
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

    inputTrim .setTarget (p.inputGainDb);
    outputTrim.setTarget (p.outputLevelDb);
    mixSm        .setTarget (std::clamp (p.mixPercent, 0.0f, 100.0f) * 0.01f);

    // EQ In out of circuit stops the network, which then holds whatever the
    // signal left in it, exactly as a cut does. Coming back in from fully out
    // it starts from rest; coming back during its own fade out its state is
    // still live and it keeps it.
    if (settingsValid && p.eqIn && ! eqInMix.isMoving() && eqInMix.value() == 0.0f)
        for (auto& n : networks)
            n.reset();

    eqInMix  .setTarget (p.eqIn ? 1.0f : 0.0f);
    polarity .setTarget (p.phaseInvert ? -1.0f : 1.0f);
    hiQAmount.setTarget (p.midHiQ ? 1.0f : 0.0f);

    if (! settingsValid)
    {
        hfFreqSm .snap (toLog2Hz (hfHz));
        midFreqSm.snap (toLog2Hz (midHz));
        lfFreqSm .snap (toLog2Hz (lfHz));
        hfGainSm .snap (p.hfGainDb);
        midGainSm.snap (p.midGainDb);
        lfGainSm .snap (p.lfGainDb);
        inputTrim    .snap (p.inputGainDb);
        outputTrim   .snap (p.outputLevelDb);
        mixSm        .snap (std::clamp (p.mixPercent, 0.0f, 100.0f) * 0.01f);
        autoGainSm   .snap (1.0f);
        eqInMix      .snap (p.eqIn ? 1.0f : 0.0f);
        polarity     .snap (p.phaseInvert ? -1.0f : 1.0f);
        hiQAmount    .snap (p.midHiQ ? 1.0f : 0.0f);
    }
}

//==============================================================================
void DspCore::updateCoefficients (int activeChannels, int numSamples) noexcept
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

    // Hi-Q glides the branch Q between the two widths, geometrically, rather
    // than stepping it: a hard change of Q under a +18 dB mid stepped the
    // output 1.7-3.4 times the signal's own largest step. Advanced by the
    // sub-block's own length, so the glide takes the same time whatever
    // the host's block size. Once it lands, midQ goes back to zero and the
    // Q is derived exactly as it always was.
    if (hiQAmount.isMoving())
    {
        const auto amount = hiQAmount.advance (numSamples);

        if (hiQAmount.isMoving())
        {
            const auto normal = midBranchQ (s.midFreqHz, false);
            const auto narrow = midBranchQ (s.midFreqHz, true);
            s.midQ = normal * std::pow (narrow / normal, amount);
        }
    }

    const auto settingsChanged = ! (settingsValid && sameSettings (s, currentSettings));

    if (! settingsChanged && params.autoGain == autoGainApplied && autoGainPrimed)
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

    const auto compensation = params.autoGain ? (float) (1.0 / networks[0].broadbandGain()) : 1.0f;
    autoGainSm.setTarget (compensation);

    // The figure comes from the settings, not from the signal, so it is known
    // before the first sample is heard: after prepare() or reset() Auto Gain
    // starts where it belongs. It used to start from unity and glide, +6.6 dB
    // high at 2 ms with the low shelf at +16 and the mid at +12. Only a
    // change heard while running glides.
    if (! autoGainPrimed)
    {
        autoGainSm.snap (compensation);
        autoGainPrimed = true;
    }
}

//==============================================================================
float DspCore::runWet (size_t ch, float x, int factor, bool eqFading, float eqAmount) noexcept
{
    float buffer[Oversampler::kMaxFactor] {};
    oversamplers[ch].upsample (x, buffer);

    for (int j = 0; j < factor; ++j)
    {
        auto v = buffer[j];

        v = inputTransformer[ch].process (v);
        v = preamp[ch].process (v);

        // EQ In takes only the equaliser out of circuit; the gain stages and
        // their iron stay in, as on the hardware. It crosses over rather than
        // stepping: with the mid at +18 the two sides differ by most of full
        // scale.
        if (eqFading)
            v = bmo::dsp::crossfade (v, networks[ch].processSample (v), eqAmount);
        else if (params.eqIn)
            v = networks[ch].processSample (v);

        v = outputAmp[ch].process (v);
        v = outputTransformer[ch].process (v);

        buffer[j] = v;
    }

    return oversamplers[ch].downsample (buffer);
}

//==============================================================================
void DspCore::process (float* const* channels, int numChannels, int numSamples) noexcept
{
    const auto activeChannels = std::clamp (numChannels, 0, maxChannels);

    if (activeChannels == 0 || numSamples <= 0)
        return;

    // A change of oversampling changes the latency, so the audio cannot pass
    // through it continuously: until 0.2.6 every stage was reset on the spot,
    // which cut to 28-65 samples of silence and then jumped. It now dips:
    // the old path fades out, the change is made at the bottom, and the new
    // path, run over the input it missed, fades in. The latency reported is
    // the new factor's from here on, as it always was, so a host that reads
    // it after this block gets the figure the audio will have.
    if (const auto wanted = supportedFactor (params.oversampling); wanted != currentFactor && ! running)
    {
        // Nothing has been heard since prepare() or reset(), so there is
        // nothing to fade: the change is made at once, as it always was.
        applyOversampling (wanted);
        pendingFactor = wanted;
    }
    else if (wanted != currentFactor)
    {
        pendingFactor  = wanted;
        latencySamples = Oversampler::latencyForFactor (wanted);
        oversamplingDip.request();
    }
    else if (oversamplingDip.isPending())
    {
        // Changed back before the dip reached the bottom: nothing to change.
        pendingFactor  = currentFactor;
        latencySamples = Oversampler::latencyForFactor (currentFactor);
        oversamplingDip.cancel();
    }

    auto factor = currentFactor;

    for (int start = 0; start < numSamples; start += kSubBlock)
    {
        const auto n = std::min (kSubBlock, numSamples - start);

        updateCoefficients (activeChannels, n);

        const auto autoGain = autoGainSm.tick();
        const auto wet      = mixSm.tick();
        const auto dryLevel = 1.0f - wet;

        // Samples outermost so the shared dry-delay cursor advances once per
        // frame rather than once per channel.
        for (int i = 0; i < n; ++i)
        {
            const auto inGain  = inputTrim.next();
            const auto outGain = outputTrim.next() * autoGain;

            if (oversamplingDip.ready())
            {
                switchOversampling (activeChannels, inGain);
                oversamplingDip.changed();
                factor = currentFactor;
            }

            // The dry ring is long enough for any factor, and read at the
            // latency of the path that is running.
            const auto readIndex = (dryWrite + kDryRing - dryLatency) % kDryRing;

            // Once per frame, shared by both channels. Idle, these are exactly
            // the switches' positions and the paths below are the ones the
            // module always had; the dip at rest is exactly 1, so it leaves
            // the sign unchanged.
            const auto eqFading = eqInMix.isMoving();
            const auto eqAmount = eqInMix.next();
            const auto sign     = polarity.next() * oversamplingDip.next();

            for (int ch = 0; ch < activeChannels; ++ch)
            {
                auto* data = channels[ch] + start;
                auto* dry  = dryDelay.data() + (size_t) ch * (size_t) dryStride;

                const auto input = data[i];

                dry[(size_t) dryWrite] = input;
                const auto delayed = dry[(size_t) readIndex];

                const auto processed = runWet ((size_t) ch, input * inGain, factor, eqFading, eqAmount)
                                     * outGain;

                // Polarity flips the blend, not the wet path alone. Until 0.2.4
                // it was applied before the iron while the dry ring held the
                // un-flipped input, so at Mix 50 % a flat EQ cancelled itself
                // and at Mix 0 the switch did nothing -- the fault the
                // Saturator fixed in its own DspCore, and the same fix. The
                // flip itself ramps through zero rather than stepping by twice
                // the signal.
                data[i] = (processed * wet + delayed * dryLevel) * sign;
            }

            dryWrite = (dryWrite + 1) % kDryRing;
        }
    }

    running = true;
}

} // namespace bmo::eq
