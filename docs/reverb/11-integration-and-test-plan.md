# BMO Reverb — Integration & Test Direction

Groundwork; no code. **DSP behaviour is `10-dsp-spec.md`'s** — architecture,
types, constants, tail formula, CPU. This pack owns repo conventions,
registration, schema layout, accent and how the suites are built. Published
thresholds cite `05-er-psychoacoustics-citations.md`. Layout/registration/harness:
`docs/1176-comp/00-repo-conventions.md`; render tooling: that pack's
`11-integration-and-test-plan.md` §4. Reverb deltas: `docs/reverb/00-repo-conventions.md`.

**Thesis (10 §0):** Reference B's tail character, Reference A's control structure
— two generators, two absolute faders, pre-delay on the tail only, a
feed-the-tail control, one Density knob bridging discrete taps and dense early
energy.

**Build rule (`docs/deesser/HANDOFF-ui-pass.md`): name your targets; never run an
untargeted build; never build the rack plugin target.** A Debug build installs
every plugin and has already overwritten this machine's 0.2.5. `rack_tests` and
the standalone are allowed. Every branch starts from Kevin's `origin/main`, never
stacked on unmerged work.

## 1. Drop-in

**Identity — DECIDED by the owner, 2026-09-21. Permanent.** Display name **BMO
Linger**; bundle id **`com.lt3audio.bmolinger`**; module id **`reverb`**
(`modules/reverb/`, `products/reverb/`); presets **`.bmoreverb`**; plugin code
**`Brvb`** (reserved at `products/AGENTS.md:149`, spent here); BMO line,
`ui::bmoLine()`. Bundle id from the display name, preset extension from the
module id, per the existing rows.

**The id and the display name differ deliberately**, as `deesser` is to "BMO
Defang" and `fetcomp` to "BMO FET" (and `eq` to "BMO CEQ"). `ModuleDef::id` lives
in state files and rack presets and never changes, so it stays plain and
descriptive; the name on the panel is free to be evocative. Do not "tidy" the id
to match the name later — that would break every saved session.

*For the record:* BMO Linger, Foyer, Afterglow, Haunt and the working title
"B Verb" were considered. A name-collision scan (not a trademark opinion)
returned Linger and Foyer clear, Afterglow adjacent and crowded, and Haunt a
direct clash with a currently-sold hardware reverb/delay pedal. "B Verb" was set
aside because it echoes Reference A's own product name one letter apart, which is
what the no-third-party-names rule exists to prevent. No brands are named here,
per that rule.

`modules/reverb/`: `params.h`; `dsp/DspCore.h` (JUCE-free), `dsp/ErGenerator.h`,
`dsp/TapTables.h`, `dsp/Fdn.h`, `dsp/Absorbent.h`, `dsp/ReverbDsp.h`
(`ModuleDsp` adapter, unpacks the flat `float*` in `Index` order);
`panel/ReverbPanel.{h,cpp}`; `presets/FactoryPresets.h`; `Module.{h,cpp}`;
`AGENTS.md` + `README.md`, linked from `modules/AGENTS.md`.

**Registration (from `86095a5`):** `modules/CMakeLists.txt`;
`modules/AGENTS.md`; `products/rack/Registry.cpp`; `products/rack/CMakeLists.txt`;
`products/CMakeLists.txt`; `products/reverb/{Product.h,main.cpp,CMakeLists.txt}`;
identity **and accent** rows in `products/AGENTS.md` *before the first build*;
`tests/CMakeLists.txt`; `tests/plugin/RackTests.cpp` (count N→N+1);
`tests/ui/LayoutTests.cpp`; `tools/CMakeLists.txt`; `tools/snapshot/main.cpp`
product list **and link line**, or the panel cannot be rendered; `scripts/build.sh`.

