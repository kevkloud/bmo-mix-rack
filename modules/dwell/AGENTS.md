# modules/dwell — BMO Dwell

What this folder cannot be read off its own code. The specification is in
`docs/delay/`; this is the part that is decided, permanent, or a trap.

## The schema is permanent, and three things are frozen together

`params.h` holds ids 0–19 exactly as `docs/delay/11-integration-and-test-plan.md`
§3 lists them. Frozen from the first release: **the ids, their order, and the
index order of the four choice lists** — `note`, `character`, `stereo`,
`throwMode`. A session keys automation by position and stores a choice as its
index, so moving a row or inserting a name into a list silently repoints every
lane and every preset that referenced it. Nothing errors and nothing warns.
New parameters append at the end; new choices append at the end of their list.

`tests/plugin/DwellTests.cpp` writes the whole table out, and
`tests/dsp/DwellDspTests.cpp` pins the enum-to-spec agreement. Either failing
is the schema moving, which is a decision for Frosty and not a fix.

**`fxType` is the exception and it expires.** The seven FX types are
*candidates*: the list and its order are free until ship, and a candidate that
fails the listening in `docs/delay/14` §3 comes **out** of the list rather than
being left in as a dead index. After ship it is append-only like the rest.

**NOTE's order is not "least to most" like the other three.** Its index *is*
the automation lane, so it ascends in duration: a lane sweep moves monotonically
in time and a clockwise knob lengthens. That is why all sixteen ship at once —
anything appended later would sit at the end, out of order, forever. The other
three lists do run least to most intervention, so index 0 is the neutral value
a corrupt state lands on.

## Latency is 0, and the delay time is not latency

`DwellDsp::latencyForParams` returns 0 at every setting and is meant to keep
doing so. `docs/delay/10` §0 drops oversampling outright — that is what pays
for the 32-tap interpolator — and there is no lookahead, so there is no dry
compensation ring either.

The trap is the other direction: **reporting TIME as latency**. What a host
compensates is a delayed copy of what it sent; a delay's repeats are new signal
arriving late on purpose. Reporting TIME would pull the track forward by up to
two seconds and drag the dry signal with it. `modules/dim` sets the same
precedent — it manufactures detune content that is not time-aligned with its
input and reports 0.

If `10` §4's half-band fallback is ever added around the shaper, its group
delay is a whole number of samples and is subtracted from `D`. The figure stays
0 and the delay time stays exact; the assertion in `11` §4 (k) still has to
hold after it.

## What ships disabled, and what does not

- **SYNC and NOTE ship disabled.** The slots and NOTE's order are permanent
  from this release (DECIDED, Frosty 2026-09-20), but no host tempo reaches a
  `ModuleDsp` today. That plumbing is `docs/delay/12` — processor →
  `ModuleEngine` → `ModuleDsp::setTempo` — and it is **its own workflow and its
  own pull request**, not part of adding this module. `kSyncIsEnabled` in
  `params.h` is the one switch: the DSP ignores both parameters while it is
  false and the panel shows the pair disabled.
- **FREEZE ships enabled in v1**, with its own button and its own slot, never
  folded into `throwMode` (DECIDED, Frosty 2026-09-20).
- **No oversampling parameter exists and none is to be added.** `10` §0 is the
  topology decision the whole CPU budget rests on. Adding a rate later would
  be a schema event, not a feature.

## `fx` and the expanded view are tied, not the same

DECIDED, Frosty 2026-09-20. `fx` (id 17) is **the sound**: a parameter, on the
compact panel, automatable, in presets. The expanded column that shows FX TYPE
and FX AMOUNT is **the view**: `ModuleDef::expandedWidth` plus a session-only
`view` attribute, never a parameter and never in a preset.

So: automation, preset load and session recall **never resize the module**. A
user clicking `fx` on while compact opens the view once, as a convenience; the
panel's own arrow then closes it while `fx` stays on; turning `fx` off never
closes it. Rack defaults compact, standalone defaults expanded — DEQ's
behaviour.

The arrow is the one new touch point beyond DEQ, which switches only from the
host bar (`ui::ExpandButton`): Dwell's panel has to be able to ask its host to
flip the session-only flag. That is panel work, in `panel/`, not in
`Module.cpp`.

`Module.cpp` and `modules/CMakeLists.txt` already name what the panel has to
be: `bmo::dwell::DwellPanel`, a `ui::ModulePanel` constructed from a
`ui::ModuleContext`, declared in `panel/DwellPanel.h` and defined in
`panel/DwellPanel.cpp`. The panel chooses its layout from the width it is
given (280 or 560) and needs no other signal. 560 rather than `13` §6a's
460 is part of the panel redesign of 2026-09-21 and is Frosty's to confirm.

**The structure is settled and the three candidates are gone.** 280 carries
eleven controls; 560 adds a second column with the other eight — VOICE, DRIVE,
MOD RATE, MOD DEPTH, DUCK, THROW MODE, FX TYPE and FX AMOUNT. It is a
**visibility** split only: every parameter stays live and is read at all times,
there is no gate, and `params.h` is untouched. The class comment in
`panel/DwellPanel.h` carries the argument, including why a width expansion
rather than LTV Comp's in-place reveal, and `kDuckHome` in `panel/
DwellPanel.cpp` is the one line that decides where DUCK lives — that is still
under investigation.

## The accent is not decided

`Module.cpp` holds one literal, `0xfff288eb`, a magenta. **It is a placeholder
so the panel can be rendered**, and it has already moved once: the olive-gold
`docs/delay/13` §6 recommends was rejected outright ("i hate this color",
Frosty 2026-09-21) and so was a pale gold that measured out of band. Frosty picks
the accent from a render, measured against both contrast rules the way BMO
FET's was — not from the formula. `11` §1 and `13` §6 list six candidates
between them; the de-esser has taken the rose near 4°, which rules out the red.
Changing it is a one-line edit, and nothing else in the module names a colour.

## What stage 1 is not

`dsp/DspCore.h` carries the parameters and passes audio through. It is plumbing
with no loop in it, and it is written that way rather than left absent so the
schema-to-DSP wiring is pinned by tests before any arithmetic is written
against a lane read off by one. The ring is allocated in `prepare` from
`kMaxTimeMs` and never from a parameter — 2.0 s at 192 kHz, 4.0 MB per
instance — so nothing is allocated on the audio thread. Stage 2 fills it in, in
the order `docs/delay/HANDOFF-add-bmo-dwell.md` sets out.

## House rules this module is easy to break

- **No hardware or software brand or product name** in code, docs or UI
  strings. The characters are named for what they are — clean, tape,
  bucket-brigade — not for the boxes they come from.
- **No control is labelled DWELL.** That is the module.
- Name the machine in every testing note and every result.
