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

`specs()` order = `enum Index` order; all automatable.

| # | id | Range / units / law | Default | Smoothing |
|---|---|---|---|---|
| 0 | `time` | 1…2000 ms, log | 375 | 10 §2's law |
| 1 | `sync` | bool | off | crossfade |
| 2 | `note` | choice, 16 | 1/8D | as `time` |
| 3 | `feedback` | 0…100 %, lin (`g = 1.05·fb^1.6`; unity ≈97 %, 97–100 % self-oscillates) | 35 | 30 ms |
| 4 | `character` | Clean/Tape/Bucket-brigade | Clean | xfade |
| 5 | `stereo` | Stereo/Ping-pong/Dual offset | Stereo | xfade |
| 6 | `lowCut` | 20…1000 Hz, log | 20 | 20 ms |
| 7 | `highCut` | 1k…20k Hz, log (cap 18 k) | 20000 | 20 ms |
| 8 | `voice` | 0…100 %, lin (10 §11: `Q = 0.5 + VOICE·5.5`) | 0 | 20 ms |
| 9 | `modRate` | 0.1…8 Hz, log | 0.6 | 20 ms |
| 10 | `modDepth` | 0…100 %, lin | 0 | 20 ms |
| 11 | `drive` | 0…100 %, lin | 0 | 20 ms |
| 12 | `duck` | 0…24 dB, lin | 4 | 20 ms |
| 13 | `mix` | 0…100 %, 10 §9's sin/cos hinge at 50 % | 35 | 30 ms |
| 14 | `throw` | bool, momentary | off | 5–10 ms ramp |
| 15 | `throwMode` | Send open/Throw/Build | Send open | xfade |
| 16 | `freeze` | bool, momentary | off | latched, no ramp |
| 17 | `fx` | bool | off | none — the stage is skipped, not faded (10 §11a) |
| 18 | `fxType` | choice, 7 | Diffuse | xfade |
| 19 | `fxAmount` | 0…100 %, lin | 35 | 20 ms |

**FX (10 §11a; reserved now, candidates until ship).** `fxType` runs least to most
intervention, the rule the other three lists already follow: **Diffuse, Sweep,
Pan/Tremolo, Octave up, Octave down, Reverse, Crush**. **Index 0 is Diffuse, not
Off** — `fx` owns off, so a corrupt state landing on index 0 gives the gentlest
type with the stage still gated by a bool that defaults off. (The alternative,
folding Off into the list as index 0, costs a permanent redundant state and makes
"is FX on" two questions; rejected.) **These are candidates**: the list and its
order may change freely until ship and are **append-only forever afterwards**, so
any candidate that fails 14 §3's listening must be **removed before ship**, never
left in as a dead index.

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
defaults expanded.

**No control is named DWELL** — that is the module. **Permanent at ship**: ids,
their order, ranges, steps, defaults, and the four choice lists **with their
index order**; new parameters append at the end. `sync`/`note`'s slots and
`note`'s order are permanent now; `sync` ships disabled until 12's tempo
plumbing lands. `freeze` (row 16) ships enabled in v1, its own slot, never
folded into `throwMode`. Module id `dwell` is final (DECIDED, Frosty 2026-09-20).

**MIX law (10 §9 owns it; the earlier linear law here is superseded).**
`wet = sin(π·MIX)` for MIX ≤ 50 %, else 1; `dry = 1` for MIX ≤ 50 %, else
`cos(π(MIX − 0.5))`. The **dry signal is bit-exact unity from 0 to 50 %** — skip the multiply rather
than scale by a computed 1.0 — wet reaches full at 50 % and holds, and only dry
fades across 50–100 %.

`note` ascends in duration — `1/32, 1/16T, 1/32D, 1/16, 1/8T, 1/16D, 1/8, 1/4T,
1/8D, 1/4, 1/2T, 1/4D, 1/2, 1/1T, 1/2D, 1/1` — because the index *is* the
automation lane: a sweep must move monotonically in time, a clockwise knob must
lengthen. Anything appended later sits at the end, out of order, forever, so the
grid ships complete. The other three lists run least to most intervention, index
0 being the neutral value a corrupt state lands on: `character` least to most
coloured; `stereo` least to most divergent; `throwMode` **Send open** (default: `throw`
inert, so a fresh instance is an ordinary delay) → **Throw** (send closed until
held) → **Build** (holding ramps loop gain toward 10's bounded target, decaying
on release). 10 owns their meaning; its names win, not its order.

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

**d. In-loop filter stability.** `voice` 100 and
`feedback` 100, both cuts at both extremes, 60 s at 44.1–192 kHz: bounded by the
safety clip, no divergence, NaN or DC growth. Since 10 §4's coefficients depend
only on `f_c/f_s`, the resonant peak must agree across every rate within 1 %.

**e. Throw, build, freeze.** `throw` toggled on and off block boundaries under
sustained input, every `throwMode`: no discontinuity above −60 dBFS on either
edge, send full open/closed within 10's ramp ±20 %; in **Send open** it changes
nothing, bit-exact. In **Build**, held 30 s, loop gain rises monotonically but
**never reaches `g = 1`**, then decays back. If FREEZE ships, held 60 s: level
drift ≤ 0.1 dB, DC ≤ −80 dBFS, no NaN.

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
on the same box: ≤ 1.5× at defaults, ≤ 3.0× heaviest. The ring is allocated once
in `prepare()` from the fixed maximum (10 §10: 4.0 MB/instance at 192 kHz); no
allocation in `process()`. `latencyForParams` is **exactly 0** everywhere.

**l. In-loop FX (10 §11a).** `fx` = off must be **bit-identical** to a build
without the stage — null the two renders sample for sample, every character, every
`fxType` index, `fxAmount` at both ends: the stage is skipped, so the type and
amount cannot leak. Per candidate, at `feedback` 100 and `fxAmount` 100, 60 s at
44.1–192 kHz: bounded, no divergence, NaN, denormal slowdown or DC growth, as (d).
`latencyForParams` is 0 for every candidate (the octaves' grain offset comes off
`D`). Crush is **exempt from (f)'s −60 dBFS floor**; its assertion is that the
non-harmonic floor is non-increasing from repeat 10 to 32. Toggling `fx` and
sweeping `fxType` on and off block boundaries: nothing above −60 dBFS on either
edge. `bench` per candidate against the FX-off loop: ≤ 1.3× any one, ≤ 1.5×
heaviest, and FX off within noise of the pre-FX build. FREEZE held 60 s with each
candidate selected: identical to FREEZE with `fx` off, because the stage is
bypassed. Reverse's second buffer is allocated in `prepare()` from the fixed
maximum whether `fx` is on or not — no allocation in `process()` when it turns on.
State: `fx`/`fxType`/`fxAmount` round-trip in the golden schema and in presets,
while the **expanded view round-trips in the session only**, separately, and
differs by default between rack (compact) and standalone (expanded). Test
direction: automating, preset-loading or recalling `fx` never resizes the
module; a user click that turns `fx` on from compact opens the view once; the
arrow toggles the view alone, touching no parameter and no audio.

## Open decisions for Frosty

1. **Trademark-search "Dwell"** — blocks the permanent identity row.
2. **Accent**: 10 §0 assumes the gold gap (W3); W1 and W2 separate better.
3. **`throwMode`'s entries and order**, and whether BUILD's target and ramp stay
   10's constants rather than parameters.

**Blocking unknown**: 01 has no MEASURED figure for a modern clean delay's
feedback ceiling or maximum time; 10 sets both by decision (1.05, 2000 ms) and
both freeze at ship.
