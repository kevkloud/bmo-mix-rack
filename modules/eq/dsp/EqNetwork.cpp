#include "EqNetwork.h"
#include <algorithm>

namespace bmo::eq
{

namespace
{
    float dbToGain (float db) noexcept
    {
        return std::pow (10.0f, db * 0.05f);
    }

    /** Keep every corner comfortably inside the Nyquist limit; tan() blows up
        as the argument approaches pi/2. */
    double clampCutoff (double hz, double sampleRate) noexcept
    {
        return std::clamp (hz, 5.0, sampleRate * 0.49);
    }
}

//==============================================================================
void EqNetwork::prepare (double newSampleRate) noexcept
{
    sampleRate = newSampleRate > 0.0 ? newSampleRate : 44100.0;
    reset();
}

void EqNetwork::reset() noexcept
{
    lowBranch.reset();
    midBranch.reset();
    highBranch.reset();

    hpf1.reset();
    hpf2.reset();
    lpf1.reset();
    lpf2.reset();
}

//==============================================================================
void EqNetwork::setSettings (const EqSettings& s) noexcept
{
    lowBranch .setCutoff (clampCutoff (s.lfFreqHz, sampleRate), sampleRate);
    highBranch.setCutoff (clampCutoff (s.hfFreqHz, sampleRate), sampleRate);
    const auto midQ = s.midQ > 0.0f
                        ? s.midQ
                        : midBranchQ (s.midFreqHz, s.midHiQ);

    midBranch .setCutoff (clampCutoff (s.midFreqHz, sampleRate), midQ, sampleRate);

    const float gains[kNumBands] { dbToGain (s.lfGainDb),
                                   dbToGain (s.midGainDb),
                                   dbToGain (s.hfGainDb) };

    for (int i = 0; i < kNumBands; ++i)
    {
        gain[(size_t) i]      = gains[i];
        gainRecip[(size_t) i] = 1.0f / gains[i];
    }

    // A cut that is off is not run, so its state is whatever the signal left
    // in it when it went off. Coming back on, that state would be played out
    // as a burst of the old signal -- -6.5 dBFS from silence, measured -- so a
    // cut that was off starts again from rest, as one never used does.
    const auto hpfWasActive = hpfActive;
    const auto lpfWasActive = lpfActive;

    hpfActive = s.hpfFreqHz > 0.0f;

    if (hpfActive && ! hpfWasActive)
    {
        hpf1.reset();
        hpf2.reset();
    }

    if (hpfActive)
    {
        // 18 dB/octave: a real pole plus a resonant complex pair. The Q on the
        // second-order part is what gives the passive LC filter its small bump
        // just above the corner.
        const auto hz = clampCutoff (s.hpfFreqHz, sampleRate);
        hpf1.setCutoff (hz, sampleRate);
        hpf2.setCutoff (hz, kHpfQ, sampleRate);
    }

    lpfActive = s.lpfFreqHz > 0.0f;

    if (lpfActive && ! lpfWasActive)
    {
        lpf1.reset();
        lpf2.reset();
    }

    if (lpfActive)
    {
        // 18 dB/octave, as the manual specifies: a real pole plus a resonant
        // pair, the mirror of the high-pass.
        const auto hz = clampCutoff (s.lpfFreqHz, sampleRate);
        lpf1.setCutoff (hz, sampleRate);
        lpf2.setCutoff (hz, kLpfQ, sampleRate);
    }
}

//==============================================================================
float EqNetwork::processSample (float x) noexcept
{
    if (hpfActive)
        x = hpf2.processHighpass (hpf1.processHighpass (x));

    // Resolve the shared feedback loop. Each branch reports its response as
    // b_i = d_i * u + v_i, so u falls out in closed form.
    float d[kNumBands], v[kNumBands];

    lowBranch .analyse (ShelfBranch::Type::low,  d[low],  v[low]);
    midBranch .analyse (Svf::Output::bandpass,   d[mid],  v[mid]);
    highBranch.analyse (ShelfBranch::Type::high, d[high], v[high]);

    float denom    = 1.0f;
    float stateSum = 0.0f;

    for (int i = 0; i < kNumBands; ++i)
    {
        denom    += gainRecip[(size_t) i] * d[i];
        stateSum += gainRecip[(size_t) i] * v[i];
    }

    const auto u = (x - stateSum) / denom;

    float y = u;

    for (int i = 0; i < kNumBands; ++i)
        y += gain[(size_t) i] * (d[i] * u + v[i]);

    lowBranch .update (u);
    midBranch .update (u);
    highBranch.update (u);

    if (lpfActive)
        y = lpf2.processLowpass (lpf1.process (OnePole::Output::lowpass, y));

    return y;
}

//==============================================================================
std::complex<double> EqNetwork::responseAt (double frequencyHz) const noexcept
{
    const std::complex<double> branch[kNumBands] {
        lowBranch .responseAt (ShelfBranch::Type::low,  frequencyHz, sampleRate),
        midBranch .responseAt (Svf::Output::bandpass,   frequencyHz, sampleRate),
        highBranch.responseAt (ShelfBranch::Type::high, frequencyHz, sampleRate)
    };

    std::complex<double> numerator { 1.0, 0.0 };
    std::complex<double> denominator { 1.0, 0.0 };

    for (int i = 0; i < kNumBands; ++i)
    {
        numerator   += (double) gain[(size_t) i]      * branch[i];
        denominator += (double) gainRecip[(size_t) i] * branch[i];
    }

    auto h = numerator / denominator;

    if (hpfActive)
        h *= hpf1.responseAt (OnePole::Output::highpass, frequencyHz, sampleRate)
           * hpf2.responseAt (Svf::Output::highpass, frequencyHz, sampleRate);

    if (lpfActive)
        h *= lpf1.responseAt (OnePole::Output::lowpass, frequencyHz, sampleRate)
           * lpf2.responseAt (Svf::Output::lowpass,     frequencyHz, sampleRate);

    return h;
}

double EqNetwork::magnitudeDbAt (double frequencyHz) const noexcept
{
    const auto m = std::abs (responseAt (frequencyHz));
    return 20.0 * std::log10 (std::max (m, 1.0e-9));
}

double EqNetwork::broadbandGain() const noexcept
{
    // Log-spaced mean across the audible band. Crude by design: it only has to
    // make an A/B level-matched, not to be perceptually weighted.
    constexpr int    kPoints = 48;
    constexpr double kLo = 20.0, kHi = 20000.0;

    double sum = 0.0;

    for (int i = 0; i < kPoints; ++i)
    {
        const auto t  = (double) i / (double) (kPoints - 1);
        const auto hz = kLo * std::pow (kHi / kLo, t);
        sum += std::abs (responseAt (hz));
    }

    const auto mean = sum / (double) kPoints;
    return mean > 1.0e-6 ? mean : 1.0;
}

} // namespace bmo::eq
