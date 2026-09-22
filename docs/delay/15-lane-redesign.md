# BMO Dwell — the lane redesign, and what stage 2 becomes

Decided by Frosty on AURORA, 2026-09-21, after the stage 1 panel was built and
rejected three times. **This document supersedes parts of `10` and `13`, and it
changes the parameter table in `11` §3.** Those three have NOT yet been
rewritten; read this first and treat their THROW, FREEZE and schema sections as
out of date.

## Why the panel kept failing

Measured, not felt: the module carried 20 parameters in a 280 px column — 7.1
controls per 100 px, against BMO EQ's 5.7 and a suite median near 3.8. It was
the densest panel in the rack by a clear margin. The field's default-visible
counts cluster at 14–25, but those are full-width horizontal devices; Dwell is a
narrow vertical strip carrying the same count. That is the whole diagnosis.

The answer is a **nine-control face** — CHARACTER, TIME with SYNC, FEEDBACK,
MIX, STEREO, LO CUT, HI CUT, FX — at 3.2 per 100 px, calmer than BMO Saturator.
Everything else is revealed. There is **no DSP gate**: revealed parameters stay
live and read at all times. This is a visibility split, not a mode.

## THROW becomes a parallel lane

The old THROW was a send gate with a three-way `throwMode` whose first entry
meant "not armed", so the button did nothing at its own default. Worse, the only
way to emphasise a word was BUILD, which raises the loop gain for *everything
circulating* — so holding it swells the previous words' repeats too.

Frosty's requirement: the ordinary delay and the emphasis must work
**simultaneously in one instance**, not as a mode. Per-word feedback needs its
own storage, so:

- **The main delay always runs**, ducked by the input per `10` §6. It **loses
  its `s` input gate entirely**, so there is no mechanism by which a throw can
  disturb it. The proof-test is that its output is bit-identical between a
  throw-held and a throw-never render.
- **A second "lane"** has its own TIME, is fed only while SEND is active, and
  the ducker never touches it.
- **The lane's tail is one bipolar knob with a sticky centre.** Below centre it
  decays (throw), at centre it holds at unity (freeze), above centre it grows
  (build). Those are three regions of the lane's loop gain, not three modes,
  which is why one control covers them.
- **SEND** gates the lane's input, a word at a time, and is meant to be
  automated. **HOLD** gates the lane's life and, when switched off, **clears**
  it — it must clear rather than mute, because a muted-but-circulating buffer
  would stack on the next SEND. **CHOP** gates the lane's output only, with the
  shortest fade that does not click, for rhythmic stuttering of a held note.
- **SEND onto an occupied lane sums**, so words layer into a chord.
- ~~**The lane is a full mirror of the main delay**, with LINK making it follow
  the main and unlinking seeding it from the main's current values.~~
  **REVERSED 2026-09-23 — the lane shares the main delay's voicing rather than
  mirroring it**, and LINK is deleted along with the six rows it governed. See
  **"The module was pulled back"** below. The lane keeps its **TIME, LEVEL,
  tail, SEND / HOLD / CHOP and its own FX**, tied to the main's by `fx_link`; it
  now runs the main's CHARACTER, STEREO, cuts, modulation and DRIVE. **DUCK
  stays main-engine only.** The parallel-from-dry throw — the reason the lane
  exists at all — is untouched.

**This supersedes README Decided item 2.** The old FREEZE bypassed every in-loop
stage to give a bit-exact, non-eroding hold. The lane's centre detent holds at
unity but still laps the character and filters each repeat, so a long freeze
darkens and colours. That is a real capability leaving v1, recorded here rather
than lost quietly.

## The module was pulled back — 2026-09-23

**Decided by Frosty.** The module had reached **33 parameters, a 980 px
three-column panel, and one parameter pushed off the rack's 32 automation
lanes**. Controls were being cut **to fit a budget rather than on merit** —
VOICE, lane DRIVE and Sweep all died that way, and each cut was argued on
whether it would fit rather than on whether it was good. **That is the symptom
this section exists to name**: a module whose parameter count is deciding its
feature set is one module doing two modules' work.

