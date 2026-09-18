# Review: the 4 ms rest, the correction lag, and a broken latency rule

Written on **AURORA**, 2026-09-11, reviewing `bmo-tune-work` at `9b18577`
(rounds three and four, `testing-notes/tune-handoff.md`). Everything below was
re-measured on AURORA against that commit; nothing is quoted from the earlier
notes without checking it.

Two findings. The first explains why Antares and Waves still clear BMO on
every group but one. The second is a live violation of the latency rule that
both gates were blind to.

## 1. The rest is an alignment, and 4 ms only aligns one pitch

**The mechanism.** `TuneCore::process` pushes the detector the newest sample
while the engine reads `rest` samples behind it. So the correction applied to
any output sample was computed from input the detector saw a while ago, and
applied to input the engine is reading somewhere else. The residue off the
note is the difference:

```
out - target  =  (detector's analysis lag - engine's read delay) x pitch slope
```

The detector's analysis lag is **one period**, not a constant: `fullNsdf`
correlates a one-period window against a block one period older
(`Detector.cpp`), and the hop is a quarter period. The engine's read delay is
`contract::kLiveRestMs`, a flat 4.0 ms.

**The evidence, from numbers already in the tree.** From the per-segment table
in `latency-and-lag-2026-09-11.md`, at the old 0.40 ms rest:

| note | T (ms) | BMO lag | BMO delay | sum | **sum ÷ T** |
|---|---:|---:|---:|---:|---:|
| A2 | 9.09 | 8.95 | 0.85 | 9.80 | **1.078** |
| D3 | 6.81 | 6.41 | 0.66 | 7.07 | **1.038** |
| E3 | 6.07 | 5.52 | 1.01 | 6.53 | **1.076** |
| A3 | 4.55 | 3.98 | 0.89 | 4.87 | **1.071** |

Flat to 4 % over two octaves. The lag the shoot-out measures is one period
less the rest.

**The evidence, by experiment.** `kLiveRestMs` set to 8.0, nothing else
changed, rebuilt, `hardtune --target` re-run on AURORA:

| vibrato | at rest 4.0 | predicted at 8.0 | **measured at 8.0** |
|---|---:|---:|---:|
| A2 | 6.069 | 2.069 | **2.116** |
| D3 | 3.392 | −0.608 | **−0.606** |
| E3 | 2.478 | −1.522 | **−1.537** |
| A3 | 0.807 | −3.193 | **−3.203** |

Adding delay subtracts from the lag 1 for 1, to within 0.05 ms on every note.
It is pure alignment; no splice, window or "room to move" term appears.

**Why a constant cannot be the fix.** RMS off the note at the same two rests:

| rest | A2 | D3 | E3 | A3 | mean |
|---|---:|---:|---:|---:|---:|
| 4.0 ms | 5.85 | 3.32 | 3.27 | **0.95** | 3.35 |
| 8.0 ms | 2.10 | 0.82 | 2.09 | **3.13** | 2.04 |

A3 gets 3.3x worse. A constant rest picks which octave to sacrifice. It pays
the debt in full at about 290 Hz and nowhere else — which is why the lag
tracks the period, and why Fuji cleared while Failure did not.

**How the others solve it.** Same table, same renders:

- **Waves' in-tune delay is T + 1.26 ms** (1.41, 0.88, 1.38, 1.38 above the
  period at the four notes). It rests one period back — period-proportional —
  and lands at 0.92–2.13 ms of residual lag. That is what its large ceiling
  buys.
- **Antares' analysis lag is a flat 4.42 ms** (lag + delay = 4.66, 3.82, 4.53,
  4.65; spread 0.84). A constant delay aligns Antares at every pitch because
  its detector's lag is constant. Copying a constant from Antares cannot work
  for a detector whose lag is a period.

## 2. The splice curve was the wrong thing to tune against

`LatencyContract.h` chose 4 ms as "where the splice curve flattens": 120, 50,
35, 31 splices for rests of 0.40, 2, 4, 6 ms. But under a **sustained**
correction the splice rate has no rest and no window width in it. The pointer
drifts `|1 - rho|` per sample and each splice moves it exactly `T`, so

```
splices per second = |1 - rho| x fs / T
```

