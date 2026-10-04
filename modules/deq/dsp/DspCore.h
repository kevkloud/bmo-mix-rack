#pragma once

#include "modules/deq/dsp/Filters.h"
#include "modules/deq/dsp/Dynamics.h"
#include "core/dsp/AnalyserTap.h"
#include "core/dsp/SwitchFade.h"
#include <array>
#include <cstdint>
#include <atomic>
#include <memory>
#include <vector>
#include <complex>

namespace bmo::deq
{

/** No hard ceiling in the design (spec A6). This is only the size of the
    fixed array, so the audio thread never allocates; the host-parameter
    budget -- 32 per rack slot -- is what actually limits a product, and
    params.h is where that gets decided. */
inline constexpr int kMaxBands = 64;

/** Coefficients are redesigned every this many samples and glide linearly
    in between. The cadence counts absolute samples from prepare()/reset(),
    never block boundaries, so the output is bit-identical whatever block size
    the host uses -- the spec's T1 check, and the reason it can hold. */
inline constexpr int kControlInterval = 8;

/** Static parameters glide to a new value over roughly this long. */
inline constexpr double kSmoothingMs = 10.0;

/** A switch -- placement, DYN, direction, solo -- crosses over in this long
    rather than stepping (core/dsp/SwitchFade.h, and the house rule in
    core/AGENTS.md: under 1.5x the steady signal's largest step). A change
    asked for while one is in progress waits for it, then crosses over from
    there: the latest choice wins, at most one crossover late. DYN and
    direction blend two gain laws rather than two filters, so they simply
    turn round from where they are. A change of shape is not a crossover at
    all; see kShapeFadeOutMs. */
inline constexpr double kSwitchFadeMs = 10.0;

/** A change of shape dips the output through silence (Band): it fades out
    over kShapeFadeOutMs while the shape arriving warms up on the band's own
    input, the band takes the arriving shape at the bottom, and the output
    fades back in over kShapeFadeInMs -- 28 ms in all.

    The owner's rule for a discrete switch (2026-10-03): it may pass through
    a short dip; it may not click, burst or linger. Every crossover tried
    here broke one of those. A blend of the two shapes' outputs cancels where
    they are out of phase (a full null for Low Cut against High Cut at one
    corner); two shapes half applied in series go over both (14 dB for two
    Q 0.1 cuts); and either way the shape arriving has to start from some
    state, and the leaving shape's -- a +24 dB bell at Q 40 and 30 Hz holds
    seconds of resonance -- burst 30 dB over the louder level. A dip cannot
    go over either level, and the fade out is the shape arriving's warm-up,
    so it comes in already settled on what the band is hearing. The fade in
    is the shorter, because the slower a fade the smaller its step and at
    30 Hz 8 ms is already under 1.2x. */
inline constexpr double kShapeFadeOutMs = 20.0;
inline constexpr double kShapeFadeInMs = 8.0;

/** The shape arriving warms up on the band's own input as recorded, from rest
    this many of its slowest time constants back, so that it comes in settled
    to within e^-6 (-52 dB) of what it would be had it always been there. An
    attenuating filter cannot attenuate a tone it has not heard for its own
    time constant -- a -24 dB bell at Q 40 and 30 Hz needs 107 ms of it --
    and the 28 ms of a change is not long enough to hear it live. It works
    through the record faster than real time during the fade out. */
inline constexpr double kWarmTimeConstants = 6.0;

/** How much of each band's input is kept for that: enough for six time
    constants of anything slower than a 30 Hz Q 40 cut (107 ms), and capped
    there; slower still, a shape arrives less than fully settled. Kept for the
    product's bands only (Settings::bandCount, at most kShapeHistoryBands), as
    floats: 12 bands take 2.9 MB at 48 kHz and 11.8 MB at 192 kHz. */
inline constexpr double kShapeHistoryMs = 640.0;
inline constexpr int kShapeHistoryBands = 16;

/** A dynamic band is only redesigned when its gain offset has moved by more
    than this since the last design. Static settings are always followed
    exactly; this only stops a band sitting in steady gain reduction from
    redesigning every interval over a thousandth of a dB of detector ripple. */
inline constexpr double kOffsetHysteresisDb = 0.001;

/** How bands combine. **Serial**, decided 2026-09-10 on the measurements in
    modules/deq/spec/topology-options.md, and confirmed by ear on 2026-09-12.

    - `serial`: each band's output feeds the next, as in a conventional
      parametric EQ. The total is the band curves added in dB, a low cut
      still cuts under an overlapping boost, and two -24 dB cuts give -48 dB.
    - `parallel` (the spec's original C4): out = x + sum(H_k x - x). Two
      coincident -24 dB cuts give -1.17 dB, polarity inverted, because
      1 + 2(G - 1) goes negative once G < 0.5.

    **Serial was confirmed by ear on 2026-09-12 and there is no user-facing
    switch** (Frosty): 57 blind pairs over seven sources, then six more with
    the two matched for amount so only the shape of the dynamic catch differed.
    See testing-notes/deq-blind-2026-09-11.md and spec/decisions.md.

    Parallel stays in the engine so `measure_deq render` can still A/B the two,
    which is what `--match` is built on. It is not a user control: a preset
    made in one would sound different in the other.

    Both are zero latency -- a chain of IIR filters has no crossover and no
    delay line -- and cost the same. */
enum class Topology { serial, parallel };

/** Where a band acts on a stereo signal. `mid` and `side` blend from stereo
    (msAmount 0) to the chosen M/S channel alone (msAmount 1). */
enum class Placement { stereo, mid, side };

struct DynamicSettings
{
    bool      enabled     = false;
    double    thresholdDb = -24.0;
    double    ratio       = 2.0;
    double    kneeDb      = 6.0;
    double    rangeDb     = -6.0;     // where the gain can move to; sign = cut/boost
    Direction direction   = Direction::above;
    double    attackMs    = 5.0;      // tau convention, see Dynamics.h
    double    releaseMs   = 120.0;
    bool      rms         = false;
};

struct BandSettings
{
    bool      enabled     = false;
    Shape     shape       = Shape::bell;
    double    frequencyHz = 1000.0;
    double    q           = 0.707;
    double    gainDb      = 0.0;
    Placement placement   = Placement::stereo;
    double    msAmount    = 1.0;
    DynamicSettings dynamics;
};

struct Settings
{
    Topology topology = Topology::serial;
    std::array<BandSettings, kMaxBands> bands {};