**Permanence.** Parameter ids, their order in `specs()`, ranges, steps, defaults
and every choice list *with its index order* freeze at first ship; append only.
That binds 10 §1's type order — **Room · Chamber · Hall · Large Hall · Plate ·
Ambience**, with Church, Shaped Hall, Pattern Room, Positional Room and Vintage
Room appended later — and ER Mode's Taps/Energy/Blend. Module id, plugin code,
bundle id, preset extension, state tags and accent freeze too.

## 2. Shared-code changes, each its own commit

**(a) Tail-length reporting — take it.** Both processors hardcode `0.0`
(`SingleModuleProcessor.h:41`, `RackProcessor.h:124`); no tail accessor exists.
Add `virtual double tailSecondsForParams(const float*, int) const` to
`bmo::ModuleDsp` **defaulting to 0.0**, mirroring `latencyForParams` — from
parameter values, not DSP state. Formula per 10 §5:
`preDelay + T_mid·max(1, r_lo, r_hi) + t_ER,max + 0.05 s`, clamped to 30 s. The
rack **sums** across occupied slots, never maxes: slots are in series, so 4 s
feeding 2 s rings longer than either, and under-reporting truncates tails while
over-reporting only costs idle pulling. Cache in an `atomic<double>` refreshed on
parameter change — `getTailLengthSeconds()` is polled and must be lock-free.
*Blast radius:* `ModuleDsp.h` (every module's vtable),
`SingleModuleProcessor.{h,cpp}`, `RackProcessor.{h,cpp}`. No module file changes.
*Proof unchanged:* a test walking the rack registry asserting every shipped
module reports exactly 0.0 at defaults and schema corners; existing schema tests
green; every panel re-rendered and pixel hashes proven unchanged; `rack_tests`
and the `independence` job green.

**(b) Mono-in→stereo-out — wanted, deferred past v1.** Both processors restrict
buses to mono *or* stereo with no conversion (`SingleModuleProcessor.cpp:70-79`);
Dimension early-returns on mono. 10 §8 is explicit that the engine already
generates stereo from one input, so **bus layout is the whole obstacle**. Doing it
needs `isBusesLayoutSupported` to accept {mono in, stereo out}, channel arrays
sized by `max(in,out)`, and either `prepare(rate,block,numIn,numOut)` or — safer
— a `ModuleDsp` capability flag defaulting false, so only an opting-in module sees
an asymmetric layout. The rack is the blocker: a slot sits mid-chain, so a
widening module widens every slot after it. Keep the rack symmetric; if ever done,
standalone only. *Proof:* a golden per-product accepted-layout table that fails if
any product gains or loses one, plus a hashed fixed stereo render per module
before/after. *v1 fallback:* accept mono and stereo as today; on a mono bus run
the stereo tap sets internally and sum at the output — no forcing needed, since
10 §3's **γ ≥ 0 at every decorrelation setting** bounds the loss at 3 dB by
construction. Show a mono indicator on the panel.

**(c) Host tempo — omit from v1** (10 §1 agrees). No playhead plumbing exists in
`core/` or `modules/`. When it lands: read the playhead once per block into a POD
`bmo::TransportInfo{bpm,ppq,playing,valid}` in `core/dsp/Transport.h`, delivered
by `ModuleDsp::setTransport()` with an empty default. **BMO Dwell
(`frosty-delay-groundwork`) needs the identical plumbing**, and the repo has the
precedent: the `textParam`/`textFn` hunk was reproduced **byte-identically**
between `c142f37` and `86095a5` so either PR could land first, verified with
`git hash-object`. Do exactly that; neither branch stacks on the other. Omit it
here because Reference A's pre-delay is milliseconds only, the ITDG argument
(05 §2) is in milliseconds rather than beats, and appending `syncon`/`syncdiv`
later is legal whereas freezing a division list now freezes it forever.

## 3. Accent

**Arithmetic verified** against the Accents table on `main`. Taken: 336.0 CEQ,
31.7 Sat, 128.9 Util, 172.0 DEQ, 80.1 Tune (spent), 236.1 LTV, 271.6 Dimension,
plus non-accent azure 198.8 that every accent must clear. The bar is the worst
figure ever accepted — DEQ's 26.8°. A gap of width *g* admits *g−53.6°*:
336.0→31.7 (55.7) gives **2.8–4.9°**; 271.6→336.0 (64.4) gives **298.4–309.2°**.
Every other gap (48.4, 48.8, 43.1, 26.8, 37.3, 35.5) is under 53.6 and admits
nothing. Defang's `#ea9f9a` (3.8°) consumes the red sliver; FET's `#5489d4`
(215.2°) sits in the already-failed 198.8→236.1 gap as a stated exception, and its
±26.8° reach (188.4–242.0°) never touches the violet. **Violet is the only
survivor**, as 10 §8 also records. Contrast vs `#2e2e32` / `#efefef`; shipped
bands (lime excluded) 5.87–7.19 dark and 1.64–2.00 pale reduce to one constraint,
relative luminance 0.406–0.507. WCAG-derived on AURORA:

| | hex | hue | Δ 271.6 | Δ 336.0 | dark | pale |
|---|---|---|---|---|---|---|
| V1 | `#dd93dd` | 300.0° | 28.4° | 36.0° | 5.98 | 1.97 |
| V2 | `#eb8ae9` | 301.2° | 29.6° | 34.8° | 6.02 | 1.95 |
| V3 | `#e8a2e5` | 302.6° | 31.0° | 33.4° | 6.91 | 1.70 |
| **V4** | `#e694e0` | 304.4° | 32.8° | 31.6° | 6.23 | 1.89 |

All pass both bands and clear 26.8° from every taken hue. **V4 is the window
centre with the best-balanced margins, and is already drawn and measured** — it
is candidate F in the FET mock, refused there only for not being blue. V3 is the
lightest if the dark plate needs lift.

**Plainly: the window is not comfortable.** 10.8° admits one hue family
(mauve/orchid); margins beat the suite's worst-ever separation by only 1.6–8.9°;
and it sits between two shipped colours, so the reverb reads as their cousin.
**BMO Dwell has not locked a hue** and, with the red gap spent, points here too —
two unbuilt modules want the last window. Sign the reverb in now, or approve a
FET-style exception; relaxing 26.8° is not a third option, since 24° reopens a
0.4° sliver and 22° two ~4° greens. Reconfirm at merge; verify with
`Inspect.exe ratio` on a real render.

## 4. Parameter layout

Aligned to 10 §6's control set; the **order** below is this pack's proposal and
freezes at first ship. **All automatable.** Continuous and per-sample-smoothed
(20 ms one-pole, 10 §5) unless flagged **S** stepped, **X** 30 ms raised-cosine
crossfade, **L** log skew.

| # | id | caption | range | default | value string | flags |
|---|---|---|---|---|---|---|
| 0 | `type` | TYPE | 6 types, 10 §1 | Room | name | S, X |
| 1 | `size` | SIZE | 0.5…80 m | *per 10* | `12.0 m` | L, X |
| 2 | `predelay` | PRE-DELAY | 0…250 ms | 0 | `40.0 ms` | X |
| 3 | `prelink` | LINK ER | off/on | off | on/off | S |
| 4 | `decay` | DECAY | 0.1…20 s | 1.8 | `1.80 s` | L |
| 5 | `decayshape` | DECAY SHAPE | 0.04…3.5 | 3.5 | `Gated`…`Linear` | L |
| 6 | `attack` | ATTACK | 0…100 % | 30 | 0–120 ms bloom | |
| 7 | `feed` | TAIL FEED | 0…100 % | 70 | `Direct`…`Early` | |
| 8 | `damplofreq` | LOW × FREQ | 16…1600 Hz | 200 | Hz | L |
| 9 | `damplo` | LOW × | 0.10…2.00 | 1.20 | `1.20×` | L |
| 10 | `damphifreq` | HIGH × FREQ | 1000…2100 Hz | 1600 | Hz | L |
| 11 | `damphi` | HIGH × | 0.10…2.00 | 0.40 | `0.40×` | L |
| 12 | `eqlofreq` | EQ LOW FREQ | 16…1600 Hz | 200 | Hz | L |
| 13 | `eqlo` | EQ LOW | −24…+12 dB | 0 | dB, `Cut` at −24 | |
| 14 | `eqhifreq` | EQ HIGH FREQ | 1000…2100 Hz | 1600 | Hz | L |
| 15 | `eqhi` | EQ HIGH | −24…+12 dB | 0 | as `eqlo` | |
| 16 | `ermode` | ER MODE | Taps/Energy/Blend | Taps | name | S, X |
| 17 | `erdensity` | DENSITY | 0…100 % | *per 10* | `Discrete`…`Dense` | |
| 18 | `ershape` | ER SHAPE | 0…3 | *per 10* | contour *p* | |
| 19 | `erspread` | ER SPREAD | 5…200 ms | *per 10* | ms | L |
| 20 | `erhicut` | ER HI-CUT | 1…20 kHz | 7 k | Hz/kHz | L |
| 21 | `ervariation` | VARIATION | 0…6 | 2 | `Var 2` | S, X |
| 22 | `moddepth` | MOD DEPTH | 0.1…0.8 ms | *per 10* | ms | |
| 23 | `modrate` | MOD RATE | 0.1…1.2 Hz | *per 10* | Hz | L |
| 24 | `width` | WIDTH | 0…200 % | 100 | % | |
| 25 | `inhicut` | IN HI-CUT | 2…20 kHz | *per 10* | Hz/kHz | L, **owner confirm** |
| 26 | `erlevel` | ER | −40…0 dB | −6 | dB, `Off` at −40 | |
| 27 | `verblevel` | REVERB | −40…0 dB | −6 | as `erlevel` | |
| 28 | `mix` | MIX | 0…100 % | 100 | % | |
| 29 | `output` | OUTPUT | −24…0 dB | 0 | dB | |

**Count: 30 — the rack's biggest panel, and both packs now say 30.** It fits a
slot's **32 host lanes**, so none of DEQ's `SlotOverflow` machinery is needed, but
only **two lanes remain**, and a later Freeze, ducking or `syncon`/`syncdiv` would
exhaust them. Say so in `AGENTS.md`. There is **no `voicing` parameter**: 10 §1
keeps the era block as per-type constants, promotable in v2 without touching type
ordinals or state layout.

**The parameter that explained 29 vs 30 is `inhicut` (IN HI-CUT), and it stays —
marked "owner confirm".** 10 §6 stated 29 while enumerating 30. On 10's own
evidence the odd one out is the input high-cut: it appears only inside §2's
*Input* sentence, beside an explicitly **fixed** 20 Hz high-pass; §6's CPU line
bundles it into "input conditioning and EQ"; and §7's fixed-values table gives
every other user control a range row but gives it none. So 10's body reads as
29 parameters plus an internal constant, while its own list reads 30. Kept as a
parameter, because §2 gives it a user range (2–20 kHz) that a constant would not
need, and because it is the only way to darken what feeds **both** generators
independently of the Reverb EQ shelves — a real control, not a miscount. If the
owner would rather it were a constant, deleting index 25 before the first ship is
free and returns a third spare lane; after that it is permanent.

**Main face vs expanded** — DEQ's precedent (`ModuleDef::expandedWidth`, 320/600,
switched on the host bar). *Main face:* the ER/tail display, TYPE, SIZE,
PRE-DELAY, DECAY, **ER**, **REVERB**, MIX — the two faders are the thesis, and
the tail-off depth-placement technique must be reachable without expanding.
*Expanded, three groups:* **EARLY** (ER MODE, DENSITY, ER SHAPE, ER SPREAD, ER
HI-CUT, VARIATION, LINK ER, TAIL FEED); **TAIL** (ATTACK, DECAY SHAPE, four
damping); **TONE & OUT** (four EQ, IN HI-CUT, WIDTH, MOD DEPTH, MOD RATE,
OUTPUT).

