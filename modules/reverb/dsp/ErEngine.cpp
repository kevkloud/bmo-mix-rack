#include "modules/reverb/dsp/ErEngine.h"

#include <algorithm>
#include <climits>
#include <cmath>
#include <cstdint>

namespace bmo::reverb
{
namespace
{
    constexpr double kPi = 3.14159265358979323846;

    /** The diffuser's delays, in milliseconds: 20.3 ms of spread at most.

        Chosen by search, against two conditions that are the table's business
        not at all. **Only one stage is ever partly in** -- stage s fades while
        every earlier one is fully in and every later one fully out -- so the
        paths a pulse can take that coexist are the earlier stages' sums with
        and without stage s's four delays: 5, then 20, then 80 of them. First,
        those land on different samples at every rate from 44.1 to 192 kHz,
        which `tests/dsp/ReverbDspTests.cpp` asserts; two paths on one sample
        add or cancel instead of sitting side by side. Second, of the sets
        that pass, the one whose neighbouring paths sit furthest apart, scored
        as the summed squared overlap of the darkest band's one-pole (a 0.1 ms
        time constant) -- two paths a sample apart are one pulse to a 3 kHz
        filter.

        **The diffuser holds the ER level only on average, and that is a
        property of the construction, not of these numbers.** The butterfly
        preserves the energy of all four lines together; a channel's output
        is one line, and a one-in, one-out filter that preserves the energy of
        every input is an allpass -- which a feed-forward network cannot be
        and 10 section 1 forbids the recursive kind. So as DENSITY opens the
        diffuser, the level moves by however the table's own tap pattern
        happens to interfere with these 64 paths: a few tenths of a decibel
        on the stand-in table, see the testing note. A longer spread lowers it
        slowly; 25.7 ms was measured and did not settle it either.

        CALIBRATE: nothing here has been heard. */
    constexpr float kDiffuserMs[ErEngine::kDiffuserStages][ErEngine::kDiffuserWidth]
    {
        { 0.33f, 1.02f, 1.69f, 2.31f },
        { 3.84f, 5.58f, 6.03f, 6.46f },
        { 6.26f, 6.59f, 11.39f, 11.50f },
    };

    int nextPowerOfTwo (int n) noexcept
    {
        int p = 1;
        while (p < n && p < (1 << 30))
            p <<= 1;
        return p;
    }

    /** A small deterministic generator for Energy mode's velvet noise. The
        seed is a function of the table's own seed, the type, the variation
        and the channel and of nothing else, so a given setting produces the
        same pulses in every run, at every block size, on every machine. */
    struct Rng
    {
        std::uint32_t s;

        float next01() noexcept
        {
            s ^= s << 13;
            s ^= s >> 17;
            s ^= s << 5;
            return (float) (s >> 8) * (1.0f / 16777216.0f);
        }
    };

    /** 10 section 3's Energy envelope: a rise (t / tau_r)^p to a plateau,
        then an exponential handover. How sigma and p divide the time between
        the three is **CALIBRATE and unheard** -- 02's end-stops are an
        unconfirmed unknown -- so this is only the documented behaviour made
        concrete: at p = 0 the cluster starts at full level and decays over
        sigma ("builds explosively and decays quickly"); as p rises the build
        slows and the plateau lengthens toward sigma ("builds slowly and
        sustains for the time Spread sets"). The decay is 60 dB over sigma. */
    float envelope (float tMs, float spreadMs, float p) noexcept
    {
        const auto sigma = std::max (spreadMs, 1.0f);
        const auto shape = std::clamp (p, 0.0f, 3.0f);
        const auto rise  = 0.5f * sigma * shape / 3.0f;
        const auto hold  = 0.5f * sigma * shape / 3.0f;
        const auto tc    = sigma / 6.9077553f;   // ln (1000): 60 dB over sigma

        if (tMs < 0.0f)
            return 0.0f;

        if (tMs < rise)
            return std::pow (tMs / rise, shape);

        if (tMs < rise + hold)
            return 1.0f;

        return std::exp (-(tMs - rise - hold) / tc);
    }

