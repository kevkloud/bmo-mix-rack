# BMO Dwell — Integration & Test Direction

Written on AURORA, 2026-09-20. No code here; this says what the devs build. DSP
meaning and constants come **per `docs/delay/10-dsp-spec.md`** (cited as 10);
conventions per `docs/fet-comp/00-repo-conventions.md` and this folder's `00`.

## 1. Conventions

`modules/dwell/` follows the `fetcomp` layout, with `AGENTS.md` + `README.md`.

**Identity row** (`products/AGENTS.md`; permanent, allocate before first build):
BMO Dwell | `dwell` | `Bdly` | `com.lt3audio.bmodwell` | `.bmodwell`,
`ui::bmoLine()`. `Bdly` is reserved at `products/AGENTS.md:149`.

**"Dwell" is not trademark-searched — do that before the row ships**: the row,
bundle id and extension are permanent.

**Registration**: `modules/CMakeLists.txt`; `products/rack/Registry.cpp`;
`products/dwell/{Product.h,main.cpp,CMakeLists.txt}`; `tests/CMakeLists.txt`
(dsp, plugin, `rack_tests`/`ui_layout_tests`); `tools/CMakeLists.txt`; and
`tools/snapshot`'s product list and link line, without which no panel renders.

**The accent is settled: the orchid `#f094e6`** (Frosty, 2026-09-21; `15`,
README item 16), measured off a render at 6.49:1 on the dark plate and 1.81:1 on
the pale. The candidate list below is kept as the record of what was weighed —
it is history, not a choice still open.

**Accent candidates**: **W1 `#f0938c`** ~4°, separations 27.7°/28.0°, ≈5.9:1 on
the dark plate (band 5.87–7.19); **W2 `#e694e0`** ~304°, 32.8°/31.6° (fetcomp
§4c's F); **W3 `#e6e278`** ~58°, 26.3°/22.1°, the gold gap 10 §0 assumes and the
weakest. W1 and W2 clear the teal's 26.8° worst case. Formula-derived — confirm
with `Inspect.exe ratio` on a render. `13-panel-direction.md` §6 proposes a
different three (`#b2bb54`, `#f79a8e`, `#f094e6`), recommending the olive-gold:
six candidates, one accent.

## 2. New plumbing

**Tempo.** No host time reaches `ModuleDsp` today; three touch points.
(1) **Processor**: `SingleModuleProcessor` and `RackProcessor` call
`getPlayHead()->getPosition()` once per block and translate; JUCE types stop
here. (2) **Rack**: it passes the result to each slot's `ModuleEngine` (and
`SlotOverflow`) before `process`. (3) **Module**: `ModuleDsp` gains 10 §7's
`virtual void setTempo (double bpm, bool valid, bool playing) noexcept {}`.

**Backward compatibility rests on that empty default body**: existing modules
inherit a no-op — no module file changes, no schema change, `core/dsp` still
JUCE-free. **The fallback must be deterministic**: last valid BPM, else TIME.

**Tail length** is hardcoded `0.0` in all three host-facing processors. Add
`virtual double tailSecondsForParams (const float*, int) const { return 0.0; }`
— `latencyForParams`'s append-with-default pattern, so other modules keep
reporting zero. Dwell returns 10 §9's figure clamped to [0.5, 30] s; the rack
**sums** slot tails and clamps the sum to 30 s (12 §4).

