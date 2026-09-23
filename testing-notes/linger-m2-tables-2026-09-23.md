# BMO Linger M2, table half: the early-reflection tables

2026-09-23, **on AURORA**, branch `frosty-linger-er-tables`, DSP-only tree
(`cmake -B build-dsp -DBMO_DSP_ONLY=ON`, Visual Studio, Debug for the tests,
Release for the seed searches). Nothing here has been heard; every figure is a
desk figure from `measure_reverb taps --audit` and `reverb_dsp_tests`.

## What was built

- `modules/reverb/dsp/ImageSource.{h,cpp}` -- the generator. Offline: it is in
  `bmo_reverb_ergen`, linked only by `reverb_dsp_tests` and `measure_reverb`,
  not in the module's DSP sources, so no plugin carries it.
- `modules/reverb/dsp/ErAudit.{h,cpp}` -- every rule as code, with a margin.
- `modules/reverb/dsp/ErTableData.inc` -- the six tables, emitted by
  `measure_reverb taps --emit`, included by `ErTable.cpp`. Never hand-edited.
- `ErTable.h` gains one function, `erSpanMsAt (const ErTable&, float)`, the
  per-type span, beside the placeholder's `erSpanMsAt (float)`. No field was
  added, renamed or removed.
- `reverb_dsp_tests`: the pin (every table regenerated and compared with the
  .inc using `==`), the shape the engine relies on, the audits on every
  shipped table, and one hand-broken table per rule that must redden.

## How a table is made (argued in the code, summarised here)

- **Voiced at the type's default SIZE, quoted at 12 m.** Heights of source and
  listener are human (1.7 / 1.5 m), so the floor bounce lands inside 5 ms --
  10 section 3's proximity allocation -- only at a real size. The audits run
  at the default SIZE too, because every rule is in ms and dB and the Size
  law moves both. At 12 m the other five fail several rules (the "at 12 m"
  column of `--audit`): SIZE is a user control and no table satisfies ms/dB
  rules at every size.
- **Shoebox 1 : 1.4 : 1.9**, listener and source off-centre, source a few
  degrees off the listener's axis. Orders 1-3, except Cavern (4) and Plate
  (0-5) -- see below.
- **21 core taps** = the strongest image in each of 21 equal slices of the
  window, then the strongest left; **27 infill** = velvet, one pulse per equal
  slice of the time the core leaves free (equal slices of plain time left some
  cells no room at all). Infill gains from the same (1 m / d) beta^n envelope.
- **Per-channel sets from two receivers.** A *shared* slot is one tap in both
  channels; a *split* slot is the left receiver's arrival on the left and the
  right's on the right, offsets 0.12-0.30 ms either side (spacing 0.20 m).
  VARIATION is nested shared sets: the *permutation* is the seeded order
  (loud and lateral first) in which slots leave the shared set; the
  *lateral-spread scalar* is 1 - gamma_target. The room's first reflection is
  never split (phantom centre). Core and infill are aimed separately so gamma
  is on target at DENSITY 0, default and 100 %.
- **Four bands**: orders 1/2/3 by 16 kHz * 0.8^n * (1 m / d)^0.2 at the band's
  geometric-mean path, plus a **proximity band at 1.2 kHz carrying every tap
  inside 8 ms** (below 1 ms too: a full-band fused tap is allowed but combs
  the dry and floors gamma).
- **Jitter +-3 %** per type from the pinned seed. Two rules are enforced
  *during* the draw -- 0.9 ms separation and the core 2 % gap rule, in every
  channel of every VARIATION -- because an infill pulse is placed into gaps;
  the audit still checks both after. Everything else is audit-only, and a
  failing table is re-seeded (`taps --reseed`).
- **Density thresholds** follow a golden-ratio order over cells (even spread
  at any density), over (0, 0.919999]: the engine's literal ramp needs every
  theta at or under 1 - 0.08, and 0.92 as a float is a hair above it.
- Deterministic everywhere: splitmix64, no `<random>` distributions, every
  number rounded onto a coarse grid (1e-4 ms, 1e-8 gain, 1e-6 theta/pan,
  0.01 Hz) so the pin is `==` across compilers. Release-emitted tables pass
  the Debug pin on AURORA; **macOS arm64 and Linux are untested**.

## Seeds and results -- every margin, at each type's default SIZE

Margins are >= 0 to pass. Units: separation ms; gaps fraction (0.02 = 2 %);
full band Hz under 1.5 kHz; Kuttruff, ceiling, (i), (ii), (iii) dB; (iv)
fraction under 0.25; centre pan under 0.25; gamma; LF.

