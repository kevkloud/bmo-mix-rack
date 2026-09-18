#pragma once

#include "modules/tune/dsp/ClassicEngine.h"
#include "modules/tune/dsp/CorrectionLaw.h"
#include "modules/tune/dsp/Detector.h"
#include "modules/tune/params.h"

namespace bmo::tune
{

/** The whole of BMO Tune RT's parameters in real units -- what setParams()
    takes, and what the adapter fills from the host's values by Index. */
struct TuneParams
{
    double retuneMs = 0.0;               ///< tau in ms, on retune_ms's steps; 0 is the snap
    int key = 0;
    ScaleType scale = ScaleType::chromatic;
    Range range = Range::autoRange;
    double vibratoPercent = 0.0;
    double flexPercent = 0.0;
    double refA = 440.0;
    NoteMask allowed = kAllNotes;

    /** From an array of values in spec order, as ModuleDsp::setParams gets. */
    static TuneParams fromValues (const float* v, int count) noexcept;
};

/** Everything --dump-analysis writes for one sample (spec T-1). */
struct AnalysisFrame
{
    long long sample = 0;
    double f0 = 0.0, clarity = 0.0;
    bool voiced = false, evaluated = false;
    double pitchIn = 0.0, target = 0.0;
    int note = -1;
    double appliedCents = 0.0, ratio = 1.0, lag = 0.0;
    bool splice = false;
    double spliceMismatch = 0.0;   ///< non-zero on the sample a splice fade ends
};

/** BMO Tune RT's DSP, whole: float in, float out, and a parameter struct. No framework, no host, no allocation after prepare(), and no
    dependence on how the host slices blocks -- every stage is a per-sample
    state machine, which the block-size invariance harness checks bit for
    bit (spec §8, T-1).
*/
class TuneCore
{
public:
    void prepare (double sampleRate, int maxBlockSize);
    void reset();
    void setParams (const TuneParams&) noexcept;

    /** Mono, in place. */
    void process (float* samples, int numSamples) noexcept;

    /** What to report to the host: always 0. Live only -- the plugin runs
        contract::kLiveRestMs behind at rest (4 ms since 2026-09-11) and up to
        a period further while correcting, and says 0, as Waves does
        (LatencyContract.h).

        A flat rest against competitors whose delay tracks the note: BMO is
        the least late of the three on a bass note and the latest on a high
        one, over Waves from about C3 upward -- 4.6 ms at A5 where Waves is
        0.7. testing-notes/tune-latency-review-2026-09-11.md. */
    static constexpr int kReportedLatency = 0;

    /** Offline tools only: called once per sample with that sample's frame.
        A plain function pointer, so installing one cannot allocate. */
    using AnalysisTap = void (*) (void* context, const AnalysisFrame&);
    void setAnalysisTap (AnalysisTap tap, void* context) noexcept { analysisTap = tap; analysisContext = context; }

    const Detector& detector() const noexcept { return det; }
    const CorrectionLaw& correction() const noexcept { return law; }
    const ClassicEngine& classic() const noexcept { return engine; }

private:
    void applyParams() noexcept;

    double fs = 48000.0;
    TuneParams params;
    bool paramsDirty = true;

    Detector det;
    CorrectionLaw law;
    ClassicEngine engine;

    long long samplePosition = 0;
    AnalysisTap analysisTap = nullptr;
    void* analysisContext = nullptr;
};

} // namespace bmo::tune
