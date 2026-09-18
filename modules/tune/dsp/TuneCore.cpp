#include "modules/tune/dsp/TuneCore.h"
#include "modules/tune/dsp/Denormals.h"
#include <algorithm>
#include <cmath>

namespace bmo::tune
{

TuneParams TuneParams::fromValues (const float* v, int count) noexcept
{
    TuneParams p;
    if (count < Index::count)
        return p;

    const auto choice = [v] (int i, int n) { return std::clamp ((int) std::lround (v[i]), 0, n - 1); };
    const auto on = [v] (int i) { return v[i] >= 0.5f; };

    p.retuneMs = retuneMsOfStep (choice (Index::retuneMs, kNumRetuneSteps));
    p.key = pitchClassOfKey (choice (Index::key, kNumKeySpellings));
    p.scale = (ScaleType) choice (Index::scale, (int) ScaleType::count);
    p.range = (Range) choice (Index::range, 5);
    p.vibratoPercent = v[Index::vibrato];
    p.flexPercent = v[Index::flex];
    p.refA = v[Index::refA];

    p.allowed = 0;
    for (int n = 0; n < 12; ++n)
        if (on (Index::noteC + n))
            p.allowed = (NoteMask) (p.allowed | (1u << n));

    return p;
}

void TuneCore::prepare (double sampleRate, int)
{
    fs = sampleRate;

    Detector::Settings ds;
    const auto limits = limitsOf (params.range);
    ds.minHz = limits.minHz;
    ds.maxHz = limits.maxHz;
    det.prepare (fs, ds);

    law.prepare (fs);
    engine.prepare (fs, fs / Detector::kCapacityMinHz);

    paramsDirty = true;
    applyParams();
    reset();
}

void TuneCore::reset()
{
    det.reset();
    law.reset();
    engine.reset();
    samplePosition = 0;
}

void TuneCore::setParams (const TuneParams& p) noexcept
{
    params = p;
    paramsDirty = true;
}

void TuneCore::applyParams() noexcept
{
    if (! paramsDirty)
        return;

    paramsDirty = false;

    const auto limits = limitsOf (params.range);
    Detector::Settings ds;
    ds.minHz = limits.minHz;
    ds.maxHz = limits.maxHz;
    det.setSettings (ds);

    CorrectionSettings cs;
    cs.refA = params.refA;
    cs.key = params.key;
    cs.scale = params.scale;
    cs.allowed = params.allowed;
    cs.retuneMs = params.retuneMs;
    cs.vibratoAmount = params.vibratoPercent / 100.0;
    cs.flex = params.flexPercent / 100.0;
    cs.clarityLo = ds.clarityLo;
    cs.clarityHi = ds.clarityHi;

    // readDelaySamples is deliberately left at its default, which means "ask
    // the contract". Copying the engine's rest here was right only while that
    // rest was a constant; once it tracked the note (2026-09-12) the copy
    // went stale the moment the singer moved. Both sides read
    // contract::liveRest now, so they cannot disagree.
    law.setSettings (cs);
}

void TuneCore::process (float* samples, int numSamples) noexcept
{
    ScopedNoDenormals noDenormals;
    applyParams();

    for (int i = 0; i < numSamples; ++i)
    {
        const auto x = samples[i];
        det.push (x);

        const auto& est = det.estimate();
        const auto evaluated = det.evaluatedThisSample();
        const auto cents = law.tick (est, evaluated);

        // Homing is allowed once the correction has faded all the way out on
        // an unvoiced stretch -- then the engine is carrying nothing worth
        // keeping in its delay.
        const auto settled = ! est.voiced && cents == 0.0;
        // The law's period, not the detector's raw one: they differ exactly
        // when a jump is unconfirmed, which is when the audible pops happen.
        samples[i] = engine.process (x, cents, law.heldPeriod(), settled);

        if (analysisTap != nullptr)
        {
            AnalysisFrame f;
            f.sample = samplePosition;
            f.f0 = est.hz;
            f.clarity = est.clarity;
            f.voiced = est.voiced;
            f.evaluated = evaluated;
            f.pitchIn = law.state().pitchIn;
            f.target = law.state().target;
            f.note = law.state().note;
            f.appliedCents = cents;
            f.ratio = engine.currentRatio();
            f.lag = engine.currentLag();
            f.splice = engine.splicedThisSample();
            f.spliceMismatch = engine.spliceMismatch();
            analysisTap (analysisContext, f);
        }

        ++samplePosition;
    }
}

} // namespace bmo::tune
