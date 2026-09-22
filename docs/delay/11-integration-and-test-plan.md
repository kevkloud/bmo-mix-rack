# BMO Dwell — Integration & Test Direction

Written on AURORA, 2026-09-20. No code here; this says what the devs build. DSP
meaning and constants come **per `docs/delay/10-dsp-spec.md`** (cited as 10);
conventions per `docs/1176-comp/00-repo-conventions.md` and this folder's `00`.

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

`specs()` order = `enum Index` order. **Thirty-three parameters, ids 0–32**
(`15`'s table, README Decided items 17 and 24–26) — the twenty-row table this
replaced carried `voice`, `throw`, `throwMode`, `freeze` and a seven-entry FX
list, none of which exist. The ids below are the **string ids** as
`modules/dwell/params.h` declares them; `enum Index` carries the same rows in
the same order in camel case. The golden tables in
`tests/plugin/DwellTests.cpp` and `tests/dsp/DwellDspTests.cpp` pin it, and
`./build-ui/tools/Release/measure_dwell.exe schema` prints it. **`fx_link` (id
32) and the three-entry FX list are Frosty's 2026-09-22 decisions**; they are in
`params.h` as of that date, but a **`build-ui/` binary built before it still
prints the 32-row table with Sweep in the list** — that is a stale exe, not a
disagreement. Rebuild before believing it.

**Exactly one row is off-lane, and that is deliberate.** A rack slot carries
`RackProcessor::kParamsPerSlot` = 32 host automation lanes, so ids 0–31 are
automatable everywhere. **`fx_link` at id 32 is not**: `SlotOverflow` keeps it
working in the panel, the DSP, presets and saved state, and it automates in the
standalone plugin, but **in a rack it has no host lane**. Frosty put it there on
purpose — *"leave this separate fx link off a lane in case it needs to be cut
later"* (2026-09-22). It is a set-and-forget tie, which is the cheapest kind of
parameter to spend the overflow on, and being outside the grid is what keeps
removing it later from renumbering anything. **So "every Dwell parameter is
rack-automatable" is no longer true**, and any document still saying it means
ids 0–31.

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
| 20 | `link` | bool — the lane's **voicing** follows the main's (ids 23–28, six rows; never the FX trio) | **on** | none — a flag; the seed is a UI gesture (10 §11.3) |
| 21 | `lane_level` | −24…+24 dB, lin, step 0.01 | 0 | 20 ms |
| 22 | `lane_time` | 1…2000 ms, log, step 0.01 | 250 | 10 §2's law; not smoothed |
| 23 | `lane_character` | choice, 3, the same list as `character` | Clean | xfade |
| 24 | `lane_stereo` | choice, 3, the same list as `stereo` | Stereo | xfade |
| 25 | `lane_low_cut` | 20…1000 Hz, log, step 0.1 | 20 | 20 ms |
| 26 | `lane_high_cut` | 1000…20000 Hz, log, step 0.1 (same cap) | 20000 | 20 ms |
| 27 | `lane_mod_rate` | 0.1…8 Hz, log, step 0.01 | 0.6 | 20 ms |
| 28 | `lane_mod_depth` | 0…100 %, lin, step 0.1 | 0 | 20 ms |
| 29 | `lane_fx` | bool — the lane's own FX stage | off | none — skipped, not faded |
| 30 | `lane_fx_type` | choice, 3, the same list as `fx_type` | Diffuse | xfade |
| 31 | `lane_fx_amount` | 0…100 %, lin, step 0.1 | 35 | 20 ms |
| 32 | `fx_link` | bool — the lane's FX trio (29–31) follows the main's (17–19). **Off-lane: `SlotOverflow`, no host automation lane in a rack** | **on** | none — a flag; the seed is a UI gesture, as `link`'s is |

**The lane is ids 13–16 and 20–31**, and 10 §11 owns every one of their
meanings. Two rows exist because no other row could carry them: `lane_gain` is
the lane's **tail**, `lane_level` its **loudness**, and one cannot set the other
(`15`). **Lane DRIVE is deliberately absent** and is the one to reconsider after
listening: an append lands at id 33, past the rack's lanes beside `fx_link`,
which costs little for a set-and-forget amount.

**The lane has two ties, not one, and they are separate on purpose.** `link`
(20) mirrors the **voicing** — `lane_character`, `lane_stereo`, the two lane
cuts and the two lane modulation rows, **six parameters, ids 23–28**.
`fx_link` (32) mirrors the **FX trio**, 29–31 against 17–19. Frosty chose
independent FX precisely so a thrown word can be crushed against a clean main
delay; folding FX into `link` would mean unlinking the whole voicing to get
that, which is the opposite of what the split is for.

**Feedback normalisation (10 §3 owns it).** `P_c` is the character's reference
loop peak, computed at `prepare` and on any change of character, TIME or sample
rate — clean ≈ 0.999, tape ≈ 1.054, bucket-brigade 0.990–0.999 with TIME. It is
**not a parameter** and never appears in the schema; it exists so the knob
position at which the loop stops decaying is the **same on every character**,
which is what the lane's centre detent promises (15). Unity means the loop's
loudest band holds; the rest still decays, so a long hold darkens. The lane's
detent divides by the **lane's own** character's `P_c`, not the main's.

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
left in as a dead index. **Both engines read the one list** (ids 18 and 30), so
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
defaults expanded. **Both unlink seeds are the same kind of thing** (10 §11.3):
a click writes the lane's six mirrored voicing rows — or, for `fx_link`, its FX
trio — from the main's current values, and the parameter changing writes
nothing.

**No control is named DWELL** — that is the module. **Permanent at ship**: ids,
their order, ranges, steps, defaults, and the choice lists **with their index
order**; new parameters append at the end. Three lists are frozen now — `note`,
`character`, `stereo` — and `fx_type` is free only until ship. The lane reuses
all four rather than declaring its own: two lists that have to stay identical
are two lists that can drift apart. `sync`/`note`'s slots and `note`'s order are
permanent now; `sync` ships disabled until 12's tempo plumbing lands. Module id
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
grid ships complete. The other three lists run least to most intervention, index
0 being the neutral value a corrupt state lands on: `character` least to most
coloured; `stereo` least to most divergent; `fx_type` least to most
intervention. 10 owns their meaning; its names win, not its order.

## 4. Building the test suites

`tests/dsp/DwellDspTests.cpp` (JUCE-free, CI `dsp`), `tests/plugin/DwellTests.cpp`
(golden schema, presets, XML round-trip, slot fit), `tools/measure/dwell/`.
Golden **state**, never audio; results name the machine. **Panel checks:
`docs/1176-comp/11-integration-and-test-plan.md` §4d.**

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

**d. In-loop filter stability.** `feedback` 100, both cuts at both extremes,
60 s at 44.1–192 kHz, **each engine** (the lane at `lane_gain` +100 with its own
cuts at their extremes): bounded by the safety clip, no divergence, NaN or DC
growth. Since 10 §4's coefficients depend only on `f_c/f_s`, the measured
response must agree across every rate within 1 %. (`voice` is gone — the cuts
are plain one-poles again, 10 §11.5 — so there is no resonant peak to compare;
what is compared is the cascade's magnitude at the corners.)

**e. The lane: send, gain, hold, chop, link** (10 §11; replaces the old throw /
build / freeze item, whose controls no longer exist).

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
   −60 dBFS on either edge. **The acceptance band for (ii) is open** (10 §11.4):
   a 1 ms raised cosine does not meet a broadband −60 dBFS figure on bright
   sustained content, so this is either asserted on content band-limited to
   5 kHz, or the fade lengthens to ~3 ms and the assertion goes broadband.
   Frosty's choice; the test is written to whichever, and **not written to a
   figure the fade cannot meet**.
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
   with both lane cuts on their rails, `drive` 0 and `lane_fx` off the chain is
   neutral and the assertion is level drift over the whole 60 s ≤ 0.1 dB, DC
   ≤ −80 dBFS, no NaN. On **Tape and Bucket-brigade the hold colours by design**
   (10 §11.5), so the assertion there is **per-lap magnitude ≤ 1 and level
   monotone non-increasing**, never a spectrum. This test is the one that proves
   10 §3's `P_c` is doing its job: without the normalisation the detent is not
   unity on tape, and this fails.
7. **`link` and `fx_link`, tested the same way and tested apart.** With `link`
   on, two renders in which the **six** mirrored voicing rows (23–28) are set to
   opposite extremes must null bit-exactly — the lane reads the main, so its own
   values cannot leak. With `fx_link` on, the same for the FX trio (29–31). The
   unlink **gesture** writes its own set from the main's current values and
   nothing jumps (nothing above −60 dBFS across the switch). **Automating
   either flag writes no parameters**: assert the host-visible parameter-change
   count across an automation pass of each is exactly **zero**, which is the
   assertion that catches the failure `15` names. Then the assertion that the
   split exists for: with **`link` on and `fx_link` off**, the lane's FX values
   must reach the audio — a lane crushed against a clean linked main delay is
   the case Frosty chose this shape for, and a test that only exercised both
   flags together would pass while it was broken. `fx_link` is off-lane, so this
   is asserted in the standalone plugin's parameter set and in state round-trip,
   not through a rack automation lane.
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

**h. Tempo sync.** Synthetic transport, BPM {20…999} × all 16 notes, timed by
(a): `60000/BPM × multiplier` within ±0.1 ms, halving past the maximum per 10
§7, never wrapping. Ramp 120→140 and jump 120→60: no click above −60 dBFS.
Transport stopped: BPM frozen, tail still decaying.

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
(tail ≤ 1.1× steady); no NaN at any extreme; an injected NaN contained within
one tail. `bench`, Release, 100 × 10 s at 48 kHz/512 against `measure_ltvcomp`
on the same box: ≤ 1.5× at defaults, ≤ 3.0× heaviest.

**The ≤ 3.0× heaviest figure predates the second engine and is NOT re-set here —
it needs re-measuring against two engines before it is trusted or changed.** At
defaults the module is unaffected: `hold` ships off, the lane is cleared and
dead and costs a branch, so "≤ 1.5× at defaults" stands as written. The heaviest
case is now two Clean sinc loops each with an FX stage at 192 kHz, which by
arithmetic is around 2.6–3.0× a single FX-off loop before anything else is
counted, so 3.0× against `measure_ltvcomp` is unlikely to survive. `bench`
settles it; **a budget known to fail is worse than no budget**, so the number
moves on a measurement and not on this paragraph.

**Both rings** are allocated once in `prepare()` from the fixed maximum — 10 §10,
now **8.0 MB per instance at 192 kHz** for the two engines; the 4.0 MB this item
quoted before covered the main ring alone. No allocation in
`process()`, including when `hold` or either `fx` turns on.
`latencyForParams` is **exactly 0** everywhere, both engines.

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
block boundaries: nothing above −60 dBFS on either edge. `bench` per candidate
against the FX-off loop: ≤ 1.3× any one, ≤ 1.5× heaviest, **per engine**, and FX
off within noise of the pre-FX build. No candidate allocates a second buffer --
Reverse was cut on 2026-09-21 -- so the FX stage allocates nothing at all, and
`process()` must allocate nothing when either `fx` turns on. **There is no
FREEZE to bypass the stage any more** (10 §11.5): the lane's detent deliberately
keeps the stage running, so a held chord with `lane_fx` on is *expected* to
change lap by lap, and the old "FREEZE with FX selected equals FREEZE with FX
off" assertion is struck rather than reworded.
**The independence assertions are run with `fx_link` off**, which is what makes
them meaningful; with it on the lane's trio is a copy of the main's by
construction. State: `fx`/`fx_type`/`fx_amount`, the lane's three and `fx_link`
round-trip in the golden schema and in presets — **`fx_link` in particular, since
being off-lane means state and presets are the only way it travels in a rack** —
while the **expanded view round-trips in the session only**, separately, and
differs by default between rack (compact) and standalone (expanded). Test
direction: automating, preset-loading or recalling `fx` never resizes the
module; a user click that turns `fx` on from compact opens the view once; the
arrow toggles the view alone, touching no parameter and no audio. The unlink
seed is tested the same way, in (e) 7.

**m. Two engines, every invariant.** `15` requires every invariant above to hold
for **both** engines, so (d), (f), (k) and (l) are each re-run a second time with
**`hold` on, the lane fed, `link` off and the lane's parameters at their
extremes** — the whole of (k)'s battery included: 44.1–192 kHz, block sizes
1/32/64/512/1023 and a random schedule identical to −120 dB, silence decaying to
exact zeros with no CPU rise, no NaN at any extreme, `latencyForParams` exactly
0. **An injected NaN must be contained within the engine it was injected into**:
a NaN in the lane must never reach the main's ring, which is (e) 1's claim in a
different currency. Expect roughly double the work in the handoff's estimate
(`15`, "What stage 2 now is").

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
is not automatable piecewise. On 2026-09-22 Frosty settled three more: **`link`
mirrors six** (the voicing rows 23–28) with the FX trio on its own `fx_link` at
id 32; **Sweep is cut**, leaving three FX types; and **`g_max` is a CALIBRATE
value**, settled by ear in `14` §3's listening round rather than decided on
paper.

**Blocking unknown**: 01 has no MEASURED figure for a modern clean delay's
feedback ceiling or maximum time; 10 sets both by decision (1.05, 2000 ms) and
both freeze at ship.