    float densityWeight (float theta, float d, float ramp) noexcept
    {
        // The core taps' threshold is exactly 0 and **they never switch off**
        // (ErTable.h, kErCoreTaps) -- which the literal ramp would contradict
        // at D = 0, where (0 - 0) / ramp is 0. So a threshold at or below zero
        // is simply on, and every other tap follows 10 section 3's ramp.
        if (theta <= 0.0f)
            return 1.0f;

        return std::clamp ((d - theta) / ramp, 0.0f, 1.0f);
    }

    int roundToSamples (float ms, double sampleRate) noexcept
    {
        return (int) std::lround ((double) ms * sampleRate * 0.001);
    }
}

//==============================================================================
float ErEngine::sizeScale (const ErTable& table, float sizeM) noexcept
{
    const auto window = std::max (table.windowMs, 1.0e-3f);
    const auto lo = kWindowFloorMs / window;
    const auto hi = std::max (lo, table.windowClampMs / window);

    return std::clamp (sizeM / kReferenceSizeM, lo, hi);
}

float ErEngine::endTaper (float tMs, float windowEndMs) noexcept
{
    const auto start = windowEndMs - kTaperMs;

    if (tMs <= start)
        return 1.0f;

    if (tMs >= windowEndMs)
        return 0.0f;

    return 0.5f * (1.0f + (float) std::cos (kPi * (double) (tMs - start) / (double) kTaperMs));
}

float ErEngine::onePoleCoefficient (double hz, double sampleRate) noexcept
{
    if (sampleRate <= 0.0)
        return 0.0f;

    // |H(w)|^2 = (1 - a)^2 / (1 - 2 a cos w + a^2) = 1/2 solves to
    // a = c - sqrt(c^2 - 1) with c = 2 - cos w. Exact at any corner up to
    // Nyquist, where it is still -3 dB.
    const auto w = std::clamp (2.0 * kPi * hz / sampleRate, 1.0e-6, kPi);
    const auto c = 2.0 - std::cos (w);

    return (float) (c - std::sqrt (c * c - 1.0));
}

float ErEngine::hiCutCoefficient (double hz, double sampleRate) noexcept
{
    // Walked to a wire at the top of the range: (f / f_open)^32 is under
    // 0.1 % at 16 kHz and exactly 1 at 20 kHz, so the pole is the solved one
    // everywhere the corner is audible and exactly zero where the control
    // says "open".
    const auto f = std::clamp (hz, 20.0, (double) kHiCutOpenHz);
    const auto open = std::pow (f / (double) kHiCutOpenHz, 32.0);

    return (float) ((double) onePoleCoefficient (f, sampleRate) * (1.0 - open));
}

int ErEngine::diffuserDelaySamples (int stage, int lineIndex, double sampleRate) noexcept
{
    const auto s = std::clamp (stage, 0, kDiffuserStages - 1);
    const auto l = std::clamp (lineIndex, 0, kDiffuserWidth - 1);

    return std::max (1, roundToSamples (kDiffuserMs[s][l], sampleRate));
}

float ErEngine::diffuserSpreadMs() noexcept
{
    float total = 0.0f;

    for (const auto& stage : kDiffuserMs)
        total += *std::max_element (std::begin (stage), std::end (stage));

    return total;
}

float ErEngine::diffuserStageWeight (int stage, float d) noexcept
{
    const auto span = (1.0f - kDiffuserStartDensity) / (float) kDiffuserStages;
    const auto from = kDiffuserStartDensity + span * (float) stage;

    return std::clamp ((d - from) / span, 0.0f, 1.0f);
}

//==============================================================================
void ErEngine::prepare (double newSampleRate, float newRampWidth, float crossfadeMs, float smoothingMs)
{
    sampleRate = newSampleRate > 0.0 ? newSampleRate : 48000.0;
    rampWidth  = std::max (newRampWidth, 1.0e-4f);

    crossfadeSamples = std::max (2, roundToSamples (crossfadeMs, sampleRate));
    crossfadeSamples += crossfadeSamples & 1;   // even, so the dip has a sample at its minimum

    // One pole, 99 % of the way in `smoothingMs`.
    const auto tau = std::max (1.0e-4, (double) smoothingMs * 0.001 / std::log (100.0));
    smoothCoeff = (float) (1.0 - std::exp (-1.0 / (tau * sampleRate)));

    // The delay line covers the longest reach of any table at its largest
    // clamped size, and Energy mode's window. Variation 6 reaches no further:
    // it is the mono set put on the side, not a delayed copy of it. Sized from the tables themselves, so a regenerated table that
    // reaches further is covered without anyone editing this.
    float maxMs = 0.0f;

    for (int type = 0; type < 6; ++type)
    {
        const auto& t = erTableFor (type);
        const auto window = std::max (t.windowMs, 1.0e-3f);
        const auto kHi = std::max (kWindowFloorMs / window, t.windowClampMs / window);

        float reach = window;

        for (const auto& v : t.variation)
            for (const auto* c : { &v.left, &v.right })
                for (int i = 0; i < std::clamp (c->numTaps, 0, kErMaxTaps); ++i)
                    reach = std::max (reach, c->taps[i].timeMs);

        maxMs = std::max (maxMs, reach * kHi);
    }

    // A bogus table must not become a gigabyte; two seconds is ten times any
    // window 10 section 3 allows.
    maxMs = std::min (maxMs, 2000.0f);

    const auto size = nextPowerOfTwo (roundToSamples (maxMs, sampleRate) + 4);
    line.assign ((size_t) size, 0.0f);
    mask = size - 1;

    for (auto& channel : diffuser)
        for (int s = 0; s < kDiffuserStages; ++s)
            for (int l = 0; l < kDiffuserWidth; ++l)
            {
                auto& r = channel[s][l];
                r.delay = diffuserDelaySamples (s, l, sampleRate);
                r.data.assign ((size_t) nextPowerOfTwo (r.delay + 1), 0.0f);
                r.mask = (int) r.data.size() - 1;
            }

    targetsDirty = true;
    reset();
}

void ErEngine::reset()
{
    std::fill (line.begin(), line.end(), 0.0f);
    writePos = 0;

    for (auto& channel : diffuser)
        for (auto& stage : channel)
            for (auto& r : stage)
            {
                std::fill (r.data.begin(), r.data.end(), 0.0f);
                r.pos = 0;
            }

    for (auto& channel : bandZ)
        std::fill (std::begin (channel), std::end (channel), 0.0f);

    hiCutZ[0] = hiCutZ[1] = 0.0f;

    fading = false;
    dipping = false;
    primed = false;   // the next block snaps every smoother to its target
}

void ErEngine::setSettings (const Settings& s) noexcept
{
    // Accumulated relative movement of the two continuous controls that
    // define a set. 10 section 3: a crossfade is retriggered once this passes
    // 1 %, so a sweep is a chain of crossfades rather than one per block.
    if (s.sizeM != target.sizeM)
    {
        accumulated += std::abs (s.sizeM - target.sizeM) / std::max (target.sizeM, 0.01f);
        sinceChange = 0;
    }

    if (s.spreadMs != target.spreadMs)
    {
        accumulated += std::abs (s.spreadMs - target.spreadMs) / std::max (target.spreadMs, 1.0f);
        sinceChange = 0;
    }

    target = s;
    targetsDirty = true;
}

bool ErEngine::definesDifferentSet (const Settings& a, const Settings& b) const noexcept
{
    // Spread only reaches the gains in the two envelope modes, so moving it
    // in Taps mode builds nothing.
    const auto spreadMatters = a.mode != kModeTaps || b.mode != kModeTaps;

    return a.mode != b.mode
        || a.variation != b.variation
        || a.sizeM != b.sizeM
        || a.shape != b.shape
        || (spreadMatters && a.spreadMs != b.spreadMs);
}

void ErEngine::updateTargets() noexcept
{
    hiCutTarget = hiCutCoefficient (target.hiCutHz, sampleRate);
    targetsDirty = false;
}

//==============================================================================
void ErEngine::build (TapSet& set, const Settings& s) noexcept
{
    const auto type = std::clamp (s.type, 0, 5);
    const auto& t = erTableFor (type);

    const auto k    = sizeScale (t, s.sizeM);
    const auto sEff = kReferenceSizeM * k;
    const auto W    = t.windowMs * k;

    for (int b = 0; b < kErBands; ++b)
    {
        const auto a = onePoleCoefficient (erBandCutoffHzAt (t, b, sEff), sampleRate);
        set.bandA[b] = a;
        set.eta[b]   = (1.0f - a) / (1.0f + a);   // sum of ((1 - a) a^n)^2
    }

    const auto v = std::clamp (s.variation, 0, kErVariations - 1);
    const auto& vs = t.variation[v];

    // Variation 6 reads the table's variation-6 set as the one mono set E;
    // `combDelayMs` and `combGain` are not read at all (see runSet).
    set.side = v == kErCombVariation;

    const auto maxDelay = std::max (0, mask - 1);

    for (int ch = 0; ch < 2; ++ch)
    {
        const auto& c = ch == 0 ? vs.left : vs.right;
        const auto n = std::clamp (c.numTaps, 0, kErMaxTaps);

        // Taps mode's set is built first whatever the mode, because its core
        // energy is the level every mode is renormalised to: switching mode
        // is not also a level change (AGENTS.md, Blend). Energy and Blend
        // then overwrite it with their own.
        float tapMs[kErMaxTaps] {};
        float tapGain[kErMaxTaps] {};

        for (int i = 0; i < n; ++i)
        {
            tapMs[i]   = c.taps[i].timeMs * k;
            tapGain[i] = c.taps[i].gain / k * endTaper (tapMs[i], W);

            set.delay[ch][i] = std::clamp (roundToSamples (tapMs[i], sampleRate), 0, maxDelay);
            set.theta[ch][i] = c.taps[i].theta;
            set.band[ch][i]  = std::clamp (c.taps[i].band, 0, kErBands - 1);
            set.base[ch][i]  = tapGain[i];
        }

        set.num[ch] = n;
        buildPairs (set, ch);

        // At D = 0 only the core taps sound, so this *is* the core energy --
        // computed by the same function that renormalises, which is what
        // makes the renormalisation exactly unity there.
        set.energy[ch] = weightedEnergy (set, ch, 0.0f);

        const auto onset = n > 0 ? tapMs[0] : 0.0f;

        if (s.mode == kModeEnergy)
        {
            // Velvet noise, grid-based with jitter: one pulse per equal cell
            // across the window, which 10 section 3 records as smoother than
            // fully random placement at the same density. Every pulse is on
            // at every density -- velvet noise is already the dense end of
            // the bridge -- and the diffuser still follows DENSITY.
            Rng rng { (t.seed * 2654435761u)
                      ^ ((std::uint32_t) (type + 1) * 0x9E3779B9u)
                      ^ ((std::uint32_t) (v + 1) * 0x85EBCA6Bu)
                      ^ ((std::uint32_t) (ch + 1) * 0xC2B2AE35u) };

            if (rng.s == 0u)
                rng.s = 1u;

            for (int i = 0; i < 4; ++i)
                rng.next01();

            const auto cell = std::max (W - onset, 0.0f) / (float) kErMaxTaps;

            for (int i = 0; i < kErMaxTaps; ++i)
            {
                const auto u    = onset + ((float) i + rng.next01()) * cell;
                const auto sign = rng.next01() < 0.5f ? -1.0f : 1.0f;

                // The band of the table tap nearest in time: the bands are
                // order bands, and order rises with time, so the colour a
                // pulse gets is the colour a reflection arriving then has.
                int band = 0;
                float best = 1.0e30f;

                for (int j = 0; j < n; ++j)
                    if (std::abs (tapMs[j] - u) < best)
                    {
                        best = std::abs (tapMs[j] - u);
                        band = std::clamp (c.taps[j].band, 0, kErBands - 1);
                    }

                set.delay[ch][i] = std::clamp (roundToSamples (u, sampleRate), 0, maxDelay);
                set.base[ch][i]  = sign * envelope (u - onset, s.spreadMs, s.shape) * endTaper (u, W);
                set.theta[ch][i] = 0.0f;
                set.band[ch][i]  = band;
            }

            set.num[ch] = n > 0 ? kErMaxTaps : 0;
            buildPairs (set, ch);
        }
        else if (s.mode == kModeBlend)
        {
            // Blend: Taps' times, bands and thresholds, the Energy envelope's
            // gains in place of the physical law. The renormalisation in
            // applyDensity brings it to Taps' level. Same times, so the same
            // pairs.
            for (int i = 0; i < n; ++i)
                set.base[ch][i] = envelope (tapMs[i] - onset, s.spreadMs, s.shape) * endTaper (tapMs[i], W);
        }
    }

    set.built = s;
}

void ErEngine::buildPairs (TapSet& set, int ch) noexcept
{
    // The inner product of two one-pole pulses, the earlier through a band
    // with pole a and the later, Delta samples on, through one with pole b,
    // is (1 - a)(1 - b) a^Delta / (1 - a b): closed form, because both are
    // geometric. Only pairs whose overlap is worth a millionth of a pulse's
    // own energy are kept, which for a table that honours the 0.9 ms
    // separation rule is usually none -- this is what keeps a table that does
    // not, or a size small enough to squeeze two taps onto adjacent samples,
    // from moving the level as DENSITY brings the second one in.
    auto& count = set.numPairs[ch];
    count = 0;

    float maxA = 0.0f;
    for (const auto a : set.bandA)
        maxA = std::max (maxA, a);

    const auto reach = maxA > 0.0f ? (int) std::ceil (std::log (1.0e-6) / std::log ((double) maxA)) : 0;

    for (int i = 0; i < set.num[ch]; ++i)
        for (int j = i + 1; j < set.num[ch]; ++j)
        {
            const auto delta = set.delay[ch][j] - set.delay[ch][i];

            if (std::abs (delta) > reach || count >= TapSet::kMaxPairs)
                continue;

            const auto early = delta >= 0 ? i : j;
            const auto late  = delta >= 0 ? j : i;
            const auto a = (double) set.bandA[set.band[ch][early]];
            const auto b = (double) set.bandA[set.band[ch][late]];

            const auto overlap = (1.0 - a) * (1.0 - b) * std::pow (a, std::abs (delta)) / (1.0 - a * b);

            if (overlap > 1.0e-6 * (1.0 - a) / (1.0 + a))
                set.pairs[ch][count++] = { (std::uint8_t) i, (std::uint8_t) j, (float) overlap };
        }
}

float ErEngine::weightedEnergy (TapSet& set, int ch, float d) noexcept
{
    // The energy the band filters actually put out for the gains a_k w_k(D),
    // written into `gain` on the way: the sum of each tap's squared gain
    // times its band's pulse energy, plus twice each overlapping pair's
    // product times its overlap. **The per-band weighting is not in 10
    // section 3's formula** and has to be: the four bands pass different
    // fractions of a pulse's energy (a 3 kHz one-pole keeps about a fifth of
    // it at 48 kHz, a 12 kHz one about two thirds), and the infill is spread
    // across bands differently from the core, so renormalising the raw gains
    // lets the level drift with density by most of a decibel.
    float sum = 0.0f;

    for (int i = 0; i < set.num[ch]; ++i)
    {
        const auto g = set.base[ch][i] * densityWeight (set.theta[ch][i], d, rampWidth);
        set.gain[ch][i] = g;
        sum += g * g * set.eta[set.band[ch][i]];
    }

    for (int p = 0; p < set.numPairs[ch]; ++p)
    {
        const auto& pair = set.pairs[ch][p];
        sum += 2.0f * set.gain[ch][pair.i] * set.gain[ch][pair.j] * pair.overlap;
    }

    return std::max (sum, 0.0f);
}

void ErEngine::applyDensity (TapSet& set, float d) noexcept
{
    // 10 section 3's bridge: a_k * w_k(D), renormalised so the ER energy is
    // the same at every density -- the energy out of the band filters, which
    // is what `weightedEnergy` measures.
    for (int ch = 0; ch < (set.side ? 1 : 2); ++ch)
    {
        const auto sum = weightedEnergy (set, ch, d);
        const auto scale = sum > 0.0f ? std::sqrt (set.energy[ch] / sum) : 0.0f;

        for (int i = 0; i < set.num[ch]; ++i)
            set.gain[ch][i] *= scale;
    }
}

void ErEngine::prime() noexcept
{
    active = 0;
    build (sets[0], target);

    density = target.density;
    applyDensity (sets[0], density);
    densityApplied = density;

    for (int s = 0; s < kDiffuserStages; ++s)
    {
        stageW[s] = diffuserStageWeight (s, density);
        stageNorm[s] = 1.0f / std::sqrt ((1.0f - stageW[s]) * (1.0f - stageW[s]) + stageW[s] * stageW[s]);
    }

    std::copy (std::begin (sets[0].bandA), std::end (sets[0].bandA), std::begin (bandA));
    hiCutA = hiCutTarget;

    fading = false;
    dipping = false;
    accumulated = 0.0f;
    sinceChange = INT_MAX / 2;
    primed = true;
}

//==============================================================================
void ErEngine::runSet (const TapSet& set, int readBase, float* accL, float* accR) const noexcept
{
    if (set.side)
    {
        // **Variation 6, "mono null": the ER go into the side and nowhere
        // else.** With E the mono set, L = +E and R = -E -- BMO Dimension's
        // mid/side convention, L = M + S and R = M - S, and its principle:
        // "anything done to S alone is invisible in the mono sum"
        // (modules/dim/dsp/DspCore.h, the DspCore class comment). After the
        // mix the module puts out dry + E and dry - E, whose sum is exactly
        // twice the dry.
        //
        // **Exactly, not nearly.** Everything after this point -- the band
        // one-poles, the diffuser, the hi-cut, the dip -- is the same
        // arithmetic in both channels on states that started equal and
        // opposite, and IEEE negation commutes with every one of those
        // multiplies and adds, so R stays the bit-exact negative of L.
        // `tests/dsp/ReverbDspTests.cpp` asserts L + R == 0.0f.
        for (int b = 0; b < kErBands; ++b)
            accL[b] = 0.0f;

        for (int i = 0; i < set.num[0]; ++i)
            accL[set.band[0][i]] += set.gain[0][i] * line[(size_t) ((readBase - set.delay[0][i]) & mask)];

        for (int b = 0; b < kErBands; ++b)
            accR[b] = -accL[b];

        return;
    }

    for (int b = 0; b < kErBands; ++b)
        accL[b] = accR[b] = 0.0f;

    for (int i = 0; i < set.num[0]; ++i)
        accL[set.band[0][i]] += set.gain[0][i] * line[(size_t) ((readBase - set.delay[0][i]) & mask)];

    for (int i = 0; i < set.num[1]; ++i)
        accR[set.band[1][i]] += set.gain[1][i] * line[(size_t) ((readBase - set.delay[1][i]) & mask)];
}

void ErEngine::process (const float* in, float* outL, float* outR, int numSamples) noexcept
{
    if (line.empty())
    {
        for (int i = 0; i < numSamples; ++i)
            outL[i] = outR[i] = 0.0f;

        return;
    }

    if (targetsDirty)
        updateTargets();

    if (! primed)
        prime();

    for (int n = 0; n < numSamples; ++n)
    {
        line[(size_t) writePos] = in[n];

        //-- Control, sample-accurate -----------------------------------------
        float dipGain = 1.0f;

        if (dipping)
        {
            dipGain = 0.5f * (1.0f + (float) std::cos (2.0 * kPi * (double) dipPhase / (double) crossfadeSamples));

            // The swap happens on the sample at the bottom of the dip, where
            // the gain is exactly zero, so the old table's last output and the
            // new table's first are both multiplied by nothing.
            if (! swapped && (int) dipPhase == crossfadeSamples / 2)
            {
                build (sets[active], target);
                applyDensity (sets[active], density);
                fading = false;
                accumulated = 0.0f;
                swapped = true;
                dipGain = 0.0f;
            }

            dipPhase += 1.0f;

            if ((int) dipPhase >= crossfadeSamples)
                dipping = false;
        }
        else if (target.type != sets[active].built.type)
        {
            dipping = true;
            swapped = false;
            dipPhase = 1.0f;
        }
        else if (! fading && definesDifferentSet (target, sets[active].built))
        {
            const auto discrete = target.mode != sets[active].built.mode
                               || target.variation != sets[active].built.variation;

            if (discrete || accumulated >= 0.01f || sinceChange >= crossfadeSamples)
            {
                auto& incoming = sets[1 - active];
                build (incoming, target);
                applyDensity (incoming, density);
                fading = true;
                fadePhase = 0.0f;
                accumulated = 0.0f;
            }
        }

        if (sinceChange < INT_MAX / 2)
            ++sinceChange;

        //-- Smoothing ----------------------------------------------------------
        if (density != target.density)
        {
            density += smoothCoeff * (target.density - density);

            if (std::abs (target.density - density) <= 1.0e-6f)
                density = target.density;
        }

        if (density != densityApplied)
        {
            applyDensity (sets[active], density);

            if (fading)
                applyDensity (sets[1 - active], density);

            for (int s = 0; s < kDiffuserStages; ++s)
            {
                const auto w = diffuserStageWeight (s, density);
                stageW[s] = w;
                stageNorm[s] = 1.0f / std::sqrt ((1.0f - w) * (1.0f - w) + w * w);
            }

            densityApplied = density;
        }

        const auto& newest = fading ? sets[1 - active] : sets[active];

        for (int b = 0; b < kErBands; ++b)
            if (bandA[b] != newest.bandA[b])
            {
                bandA[b] += smoothCoeff * (newest.bandA[b] - bandA[b]);

                if (std::abs (newest.bandA[b] - bandA[b]) <= 1.0e-7f)
                    bandA[b] = newest.bandA[b];
            }

        if (hiCutA != hiCutTarget)
        {
            hiCutA += smoothCoeff * (hiCutTarget - hiCutA);

            if (std::abs (hiCutTarget - hiCutA) <= 1.0e-7f)
                hiCutA = hiCutTarget;
        }

        //-- Taps -------------------------------------------------------------
        float accL[kErBands], accR[kErBands];
        runSet (sets[active], writePos, accL, accR);

        if (fading)
        {
            // Raised cosine, the two windows summing to exactly one.
            const auto gIn  = 0.5f * (1.0f - (float) std::cos (kPi * (double) fadePhase / (double) crossfadeSamples));
            const auto gOut = 1.0f - gIn;

            float inL[kErBands], inR[kErBands];
            runSet (sets[1 - active], writePos, inL, inR);

            for (int b = 0; b < kErBands; ++b)
            {
                accL[b] = gOut * accL[b] + gIn * inL[b];
                accR[b] = gOut * accR[b] + gIn * inR[b];
            }

            fadePhase += 1.0f;

            if ((int) fadePhase >= crossfadeSamples)
            {
                active = 1 - active;
                fading = false;
            }
        }

        //-- Order bands, diffuser, hi-cut, per channel -----------------------
        float out[2];

        for (int ch = 0; ch < 2; ++ch)
        {
            const float* acc = ch == 0 ? accL : accR;
            float* z = bandZ[ch];
            float e = 0.0f;

            for (int b = 0; b < kErBands; ++b)
            {
                auto y = (1.0f - bandA[b]) * acc[b] + bandA[b] * z[b];

                // Flushed to exact zero long before a denormal: a host sets
                // FTZ, but the tests and the measurement tool do not, and a
                // decaying one-pole is the classic hundredfold slowdown.
                if (std::abs (y) < 1.0e-30f)
                    y = 0.0f;

                z[b] = y;
                e += y;
            }

            // The diffuser. The pulse enters all four lines at half level
            // (energy 1), each stage delays and recombines through the
            // orthonormal butterfly, and twice line 0 comes out -- so with
            // every stage out this is exactly a wire, and with a stage in it
            // turns one pulse into four of a quarter the energy each.
            //
            // **The fourth line enters inverted, and that is load-bearing.**
            // The butterfly is an involution (H H = I), so at DC three stages
            // act as one and line 0 reads the sum of the four inputs: fed
            // [1, 1, 1, 1] / 2 that is a gain of 2, +6 dB at DC with every
            // stage in, and an ER of positive taps keeps a few per cent of its
            // energy near DC -- enough to lift the level by a quarter of a
            // decibel as DENSITY opens the diffuser. [1, 1, 1, -1] / 2 has the
            // same energy and a DC gain of exactly 1 with none, one, two or
            // three stages in.
            float v[kDiffuserWidth] { 0.5f * e, 0.5f * e, 0.5f * e, -0.5f * e };

            for (int s = 0; s < kDiffuserStages; ++s)
            {
                float d[kDiffuserWidth];

                for (int l = 0; l < kDiffuserWidth; ++l)
                {
                    auto& r = diffuser[ch][s][l];
                    r.data[(size_t) r.pos] = v[l];
                    d[l] = r.data[(size_t) ((r.pos - r.delay) & r.mask)];
                    r.pos = (r.pos + 1) & r.mask;
                }

                const auto a0 = d[0] + d[1], a1 = d[0] - d[1];
                const auto a2 = d[2] + d[3], a3 = d[2] - d[3];
                const float h[kDiffuserWidth] { 0.5f * (a0 + a2), 0.5f * (a1 + a3),
                                                0.5f * (a0 - a2), 0.5f * (a1 - a3) };

                // 10 section 3's uncorrelated-crossfade normaliser: the
                // delayed, recombined copy shares no sample with its input,
                // so the two add in power and 1/sqrt((1-w)^2 + w^2) holds the
                // level through the fade. At w = 0 both factors are exactly 1
                // and 0, and the stage is a wire.
                const auto wet = stageW[s] * stageNorm[s];
                const auto dry = (1.0f - stageW[s]) * stageNorm[s];

                for (int l = 0; l < kDiffuserWidth; ++l)
                    v[l] = dry * v[l] + wet * h[l];
            }

            const auto diffused = 2.0f * v[0];

            // ER HI-CUT. At the top of its range hiCutA is exactly zero and
            // this line is exactly `diffused`.
            auto y = (1.0f - hiCutA) * diffused + hiCutA * hiCutZ[ch];

            if (std::abs (y) < 1.0e-30f)
                y = 0.0f;

            hiCutZ[ch] = y;
            out[ch] = y * dipGain;
        }

        outL[n] = out[0];
        outR[n] = out[1];

        writePos = (writePos + 1) & mask;
    }
}

//==============================================================================
float ErEngine::currentTapGain (int channel, int tap) const noexcept
{
    const auto& set = fading ? sets[1 - active] : sets[active];
    const auto ch = set.side ? 0 : std::clamp (channel, 0, 1);

    return tap >= 0 && tap < set.num[ch] ? set.gain[ch][tap] : 0.0f;
}

int ErEngine::currentTapCount (int channel) const noexcept
{
    const auto& set = fading ? sets[1 - active] : sets[active];
    return set.num[set.side ? 0 : std::clamp (channel, 0, 1)];
}

size_t ErEngine::memoryBytes() const noexcept
{
    size_t bytes = line.size() * sizeof (float);

    for (const auto& channel : diffuser)
        for (const auto& stage : channel)
            for (const auto& r : stage)
                bytes += r.data.size() * sizeof (float);

    return bytes;
}

} // namespace bmo::reverb