    /** How many of `bands` the product has: BMO DEQ's twelve. A band inside
        it that has been live since reset() keeps its detector listening while
        the band is off or its dynamics are, so dynamics coming back into use
        carry on from where a band that never left would be, rather than from
        whatever they last heard (see DspCore::Band). A band never switched on,
        or one past this count while it is off, costs nothing. */
    int bandCount = kMaxBands;

    /** The most resonant a Low Cut or High Cut is designed, applied to the Q
        each design actually uses -- while Q glides and through a change of
        shape -- not only to the target: BMO DEQ sets its kCutMaxQ (params.h)
        so a cut never has a resonant peak at any instant. A Q within 0.005
        over it is left alone, the same half knob step of slack effectiveQ
        allows, so the knob's own 0.71 runs as itself. The default leaves
        cuts alone. */
    double cutMaxQ = DesignLimits::kMaxQ;

    /** The widest a Low Shelf or High Shelf is designed (BMO DEQ: kShelfMaxQ,
        params.h). The cap lives here, applied by every design (designQ),
        rather than in the Q handed over, so that the band's Q -- one glide
        whatever the shape -- stays the knob's when the shape changes: a band
        toggled between a bell at Q 2 and a cut used to see its Q pulled
        toward the cut's 0.71 and back, and stepped 1.6x doing it. */
    double shelfMaxQ = DesignLimits::kMaxQ;
};

/** The Q a design of `shape` runs at: the band's own, or its shape's cap. A
    cut is compared with half a knob step of slack, so the knob's own 0.71 --
    0.71000004 once a host has snapped it -- runs as itself. */
inline double designQ (const Settings& s, Shape shape, double q) noexcept
{
    if ((shape == Shape::lowShelf || shape == Shape::highShelf) && q > s.shelfMaxQ)
        return s.shelfMaxQ;

    if ((shape == Shape::lowCut || shape == Shape::highCut) && q > s.cutMaxQ + 0.005)
        return s.cutMaxQ;

    return q;
}

//==============================================================================
/** The zero-latency dynamic EQ, JUCE-free. A ModuleDsp adapter maps a params.h
    value array onto Settings; tests and tools drive this directly.

    Per sample, per band:

    1. The band's coefficients step toward the latest design (Svf.h).
    2. Its detector reads a band-limited copy of the *dry* input through a
       sidechain filter of its own. It does not tap the band's filter: that
       filter's poles move with the band's gain (a bell's pole Q is A*Q), so
       a tapped detector would hear 0 dB at -12 dB of band gain and +12 dB at
       +12, and the dynamics would become a feedback loop.
    3. The band filters its input's M and S. Filtering L and R separately is
       the same thing -- H(L) = H(M) + H(S) -- so one pair of filters gives
       both the L/R contribution and the M/S one, and the M/S blend is exact at
       every value with no warm-up when it moves off an end.
    4. The contribution w = y - x is scaled by the band's enable fade and
       summed per the topology.

    Every control-interval samples, each band redesigns its target
    coefficients from its smoothed controls plus the detector's current gain
    offset. Nothing here delays the audio: latency is 0 in every mode.
*/
class DspCore
{
public:
    void prepare (double sampleRate, int maxBlockSize, int numChannels) noexcept;
    void reset() noexcept;

