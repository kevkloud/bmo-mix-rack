#pragma once

#include <algorithm>
#include <cmath>

namespace bmo::dsp
{

/** Turning a hard switch into a short fade.

    A switch that changes the signal path in one sample puts a step in the
    output wherever the two paths disagree, and the house rule is that a step
    which measures gets a fade, heard or not. The bound the repository holds
    a switch to is that the largest sample-to-sample step after it is less
    than 1.5 times the steady signal's own largest step. Three pieces, all
    allocation-free, lock-free and JUCE-free:

      - Ramp: a value that moves in a straight line to a new target over a
        fixed time, then holds that target exactly.
      - crossfade(): an equal-gain blend of an old path and a new one, which
        a Ramp from 0 to 1 drives.
      - Dip: a gain that fades to zero, waits there for the caller to make a
        change the audio cannot follow continuously (a latency change, a
        filter reset), and fades back up.

    Contract, which `tests/dsp/SwitchFadeTests.cpp` holds:

      - The length is a time, not a count: `prepare (rate, ms)` gives
        round (rate * ms / 1000) samples, never fewer than one, so the same
        fade takes the same time at every sample rate.
      - A move lands exactly: the sample at which it completes returns the
        target bit for bit, and so does every sample after it.
      - Re-targeting mid-move starts from the value in use, so a switch
        flicked back before its fade completes turns round without a step,
        and takes the full length again to arrive.
      - Idle is bit-exact. A Ramp that is not moving returns its value
        unchanged; crossfade() at 0 or 1 returns the selected input bit for
        bit, a -0.0 included, whatever the other input holds -- a NaN or an
        infinity on the path not in use does not reach the output; and a
        Dip at rest -- default-constructed, prepared or reset -- returns
        exactly 1.0f. So code that uses these while nothing is switching
        renders bit-identical to code that never had them. Callers that want
        to skip the work of the path not in use test isMoving().
      - A Ramp before prepare() rests at 0 with a length of one sample.

    Why straight lines rather than a curve: what the bound measures is the
    largest step, and for a given time a straight line has the smallest
    largest step of any monotone fade -- a raised cosine of the same length
    is pi/2 steeper in the middle. Why equal gain rather than equal power:
    the two sides of every switch here are the same signal through two
    paths, strongly correlated, and correlated signals sum in amplitude.
*/
class Ramp
{
public:
    /** Sets how long a move takes. Stops any move in progress at its target:
        prepare() is where a module's state starts over anyway. */
    void prepare (double sampleRate, double milliseconds) noexcept
    {
        length = std::max (1, (int) std::lround (std::max (sampleRate, 0.0) * milliseconds * 0.001));
        inverseLength = 1.0f / (float) length;
        snap (goal);
    }

    /** Jump to `v` and hold it. */
    void snap (float v) noexcept
    {
        from = current = goal = v;
        done = 0;
        moving = false;
    }

    /** Start moving towards `target` from the value in use. Asking for the
        target already being approached (or held) changes nothing, so this
        is safe to call every block with the control's current state. */
    void setTarget (float target) noexcept
    {
        // Exact comparison on purpose: a target is a control's position, and
        // anything else is a new move.
        if (! (target < goal) && ! (goal < target))
            return;

        if (! (target < current) && ! (current < target))
        {
            snap (target);
            return;
        }

        from   = current;
        goal   = target;
        done   = 0;
        moving = true;
    }

    /** One sample on; returns the value for that sample. */
    float next() noexcept
    {
        if (moving)
            step (1);

        return current;
    }

    /** `samples` on at once, for a value read at control rate. Returns the
        value at the last of them, so a sub-block of any length moves it by
        its own duration and the time a move takes does not depend on how a
        host splits its blocks. */
    float advance (int samples) noexcept
    {
        if (moving && samples > 0)
            step (samples);

        return current;
    }

    float value() const noexcept           { return current; }
    float target() const noexcept          { return goal; }
    bool  isMoving() const noexcept        { return moving; }
    int   lengthInSamples() const noexcept { return length; }

private:
    void step (int samples) noexcept
    {
        done += samples;

        if (done >= length)
        {
            current = goal;
            moving  = false;
        }
        else
        {
            current = from + (goal - from) * ((float) done * inverseLength);
        }
    }

    float from = 0.0f, current = 0.0f, goal = 0.0f;
    float inverseLength = 1.0f;
    int   length = 1, done = 0;
    bool  moving = false;
};

//==============================================================================
/** Equal-gain blend: `oldPath` at position 0, `newPath` at 1. At either end
    it returns that input itself, bit for bit, and never reads the other:
    a sum of products would turn a -0.0 into +0.0, and a NaN or an infinity
    on the path not in use into a NaN on the one that is. */
inline float crossfade (float oldPath, float newPath, float position) noexcept
{
    if (! (position > 0.0f))
        return oldPath;

    if (! (position < 1.0f))
        return newPath;

    return oldPath * (1.0f - position) + newPath * position;
}

//==============================================================================
/** A gain that dips to zero around a change the signal cannot pass through
    continuously, such as a change of latency, where no blend of before and
    after exists because the two are not aligned in time.

        if (dip.ready()) { make the change; dip.changed(); }
        out *= dip.next();

    request() starts the fade down from wherever the gain is; ready() is true
    from the first sample after it reaches exactly zero; changed() starts the
    fade back up. One sample sits at zero between them. cancel() withdraws a
    request that has not been acted on and fades up from where the gain is.
*/
class Dip
{
public:
    /** At rest at exactly 1 from construction, as after reset(): a Dip used
        before prepare() passes the signal rather than silencing it. */
    Dip() noexcept { reset(); }

    void prepare (double sampleRate, double milliseconds) noexcept
    {
        ramp.prepare (sampleRate, milliseconds);
        reset();
    }

    /** At rest: gain exactly 1, nothing pending. */
    void reset() noexcept
    {
        ramp.snap (1.0f);
        pending = false;
    }

    void request() noexcept
    {
        pending = true;
        ramp.setTarget (0.0f);
    }

    void cancel() noexcept
    {
        pending = false;
        ramp.setTarget (1.0f);
    }

    bool ready() const noexcept   { return pending && ! ramp.isMoving() && ramp.value() == 0.0f; }

    void changed() noexcept
    {
        pending = false;
        ramp.setTarget (1.0f);
    }

    float next() noexcept         { return ramp.next(); }
    float value() const noexcept  { return ramp.value(); }
    bool  isPending() const noexcept { return pending; }

    /** Neither pending nor moving: the gain is exactly 1. */
    bool  isIdle() const noexcept { return ! pending && ! ramp.isMoving(); }

private:
    Ramp ramp;
    bool pending = false;
};

} // namespace bmo::dsp
