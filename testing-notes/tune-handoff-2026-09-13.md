# Handoff: BMO Tune RT after rounds five, six and seven

Written on **AURORA**, 2026-09-13, at the end of a session that reviewed the
2026-09-11 handoff, found what was wrong with it, and **measured six things
and threw five of them away**. The one that survived is not a fix -- it is a
diagnosis, and it is the first thing in a week that has agreed with Frosty's
ears on the first try. This supersedes
`tune-handoff.md` for everything after `9b18577`; that file still has rounds
three and four and is still worth reading first for how the engine works.

Branch **`bmo-tune-work`**, off `integration`, worktree
`../bmo-mix-rack-333-tunework`. `WORKFLOWS.md` on `integration` has the branch
map and the stages.

---

## The short version

**The best-sounding build is still `31b30ef` -- "round four", the 4 ms rest,
no prediction.** Nothing built since has beaten it by ear. One change since
is a draw and stays in; three are losses and are out.

**And the pops are now diagnosed.** Round seven swept the rest from 2 to
8 ms; Frosty heard all four as *"audible pops at an unacceptable level,
almost indistinguishable from one another"*. That closed the axis three
rounds had been spent on -- and it pointed at what was left. Five of the six
pops he timestamped fire at the **same millisecond at every rest**, and in the
40 ms before each of them the detector's own f0 spans a ratio of 1.76 to
3.94, against 1.00 to 1.02 before the quiet ones.

**The audible pops are the detector losing the period, and the engine
splicing on a period that is not the singer's.** Six against six, cleanly
separated, first try -- where the splice count and the splice landing error
were each refuted against these same ears.

So the work is in `Detector`, not `ClassicEngine`, and not in any constant.
Nothing in the engine can fix it: the search reach that would rescue an
octave-wrong period is exactly the reach that thrashes (see the table below).

---

## Which build won the blinds

Every round: Frosty on AURORA, Apollo Twin X gen 2 into HEDD Type 20 mk2,
answers written before the key was opened.

### Round five, 2026-09-12 -- the prediction

Antares, round four (`31b30ef`), and the prediction build (`b4bfc73`).

| group | order |
|---|---|
| Failure 0 ms | Antares > **prediction** > round four |
| Failure 20 ms | Antares > round four > **prediction** |
| Fuji 0 ms | round four > **prediction** > Antares |
| Fuji 20 ms | Antares > **prediction** > round four |

**Two all.** The prediction also drew the round's one new complaint,
"audible formant shift", on Failure 20 ms.

### Round six, 2026-09-13 -- the splice landing, and a note transition

Antares, round four, prediction + splice landing (`1a289c9`), and that plus a
1 ms note transition.

| group | order |
|---|---|
| Failure 0 ms | Antares > **round four** > landing > +1 ms |
| Failure 20 ms | Antares > (**round four** = landing) > +1 ms |
| Fuji 0 ms | (**round four** = Antares) > landing > +1 ms |
| Fuji 20 ms | **round four** > landing > Antares > +1 ms |

**Round four: 3 wins, 1 tie, 0 losses.** The 1 ms transition was worst of
four in all four groups.

### Round seven, 2026-09-13 -- the rest, from 2 to 8 ms

Four arms, same build, nothing differing but the rest.

> *"all provided options have audible pops at an unacceptable level, almost
> indistinguishable from one another"*

No ranking, and none needed. Across those arms the splice count runs
49 / 36 / 31 / 31 on Failure and 33 / 22 / 10 / 11 on Fuji -- a fourfold
change in window room and better than a threefold change in splices on Fuji
-- and they cannot be told apart. **The rest is not what makes a pop.**

