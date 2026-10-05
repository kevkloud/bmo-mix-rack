#pragma once

#include <array>
#include <cmath>

namespace bmo::eq
{

// Only the Console EQ unit B is modelled. The Console EQ unit A was removed: it was the same equaliser
// with fewer options -- one high-shelf frequency instead of three, no switchable
// mid Q, no low-pass, and a different low-cut table -- so keeping it meant
// carrying a second set of tables and a branch through every filter for a
// strictly smaller feature set.
//
// Every frequency below is from the Console EQ unit A and B user manual, issue 5, and
// tests hold both the panel legends and the filter tuning to them.

//==============================================================================
// Frequency tables, in Hz.
//
// These are the nominal published switch positions. They are the *labels* and
// the starting point for the network model; the realised curve shapes (Q, shelf
// slope, the high-pass filter's resonant bump) come out of the LC network in
// EqNetwork, not from these numbers.
//
// Note the tables are indexed *positionally*. A frequency selector's automation
// value is the switch position, not the frequency, so switching models moves
// you to the corresponding detent on the other unit's table. That mapping is
// deterministic and round-trip-safe, which is what we want for saved sessions.
//==============================================================================

// High shelf: +/-16 dB shelving at 10, 12 or 16 kHz.
inline constexpr std::array<float, 3> kHighShelfFreqs { 10000.0f, 12000.0f, 16000.0f };

// Mid bell. Identical on both units.
inline constexpr std::array<float, 6> kMidFreqs { 360.0f, 700.0f, 1600.0f, 3200.0f, 4800.0f, 7200.0f };

// Low shelf. Identical on both units.
inline constexpr std::array<float, 4> kLowShelfFreqs { 35.0f, 60.0f, 110.0f, 220.0f };

// Low cut, 18 dB/octave. Retailers sometimes quote the Console EQ unit A's 50/80/160/300 for
// this module; the manual gives these, and the resistor modification it
// describes changes termination rather than frequency.
inline constexpr std::array<float, 4> kHpfFreqs { 45.0f, 70.0f, 160.0f, 360.0f };

// High cut, 18 dB/octave.
inline constexpr std::array<float, 5> kLpfFreqs { 6000.0f, 8000.0f, 10000.0f, 14000.0f, 18000.0f };

//==============================================================================
// Branch Q values.
//
// The structure of the EQ (see EqNetwork) follows the topology of the original:
// a set of resonant branches sharing one feedback path, which is what produces
// band interaction and proportional Q. The numbers below set what those
// branches do, and every one of them is now either fitted to a measurement or
// chosen for a stated reason -- they began as estimates, and the difference
// mattered: the mid band was up to 15.8 % off its marked frequency and the
// high-pass had a 0.87 dB resonance that does not exist.
//
// The measurements are traced from response plots of an assembled board
// published with the Nyan-1073-EQ hardware project (CC BY-SA 4.0), read by
// calibrating against the plot axes and following each curve by colour. The
// `measure` tool reproduces every comparison: `bell`, `fitq`, `shelf`, `hpf`.
//
// That board is one reference, not ground truth. Its shelves measure +18 to
// +21 dB where the unit is specified at +/-16, so where its figures conflict
// with the published specification the specification wins; this is noted at
// each point where it happens.
//==============================================================================

//------------------------------------------------------------------------------
// Mid band Q, per switch position, derived from the circuit rather than guessed.
//
// A series LC branch has Q = w0 * L / R, so Q tracks the product of centre
// frequency and inductance. The mid band does not switch these uniformly: the
// lower three positions switch both inductance and capacitance, using taps of
// 10 H, 7 H and 3 H, which holds Q roughly level; the upper three share a
// single 200 mH winding and switch capacitance alone, so there Q climbs in
// proportion to frequency. That asymmetry is why 360 Hz is broad and musical
// while 7.2 kHz is a focused presence peak -- behaviour a single Q constant
// cannot produce, and this model previously did not have.
//
// R is not published, so the absolute values come from measurement rather than
// from that reasoning: each figure below is solved so the realised -3 dB bell
// width at +18 dB matches a response plot traced from the Nyan-1073-EQ hardware
// project (CC BY-SA 4.0), which measured an assembled board. `measure fitq`
// reproduces the solve.
//
// The result supports the reasoning above without being derived from it: the
// lower three land roughly level at 1.39, 1.59 and 1.49, while the upper three
// climb steadily, 1.31 to 2.02 to 2.68. The circuit explains the shape; the
// measurement fixes the values. Deriving the values from w0 * L alone came out
// uniformly broad -- a realised Q of 0.83 at 360 Hz against a measured 1.24.
//------------------------------------------------------------------------------

inline constexpr std::array<float, 6> kMidBranchQ { 1.39f, 1.59f, 1.49f, 1.31f, 2.02f, 2.68f };

/** The Console EQ unit B's Hi-Q switch narrows the mid band; it scales whatever the
    position's Q already is rather than replacing it.

    The factor is not published. Every source agrees on the direction -- Hi-Q
    is the narrow setting, the other is wide and musical -- but none gives a
    number, and no measurement of a Console EQ unit B is to hand; the plots the rest of
    these constants are fitted to are of a Console EQ unit A, which has no such switch.
    Doubling is the conventional reading of a two-position narrow/wide switch.

    The manual also specifies the Console EQ unit B's mid as "smooth +/-12dB or +/-18dB
    peaking with switchable 'High Q'" without saying which range goes with
    which Q, and that is not modelled: the gain stays +/-18 either way. Guessing
    would mean a knob that reads +18 while producing +12, which is worse than
    the omission, and the leading commercial emulation of this module documents
    Hi-Q purely as a bandwidth change. */
inline constexpr float kMidHiQFactor = 2.0f;

// The high-pass is third order: one real pole plus a complex pair. A pair at
// Q = 1.0 alongside a coincident real pole is third-order Butterworth, which is
// maximally flat. That is what the hardware measures -- response plots of an
// assembled board show no peak whatever, worst case +0.0 dB. An earlier guess
// of 1.30 here invented a 0.87 dB resonance that is not there.
inline constexpr float kHpfQ       = 1.00f;

// The Console EQ unit B's low-pass is third order too. The user manual gives it the same
// 18 dB per octave as the high-pass, switchable between 6, 8, 10, 14 and
// 18 kHz -- it was implemented here as a second-order 12 dB/octave section
// until that was checked. Butterworth again: no measurement of this filter is
// to hand, and the high-pass, which is measurable, turned out flat.
inline constexpr float kLpfQ       = 1.00f;

//==============================================================================
inline constexpr float highShelfFreqHz (int position) noexcept
{
    const auto i = (position < 0 ? 0 : (position > 2 ? 2 : position));
    return kHighShelfFreqs[(size_t) i];
}

inline constexpr float midFreqHz (int position) noexcept
{
    const auto i = (position < 0 ? 0 : (position > 5 ? 5 : position));
    return kMidFreqs[(size_t) i];
}

inline constexpr float lowShelfFreqHz (int position) noexcept
{
    const auto i = (position < 0 ? 0 : (position > 3 ? 3 : position));
    return kLowShelfFreqs[(size_t) i];
}

/** Mid Q as a function of centre frequency, interpolated in log frequency
    between the switch positions.

    Taking frequency rather than an index means a selector gliding between two
    detents carries its Q along with it, and the curve display and the audio
    path derive it the same way from the same value. */
inline float midBranchQ (float hz, bool hiQ) noexcept
{
    const auto f = hz <= 0.0f ? kMidFreqs[0] : hz;

    float q = kMidBranchQ[0];

    if (f >= kMidFreqs[5])
    {
        q = kMidBranchQ[5];
    }
    else if (f > kMidFreqs[0])
    {
        for (size_t i = 0; i + 1 < kMidFreqs.size(); ++i)
        {
            if (f <= kMidFreqs[i + 1])
            {
                const auto t = (std::log2 (f) - std::log2 (kMidFreqs[i]))
                             / (std::log2 (kMidFreqs[i + 1]) - std::log2 (kMidFreqs[i]));

                q = kMidBranchQ[i] + t * (kMidBranchQ[i + 1] - kMidBranchQ[i]);
                break;
            }
        }
    }

    return hiQ ? q * kMidHiQFactor : q;
}

/** High cut. Position 0 is Off, hence the -1. Returns 0 for Off. */
inline constexpr float lpfFreqHz (int position) noexcept
{
    if (position <= 0)
        return 0.0f;

    const auto i = (position > 5 ? 5 : position) - 1;
    return kLpfFreqs[(size_t) i];
}

/** Low cut. Position 0 is Off, hence the -1. Returns 0 for Off. */
inline constexpr float hpfFreqHz (int position) noexcept
{
    if (position <= 0)
        return 0.0f;

    const auto i = (position > 4 ? 4 : position) - 1;
    return kHpfFreqs[(size_t) i];
}


} // namespace bmo::eq
