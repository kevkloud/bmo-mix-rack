#include "DspCore.h"
#include "DriveTables.h"
#include <algorithm>

namespace bmo::sat
{

namespace
{
    float dbToGain (float db) noexcept { return std::pow (10.0f, db * 0.05f); }

    int supportedFactor (int factor) noexcept
    {
        return factor >= 8 ? 8 : factor >= 4 ? 4 : factor >= 2 ? 2 : 1;
    }
}

//==============================================================================
float DspCore::driveFor (float amountPercent) noexcept
{
    const auto t = std::clamp (amountPercent, 0.0f, 100.0f) * 0.01f;

    return tables::kDriveMin * std::pow (tables::kDriveMax / tables::kDriveMin, t);
}

float DspCore::makeupGainDb (float amountPercent) noexcept
{
    return tables::makeupDb (amountPercent);
}

//==============================================================================
void DspCore::Channel::prepare (double rate, const Character& c) noexcept
{
    character = &c;

    for (auto& pole : bodyInput)
        pole.setCutoff (c.bodySourceHz, rate);

    bodySplit.setCutoff (c.bodySourceHz, rate);

    for (auto& pole : sheenInput)
        pole.setCutoff (c.sheenSourceHz, rate);

    sheenSplit.setCutoff (c.residualSplitHz, rate);
    sheenTilt.set (c.sheenHz, c.sheenTilt, rate);

    highPass.set (c.highPassHz, rate);
    setTone (100.0f, rate);

    dc.prepare (rate);
    reset();
}

void DspCore::Channel::reset() noexcept
{
    resetStage();
    oversampler.reset();
}

void DspCore::Channel::resetStage() noexcept
{
    shaper.reset();
    bodyShaper.reset();
    sheenShaper.reset();
    bell.reset();
    highPass.reset();

    for (auto& pole : bodyInput)
        pole.reset();

    for (auto& pole : sheenInput)
        pole.reset();

    bodySplit.reset();
    sheenSplit.reset();
    sheenTilt.reset();
    dc.reset();
}

void DspCore::Channel::setTone (float amountPercent, double rate) noexcept
{
    // The amount scales the fitted shape in decibels, so half of TONE is half
    // the voicing rather than half the gain -- the shape stays the shape.
    const auto t = std::clamp (amountPercent, 0.0f, 100.0f) * 0.01f;
    const auto gain = std::pow (10.0f, character->bellGainDb * t * 0.05f);

    bell.set (character->bellHz, character->bellQ, gain, rate);

    // The high-pass is blended rather than switched, so that TONE at zero is
    // the signal untouched rather than the signal with a filter still on it.
    // Anything the voicing does has to leave when the voicing leaves.
    toneAmount = t;
}

void DspCore::Channel::setDrive (float drive) noexcept
{
    shaper.setDrive (drive);
    bodyShaper.setDrive (drive);
    sheenShaper.setDrive (drive);
}

float DspCore::Channel::process (float x) noexcept
{
    // The fitted curve, exactly: x + (shape(x) - x), with the residual
    // anti-aliased. Everything the calibration in Shaper.h says about the
    // positive and negative average gains is a statement about this line.
    auto y = x + shaper.processResidual (x);

    // One generator per band that needs filling. Each is fed the signal below
    // its corner, and only what it makes above that corner is kept.
    y += character->bodyGain  * generate (bodyShaper,  bodyInput,  bodySplit,  x);
    y += character->sheenGain * sheenTilt.process (generate (sheenShaper, sheenInput, sheenSplit, x));

    // The voicing goes after the saturation: the reference's bell sits on the
    // finished sound, and putting it before would drive the curve with a shape
    // the reference never fed it.
    const auto filtered = y + toneAmount * (highPass.process (y) - y);
    return dc.process (bell.process (filtered));
}

float DspCore::Channel::runWet (float driven, int factor, bool saturate,
                                bool fading, float amount) noexcept
{
    float buffer[Oversampler::kMaxFactor] {};
    oversampler.upsample (driven, buffer);

    // Fading, the stage and the wire are blended; settled, the path is the
    // one the module always had, untouched by the blend.
    if (fading)
        for (int j = 0; j < factor; ++j)
            buffer[j] = bmo::dsp::crossfade (buffer[j], process (buffer[j]), amount);
    else
        for (int j = 0; j < factor; ++j)
            buffer[j] = saturate ? process (buffer[j]) : buffer[j];

    return oversampler.downsample (buffer);
}

float DspCore::Channel::generate (AsymmetricShaper& generator, OnePole (&input)[2],
                                  OnePole& split, float x) noexcept
{
    auto source = x;

    for (auto& pole : input)
        source = pole.process (OnePole::Output::lowpass, source);

    float below = 0.0f, above = 0.0f;
    split.process (generator.processResidual (source), below, above);
    return above;
}

//==============================================================================
void DspCore::prepare (double newSampleRate, int maxBlockSize, int numChannels,
                       int oversampleFactor)
{
    sampleRate  = newSampleRate > 0.0 ? newSampleRate : 44100.0;
    maxBlock    = std::max (maxBlockSize, 1);
    maxChannels = std::clamp (numChannels, 1, (int) livePath().size());

    // Sized for the highest factor, so a change of oversampling at run time
    // never has to allocate on the audio thread.
    dryDelay.assign ((size_t) (kDryRing * (int) livePath().size()), 0.0f);
    dryStride = kDryRing;

    const auto controlRate = sampleRate / (double) kSubBlock;

    for (auto* s : { &inputGainSm, &outputLevelSm, &mixSm, &makeupSm, &toneSm })
        s->prepare (controlRate, 20.0);

    // Drive is smoothed more slowly than a gain. It moves the shape of the
    // curve rather than a level, and a curve that changes shape quickly under
    // an automation ramp is audible as a flutter on sustained material.
    driveSm.prepare (controlRate, 40.0);

    // Auto Gain's detector. 1.5 seconds: slower than any phrase, so it cannot
    // act on the programme's dynamics.
    autoGainCoeff = (float) (1.0 - std::exp (-1.0 / (controlRate * 1.5)));

    // Its reading is kept. It is a ratio of what the stage puts out to what
    // it is fed, a property of the settings and the material rather than of
    // the stream, so it is as good after prepare() as before; throwing it
    // away restarted the makeup at unity, +6.8 dB high for 10 ms with Tone at
    // 100. An instance that has heard nothing has no reading, and starts at
    // unity as it always has.

    oversamplingDip.prepare (sampleRate, kSwitchFadeMs);

    // Taken as found, never faded in from whatever they held before.
    for (auto* r : { &satMix, &polaritySwitch })
        r->prepare (sampleRate, kSwitchFadeMs);

    satMix        .snap (params.saturationIn ? 1.0f : 0.0f);
    polaritySwitch.snap (params.phaseInvert ? -1.0f : 1.0f);

    applyOversampling (oversampleFactor);
    pendingFactor = currentFactor;

    primed = false;
    reset();
}

void DspCore::preparePath (std::array<Channel, 2>& path, int factor) noexcept
{
    // None of this allocates; it only recomputes coefficients and clears
    // state, so it is safe on the audio thread when the factor changes.
    for (auto& c : path)
    {
        c.oversampler.setFactor (factor);
        c.prepare (sampleRate * (double) factor, character);
    }
}

void DspCore::applyOversampling (int factor)
{
    factor = supportedFactor (factor);

    pathFactor[(size_t) live] = factor;
    preparePath (livePath(), factor);

    currentFactor  = factor;
    latencySamples = Oversampler::latencyForFactor (factor);
    dryLatency     = latencySamples;
    effectiveRate  = sampleRate * (double) factor;
    warming        = false;
}

void DspCore::beginWarming (int factor) noexcept
{
    // The standby path is cleared and set up at the new factor with the
    // settings in use, and from the next sample it hears the live input
    // alongside the live path. Nothing it does reaches the output until the
    // dip turns.
    pathFactor[(size_t) (1 - live)] = factor;
    preparePath (standbyPath(), factor);

    for (auto& c : standbyPath())
    {
        c.setDrive (held.drive);
        c.setTone (held.tone, sampleRate * (double) factor);
    }

    warming       = true;
    warmedSamples = 0;
}

void DspCore::switchOversampling() noexcept
{
    // Called at the bottom of the dip, where the output is silent: the path
    // that has been running alongside at the new factor becomes the live one,
    // already mid-stream, and the old one stops. The dry path's read point
    // moves to the new latency.
    live           = 1 - live;
    warming        = false;
    currentFactor  = pathFactor[(size_t) live];
    dryLatency     = Oversampler::latencyForFactor (currentFactor);
    effectiveRate  = sampleRate * (double) currentFactor;
}

void DspCore::reset() noexcept
{
    for (auto& c : livePath())
        c.reset();

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

    // The next sample starts a control period, as the first one after
    // prepare() does; what the detector had heard of the unfinished one is
    // dropped with the rest of the stream.
    periodPos = periodSamples = 0;
    periodInput = periodProcessed = 0.0;

    // Likewise a switch's fade: the stage starts over from rest either way,
    // so the switch is simply where it was going.
    satMix        .snap (satMix.target());
    polaritySwitch.snap (polaritySwitch.target());
}

//==============================================================================
void DspCore::setParams (const Params& p) noexcept
{
    params = p;

    const auto drive = driveFor (p.driveAmount);

    inputGainSm  .setTarget (dbToGain (p.inputGainDb));
    outputLevelSm.setTarget (dbToGain (p.outputLevelDb));
    mixSm        .setTarget (std::clamp (p.mixPercent, 0.0f, 100.0f) * 0.01f);
    driveSm      .setTarget (drive);
    toneSm       .setTarget (std::clamp (p.toneAmount, 0.0f, 100.0f));

    if (! p.autoGain)
        makeupSm.setTarget (1.0f);

    // Sat In out of circuit stops the stage, which then holds whatever the
    // signal left in its filters; brought back in after the signal had
    // stopped, it released that at up to +8.8 dBFS. Coming back in from fully
    // out it now starts from rest; coming back during its own fade out its
    // state is still live and it keeps it.
    if (primed && p.saturationIn && ! satMix.isMoving() && satMix.value() == 0.0f)
        for (auto& path : paths)
            for (auto& c : path)
                c.resetStage();

    satMix        .setTarget (p.saturationIn ? 1.0f : 0.0f);
    polaritySwitch.setTarget (p.phaseInvert ? -1.0f : 1.0f);

    if (primed)
        return;

    satMix        .snap (p.saturationIn ? 1.0f : 0.0f);
    polaritySwitch.snap (p.phaseInvert ? -1.0f : 1.0f);

    toneSm.snap (std::clamp (p.toneAmount, 0.0f, 100.0f));

    // Auto Gain starts where its detector last read, which survives prepare()
    // and reset(); unity only if it has never heard anything.
    makeupSm.snap (p.autoGain ? currentAutoGain() : 1.0f);

    inputGainSm  .snap (dbToGain (p.inputGainDb));
    outputLevelSm.snap (dbToGain (p.outputLevelDb));
    mixSm        .snap (std::clamp (p.mixPercent, 0.0f, 100.0f) * 0.01f);
    driveSm      .snap (drive);
    primed = true;
}

//==============================================================================
void DspCore::process (float* const* channelData, int numChannels, int numSamples) noexcept
{
    const auto activeChannels = std::clamp (numChannels, 0, maxChannels);

    if (activeChannels == 0 || numSamples <= 0)
        return;

    // A change of oversampling changes the latency, so the audio cannot pass
    // through it continuously: until 0.2.6 every stage was reset on the spot,
    // which cut to 40-70 samples of exact zero and then jumped, at any Mix.
    // It now dips. On the request the standby path is set up at the new
    // factor and runs on the live input alongside the live path while the
    // old one fades out; at the bottom the standby becomes live, already
    // mid-stream, and fades in; the old path stops. So no callback does more
    // than both factors' worth of work -- warming the new path in one go at
    // the bottom, as the first version of this did, cost 1.4 blocks at
    // 192 kHz / 32. The latency reported is the new factor's from the request
    // on, as it always was, so a host that reads it after this block gets the
    // figure the audio will have.
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

    const auto polarity = params.phaseInvert ? -1.0f : 1.0f;
    auto factor = currentFactor;

    // The smoothers and Auto Gain's detector advance once per kSubBlock-sample
    // control period, and the periods run on the stream rather than on the
    // host's blocks: one that a block ends inside carries on into the next.
    // Until 0.2.6 every call started a period of its own, so a host sending
    // blocks shorter than kSubBlock ran every time constant faster in
    // proportion -- at block 1, Auto Gain's 1.5 s detector acted on 200 ms
    // sections. Blocks that are whole multiples of kSubBlock never ended a
    // period early, so for them nothing has changed.
    for (int start = 0; start < numSamples;)
    {
        if (periodPos == 0)
        {
            held.inGain  = inputGainSm.tick();
            held.outGain = outputLevelSm.tick();
            held.makeup  = makeupSm.tick();
            held.wet     = mixSm.tick();
            held.drive   = driveSm.tick();
            held.tone    = toneSm.tick();

            for (int ch = 0; ch < activeChannels; ++ch)
            {
                livePath()[(size_t) ch].setDrive (held.drive);
                livePath()[(size_t) ch].setTone (held.tone, effectiveRate);

                // A path warming for an oversampling change follows the
                // knobs too, so it turns live with the settings in use.
                if (warming)
                {
                    standbyPath()[(size_t) ch].setDrive (held.drive);
                    standbyPath()[(size_t) ch].setTone (held.tone, sampleRate * (double) pathFactor[(size_t) (1 - live)]);
                }
            }

            periodInput = periodProcessed = 0.0;
            periodSamples = 0;
        }

        const auto n = std::min (kSubBlock - periodPos, numSamples - start);

        const auto inGain   = held.inGain;
        const auto outGain  = held.outGain;
        const auto makeup   = held.makeup;
        const auto wet      = held.wet;
        const auto dryLevel = 1.0f - wet;

        double blockInput = periodInput, blockProcessed = periodProcessed;

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
                factor = currentFactor;
            }

            // The dry ring is long enough for any factor, and read at the
            // latency of the path that is running.
            const auto readIndex = (dryWrite + kDryRing - dryLatency) % kDryRing;

            // Once per frame, shared by both channels. Idle, the dip is not
            // applied at all, so the output is exactly what it always was.
            const auto dipping = ! oversamplingDip.isIdle();
            const auto dip     = dipping ? oversamplingDip.next() : 1.0f;

            // Sat In and Phase the same way: while one fades, its ramp gives the
            // position; settled, the switch is used exactly as it always was.
            const auto satFading = satMix.isMoving();
            const auto satAmount = satFading ? satMix.next() : 1.0f;
            const auto sign      = polaritySwitch.isMoving() ? polaritySwitch.next() : polarity;

            for (int ch = 0; ch < activeChannels; ++ch)
            {
                auto& channel = livePath()[(size_t) ch];
                auto* data = channelData[ch] + start;
                auto* dry  = dryDelay.data() + (size_t) ch * (size_t) dryStride;

                const auto input = data[i];

                dry[(size_t) dryWrite] = input;
                const auto delayed = dry[(size_t) readIndex];

                const auto driven = input * inGain;
                wetSamplesProcessed += (unsigned long long) factor;
                const auto shaped = channel.runWet (driven, factor, params.saturationIn, satFading, satAmount);

                // Heard and discarded: the new path keeping up with the input.
                if (warming)
                {
                    const auto standbyFactor = pathFactor[(size_t) (1 - live)];
                    wetSamplesProcessed += (unsigned long long) standbyFactor;
                    standbyPath()[(size_t) ch].runWet (driven, standbyFactor, params.saturationIn, satFading, satAmount);
                }

                // Measured before the makeup is applied, so the detector reads
                // what the stage did rather than what it and its own
                // compensation did together -- a loop that would take a while
                // to settle and could be made to oscillate.
                blockInput     += (double) driven * driven;
                blockProcessed += (double) shaped * shaped;

                // Polarity is the last thing that happens to the signal, and
                // Output the last thing after that. Both apply to the blend
                // rather than to the wet path alone, which is what makes the
                // button mean "flip what leaves the plugin" in every state.
                //
                // It used to be applied to the input instead, and that was
                // wrong twice over. The dry path of the Mix control never saw
                // it, so at Mix 0 the button did nothing at all. And because
                // the curve is asymmetric, -f(-x) is not f(x): flipping ahead
                // of the shaper changed which harmonics came out, so engaging
                // a polarity switch altered the sound. A polarity control has
                // one job and it is not that. The flip itself ramps through zero
                // rather than stepping by twice the signal.
                const auto blended = shaped * makeup * wet + delayed * dryLevel;

                const auto out = blended * sign * outGain;
                data[i] = dipping ? out * dip : out;
            }

            dryWrite = (dryWrite + 1) % kDryRing;

            if (warming)
                ++warmedSamples;
        }

