#pragma once

#include "Detector.h"
#include "core/dsp/SwitchFade.h"
#include <algorithm>
#include <array>
#include <cmath>

namespace bmo::opto
{

enum class Mode { La2a = 0, Distressor = 1 };

/** One-pole parameter smoother, same shape as the one in modules/sat/dsp --
    see that file for why: snaps to the target once it is within epsilon, so
    a settled parameter compares exactly equal. CRUSH and LEVEL are steady
    knobs most of the time, but a session can still automate either, and
    stepping the curve or the makeup gain block-to-block with no ramp is an
    audible zipper. Mode, Link and Color are switches, and a one-pole is the
    wrong shape for those: they cross over in a straight line instead -- see
    DspCore. */
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

        if (std::abs (target - current) < 1.0e-5f)
            current = target;

        return current;
    }

private:
    float coeff = 1.0f, current = 0.0f, target = 0.0f;
};

//==============================================================================
/** BMO Opto: input -> the cell -> makeup gain (LEVEL) -> drive (optional) ->
    output.

    Mode picks a genuinely different signal path, not just different curve
    numbers: one mode runs a feedback cell (La2aCell), the other a feedforward
    one (DistressorCell) -- see Detector.h for why each is built the way it
    is. Link shares one cell's gain reduction across both channels instead of
    letting them compress independently; it is orthogonal to Mode. Color is an
    on/off harmonic stage, mode-flavoured the same way -- except Tele mode has
    no off switch for it: `params.color` is only honoured in Stressed mode,
    Tele always runs its drive stage. That's a placeholder product decision
    (see params.h), enforced here so it holds regardless of what the panel
    does or doesn't grey out.

    **None of the three switches steps the output.** Each used to change the
    path in one sample, and each measured: Mode by 45 to 156 times the
    signal's own largest step, Link by 18 to 85 times on the quieter channel,
    which unlinking also left 15 to 21 dB louder at once, and Color by 8 times
    where the clip bends. The house rule is that a step which measures gets a
    fade, heard or not. So each is a 10 ms straight-line crossfade from
    core/dsp/SwitchFade.h between the two paths it chooses between, and the
    paths are arranged so that both sides of every crossfade are real:

      - Both cells listen all the time, whichever one Mode has chosen to
        hear, and Tele's drive stage runs with them. See Cells.
      - Link has its own pair of cells, apart from the two channels' own, and
        when Link moves the pair that is about to be heard starts from the
        state of the pair that was. See followSwitches().

    With nothing switching, each crossfade returns one side exactly, and the
    output is the same bits it was before any of this existed. */
class DspCore
{
public:
    struct Params
    {
        float crushPercent = 35.0f;
        float levelDb      = 0.0f;
        Mode  mode         = Mode::La2a;
        bool  link         = true;
        bool  color        = false;
    };

    void prepare (double newSampleRate, int /*maxBlockSize*/, int numChannels) noexcept
    {
        rate = newSampleRate;
        numActiveChannels = std::clamp (numChannels, 1, (int) own.size());

        for (auto* cells : { &own[0], &own[1], &linked })
        {
            cells->la2a.prepare (rate);
            cells->distressor.prepare (rate);
        }

        // The drive's DC blocker derives its pole from the rate -- see
        // DcBlocker::prepare() for why a hard-coded one was wrong.
        for (auto& stage : stages)
            stage.la2aDrive.prepare (rate);

        crushSmoother.prepare (rate, 15.0);
        levelSmoother.prepare (rate, 15.0);
        crushSmoother.snap (params.crushPercent);
        levelSmoother.snap (params.levelDb);

        modeFade.prepare (rate, kSwitchFadeMs);
        linkFade.prepare (rate, kSwitchFadeMs);
        colorFade.prepare (rate, kSwitchFadeMs);

        reset();
    }

    void reset() noexcept
    {
        for (auto* cells : { &own[0], &own[1], &linked })
        {
            cells->la2a.reset();
            cells->distressor.reset();
        }

        for (auto& stage : stages)
        {
            stage.la2aDrive.reset();
            stage.distressorDrive.reset();
        }

        reportedReductionDb = 0.0f;

        // Nothing has been heard since this, so there is nothing for a switch
        // to cross over from: the next block takes them as they stand.
        switchesSettled = false;
    }

    void setParams (const Params& p) noexcept
    {
        params = p;
        crushSmoother.setTarget (p.crushPercent);
        levelSmoother.setTarget (p.levelDb);
    }

