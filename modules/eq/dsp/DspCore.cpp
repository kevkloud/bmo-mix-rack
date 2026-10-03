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
    maxChannels = std::clamp (numChannels, 1, (int) livePath().networks.size());

    // Sized for the highest factor, so a change of oversampling at run time
    // never has to allocate on the audio thread.
    dryDelay.assign ((size_t) (kDryRing * (int) livePath().networks.size()), 0.0f);
    dryStride = kDryRing;

    const auto controlRate = sampleRate / (double) kSubBlock;

    for (auto* s : { &hfFreqSm, &midFreqSm, &lfFreqSm })
        s->prepare (controlRate, 25.0);

    for (auto* s : { &hfGainSm, &midGainSm, &lfGainSm })
        s->prepare (controlRate, 20.0);

    // Mix and Auto Gain keep the 20 ms they always had, now in real time per
    // sample like the trims.
    mixSm     .prepare (sampleRate, 20.0);
    autoGainSm.prepare (sampleRate, 20.0);

    // The trims keep the 20 ms they always had, now in real time per sample.
    inputTrim .prepare (sampleRate, 20.0);
    outputTrim.prepare (sampleRate, 20.0);

    for (auto* r : { &eqInMix, &polarity, &hiQAmount, &midFreqMix })
        r->prepare (sampleRate, EqNetwork::kSwitchFadeMs);

    // Taken as found, never faded in from whatever they held before.
    eqInMix   .snap (params.eqIn ? 1.0f : 0.0f);
    polarity  .snap (params.phaseInvert ? -1.0f : 1.0f);
    hiQAmount .snap (params.midHiQ ? 1.0f : 0.0f);
    midFreqMix.snap (1.0f);

    oversamplingDip.prepare (sampleRate, EqNetwork::kSwitchFadeMs);

    applyOversampling (oversampleFactor);
    pendingFactor = currentFactor;

    settingsValid = false;
    reset();
}

