// core/dsp/SwitchFade.h: the contract its header states, one section per
// clause. Every module that fades a switch relies on these, so they are
// held here once rather than re-derived in each module's tests.

#include "core/dsp/SwitchFade.h"

#include <cmath>
#include <cstring>
#include <iostream>
#include <limits>
#include <string>
#include <vector>

using bmo::dsp::Ramp;
using bmo::dsp::Dip;
using bmo::dsp::crossfade;

namespace
{
    int failures = 0;

    void check (bool ok, const std::string& what)
    {
        if (! ok) { std::cerr << "FAIL: " << what << '\n'; ++failures; }
    }

    bool sameBits (float a, float b)
    {
        return std::memcmp (&a, &b, sizeof (float)) == 0;
    }

    /** Samples from setTarget until the ramp stops moving. */
    int samplesToLand (Ramp& r, float target)
    {
        r.setTarget (target);
        int n = 0;
        while (r.isMoving() && n < 10000000) { r.next(); ++n; }
        return n;
    }
}

int main()
{
    //== 1. The length is a time: it scales with the sample rate =============
    {
        for (double fs : { 44100.0, 48000.0, 96000.0, 192000.0 })
            for (double ms : { 1.0, 5.0, 10.0 })
            {
                Ramp r;
                r.prepare (fs, ms);
                r.snap (0.0f);

                const auto expected = (int) std::lround (fs * ms / 1000.0);
                check (r.lengthInSamples() == expected,
                       "length at " + std::to_string ((int) fs) + " Hz for " + std::to_string (ms)
                           + " ms should be " + std::to_string (expected) + ", is "
                           + std::to_string (r.lengthInSamples()));
                check (samplesToLand (r, 1.0f) == expected,
                       "a move takes exactly its length at " + std::to_string ((int) fs) + " Hz");
            }

        Ramp tiny;
        tiny.prepare (44100.0, 0.0);
        check (tiny.lengthInSamples() == 1, "a zero-length fade still takes one sample");

        // advance() moves by a duration, so the same move takes the same
        // number of samples however it is split.
        for (int split : { 1, 7, 32, 441, 512 })
        {
            Ramp r;
            r.prepare (48000.0, 10.0);
            r.snap (0.0f);
            r.setTarget (1.0f);

            int n = 0;
            while (r.isMoving()) { r.advance (split); n += split; }

            check (n >= 480 && n - split < 480,
                   "advance() in steps of " + std::to_string (split) + " lands after 480 samples, not "
                       + std::to_string (n));
        }
    }

    //== 2. A move lands exactly on its target, without overshoot ============
    {
        for (float target : { 1.0f, 0.0f, -1.0f, 0.3f, 15.848932f, 1.0e-3f })
        {
            Ramp r;
            r.prepare (44100.0, 10.0);
            r.snap (0.7f);
            r.setTarget (target);

            const auto lo = std::min (0.7f, target), hi = std::max (0.7f, target);
            bool inside = true, monotone = true;
            float previous = 0.7f;

            for (int i = 0; i < r.lengthInSamples(); ++i)
            {
                const auto v = r.next();
                inside   = inside && v >= lo && v <= hi;
                monotone = monotone && (target > 0.7f ? v >= previous : v <= previous);
                previous = v;
            }

            check (! r.isMoving(), "the ramp stops after its length");
            check (sameBits (r.value(), target), "lands bit-exactly on " + std::to_string (target));
            check (inside && monotone, "moves monotonically between start and target towards "
                                           + std::to_string (target));

            for (int i = 0; i < 1000; ++i)
                inside = inside && sameBits (r.next(), target);

            check (inside, "holds the target bit-exactly once there");
        }
    }

    //== 3. Re-targeting mid-move starts from the value in use ===============
    {
        Ramp r;
        r.prepare (48000.0, 10.0);   // 480 samples
        r.snap (0.0f);
        r.setTarget (1.0f);

        for (int i = 0; i < 200; ++i)
            r.next();

        const auto inUse = r.value();
        r.setTarget (0.0f);

        check (sameBits (r.value(), inUse), "re-targeting does not move the value by itself");

        const auto first = r.next();
        check (first < inUse && inUse - first <= inUse / 480.0f * 1.001f,
               "the first sample after re-targeting is one step on from the value in use ("
                   + std::to_string (inUse) + " -> " + std::to_string (first) + ")");

        int n = 1;
        while (r.isMoving()) { r.next(); ++n; }
        check (n == 480, "a re-targeted move takes the full length again, took " + std::to_string (n));
        check (sameBits (r.value(), 0.0f), "and lands on the new target");

        // Asking again for the target being approached does not restart it.
        r.snap (0.0f);
        r.setTarget (1.0f);
        for (int i = 0; i < 100; ++i) r.next();
        r.setTarget (1.0f);
        n = 100;
        while (r.isMoving()) { r.next(); ++n; }
        check (n == 480, "setTarget with the same target every block does not restart the move");

        // Re-targeting to the value in use is a snap, not a zero-length move.
        r.snap (0.25f);
        r.setTarget (0.25f);
        check (! r.isMoving(), "a target equal to the value in use leaves the ramp idle");
    }

    //== 4. Idle is bit-exact pass-through ===================================
    {
        std::vector<float> values { 0.0f, -0.0f, 1.0f, -1.0f, 0.1234567f, -3.0e-39f, 1.0e-30f,
                                    std::numeric_limits<float>::denorm_min(), 123456.7f, -0.999999f };

        // At 0 and 1 the selected path comes back bit for bit -- a -0.0
        // included -- and whatever the other path holds, a NaN or an
        // infinity included: a fault on the path not in use must not reach
        // the one that is.
        values.push_back (std::numeric_limits<float>::quiet_NaN());
        values.push_back (std::numeric_limits<float>::infinity());
        values.push_back (-std::numeric_limits<float>::infinity());

        bool exact = true;
        for (float a : values)
            for (float b : values)
            {
                exact = exact && sameBits (crossfade (a, b, 1.0f), b);
                exact = exact && sameBits (crossfade (a, b, 0.0f), a);
            }
        check (exact, "crossfade at 0 and 1 returns the selected input bit for bit, whatever the other holds");

        Ramp r;
        r.prepare (96000.0, 10.0);
        r.snap (-1.0f);
        bool still = true;
        for (int i = 0; i < 100000; ++i)
            still = still && sameBits (r.next(), -1.0f) && ! r.isMoving();
        check (still, "an idle ramp returns its value unchanged");

        Dip d;
        d.prepare (48000.0, 10.0);
        bool unity = true;
        for (int i = 0; i < 100000; ++i)
            unity = unity && sameBits (d.next(), 1.0f) && d.isIdle();
        check (unity, "a Dip at rest is exactly 1, so multiplying by it changes nothing");

        for (float x : values)
            unity = unity && (std::isnan (x) || sameBits (x * d.value(), x));
        check (unity, "x times a resting Dip is x, bit for bit");

        // Before prepare(), and after reset(), a Dip is at rest at exactly
        // 1 too: the header promises the resting gain is 1, and a Dip that
        // rested at 0 would silence a module that used it before preparing.
        Dip fresh;
        check (fresh.isIdle() && sameBits (fresh.value(), 1.0f) && sameBits (fresh.next(), 1.0f),
               "a default-constructed Dip is idle at exactly 1");

        Dip used;
        used.prepare (48000.0, 10.0);
        used.request();
        for (int i = 0; i < 100; ++i) used.next();
        used.reset();
        check (used.isIdle() && sameBits (used.value(), 1.0f) && sameBits (used.next(), 1.0f),
               "a Dip reset mid-fade is idle at exactly 1");

        // A Ramp before prepare() rests at 0, idle, with a one-sample length.
        Ramp unprepared;
        check (! unprepared.isMoving() && sameBits (unprepared.value(), 0.0f)
                   && unprepared.lengthInSamples() == 1,
               "a default-constructed Ramp is idle at 0 with a length of one sample");
    }

    //== 5. Dip: down, one sample at zero, the change, and back up ==========
    {
        Dip d;
        d.prepare (48000.0, 10.0);
        d.request();

        int down = 0;
        float previous = 1.0f;
        bool falling = true;
        while (! d.ready() && down < 100000)
        {
            const auto g = d.next();
            falling = falling && g <= previous;
            previous = g;
            ++down;
        }

        check (down == 480, "a Dip reaches zero in its length, took " + std::to_string (down));
        check (falling && sameBits (d.value(), 0.0f), "falling monotonically to exactly zero");

        d.changed();
        check (! d.ready(), "ready() clears once the change is made");

        int up = 0;
        while (! d.isIdle() && up < 100000) { d.next(); ++up; }
        check (up == 480 && sameBits (d.value(), 1.0f), "and returns to exactly 1 in its length");

        // A request withdrawn halfway turns round from where the gain is.
        d.request();
        for (int i = 0; i < 240; ++i) d.next();
        const auto inUse = d.value();
        d.cancel();
        const auto first = d.next();
        check (first > inUse && first - inUse <= (1.0f - inUse) / 480.0f * 1.001f,
               "cancel() fades up from the gain in use");
        while (! d.isIdle()) d.next();
        check (sameBits (d.value(), 1.0f) && ! d.isPending(), "and comes to rest at 1");

        // Requesting again while already down is harmless.
        d.request();
        while (! d.ready()) d.next();
        d.request();
        check (d.ready(), "a second request at the bottom keeps it ready");
    }

    if (failures == 0)
        std::cout << "All switch fade tests passed.\n";

    return failures == 0 ? 0 : 1;
}
