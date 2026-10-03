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

    hpfMix.prepare (sampleRate, kSwitchFadeMs);
    lpfMix.prepare (sampleRate, kSwitchFadeMs);
    takeSwitchesAsFound = true;

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

    // A fade in progress was carrying state that is now gone.
    hpfMix.snap (hpfActive ? 1.0f : 0.0f);
    lpfMix.snap (lpfActive ? 1.0f : 0.0f);

    // Likewise a gain ramp: the gains are where they were going.
    gain = gainTarget;
    gainRecip = gainRecipTarget;
    gainRampLeft = 0;
}

//==============================================================================
void EqNetwork::setSettings (const EqSettings& s, int gainRampSamples) noexcept
{
    const auto firstSettings = takeSwitchesAsFound;

    lowBranch .setCutoff (clampCutoff (s.lfFreqHz, sampleRate), sampleRate);
    highBranch.setCutoff (clampCutoff (s.hfFreqHz, sampleRate), sampleRate);
    const auto midQ = s.midQ > 0.0f
                        ? s.midQ
                        : midBranchQ (s.midFreqHz, s.midHiQ);

    midBranch .setCutoff (clampCutoff (s.midFreqHz, sampleRate), midQ, sampleRate);

    const float gains[kNumBands] { dbToGain (s.lfGainDb),
                                   dbToGain (s.midGainDb),
                                   dbToGain (s.hfGainDb) };

    // The band gains move a step per sample to their new values rather than
    // all at once. They sit inside the feedback loop, whose branch states
    // hold the signal at the gains it was shaped by, and a smoothed move
    // handed over once per 32-sample control period arrived as a staircase
    // of jumps against that state: LF -16 -> +16 dB under a 35 Hz tone
    // stepped 5.2x the tone's own largest step, and back down 22x. Linear in
    // g and in 1/g, between control points 32 host samples apart, so the
    // loop's denominator stays above 1; the last sample lands on exactly the
    // values the settings ask for.
    bool gainsMove = false;

    for (int i = 0; i < kNumBands; ++i)
    {
        gainTarget[(size_t) i]      = gains[i];
        gainRecipTarget[(size_t) i] = 1.0f / gains[i];
        gainsMove = gainsMove || ! (gain[(size_t) i] == gainTarget[(size_t) i]);
    }

    if (gainsMove && gainRampSamples > 1 && ! firstSettings)
    {
        const auto inverse = 1.0f / (float) gainRampSamples;

        for (size_t i = 0; i < (size_t) kNumBands; ++i)
        {
            gainStep[i]      = (gainTarget[i] - gain[i]) * inverse;
            gainRecipStep[i] = (gainRecipTarget[i] - gainRecip[i]) * inverse;
        }

        gainRampLeft = gainRampSamples;
    }
    else
    {
        gain = gainTarget;
        gainRecip = gainRecipTarget;
        gainRampLeft = 0;
    }

    // A cut switched on or off crosses over between the unfiltered signal and
    // the filtered one rather than stepping between them: a 100 Hz tone
    // through Low Cut 360, switched off, stepped 62-74 times the tone's own
    // largest step. The filter runs until its fade out is complete, then
    // stops. A cut that has stopped holds whatever the signal left in it,
    // and coming back on would play that out -- -6.5 dBFS from silence,
    // measured -- so a cut coming on from fully off starts from rest, as one
    // never used does. One coming back during its own fade out has state
    // that is still live, and keeps it.
    const auto hpfOn = s.hpfFreqHz > 0.0f;
    const auto lpfOn = s.lpfFreqHz > 0.0f;

    if (takeSwitchesAsFound)
    {
        hpfMix.snap (hpfOn ? 1.0f : 0.0f);
        lpfMix.snap (lpfOn ? 1.0f : 0.0f);
        takeSwitchesAsFound = false;
    }

    if (hpfOn && ! hpfMix.isMoving() && hpfMix.value() == 0.0f)
    {
        hpf1.reset();
        hpf2.reset();
    }

    if (lpfOn && ! lpfMix.isMoving() && lpfMix.value() == 0.0f)
    {
        lpf1.reset();
        lpf2.reset();
    }

    hpfMix.setTarget (hpfOn ? 1.0f : 0.0f);
    lpfMix.setTarget (lpfOn ? 1.0f : 0.0f);

    hpfActive = hpfOn;
    lpfActive = lpfOn;

    // Switching off leaves the coefficients alone, so the fade out runs the
    // filter that was in.
    if (hpfActive)
    {
        // 18 dB/octave: a real pole plus a resonant complex pair. The Q on the
        // second-order part is what gives the passive LC filter its small bump
        // just above the corner.
        const auto hz = clampCutoff (s.hpfFreqHz, sampleRate);
        hpf1.setCutoff (hz, sampleRate);
        hpf2.setCutoff (hz, kHpfQ, sampleRate);
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
    if (hpfMix.isMoving())
    {
        const auto filtered = hpf2.processHighpass (hpf1.processHighpass (x));
        x = bmo::dsp::crossfade (x, filtered, hpfMix.next());
    }
    else if (hpfActive)
    {
        x = hpf2.processHighpass (hpf1.processHighpass (x));
    }

    if (gainRampLeft > 0)
    {
        if (--gainRampLeft == 0)
        {
            gain = gainTarget;
            gainRecip = gainRecipTarget;
        }
        else
        {
            for (size_t i = 0; i < (size_t) kNumBands; ++i)
            {
                gain[i]      += gainStep[i];
                gainRecip[i] += gainRecipStep[i];
            }
        }
    }

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

    if (lpfMix.isMoving())
    {
        const auto filtered = lpf2.processLowpass (lpf1.process (OnePole::Output::lowpass, y));
        y = bmo::dsp::crossfade (y, filtered, lpfMix.next());
    }
    else if (lpfActive)
    {
        y = lpf2.processLowpass (lpf1.process (OnePole::Output::lowpass, y));
    }

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
        numerator   += (double) gainTarget[(size_t) i]      * branch[i];
        denominator += (double) gainRecipTarget[(size_t) i] * branch[i];
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
