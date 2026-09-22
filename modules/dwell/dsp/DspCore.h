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

    **Stage 2 builds one delay engine and instantiates it twice** -- the main
    delay and the lane -- rather than one bespoke dual engine with the lane
    written into it (DECIDED, Frosty 2026-09-22, with the voicing cut that made
    it obvious). The two engines now take the *same* character, stereo mode,
    cuts, modulation and drive and differ only in their time, their gain law
    and their FX stage, which is exactly the shape a reusable engine has: one
    class, a `Params` of its own, two members here. Written the other way the
    lane would be a set of branches threaded through the main loop, and pulling
    it apart later -- if Frosty ever wants the throw lane as its own product,
    or the main delay without one -- would be a rewrite rather than a
    deletion. This is the framing to build against, not a preference.

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
        schema's own order. `DwellDsp::setParams` fills it.

        **Two engines, one voicing** from 2026-09-22: the lane reads
        `characterChoice`, `stereoChoice`, both cuts, both modulation values
        and `drivePct` from the fields below rather than from lane copies of
        them, because there are no lane copies any more. `duckDb` is the one
        main-delay value it does *not* read -- the ducker pushes the main wet
        out of the way of the dry, and the lane's job is to be heard.

        Each field's initialiser is its spec default, so a core that has never
        been handed a parameter array is still the module at its defaults. */
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
        float modRateHz     = 0.6f;
        float modDepthPct   = 0.0f;
        float drivePct      = 0.0f;
        float duckDb        = 0.0f;
        float mixPct        = 35.0f;

        /** The lane's gates and its tail. `laneGain` is bipolar: below 0 the
            lane decays, at 0 it holds at exact unity, above it builds. */
        bool  sendHeld      = false;
        float laneGain      = -40.0f;
        bool  hold          = false;
        bool  chop          = false;

        bool  fx            = false;
        int   fxTypeChoice  = 0;
        float fxAmountPct   = 35.0f;

        /** What the lane declares for itself: how loud it is against the main
            delay's wet, how long its own repeat is, and its own FX stage.
            Everything else it needs is above -- one set of voicing values, two
            engines reading them. */
        float laneLevelDb       = 0.0f;
        float laneTimeMs        = 250.0f;
        bool  laneFx            = false;
        int   laneFxTypeChoice  = 0;
        float laneFxAmountPct   = 35.0f;

        /** Whether the lane's FX trio follows the main delay's. Carried here
            whatever it says -- a value that stops arriving is a value the lane
            could not go back to -- and it is the later stage that acts on it.
            Defaults on: a fresh instance is one delay with one set of
            controls. */
        bool  fxLink            = true;
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