        periodInput     = blockInput;
        periodProcessed = blockProcessed;
        periodSamples  += n * activeChannels;
        periodPos      += n;
        start          += n;

        if (periodPos == kSubBlock)
        {
            updateAutoGain (periodInput, periodProcessed, periodSamples);
            periodPos = 0;
        }
    }

    running = true;
}

//==============================================================================
void DspCore::updateAutoGain (double blockInput, double blockProcessed, int samples) noexcept
{
    if (samples <= 0)
        return;

    const auto in  = blockInput  / (double) samples;
    const auto out = blockProcessed / (double) samples;

    // Silence carries no information about the gain and would drag both
    // averages towards zero, so the detector holds its reading through it.
    constexpr double kFloor = 1.0e-9;          // about -90 dBFS

    if (in > kFloor && out > kFloor)
    {
        inputEnergy     += (double) autoGainCoeff * (in  - inputEnergy);
        processedEnergy += (double) autoGainCoeff * (out - processedEnergy);
    }

    if (! params.autoGain || inputEnergy <= kFloor || processedEnergy <= kFloor)
        return;

    // Clamped, because a level match is a convenience and should never be
    // capable of a surprise: at most 12 dB either way.
    const auto wanted = std::sqrt (inputEnergy / processedEnergy);
    makeupSm.setTarget ((float) std::clamp (wanted, 0.25, 4.0));
}

float DspCore::currentAutoGain() const noexcept
{
    // The same figure updateAutoGain() aims the makeup at, or unity when the
    // detector has nothing to say.
    constexpr double kFloor = 1.0e-9;

    if (inputEnergy <= kFloor || processedEnergy <= kFloor)
        return 1.0f;

    return (float) std::clamp (std::sqrt (inputEnergy / processedEnergy), 0.25, 4.0);
}

} // namespace bmo::sat
