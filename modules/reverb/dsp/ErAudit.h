#pragma once

#include "modules/reverb/dsp/ErTable.h"

namespace bmo::reverb::ergen
{

//==============================================================================
/** The audits of 10 section 3 and 11 section 6, as code, run on a finished
    table -- after the jitter, as 10 section 8 insists.

    They read **only the table** plus four facts about its type that the table
    does not carry: the default SIZE, ER level and DENSITY it opens at, and the
    direct path its geometry implies (for the lateral fraction, whose
    denominator includes the direct sound). So breaking a table by hand reddens
    them without touching the generator, which is how each rule is proven
    non-vacuous.

    **Where the rules are evaluated.** Every rule is in milliseconds and
    decibels, and SIZE scales a table's times by S / S_ref and its gains by
    S_ref / S, so no table can satisfy them at every size: a 0.5 m room has
    every tap inside 8 ms, and every tap 27 dB louder than at 12 m. The audits
    therefore evaluate each table **at its type's default SIZE**, which is the
    size it was derived at and the size a fresh instance plays it at. For Room
    that is the reference size itself. `measure_reverb taps --audit` prints the
    reference-size figures beside them for the other five.

    **Levels are at the ER fader's top (0 dB) and before density
    renormalisation**, i.e. the table's own gains, which is the loud end of
    both -- except rule (iii), which 10 section 3 states at the default ER
    fader and so is evaluated there. The dichotic bonus is not spent: the
    Kuttruff ceiling is applied to every tap at full diotic strictness, so the
    ~10 dB 10 section 3 allows once a tap is decorrelated is headroom left in
    hand, not used.

    **PLATE IS NOT A ROOM, AND THE ROOM RULES DO NOT APPLY TO IT.** Owner
    decision, 2026-09-23: "plate verbs are a physical metal plate model, not a
    room model. room rules shouldn't apply." So for Plate the separation and
    gap rules, the 1-8 ms full-band ban, the Kuttruff ceiling, all four flam
    rules, the first-tap-near-centre rule, the lateral fraction and the Moorer
    check are **not evaluated** (their margins print n/a). What still binds
    Plate is what binds any source: the -15.3 dB single-tap ceiling (a
    colouration bound), taps inside the window, and gamma >= 0 at VARIATION
    0-5 (mono safety; the rooms' falling ladder is not asserted of it). In
    their place, six **plate rules** from measurement -- source: measured,
    16 EMT 140 IRs, research doc section 8, 2026-09-23. (They replace three
    rules taken from an earlier estimate, one of which -- "the heard energy
    peaks in the first 5 ms" -- the measurement showed to be wrong: a plate's
    energy swells.) On heard energy, every channel of VARIATION 0-5:

      - onset: the first tap at or under 2 ms (measured: sound within ~2 ms);
      - front: energy inside 5 ms at most 4 % of the first 100 ms
        (measured 0.1-4 %);
      - swell time: the 2 ms-window envelope peaks between 10 and 25 ms;
      - swell rise: that peak at least 8 dB over the 0-5 ms level (measured
        10-13 dB);
      - band onsets (10 % of each band's first 100 ms): highs first, the
        8 kHz band by 4 ms, the 500 Hz band at 8-16 ms (measured about 2.5,
        10, 13 and 18 ms for 8 k / 2 k / 500 / 125 Hz);
      - 500 Hz L/R: the right channel's onset 3-7 ms after the left's
        (measured 3.6-6.4 ms, right later).

    Reported and not asserted: every band's onset in each channel, the 2 kHz
    and 8 kHz L/R offsets (measured 1 kHz 0.8-1.4 ms, 2 kHz -0.2-1.1, 4-8 kHz
    0), and the span. Do not "fix" Plate back into a room: a Plate table
    that fails a room rule is not failing anything. */


enum Rule
{
    ruleSeparation = 0,   ///< >= 0.9 ms between neighbours, every channel, every position, all 48
    ruleGaps,             ///< no two core inter-tap gaps within 2 % of each other
    ruleFullBand,         ///< no tap in 1-8 ms unless its band is below 1.5 kHz
    ruleKuttruff,         ///< 2-20 ms: level <= -0.6 t - 8 dB, no dichotic bonus
    ruleTapCeiling,       ///< no tap above -15.3 dB
    ruleFlamLate,         ///< (i) no tap after 25 ms above -12 dB re cumulative energy at 25 ms
    ruleFlamOnset,        ///< (ii) no second onset: 5 ms window energies do not rise after the peak
    ruleLoc,              ///< (iii) ER energy in 100 ms >= 3 dB below direct at the default fader
    ruleProximity,        ///< (iv) a deliberate small allocation inside 5 ms
    ruleCentre,           ///< the first reflection stays near centre
    ruleGamma,            ///< gamma >= 0 at VARIATION 0-5, falling ~0.95 -> ~0.05; Var 6 mono null, its mono sum exactly zero
    ruleLateral,          ///< the room's early lateral fraction 0.10-0.25, at VARIATION 2
    ruleMoorer,           ///< Room only: count and span against Moorer's 19 taps, 4.3-79.7 ms