Widening the window removes *transient* excursions only. On Failure the singer
sits on D# in D major — permanently ~50 cents from either allowed note — so
`|1 - rho| ~ 0.029` is sustained; at T ~ 284 samples that is ~4.5 splices/s
whenever that note is held. 35 → 31 is the count asymptoting to that floor,
not a benefit running out.

So 4 ms was set by a metric that had stopped responding, while the metric that
was still falling steeply — the lag, 3.19 → 1.20 from 4 to 6 ms — was read as
a side benefit. **The remaining 38 pops on Failure will not yield to more
window.**

The fix was already written down and then not taken: `modules/tune/AGENTS.md`
said *"delay the audio so the estimate is on time — which the latency rule now
allows"*. `31b30ef` did delay the audio, but as a constant, and was written up
as a splice fix, so nothing downstream checked it against what it was actually
doing and `hardtune_target` stayed off.

## 3. The latency rule's ceiling is a curve, and BMO is over it at the top

Two gates were meant to hold this rule, and neither did:

- **`bmo-tune-latency` tested the wrong column.** It compared `row.restMs` —
  the *in-tune* delay, 4.00 ms in every cell — against
  `kWaves.trueLatencyMs`, which `References.h` documents as "worst delay, in
  tune **or** correcting". It computed `worstMs` and never tested it, so it
  printed `every cell's rest delay is under the ceiling` and exited 0.
- **The stimulus held a correction only on A3 and D3.** `HardTuneTests` read
  6.53 ms for a 2.3-octave plugin through a five-semitone window.

**A correction to the first version of this note.** Fixing the first gate
alone, and holding every cell to Waves' 10.62 ms, reported 54 cells over at
the *bottom* of the range — worst 15.33 ms at E2. That was wrong, and wrong
for the reason this note had already given two paragraphs earlier: 10.62 ms
is a scalar, and what it bounds is pitch-dependent for every tuner in the
comparison. It is Waves' figure **at A2**. Comparing BMO's low notes against
it is not the rule.

Measured properly — marked segments at E2, A2, D3, A3, A4, A5, all three
tuners through the same stimulus, same code, delay while correcting:

| note | T (ms) | **BMO** | Antares | **Waves** |
|---|---:|---:|---:|---:|
| E2 | 12.13 | 9.18 | 10.74 | **19.22** |
| A2 | 9.09 | 8.03 | 8.06 | 13.80 |
| D3 | 6.81 | 3.88 | 5.92 | 10.09 |
| A3 | 4.55 | 5.97 | 5.49 | 7.05 |
| A4 | 2.27 | **5.01** | 3.59 | **3.82** |
| A5 | 1.14 | **4.61** | 2.96 | **0.71** |

**Waves' delay is 1.68 ms per ms of period**, intercept −1.2 — almost purely
proportional, with essentially no fixed floor (0.71 ms at A5). BMO's is a
constant 4 ms plus its excursion. So the two cross, at about **C3**: BMO is
comfortably under Waves below it, and over it above, by **3.90 ms at A5**.
186 cells of the sweep, all at the top of the range — the exact inverse of
what the scalar said. The crossover is soft: the lowest failing cell, C3,
misses by 0.04 ms. It is hard by A3, which misses by 1.1 ms.

Against Antares the same shape: BMO is under at E2, A2 and D3, over at A3,
A4 and A5.

Worst against worst, BMO is still the least late of the three (9.18 against
Antares' 10.74 and Waves' 19.22) — but that comparison is dominated by the
lowest note in the stimulus and says nothing about the rest of the range. It
is kept as a check because it is the rule as written; the per-note check is
the one that means anything, and it fails.

**The cause is the same constant, plus `hi = rest + T`** in
`ClassicEngine::process`: an absolute delay of rest plus a whole period, on a
rest that is already 4 ms. At A5 that is 4 ms of rest where Waves spends 0.7.

## What this means for the fix

**The delay route is not simply "spend the headroom".** Aligning by delay
alone needs rest ~ 1.07 x T: 9.7 ms at A2, 13.0 ms at E2, but only 1.2 ms at
A5. That is a rest proportional to the period — which is what Waves does, and
under Waves' curve there is room for it at the bottom of the range. What there
is no room for is the *constant*: at A5 the rule allows 0.71 ms and the rest
alone is 4 ms. So the same change has to make the rest track the period in
both directions, down at the top of the range as well as up at the bottom.