## 5. Panel — ER/tail display

**Worth it here.** It is the only way to see ITDG (05 §2), tap spread, the
ER→tail handover and the decay slope at once, for a module whose failure mode is
invisible in a spectrum — and it earns its keep as a fault-finder: the Defang
pass found a resonant shelf dip *in a render* that every test had passed over.

Build it like **Defang's band sketch, not DEQ's analyser**: static,
message-thread, redrawn from parameters only, no `AnalyserTap`, no audio path, so
it cannot affect sound or latency. Draw the impulse at t=0, ER taps as lines at
their times with heights from gains and L/R offset from pans, the pre-delay gap,
and the tail envelope to −60 dB tilted by the damping multipliers; 0–500 ms.

**Cost and the real risk.** The panel needs the tap tables without the DSP, so
`dsp/TapTables.h` and the Size law must be JUCE-free and panel-includable (Defang
put its constants in `DspCore::Params` so both sides inherit the decisions, not
the numbers). Risk is sketch/DSP drift — one source of truth plus a layout test
asserting the sketch's first tap time equals the table's. **Fallbacks:** (1)
envelope only — pre-delay gap plus damped exponential, still shows ITDG; (2) no
display, numeric ITDG and T60 readouts.

## 6. Building the test suites

**IRs are captured in-test and never written to disk.** Unit impulse (plus a
fixed-seed noise burst and a sweep, since modulation makes the IR time-varying)
through `DspCore` into a `std::vector<float>`; assert on numbers. **No audio ever
committed** — twice a tool has written WAVs into the tree; read
`git status --short` before every `git add`. `measure_*` may write to the
gitignored `packages/reverb-listening/`. Every ER item below runs with `verblevel`
at −40.