He reeled it in rather than split it. **The lane now shares the main delay's
voicing**: one CHARACTER, one STEREO, one pair of cuts, one modulation, one
DRIVE, governing both engines. **Seven parameters are deleted** — `link`,
`lane_character`, `lane_stereo`, `lane_low_cut`, `lane_high_cut`,
`lane_mod_rate`, `lane_mod_depth` — and the schema fell to 26, renumbered with
no holes; `lane_note` was then added on the same day, so **it is 27, ids 0–26**.
The whole table is inside a rack slot's lanes with **five spare**, **`fx_link`
is an ordinary on-lane parameter**, and nothing is outside the grid any more.
The pressure that was cutting controls is gone — and the first thing that
happened afterwards was a parameter being **added** on merit.

**Splitting into two modules was considered and deferred, and the reason it was
not chosen is worth recording**: two modules in a rack run **in series**, so a
separate throw module would catch the main delay's **output** rather than the
dry signal. That loses the parallel-from-dry topology the lane was built for —
the whole point being that a thrown word is fed from the source, not from
already-coloured repeats, which is also what makes the main loop provably
undisturbed (`10` §11.1). A split becomes possible only if the two can be fed in
parallel, which the rack's series chain does not offer today. **Deferred, not
rejected** — and `10` §11.1 now requires the DSP to be **one reusable engine
instantiated twice**, precisely so the split stays cheap if Frosty wants it.

## The schema

**Settled at 27 parameters, ids 0–26 — see "THE PARAMETER TABLE" below**, which
is authoritative. The ids are renumbered from the stage 1 checkpoint, which is
permitted because nothing has shipped.

Two kinds of change are not equal. **Deletions, reordering, renames and a
parameter's TYPE are frozen at ship; additions are not** — a new parameter may
be appended afterwards, and the only cost is that it sits at the end of the
automation list and, past id 32, would lose its host lane in a rack. That
asymmetry is why the cuts below had to happen now.

`feedback`'s law also changes: divided by the character's peak in-loop
magnitude, so unity lands at 97 % on every character. See below.

## The stability bug this uncovered — FIXED in the spec 2026-09-21

