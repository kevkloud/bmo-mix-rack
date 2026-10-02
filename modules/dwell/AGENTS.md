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
lane's voicing back on 2026-09-22 (`docs/delay/15`, "The module was pulled
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
`lane_*` rows appended and then **seven of them deleted again on 2026-09-22** —
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
for the 24-tap interpolator — and there is no lookahead, so there is no dry
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

## Tempo, tail, and what does not exist

- **SYNC is live** (2026-10-01), on PR #31's `ModuleDsp::setTempo`. The adapter
  (`dsp/DwellDsp.h`) holds the last valid host tempo and maps NOTE and LANE
  NOTE to milliseconds **in `setParams`, at the held tempo** -- not in
  `setTempo`, which arrives after the parameters each block and would hand
  the engines the knob's time then the note's, re-sweeping the loop peak every
  block. Before the first tempo the knobs stand; a lost tempo is held; a
  stopped transport changes nothing; a division longer than the 2 s ring is
  halved until it fits (`docs/delay/10` §7, `dsp/Timing.h`). **The first
  valid tempo after `prepare` or `reset` lands** -- both reads jump straight to
  the synced times while the ring is still empty -- because as an ordinary
  move tape and bucket-brigade glided three seconds in from the TIME knob on
  every fresh instance and every rack chain edit (2026-10-01). Every later
  tempo change moves under §2's law, as before. The panel names
  the division on the screen rather than printing a millisecond figure it
  cannot know. `tests/plugin/TempoTests.cpp` carves Dwell out of its
  byte-identity walk by name, and still asserts it is identical with SYNC off
  and different with SYNC on, standalone and in a rack slot.
- **Dwell reports a tail** (`tailSecondsForParams`, `dsp/Timing.h`): the
  longer of the two engines', clamped [0.5 s, 30 s]. Each engine's is swept
  over frequency (2026-10-01): the laps the loop gain at `P_c` = 1 needs to
  fall from **the build-up a held input can leave, `1/(1 - g)` of that
  input**, to 60 dB under it, times **TIME plus that frequency's filter group delay plus
  what an in-loop FX adds to a lap** -- Diffuse's peak allpass delay, which is
  conservative by design (renders ran 8-88 % of it), and Crush's hold. The
  lane counts only with HOLD on, and a FREEZE or BUILD reports 30 s. **With
  SYNC on each time is taken at the 2 s ring**, because a tail comes from
  parameters alone and the tempo is not one. `DwellDspTests` renders the
  figure against the real decay; the exceptions it does not cover are written
  at `tailSecondsFor`. `tests/plugin/TailTests.cpp` lists Dwell beside Linger
  as the two modules that ring.
- **Crush truncates toward zero** (2026-10-01, Frosty to confirm): rounding
  expanded and held a limit cycle above about 60 % FEEDBACK. A one-step cycle
  through the lap's filter overshoot still survives at FEEDBACK 95 % and up,
  AMOUNT 100, at 44.1 and 96 kHz; `docs/delay/10` §11a has the numbers and the
  option that would end it.
- **MIX is smoothed across its 50 % hinge**, and the dry *lands* on exactly 1.0
  below it (`Smoother::tickLanding`), which is when the bit-exact null returns.
  A float one-pole on its own stalls about 3e-5 short of 1.0.
- **The output level is reviewed and left alone** (Frosty, 2026-10-01). With
  a -18 dBFS RMS 1 kHz sine in, measured on AURORA, the peak out is -8.5 dBFS
  at the defaults, +1.1 at FEEDBACK 100 and MIX 50, **+6.7** with HOLD and SEND
  on, LANE GAIN +100, LANE LEVEL 0, FEEDBACK 100 and MIX 50, and +24.9 with
  LANE LEVEL +24. The +6.7 is three signals summed -- the dry, the main delay
  self-oscillating, the lane building -- each bounded near unity by its own
  in-loop clip; **nothing limits the sum**, and no limiter or gain change is
  to be added without asking (`docs/delay/10` §9).
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
- **The lane shares the main delay's voicing** (DECIDED, Frosty 2026-09-22).
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

## The panel: BMO Linger's paged handheld, one width