Plate's column is the table after the owner's 2026-09-23 decision that a plate
is not a room: the room rules print n/a for it and its own three rules are
below the line.

| rule | Room s1 | Chamber s1 | Hall s2 | Cavern s1444 | Plate s243 | Ambience s31 |
|---|---|---|---|---|---|---|
| separation >= 0.9 ms (all 48, 14 sets) | 0.0593 | 0.0166 | 0.1279 | 0.1468 | n/a | 0.0049 |
| core gaps >= 2 % apart | 0.0024 | 0.0004 | 0.0020 | 0.0014 | n/a | 0.0001 |
| no full-band tap 1-8 ms | 300 Hz | 300 | 300 | none in zone | n/a | 300 |
| Kuttruff 2-20 ms (no dichotic bonus) | 3.34 | 4.34 | 6.95 | 13.12 | n/a | 3.23 |
| no tap above -15.3 dB | 1.21 | 1.91 | 6.84 | 11.94 | 8.89 | 0.46 |
| flam (i) after 25 ms, -12 dB | 0.22 | 1.20 | 0.64 | **-1.50 FAIL (known)** | n/a | 4.47 |
| flam (ii) no second onset (3 dB) | 2.26 | 2.40 | 1.10 | 0.55 | n/a | 1.36 |
| flam (iii) LOC, >= 3 dB below direct | 13.91 | 13.51 | 19.48 | 25.92 | n/a | 10.94 |
| flam (iv) inside 5 ms, present and <= 25 % | 0.174 | 0.184 | 0.185 | 0.215 | n/a | 0.192 |
| first tap near centre | 0.174 | 0.137 | 0.167 | 0.164 | n/a | 0.155 |
| gamma (rooms: ladder; Plate: >= 0 at 0-5) | 0.034 | 0.043 | 0.016 | 0.016 | 0.079 | 0.052 |
| lateral fraction 0.10-0.25 | 0.004 | 0.018 | 0.011 | 0.021 | n/a | 0.030 |
| Moorer sanity (Room) | 0.019 | -- | -- | -- | -- | -- |
| plate: onset <= 1 ms (ms) | -- | -- | -- | -- | 0.247 | -- |
| plate: heard peak in 0-5 ms (dB over next) | -- | -- | -- | -- | 0.269 | -- |
| plate: bands in order (ms) | -- | -- | -- | -- | 0.649 | -- |

gamma at VARIATION 0-6, default density: Room .925 .671 .463 .356 .249 .116
.105; Chamber .968 .790 .589 .399 .267 .105 .105; Hall .916 .762 .515 .449
.288 .131 .105; Cavern .921 .664 .467 .277 .212 .082 .105; Plate .964 .772
.606 .422 .295 .117 .105; Ambience .958 .731 .579 .408 .260 .091 .105. Also
asserted at DENSITY 0 and 100 % (the rooms: all >= 0, falling >= 0.05 a step;
Plate: >= 0 only). Mono loss at VARIATION 5 is therefore 10 log10((1 + gamma)
/ 2) = -2.7 to -2.5 dB.
VARIATION 6: comb delay 10 ms (Room, Chamber, Ambience) / 20 ms (the rest) at
the default SIZE, gain 0.90, gamma 0.105 -- held back per the orchestrator's
note until VARIATION 6's meaning is settled.

Other figures: loudest tap Room -16.5, Chamber -17.2, Hall -22.1, Cavern
-27.2, Plate -24.2, Ambience -15.8 dB. Heard ER energy inside 5 ms: 7.6 /
6.6 / 6.5 / 3.5 / 28.7 (Plate) / 5.8 %. Room against Moorer: 21 core taps (19),
first tap 3.73 ms (4.3), core span 3.73-90.1 ms, whole table to 98.1 ms (79.7).

## The two spec conflicts, not resolved silently

**Flamming (iii).** Implemented as 10 section 3 has it: ER energy in 100 ms
at the default ER fader, every tap on, no renormalisation, is (Room, Chamber,
Hall, Cavern, Ambience) 13.9 / 13.5 / 19.5 / 25.9 / 10.9 dB **beyond** the 3 dB it must be below direct
(so 16.9 ... 28.9 dB below). 11 section 6's dropped rule, measured, at the
default density: ER energy before 30 ms is Room 86.8 %, Chamber 73.9 %, Hall
73.3 %, Cavern 55.8 %, Ambience 95.2 % -- every room meets the old >= 50 % as
well (Plate, not held to either: 84.9 %, LOC 24.0 dB below direct). (iv), the small allocation inside 5 ms, has no number in
the spec; this pass asserts "present in every channel, and at most a quarter
of the heard ER energy", and says so in `ErAudit.cpp`.

