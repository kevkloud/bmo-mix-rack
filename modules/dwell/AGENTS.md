# modules/dwell — BMO Dwell

What this folder cannot be read off its own code. The specification is in
`docs/delay/`; this is the part that is decided, permanent, or a trap.

## The schema is permanent, and three things are frozen together

`params.h` holds **ids 0–26 — twenty-seven parameters**, which
`docs/delay/11-integration-and-test-plan.md` §3 and `docs/delay/15-lane-redesign.md`
both list. **Every one of them is rack-automatable.** A rack slot carries
`RackProcessor::kParamsPerSlot` = 32 host lanes, and anything past that would
live in `SlotOverflow`: working in the panel, the DSP, presets and saved state,
automatable standalone, but **with no host automation lane in a rack**.
**Dwell uses none of that** — 27 rows fit inside the grid with **five lanes to
spare**. The limit is still worth knowing, because it is the constraint that
shaped this module: controls were being cut to fit it until Frosty pulled the
lane's voicing back on 2026-09-23 (`docs/delay/15`, "The module was pulled
back"). Frozen from the first release: **the
ids, their order, and the index order of the four choice lists** — `note`,
`character`, `stereo`, `fx_type`. A session keys automation by position and
stores a choice as its index, so moving a row or inserting a name into a list
silently repoints every lane and every preset that referenced it. Nothing errors
and nothing warns. New parameters append at the end; new choices append at the
end of their list.

**Nothing has shipped, which is the only reason the table could keep moving**:
VOICE deleted and everything after it renumbered, `throw` renamed `send`,
`throw_mode` replaced by the bipolar float `lane_gain`, `freeze` renamed `hold`,
`chop` added, `fx_type` shortened from seven entries to four and then to three,
`lane_*` rows appended and then **seven of them deleted again on 2026-09-23** —
`link` and the lane's six voicing rows — with everything after them renumbering
and **no holes left behind**. Deletions, reorders, renames and type changes are
illegal after ship; appends are not. **There is no lane DRIVE to reconsider any
more**: `drive` (10) drives both engines.

`tests/plugin/DwellTests.cpp` writes the whole table out, and
`tests/dsp/DwellDspTests.cpp` pins the enum-to-spec agreement. Either failing
is the schema moving, which is a decision for Frosty and not a fix.
`./build-ui/tools/Release/measure_dwell.exe schema` prints the live table with
ranges, defaults and choice lists — **read it rather than re-deriving anything**.

**`fx_type` is the exception and it expires.** The **three** FX types —
Diffuse, Pan/Tremolo, Crush — are *candidates*: the list and its order are free
until ship, and a candidate that fails the listening in `docs/delay/14` §3 comes
**out** of the list rather than being left in as a dead index. After ship it is
append-only like the rest. Octave up, Octave down and Reverse were cut on
2026-09-21: the octaves compound in a feedback loop, and Reverse was the only
type needing a second buffer. **Sweep was cut on 2026-09-22, and it was cut
*because* VOICE was** — Sweep moved VOICE's resonant centre per repeat, so with
VOICE gone it had no filter to act on, and giving the FX stage its own resonant
band-pass would put that filter straight back. Expect someone to see a gap where
a sweep belongs; re-adding one means re-opening the VOICE decision first.
**Both engines read this one list** (`fx_type` and `lane_fx_type`), as they read
one `character` and one `stereo` list.

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

- **SYNC, NOTE and LANE NOTE ship disabled.** The slots and NOTE's order are
  permanent from this release (DECIDED, Frosty 2026-09-20 and 2026-09-23), but
  no host tempo reaches a `ModuleDsp` today. That plumbing is `docs/delay/12` —
  processor → `ModuleEngine` → `ModuleDsp::setTempo` — and it is **its own
  workflow and its own pull request**, not part of adding this module.
  `kSyncIsEnabled` in `params.h` is the one switch: the DSP ignores all three
  parameters while it is false and the panel shows them disabled.
- **There is one SYNC and it governs both engines.** `note` (2) is the main
  delay's division, **`lane_note` (22) is the lane's**, off the same sixteen
  values; there is deliberately **no `lane_sync`** (`docs/delay/10` §11.7).
  Without `lane_note` the lane would free-run in milliseconds while the main
  locked to the grid, and drift against it — which is the one thing the lane's
  rhythm cannot survive.
  **Do not "tidy" `lane_note`'s default.** It is **1/8**, not the main's 1/8D,
  because 1/8 is 250 ms at 120 BPM and `lane_time` defaults to 250 ms — just as
  `note` 1/8D is 375 ms against `time`'s 375. **That agreement is what makes
  enabling SYNC at 120 BPM inaudible**, the property `modules/vcomp`'s COMPLEX
  was built around, and `11` §4h asserts it.
- **FREEZE, THROW MODE, BUILD and VOICE do not exist.** They were cut on
  2026-09-21 (`docs/delay/15`, README items 10–12 and 18) and nothing in this
  module should name them. What replaced THROW and FREEZE is **the lane**: a
  second delay engine running at the same time as the main one, with `send`
  gating its input, `hold` gating its life (off **clears** it, never mutes),
  `chop` gating its output, and the bipolar `lane_gain` setting its tail —
  decaying below centre, holding at exact unity at centre, building above it.
  What replaced VOICE is nothing: LO CUT and HI CUT are plain one-poles again.
  **The bit-exact, non-eroding hold the old FREEZE gave is gone from v1** and
  the detent's hold colours as it laps the character chain; that cost is
  recorded in `docs/delay/10` §11.5, not glossed.
