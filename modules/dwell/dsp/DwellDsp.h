#pragma once

#include "core/dsp/ModuleDsp.h"
#include "modules/dwell/dsp/DspCore.h"
#include "modules/dwell/dsp/Timing.h"
#include "modules/dwell/params.h"

namespace bmo::dwell
{

/** The adapter between the suite's parameter array and BMO Dwell's core.

    `setParams` is the only place in the module that turns a spec index into a
    named value, which is why it is worth a test of its own: the schema is
    permanent, so a lane read off by one here would be wrong for the life of
    the product and would look like a DSP fault rather than a wiring one. */
class DwellDsp final : public ModuleDsp
{
public:
    void prepare (double sampleRate, int maxBlockSize, int numChannels) override
    {
        core.prepare (sampleRate, maxBlockSize, numChannels);
    }

    void reset() override { core.reset(); }

    void setParams (const float* v, int count) override
    {
        if (count < Index::count)
            return;

        DspCore::Params p;
        p.timeMs          = v[Index::time];
        p.sync            = kSyncIsEnabled && v[Index::sync] > 0.5f;
        p.noteChoice      = (int) v[Index::note];
        p.feedbackPct     = v[Index::feedback];
        p.characterChoice = (int) v[Index::character];
        p.stereoChoice    = (int) v[Index::stereo];
        p.lowCutHz        = v[Index::lowCut];
        p.highCutHz       = v[Index::highCut];
        p.modRateHz       = v[Index::modRate];
        p.modDepthPct     = v[Index::modDepth];
        p.drivePct        = v[Index::drive];
        p.duckDb          = v[Index::duck];
        p.mixPct          = v[Index::mix];
        p.sendHeld        = v[Index::send] > 0.5f;
        p.laneGain        = v[Index::laneGain];
        p.hold            = v[Index::hold] > 0.5f;
        p.chop            = v[Index::chop] > 0.5f;
        p.fx              = v[Index::fx] > 0.5f;
        p.fxTypeChoice    = (int) v[Index::fxType];
        p.fxAmountPct     = v[Index::fxAmount];

        // The lane, ids 20-24. **Five values, and no voicing among them**: the
        // lane runs the character, stereo mode, cuts, modulation and drive
        // read above, so there is nothing here to keep in step with them
        // (modules/dwell/params.h, 2026-09-22).
        p.laneLevelDb           = v[Index::laneLevel];
        p.laneTimeMs            = v[Index::laneTime];
        p.laneFx                = v[Index::laneFx] > 0.5f;
        p.laneFxTypeChoice      = (int) v[Index::laneFxType];
        p.laneFxAmountPct       = v[Index::laneFxAmount];

        // Id 25, the last row: whether the lane's FX trio follows the main
        // delay's. Carried whatever it says -- a value that stops arriving
        // here is a value the lane could not go back to.
        p.fxLink                = v[Index::fxLink] > 0.5f;

        laneNoteChoice = (int) v[Index::laneNote];
        params = p;
        apply();
    }

    /** The host's tempo for this block, after `setParams` and before `process`
        (core/dsp/ModuleDsp.h). docs/delay/10 §7's policy:

        - **No valid tempo: hold the last one**, and if none has ever arrived
          the engines stay on their own millisecond times. `heldBpm` is 0
          until the first, and a chain edit that rebuilds this DSP starts it
          at 0 again -- which the contract says to expect.
        - **A stopped transport changes nothing.** The tempo is still valid,
          audio flows and the loop decays; nothing is muted or flushed.
        - **Only a changed tempo re-applies.** `setParams` already mapped the
          divisions at the held tempo, so re-applying every block would hand
          the engines the same time twice for nothing. */
    void setTempo (double bpm, bool valid, bool playing) noexcept override
    {
        (void) playing;

        if (! valid || bpm == heldBpm)
            return;

        heldBpm = bpm;

        if (params.sync)
            apply();
    }

    /** §9 and §11.6's tail, from parameters only. See `tailSecondsFor`. */
    double tailSecondsForParams (const float* values, int count) const override
    {
        return tailSecondsFor (values, count);
    }

    /** The tempo the divisions are mapped at, or 0 before the first. For tests. */
    double getHeldBpm() const noexcept { return heldBpm; }

    void process (float* const* channels, int numChannels, int numSamples) override
    {
        core.process (channels, numChannels, numSamples);
    }

    /** **Zero, at every setting, permanently.**

        There is no oversampling (docs/delay/10 §0 drops it outright, which is
        what pays for the 24-tap interpolator) and no lookahead, so nothing
        here ever costs the host a sample of delay.

        **The wet delay time is not latency and is never reported as such.**
        What the host compensates for is a delayed copy of what it sent; a
        delay's repeats are new signal arriving late on purpose. Reporting
        TIME here would pull the whole track forward by up to two seconds and
        move the dry signal with it. BMO Dimension's detune voices set the same
        precedent -- `modules/dim/dsp` reports 0 while manufacturing content
        that is not time-aligned with the input.

        If 10 §4's half-band fallback is ever added around the shaper, its
        group delay is a whole number of samples and is subtracted from D, so
        the figure stays 0 and the delay time stays exact. */
    int latencyForParams (const float*, int) const override { return 0; }

    DspCore& getCore() noexcept { return core; }

private:
    /** Hands the engines the parameters, with SYNC's divisions mapped to
        milliseconds at the held tempo. **Mapped here, in `setParams`, not in
        `setTempo`**: the tempo arrives after the parameters each block, and
        mapping it there would hand the engines the knob's time and then the
        note's, every block -- and a TIME move re-sweeps the 1024-point loop
        peak. Before the first tempo, the knobs stand. */
    void apply() noexcept
    {
        auto p = params;

        if (p.sync && heldBpm > 0.0)
        {
            p.timeMs     = (float) syncedMs (p.noteChoice, heldBpm);
            p.laneTimeMs = (float) syncedMs (laneNoteChoice, heldBpm);
        }

        core.setParams (p);
    }

    DspCore core;
    DspCore::Params params;
    int laneNoteChoice = kDefaultLaneNote;
    double heldBpm = 0.0;
};

inline std::unique_ptr<ModuleDsp> createDsp() { return std::make_unique<DwellDsp>(); }

} // namespace bmo::dwell