    /** Stores targets; cheap. The first call after prepare()/reset() is
        snapped to rather than glided to. */
    void setSettings (const Settings& s) noexcept { current = s; }
    const Settings& settings() const noexcept      { return current; }

    void process (float* const* channels, int numChannels, int numSamples) noexcept;
    void process (double* const* channels, int numChannels, int numSamples) noexcept;

    static constexpr int latencySamples() noexcept { return 0; }

    /** Hear one band alone, or -1 for the whole EQ. Momentary and never a
        parameter: the panel sets it while the control is held (ModuleDsp).

        **What you hear is the band's contribution** -- what it adds or takes
        away, `H(x) - x`, which is the quantity the parallel topology sums and
        the serial one accumulates. On a dynamic band that contribution moves
        with the detector, so soloing a de-esser is the sibilance being caught,
        which is the thing worth listening to. Its filtered *output* would be
        the whole signal with a dip in it, which is not.

        A band that is off contributes nothing, so soloing one is silence
        rather than the untouched signal: "this band, alone" has to mean the
        same thing whatever the band is doing.

        Read once per block, so it cannot break block-size invariance. */
    void setSolo (int band) noexcept { shared->solo.store (band, std::memory_order_relaxed); }
    int soloedBand() const noexcept  { return shared->solo.load (std::memory_order_relaxed); }

    /** The post-EQ window a panel's analyser draws. There is room in the
        engine for a pre tap as well -- `preTap()` is written on the same terms
        -- so adding a second curve later is a change to a panel and not to the
        audio path. Only the post one is wired to anything today. */
    AnalyserTap& postTap() noexcept { return shared->post; }
    AnalyserTap& preTap() noexcept  { return shared->pre; }

    /** The largest gain move any dynamic band is making right now, dB,
        **signed: positive is gain taken away, negative is gain added**.

        Not `>= 0`, which is what it was until 2026-09-15 and what the name
        still reads as. The name stays because it is the one
        `ModuleDsp::currentGainReductionDb` declares for every module, and
        widening what the value means costs less than a second channel through
        the engine, the processor and `ModuleContext`. Every other module in
        the suite only ever cuts, so BMO DEQ is the only one that returns the
        other sign. See the definition for what went wrong without it. */
    double currentGainReductionDb() const noexcept;

    //== Inspection, for tests, tools and a future curve display ===============
    double sampleRate() const noexcept { return rate; }

    /** A band's design at its target static settings (no dynamic offset). */
    Biquad bandDesign (int band) const noexcept;

    /** The combined static response at the target settings, for a centred
        (L = R) source, per the topology. */
    std::complex<double> staticResponseAt (double hz) const noexcept;

    double bandOffsetDb (int band) const noexcept   { return bands[(size_t) band].offsetDb; }
    double bandEnvelope (int band) const noexcept   { return bands[(size_t) band].detector.envelope(); }
    double bandGainDb (int band) const noexcept     { return bands[(size_t) band].appliedGainDb; }

    /** How many samples this band's detector has heard since construction:
        a band that has never been switched on hears none. */
    std::uint64_t detectorTicks (int band) const noexcept { return bands[(size_t) band].listened; }

    /** The coefficients a band is running this sample, or with `arriving`
        those of the shape it is changing to while it warms up (which stay
        where they were once a change is over). */
    SvfCoeffs bandCoefficients (int band, bool arriving = false) const noexcept
    {
        return arriving ? bands[(size_t) band].arriveCoeffs : bands[(size_t) band].cur;
    }

    /** No subnormal anywhere in filter or detector state. */
    bool allStateNormal() const noexcept;

    /** Every coefficient set in use, and every one being glided toward, is
        stable. Checked by the modulation tests at every step. */
    bool allCoefficientsStable() const noexcept;

private:
    /** A value that moves toward its target once per control tick, and in
        straight lines between ticks. */
    struct Glide
    {
        double target = 0.0, tick = 0.0, now = 0.0, step = 0.0;

        void snap (double v) noexcept { target = tick = now = v; step = 0.0; }
        void advanceTick (double alpha) noexcept
        {
            tick = target + alpha * (tick - target);
            if (std::abs (tick - target) < 1.0e-9) tick = target;
        }
    };