It also settles the deeper rest: 6 ms measures best on correction (residue
0.94 c against 1.24, Fuji's splices halved) but the difference is inaudible,
so there is no case for spending 1.7 ms of latency on it. **The rest stays at
4 ms.**

### Standing

- **Best BMO by ear: round four (`31b30ef`).** Never beaten.
- **Current HEAD** is round four + the prediction. Against round four it is a
  draw (2-2, round five). It stays because it is a large measured improvement
  -- correction lag 3.19 -> 0.71 ms, vibrato residue 3.35 -> 1.24 cents,
  which meets Antares' 1.30 for the first time -- and costs nothing by ear.
  **It is not "earned" and must not be described as one.**
- **Antares wins Failure**, at both speeds, in every round it has been in.
- **BMO beats Antares on Fuji**, in both rounds, at both speeds.
- The worst thing left, in Frosty's words: **"Pops on Failure."**

---

## Avenues tested and failed

Do not re-derive these. Each cost most of a day between them, each has its
numbers in the commit that tried it.

| what | result | where |
|---|---|---|
| **A bigger constant rest** (8 ms) | Trades octaves against each other -- A2's residue 5.85 -> 2.10 c, A3's 0.95 -> 3.13. A constant pays the alignment debt at one pitch only (~290 Hz). Not shipped. | `2be99bb` |
| **A period-proportional rest** (0.5-1.25 x T) | Worse on every axis: latency no better, lag 0.71 -> 1.8-3.5 ms, residue 1.24 -> 2.9-3.8 c, splices 38 -> 63-74, and it clears one Waves cell of six. Mechanism in the tree, `kRestPeriods = 0`. | `5c58198` |
| **Note transition smoothing** (0.5-10 ms) | More splices, not fewer: 10 ms nearly doubles them. At 1 ms free but pointless. **Heard as worst of four in all four groups.** Kills the formant hypothesis. `noteTransitionMs = 0`. | `fc129b1` |
| **Splice similarity search** (WSOLA) | Landing error improved everywhere and the worst splice in the take went 1.97 -> 0.37. **Heard as 0 wins, 1 tie, 3 losses.** Reverted. | `1a289c9`, `14bbae7` |
| **Splice COUNT as the metric** | 120 -> 38 across round four with no audible change. Refuted. | `00d4333` |
| **Splice LANDING ERROR as the metric** | Built to replace the count. Improved; sound did not. Refuted. | `14bbae7` |
| **"The pops are dropouts being corrected"** | Frosty's hypothesis, tested: of 38 splices only 4 fall within 100 ms of a voicing gap. Fixing voicing leaves 34 of 38 untouched. | `00d4333` |
| **A full period of search reach** | Every period multiple correlates equally, so shimmer picks a different cycle: landing error on a *correct* period 3e-10 -> 0.13, splices 8 -> 15, sine THD+N to +46 dB. | `1a289c9` |
| **A one-pole on the prediction slope** | Only ever cost, monotonically (0.97 ms / 1.23 c at 0; 1.15 / 1.47 at 5 ms). `predictSlopeMs = 0`. | `b4bfc73` |
| **A flat 1.5-hop staleness correction** | Overshoots every vibrato into negative lag, residue back to 1.46 c. Replaced by anchoring the slope to the estimate it was measured at. | `b4bfc73` |
| **The REST as a lever on the pops** (2, 4, 6, 8 ms) | Splices 49/36/31/31 on Failure and 33/22/10/11 on Fuji; **heard as indistinguishable, all unacceptable.** Window room is not what makes a pop. Closes three rounds of work. | `7790c8d` |
| **Confirming the period before the engine acts on it** | A real inconsistency, and fixed -- but not the cure. The octave errors last several hops, so confirmPitch rightly accepts them after one. Landing error worst 1.97 -> 1.51; the pops all still there. | `94648f0` |

---

## What is true, and stands

1. **Every pop is a splice.** Frosty timestamped seven on Failure; all seven
   landed on one, six within 61 ms. There is no second mechanism.
2. **Only 7 of 38 splices are audible** -- and what separates them is now
   known. In the 40 ms before an audible one the detector's f0 spans a ratio
   of 1.76 to 3.94; before a quiet one, 1.00 to 1.02. **The audible splices
   are the ones taken while the detector has lost the period.**
   `bmo-tune-field` reports it: 12 of 37 on Failure at 20 ms, 4 of 16 on
   Fuji. It is the third measure tried against these ears and the first that
   agreed with them, so it is the one to drive work from.
3. **The correction was landing late because the read and the detector were
   not aligned.** The detector's estimate refers to 1.07 x T behind the
   newest sample; the engine read a flat 4 ms behind. The residue is
   `(analysis lag - read delay) x pitch slope`. Verified 1:1 by experiment.
   Fixed by prediction, which costs no latency.
4. **The latency rule is a curve, not a scalar.** Waves' delay is ~1.68 x the
   period. Held to one number it is wrong in both directions. `References.h`
   carries the measured curve; `ceilingMsAt` reads it.
5. **The rule is unreachable at the top of the range.** At A5 Waves' whole
   delay is 0.709 ms, less than one period (1.136 ms); BMO's floor plus one
   whole-cycle excursion is 1.491 ms. Arithmetic, not effort.
6. **Nothing holds the read at the rest.** It is a window edge and a homing
   target, and homing only runs on unvoiced settled material. Inside a phrase
   the read sits where correction history left it. This is why the
   period-proportional rest could not work.

---

## Open, in the order worth doing

1. **The detector's octave and twelfth errors. This is the whole job now.**
   It is what the audible pops are, and everything else on this list is
   smaller. On Failure the detector reads 303 Hz as 683, 186 as 734, 219 as
   809 -- and 12 of 37 splices are taken while it is doing so.

   Where to start: `bmo-tune-field --splices` and the "LOST the period" line
   locate them; the five that Frosty hears are at 2.893, 6.135, 7.019, 14.455
   and 17.410 s on Failure at retune 20 ms. `Detector::preferWholeCycle` and
   guard 4 (`modules/tune/AGENTS.md`, "Where the code departs from the spec")
   are where the existing octave work lives, and the shoot-out notes record
   what guard 4 already bought: 8.5 % of frames reading a twelfth up, down to
   0.7 %. What is left is the residue of that, on scoops.

   **Do not try to fix this in the engine.** The search reach that would
   rescue an octave-wrong period is exactly the reach that lets shimmer pick
   the wrong cycle and thrash -- measured, `1a289c9`.
3. **The live-monitoring budget.** Frosty's to set. Until it exists the
   per-note latency rule can never be green; it is now an open check under
   `--target` rather than a build blocker.
4. **`hardtune_target`'s last check**: worst correction lag 1.97 ms at A2
   against Antares' 1.66. Every other vibrato is inside half a millisecond.
5. **Bass and Instrument declare 55 Hz** while the latency curve stops at E2
   (Frosty, 2026-09-12: "it's a vocal tuner so no need to drop below E2").
   One of the two has to move. Low priority, Frosty's call.
6. **Waves across block sizes** -- measured at 128 and 2048 only, and the
   whole ceiling curve rests on it.

---

## Things that were wrong in the tree, now fixed

- **`field-audio/` was not gitignored.** The rule "field audio, blind sets and
  the licensed fonts are never committed" came across with Tune as prose; the
  `.gitignore` line did not. On a public fork. (`2d3af87`)
- **`bmo-tune-latency` tested the rest delay** against a ceiling documented as
  a worst case, so it passed whatever the engine did. (`7835096`)
- **The stimulus held a correction only on A3 and D3** -- a 2.3-octave plugin
  judged through a five-semitone window. Now E2 to A5. (`3c95284`)
- **The ruler's envelope was too short** for its own lowest note: 10 ms
  against A2's 9.09 ms period, reading an ideal corrector 0.31 ms out. Now
  25 ms with 40 ms markers; marked-segment correlations 0.78-1.00 -> 0.95-1.00.
- **The latency rule was cited to the root `AGENTS.md`** by three files and was
  not in it. (`2be99bb`)
- **The regression ratchet was two generations stale** (6.22 ms / 6.61 c, from
  before the 4 ms rest), so it would not have noticed that rest being
  reverted. Now 0.71 / 1.24.
- **The law copied the engine's rest once** at `applyParams`; correct only
  while that rest was constant. Both read `contract::liveRest` now.
- **The law confirmed the pitch but not the period.** `period` was written
  from `e.period` unconditionally, so on an unconfirmed jump the law ignored
  the pitch while the engine was handed a period half as long -- and a halved
  T collapses the window under the read pointer and forces a splice on the
  spot. Both come from the accepted estimate now. (`94648f0`)
- **The prediction only worked forward.** The gap between the estimate and
  the read points backward whenever the engine rests past it, which is the
  ordinary case above ~280 Hz. Refusing that half is what made a deeper rest
  look bad: residue at 12 ms was 5.10 c and is 1.82 with both halves.
  (`5fa6f97`)

---

## Rules that still hold

- Name the machine: AURORA (laptop, `C:\Users\thesp`) or ICE QUEEN (desktop,
  `C:\Users\stefr`).
- Measure, never judge by ear alone -- **and never by measurement alone**,
  which is this session's lesson. Nothing is fixed that a test did not first
  fail on; Frosty hears every fix before it is called fixed.
- The schema is frozen; new meaning gets a new id (`kRetiredIds`).
- No FFT in the correction path; nothing allocates after `prepare()`; the host
  is told 0.
- Field audio, blind sets and the licensed fonts are never committed -- and
  now `.gitignore` agrees.
- Pushes only with Frosty's say. A CI round trip is about 22 minutes.