    // Plate's own rules -- n/a for a room. See the comment above Rule.
    rulePlateOnset,       ///< first tap at or under 2 ms
    rulePlateFront,       ///< heard energy inside 5 ms <= 4 % of the first 100 ms
    rulePlateSwellTime,   ///< the 2 ms-window envelope peaks between 10 and 25 ms
    rulePlateSwellRise,   ///< ... at least 8 dB over its 0-5 ms level
    rulePlateBands,       ///< band onsets highs first; 8 kHz <= 4 ms; 500 Hz 8-16 ms
    rulePlateLr500,       ///< 500 Hz onset: right 3-7 ms after left
    numRules
};

/** Rule (ii)'s allowance for a sparse set: see ErAudit.cpp. */
inline constexpr double kOnsetToleranceDb = 3.0;

const char* ruleName (int rule) noexcept;

/** What a type's audit needs that its table does not carry. */
struct AuditContext
{
    float sizeM;           ///< the type's default SIZE
    float erLevelDb;       ///< its default ER fader
    float density;         ///< its default DENSITY, 0..1
    float directGain;      ///< 1 m / d_direct at sizeM
    bool  isRoom;          ///< Moorer's table is a reference for Room only
    bool  isPlate;         ///< a plate has no room lateral fraction; see ErAudit.cpp
};

AuditContext contextFor (int typeIndex);

/** A candidate's context: its stand-in type's defaults and its own direct path. */
AuditContext candidateContext (const char* name);

struct Figures
{
    double gamma[kErCombVariation];     ///< VARIATION 0-5 at the default density; Var 6 has no gamma
    double gammaCore[kErCombVariation]; ///< core taps only (DENSITY 0)
    double gammaFull[kErCombVariation]; ///< every tap (DENSITY 100 %)
    int    monoNullMismatches;          ///< Var 6: taps where left and right differ; 0 is an exact mono null
    double lateralFraction;             ///< the room's LF: VARIATION 2, core taps, lateral cosine squared -- the rule
    double lateralFractionStereo;       ///< what the output carries: side over mid, 125-1000 Hz, default density
    double lateralFractionStereoFull;   ///< the same at DENSITY 100 %
    double energyBefore30;              ///< share of ER energy before 30 ms -- 11 section 6's dropped rule, measured
    double locMarginDb;                 ///< rule (iii)'s ER-to-direct figure, dB below direct
    double proximityShare;              ///< share of ER energy inside 5 ms
    double firstTapMs, lastTapMs;       ///< VARIATION 2, left, at the default size
    double coreFirstMs, coreLastMs;
    double maxTapDb;
    double largestRiseDb;               ///< rule (ii) taken literally: the largest 5 ms rise after the peak
    double worstGapPct;                 ///< smallest core gap-to-gap difference, per cent
    int    fullSetGapCollisions;        ///< over all 48 taps, adjacent gaps within 2 % -- reported, not a rule

    // Plate, reported against the research (all VARIATION 2 unless said).
    double plateFrontShare;             ///< heard energy inside 5 ms over the first 100 ms
    double plateRiseDb, platePeakMs;    ///< the swell: peak over the 0-5 ms level, and when
    double plateOnsetMs[2][kErBands];   ///< band onsets (10 % of the band's first 100 ms), left / right
};

struct Report
{
    double margin[numRules];            ///< >= 0 passes; the units are the rule's own
    bool   pass[numRules];
    bool   allPass;
    Figures figures;
};

/** Run every rule on `table` in `ctx`. */
Report audit (const ErTable& table, const AuditContext& ctx);

/** The same table evaluated as if it were played at `sizeM` -- for printing
    the reference-size figures beside the default-size ones. */
Report auditAtSize (const ErTable& table, const AuditContext& ctx, float sizeM);

/** Rule (ii)'s raw material: the energy in successive 5 ms windows of one
    channel at the context's size and density, from 0 ms. Returns the count. */
int windowEnergies (const ErTable& table, const AuditContext& ctx, int variation, bool right,
                    double* out, int maxWindows);

} // namespace bmo::reverb::ergen