- **T60.** Schroeder backward integration `EDC(t)=∫ₜ^∞h²`; fit −5…−35 dB (T30×2)
  and −5…−25 dB (T20×3) per ISO 3382-1, the two within 10% of each other — that
  is what proves the decay is exponential. Versus `decay`: **±10% or ±50 ms,
  whichever is larger**, over 0.3–6 s; ±20% outside.
- **Damping.** Octave-band the IR (125 Hz–8 kHz), Schroeder each.
  `T60(band)/T60(mid)` tracks `damplo`/`damphi` within **±15%** at 0.25/0.5/1.0/2.0
  two octaves outside each knee; mid stays within **±5%** of `decay` whatever the
  multipliers — the reason 10 §1 chose absorbent filters.
- **ER taps.** At DENSITY minimum, hi-cut open, peak-pick: times **±1 sample**,
  gains **±0.2 dB** against the golden image-source table, pan from the L/R ratio
  **±0.02**. Gains follow `(1 m/dₖ)·β^nₖ` (10 §3; roll-off 05 §10.3); Moorer's
  19-tap table over 4.3–79.7 ms is the sanity reference.
- **Comb rules, as assertions** (10 §3; thresholds 05 §6.2–6.4). **No full-band
  tap between 1 and 8 ms** — inside it only with its one-pole below 1.5 kHz, or
  below 1 ms where it fuses (05 §1.1). **Separation ≥0.9 ms**; **no two inter-tap
  gaps within 2%**; jitter ±3%, deterministic per type. **No single tap above
  −15.3 dB** (from `20log₁₀((1+a)/(1−a)) ≤ 3 dB`). Against dry, mono-summed ER
  power **ΣP ≤ −15.8 dB** for ≤1 dB RMS ripple — a bound that is a *function of
  the wet/dry setting*, so sweep MIX and have the panel say so.