DECIDED, Frosty 2026-10-01 ("go with option one and bring Lane's fx to its
tab"), chosen on renders over the old face kept with a tabbed box under it.
**380 px, one width, `expandedWidth` 0** — the 280 compact / 840 expanded pair
and the panel's own expand arrow are gone, for Linger's reason: three pages
never need to be on screen at once, and a tab reaches each in one click where
the arrow reached them in one click and 560 px.

- **The screen** carries the page menu, TONE / LANE / FX, as its top band, and
  draws the loop under it: a train of stems from the first repeat at 0 dB to
  -60, computed with `dsp/GainLaws.h` — **the same two functions `DspCore`
  runs**, at `P_c` = 1. Do not draw it any other way; the layout test checks
  the count against the law written out independently. On LANE it letters
  THROW / FREEZE / BUILD with the live one lit; on TONE it carries the GR bar,
  and its timer runs only on that page.
- **The foot never changes**: TIME (or NOTE), FEEDBACK, MIX with their unity
  and dry-hinge strips, and SYNC on the DELAY rule.
- **TONE**: CHARACTER, STEREO; LO CUT, HI CUT and DUCK as **faders** (`ui::Fader`)
  down both grid rows (Frosty, 2026-10-01, "3 with sliders").
- **LANE**: SEND, HOLD, CHOP; the lane's FX types; TAIL, TIME, LEVEL / the
  lane's FX gate, AMOUNT and LINK. **The lane's FX lives here**, not on FX.
- **FX**: the loop's colour -- DRIVE, RATE, DEPTH -- on the top row, then the
  main delay's FX types, gate and AMOUNT **in the same cells** the lane's
  occupy on LANE, so turning between the two pages moves nothing but what the
  FX controls are bound to.

**The page is view state** — `ModulePanel::setUiState ("page", ...)`, never a
parameter, never in a preset. Nothing resizes and nothing turns a page when a
parameter moves, so the old "`fx` opens the column once, by a click" rule has
nothing left to guard and is gone with the column.

**SYNC swaps both engines' time for their note, together.** With SYNC on,
NOTE takes TIME's cell in the foot and LANE NOTE (`lane_note`, id 22) takes LANE
TIME's on the LANE page, because one switch governs both engines. A preset or
host can write SYNC as well as the switch can, so the panel shows whichever
knob is live; `resized` reads SYNC itself so any layout matches the parameter.
Every parameter has a control, and `tests/ui/LayoutTests.cpp` sums them.

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

## The accent is decided

**The accent is jade `#46c988`** (DECIDED, Frosty 2026-10-01), inside the band at
6.42:1 dark and 1.83:1 pale, on the suite's own inks: the derived light-mode
ink, lit buttons in the accent, choice rows in the utility azure.
`products/AGENTS.md` carries the row, the neighbours (21.1 degrees from Util's
green, 22.0 from DEQ's teal -- known, and chosen on a side-by-side render) and
the road through the orchid and a bright yellow that led here.

`Module.cpp` carries it as `kAccent`, and **nothing in the panel names a hex**.
`Module.h` carries one style constant, `kFollowAlpha`: the lane's FX row and
AMOUNT are dimmed by transparency while FX LINK holds them, not stepped toward
grey. `ui::declareLightInk` stays in core for themes; Dwell does not use it,
and the layout test says so.

## What stage 1 is not

`dsp/DspCore.h` carries the parameters and passes audio through. It is plumbing
with no loop in it, and it is written that way rather than left absent so the
schema-to-DSP wiring is pinned by tests before any arithmetic is written
against a lane read off by one. The ring is allocated in `prepare` from
`kMaxTimeMs` and never from a parameter — 2.0 s at 192 kHz, **2.0 MB per
channel** — so nothing is allocated on the audio thread. Stage 2 fills it in, in
the order `docs/delay/HANDOFF-add-bmo-dwell.md` sets out.

**The memory figure doubles twice, and both are structural.** The lane's ring is
the same fixed maximum as the main's — `lane_time` shares TIME's range — and the
**compander's control ring is as long as the audio ring**, per channel per
engine, allocated whichever character is selected (`docs/delay/10` §4, §10). So
an instance is **16 MB at 192 kHz**, 4.0 MB at 44.1 kHz; the 8.0 MB an earlier
pass quoted counted the audio rings alone; `params.h`'s `kMaxTimeMs` comment
carries the 16 MB figure and names the two wrong ones. Every
invariant in `11` §4 — sample-rate and block-size invariance, denormal, NaN and
silence robustness, the bit-exact dry null, the alias floor — has to hold for
**both** engines, so expect roughly double the work the handoff estimates.

**Two things stage 2b settled that are easy to undo by accident**
(`docs/delay/15`, "What stage 2b turned up"; committed c4d2d33):

- **The DC blocker goes after the shaper, not before it.** Before it, LOW CUT
  has already taken the DC so it does nothing, while the shaper's own asymmetric
  offset compounds in the ring: **−38.7 dBFS at DRIVE 100** against **−114.3**
  with it moved. It is invisible at DRIVE 0, so only the high-DRIVE case catches
  a regression.
- **TAPE's character floor modulates the read position, never the output.** A
  zeroed ring then still reads zero, so `11` §4k's silence-to-exact-zeros holds
  without a gate. An output-gain implementation would pass on clean and fail on
  tape.

## House rules this module is easy to break

- **No hardware or software brand or product name** in code, docs or UI
  strings. The characters are named for what they are — clean, tape,
  bucket-brigade — not for the boxes they come from.
- **No control is labelled DWELL.** That is the module.
- Name the machine in every testing note and every result.