I tried it anyway, to price it: `rest = 1.14 x T` with a +/- T/2 window took
the mean lag from 3.19 to **0.54 ms** and the true latency to **13.44 ms**,
failing the rule. It also exposed a third thing worth knowing:

**The engine has no mechanism to seek a rest.** The window only *bounds* the
read pointer; nothing drives it to a target delay. Homing exists but is gated
on `settled` — unvoiced **and** correction faded (`TuneCore.cpp`) — so inside
a continuous phrase the delay is a free-running consequence of correction
history. In that run the in-tune delay at D3, E3 and A3 never moved off
4.4 ms at all. This is also the likeliest reason Failure and Fuji differ:
Failure is denser, so the pointer rarely re-homes and sits at an arbitrary
offset for whole phrases.

**Prediction is the cheapest route, and it is the only one that helps at the
top of the range.** The analysis lag is ~1.07 x T and known at run time, so
`CorrectionLaw` can extrapolate `pitchIn` forward by it at no latency cost at
all — which is the only kind of fix available at A5, where the rule allows
0.71 ms in total. Not started; it needs a failing test and then Frosty's ears,
as everything here does.

A period-proportional rest is still worth doing alongside it, for the bottom
of the range and for the window: it is what Waves does, and `hi = rest + T`
has to go whatever else happens.

**The rule is now a curve** (`references::ceilingMsAt`), read off Waves'
measured delay at each note and interpolated in the period. Both judgements
that were open here are Frosty's and were answered on 2026-09-11:

- **Per note**, not worst against worst. Both are checked; worst against
  worst passes today with 10 ms to spare and catches nothing.
- **The curve stops at E2** -- "it's a vocal tuner so no need to drop below
  E2" -- so it is held flat below rather than extrapolated, and nothing below
  E2 is judged.
- **Waves is the proxy, live monitoring is the point**: per note is preferred
  "so long as it remains fast enough for live monitor we can adjust". No
  budget figure is encoded, deliberately -- the only thing a number would do
  today is turn a red test green without changing the plugin. It is written
  into the root `AGENTS.md` as available when a change actually needs it.
  Worth noting that the route recommended below, prediction, costs no latency
  at all and so never needs to invoke it.

Two things the E2 floor leaves hanging, neither settled:

- **Bass and Instrument declare 55 Hz** (`params.h`, Frosty's 2026-09-10
  call), two and a half tones below E2. Those cells are held to E2's ceiling,
  which is generous rather than measured. Either the ranges come up or the
  curve goes down.
- **`bmo-tune-latency` understates below E2 anyway.** It holds a note 35
  cents sharp for 0.6 s, and at A1 the read needs ~0.9 s to drift a whole
  period, so the sweep plateaus at ~15 ms where the window actually allows
  rest + T = 22.2 ms. The low cells are transient-limited, not steady state.
  It also shows lock time growing to 40 ms at A1, which is its own question.

## What this commit changes, and what it does not

Changed: the documentation that was wrong (below), the `bmo-tune-latency`
gate, the stimulus and its ruler, `References.h`, and the `HardTuneTests`
ratchet plus the new per-note gate. **No DSP was touched.** The engine, the
detector and the correction law are byte for byte as they were at `9b18577`.

`ctest`: 13 of 14 pass. `tune_hardtune` is red on the per-note latency rule,
which is the point of it.

Corrected claims, all of which had gone stale when `31b30ef` landed:

| where | said | is |
|---|---|---|
| `README.md` | costs 0.4 ms; 3.8 ms at worst; "the least late of the three tuners" | 4.0 ms; 9.18 ms on the stimulus; least late worst-against-worst, **later than both above A3** |
| `ClassicEngine.h` | rests 19 samples, 0.4 ms | 192 samples, 4.0 ms |
| `TuneCore.h` | runs 0.4 ms behind at rest | 4.0 ms |
| `modules/tune/AGENTS.md` | Live floor 0.40 ms, meets spec's <= 1.5 ms | 4.00 ms, **fails** that gate |
| `modules/tune/AGENTS.md` | true latency 3.82 ms; lag 6.22 ms mean | 9.18 ms; 3.19 ms mean |
| `modules/tune/AGENTS.md` | `bmo-tune-latency` "checks it per semitone" | it checked the rest, not the rule |
| root `AGENTS.md` | (cited by three files as holding the latency rule) | did not mention it at all; now does |
| `References.h` | Antares 6.49 ms, Waves 10.62 ms true latency | 10.74 and 19.22, on a stimulus that reaches the low notes |