`10` §3 rested its bound on all in-loop magnitudes being ≤ 1 ("all are ≤ 1 by
construction"). §4 gives tape a +2 dB shelf at 55 Hz, so that was false.

**The first figure recorded here was wrong and is corrected.** 1.2589 is the
shelf's nameplate gain, and unity at 84 % is what that would imply — but the
asymptote lives below the 10 Hz blocker and LOW CUT's 20 Hz floor, so the loop
never sees it. Tape's real chain peak is **1.054 at 63 Hz**, unity at **93.9 %**
before normalisation. Normalising by 1.2589 would have made tape *decay*
**1.55 dB per lap**.

It is not merely early. The bump is at 55–65 Hz while the interpolator's loss is
at the top, so the two **tilt** rather than cancel: a note held at nominal unity
on tape gains low end and loses top every lap.

**The fix, now in `10` §3:** the feedback law divides by `P_c`, the character's
reference loop peak, swept from the built coefficients at `prepare` and on any
change of character, TIME or sample rate — **computed, never hardcoded**, so it
cannot drift from whatever a CALIBRATE pass actually lands on. Clean ≈ 0.999,
tape ≈ 1.054, bucket-brigade 0.990–0.999 with TIME. Unity is then 97.0 % on
every character and the panel carries one tick. The head bump stays **inside**
the loop, so it still compounds per repeat — taking it out would stop tape
getting warmer as it repeats, which is the point of a head bump.

Unity means the loop's **loudest band** holds, not every band; a non-flat loop
cannot do the latter and stay bounded. So a note parked at the lane's centre
detent still darkens as it sustains.

**Bucket-brigade was cleared, then wasn't.** Its Butterworths are `Q = 1/√2`
exactly — the no-peaking boundary — so the filters are fine at 0.990–0.999. But
a compander whose expander re-detects has net gain `0.5·(Ê − S[Ê])`: zero in
steady state, and up to **+0.184 dB per dB of envelope step** through a rising
one — +3.7 dB on a 20 dB transient, unbounded, in-loop, at the same point in the
circulating word every lap. `10` §4 now specifies the expander reading the
compressor's **stored** gain at the same fractional position and applying its
exact reciprocal, so the pair is unity at every instant. The clamp fallback
remains Frosty's call, and **the overshoot figure is MEASURED**: a re-detecting
pair benched at **+3.67 dB on a 20 dB step, 0.184 dB per dB** (AURORA,
2026-09-23, stage 2b at c4d2d33), landing exactly on the model. **The control
ring that buys the fix costs as much memory as the audio ring** — see the
memory note below.

## What stage 2 now is

The handoff's stage 2 described one loop. It is now **two engines**, and every
invariant in `11` §4 — sample-rate and block-size invariance, denormal, NaN and
silence robustness, the bit-exact dry null at MIX 0/25/50, the alias floor —
must hold for both. Expect roughly double the work in the handoff's estimate.

Order still stands otherwise: delay line and feedback law first, then the
time-change laws and character modes, then the lane and its gates, then the FX
candidates cheapest first. Add to it:

- The main loop's `s` gate is **removed**, not repurposed.
- The feedback normalisation above lands before anything depends on unity.
- A test that the main loop is bit-identical with and without throws.
- A test that unity actually holds at the detent, per character.
- **The DSP is one reusable engine instantiated twice**, not a bespoke dual
  engine (`10` §11.1) — so the module can be split later without redoing the
  expensive part.
- **Nothing is seeded by any parameter change.** The seed-on-unlink machinery
  went with LINK; `fx_link` needs none, because the lane's FX values are never
  overwritten while the tie is on.

**What stage 2b turned up** (built and committed 2026-09-23, c4d2d33):

- **Memory is 16 MB per instance at 192 kHz, not 8.0** (`10` §10). Two engines
  is two audio rings, and the compander's control ring is **the same length as
  the audio ring**, per channel per engine, allocated whichever character is
  selected. Eight Dwells in a full rack is ~130 MB of rings.
- **The DC blocker was on the wrong side of the shaper**, and it was a real bug
  rather than a preference: before the shaper it has nothing to do, because LOW
  CUT has already taken the DC, while the asymmetric shaper's own offset went
  into the ring and compounded. **−38.7 dBFS at DRIVE 100 against `11` §4c's
  acceptance; −114.3 dBFS with it moved after.** `P_c` is unaffected, since it
  is swept at DRIVE 0 where the shaper is out of the chain.
- **The tape character floor was decided, built and never written down** — the
  gap that let it fall through the first time. It is now `10` §5a: **TAPE only**,
  0.03 % of speed, riding MOD RATE and summing with MOD DEPTH, modulating the
  **read position** so silence stays silent without a gate.

## THE PARAMETER TABLE — settled 2026-09-21, amended 2026-09-22, cut back and then extended 2026-09-23, 27 parameters

The rack gives each slot **32 host automation lanes** (`RackProcessor.h`,
`kParamsPerSlot`). Past that, `SlotOverflow` keeps a parameter working in the
panel, the DSP, presets and saved state, but it gets no host lane and **cannot
be automated in a rack**. **That limit is the constraint that shaped this
module** — it is what the 2026-09-21 cuts were fighting — but at 27 rows
**Dwell uses no overflow at all**: every parameter is rack-automatable, with
**five lanes to spare**.

Ids are renumbered from the stage 1 checkpoint, which is permitted because
nothing has shipped. Carrying holes where the deleted rows were would be worse.

| id | name | range / law | default |
|---|---|---|---|
| 0 | `time` | 1…2000 ms, log | 375 |
| 1 | `sync` | bool | off (ships disabled) |
| 2 | `note` | choice, 16 | 1/8D (ships disabled) |
| 3 | `feedback` | `g = (1.05·fb^1.6) / peak_c` | 35 |
| 4 | `character` | Clean / Tape / Bucket | Clean |
| 5 | `stereo` | Stereo / Ping-pong / Dual | Stereo |
| 6 | `low_cut` | 20…1000 Hz, log | 20 |
| 7 | `high_cut` | 1k…20k Hz, log | 20000 |
| 8 | `mod_rate` | 0.1…8 Hz, log | 0.6 |
| 9 | `mod_depth` | 0…100 % | 0 |
| 10 | `drive` | 0…100 % | 0 |
| 11 | `duck` | 0…24 dB | 0 |
| 12 | `mix` | 0…100 % | 35 |
| 13 | `send` | bool | off |
| 14 | `lane_gain` | −100…+100, 0 = exact unity, sticky centre | −40 |
| 15 | `hold` | bool | off |
| 16 | `chop` | bool | off |
| 17 | `fx` | bool | off |
| 18 | `fx_type` | choice, 3 | Diffuse |
| 19 | `fx_amount` | 0…100 % | 35 |
| 20 | `lane_level` | −24…+24 dB | 0 |
| 21 | `lane_time` | 1…2000 ms, log | 250 |
| 22 | `lane_note` | choice, 16, the same list as `note` | 1/8 (ships disabled) |
| 23 | `lane_fx` | bool | off |
| 24 | `lane_fx_type` | choice, 3 | Diffuse |
| 25 | `lane_fx_amount` | 0…100 % | 35 |
| 26 | `fx_link` | bool — ties the FX trio 23–25 to 17–19 | on |

Rows 4–10 — CHARACTER, STEREO, the two cuts, the two modulation controls and
DRIVE — **govern both engines** from 2026-09-23. DUCK (11) is main-engine only;
MIX (12) governs both because both sum into the wet before it.

**`lane_note` (22) was added on 2026-09-23** and takes the count to 27, ids
0–26, with **five rack lanes spare**. The lane had its own TIME and no division,
so the moment `12`'s plumbing landed the main delay would lock to the grid while
the lane free-ran in milliseconds and **drifted against it** — which destroys
the lane's rhythmic point, a quarter underneath while throws land on a dotted
eighth. **There is one `sync` (1) and it governs both engines**; each picks its
own division. A separate `lane_sync` was rejected: wanting the main synced while
the lane free-runs is a strange thing to want, and turning SYNC off for the
module gets it. **The defaults agree at 120 BPM** — 375 ms ↔ 1/8D, 250 ms ↔ 1/8
— so enabling SYNC there changes nothing audible, the silent-toggle property
`modules/vcomp`'s COMPLEX was built around, and the reason `lane_note` defaults
to 1/8 rather than copying the main's 1/8D. It **ships disabled** with SYNC and
NOTE. `10` §11.7 owns all of this.

### What was cut, and why

- **VOICE and lane VOICE are gone** (Frosty, 2026-09-21). LO CUT and HI CUT are
  already continuous log sweeps; VOICE only added *resonance* on top of them,
  raising Q from 0.5 to 6. Cutting it also removes the reason those filters had
  to be state-variable and peak-normalised in closed form — they are plain
  one-poles again, and `10` §11's warning that an unnormalised Q = 6
  self-oscillates at a third of the feedback travel no longer applies.
- **`fx_type` loses Octave up, Octave down and Reverse**, leaving Diffuse,
  Sweep, Pan/Tremolo and Crush. The octaves compound in a feedback loop — three
  repeats is three octaves — and Reverse was the only type needing a second
  buffer, which the handoff flagged must not be allocated on the audio thread.
  That problem is now gone. Choice lists are append-only after ship, so this had
  to happen now or never.
- **`fx_type` then loses Sweep too** (Frosty, 2026-09-22), leaving **Diffuse,
  Pan/Tremolo and Crush — three**. **It was cut because VOICE was cut**: Sweep
  was specified as VOICE's resonant centre being moved per repeat, so removing
  VOICE left it with no filter to act on. The alternative was to give the FX
  stage its own resonant band-pass — the filter that had just been removed,
  returning one section later under another name — so the candidate goes
  instead. That is the non-obvious part, and it is recorded because anyone
  reading only the list will see a gap where a sweep belongs. Again: lists are
  append-only after ship, so this was the last moment.
- **lane DRIVE was cut on 2026-09-21**, and the question it left is now moot:
  from 2026-09-23 **`drive` (10) drives both engines**, so there is nothing to
  append. Saturation being slow and cumulative, a thrown word decaying over a
  second or two was always the path with least to work with; it now gets the
  main's setting, which is more than it had.
- **SYNC and NOTE are kept** even though they ship disabled, and **LANE NOTE
  joins them** (2026-09-23) on the same disabled switch — see the note under the
  table.
- **Both FX buttons are kept.** Using "amount at 0" as the bypass was considered
  and rejected: it loses the one-click A/B that makes an effect stage usable,
  and Crush's bit depth does not naturally read zero as a no-op.
- **`fx_link` (id 26) is added rather than cut**, tying the lane's FX trio to
  the main's. Added off-lane on 2026-09-22 — *"leave this separate fx link off a
  lane in case it needs to be cut later"* (Frosty) — it is **an ordinary on-lane
  parameter from 2026-09-23**, because at 27 rows there is no overflow to sit
  in. FX is the one part of the lane's voice that stayed its own: a thrown word
  can be crushed against a clean main delay, which a second set of cuts and
  modulation could not justify in the same way.
- **The lane's voicing rows are cut, and `link` with them** (Frosty,
  2026-09-23): `lane_character`, `lane_stereo`, `lane_low_cut`, `lane_high_cut`,
  `lane_mod_rate`, `lane_mod_depth` and `link` — **seven**. Not to fit a budget,
  which is the point: the budget was what had been driving the cuts, and sharing
  the voicing removed the pressure instead of paying it. See "The module was
  pulled back" above.