- **Mono compatibility.** Loss `10log₁₀((1+γ)/2)` dB, γ the ER bus L/R
  correlation: **γ ≥ 0 at all seven VARIATION positions** (anti-correlated tap
  pairs forbidden outright), falling ≈0.95 → ≈0.05 monotonically, loss ≤3 dB
  throughout (05 §9.3).
- **Flamming, three rules** (10 §3), on a dry snare: (i) **no tap after 25 ms
  above −12 dB relative to cumulative ER energy at 25 ms**; (ii) cumulative energy
  in successive 5 ms windows non-increasing after the peak, so there is no second
  onset; (iii) **≥50% of ER energy before 30 ms**. Grounds: echo threshold and
  Barron's single-reflection map (05 §1.1–1.3), Griesinger's 10–50 ms proximity
  cost (05 §4).
- **Lateral energy.** Early lateral fraction **0.10–0.25** at default VARIATION
  (ISO 3382-1; JND 05 §3).
- **ER-only.** Energy after (ER span + 5 ms) **≥60 dB below** ER energy; last tap
  ramps out over **≥5 ms** (10 §3); ER-only magnitude, 1/3-octave smoothed, flat
  **±3 dB** over 200 Hz–10 kHz.
- **Density sweep.** DENSITY 0→100%: ER energy constant within **±0.2 dB** (10 §3
  renormalises), no tap appearing at non-zero level, no click — the weighting is
  continuous, so a click is a bug in the ramp width Δ. At the top, pulse rate
  clears **≥2000/s** (05 §8).
