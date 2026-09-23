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
    their place, three **plate rules**, the structural ones from Frosty's
    plate research (2026-09-23), physics or sourced:

      - instant onset: the first tap at or under 1 ms (a real plate has no
        pre-delay);
      - front-loaded: the heard energy peaks in the first 5 ms window (the
        highs are dense by about 5 ms);
      - dispersion order: each darker band's first arrival comes after the
        brighter band's (group speed goes as sqrt(f), so highs first).

    Reported and **not** asserted, because the research labels them estimates:
    the L/R offset per band (about 5-10 ms in the lows, under 1 ms in the
    highs), the span (about 30 ms), and how close each band's first arrival
    sits to 1/sqrt(f). Do not "fix" Plate back into a room: a Plate table that
    fails a room rule is not failing anything. */

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
    ruleGamma,            ///< gamma >= 0 at all seven positions, 0-5 falling ~0.95 -> ~0.05
    ruleLateral,          ///< the room's early lateral fraction 0.10-0.25, at VARIATION 2
    ruleMoorer,           ///< Room only: count and span against Moorer's 19 taps, 4.3-79.7 ms

    // Plate's own rules -- n/a for a room. See the comment above Rule.
    rulePlateOnset,       ///< the first tap at or under 1 ms
    rulePlateFront,       ///< the heard energy peaks in the first 5 ms window
    rulePlateDispersion,  ///< each band's first arrival after the brighter band's
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

struct Figures
{
    double gamma[kErVariations];        ///< at the default density
    double gammaCore[kErVariations];    ///< core taps only (DENSITY 0)
    double gammaFull[kErVariations];    ///< every tap (DENSITY 100 %)
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
    double plateFrontShare;             ///< heard energy inside 5 ms, default density
    double plateBandFirstMs[kErBands];  ///< each band's first arrival, left channel
    double plateBandMeanMs[kErBands];   ///< each band's mean arrival, left channel
    double plateLrMs[kErBands];         ///< VARIATION 5: mean L-R offset per band
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
