#pragma once

#include "core/dsp/Oversampler.h"
#include "modules/fetcomp/dsp/Calibration.h"
#include "modules/fetcomp/dsp/Detector.h"
#include "modules/fetcomp/dsp/FetCell.h"
#include "modules/fetcomp/dsp/Stages.h"
#include "modules/fetcomp/params.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace bmo::fetcomp
{

//==============================================================================
/** One-pole parameter smoother, the same shape as the ones in modules/sat/dsp,
    modules/opto/dsp and modules/vcomp/dsp -- see any of them for why it snaps
    to the target inside an epsilon, so a settled parameter compares exactly
    equal.

    INPUT, OUTPUT and MIX each get one. All three are steady knobs most of the
    time and all three are automatable, and stepping a gain block to block with
    no ramp is an audible zipper. 20 ms, per docs/1176-comp/10-dsp-spec.md 10.

    ATTACK and RELEASE deliberately have none: they are the knob *position*,
    they set coefficients rather than a level, and 11 section 2 marks them
    "target only". RATIO and VOICING are switches and are crossfaded instead,
    which is a different thing and is done in DspCore below. */
class Smoother
{
public:
    void prepare (double sampleRate, double timeMs) noexcept
    {
        const auto tau = std::max (timeMs, 0.01) * 0.001;
        coeff = (float) (1.0 - std::exp (-1.0 / (std::max (sampleRate, 1.0) * tau)));
    }

    void snap (float v) noexcept      { current = target = v; }
    void setTarget (float t) noexcept { target = t; }

    float tick() noexcept
    {
        current += coeff * (target - current);

        if (std::abs (target - current) < 1.0e-6f)
            current = target;

        return current;
    }

private:
    float coeff = 1.0f, current = 0.0f, target = 0.0f;
};

//==============================================================================
/** BMO FET: a 1176-style FET compressor.

        INPUT drive -> input transformer/amp -> FET shunt cell
                    -> output amplifier -> output transformer
                    -> OUTPUT makeup -> mix

    The detector taps the **cell output**, before the output amplifier, and
    returns a control to the gate: a true feedback loop. There is no threshold
    control, because the hardware family has none -- INPUT drives signal into a
    fixed threshold and OUTPUT restores level -- and no lookahead.

    The pieces, and where each is written down:

    - `FetCell.h` is the divider law `g = 1/(1 + k*c)` and the per-sample
      implicit solve. The attack one-pole sits **inside** the divider, or the
      loop is a unit-delay loop and oscillates; substituting it gives a
      quadratic whose stable root is a closed form. No iteration, no unit
      delay, and `alpha = 1` is legal -- which is what makes the 20 us attack
      work at base rate.
    - `Detector.h` is the linear sidechain, the ratio family, the
      programme-dependent release and all-buttons' plateau and lag.
    - `Stages.h` is the static colour: transformer poles, LF core saturation
      and the two amplifiers, each anti-aliased by first-order ADAA on its
      residual only.
    - `Calibration.h` holds **every** constant the spec marks CALIBRATE, in
      one place, each of them a first-pass value awaiting measurement and an
      ear. Nothing in this file invents a number.

    **Stereo is always linked and is not a parameter** -- the cell is one
    control voltage. One shared control is driven by the louder channel's cell
    output and applied to both, deriving both outputs from the previous
    sample's gain, exactly as modules/opto/dsp/DspCore.h's `processLinked`
    already does for its feedback cell.

    **What must survive any change here** -- the contract the rest of the
    module is built against:

    - `Params` below carries every parameter `specs()` has, in real units, and
      the adapter (FetcompDsp.h) is the only thing that unpacks the flat array.
      Add DSP state here, not parameters.
    - Latency is 0 / 40 / 60 samples at Off / 2x / 4x and **0 at the default**,
      it is `Oversampler::latencyForFactor` and nothing hand-written, and it is
      the same in both voicings. The voicing must not appear in
      `latencyForParams` at all.
    - The dry path of MIX is delay-matched to the wet one, which carries the
      oversampler's round trip. Undelayed, a partial blend combs -- two copies
      ~1.2 kHz apart in notch spacing at 48 k -- and `mix` = 0 stops nulling,
      which is also how bypass is proven. The ring is cleared when the factor
      changes: the EQ review records what going without costs, a click and up
      to 70 samples of misalignment.
    - `currentGainReductionDb()` is **signed, positive = gain taken away**
      (core/dsp/ModuleDsp.h), and it keeps reporting the true figure past the
      24 dB the meter pins at. The clamp is a drawing limit, not a measurement
      one.
    - Nothing hard-clips before the output stage. The divider law is bounded
      by construction and the shapers are soft, so a slammed signal degrades
      into the stage models rather than into a clip.

    The spec is docs/1176-comp/10-dsp-spec.md; what the tests ask of it is
    docs/1176-comp/11-integration-and-test-plan.md 3. */
class DspCore
{
public:
    struct Params
    {
        float   inputDb         = 0.0f;
        float   outputDb        = 0.0f;
        float   attackPosition  = kPositionDefault;   ///< 1..7, 7 fastest
        float   releasePosition = kPositionDefault;   ///< 1..7, 7 fastest
        Ratio   ratio           = Ratio::four;
        float   mixPercent      = 100.0f;
        Voicing voicing         = Voicing::black;
        int     oversampling    = 1;                  ///< the factor, 1 / 2 / 4
    };

    void prepare (double newSampleRate, int /*maxBlockSize*/, int numChannels) noexcept
    {
        rate = std::max (newSampleRate, 1.0);
        numActiveChannels = std::clamp (numChannels, 1, kMaxChannels);

        inputGain .prepare (rate, kGainSmoothingMs);
        outputGain.prepare (rate, kGainSmoothingMs);
        mix       .prepare (rate, kGainSmoothingMs);

        inputGain .snap (gainFor (params.inputDb));
        outputGain.snap (gainFor (params.outputDb));
        mix       .snap (params.mixPercent * 0.01f);

        applyFactor (params.oversampling);

        // The switches are where the parameters say they are, not mid-fade:
        // preparing is not a user gesture.
        currentSidechain = targetSidechain = sidechainFor (params.ratio);
        ratioFade = 1.0;

        for (auto& c : channels)
        {
            c.stages[0].setVoicing (constantsFor (params.voicing));
            c.stages[1].setVoicing (constantsFor (params.voicing));
        }

        activeStages = 0;
        voicingFade = 1.0;
        fromCell = toCell = constantsFor (params.voicing);

        updateCoefficients();
        reset();
    }

    void reset() noexcept
    {
        for (auto& c : channels)
        {
            c.oversampler.reset();
            c.stages[0].reset();
            c.stages[1].reset();
            c.lastCellOutput = 0.0f;
        }

        release.reset();
        lag.reset();
        plateauLevel.reset();

        cell = {};
        cell.bias = currentSidechain.standingControl;

        clearDryLine();
        reportedReductionDb = 0.0f;
    }

    void setParams (const Params& p) noexcept
    {
        // A factor change re-primes the delay line rather than leaving a ring
        // full of samples that belong to a different alignment, and resets the
        // oversampler's own filters with it.
        if (p.oversampling != params.oversampling)
        {
            applyFactor (p.oversampling);
            clearDryLine();

            for (auto& c : channels)
            {
                c.oversampler.reset();
                c.stages[0].reset();
                c.stages[1].reset();
            }
        }

        if (p.ratio != params.ratio)
        {
            // The control state is preserved -- what walks across is the
            // network the loop looks through, so the compression does not
            // jump while the constants move.
            currentSidechain = blendedSidechain();
            targetSidechain  = sidechainFor (p.ratio);
            ratioFade = 0.0;
        }

        if (p.voicing != params.voicing)
            startVoicingFade (p.voicing);

        const auto retime = p.attackPosition  != params.attackPosition
                         || p.releasePosition != params.releasePosition
                         || p.ratio           != params.ratio;

        params = p;

        inputGain .setTarget (gainFor (p.inputDb));
        outputGain.setTarget (gainFor (p.outputDb));
        mix       .setTarget (std::clamp (p.mixPercent, 0.0f, 100.0f) * 0.01f);

        if (retime)
            updateCoefficients();
    }

    void process (float* const* channelData, int numChannels, int numSamples) noexcept
    {
        const auto active = std::clamp (numChannels, 1, numActiveChannels);
        const auto factor = currentFactor;
        auto worstReduction = 0.0f;

        for (int n = 0; n < numSamples; ++n)
        {
            // One tick per sample for the whole block, not per channel: the
            // three gains are shared, and ticking them per channel would run
            // the ramp at twice the rate in stereo.
            const auto in  = inputGain .tick();
            const auto out = outputGain.tick();
            const auto wet = mix       .tick();
            const auto dry = 1.0f - wet;

            advanceCrossfades();

            const auto readIndex = (dryWrite + 1) % dryLength;
            float delayed[kMaxChannels] {};
            float buffer[kMaxChannels][Oversampler::kMaxFactor] {};

            for (int ch = 0; ch < active; ++ch)
            {
                auto& line = dryLine[(size_t) ch];
                line[(size_t) dryWrite] = channelData[ch][n];
                delayed[ch] = line[(size_t) readIndex];

                channels[(size_t) ch].oversampler.upsample (channelData[ch][n] * in,
                                                            buffer[ch]);
            }

            for (int j = 0; j < factor; ++j)
                worstReduction = std::max (worstReduction, processFrame (buffer, active, j));

            for (int ch = 0; ch < active; ++ch)
            {
                const auto shaped = channels[(size_t) ch].oversampler.downsample (buffer[ch]);

                // OUTPUT is makeup for the compressed path and belongs to it
                // alone. Applied to the blend instead it would lift the dry
                // signal too, and `mix` = 0 would stop being a bypass.
                channelData[ch][n] = wet * shaped * out + dry * delayed[ch];
            }

            dryWrite = (dryWrite + 1) % dryLength;
        }

        if (numSamples > 0)
            reportedReductionDb = worstReduction;

        // Anything past the channels prepare() sized for is left alone. This
        // is a stereo module; a host that hands over more gets them back
        // untouched rather than half-processed.
    }

    /** Delay this core adds at the base rate, for a factor. Whole samples by
        construction -- see core/dsp/Oversampler.h. */
    static int latencyFor (int factor) noexcept
    {
        return Oversampler::latencyForFactor (factor);
    }

    /** The worst reduction seen in the block just processed, in dB, always
        >= 0 -- signed the repo's way, positive = gain taken away, and read
        from the audio thread immediately after process(), the same
        publish-and-sample rule core/dsp/Meter.h keeps.

        **It keeps reporting the true figure past 24 dB**, which is where
        `ui::DynamicsMeter` pins. The clamp is a drawing limit. */
    float currentGainReductionDb() const noexcept { return reportedReductionDb; }

    const Params& getParams() const noexcept { return params; }

private:
    static constexpr int kMaxChannels = 2;

    /** Sized from the oversampler's worst case rather than from the factors
        this module offers: stating the number by hand got it wrong once
        elsewhere in the tree and overran the buffer. One extra slot, because
        the write and the read are a whole `length` apart. */
    static constexpr size_t kLineLength = (size_t) Oversampler::kMaxLatency + 2;

    struct Channel
    {
        Oversampler oversampler;

        /** Two constant sets, so a voicing change can crossfade the shapers'
            *outputs* rather than ramp their coefficients. ADAA state has to be
            rebuilt on any coefficient change, and rebuilding it every sample
            through a ramp would smear the residual it exists to anti-alias. */
        std::array<StaticStages, 2> stages;

        /** The previous sample's **signed** cell output, which is what the
            distortion factor reads. Signed, or the even order vanishes. */
        float lastCellOutput = 0.0f;
    };

    static float gainFor (float db) noexcept { return std::pow (10.0f, db * 0.05f); }

    void applyFactor (int factor) noexcept
    {
        currentFactor = factor >= 4 ? 4 : (factor >= 2 ? 2 : 1);

        for (auto& c : channels)
        {
            c.oversampler.setFactor (currentFactor);
            c.stages[0].prepare (rate * currentFactor);
            c.stages[1].prepare (rate * currentFactor);
        }

        dryLength = latencyFor (currentFactor) + 1;
        updateCoefficients();
    }

    void clearDryLine() noexcept
    {
        for (auto& line : dryLine)
            line.fill (0.0f);

        dryWrite = 0;
    }

    /** Everything derived from a time and the **effective** rate, so nothing
        is hard-coded and every coefficient follows the sample rate and the
        oversampling factor without being told twice. */
    void updateCoefficients() noexcept
    {
        const auto effective = rate * currentFactor;

        sidechain.attack   = attackStepFor (params.attackPosition, effective);
        sidechain.rectPole = 1.0 - poleFor (kRectifierMicroseconds * 1.0e-6, effective);

        release.setTime (releaseSecondsFor (params.releasePosition),
                         targetSidechain.releaseScale, effective);
        lag.setTime (targetSidechain.lagSeconds, effective);
        plateauLevel.setTime (kAllButtonsPlateauEnvelopeMs * 0.001, effective);
    }

    void startVoicingFade (Voicing to) noexcept
    {
        const auto incoming = activeStages ^ 1;

        for (auto& c : channels)
        {
            // The incoming set takes over the outgoing one's filter memory and
            // is re-seated on the sample the signal is actually at: one sample
            // of first-order error, inaudible under the fade, against an
            // unbounded spike if either is left where it was.
            c.stages[(size_t) incoming].setVoicing (constantsFor (to));
            c.stages[(size_t) incoming].adoptStateFrom (c.stages[(size_t) activeStages]);
        }

        fromCell = blendedCell();
        toCell   = constantsFor (to);
        voicingFade = 0.0;
        fadingStages = true;
        activeStages = incoming;
    }

    VoicingConstants blendedCell() const noexcept
    {
        return blendCell (fromCell, toCell, voicingFade);
    }

    Sidechain blendedSidechain() const noexcept
    {
        return blend (currentSidechain, targetSidechain, ratioFade);
    }

    void advanceCrossfades() noexcept
    {
        if (ratioFade < 1.0)
        {
            ratioFade = std::min (1.0, ratioFade + 1.0 / (kRatioCrossfadeMs * 0.001 * rate));

            if (ratioFade >= 1.0)
                currentSidechain = targetSidechain;
        }

        if (voicingFade < 1.0)
        {
            voicingFade = std::min (1.0, voicingFade + 1.0 / (kVoicingCrossfadeMs * 0.001 * rate));

            if (voicingFade >= 1.0)
            {
                fromCell = toCell;
                fadingStages = false;
            }
        }

        const auto s = blendedSidechain();
        sidechain.gain      = s.gain;
        sidechain.threshold = s.threshold;
        activeSidechain = s;

        cellConstants = blendedCell();
        cellConstants.qBias *= s.qScale;
        cell.bias = s.standingControl;
    }

    /** One oversampled frame: the static blocks before the cell, one shared
        solve, the divider applied to every channel, and the static blocks
        after it. Returns the reduction this frame is holding, in dB. */
    float processFrame (float (&buffer)[kMaxChannels][Oversampler::kMaxFactor],
                        int activeChannels, int j) noexcept
    {
        const auto mixIn = (float) voicingFade;
        float cellIn[kMaxChannels] {};
        auto m = 0.0;

        for (int ch = 0; ch < activeChannels; ++ch)
        {
            auto& c = channels[(size_t) ch];
            const auto x = buffer[ch][j];

            auto shaped = c.stages[(size_t) activeStages].processInput (x);

            if (fadingStages)
                shaped = (1.0f - mixIn) * c.stages[(size_t) (activeStages ^ 1)].processInput (x)
                       + mixIn * shaped;

            cellIn[ch] = shaped;
            m = std::max (m, (double) std::abs (shaped));
        }

        // The sidechain gain is frozen for this sample. For the four ratios it
        // is simply G_R; under all-buttons it is the fitted collapse, read
        // from an envelope of the previous samples' demand so the quadratic
        // below is unchanged and the collapse is keyed on a level rather than
        // on one sample of a rectified waveform.
        const auto rawDemand = activeSidechain.gain
                                 * std::max (0.0, cell.rectifier - activeSidechain.threshold);

        sidechain.gain = collapsedGain (activeSidechain, plateauLevel.process (rawDemand));

        const auto v = solveCellOutput (m, cell, sidechain);

        const auto predicted = (1.0 - sidechain.rectPole) * cell.rectifier
                             + sidechain.rectPole * v;
        const auto demand = sidechain.gain * std::max (0.0, predicted - sidechain.threshold);

        // Attack lives in the solve; the release branches sit outside it, so
        // the programme-dependent shape stays the one recorded in vcomp.
        const auto attacked = (1.0 - sidechain.attack) * cell.control
                            + sidechain.attack * demand;

        auto control = release.tick (demand, attacked, cell.control);
        control = lag.process (control);
        control = std::clamp (control, 0.0, std::max (0.0, kControlCeiling - cell.bias));

        if (control < kControlFloor)
            control = 0.0;

        cell.control = control;

        const auto total = control + cell.bias;
        const auto depth = normalisedDepthFor (total);
        auto detected = 0.0;
        auto detectorSample = 0.0;

        for (int ch = 0; ch < activeChannels; ++ch)
        {
            auto& c = channels[(size_t) ch];

            // Each channel's FET sees its own drain-source voltage, so each
            // gets its own distortion factor even though the control voltage
            // is shared. The solve above used the factor the detector channel
            // left behind, which is the one that decides the loop.
            const auto factor = cellDistortionFactor ((double) c.lastCellOutput,
                                                      depth, cellConstants);
            const auto g = 1.0 / (1.0 + kCellConductance * total * factor);
            const auto y = (float) ((double) cellIn[ch] * g);

            c.lastCellOutput = y;

            const auto magnitude = (double) std::abs (y);

            if (magnitude >= detected)
            {
                detected = magnitude;
                detectorSample = (double) y;
            }

            auto shaped = c.stages[(size_t) activeStages].processOutput (y);

            if (fadingStages)
                shaped = (1.0f - mixIn) * c.stages[(size_t) (activeStages ^ 1)].processOutput (y)
                       + mixIn * shaped;

            buffer[ch][j] = shaped;
        }

        // The detector taps the cell output, before the output amplifier, and
        // hears the louder of the two channels -- one control voltage for the
        // pair, which is why there is no LINK switch.
        cell.rectifier = (1.0 - sidechain.rectPole) * cell.rectifier
                       + sidechain.rectPole * detected;

        if (cell.rectifier < kControlFloor)
            cell.rectifier = 0.0;

        cell.factor = cellDistortionFactor (detectorSample, depth, cellConstants);

        return reductionDbFor (total);
    }

    Params params;
    double rate = 48000.0;
    int numActiveChannels = kMaxChannels;
    int currentFactor = 1;

    Smoother inputGain, outputGain, mix;

    std::array<Channel, kMaxChannels> channels;

    CellState cell;
    SidechainState sidechain;
    ReleaseStage release;
    ControlLag lag;
    PeakFollower plateauLevel;

    Sidechain currentSidechain = sidechainFor (Ratio::four);
    Sidechain targetSidechain  = sidechainFor (Ratio::four);
    Sidechain activeSidechain  = sidechainFor (Ratio::four);
    double ratioFade = 1.0;

    VoicingConstants cellConstants = kBlack, fromCell = kBlack, toCell = kBlack;
    double voicingFade = 1.0;
    int  activeStages = 0;
    bool fadingStages = false;

    std::array<std::array<float, kLineLength>, kMaxChannels> dryLine {};
    int dryWrite = 0, dryLength = 1;

    float reportedReductionDb = 0.0f;
};

} // namespace bmo::fetcomp