- **ER hi-cut.** At 2/4/8/16 kHz the −3 dB point is within ±10% and no tap moves
  (cross-correlate two IRs, peak lag 0 ±1 sample).
- **Echo density and mixing time.** Normalised echo density (Abel–Huang):
  sliding-window fraction of samples above the window's σ, normalised by
  `erfc(1/√2)≈0.3173`. Crosses **0.9 by the type's mixing time**, at or under
  Polack's √V ms (05 §8); Hall at τ̄ = 55 ms reaches ~1000 echoes/s at ≈105 ms
  (10 §4). Run at `feed` 0 and 0.7 and require the default to be faster.
- **Level laws and the phasing trap.** ER and REVERB are absolute trims: at −6,
  −12, −24 the IR energy moves by exactly that ±0.1 dB; at −40 it is ≥100 dB down
  (off, not −40). Pin one MIX law, test to ±0.1 dB. Since **dry is never delayed
  nor summed against a delayed copy of itself** (10 §2): (i) MIX 50%, pre-delay
  40 ms, wet faders −40 — output nulls against dry to **≤−80 dB**; (ii) MIX 50%,
  pre-delay swept 0→250 ms — magnitude below 500 Hz moves **≤0.5 dB**.

The rest, one line each:

| Test | Assertion |
|---|---|
| Modal density | Each *mᵢ* prime, **re-derived per rate, not multiplied**; `Σmᵢ ≥ 0.15·fs`, every type and rate. **10 §4 records Plate failing at 8 lines** — write it before Plate is tuned and expect red until 12 lines or 2× τ̄ |
| Onset | First 50 ms in 1 ms windows, no jump above **3 dB** after the ER span; ATTACK 0→100% blooms monotonically over **0–120 ms** (10 §2) |
| Ringing | Late tail (2× mixing time to −30 dB): spectral flatness **≥0.3**, no 1/3-octave band **>6 dB** over the smoothed mean, envelope autocorrelation **no peak >0.2 at lags 2–200 ms**, every type |
| Modulation | 1 kHz sine, wet, tail only; instantaneous frequency from the phase derivative in 50 ms windows. Peak deviation **≤3 cents** at the top of depth and rate (10 §4). Report its spectrum — a visible rate means chorused, not randomised |
| Pre-delay | First tail sample above −60 dB within **±1 sample**, every rate; ER taps untouched unless LINK ER is on (peak lag 0 over the first 100 ms); **range cannot go negative** and `latencyForParams` returns 0 throughout (10 §2) |
| Parameter changes | TYPE: 30 ms dip, tables swapped at the minimum — no click, **no allocation**, no second engine. SIZE/PRE-DELAY: 30 ms crossfade, retriggered at 1% accumulated \|ΔS\|, windows summing to one, **ER and late sharing the scheme** (10 §3), no pitch shift on a held sine. Coefficients: no 1 ms energy jump above 3 dB |
| Stability | Matrix orthogonal to 1e−6, `max\|Hᵢ(ω)\| ≤ 1 − 1e−4`. At `damphi` 2.0 / `decay` 20 s (effective T60 40 s): ten minutes then silence, never above +6 dBFS, RMS never growing over any 10 s window |
| Denormals | 60 s of silence after a loud burst with FTZ/DAZ **disabled** — block time must not rise (the ~100× trap), tail reaching exactly 0.0f; this is what 10 §4's ±1e−20 injection is for |
| NaN / silence | ±1.0 square, DC step, denormal input, fuzzed over schema corners at every type — every sample finite; after `reset()`, zeros in gives exactly zeros out |
| Tail report | `tailSecondsForParams` **≥ measured −60 dB time** and **≤30 s**, every type, 44.1/48/96/192 kHz — what makes §2(a) mean anything |
| Bypass | No per-slot enable flag exists (`00` §2, 10 §5), so a removed reverb truncates: assert the wet bus fades over **150 ms** in `reset()` on the envelope slope, and no click into the remaining chain |
| Sample rate | 44.1–192 kHz. *Must not differ:* per-band T60 ±5%, tap times *in ms* ±0.1 ms, pre-delay ±0.1 ms, density crossing ±10%, latency **exactly 0**. *May differ:* sample values (lines re-primed per rate), modal detail above ~15 kHz, memory (linear in rate) |
| Block size | 1/16/32/64/**127**/512/2048 **bit-identical** for fixed parameters; if not, something smooths per block instead of per sample — a bug, not a tolerance |
| Buses | `numChannels` 1 and 2: mono finite and ≤3 dB down by the γ ≥ 0 rule; no mono→stereo layout in v1 (§2b) |
| CPU / memory | 10 §6's budget: `measure_reverb bench`, 60 s noise, 48 kHz/128, **Release**, median of five, on AURORA — **≤1.5% of a core at 48 kHz/128, ≤5% at 192 kHz**, eight slots under 12% and 40%. Memory ≈300 kB / ≈1.2 MB, allocated in `prepare()`, **zero allocation in `process()`**. Measure 10 §8's worst case first (DENSITY 48 taps, 3 diffuser stages, 192 kHz) |
| Golden STATE | `checkSchema` pins ids, order, ranges, steps, defaults, formats and both choice lists with their index order; pin the **derived** per-type tap tables too, since 10 §8's "failing table is re-seeded, not patched" only works if it is pinned. For a fingerprint, a **hash of a fixed-seed IR** plus scalars — never audio |