    /** One band, in two halves with two lifetimes.

        **The listener** -- frequency, Q and placement glides, the sidechain
        filter, the detector and the gain offset it asks for -- starts the
        first time a band inside Settings::bandCount is live, then runs
        whether or not the band is on and whether or not its dynamics are,
        and only reset() stops it. A band never switched on has nothing that
        could be stale and does no work (round 2 of the review: all twelve
        listening at the defaults cost 0.72 % -> 2.70 % of a 192 kHz / 32
        block). Until the 2026-10-03 review it ran only while the dynamics
        were in use, so it stood still while they were not and came back with
        a stale envelope: a -12 dB cut lasting 3.5 s at release 2000 ms on a
        signal that had gone quiet meanwhile. Listening costs the sidechain
        and the detector for a band that has been used and is now off, and
        buys a band whose dynamics, coming back by any route, are where they
        would be had they never left. It never reaches the audio of a band
        whose dynamics are off.

        **The band itself** -- its filter, its design and its fades -- runs
        only while the band is on or fading out, and a band that has faded
        out completely starts again from rest. */
    struct Band
    {
        Glide logHz, logQ, gainDb, beta, enable;

        SvfCoeffs cur, next, step;   // in use, being glided to, per-sample increment
        SvfState  m, s;

        SvfCoeffs sideCoeffs;        // the detector's own sidechain filter
        SvfTaps   sideTaps;
        SvfState  sideM, sideS;
        Detector  detector;
        GainComputer computer;

        double offsetDb = 0.0, appliedGainDb = 0.0;
        bool   live = false;         // enabled, or still fading out
        bool   hearing = false;      // the listener is running
        std::uint64_t listened = 0;  // samples the detector has heard (detectorTicks)

        // The listener's switches. dynMix is how far the dynamics are in use
        // and dirMix how far toward Below, both read at control rate; each
        // blends two offsets, so either turns round from where it is.
        // placeMix crosses from `fromPlacement` to `placement`, per sample,
        // for the detector's input and the band's output alike.
        dsp::Ramp dynMix, dirMix, placeMix;
        Placement placement = Placement::stereo, fromPlacement = Placement::stereo;

        // A change of shape (kShapeFadeOutMs): the output fades through
        // silence while the shape arriving runs in its own filter, on the
        // band's input, from rest -- or from the running filter's state when
        // the two have the same poles (a Low Cut and a High Cut at one corner
        // and Q), where that state is exactly its own -- and the band takes it
        // at the bottom. The shape leaving keeps its state to the end, so its
        // fade is its own sound getting quieter and nothing else.
        dsp::Ramp shapeFade;               // the band's output gain through a change
        bool      changing = false;        // a shape is arriving
        Shape     shape = Shape::bell;     // the shape the band is running
        Shape     arriving = Shape::bell;  // and the one it is changing to
        SvfCoeffs arriveCoeffs;
        SvfState  arriveM, arriveS;
        double    arriveHz = 1000.0, arriveQ = 0.707, arriveStatic = 0.0, arriveOffset = 0.0;  // what it was designed from
        bool      arriveInit = false;      // its state is still to be set
        int       behind = 0;              // recorded samples it has still to hear
        int       warmStep = 1;            // how many it hears per sample, catching up

        // The band's own input, M and S, the last kShapeHistoryMs of it, for
        // a shape arriving to warm up on. Allocated in prepare().
        std::vector<float> histM, histS;
        int       histPos = 0, histFilled = 0;

        // What `next` was designed from, so a static band is not redesigned.
        Shape  designedShape = Shape::bell;
        double designedHz = -1.0, designedQ = -1.0, designedStatic = 1.0e9, designedOffset = 0.0;
        double sideHz = -1.0, sideQ = -1.0; Shape sideShape = Shape::bell;
        double detAttack = -1.0, detRelease = -1.0; bool detRms = false;
    };

    template <typename Sample>
    void processImpl (Sample* const* channels, int numChannels, int numSamples) noexcept;

    void controlTick() noexcept;

    /** The design of `shape` at these settings, its Q capped where it is a
        cut (designQ). */
    SvfCoeffs designFor (Shape shape, double hz, double q, double gainDb) const noexcept;

    /** Back to rest: the band's filter always, its listener too when asked. */
    void resetBand (Band& b, bool listenerToo) noexcept;

    /** What the panel and the audio thread share. Held behind a pointer for
        one reason: an atomic is neither copyable nor movable, and an engine is
        built and handed back by value all over the tests and the tools. The
        allocation happens once, at construction, never on the audio thread. */
    struct Shared
    {
        AnalyserTap pre, post;
        std::atomic<int> solo { -1 };
    };

    std::array<Band, kMaxBands> bands {};
    Settings current;
    DesignGrid grid;
    std::unique_ptr<Shared> shared = std::make_unique<Shared>();
    double rate = 48000.0, tickAlpha = 0.0;
    int tickPhase = 0, bandsInUse = 0, shapeWarmSamples = 1;
    bool primed = false, prepared = false;

    // Solo crosses over from what was being heard (-1: the whole EQ) to what
    // is asked for. The first block after prepare()/reset() takes it as it is.
    dsp::Ramp soloMix;
    int soloFrom = -1, soloTo = -1;
    bool soloPrimed = false;
};

} // namespace bmo::deq