**Latency** is **0 at every setting**: 10 §0 drops oversampling, so there is no
`oversampling` parameter and no dry compensation ring. **Wet delay time is never
reported as latency** (`modules/dim`'s precedent). If 10 §4's half-band fallback
is added, its whole-sample group delay comes off `D`, keeping the figure 0.

## 3. Parameters

`specs()` order = `enum Index` order; all automatable. **Twenty-seven
parameters, ids 0–26** (DECIDED, Frosty 2026-09-23; `15`, README Decided items
28 and 31). The ids
below are the **string ids** as `modules/dwell/params.h` declares them; `enum
Index` carries the same rows in the same order in camel case. The golden tables
in `tests/plugin/DwellTests.cpp` and `tests/dsp/DwellDspTests.cpp` pin it, and
`./build-ui/tools/Release/measure_dwell.exe schema` prints it — **rebuild before
believing that tool**, since a binary built against an older table prints it
without complaint.

**The lane shares the main delay's voicing now, and that is where seven rows
went.** `link`, `lane_character`, `lane_stereo`, `lane_low_cut`,
`lane_high_cut`, `lane_mod_rate` and `lane_mod_depth` are **deleted**, and
everything after them renumbers with **no holes**. `character`, `stereo`, the
two cuts, the two modulation rows and `drive` now govern **both engines**;
**DUCK is main-engine only** (10 §6, §11.3); and the lane keeps what makes it a
lane — its TIME, LEVEL, tail, three gates and its own FX stage, tied to the
main's by `fx_link`.

**Every parameter is rack-automatable again.** A rack slot carries
`RackProcessor::kParamsPerSlot` = **32** host automation lanes, and anything past
that would fall to `SlotOverflow` — still working in the panel, the DSP, presets
and saved state, still automatable standalone, but with **no host lane in a
rack**. At 27 rows **Dwell uses none of that**: the whole table is inside the
grid with **five lanes spare**. The constraint is still the one that shaped this
module — it is why the 2026-09-21 table cut controls to fit, which is the
pressure Frosty removed by pulling the lane's voicing back (`15`) — but Dwell no
longer spends a single row outside the lanes, and the spare five are what let
`lane_note` be added on merit rather than argued against a budget.

| # | id | Range / units / law | Default | Smoothing |
|---|---|---|---|---|
| 0 | `time` | 1…2000 ms, log, step 0.01 | 375 | 10 §2's law; not smoothed |
| 1 | `sync` | bool | off (ships disabled) | crossfade |
| 2 | `note` | choice, 16 | `1/8D` (index 8; ships disabled) | as `time` |
| 3 | `feedback` | 0…100 %, lin, step 0.1 (`g = (1.05·fb^1.6)/P_c`, 10 §3; unity **97.0 % on every character**, 97–100 % self-oscillates) | 35 | 30 ms |
| 4 | `character` | choice, 3: Clean / Tape / Bucket-brigade | Clean | xfade |
| 5 | `stereo` | choice, 3: Stereo / Ping-pong / Dual offset | Stereo | xfade |
| 6 | `low_cut` | 20…1000 Hz, log, step 0.1 | 20 | 20 ms |
| 7 | `high_cut` | 1000…20000 Hz, log, step 0.1 (working corner capped at `min(18 kHz, 0.45·f_s)`) | 20000 | 20 ms |
| 8 | `mod_rate` | 0.1…8 Hz, log, step 0.01 | 0.6 | 20 ms |
| 9 | `mod_depth` | 0…100 %, lin, step 0.1 | 0 | 20 ms |
| 10 | `drive` | 0…100 %, lin, step 0.1 | 0 | 20 ms |
| 11 | `duck` | 0…24 dB, lin, step 0.1 | **0** | 20 ms |
| 12 | `mix` | 0…100 %, step 0.1, 10 §9's sin/cos hinge at 50 % | 35 | 30 ms; dry snaps exactly below the hinge |
| 13 | `send` | bool — gates the **lane's input** (10 §11.4) | off | 5 ms open / 15 ms close, half-cosine |
| 14 | `lane_gain` | −100…+100 %, lin, step 0.1; **0 is exact unity**, below decays, above builds (10 §11.2) | **−40** | 30 ms, snapped exactly at the detent |
| 15 | `hold` | bool — gates the lane's life; **off clears it** | off | 1 ms mute, then the clear; on is instant |
| 16 | `chop` | bool — gates the lane's **output** only | off | 1 ms raised cosine both edges (CALIBRATE; 10 §11.4) |
| 17 | `fx` | bool — the main loop's FX stage | off | none — the stage is skipped, not faded (10 §11a) |
| 18 | `fx_type` | choice, 3: Diffuse / Pan/Tremolo / Crush | Diffuse | xfade |
| 19 | `fx_amount` | 0…100 %, lin, step 0.1 | 35 | 20 ms |
| 20 | `lane_level` | −24…+24 dB, lin, step 0.01 | 0 | 20 ms |
| 21 | `lane_time` | 1…2000 ms, log, step 0.01 | 250 | 10 §2's law; not smoothed |
| 22 | `lane_note` | choice, 16, the same list as `note` | `1/8` (index 6; ships disabled) | as `lane_time` |
| 23 | `lane_fx` | bool — the lane's own FX stage | off | none — skipped, not faded |
| 24 | `lane_fx_type` | choice, 3, the same list as `fx_type` | Diffuse | xfade |
| 25 | `lane_fx_amount` | 0…100 %, lin, step 0.1 | 35 | 20 ms |
| 26 | `fx_link` | bool — the lane's FX trio (23–25) follows the main's (17–19) | **on** | none — a flag; nothing is seeded (10 §11.3) |

**The lane is ids 13–16 and 20–26**, and 10 §11 owns every one of their
meanings. Two rows exist because no other row could carry them: `lane_gain` is
the lane's **tail**, `lane_level` its **loudness**, and one cannot set the other
(`15`). **Lane DRIVE is deliberately absent**; `drive` (10) now drives both
engines, so there is nothing to append.

**`lane_note` (22) is the lane's half of SYNC, and there is only one SYNC.**
`sync` (1) **governs both engines**: the module is either on the grid or it is
not, and each engine then picks its own division — `note` (2) for the main,
`lane_note` (22) for the lane (DECIDED, Frosty 2026-09-23). A separate
`lane_sync` was considered and rejected: **wanting the main synced while the
lane free-runs is a strange thing to want**, and anyone who does turns SYNC off
for the module and sets both times in milliseconds. Without this row the lane
would have no division at all, so the moment `12`'s plumbing lands the main
delay would lock to the grid while the lane kept free-running in milliseconds
and **drifted against it** — a quarter underneath while throws land on a dotted
eighth is exactly the lane's rhythmic point, and it cannot survive one engine
following the tempo and the other ignoring it. `lane_note` **ships disabled
alongside SYNC and NOTE**, since `12` does not exist yet.

**The millisecond and note defaults agree at 120 BPM, and that is designed.**
`time` 375 ms against `note` 1/8D, which is 375 ms at 120; `lane_time` 250 ms
against `lane_note` 1/8, which is 250 ms at 120. **Enabling SYNC at 120 BPM is
therefore silent** — the same property `modules/vcomp`'s COMPLEX was built
around, and the reason `lane_note` defaults to **1/8 rather than to the main's
1/8D**: matching the main's division would have broken the agreement with
`lane_time`'s own default and made the toggle audible.

**One tie remains, and it is the one worth a parameter.** `fx_link` (26) ties
the lane's FX trio (23–25) to the main's (17–19), default on. FX is the part of
the lane's voice that stayed its own, because **a thrown word can be crushed
against a clean main delay** — a second set of cuts and modulation could not
earn its rows the same way, and those went (see above). **Nothing is seeded when
the tie releases**: the lane's three values are still there, untouched, so there
is no UI gesture and no automation pass that writes parameters (10 §11.3).

**Feedback normalisation (10 §3 owns it).** `P_c` is the character's reference
loop peak, computed at `prepare` and on any change of character, TIME or sample
rate — clean ≈ 0.999, tape ≈ 1.054, bucket-brigade 0.990–0.999 with TIME. It is
**not a parameter** and never appears in the schema; it exists so the knob
position at which the loop stops decaying is the **same on every character**,
which is what the lane's centre detent promises (15). Unity means the loop's
loudest band holds; the rest still decays, so a long hold darkens. **There is
one CHARACTER now**, so one `P_c` law serves both engines — but each evaluates
it at **its own TIME**, which matters on bucket-brigade, whose filters are
derived from a clock that TIME sets (10 §4).

**FX (10 §11a; candidates until ship).** `fx_type` runs least to most
intervention, the rule the other lists already follow: **Diffuse, Pan/Tremolo,
Crush** — **three**. Octave up, Octave down and Reverse were **cut on
2026-09-21** (docs/delay/15): the octaves compound in a feedback loop -- three
repeats is three octaves -- and Reverse was the only type needing a second
buffer, which must not be allocated on the audio thread. **Sweep was cut on
2026-09-22, and it was cut *because* VOICE was** — Sweep was specified as
VOICE's resonant centre being moved per repeat, so cutting VOICE left it with
nothing to sweep. Rather than give the FX stage its own resonant band-pass and
quietly reintroduce the filter that was just removed, the candidate goes.
**That is the non-obvious part and the reason it is written down**: anyone
reading only the FX list will see a gap where a sweep belongs and try to put one
back. Choice lists are append-only after ship, so this was the last moment to
remove it. **Index 0 is Diffuse, not
Off** — `fx` owns off, so a corrupt state landing on index 0 gives the gentlest
type with the stage still gated by a bool that defaults off. (The alternative,
folding Off into the list as index 0, costs a permanent redundant state and makes
"is FX on" two questions; rejected.) **These are candidates**: the list and its
order may change freely until ship and are **append-only forever afterwards**, so
any candidate that fails 14 §3's listening must be **removed before ship**, never
left in as a dead index. **Both engines read the one list** (ids 18 and 23), so
there is nothing to keep in step; `fx_link` ties their *values*, which is a
different thing from sharing the list.

**Expanded is not `fx`** — **DECIDED (Frosty, 2026-09-20): tied but not the
same.** `fx` (id 17) is the sound, lives on the compact panel, and lights when
on. The view stays DEQ's mechanism — `ModuleDef::expandedWidth`, a session-only
`view` attribute, never a parameter or preset value — but Dwell adds a small
on-panel arrow to open and close it, so the panel must be able to request the
host's `ui::ExpandButton` flag: a touch point beyond DEQ's host-bar-only switch
(`core/product/ModuleDef.h`, `core/AGENTS.md`). The tie: clicking `fx` on while
compact opens the view once, as a convenience; the arrow then closes it while
`fx` stays on; turning `fx` off never closes the view. Automation, preset load
and session recall never resize the module. Rack defaults compact; standalone
defaults expanded. **`fx_link` is not this kind of thing and needs no gesture**
(10 §11.3): it writes nothing at all, in either direction, whoever moves it.

**No control is named DWELL** — that is the module. **Permanent at ship**: ids,
their order, ranges, steps, defaults, and the choice lists **with their index
order**; new parameters append at the end. Three lists are frozen now — `note`,
`character`, `stereo` — and `fx_type` is free only until ship. The lane reuses
all four rather than declaring its own: two lists that have to stay identical
are two lists that can drift apart, and `lane_note` reusing `note`'s sixteen is
the clearest case of it. `sync`/`note`/`lane_note`'s slots and `note`'s order
are permanent now; **all three ship disabled** until 12's tempo plumbing lands,
on the one `kSyncIsEnabled` switch. Module id
`dwell` is final (DECIDED, Frosty 2026-09-20). **Nothing has shipped**, which is
the only reason the 2026-09-21 table could delete VOICE, renumber everything
after it, rename three rows, re-type a fourth and shorten a choice list, and the
only reason 2026-09-22 could shorten that list again by cutting Sweep — the last
moment any of it was legal.

**MIX law (10 §9 owns it; the earlier linear law here is superseded).**
`wet = sin(π·MIX)` for MIX ≤ 50 %, else 1; `dry = 1` for MIX ≤ 50 %, else
`cos(π(MIX − 0.5))`. The **dry signal is bit-exact unity from 0 to 50 %** — skip the multiply rather
than scale by a computed 1.0 — wet reaches full at 50 % and holds, and only dry
fades across 50–100 %. **Both engines sum into the wet before MIX**, so MIX
governs them together and MIX 0 still silences both.

`note` ascends in duration — `1/32, 1/16T, 1/32D, 1/16, 1/8T, 1/16D, 1/8, 1/4T,
1/8D, 1/4, 1/2T, 1/4D, 1/2, 1/1T, 1/2D, 1/1` — because the index *is* the
automation lane: a sweep must move monotonically in time, a clockwise knob must
lengthen. Anything appended later sits at the end, out of order, forever, so the
grid ships complete. **`lane_note` reads the same sixteen at the same indices**,
so `note` = 8 and `lane_note` = 6 are 1/8D and 1/8 on both engines and always
will be. The other three lists run least to most intervention, index
0 being the neutral value a corrupt state lands on: `character` least to most
coloured; `stereo` least to most divergent; `fx_type` least to most
intervention. 10 owns their meaning; its names win, not its order.

## 4. Building the test suites

`tests/dsp/DwellDspTests.cpp` (JUCE-free, CI `dsp`), `tests/plugin/DwellTests.cpp`
(golden schema, presets, XML round-trip, slot fit), `tools/measure/dwell/`.
Golden **state**, never audio; results name the machine. **Panel checks:
`docs/fet-comp/11-integration-and-test-plan.md` §4d.**

**a. Time accuracy and interpolation.** Impulse, `feedback` 0; sub-sample peak
by parabolic fit and cross-correlation; phases 0.0–0.9, 1–2000 ms, 44.1–192 kHz;
error ≤ 0.05 sample. Then a log sweep to 0.45·fs at those phases: clean flat
±0.01 dB with phase-to-phase variation ≤ 0.1 dB (it becomes modulation noise),
Hermite within 10 §1's curve.

**b. Time-change artefacts.** 1 kHz sine; a 300→150 ms step and a 2 s ramp, on
and off block boundaries, every character. No click above −60 dBFS; tape's glide
continuous and inside ρ ∈ [0.75, 1.25].

**c. Feedback decay and self-oscillation.** Impulse, `feedback` 0–100 by 5:
per-repeat decay within ±0.5 dB of `20·log10(g)` below onset, repeats-to-−60 dB
matching the reported tail. At full travel, 10 min at 48 kHz per character: it
sustains yet stays bounded — peak under the ceiling, converging within 1 dB,
DC ≤ −80 dBFS, no NaN, no denormal slowdown.

**The DC bound is the one that caught a real bug, so run it at maximum DRIVE
and keep it there.** With the blocker placed *before* the shaper, as 10 §4 used
to specify, the loop measured **−38.7 dBFS at DRIVE 100** — a clear fail; with
it after the shaper, **−114.3 dBFS** (AURORA, 2026-09-23, c4d2d33). Nothing in
the chain reveals the difference at DRIVE 0, because the shaper branches out
entirely, **so this assertion only bites at high DRIVE** — which is exactly why
the ordering survived so long. Treat a regression here as the blocker having
been moved back.

**d. In-loop filter stability.** `feedback` 100, both cuts at both extremes,
60 s at 44.1–192 kHz, **each engine** — the cuts are shared, so the same
extremes drive both, and the lane is run at `lane_gain` +100 and at its own
extremes of TIME: bounded by the safety clip, no divergence, NaN or DC
growth. Since 10 §4's coefficients depend only on `f_c/f_s`, the measured
response must agree across every rate within 1 %. (`voice` is gone — the cuts
are plain one-poles again, 10 §11.5 — so there is no resonant peak to compare;
what is compared is the cascade's magnitude at the corners.)

**e. The lane: send, gain, hold, chop, fx link** (10 §11; replaces the old throw
/ build / freeze item, whose controls no longer exist).

1. **The main loop is undisturbed — the headline assertion.** Two renders, 30 s
   of sustained input, `feedback` 50, every character: (A) `send` never touched,
   (B) `send` toggled on and off block boundaries throughout. With **`hold` off**
   the lane emits exact zeros, so the two renders must null **bit-exactly,
   sample for sample** — not a −120 dB figure. Then the stronger form: with
   **`hold` on and the lane fed**, the **main engine's own output tap** must be
   bit-identical between (A) and (B). **That second form needs the DSP to expose
   the main wet bus before the lane is summed** — it cannot be made from
   parameters alone, because `lane_level` bottoms at −24 dB rather than −∞ and
   no parameter silences a running lane. Either the DSP exposes the tap for
   `tests/dsp` (which links it directly), or the shipped assertion is the
   `hold`-off form plus a review that the `s` term is gone from 10 §3's
   injection. **Open, and it is a build decision, not a spec one.**
2. **`hold` off clears.** `hold` on, `lane_gain` at the detent, `send` held over
   a burst, then released: assert a sustained lane output. Switch `hold` off,
   then on, then `send` a *different* short burst. The output must contain
   **only** the new burst — the lane's contribution between the clear and the
   new send is **exact zeros**, and correlation against the first burst is at
   the noise floor. The clear itself produces nothing above −60 dBFS (the 1 ms
   mute precedes the zeroing). Every character, on and off block boundaries.
   This is the test that a mute would pass and a clear-and-mute confusion would
   fail: a muted circulating buffer stacks on the next send.
3. **`chop` is click-free and non-destructive.** A held chord (`hold` on,
   detent, `send` closed), `chop` toggled at a sixteenth-note rate for 30 s, on
   and off block boundaries. Two assertions: (i) the **lane's contents are
   bit-identical** to a `chop`-never render at every sample — `chop` touches the
   output only, and that is provable rather than audible; (ii) no click above
   −60 dBFS on either edge, **measured on content band-limited to 5 kHz**
   (DECIDED, Frosty 2026-09-22; 10 §11.4). The fade stays at 1 ms, because
   tightness is what a rhythmic gate is for, and the ~3 ms a broadband figure
   would need costs sixteenths above ~160 BPM. Above 5 kHz the edge is
   measurable; `14` records it by sweep rather than asserting it away.
4. **Summing is bounded by the clip.** `hold` on, detent, `lane_level` 0 dB:
   `send` a full-scale burst once per lane period for 60 s, every character. The
   circulating peak converges to and stays under the clip ceiling; the increase
   from burst 8 to burst 64 is ≤ 0.5 dB (CALIBRATE); no NaN, no denormal
   slowdown, DC ≤ −80 dBFS. Then the transient bound: the ring may momentarily
   hold above the ceiling by no more than one input peak (10 §11.4) and never
   more.
5. **The clip bounds the lane — at the top of LEVEL's travel, not at unity**
   (`15`). `lane_gain` +100, **`lane_level` +24 dB**, `hold` on, one impulse, 10
   minutes at 48 kHz per character: bounded, converging within 1 dB, DC
   ≤ −80 dBFS, no NaN, no divergence. **The output is expected to exceed 0 dBFS
   by up to ~24 dB and that is not a failure** — the assertion is boundedness,
   not level. Run at unity instead it would prove nothing, because unity never
   reaches the clip.
6. **Unity holds exactly at the detent, per character.** `lane_gain` exactly 0,
   `hold` on, one impulse, 60 s at 44.1–192 kHz, every character. On **Clean**
   with both cuts on their rails, `drive` 0 and `lane_fx` off the chain is
   neutral and the assertion is level drift over the whole 60 s ≤ 0.1 dB, DC
   ≤ −80 dBFS, no NaN. On **Tape and Bucket-brigade the hold colours by design**
   (10 §11.5), so the assertion there is **per-lap magnitude ≤ 1 and level
   monotone non-increasing**, never a spectrum. This test is the one that proves
   10 §3's `P_c` is doing its job: without the normalisation the detent is not
   unity on tape, and this fails.
7. **`fx_link`, and the shared voicing it is the exception to.** With `fx_link`
   on, two renders in which the lane's FX trio (23–25) is set to opposite
   extremes must null bit-exactly — the lane's stage reads the main's three, so
   its own values cannot leak. Turning it off must make them reach the audio,
   which is the case the parameter exists for: **a lane crushed against a clean
   main delay**. **Automating it writes no parameters**: assert the host-visible
   parameter-change count across a `fx_link` automation pass is exactly **zero**
   — there is no seeding left in the module, so that assertion is now simply
   that nothing writes. Nothing jumps across the switch either (nothing above
   −60 dBFS).
   Then the shared voicing itself: moving `character`, `stereo`, either cut,
   either modulation row or `drive` must change **both** engines' output, and
   moving `duck` must change **only the main's** — the ducker never reaches the
   lane (10 §6). Both directions are asserted, because a shared parameter
   silently reaching one engine only is the failure this shape invites.
8. **`send`'s ramp.** `send` toggled on and off block boundaries under sustained
   input: no discontinuity above −60 dBFS on either edge; fully open and fully
   closed within 10 §11.4's 5/15 ms ±20 %.

**f. THD and accumulated aliasing.** Fold aliases onto bins no harmonic occupies
(48 kHz: 9 kHz tone, image 21 kHz — `SatDspTests::testOversampling`); gate
repeat N, Goertzel at bin centres. N = 1/4/10/32 × `drive` × `character` × fs.
10 §4's acceptance: **≤ −60 dBFS after 10 repeats at maximum DRIVE**, growth
sub-linear in N. Failure triggers 10's half-band fallback; (k)'s latency
assertion must still hold after it.

**g. Ducking.** Repeats at `feedback` 50, a 100 ms dry burst at −6 dBFS, 500 ms
gap; wet envelope isolated by nulling a `duck` = 0 render. Attack and release
within 10 §6 ±20 %, depth tracking `duck` ±0.5 dB, `duck` = 0 bit-exact, dry
muted gives no ducking.

**h. Tempo sync, both engines.** Synthetic transport, BPM {20…999} × all 16
notes, timed by (a): `60000/BPM × multiplier` within ±0.1 ms, halving past the
maximum per 10 §7, never wrapping. **Run over `note` and `lane_note`
independently, and over both at once**: with one `sync` governing both engines
(10 §11.7), the failure to catch is one engine following the grid while the
other free-runs — assert the lane's measured repeat period tracks `lane_note`
and not `note`, and that neither engine is still reading its millisecond time
while `sync` is on. Ramp 120→140 and jump 120→60: no click above −60 dBFS on
**either** engine. Transport stopped: BPM frozen, both tails still decaying.

**The silent-toggle property is its own assertion**: at **120 BPM with the
defaults** — `time` 375 ms / `note` 1/8D, `lane_time` 250 ms / `lane_note` 1/8 —
toggling `sync` must change **nothing audible** on either engine. Assert the
measured periods before and after the toggle agree within (a)'s 0.05-sample
tolerance, and that the toggle itself gives nothing above −60 dBFS. This test
fails if anyone ever "tidies" `lane_note`'s default to match the main's 1/8D.

**i. Mix law.** The dry null is **bit-exact, sample for sample** — not
a −120 dB figure — at MIX 0, 25 and 50 % with the wet path silenced, at every
character. Wet-only level identical at 50 % and 100 % within 0.01 dB; at 50 %
both paths are full. Across 50–100 % the dry level is monotone and matches
`cos(π(MIX − 0.5))` within ±0.05 dB; automating through the knee gives nothing
above −80 dB.

**j. Bypass tail.** Bypass mutes input injection but keeps processing (10 §9),
so the tail decays rather than cuts and re-enabling never clicks above −60 dB;
across `time` × `feedback` × `character`, `tailSecondsForParams` is never below
the measured time to −60 dBFS.

**k. Invariance, robustness, cost.** Across 44.1–192 kHz, times, decays, corners,
mod rate and duck ballistics hold; block sizes 1/32/64/512/1023 and a random
schedule identical to −120 dB. Silence decays to exact zeros with no CPU rise
(tail ≤ 1.1× steady) — **including on TAPE, whose character floor modulates at
MOD DEPTH 0** (10 §5a). That holds structurally rather than by a gate: the floor
moves the **read position**, so a zeroed ring still reads zero. **Run the
silence case on every character**, since a floor implemented as an output gain
instead would pass on clean and fail here. No NaN at any extreme; an injected
NaN contained within one tail.

**CPU: an absolute ceiling plus a regression guard** (DECIDED, Frosty
2026-09-23). The old wording — "≤ 1.5× at defaults, ≤ 3.0× heaviest" against a
named reference tool — **is struck, and the ratio form goes with it.** It was
unusable in three independent ways, all found by the bench of 2026-09-22 on
AURORA: it named a "bench" mode that exists in no measurement tool in this
tree; it named a tool "measure_ltvcomp", which is not a target (the module id went
`vcomp` → `ltvcomp` for the product, not for the tool — it is `measure_vcomp`);
and it never said what settings the reference ran at, a silence that moves the
answer by **2.7×** on its own. It was also breached on every reading of it:
**6.81× at defaults against ≤ 1.5×, and 23.74× at the true heaviest against
≤ 3.0×** — and still breached, by 3.0× over, against the most generous
denominator available. A budget nobody can reproduce is not a budget, so the
unreproducible denominator is fixed by **not having one**.

What replaces it is a **percentage of one core per instance at a named sample
rate and block size** — the figure a loaded rack actually cares about — plus a
**regression guard against the recorded baseline**, so a change that makes the
module heavier is caught without a reference that can drift underneath it.

**The ceiling** (a line not to cross, per instance, stereo, block 512):

| | 48 kHz | 192 kHz |
|---|---|---|
| at defaults | **≤ 1.15 %** of one core | **≤ 4.1 %** |
| at defaults with a control automated per block | **≤ 2.35 %** | **≤ 9.1 %** |
| at the heaviest, static | **≤ 4.0 %** | **≤ 15.0 %** |
| at the heaviest with a control automated per block | **≤ 5.2 %** | **≤ 19.9 %** |

**The regression guard:** no configuration in the recorded baseline below may
measure **more than 1.15×** its recorded figure on the same machine.

**Why these numbers.** Every ceiling is the measured baseline **× 1.30**, and
the guard is **× 1.15**; neither is a round number and both are set by what the
method can resolve. Within one process the spread is 1–3 %, but the *same*
Dwell configuration shifts by up to **~10 % between processes**, because the
rings are power-of-two sized and where the allocator puts them changes the
cache-conflict pattern. A guard tighter than ~10 % would fire on the allocator;
15 % will not, and it is half the distance to the ceiling, so **the guard
always fires first and the ceiling is the thing it protects**. Thirty per cent
of headroom is about three times the worst observed noise — enough that the
gate is not flaky, tight enough that a real regression (an extra filter, a
second sweep, a tap count going the wrong way) trips it long before anyone
hears it. **The ceiling is not a target to beat.** Re-base it *downward* freely
and say so; raising it costs a fresh measurement and Frosty's word, the same
ratchet BMO Tune RT's latency curve runs on.

**And the eight slots are why the top rate is not generous.** At the 48 kHz
heaviest ceiling, eight Dwells in a full rack is **32 % of one core** —
comfortable. At 192 kHz the same arithmetic is **120 %, over one core
already**, against **92.5 % measured**. (Both figures are arithmetic on a
single measured instance; **eight instances were never run.**) A rack that is
eight Dwells all on Clean + Diffuse with both engines live is not a session
anyone builds — two or three of them beside other modules is — but there is no
room at the top rate to hand out headroom for its own sake, which is why the
192 kHz rows track their measurements as closely as the 48 kHz ones do. The
same case is ~130 MB of rings (`10` §10), so CPU is not the binding constraint
at 192 kHz; memory is.

**Measurement conditions — the reproducibility is the point of this rewrite, so
these are normative.** A reading taken any other way is not comparable and must
not be recorded as a baseline.

- **Build:** Release, x64, `BMO_DSP_ONLY=ON`, linked against **the
  repository's own Release static libraries** (`bmo_dwell_dsp`, `bmo_dsp`).
  Name the targets explicitly — an untargeted build installs plugins over the
  user's set. Flags as `build-dsp/` gives its own targets in Release
  (`/O2 /Ob2 /MD /std:c++20 /DNDEBUG` under MSVC); the baseline below was taken
  under MSVC 19.44.35228.
- **Signal path:** `prepare`, then per block `setParams` **then** `process`,
  stereo, through the ordinary `ModuleDsp` interface, no host and no JUCE.
  `setParams` is called once per block *because a host does*, and it is inside
  the timed region deliberately — it is where the loop-peak sweep lives, and
  leaving it out hides the automated-control rows entirely.
- **Rate and block:** 48 kHz and 192 kHz, **block 512**, both gated. 44.1 and
  96 kHz are recorded but not gated.
- **Input:** a deterministic pseudo-noise bed at about −14 dBFS RMS under a slow
  amplitude envelope, so the ducker's follower, the compander and the gate all
  see something that moves. **The same samples feed every configuration.**
- **Run count and duration:** **100 runs × 10 s of audio** for every gated row,
  25 × 10 s for exploratory rows; **two warm-up runs discarded per
  configuration**; the **median** is the reported figure. Every timed run is
  followed by an untimed output check — a row with any non-finite sample is not
  a reading.
- **Core pinning.** Pin the process to **one logical CPU at high priority.**
  On a hybrid part (AURORA is an i7-12700H) an unpinned run migrates between
  P-cores and E-cores and the timings go **bimodal**; pinning cut the
  run-to-run spread by about **3×**. This is not optional — without it the
  numbers are not repeatable on the same machine, let alone across machines.
- **Clock spin-up.** Burn **20 s of the heaviest case before timing
  anything.** The first sweep of the 2026-09-22 bench measured its *early*
  configurations 2–3× slow and its *late* ones at full speed inside one
  process: a core ramping to its working clock, not a property of any
  configuration. **That whole first sweep was discarded.** With the spin-up in
  place, an isolated process and a 60-configuration process agree to within a
  few per cent.
- **Machine quiet.** Three heaviest-case rows in the discarded sweep came back
  with a standard deviation of 67–99 ms against a normal 2–8, from nothing more
  than someone polling the machine while it measured. Nothing in the baseline
  below has a standard deviation above **3 %** of its median.

**The baseline — what the guard guards.** All **MEASURED on AURORA,
2026-09-22**, worktree `bmo-mix-rack-333-dwell` at **720b8b7** (stages 2a–2e,
the DSP complete), under the conditions above. Milliseconds of CPU per 10 s of
audio, and the same figure as a percentage of one pinned P-core.

| configuration, 48 kHz / 512 | med ms / 10 s | % of one core |
|---|---|---|
| **at defaults** | **88.04** | **0.880** |
| at defaults, HOLD on and the lane fed | 178.08 | 1.781 |
| at defaults, TIME automated per block | 179.97 | 1.800 |
| **at the heaviest, static** | **306.19** | **3.062** |
| at the heaviest, TIME automated per block | 399.11 | 3.991 |

Reproducibility of the two headline rows across four independent 48 kHz
measurements: defaults 88.04 / 88.13 / 89.34 / 90.45 ms; heaviest 300.49 /
301.22 / 306.19 / 308.03 ms.

| % of one core | 44.1 kHz | 48 kHz | 96 kHz | 192 kHz |
|---|---|---|---|---|
| at defaults | 0.812 | 0.880 | 1.632 | 3.170 |
| at defaults, TIME automated per block | — | 1.800 | — | 6.962 |
| at the heaviest, static | 2.787 | 3.062 | 5.914 | 11.561 |
| at the heaviest, TIME automated per block | — | 3.991 | — | 15.287 |

Each ceiling above is its 48 kHz or 192 kHz entry here **× 1.30**, rounded up
to three significant figures. The 44.1 and 96 kHz columns are recorded so the
scaling can be checked; they are not gated, and neither is the
HOLD-on-at-defaults row.

**Cost scales close to linearly with sample rate** — there is no per-block
cliff — and **block size is flat**: at 32, 64, 128 and 1023 samples, defaults
measure 91.1–92.2 ms and the heaviest 301.8–315.0 ms. That is because the
1024-point loop-peak sweep does not run while the parameters are static, which
is exactly what the automated rows price.

**What the heaviest case actually is, because it is not what this section used
to predict.** It is **Clean + Diffuse on both paths**, both engines live, DRIVE
100, modulation 8 Hz / 100 %, FEEDBACK 95, DUCK 24, MIX 50, dual offset, TIME
and LANE TIME at 2000 ms, FX AMOUNT 100 on both. **It is not bucket-brigade** —
BBD, with its clock-derived Butterworths and its compander control ring, sits
*between* Clean and Tape (Clean + Diffuse 306.19 ms, BBD + Diffuse 244.76,
Tape + Diffuse 209.71). **Nobody should assume the compander is the cost.**
Unlinking the two paths' FX types buys nothing worse (mixed Crush/Diffuse,
`fx_link` off: 224.65 ms), and nineteen single-knob probes off the worst case
moved **nothing upward**, so 306 ms is the static ceiling and not a guess at
one. **DRIVE is the single dominant control on top of the character**: DRIVE
100 → 0 takes 31.0 % off, the ADAA residual shaper being about a third of the
heaviest case on its own and branched past entirely at DRIVE 0. Nothing else
moves it more than 10 %, and TIME is flat from 17.5 ms to 2000 ms.

**The automated rows are a deliberate widening of what this budget covers,
because the old one did not say and the answer differs by a factor of two.**
`DelayEngine::setParams` re-sweeps a 1024-point grid whenever TIME moves, and
each point does a sinc read; on bucket-brigade a TIME move is also a filter
move (`f_clk = N/2T`), so the filter magnitudes re-sweep too. TIME alternating
between 370 and 380 ms every block — an ordinary thing for a host to do —
costs **2.04× at defaults**, 1.43× on BBD at its heaviest, 1.30× on Clean at
its heaviest. **The genuine worst case anywhere in the bench is Clean at the
heaviest with TIME automated: 3.99 % of one core at 48 kHz, 15.29 % at
192 kHz**, and that is the row the 5.2 % / 19.9 % ceilings are cut from.

**Two findings that read better than any ratio suggested.**

1. **The lane costs nothing at the shipped defaults, and it is not a
   rounding.** HOLD ships off, and the second engine **genuinely does not
   run**: 88.04 ms with the lane dead against 178.08 ms with it live, same
   process, everything else identical. This section's long-standing claim —
   "`hold` ships off, the lane is cleared and dead and costs a branch" — is
   **confirmed as measured**, not merely argued. **When it does run it adds
   84–102 %** of the engine it sits beside (+102 % at defaults, +84–88 % on
   the heavy BBD cases, +85–97 % at 192 kHz). **One engine is half the module**
   — the largest structural cost in it, larger than DRIVE, larger than any
   character, larger than any FX type — and if the split question ever
   returns, that is the number.
2. **§4l's ≤ 1.3× per FX type holds, with margin, and §4l is not touched
   here.** Measured per engine against the FX-off loop at otherwise identical
   settings in the same process, medians of six independent processes:
   **Diffuse 1.200×, Pan/Tremolo 1.022×, Crush 0.949×** on Clean at defaults;
   1.125× / 1.047× / 0.967× on bucket-brigade driven. Worst candidate 1.200×
   against ≤ 1.3×. Diffuse is the only one that costs anything material, which
   is what a six-stage Schroeder allpass chain should cost against one LFO and
   a quantiser — **for `14`'s listening round, if Diffuse wins on sound it
   costs about a fifth of a delay engine to keep, and the other two are free.**
   **One measured oddity, flagged rather than smoothed:** Crush reads
   consistently *cheaper* than FX off, reproducing in all six processes with
   the paired ratio between 0.918 and 0.958 — outside the within-process
   spread, and unexplained from the code. It changes no verdict, but **it must
   not be quoted as "Crush is free" until somebody understands why.**

**One optimisation is open and this budget does not assume its outcome.**
**Clean's 32-tap polyphase Kaiser sinc read is the single largest cost in the
module and the whole reason Clean is the most expensive character**: bare, at
48 kHz, Clean is 89.82 ms against Tape's 50.30 and bucket-brigade's 64.79 —
**1.79× Tape, and identically 1.79× at 192 kHz**, which is the interpolator and
nothing else (`10` §1 gives Tape and BBD a 4-point Hermite and calls the sinc
"the only place ~32 MACs is spent"). **The taps came down to 24 on 2026-10-01**
(Frosty, blind A/B; `10` §1 has the reason). The baseline above is still the
32-tap engine at 720b8b7, so **it now over-states the module**. The bench of
2026-09-22 put 24 taps at 13 % under 32 on Clean bare and 8 % under at the
heaviest, so every row should come down. **The guard cannot misfire on this**,
because a cheaper module reads under its baseline, never over it. But the
ceilings now sit further above the module than the ×1.30 argument intends.
**Re-base this whole baseline downward and say so**, and do it through the
`cpu` mode below, not a rebuilt scratch harness. The harness that took the
figures above was in a session scratchpad and no longer exists, which is the
reproducibility gap below in a nutshell.

**What is still not reproducible from the repository alone, and it is the one
thing left open.** The baseline above was taken with a **single-file harness
outside the repository**, in a scratchpad, because the bench was commissioned
to write nothing into the tree. `14` §2 wants measurements to come "from a tool
in the tree driving the shipping path", and this one does not yet. **No
measurement tool in this tree has a CPU mode**: `measure_dwell` has `schema`
and `latency` and nothing else. **The follow-up is to land a `cpu` mode in
`tools/measure/dwell/main.cpp`** that implements the conditions above — the
pinning and the spin-up included, or it will measure the laptop instead of the
module — and to re-take the baseline through it. Until that exists, **the
figures here are quotable as measurements but the gate cannot be run by anyone
who only has the repo**, and that, not the numbers, is the last unfinished part
of this section.

**All four rings** are allocated once in `prepare()` from the fixed maximum —
two audio rings, one per engine, and **two compander control rings of the same
length** (10 §4), which is **16 MB per instance at 192 kHz** (10 §10). Earlier
figures of 4.0 and 8.0 MB counted one engine and then the audio rings alone. The
control rings are allocated whichever character is selected, so **assert the
allocation is character-independent**: switching to bucket-brigade in
`process()` must not allocate. No allocation in `process()` at all, including
when `hold` or either `fx` turns on. `latencyForParams` is **exactly 0**
everywhere, both engines.

**l. In-loop FX (10 §11a).** There is **one stage per engine** and everything
here is asserted **per path**, `fx`/`fx_type`/`fx_amount` on the main and
`lane_fx`/`lane_fx_type`/`lane_fx_amount` on the lane.

FX off must be **bit-identical** to a build without the stage — null the two
renders sample for sample, every character, every type index, amount at both
ends, **for each path independently and for both off together**: the stage is
skipped, so the type and amount cannot leak. **The two stages share no state**,
which is its own assertion: driving the lane's stage at amount 100 while the
main's is off must not change the main's output by one bit, and the reverse.
Per candidate, at `feedback` 100 and amount 100, 60 s at 44.1–192 kHz: bounded,
no divergence, NaN, denormal slowdown or DC growth, as (d).
`latencyForParams` is 0 for every candidate. Crush is **exempt from (f)'s
−60 dBFS floor**; its assertion is that the non-harmonic floor is non-increasing
from repeat 10 to 32. Toggling either `fx` and sweeping either type on and off
block boundaries: nothing above −60 dBFS on either edge. Timed per candidate
under (k)'s measurement conditions, against the FX-off loop at otherwise
identical settings **in the same process**: ≤ 1.3× any one, ≤ 1.5× heaviest,
**per engine**, and FX off within noise of the pre-FX build. **This budget
holds as measured** (AURORA, 2026-09-22) — worst candidate Diffuse at 1.200×;
the figures and one flagged oddity are in (k). No candidate allocates a second buffer --
Reverse was cut on 2026-09-21 -- so the FX stage allocates nothing at all, and
`process()` must allocate nothing when either `fx` turns on. **There is no
FREEZE to bypass the stage any more** (10 §11.5): the lane's detent deliberately
keeps the stage running, so a held chord with `lane_fx` on is *expected* to
change lap by lap, and the old "FREEZE with FX selected equals FREEZE with FX
off" assertion is struck rather than reworded.
**The independence assertions are run with `fx_link` off**, which is what makes
them meaningful; with it on the lane's trio is a copy of the main's by
construction. State: `fx`/`fx_type`/`fx_amount`, the lane's three and `fx_link`
round-trip in the golden schema and in presets,
while the **expanded view round-trips in the session only**, separately, and
differs by default between rack (compact) and standalone (expanded). Test
direction: automating, preset-loading or recalling `fx` never resizes the
module; a user click that turns `fx` on from compact opens the view once; the
arrow toggles the view alone, touching no parameter and no audio.

**m. Two engines, every invariant.** `15` requires every invariant above to hold
for **both** engines, so (d), (f), (k) and (l) are each re-run a second time with
**`hold` on, the lane fed, `fx_link` off and the lane's own rows at their
extremes** — the whole of (k)'s battery included: 44.1–192 kHz, block sizes
1/32/64/512/1023 and a random schedule identical to −120 dB, silence decaying to
exact zeros with no CPU rise, no NaN at any extreme, `latencyForParams` exactly
0. **An injected NaN must be contained within the engine it was injected into**:
a NaN in the lane must never reach the main's ring, which is (e) 1's claim in a
different currency. **The engine is one type instantiated twice** (10 §11.1), so
this is one body of code exercised under two parameter sets rather than two code
paths to keep in step — which is most of why the second run is affordable.

**Tail reporting, both engines.** `tailSecondsForParams` reports **the larger of
the two engines' tails** from parameters only (10 §11.6): the main's per 10 §9;
the lane's when `lane_gain < 0` **and `hold` is on**; and the 30 s clamp whenever
`hold` is on and `lane_gain ≥ 0`, because a hold or a build does not decay. With
`hold` off the lane contributes nothing. (j)'s assertion — the reported tail is
never below the measured time to −60 dBFS — is then run over `lane_time` ×
`lane_gain` × `hold` as well.

## Open decisions for Frosty

1. **Trademark-search "Dwell"** — blocks the permanent identity row. (README
   item 14 accepts the USPTO search alone; this is the record of what that
   covers.)
2. **`chop`'s fade and its acceptance band** (10 §11.4): band-limit the −60 dBFS
   assertion, lengthen the fade to ~3 ms, or record the measured edge. (e) 3 is
   written to whichever. **This one needs a measurement, not a ruling** — it is
   the last open item in the pack that a sweep settles.
3. **Whether the main wet bus is exposed for (e) 1's stronger form**, or the
   shipped assertion stays the `hold`-off null plus a code review.

**Settled since this list was written.** The accent is the orchid `#f094e6`
(README item 16), so §1's three candidates and 10 §0's gold-gap assumption are
history rather than a choice. `throwMode` no longer exists, and with it the
question of BUILD's target and ramp as parameters — 10 §11.2's `lane_gain` law
is not automatable piecewise. On 2026-09-22 Frosty settled **Sweep is cut**,
leaving three FX types, and **`g_max` is a CALIBRATE value** settled by ear in
`14` §3's listening round rather than decided on paper.

**On 2026-09-23 he settled the shape of the module**, which closed the LINK
questions by removing LINK: the lane **shares** the main delay's voicing, seven
rows are deleted, the schema is **26**, `fx_link` is an ordinary on-lane
parameter at id 26, and stage 2 builds **one reusable engine instantiated
twice** (10 §11.1). The reasoning is in `15`: controls were being cut to fit 32
lanes rather than on merit, which is a sign the module was doing two modules'
work.

**Blocking unknown**: 01 has no MEASURED figure for a modern clean delay's
feedback ceiling or maximum time; 10 sets both by decision (1.05, 2000 ms) and
both freeze at ship.