**Where they live.** *JUCE-free DSP* (`tests/dsp/ReverbDspTests.cpp`,
`bmo_add_dsp_tool`, the seconds-long Linux `dsp` job): everything above that is
arithmetic on `DspCore`; CI IRs 2–4 s and a short rate list, with the ten-minute
stability run and the full rate matrix behind a flag CI does not set. *Plugin/schema*
(`tests/plugin/ReverbTests.cpp`): golden schema, choice-order pins, preset and
state XML round-trip, `getLatencySamples()==0` at every rate,
`getTailLengthSeconds()` ≥ measured, the accepted-layout table; `RackTests.cpp`
takes the count. *UI* (`LayoutTests.cpp`): `checkReverbPanel`, the
sketch-vs-table first-tap assertion, `--dump`. *Manual*
`tools/measure/reverb/main.cpp`: `ir t60 er density mono sweep bench` — every figure
quoted in `testing-notes/`, plus the CALIBRATE runs.

Every result names the machine — **AURORA** — plus configuration and host.
**Check the build exited 0 before believing any ctest count**; a failed compile
leaves the old exe.

**Listening pass — NOT YET HEARD.** 10's CALIBRATE items (β, the tap cutoff law,
modulation distribution, the Attack contour, Energy-mode end-stops) can only be
settled here. *Lead vocal* (dense mix): ER only, then tail — does it cloud? *Rap
vocal*, the hardest case: ER-only at 15–25 ms, tail −40, must read as depth not
reverb. *Snare*, dry and close: ER solo, listen for flam; then tail at 1.2 s.
*Drum room*: sweep SIZE — no combing when summed, no chorus during the crossfade.
*Acoustic guitar*: hold a chord, sweep DENSITY and ER HI-CUT, and check the
3-cent bound on a held note, which 10 §4 flags as the one that may still read as
wobble. *ER-only depth placement*: Ambience, tail off, ER level the only distance
control on a vocal and a snare — the source moves back without wash. *Mono check*:
all of the above summed, at VARIATION 0 and 6.