- **The main delay has no input gate.** `docs/delay/10` §3's `s` term is
  removed, not repurposed, so nothing the lane does can disturb the main loop —
  and `11` §4e's headline test asserts exactly that, bit for bit.
- **The lane shares the main delay's voicing** (DECIDED, Frosty 2026-09-23).
  `character`, `stereo`, both cuts, both modulation rows and `drive` govern
  **both engines**; **`duck` is main-engine only** — the ducker never reaches
  the lane; `mix` governs both, since both sum into the wet before it. The lane
  owns its TIME, LEVEL, tail, three gates and its FX stage, and **nothing
  else**. There is **no `link` and no `lane_character`, `lane_stereo`,
  `lane_low_cut`, `lane_high_cut`, `lane_mod_rate` or `lane_mod_depth`** — all
  seven were deleted, not defaulted.
- **The DSP is one reusable engine instantiated twice**, not a bespoke dual
  engine (`docs/delay/10` §11.1). This is a **requirement**, not a style note:
  Frosty chose it so the module can be split into a plain delay and a throw
  delay later without redoing the expensive part. Anything that interleaves the
  two paths defeats it.
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

**`fx_link` (id 26) is the one tie left, and it has no gesture.** It makes the
lane's FX trio (23–25) follow the main's (17–19), default on, and while it is on
the lane's three values are **ignored, not overwritten** — so they are still
there when it releases and **nothing is seeded, in either direction**
(`docs/delay/10` §11.3). The seed-on-unlink machinery this file used to warn
about went with `link`; there is no longer any parameter in this module whose
change writes another parameter, and `11` §4e asserts that by counting
host-visible parameter changes across an `fx_link` automation pass: exactly
zero.

**FX is the one part of the lane's voice that stayed its own**, because a thrown
word can be crushed against a clean main delay. A second set of cuts and
modulation could not earn its rows the same way, which is why they went and this
did not.

`Module.cpp` and `modules/CMakeLists.txt` already name what the panel has to
be: `bmo::dwell::DwellPanel`, a `ui::ModulePanel` constructed from a
`ui::ModuleContext`, declared in `panel/DwellPanel.h` and defined in
`panel/DwellPanel.cpp`. The panel chooses its layout from the width it is
given and needs no other signal — **which width is in flux**: 280 compact is
settled, the second width has been 460, 560 and 980 in turn, and the
2026-09-23 pullback removed seven controls from what the reveal has to hold.
`Module.cpp` is the record; this file is not.

**The face is nine controls** (DECIDED, Frosty 2026-09-21; `docs/delay/15`):
CHARACTER, TIME with SYNC, FEEDBACK, MIX, STEREO, LO CUT, HI CUT and FX. That
figure is the whole diagnosis of why the first three panels were rejected — 20
parameters in a 280 px column is 7.1 controls per 100 px, the densest panel in
the rack; nine is 3.2. Everything else is **revealed**, and it is a
**visibility** split only: every parameter stays live and is read at all times,
there is no DSP gate, and `params.h` is untouched.

**The expanded inventory and its width are not settled, and the 2026-09-23
pullback made the problem smaller rather than solving it.** What the reveal has
to hold is now **one** voicing, the main delay's depth, and the lane's own
controls — SEND, HOLD, CHOP, LANE GAIN, LEVEL, TIME and its FX trio. The second
mirrored voicing and LINK are gone, so the 980 px three-column layout is
oversized for what remains. That is a panel decision in `panel/`, taken against
a render, not something to settle from this file.

## The accent is decided

**The accent is the orchid `#f094e6`** (DECIDED, Frosty 2026-09-21; `15`,
`docs/delay/README.md` item 16), measured off a render at 6.49:1 on the dark
plate and 1.81:1 on the pale, with `products/AGENTS.md` carrying the allocation
row. It was chosen from renders after the olive-gold `docs/delay/13` §6
recommends was rejected outright ("i hate this color", Frosty 2026-09-21) and a
pale gold measured out of band; the de-esser has taken the rose near 4°.

`Module.cpp` carries it as `kAccent = 0xfff094e6`, and **nothing else in the
module names a colour** — the earlier magenta placeholder `0xfff288eb` is gone.
Changing it would be a one-line edit, but it is decided, not a placeholder any
more.

## What stage 1 is not

`dsp/DspCore.h` carries the parameters and passes audio through. It is plumbing
with no loop in it, and it is written that way rather than left absent so the
schema-to-DSP wiring is pinned by tests before any arithmetic is written
against a lane read off by one. The ring is allocated in `prepare` from
`kMaxTimeMs` and never from a parameter — 2.0 s at 192 kHz, **2.0 MB per
channel** — so nothing is allocated on the audio thread. Stage 2 fills it in, in
the order `docs/delay/HANDOFF-add-bmo-dwell.md` sets out.

**Stage 2 is now two engines, so the memory figure doubles.** The lane's ring is
the same fixed maximum as the main's — `lane_time` shares TIME's range — which
is **8.0 MB per instance at 192 kHz**, 2.0 MB at 44.1 kHz. `params.h`'s
`kMaxTimeMs` comment still says 4.0 MB per instance and is describing one ring;
it wants correcting in a code pass. Every invariant in `11` §4 — sample-rate and
block-size invariance, denormal, NaN and silence robustness, the bit-exact dry
null, the alias floor — now has to hold for **both** engines, so expect roughly
double the work the handoff estimates.

## House rules this module is easy to break

- **No hardware or software brand or product name** in code, docs or UI
  strings. The characters are named for what they are — clean, tape,
  bucket-brigade — not for the boxes they come from.
- **No control is labelled DWELL.** That is the module.
- Name the machine in every testing note and every result.