## Settled since

**The accent is the orchid `#f094e6`** (Frosty, 2026-09-21), at 306.5 degrees.
Measured off a rendered panel rather than computed: 6.49:1 on the dark plate
and 1.81:1 on the pale, both mid-band. Its nearest neighbour is BMO EQ's pink
at 29.5 degrees; `13` §6's warning that an orchid "may read as EQ in a rack"
was put to Frosty with renders before he chose. `products/AGENTS.md` carries
the allocation row.

**The lane gets its own LEVEL** (Frosty, 2026-09-21). Two delays now sum into
one wet path, and `laneGain` sets the lane's *tail*, not its *loudness* — so
without this the thrown word's volume relative to the main delay would be fixed
by construction, which is wrong for a feature whose whole job is emphasis.

`-24…+24 dB, default 0, step 0.01`, matching every other level in the suite
(`modules/eq` Output, `modules/opto` Level, `modules/vcomp` Output) rather than
inventing a range. Default 0 dB is unity against the main delay's wet: the lane
is the same loudness until asked otherwise, and since SEND ships off the module
is silent at defaults either way.

One interaction to hold in mind when it is built: the lane can self-oscillate
in the build region, and +24 dB on top of that is a lot. The safety clip
governs it, as it governs the main loop, but the test that proves the clip
bounds the lane should be run at the top of LEVEL's travel, not at unity.