## 7. Milestones, panel-first

| | Contents | Exit test |
|---|---|---|
| **M0** skeleton | Directory, identity and accent rows, `params.h` with the **full 30-parameter schema** and frozen type order, pass-through adapter reporting zero latency and zero tail, every registration point | Schema green; rack N→N+1; standalone loads; `ui_layout_tests` pass; both appearances hashed — after a build that exited 0 |
| **M1** panel | Real panel, main face + expanded, ER/tail sketch or fallback, value strings, presets | Renders reviewed **by the owner**; ratios, gaps, hashes in `testing-notes/ui-pass-reverb-<date>.md`, naming AURORA |
| **M2** ER generator | Image-source tables, Size law and crossfade, order-banded filters, diffuser, VARIATION, hi-cut, the Density bridge — tail silent | The whole ER block of §6 plus the ER-only listening items. **This milestone decides the module** |
| **M3** late network | FDN, absorbent filters, damping, EQ, pre-delay, TAIL FEED, ATTACK, DECAY SHAPE, modulation | Modal density (incl. the Plate failure), T60, damping, echo density, ringing, modulation, pre-delay, level laws, phasing nulls, clicks |
| **M4** types | Six v1 types and their constant blocks incl. reserved era fields | Every §6 test at every type; order frozen; Plate's line count resolved |
| **M5** shared code | §2(a) as its own reviewed commit; (b)/(c) only if taken, byte-identical with BMO Dwell | Tail report ≥ measured and ≤30 s; existing modules proven unchanged by hash and schema test |
| **M6** acceptance | Invariance, stability, CPU/memory, CALIBRATE, listening | Those §6 blocks, recorded naming AURORA |

**Done** = every milestone's exit test green in all three CI jobs on both
platforms, each after a build that exited 0; no audio, renders or fonts
committed; `AGENTS.md` + `README.md` present and linked; identity row, accent and
name permanent with the collision scan recorded; `measure_reverb` registered;
figures written up naming AURORA; the listening checklist heard; branch from
Kevin's `origin/main`, stacked on nothing unmerged.

**Blocking unknown:** none. **Decided:** the name, the module id and the
30-parameter count (§1, §4). **Owner confirm, before the first build:** the
accent; whether `inhicut` ships as a parameter or becomes a constant (§4);
`feed`'s caption — 10's **Diffusion** (Reference A's word) or **TAIL FEED** as
proposed here, since "diffusion" means density everywhere else in the suite;
whether ER SHAPE and ER SPREAD grey out in Taps mode or sit inert; the MIX law
and its default.
