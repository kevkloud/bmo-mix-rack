#pragma once

#include "modules/dwell/params.h"

#include <algorithm>
#include <cmath>

namespace bmo::dwell
{

//==============================================================================
/** BMO Dwell's audio core.

    **This is stage 1: the plumbing, not the delay.** Every parameter is
    carried in real units from the schema to this struct, the sample rate and
    the channel count are taken at `prepare`, and `process` passes the buffer
    through untouched. The ring, the character chain, the feedback matrix and
    the FX stage are stage 2, per docs/delay/10-dsp-spec.md.

    It exists in this state on purpose rather than as a stub: the schema is
    permanent from this release, so the wiring from spec index to named value
    is pinned by tests now, before any DSP can be written against a mis-read
    lane. `DwellDspTests` checks the mapping value by value.

    **Latency is 0 at every setting, now and after stage 2.** There is no
    oversampling (docs/delay/10 §0 drops it), no lookahead and therefore no dry
    compensation ring. The wet delay time is *not* latency and is never
    reported as such -- the host is not being asked to shift anything, the same
    way BMO Dimension's detune voices are not reported. If the half-band
    fallback in 10 §4 is ever added, its group delay is a whole number of
    samples and comes off D, so this line does not change either.
*/
class DspCore
{
public:
    /** Every parameter in the real units the panel and the host show, in the
        order docs/delay/11 §3 fixes. `DwellDsp::setParams` fills it. */
    struct Params
    {
        float timeMs        = 375.0f;
        bool  sync          = false;    ///< read only once 12's tempo plumbing lands
        int   noteChoice    = kDefaultNote;
        float feedbackPct   = 35.0f;
        int   characterChoice = 0;
        int   stereoChoice  = 0;
        float lowCutHz      = 20.0f;
        float highCutHz     = 20000.0f;
        float voicePct      = 0.0f;
        float modRateHz     = 0.6f;
        float modDepthPct   = 0.0f;
        float drivePct      = 0.0f;
        float duckDb        = 4.0f;
        float mixPct        = 35.0f;
        bool  throwHeld     = false;
        int   throwModeChoice = 0;
        bool  freeze        = false;
        bool  fx            = false;
        int   fxTypeChoice  = 0;
        float fxAmountPct   = 35.0f;
    };

    void prepare (double newSampleRate, int newMaxBlockSize, int newNumChannels) noexcept
    {
        sampleRate   = newSampleRate > 0.0 ? newSampleRate : 48000.0;
        maxBlockSize = std::max (newMaxBlockSize, 1);
        numChannels  = std::max (newNumChannels, 1);

        // Stage 2 allocates the ring here, from kMaxTimeMs and never from a
        // parameter, so nothing is allocated on the audio thread. Nothing to
        // allocate yet.
        reset();
    }

    void reset() noexcept {}

    void setParams (const Params& p) noexcept { params = p; }

    const Params& getParams() const noexcept { return params; }

    /** Pass-through, bit-exact. Stage 1 has no loop to run and no dry gain to
        apply, and the dry path's MIX law (10 §9) is bit-exact unity below
        50 % anyway -- so the null test that stage 2 has to pass already
        passes, and it is registered now rather than written later. */
    void process (float* const*, int, int) noexcept {}

    double getSampleRate() const noexcept { return sampleRate; }
    int getMaxBlockSize() const noexcept  { return maxBlockSize; }
    int getNumChannels() const noexcept   { return numChannels; }

    /** The longest delay the ring is sized for, in samples at the prepared
        rate. Stage 2 allocates the next power of two at or above this. */
    int maxDelaySamples() const noexcept
    {
        return (int) std::ceil ((double) kMaxTimeMs * 0.001 * sampleRate);
    }

private:
    Params params;

    double sampleRate  = 48000.0;
    int maxBlockSize   = 512;
    int numChannels    = 2;
};

} // namespace bmo::dwell