**Lateral fraction.** Room 0.104, Chamber 0.118, Hall 0.111, Cavern 0.121,
Ambience 0.130 -- inside 0.10-0.25, so both 10 section 3 (0.10-0.35) and 11
section 6 (0.10-0.25) are met; Room's margin is thin (0.004). This is the
**room's** LF: ISO/Barron, images weighted by lateral cosine squared, core
taps, direct at the geometry's own 1 m / d. Plate is not held to it (owner,
2026-09-23); its figure is 0.090. What the *stereo output* carries -- side over
mid, 125-1000 Hz, VARIATION 2 -- is reported beside it: 0.123 / 0.152 / 0.153 /
0.407 / 0.222 (Plate) / 0.088. Split offsets of 0.24-0.6 ms decorrelate above about a
kilohertz, not in the low-mids 10 section 3 asks for; offsets of 0.8-2 ms were
tried and **cannot be placed** (with a common shared time, each slot's three
arrivals need ~3.2 ms of the window, and 48 of those exceed 100 ms). That is
an open design question, not solved here.

## Plate -- not a room (owner, 2026-09-23), and its own rules

**Owner decision, 2026-09-23:** "plate verbs are a physical metal plate model,
not a room model. room rules shouldn't apply." So Plate is no longer held to
the separation and gap rules, the 1-8 ms full-band ban, Kuttruff, the four
flam rules, the centre rule, the lateral fraction or Moorer. It is still held
to what binds any source -- the -15.3 dB tap ceiling (margin 8.89 dB), taps
inside the window, gamma >= 0 at VARIATION 0-5 (margin 0.079), the pin -- and
to three plate rules from Frosty's plate research, the structural ones:

- **instant onset** -- first tap at or under 1 ms: 0.75 ms, margin 0.25 ms;
- **front-loaded** -- the heard energy peaks in the first 5 ms window: margin
  0.27 dB over the next window (thin); 28.7 % of the heard energy is inside
  5 ms;
- **dispersion order** -- each darker band's first arrival after the
  brighter's: margin 0.65 ms.