## Still open

- **`chop`'s fade against `11` §4's −60 dBFS assertion.** A 1 ms raised cosine
  is click-free by ear but does not meet a broadband figure on bright sustained
  content. This needs a **measurement**, not a ruling: band-limit the assertion
  to 5 kHz, lengthen the fade to ~3 ms, or record the measured edge (`10`
  §11.4).
- The **lit-state glow on the pale plate**. It reads by fill rather than by
  glow: the bloom peaks at 1.37:1 against `#efefef` versus 2.49:1 on the dark
  plate, because a bloom brightens and there is little room to brighten against
  light grey. The fix is a darker halo -- the panel already derives `#965491`
  for pale-plate legends at 4.6:1.
- The **expanded width**, reopened downward by the 2026-09-23 pullback. What the
  reveal has to hold lost the lane's second voicing and its LINK entirely —
  seven controls — so the 980 px three-column layout is oversized for what is
  left. **The new width is the panel's to settle against a render**, not this
  document's, and no figure is set here.

**Settled on 2026-09-22, and no longer open**: the lane's **build ceiling
`g_max` is a CALIBRATE value**, heard in `14` §3's listening round rather than
decided on paper — 1.05 is about 12 s from unity to the ceiling at 250 ms lane
time, 1.10 about 6 s, 1.3 a violent swell, and the safety clip bounds every one
of them. **Sweep is cut.**

**Settled on 2026-09-23**: the lane **shares** the main delay's voicing, `link`
and the six lane voicing rows are deleted, **`lane_note` is added** with one
SYNC governing both engines (`10` §11.7), the schema is **27 with nothing
outside the rack's lanes**, and the DSP is **one engine instantiated twice**
(`10` §11.1).