    void process (float* const* channelData, int numChannels, int numSamples) noexcept
    {
        const auto active = std::min (numChannels, numActiveChannels);
        auto blockMaxReduction = 0.0f;

        followSwitches (active);

        for (int i = 0; i < numSamples; ++i)
        {
            const auto crushNow      = crushSmoother.tick();
            const auto teleCurve     = curveForLa2a (crushNow);
            const auto stressedCurve = curveForDistressor (crushNow);
            const auto makeupLin     = std::pow (10.0f, levelSmoother.tick() / 20.0f);

            // 0 is Tele, the two channels' own cells, Color off; 1 is
            // Stressed, the linked pair, Color on.
            const auto modeAt  = modeFade.next();
            const auto linkAt  = linkFade.next();
            const auto colorAt = colorFade.next();

            const auto heard = hear (channelData, i, active, linkAt, teleCurve, stressedCurve);

            blockMaxReduction = std::max (blockMaxReduction,
                                          between (heard.teleReductionDb, heard.stressedReductionDb, modeAt));

            for (int ch = 0; ch < active; ++ch)
            {
                auto& stage = stages[(size_t) ch];

                // Tele has no off switch for its drive stage -- it always
                // runs, heard or not, because its DC blocker has a memory
                // like the cells do. Stressed's is a real toggle and has
                // none, so it only runs where it can be heard.
                const auto tele = stage.la2aDrive.process (heard.tele[ch] * makeupLin);
                auto stressed   = heard.stressed[ch] * makeupLin;

                if (modeAt > 0.0f && colorAt > 0.0f)
                    stressed = between (stressed, stage.distressorDrive.process (stressed), colorAt);

                channelData[ch][i] = between (tele, stressed, modeAt);
            }
        }

        reportedReductionDb = blockMaxReduction;
    }

    /** The worst (largest) gain reduction seen in the block just processed,
        in dB, always >= 0. Called from the audio thread immediately after
        process(), same as core/dsp/Meter.h's own publish-and-sample rule --
        see core/product/ModuleEngine.h. */
    float currentGainReductionDb() const noexcept { return reportedReductionDb; }

private:
    /** How long a switch takes to cross over: the figure the repository's
        other switches use. */
    static constexpr double kSwitchFadeMs = 10.0;

    /** One of each cell, and both listen all the time, whichever one Mode
        has chosen to hear.

        The cell that was not in use used to be left exactly as it was when
        Mode last moved away from it. Its envelope, charge and dosage are
        memories of the programme, and a memory that stops being written to is
        simply out of date: switch away just after a loud passage and back
        three seconds later, and the cell came back still holding that
        passage, 11.1 dB (Tele) and 6.7 dB (Stressed) below a core that had
        never left, for more than three seconds. Running both costs one more
        cell and means the one that is switched to has heard everything the
        other has, which is also what gives Mode's crossfade two real sides. */
    struct Cells
    {
        La2aCell       la2a;
        DistressorCell distressor;
    };

    struct Stage
    {
        La2aDrive       la2aDrive;
        DistressorDrive distressorDrive;
    };

    /** What one sample comes to in each mode, before makeup and drive. */
    struct Heard
    {
        float tele[2] {}, stressed[2] {};
        float teleReductionDb = 0.0f, stressedReductionDb = 0.0f;
    };

    /** `a` at 0, `b` at 1, and each of them exactly there: a crossfade that
        is not moving costs nothing and changes nothing, and never multiplies
        the side that is not in use by zero, which would turn an infinity on
        that side into a NaN on this one. */
    static float between (float a, float b, float at) noexcept
    {
        return at <= 0.0f ? a : at >= 1.0f ? b : dsp::crossfade (a, b, at);
    }