The model now: a 2 m x 1 m plate, driver 0.3 m from pickups 0.6 m apart, 2D
images of orders 0-5 where **every path arrives once per band** at that band's
group speed (438 / 196 / 98 / 69 m/s), taps kept 0.25 ms apart rather than
0.9, the infill starting at 0.5 ms, every dark-band slot split at every
VARIATION (a plate's two pickups always differ in the lows), window 45 ms at
the default SIZE. Seed 243 -- the first from 1 that passes; only 1 seed in 243
could be placed, so the placement is tight and a generator change will move
the seed.

| against the research (Est unless said) | before (seed 3, room rules binding) | after (seed 243) |
|---|---|---|
| first tap | 0.80 / 0.56 ms (L / R) | 0.75 ms |
| heard energy inside 5 ms | 23.2 % | 28.7 % |
| span (research about 30 ms) | 0.80-117.7 ms | 0.75-44.8 ms |
| band first arrivals, x of band 0 (1/sqrt f: 1 / 2.24 / 4.47 / 6.32) | 0.8 / 9.0 / 24.9 / 12.8 ms -- out of order | 0.75 / 1.70 / 3.15 / 4.86 ms = 1 / 2.25 / 4.18 / 6.46 |
| band mean arrivals | 0.8 / 22.7 / 62.5 / 77.8 ms | 7.9 / 19.0 / 25.7 / 33.9 ms |
| VARIATION 5 mean L-R, high band (research under 1 ms) | 0.48 ms, all bands | 0.40 ms |
| VARIATION 5 mean L-R, 2 kHz / 500 Hz bands | -- | 1.32 / 3.87 ms |
| VARIATION 5 mean L-R, low band (research 5-10 ms) | 0.48 ms | 3.38 ms |

**What Plate still does not meet (reported, not asserted):** the span is 45 ms
against about 30 -- a 40 ms window could not be placed at any of 200 seeds;
the low-band L/R offset is 3.4 ms against 5-10 -- 0.9 m pickups (which would
give about 5 ms) could not be placed at any of 800 seeds at 45 or 50 ms; and
the front-loaded margin is thin at 0.27 dB. All three are limits of fitting 48
taps a channel with split arrivals at every VARIATION, not of the physics.

## Where the tables sit against the research rows (default SIZE)

| type | first tap | first full-band tap | row | Lindau t_mp50 |
|---|---|---|---|---|
| Room 12 m | 3.73 ms (dark) | 9.06 ms | -- | 36 ms |
| Chamber 18 m | 2.93 ms (dark) | 9.30 ms | 2-6 ms (Est) -- inside | 49 ms (row 12-30 Est) |
| Hall 34 m | 1.44 ms (dark) | 10.72 ms | 15-30 ms (M) -- **earlier than the halls** | 93 ms (row 90-130) |
| Cavern 55 m | 0.74 ms (dark) | 9.38 ms | a few ms (Est); sparse, late echoes -- matches | 168 ms, extrapolated (row 150-400) |
| Ambience 8 m | 4.12 ms (dark) | 8.32 ms | 1-4 ms (Est) -- just outside | 29 ms (row 20-30) |

Hall's first tap is the floor-bounce proximity allocation (1.4 ms, dark),
and its first full-band tap is at 10.7 ms: both earlier than measured halls'
15-30 ms. Not forced; the proximity rule (iv) wants something inside 5 ms,
and a listener 6 m from a side wall is what passes flam (i). Ambience against
EBU Tech 3276 (control rooms, >= 10 dB below direct in the first 15 ms): the
loudest tap is -15.8 dB against the dry at a 0 dB fader (-19.8 at the default
-4 dB), so it passes against the dry; against the geometry's own direct
(3.0 m, -9.5 dB) it is only 6.2 dB below. Mixing time is not measurable from
the tables (it needs the diffuser and tail); the Lindau figures are the
formula at each default volume.

## Cavern fails flam (i): a known failure, for the ear

Cavern's image field at 55 m has the floor bounce, two early walls, then
nothing for ~95 ms and a cluster at 108-135 ms (orders 2-4), which stands
above the little energy before 25 ms. Seed 1444 fails (i) by 1.50 dB and
passes every other rule. At 55 m, orders 1-3 give 26 images in the window and
18 distinct -- fewer than 21 -- so Cavern goes to order 4 (32 images). The
research row calls Cavern's early field "long, sparse, with flutter", which is
what fails (i).

**Owner decision, 2026-09-23:** this is an ear call at the listening
checkpoint -- Cavern at 55 m against a smaller SIZE -- not a desk fix. No
more seed searching. The test carries it by name as a known failure, held to
a -1.6 dB floor so it cannot quietly get worse.

## Non-vacuity, observed (all on AURORA, each rebuild exit 0)

- Hand edit of `ErTableData.inc` (Room, VARIATION 3, right, first tap, band
  3 -> 0): **3 red** -- the pin, "Room: no full-band tap 1-8 ms", "Room: flam
  (iv) inside 5 ms". Restored; green.
- Audit weakened by hand in `ErAudit.cpp` (separation threshold 0.9 -> 0,
  Kuttruff -8 dB -> +30 dB): **2 red** -- the two hand-broken-table checks for
  those rules. Restored; green.
- **Plate, after the owner decision:** hand edit of `ErTableData.inc` (Plate,
  VARIATION 0, left, first tap, band 0 -> 3; Plate, VARIATION 1, left, a tap
  at 15.2 ms at 12 m, gain -> 0.5): **4 red** -- the pin, "Plate: tap <= -15.3
  dB", "Plate: plate: peak in 0-5 ms", "Plate: plate: bands in order".
  Restored; green. The onset rule is proven by the suite's own break (a Plate
  channel shifted to start at 2 ms).
- In the suite permanently: one hand-broken copy of Room's table per room rule
  (13), one of Plate's per plate rule (3), plus checks that Plate is held to
  no room rule and Room to no plate rule.

## For the engine half and integration

- `windowMs` at 12 m is Room 100, Chamber 66.7, Hall 70.6, Cavern 43.6, Plate
  24.5 and **Ambience 150 -- above its 100 ms clamp**, because every table
  fills its clamp at its default SIZE. The clamp must be applied by dropping
  taps at every size, the reference included.
- Core taps have theta 0. The audits treat them as always on (10 section 3);
  the engine's literal ramp gives them weight 0 at DENSITY 0 exactly, and the
  renormalisation's denominator 0 there.
- Level rules are on the table's own gains (0 dB fader, before density
  renormalisation). If the renormalisation's E is not the core's energy, the
  gains at DENSITY 0 move and these margins move with them.
- Band 3 (1.2 kHz at the default SIZE) carries every tap inside 8 ms.
- The panel still draws `TapTables.h`; the tail formula still calls the
  placeholder `erSpanMsAt (float)`.

## Tests

Build of `reverb_dsp_tests measure_reverb` (Debug) exit 0; `reverb_dsp`
passes. Full DSP-only suite on AURORA, after a targeted build of all 18 test
targets plus measure_reverb that exited 0: ctest 18/18 passed (tune_hardtune_target
disabled by design), exit 0.
