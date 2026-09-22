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
- **The lane is a full mirror of the main delay** — its own character, stereo
  mode, filters, voice, modulation, drive and FX — with a **LINK** switch that
  makes it follow the main. Unlinking **seeds the lane from the main's current
  values**, so nothing jumps.

**This supersedes README Decided item 2.** The old FREEZE bypassed every in-loop
stage to give a bit-exact, non-eroding hold. The lane's centre detent holds at
unity but still laps the character and filters each repeat, so a long freeze
darkens and colours. That is a real capability leaving v1, recorded here rather
than lost quietly.

## The schema

Ids 14–16 are repurposed in place (`send`, `laneGain`, `hold`); the lane's own
voicing and FX append from 20. **`laneGain` changes type from a 3-choice to a
float, and that is the one change that must happen before ship** — additions may
be appended afterwards, at the cost of automation lane ordering, but a type
change may not. The full proposed table is drafted and awaiting review; it takes
the module from 20 parameters to roughly 35.

`feedback`'s law also changes: divided by the character's peak in-loop
magnitude, so unity lands at 97 % on every character. See below.

## The stability bug this uncovered, which now blocks

`10` §3 rests its bound on all in-loop magnitudes being ≤ 1 — "all are ≤ 1 by
construction". But §4 gives tape a **+2 dB shelf at 55 Hz**, which is |H| = 1.26.
Derived on AURORA: tape reaches unity at FEEDBACK **84.0 %**, not the 97.0 % that
`10` §3, `11` §3 and the panel's planned tick all state, and at full feedback the
tape loop sits at **1.322**.

It is not merely early. The bump is at 55 Hz while the interpolator's loss is at
high frequencies — `10` §11 already notes Hermite is below unity at fractional
phases — so the two **tilt** rather than cancel. A note parked in the freeze
detent on tape gains 2 dB of 55 Hz and loses top end every lap: within seconds a
held chord becomes a boom.

**A detent labelled FREEZE is a promise that centre is unity**, so this must be
fixed for the lane to be honest. The recommended mechanism is to normalise the
feedback law by each character's peak in-loop magnitude, rather than removing
the bump from the loop — removing it would stop the bump accumulating per
repeat, and accumulation is what a head bump is for. The cost is that tape's
tail is slightly shorter than clean's at the same knob position, which is what
tape does anyway. **Bucket-brigade has not been checked**: its 2:1 compander
straddles the delay line, and with 5/50 ms constants on both halves the pair
will not track through transients, so the loop may momentarily exceed unity.
That needs deriving.

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
- Seeding on unlink must be a **UI gesture, not a side effect of the `link`
  parameter changing** — otherwise automating LINK rewrites eight parameters on
  every pass and fights the user's own automation.

## Settled since

**The accent is the orchid `#f094e6`** (Frosty, 2026-09-21), at 306.5 degrees.
Measured off a rendered panel rather than computed: 6.49:1 on the dark plate
and 1.81:1 on the pale, both mid-band. Its nearest neighbour is BMO EQ's pink
at 29.5 degrees; `13` §6's warning that an orchid "may read as EQ in a rack"
was put to Frosty with renders before he chose. `products/AGENTS.md` carries
the allocation row.

## Still open

- Whether the lane needs its own **level** control to balance against the main
  delay. `laneGain` sets its tail, not its loudness, so as drafted the lane's
  volume relative to the main delay is fixed -- which seems wrong for a feature
  whose job is emphasis.
- The lane's **build ceiling**: the main loop caps at 1.05 by Decided item 4.
  Does the lane's build region cap there too, or higher because a violent build
  is the point?
- The **lit-state glow on the pale plate**. It reads by fill rather than by
  glow: the bloom peaks at 1.37:1 against `#efefef` versus 2.49:1 on the dark
  plate, because a bloom brightens and there is little room to brighten against
  light grey. The fix is a darker halo -- the panel already derives `#965491`
  for pale-plate legends at 4.6:1.
- The **expanded width**. Two mirrored voicings plus two FX sections will not
  fit the built 560.