    /** Sets each crossfade moving towards where its switch now stands, and
        hands the cells' state across when Link starts to move.

        Mode and Color have nothing to hand over: both sides of each are
        always running. Link's sides are not. Only the pair in use listens,
        because running three pairs to keep two warm would triple the cost of
        the cells for a switch that is rarely touched. So when Link starts to
        move from rest, the pair about to be heard is given the state of the
        pair that was:

          - Link off: each channel's own cells start as copies of the linked
            pair. The reduction on both channels is continuous, and each then
            goes its own way at its own release.
          - Link on: the linked pair starts as a copy of whichever channel's
            cell is reducing more, which is the one it would have followed.
            That channel is continuous; the other crosses over to the shared
            gain in the 10 ms.

        A switch flicked back before its crossfade has finished finds both
        pairs running and current, so nothing is copied. */
    void followSwitches (int active) noexcept
    {
        const auto modeTarget  = params.mode == Mode::Distressor ? 1.0f : 0.0f;
        const auto linkTarget  = params.link ? 1.0f : 0.0f;
        const auto colorTarget = params.color ? 1.0f : 0.0f;

        if (! switchesSettled)
        {
            modeFade.snap (modeTarget);
            linkFade.snap (linkTarget);
            colorFade.snap (colorTarget);
            switchesSettled = true;
            return;
        }

        modeFade.setTarget (modeTarget);
        colorFade.setTarget (colorTarget);

        if (linkTarget != linkFade.target())
        {
            if (! linkFade.isMoving() && active >= 2)
            {
                if (params.link)
                {
                    const auto deeperTele     = own[1].la2a.currentReductionDb() > own[0].la2a.currentReductionDb() ? 1 : 0;
                    const auto deeperStressed = own[1].distressor.currentReductionDb() > own[0].distressor.currentReductionDb() ? 1 : 0;

                    linked.la2a       = own[(size_t) deeperTele].la2a;
                    linked.distressor = own[(size_t) deeperStressed].distressor;
                }
                else
                {
                    own[0] = linked;
                    own[1] = linked;
                }
            }

            linkFade.setTarget (linkTarget);
        }
    }

    /** One sample through the cells. Every pair in use listens, and what
        each mode would put out is the channels' own cells, the linked pair,
        or during Link's crossfade a blend of the two. */
    Heard hear (float* const* channelData, int i, int active, float linkAt,
                const Curve& teleCurve, const Curve& stressedCurve) noexcept
    {
        Heard heard;

        const auto useLinked = active >= 2 && linkAt > 0.0f;
        const auto useOwn    = ! useLinked || linkAt < 1.0f;

        if (useOwn)
        {
            // Each channel's own cells react only to its own signal.
            for (int ch = 0; ch < active; ++ch)
            {
                auto& cells = own[(size_t) ch];

                heard.tele[ch]     = cells.la2a.process (channelData[ch][i], teleCurve);
                heard.stressed[ch] = cells.distressor.process (channelData[ch][i], stressedCurve);

                heard.teleReductionDb     = std::max (heard.teleReductionDb, cells.la2a.currentReductionDb());
                heard.stressedReductionDb = std::max (heard.stressedReductionDb, cells.distressor.currentReductionDb());
            }
        }

        if (useLinked)
        {
            // One pair decides a single gain reduction from the louder of
            // the two channels, applied identically to both -- like a real
            // stereo-linked pair sharing one control voltage.
            const auto l = channelData[0][i];
            const auto r = channelData[1][i];

            // Feedback: detection reads the *output*, so derive both
            // channels' outputs from the gain the previous sample decided,
            // pick the louder for detection, then update from that.
            const auto teleGain = linked.la2a.currentGainLin();
            const auto lTele = l * teleGain, rTele = r * teleGain;
            linked.la2a.updateFromOutputSample (std::abs (lTele) > std::abs (rTele) ? lTele : rTele, teleCurve);

            // Feedforward: detection reads the input, and the gain it
            // decides is this sample's.
            linked.distressor.updateFromInputSample (std::abs (l) > std::abs (r) ? l : r, stressedCurve);
            const auto stressedGain = linked.distressor.currentGainLin();

            const float linkedTele[2]     { lTele, rTele };
            const float linkedStressed[2] { l * stressedGain, r * stressedGain };

            for (int ch = 0; ch < 2; ++ch)
            {
                heard.tele[ch]     = useOwn ? between (heard.tele[ch], linkedTele[ch], linkAt) : linkedTele[ch];
                heard.stressed[ch] = useOwn ? between (heard.stressed[ch], linkedStressed[ch], linkAt) : linkedStressed[ch];
            }

            const auto linkedTeleDb     = linked.la2a.currentReductionDb();
            const auto linkedStressedDb = linked.distressor.currentReductionDb();

            heard.teleReductionDb     = useOwn ? between (heard.teleReductionDb, linkedTeleDb, linkAt) : linkedTeleDb;
            heard.stressedReductionDb = useOwn ? between (heard.stressedReductionDb, linkedStressedDb, linkAt) : linkedStressedDb;
        }

        return heard;
    }

    double rate = 44100.0;
    int numActiveChannels = 2;

    std::array<Cells, 2> own;      // one pair per channel, heard when Link is off
    Cells linked;                  // one pair for both, heard when Link is on
    std::array<Stage, 2> stages;

    Smoother crushSmoother, levelSmoother;
    dsp::Ramp modeFade, linkFade, colorFade;
    bool switchesSettled = false;

    Params params;
    float reportedReductionDb = 0.0f;
};

} // namespace bmo::opto