//==============================================================================
void DspCore::WetPath::prepare (double hostRate, int newFactor) noexcept
{
    factor = supportedFactor (newFactor);

    for (auto& o : oversamplers)
        o.setFactor (factor);

    const auto rate = hostRate * (double) factor;

    // None of these allocate; they only recompute coefficients, so this is
    // safe to call from the audio thread when the factor changes.
    for (size_t ch = 0; ch < networks.size(); ++ch)
    {
        networks[ch].prepare (rate);

        inputTransformer [ch].prepare (rate);
        outputTransformer[ch].prepare (rate);
        preamp           [ch].prepare (rate);
        outputAmp        [ch].prepare (rate);

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
}

void DspCore::WetPath::reset() noexcept
{
    for (auto& n : networks)           n.reset();
    for (auto& o : oversamplers)       o.reset();
    for (auto& t : inputTransformer)   t.reset();
    for (auto& t : outputTransformer)  t.reset();
    for (auto& a : preamp)             a.reset();
    for (auto& a : outputAmp)          a.reset();
}

//==============================================================================
void DspCore::applyOversampling (int factor)
{
    livePath().prepare (sampleRate, factor);

    currentFactor  = livePath().factor;
    latencySamples = Oversampler::latencyForFactor (currentFactor);
    dryLatency     = latencySamples;
    effectiveRate  = sampleRate * (double) currentFactor;

    warming = false;
    settingsValid = false;
}

//==============================================================================
void DspCore::beginWarming (int factor) noexcept
{
    // The standby path is cleared and set up at the new factor, with the
    // settings in use, and from the next sample it hears the live input
    // alongside the live path. Nothing it does reaches the output until the
    // dip turns.
    auto& standby = standbyPath();
    standby.prepare (sampleRate, factor);

    if (settingsValid)
        for (auto& n : standby.networks)
            n.setSettings (currentSettings);

    // A mid crossover in progress belongs to the live path. The standby path
    // starts at the new frequency with a copy that is the same, so whatever
    // the shared ramp says, its blend is the new network.
    standby.previous = standby.networks;

    warming       = true;
    warmedSamples = 0;
}

void DspCore::switchOversampling() noexcept
{
    // Called at the bottom of the dip, where the output is silent: the path
    // that has been running alongside at the new factor becomes the live one,
    // already mid-stream, and the old one stops. Settings, smoothers and the
    // knobs mid-move all carry on; only Auto Gain, whose figure depends on
    // the rate, is re-read -- snapped, since nothing is audible to glide.
    live          = 1 - live;
    warming       = false;
    currentFactor = livePath().factor;
    dryLatency    = Oversampler::latencyForFactor (currentFactor);
    effectiveRate = sampleRate * (double) currentFactor;

    if (autoGainApplied)
        autoGainSm.snap ((float) (1.0 / livePath().networks[0].broadbandGain()));

    eqInMix.snap (params.eqIn ? 1.0f : 0.0f);
}

void DspCore::reset() noexcept
{
    livePath().reset();

    std::fill (dryDelay.begin(), dryDelay.end(), 0.0f);
    dryWrite = 0;

    // A dip in progress was hiding a change that no longer has anything to
    // hide; a change still wanted is made at once by the next process(). The
    // latency reported goes back to the factor that is running: a request
    // dropped here and then withdrawn would otherwise leave the abandoned
    // factor's figure behind, with nothing left to correct it. A standby path
    // warming for that change stops.
    oversamplingDip.reset();
    warming        = false;
    pendingFactor  = currentFactor;
    latencySamples = Oversampler::latencyForFactor (currentFactor);
    running = false;

    // Auto Gain starts at its figure for the settings on the next block.
    autoGainPrimed = false;

    // A mid crossover in progress is history; the network is at its new
    // frequency already.
    midFreqMix.snap (1.0f);

    // The next sample starts a control period, as the first after prepare()
    // does.
    periodPos = 0;
}

//==============================================================================
void DspCore::setParams (const Params& p) noexcept
{
    params = p;

    const auto hfHz  = highShelfFreqHz (p.hfFreqIndex);
    const auto midHz = midFreqHz (p.midFreqIndex);
    const auto lfHz  = lowShelfFreqHz (p.lfFreqIndex);

    // The mid's frequency does not glide; it crosses over, and the change is
    // made in updateCoefficients(). See there.
    hfFreqSm .setTarget (toLog2Hz (hfHz));
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
        for (auto& path : paths)
        {
            for (auto& n : path.networks)  n.reset();
            for (auto& n : path.previous)  n.reset();
        }

    eqInMix  .setTarget (p.eqIn ? 1.0f : 0.0f);
    polarity .setTarget (p.phaseInvert ? -1.0f : 1.0f);
    hiQAmount.setTarget (p.midHiQ ? 1.0f : 0.0f);

    if (! settingsValid)
    {
        hfFreqSm .snap (toLog2Hz (hfHz));
        midFreqSm.snap (toLog2Hz (midHz));
        lfFreqSm .snap (toLog2Hz (lfHz));
        midIndexApplied = p.midFreqIndex;
        midFreqMix   .snap (1.0f);
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
    // The mid's frequency selector crosses over between the network as it was
    // and the network at the new frequency, in EqNetwork::kSwitchFadeMs. It
    // used to glide the centre in log2 Hz, which swept the band across every
    // frequency in between: at +18 dB, 360 Hz -> 7.2 kHz carried the peak
    // through a 1.6 kHz tone and stepped the output 4.63x its own largest step
    // (6.6x with Hi-Q). A crossover never passes through a response that is
    // neither end. The network as it was is a copy, frozen at the settings it
    // had; the live one jumps to the new frequency and carries on following
    // the knobs. A further change while one crosses waits for it to finish,
    // at most 10 ms, since a copy of a half-crossed pair is not either end.
    // HF and LF still glide: at full boost or cut, every pair of their
    // choices, both ways, measured at most 1.5x without a crossover.
    if (params.midFreqIndex != midIndexApplied && ! midFreqMix.isMoving())
    {
        for (auto& path : paths)
            path.previous = path.networks;

        midIndexApplied = params.midFreqIndex;
        midFreqSm.snap (toLog2Hz (midFreqHz (midIndexApplied)));
        midFreqMix.snap (0.0f);
        midFreqMix.setTarget (1.0f);
    }

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
        // A path warming for an oversampling change follows the knobs too,
        // so it turns live with the settings the live one had.
        for (int ch = 0; ch < activeChannels; ++ch)
        {
            livePath().networks[(size_t) ch].setSettings (s);

            if (warming)
                standbyPath().networks[(size_t) ch].setSettings (s);
        }

        currentSettings = s;
        settingsValid   = true;
    }

    // Auto Gain is re-decided on its own change as well as on a band's. Until
    // 0.2.4 its target was only ever set after a settings change, so flipping
    // the switch with nothing else moving did nothing at all, and the next
    // knob move then jumped the level by the whole compensation at once.
    autoGainApplied = params.autoGain;

    const auto compensation = params.autoGain ? (float) (1.0 / livePath().networks[0].broadbandGain()) : 1.0f;
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
float DspCore::runWet (WetPath& path, size_t ch, float x, const Fades& fades) noexcept
{
    const auto eqFading = fades.eqFading;
    const auto eqAmount = fades.eqAmount;

    const auto factor = path.factor;
    wetSamplesProcessed += (unsigned long long) factor;

    auto& networks          = path.networks;
    auto& inputTransformer  = path.inputTransformer;
    auto& outputTransformer = path.outputTransformer;
    auto& preamp            = path.preamp;
    auto& outputAmp         = path.outputAmp;
    auto& oversamplers      = path.oversamplers;

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
        // The network, crossed over from its copy while the mid's frequency
        // changes; idle, exactly the network.
        const auto network = [&] (float in) noexcept
        {
            const auto out = networks[ch].processSample (in);

            return fades.midFading
                     ? bmo::dsp::crossfade (path.previous[ch].processSample (in), out, fades.midAmount)
                     : out;
        };

        if (eqFading)
            v = bmo::dsp::crossfade (v, network (v), eqAmount);
        else if (params.eqIn)
            v = network (v);

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
    // which cut to 28-65 samples of silence and then jumped. It now dips. On
    // the request the standby path is set up at the new factor and runs on
    // the live input alongside the live path while the old one fades out;
    // at the bottom the standby becomes live, already mid-stream, and fades
    // in; the old path stops. So no callback does more than both factors'
    // worth of work -- warming the new path in one go at the bottom, as the
    // first version of this did, cost 2.4 blocks at 192 kHz / 32. The
    // latency reported is the new factor's from the request on, as it always
    // was, so a host that reads it after this block gets the figure the
    // audio will have.
    if (const auto wanted = supportedFactor (params.oversampling); wanted != currentFactor && ! running)
    {
        // Nothing has been heard since prepare() or reset(), so there is
        // nothing to fade: the change is made at once, as it always was.
        applyOversampling (wanted);
        pendingFactor = wanted;
    }
    else if (wanted != currentFactor)
    {
        if (! warming || wanted != pendingFactor)
            beginWarming (wanted);

        pendingFactor  = wanted;
        latencySamples = Oversampler::latencyForFactor (wanted);
        oversamplingDip.request();
    }
    else if (oversamplingDip.isPending())
    {
        // Changed back before the dip reached the bottom: nothing to change.
        pendingFactor  = currentFactor;
        latencySamples = Oversampler::latencyForFactor (currentFactor);
        warming = false;
        oversamplingDip.cancel();
    }

    // The band smoothers and Auto Gain's target advance once per kSubBlock-
    // sample control period, and the periods run on the stream rather than on
    // the host's blocks: one that a block ends inside carries on into the
    // next call. Until 0.2.6 every call started a period of its own, so a
    // host sending blocks shorter than kSubBlock ran every glide faster in
    // proportion -- 32 times at a block of 1. Blocks that are whole multiples
    // of kSubBlock never ended a period early, so for them nothing changed.
    // A knob delivered by a block that starts inside a period is read at the
    // next period's start, at most kSubBlock - 1 samples later; the gains
    // applied to the audio and the switches' fades act per sample and are
    // not delayed.
    for (int start = 0; start < numSamples;)
    {
        if (periodPos == 0)
            updateCoefficients (activeChannels, kSubBlock);

        const auto n = std::min (kSubBlock - periodPos, numSamples - start);

        // Samples outermost so the shared dry-delay cursor advances once per
        // frame rather than once per channel.
        for (int i = 0; i < n; ++i)
        {
            // The turn waits until the new path has heard the whole span of
            // its oversampling filters. A 10 ms fade down is 441 samples or
            // more at any rate this runs at, against 141 for 8x, so this only
            // holds the gain at zero when the target changed late in a fade.
            if (oversamplingDip.ready()
                && warmedSamples >= 2 * Oversampler::latencyForFactor (pendingFactor) + 1)
            {
                switchOversampling();
                oversamplingDip.changed();
            }

            // The gains applied to the audio move every sample: the trims,
            // Auto Gain and Mix. Mix and Auto Gain used to step once per
            // sub-block, Mix's first step of a 0 -> 100 move 3.56 % of the
            // way at 44.1 kHz. Each lands on its target and is then free.
            // Read after the turn above, so Auto Gain's new-rate figure,
            // snapped there, applies from this sample.
            const auto inGain   = inputTrim.next();
            const auto outGain  = outputTrim.next() * autoGainSm.next();
            const auto wet      = mixSm.next();
            const auto dryLevel = 1.0f - wet;

            // The dry ring is long enough for any factor, and read at the
            // latency of the path that is running.
            const auto readIndex = (dryWrite + kDryRing - dryLatency) % kDryRing;

            // Once per frame, shared by both channels. Idle, these are exactly
            // the switches' positions and the paths below are the ones the
            // module always had; the dip at rest is exactly 1, so it leaves
            // the sign unchanged.
            Fades fades;
            fades.eqFading  = eqInMix.isMoving();
            fades.eqAmount  = eqInMix.next();
            fades.midFading = midFreqMix.isMoving();
            fades.midAmount = midFreqMix.next();
            const auto sign = polarity.next() * oversamplingDip.next();

            for (int ch = 0; ch < activeChannels; ++ch)
            {
                auto* data = channels[ch] + start;
                auto* dry  = dryDelay.data() + (size_t) ch * (size_t) dryStride;

                const auto input = data[i];

                dry[(size_t) dryWrite] = input;
                const auto delayed = dry[(size_t) readIndex];

                const auto processed = runWet (livePath(), (size_t) ch, input * inGain, fades)
                                     * outGain;

                // Heard and discarded: the new path keeping up with the input.
                if (warming)
                    runWet (standbyPath(), (size_t) ch, input * inGain, fades);

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

            if (warming)
                ++warmedSamples;
        }

        periodPos = (periodPos + n) % kSubBlock;
        start    += n;
    }

    running = true;
}

} // namespace bmo::eq