Minor, also corrected: `LatencyContract.h` said 35 splices at 4 ms where the
handoff says 38, for the same configuration; and "every voice cell resting at
exactly 4.000 ms" holds at 48 kHz only — at 44.1 kHz, which both shoot-out
takes are, `liveRestSamples` gives 176 samples = 3.991 ms.

## What the prediction did (2026-09-12, AURORA)

Built, measured, **not yet heard**. `CorrectionLaw` carries the estimate
forward over (analysis lag − read delay), which costs no latency.

| at retune 0 | before | after | Antares |
|---|---:|---:|---:|
| correction lag, mean | 3.19 ms | **0.71 ms** | −0.24 ms |
| correction lag, worst | 6.07 ms (A2) | **1.97 ms** (A2) | 1.66 ms |
| vibrato residue, mean | 3.35 c | **1.24 c** | 1.30 c |

`hardtune_target`'s residue check passes for the first time. Its worst-lag
check is the only thing left in that suite, and it is A2 alone — D3 reads
−0.06 ms, E3 0.49, A3 0.42.

On the shoot-out takes it costs nothing measurable:

| | Failure 0 / 20 ms | Fuji 0 / 20 ms |
|---|---|---|
| splices | 44 → **41** / 38 → 38 | 23 → **22** / 15 → 16 |
| flips back under 80 ms | 107 → 108 | 39 → **31** |
| dropouts under 80 ms | 15 → 15 | 12 → 12 |

CPU 0.934 % median at 48 kHz / 128, unchanged within noise.

Four things about the shape of it, each of which cost a measurement and any
of which someone could undo without noticing:

- **The slope is a median of three differences.** A one-evaluation move is a
  step or detector noise; predicting on it overshoots by what it stepped (a
  40 cent step at retune 3 ms cut the measured 10-90 settling from 6.6 to
  3.2 ms).
- **It is extrapolated from the estimate it was measured at**, not the newest
  one — a median returns a slope from ~1.5 hops back. Spending it as if it
  were current costs 1.2 ms of worst-case lag at A2; adding a flat 1.5-hop
  correction instead overshoots every vibrato into negative lag.
- **A prediction that lands further than `predictMaxCents` from the estimate
  is dropped, not clamped to it.** Clamped, it fires as an error rather than
  a guard after a pitch move too small for `confirmPitch` to call a jump:
  0.71 ms of mean lag became 1.94, and 1.24 c became 2.41.
- **The smoother on top of the median is off**, because it only ever cost,
  monotonically. The sweep is in `CorrectionSettings`.

## Pick up here

1. **Hear it.** The prediction is in and measured (above) and has never been
   listened to. That is the next thing, ahead of any more building: a round
   five against Antares on both takes, at 0 and 20 ms.
2. **The worst-case lag at A2**, 1.97 ms against Antares' 1.66 -- the last
   check in `hardtune_target`. A2 is where the hop is longest, so the slope
   is coarsest exactly where the most is being predicted.
3. **Bound the window.** `hi = rest + T` is an absolute delay of rest plus a
   period; Waves' correcting delay barely exceeds its own in-tune delay. This
   is separable from the alignment work and can go first.
4. **Bass and Instrument against the E2 floor** -- they declare 55 Hz and the
   rule now stops at E2. One of the two has to move; and the sweep's figures
   down there are transient-limited, so they need a longer held note before
   they mean anything.
5. **The 4 ms rest has still never been felt.** Unchanged from the handoff:
   the blind sets align it away and the installed VST3 on AURORA is 0.1.
6. **Waves across block sizes** is still open from the handoff — measured at
   128 and 2048 only, and the whole ceiling curve is built on it.
