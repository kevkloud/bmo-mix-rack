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
That binds 10 §1's type order — **Room · Chamber · Hall · Cavern · Plate ·
Ambience** — and ER Mode's Taps/Energy/Blend.

**The type list was settled by the owner on 2026-09-21 and index 3 changed.**
*Large Hall was cut:* the late network scales with the taps under SIZE, so
Hall→Large Hall is τ̄ 55→80, a factor of 1.45 inside a SIZE range spanning
0.5–80 m. SIZE already covers it several times over, and its only non-size
residual is β, whose own ladder is indexed by size. Reference A offers two halls
but nowhere states that the difference is size — that was this pack's inference,
and it does not hold. *Cavern takes the slot*, carrying what was reserved as
Church: the long, dense, stone-reflective character. The name is deliberately
secular, and Church is therefore struck from the reserved list rather than
waiting in it. Reserved for later: Shaped Hall, Pattern Room, Positional Room,
Vintage Room — though note that four of those five read as universal controls
rather than as rooms, so the reserve may be emptier than it looks.

Renaming a choice position is free at any time; the **count** is what
normalisation depends on, and six is unchanged.

**Levels are now per-type, and a type re-applies on every change.** `erlevel`
and `verblevel` join the per-type constants, taking `roomDefaults` from eight to
ten. Without that, Ambience was unbuildable as specified: the pack describes it
as "tiny tail, ER-dominant by default" while the two faders were
type-independent, so no type could set its own tail level. Frosty confirmed
Ambience is a sound he reaches for often and will bring references to the
Ableton pass.

*The consequence, recorded rather than discovered later:* `type` is an
automatable parameter that now writes other automatable parameters. Automating
TYPE while also automating ER or REVERB puts the two in conflict — the type
change stamps a level the host is simultaneously driving. This is inherent to
"a type is a voicing", which is the behaviour that was chosen. Module id, plugin code,
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
over-reporting only costs idle pulling. **The rack clamps its summed total at
the same 30 s a module clamps itself at** — added 2026-09-21 with Frosty's
approval, for the case the slot limit does not stop: `addModule` counts slots
and never looks for duplicates, so eight BMO Lingers is a legal chain and eight
honest thirties is a four-minute tail. That is free at transport stop, where
over-reporting only idles the host, and not free for an offline bounce, where
the figure is rendered onto the end of every export. Both clamps read the one
constant in `core/dsp/ModuleDsp.h`; do not write 30.0 anywhere else. Cache in an
`atomic<double>` refreshed on
parameter change — `getTailLengthSeconds()` is polled and must be lock-free.
*Blast radius:* `ModuleDsp.h` (every module's vtable),
`SingleModuleProcessor.{h,cpp}`, `RackProcessor.{h,cpp}`. No module file changes.
*Proof unchanged:* a test walking the rack registry asserting every shipped
module reports exactly 0.0 at defaults and schema corners; existing schema tests
green; every panel re-rendered and pixel hashes proven unchanged; `rack_tests`
and the `independence` job green.

**(b) Mono-in→stereo-out — SHIPPED IN v1, 2026-09-21 on AURORA.** Frosty reversed
the deferral below. The analysis that follows is kept because it is where the
requirement came from, but three of its conclusions turned out to be wrong and
the implementation in `core/product/BusLayouts.h` does not follow them:

- *"The rack is the blocker… if ever done, standalone only."* It is not. The rack
  widens **once, at its own input, ahead of slot 1**, so no slot ever sees an
  asymmetric layout and no module widens the one after it. The rack ships this.
- *"A `ModuleDsp` capability flag defaulting false, so only an opting-in module
  sees an asymmetric layout."* Not needed, and it would have been the wrong
  shape. No module sees an asymmetric layout under duplication.
- *"Bus layout is the whole obstacle"* (10 §8). Channel handling had to change
  too. The old `processBlock` **cleared** the channels the input did not cover;
  a stereo buffer with a silent right channel is not a mono signal, it is a
  hard-left one. Dimension's guard only catches `numChannels < 2`, so it would
  have imaged that hard-left source — the exact error its own comment records at
  +1.17 dB. The input is now **duplicated** into the uncovered channels at unity,
  which is the signal every module already handles when a host feeds both inputs
  of a stereo instance. Stereo-in→mono-out stays rejected: folding is a mix
  decision and BMO Util is where a user makes it visibly.

Covered by `tests/plugin/BusTests.cpp` (ctest `bus`): per module and for a full
rack, absolute per-channel RMS and peak constants captured at `8fed835` *before*
either processor was touched, so the existing mono→mono and stereo→stereo paths
are proven bit-unchanged rather than argued to be.

*The original analysis, for the record:*

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

**Decided 2026-09-21 on AURORA: BMO Linger takes V4 `#e694e0`.** Frosty chose it
from a proof sheet that drew all four candidates through the real panel rules —
`faceOf` for knob caps, `accentInk` for captions, `onAccentOf` for switch ink —
on both plates, rather than from hex. An independent audit of the arithmetic
above reproduced it: swept at 0.1° over the whole circle, the admissible set is
the single arc 298.4–309.2°, and V4 sits at its centre.

The reverb therefore spends the window, and **BMO Dwell cannot also be violet**:
two accents need 2×26.8° and the arc is 10.8° wide, a 16.0° shortfall. That is
Dwell's call to make, but the pack it will make it from has a defect worth
naming here. `docs/delay/00-repo-conventions.md` omits BMO Tune RT from its
accent list, which is why its olive-gold `#b2bb54` reads there as unconditionally
clean. It is not: it clears every *rack* accent by 33.5° but needs an 11.9°
exception against Tune RT. The exception is cheap — Tune is not a rack module and
can never sit beside Dwell — but it should be taken knowingly, not by omission.

## 4. Parameter layout

Aligned to 10 §6's control set. This table is the **authoritative copy** of the
schema: `modules/reverb/params.h` was written from it, `10-dsp-spec.md` §6
restates the same names in the same order, and `checkSchema` in
`tests/plugin/ReverbTests.cpp` pins ids, order, ranges, steps, defaults, formats
and both choice lists with their index order. The steps themselves live in
`params.h` and are not repeated here. **All automatable.** Continuous and
per-sample-smoothed (20 ms one-pole, 10 §5) unless flagged **S** stepped,
**X** 30 ms raised-cosine crossfade, **L** log skew.

**Captions are ASCII.** The two display faces are licensed individually and live
outside this repository, so the damping pair reads `LOW x` / `HIGH x` and prints
`1.20x` rather than carrying a multiplication sign.

***per type*** in the default column marks one of the **nine** rows a type
change writes (§4b); the number beside it is Room's, which is what a fresh
instance opens on — and is therefore a claim about what Room *is*, not merely a
knob position.

| # | id | caption | range | default | value string | flags |
|---|---|---|---|---|---|---|
| 0 | `type` | TYPE | 6 types, 10 §1 | Room | name | S, X |
| 1 | `size` | SIZE | 0.5…80 m | *per type* (12.0) | `12.0 m` | L, X |
| 2 | `predelay` | PRE-DELAY | 0…250 ms | 0 | `40.0 ms` | X |
| 3 | `decay` | DECAY | 0.1…20 s | 1.8 | `1.80 s` | L |
| 4 | `feed` | SOURCE | 0…100 % | *per type* (70) | `70 % (Mostly Early)` | |
| 5 | `damplo` | LOW x | 0.10…2.00 | 1.20 | `1.20x` | L |
| 6 | `damphi` | HIGH x | 0.10…2.00 | 0.40 | `0.40x` | L |
| 7 | `eqfilter` | FILTER | Off / Lo Cut / Hi Cut / Bandpass | Off | name | S |
| 8 | `eqlofreq` | EQ LOW FREQ | 16…1600 Hz | 200 | Hz | L |
| 9 | `eqlo` | EQ LOW | −24…+12 dB | 0 | dB, `Cut` at −24 | |
| 10 | `eqloq` | EQ LOW Q | 0.1…2.0 | 0.71 | bare number | L |
| 11 | `eqmidfreq` | EQ MID FREQ | 20…20000 Hz | 1000 | Hz | L |
| 12 | `eqmid` | EQ MID | −24…+12 dB | 0 | dB *(no `Cut`)* | |
| 13 | `eqmidq` | EQ MID Q | 0.1…40 | 0.71 | bare number | L |
| 14 | `eqhifreq` | EQ HIGH FREQ | 1000…20000 Hz | 6000 | Hz | L |
| 15 | `eqhi` | EQ HIGH | −24…+12 dB | 0 | as `eqlo` | |
| 16 | `eqhiq` | EQ HIGH Q | 0.1…2.0 | 0.71 | bare number | L |
| 17 | `ermode` | ER MODE | Taps/Energy/Blend | Taps | name | S, X |
| 18 | `erdensity` | DENSITY | 0…100 % | *per type* (50) | `50 % (Diffuse)` | |
| 19 | `erspread` | ER SPREAD | 5…200 ms | *per type* (80) | ms | L |
| 20 | `erhicut` | ER HI-CUT | 1…20 kHz | 7 k | Hz/kHz | L |
| 21 | `ervariation` | VARIATION | 0…6 | 2 | `Var 2`; `Var 6 (mono null)` | S, X |
| 22 | `moddepth` | MOD DEPTH | 0.1…0.8 ms | *per type* (0.28) | `0.28 ms` | |
| 23 | `modrate` | MOD RATE | 0.1…1.2 Hz | *per type* (0.50) | Hz | L |
| 24 | `width` | WIDTH | 0…200 % | 100 | % | |
| 25 | `inhicut` | IN HI-CUT | 2…20 kHz | *per type* (20 k) | Hz/kHz | L, **owner confirm** |
| 26 | `erlevel` | ER | −40…0 dB | *per type* (−6) | dB, `Off` at −40 | |
| 27 | `verblevel` | REVERB | −40…0 dB | *per type* (−6) | as `erlevel` | |
| 28 | `mix` | MIX | 0…100 % | 100 | % | |
| 29 | `output` | OUTPUT | −24…0 dB | 0 | dB | |

**The EQ captions above are the host's, not the panel's.** A lane in a DAW says
EQ LOW FREQ; the panel shows **one** FREQ / GAIN / Q set repointed by a LOW /
MID / HIGH segment row, so its captions are three characters and not twelve
(§4e). `eqfilter`'s ring says OFF / L / H / B where the lane says the four words
in full — `kEqFilterLegend` against `kEqFilterNames`, and
`ui::ConcentricBand::setLegend` is the shared method that changes one without
touching the other.

**The two outer Qs stop at `kShelfMaxQ` = 2.0** rather than travelling to a
bell's 40 and doing nothing over 2: past about 2 a shelf's resonant bump is
where the matched design is weakest near Nyquist, and the outer nodes can never
be bells here. BMO DEQ carries the same number but clamps to it behind a wider
knob, because a DEQ band's shape is a choice. The middle node is always a bell
and keeps DEQ's own 0.1–40.

**Count: 30, and the count is the decision.** It fits a slot's **32 host lanes**
with **two spare**, so none of DEQ's `SlotOverflow` machinery is needed — but
Freeze, a ducking control and the tempo-sync pair `syncon`/`syncdiv` are four
candidates for two lanes and are back to being argued against each other. There
is **no `voicing` parameter**: 10 §1 keeps the era block as per-type constants,
promotable in v2 without touching type ordinals or state layout.

*This section said thirty until 2026-09-21, then twenty-four, and it is thirty
again — and they are **not the same thirty**.* The control-set trim cut six
(§4a) and the Reverb EQ spent six of the eight lanes that bought, the same day
(§4c). The paragraphs that follow are the record of both, kept rather than
deleted because the six that went can come back and whoever brings one back
should not have to reconstruct the argument against it.

**The order above is the order in `Index`, and the six EQ ids went beside their
siblings rather than on the end.** That was a readability choice that cost the
lane order: a session stores plain values keyed by id and would have survived a
reshuffle either way, but **a rack slot maps host lane N to parameter N**
(`core/rack/SlotParameter.h`), so every lane after `damphi` moved. Nothing
errors and nothing warns. It was free because nothing has shipped; **after first
ship the only legal move is appending at the end**, and the next reader does not
get this choice. `tests/plugin/RackTests.cpp`'s bank table is the second copy of
the lane order, so changing it fails a build.

### 4a. The control-set trim — six cut on 2026-09-21, and it is reversible

Frosty opened it that morning — "I'm not sure all these controls will survive
trim" — and it closed the same day, **before anything shipped**, which is what
made it free. Every one of the six is **character rather than a mix move**, what
makes a Plate a Plate rather than what an engineer dials mid-session, so none of
them vanished: each became a constant in the per-type block (§4b) instead.

| Cut | Was # | Whose call | Why, and where it went |
|---|---|---|---|
| `attack` | 6 | **Owner** | Verbatim: attack should be type dependent. The tail's bloom contour. → `TypeConstants::attack` |
| `decayshape` | 5 | **Owner** | The same sentence. The gated/linear curve. → `TypeConstants::decayShape` |
| `damplofreq` | 8 | **Owner** | "The frequencies should be handled by the onboard EQ." A damping knee is a property of a room, not a mix decision; `damplo` survives as a pure decay multiplier over it. → `TypeConstants::dampLoFreqHz` |
| `damphifreq` | 10 | **Owner** | The same argument — and it spanned 1000–2100 Hz, **1.07 octaves**, which the trim review called a constant with a knob on it. → `TypeConstants::dampHiFreqHz` |
| `ershape` | 18 | **Claude's, not the owner's** | A unitless exponent whose end-stops this pack itself records as unconfirmed (10 §7, HW-2 row), and the early cluster's contour — the same "what kind of room is this" argument the owner used for `attack` and `decayshape`. It was *already* a per-type constant; the trim took the knob, not the number. Proposed by Claude, accepted by the owner. → `TypeConstants::erShape` |
| `prelink` | 3 | **Claude's, not the owner's** | Set-and-forget: off is the reference behaviour, every type shipped it identically, nobody automates it. Proposed by Claude, accepted. It is a **fixed** constant and not a per-type field, because no type wanted its own answer. → `kPreLinkFixed = false` |

**Two of the six are Claude's judgement and are flagged as such so that Frosty
can overturn either without archaeology.** `ershape` and `prelink` were not
asked for.

**Why it was free, and where the line is.** Cutting a **float or a bool is
reversible**: state is stored as plain values keyed by parameter id
(`ParamSet::toXml` writes `getReal(i)`), so re-appending one at the end later
costs a saved session nothing — it simply gains a parameter it did not have.
All six were floats or the one bool. Cutting or shortening a **choice is not**
reversible, because it reaches the host as `juce::AudioParameterChoice`, which
normalises as index/(n−1): change the count and every recorded automation lane
on it remaps silently. **`type` and `ermode` were not touched and must not be**,
and `tests/plugin/ReverbTests.cpp` asserts both counts for exactly that reason.

**Nothing that stayed was also retuned *by the trim*.** Every row that survived
it kept its range, its step and its default, and the four constants the trim
brought in are the schema's *own previous defaults* for Room, which is why
§4b's table marks them SCHEMA rather than CALIBRATE. A fresh Room after the trim
is the fresh Room that was there before it. The cut moved no sound. (One row has
moved since, and it was not the trim that moved it: `eqhifreq` was widened from
1000–2100 Hz to 1 kHz–20 kHz on 2026-09-22 — §4c.)

The relative order of the twenty-four was unchanged and the ids were unchanged;
the six that went took their indices with them and everything after each one
closed up. That is what makes a state file written before the trim still restore
every parameter it still has. The EQ change then moved the indices again, on the
same argument and with the same freedom — see the note under §4's table for why
that freedom ends at first ship.

### 4b. A type is a voicing: fourteen constants, nine of them parameters

`erlevel` and `verblevel` joined the per-type block earlier the same day (§1),
taking `roomDefaults` from eight to ten; the trim then added `decayShape`,
`attack`, `dampLoFreqHz` and `dampHiFreqHz`, and `erShape` — already there —
stopped being a knob as well. **The row is fourteen fields wide and five of them
have no host lane**, so `typeSettings` returns **nine** settings and
`TypeVoicing` never sees the other five: those reach the engine directly in
`ReverbDsp::paramsFrom`, keyed off the TYPE value, because a `Setting` can only
name a parameter id and a field with no id has nothing to be set on.

The nine a type writes are `size`, `erdensity`, `erspread`, `moddepth`,
`modrate`, `inhicut`, `feed`, `erlevel` and `verblevel` — the rows marked *per
type* above — and a type **re-applies them on every change**, not only at
instantiation. The five with no lane are `erShape`, `decayShape`, `attack`,
`dampLoFreqHz` and `dampHiFreqHz`.

*The consequence recorded in §1 — TYPE is automatable and writes automatable
parameters — is unchanged, and the trim made it smaller rather than larger:*
five of the settings a type changes no longer have a host lane at all, so there
is nothing for an automation curve to fight over on any of them.

`predelay`, `decay`, the two damping multipliers, the **ten** EQ rows, `ermode`,
`erhicut`, `ervariation`, `width`, `mix` and `output` are the user's and stay
put through a type change.

### 4c. Built: the Reverb EQ is three parametric nodes

**Built, committed and green on `frosty-add-bmo-linger`, 2026-09-21/22.** The
table above is the built schema; this section is the record of how it got there.

*The proposal this section carried was:* the four shelf rows — `eqlofreq`,
`eqlo`, `eqhifreq`, `eqhi`, then indices 7–10 — replaced by a **3-node
parametric EQ**, three fixed nodes (low shelf, bell, high shelf) each with FREQ,
GAIN and Q, plus a **filter bool** flipping nodes 1 and 3 to cuts and greying
their GAIN. 24 − 4 + 9 + 1 = **30**, two lanes spare of 32 — the headroom the
trim bought, spent deliberately and with the argument written down, which is
what the trim's "spending it still takes an edit and an argument" meant.

**What shipped differs from that in two places, and everything else is as
proposed.**

**It is purely additive, so the ids were preserved rather than replaced.** No
parameter was removed and no id changed meaning: `eqlofreq`/`eqlo` already
*were* node 1's frequency and gain, and `eqhifreq`/`eqhi` node 3's, so a state
file written against the twenty-four restores every value it still holds. What
the six new ids buy is the middle bell, a Q on each node, and the mode. The
four rows are not "out" at all; they are two thirds of nodes 1 and 3.

**The three node shapes are fixed and there is no shape selector anywhere** —
Frosty's explicit call, and the same argument `kTypeNames` makes about counts: a
per-node shape list is a `juce::AudioParameterChoice`, which normalises as
index/(n−1), so it could never be revised after first ship without remapping
every automation point written on it. Fixed shapes give a real three-band
parametric with nothing permanent to regret, and BMO DEQ is already the module
for arbitrary shapes.

#### `eqfilter` is a four-position choice, not a bool — and four is permanent

It was a bool until **2026-09-22**. Frosty's call is that the two halves are
separately useful: a tail that needs its bottom taken off usually does not also
want its air taken off, and a bool made those one decision. The four positions
are `Off`, `Lo Cut`, `Hi Cut`, `Bandpass`, in the order the two cuts arrive on
the curve left to right with the extremes at the ends — which also puts Off at
index 0, so the default is the bottom of the lane and a fresh instance is the
shelving EQ that shipped before the mode existed.

**It cost no host lane.** Same id, same position in `Index`, same lane: the
schema stays at thirty with two spare.

**What it cost instead is the freedom to change its mind about the count, and
that is now spent.** A choice normalises as index/(n−1), so a fifth position
would rescale every automation point ever written on this lane — the trap
`kTypeNames` and `kErModeNames` already carry, and the reason this one had to be
argued before it was built rather than after. **The count and the order are
permanent at first ship.** Renaming a position stays free; adding one never is.

**Bandpass rather than "Both".** A low cut plus a high cut *is* a bandpass:
naming it after the result says what the setting does to the tail, where naming
it after the mechanism says which two switches are down. It is also the name the
position would have if it had been built as one filter rather than as two.

Whichever node the mode has made a cut: **FREQ and Q carry over unchanged** — a
cut has a corner and a cut has a resonance — and **GAIN has no meaning**, so
that node's gain stops reaching the response and its knob greys out. The gain is
**withheld and never written**, so a trip through a cut position and back
restores the shelf the user had: a mode must not eat an edit. Node 2's bell is
untouched in all four positions, which is what makes this a mode *per EQ* rather
than a shape *per node*.

One state transition is not preserved and it is worth naming, since everything
else is: a state file that had the old bool **on** restores as `Lo Cut` rather
than as both cuts, because 1 now names the first cut. Free before first ship.

#### `eqhifreq` was widened, and the bell's width was treating a symptom

Node 3 ran **1000–2100 Hz, default 1600** — **1.07 octaves**, a high shelf that
could not reach air, on a module whose commonest EQ move is darkening or
brightening a tail. That range was **inherited rather than chosen**: `eqhifreq`
predates the parametric, and making the change "purely additive" to preserve the
id preserved its range along with it. It is now **1 kHz – 20 kHz, opening at
6 kHz**.

Note what that says about the bell above it. The bell was given the full
20 Hz – 20 kHz partly for a good reason (a parametric bell should sweep the
whole band) and partly for a bad one — node 1 stopped at 1.6 kHz and node 3 at
2.1 kHz, so the bell was the only way for the Reverb EQ to reach the presence
region at all. With node 3 running to 20 kHz the bell is wide because a bell
should be, and not because it is covering for a shelf. **Read the two ranges
together.**

The three nodes now open at **200 Hz / 1 kHz / 6 kHz**, spread across the band,
instead of the bell and the high shelf sitting 0.68 octaves apart with their
markers touching on the screen. Widening a range and moving a default is free
until first ship and changes no id; the *sound* at the default is unchanged,
because every gain still opens at 0 dB.

#### The filter DSP was reproduced, not stacked on

`core/dsp/{Biquad,Design,Prototype,Svf}` exist on the **unmerged** BMO Defang
branch. They were **reproduced byte-identically** here and never inherited by
stacking, which is this repository's standing rule (§1, and the handoff's "Where
to work"); the five blobs are verified with `git hash-object` on AURORA and the
table is in `modules/reverb/AGENTS.md`. Nothing in the five was edited.
Everything BMO Linger needed on top is in `dsp/EqNodes.h`: the three nodes, the
shape table, the gain-does-not-reach-a-cut rule and the summed response.

**`EqNodes.h` is the one place the EQ's response is computed**, and both the
engine and the screen's curve go through it — so the EQ picture is a measurement
of the shipped design rather than a sketch beside it, which is what §5 names as
the display's one real risk.

### 4d. `inhicut`, still marked "owner confirm"

10 §6 stated 29 while enumerating 30. On 10's own evidence the odd one out is the
input high-cut: it appears only inside §2's *Input* sentence, beside an
explicitly **fixed** 20 Hz high-pass; §6's CPU line bundles it into "input
conditioning and EQ"; and §7's fixed-values table gives every other user control
a range row but gives it none. So 10's body reads as 29 parameters plus an
internal constant, while its own list reads 30.

**Kept as a parameter**, because §2 gives it a user range (2–20 kHz) that a
constant would not need, and because it is the only way to darken what feeds
**both** generators independently of the Reverb EQ. It survived the trim on a
sharper version of the same argument: *what you feed the reverb is a mix move in
a way that where a room stops absorbing is not.* If the owner would rather it
were a constant, deleting it before the first ship is free and returns a
**third** spare lane; after that it is permanent. It sat at index 19 while the
schema was twenty-four and is **index 25** now (§4c moved every lane after
`damphi`).

### 4e. The panel: a paged handheld, one width, and the screen carries the menu

**Two proposals have been superseded here, and both are kept below.** The first
was "main face vs expanded", DEQ's precedent — `ModuleDef::expandedWidth`, two
widths switched on the host bar, seven controls and a display on the main face
and three groups (EARLY / TAIL / TONE & OUT) behind the switch. Frosty replaced
it with a **paged handheld** on 2026-09-21: **`expandedWidth` is 0 and the
module is not expandable.** The second was that handheld's own first face — page
keys on the plate, a 105 px letterbox screen, a persistent row of three — which
Frosty had **rebuilt on 2026-09-22** when the EQ became a three-node parametric.
What follows is what is built.

- **One width, 380 px, panel 380 × 740.** A panel insets by `kPad` = 10 a side,
  so the three-column cell at 380 is (380 − 20) / 3 = **120 px** — the same cell
  the four-column 500 px face had, which is why the width came down by 120 px
  with no caption pass. `ModulePanel::kContentHeight` is **688 suite-wide** and
  `RackEditor` sets every panel to it, so the total cannot move; the arithmetic
  inside it is `ReverbPanel`'s own class comment and is not repeated here.
- **The screen is 258 px and its top 26 px is the page menu**, drawn inside the
  display in the screen's own ink — three divided segments, the selected one as
  inverted video, a hairline between each pair and a rule under the band. That
  leaves **232 px of drawing area**, against 105 on the first face. The three
  round page keys and their PAGE rule are gone from the plate: they were a row
  of buttons duplicating what the picture already says, and a handheld's page
  menu belongs in its screen. Two consequences — **the screen takes clicks now**
  (`LingerScreen::onPageChosen`, with the panel's `setPage` the only thing on
  the other end, and a click below the band does nothing), and **nothing in the
  generic layout walk can see the menu**, because it is painted: no bounds, no
  caption, no child. So `menuSegment` and `menuLabelOverflow` are public and
  `checkReverbPanel` does explicitly what the walk used to do for the keys.
- **A 26 px segmented row sits under the bezel, reserved on every page and
  filled on two.** Frosty rejected round keys here: three circles want a 44 px
  row, which would make a sub-selection the third tallest thing on the panel,
  and a rectangle with a word in it is what every switch in the suite already
  is. **EARLY** binds it to `ermode`, a real three-position choice parameter,
  replacing the dropdown that control used. **EQ** binds it to LOW / MID / HIGH,
  which is **UI state** — `ui.node=low|mid|high`, refused rather than defaulted
  on an unknown value, because the schema is thirty with two lanes spare and
  which node a panel is pointed at must never take one. **TAIL has none, and the
  absence is meaningful**: segments appearing is what says there is a
  sub-selection on this page. It is **one component with two bindings** —
  nothing about the control differs between the uses, only where the chosen
  index is kept, and a control should not know that.
- **EARLY (6 + segments):** DENSITY, ER SPREAD, ER HI-CUT, VARIATION, SOURCE,
  SIZE — with ER MODE as the segment row.
- **TAIL (6, no segments):** PRE-DELAY, WIDTH, MOD RATE, LOW x, HIGH x,
  MOD DEPTH.
- **EQ (6 + segments):** FREQ, GAIN, Q, FILTER, IN HI-CUT, OUTPUT — with
  LOW / MID / HIGH as the segment row, repointing the first three.
- **Always on, at the foot:** ER, REVERB and MIX as **faders**, then TYPE over
  DECAY in the strip's fourth column. 5 + 6 + 6 + 6 controls is 23, one of which
  is a segmented row bound to `ermode` and three of which stand for nine lanes
  rather than three — which is thirty, the whole schema. `checkReverbPanel`
  asserts that sum with the two extra terms written out, so a panel that lost
  the node selector fails there.
- **The persistent row is dissolved.** It held SIZE, PRE-DELAY and DECAY, and
  none of the three was global in any sense the module could state: SIZE scales
  every tap time, so it belongs on the page that draws the taps; PRE-DELAY is
  tail-only and permanently so (`kPreLinkFixed`), so it belongs on TAIL; and
  DECAY joined the strip. WIDTH still sits on TAIL and not on the EQ page, for
  the reason the first face gave — it is M/S gain on the **tail** only, and the
  EQ page draws a response WIDTH is not one of.
- **ER, REVERB and MIX are faders, and the spec has asked for that since the
  groundwork pack** — "two absolute faders" is 10 §0's own phrase. They were
  knobs on the first two faces; `ui::Fader` in `core/ui` is what made them what
  they were always described as. A fader carries its reading under its caption,
  which is why DECAY prints one too: a knob in that row with no number would
  read as the one control whose value the panel would not tell you. **The LEVEL
  rule spans the three faders and stops** — it ran edge to edge over a fourth
  column it does not describe, and `ModulePanel::Rule` carries a `span` now.
- **FILTER is a legend ring, not a switch.** `ui::ConcentricBand` with a null
  gain parameter, which is `Knob::Style::filter`, showing **OFF / L / H / B**
  while the lane and the readout say the four words in full. It replaced a
  `ui::SwitchButton` marked provisional in the code that added it: `eqfilter`
  became a four-position choice and a `ButtonParameterAttachment` could only
  ever reach two of them. It draws in the suite's **filter azure** and not in
  the module accent, because `Knob::Style::filter` is not `character` — a
  decision taken by the shared component rather than by this panel, consistent
  with BMO EQ's LO-CUT and BMO DEQ's SHAPE.
- **TYPE's caption sits above its dropdown, with DECAY's knob below it.** With
  both captions underneath, the upper label fell between the two controls, and a
  label between two controls binds downward: it read as a second caption for
  DECAY and the column stopped being two controls. `ui::ChoiceBox::setCaptionAbove`
  is off everywhere else in the suite. TYPE is **the only dropdown left** —
  "room type makes no sense as a knob", Frosty, 2026-09-21 — and ER MODE, the
  other one, is the EARLY page's segment row now.
- **The page is UI state, not a parameter:** `ui.page=early|tail|eq` through
  `ModulePanel::setUiState`, as BMO Opto's meter mode and BMO DEQ's band already
  are. An unknown value is **refused rather than defaulted** — a render labelled
  EQ that shows EARLY is worse than no render.
- **The third page is captioned EQ and was TONE**, Frosty's call when the two
  shelves became a three-node parametric. The render key moved with it, and
  **`tone` is refused like any other unknown value** rather than accepted as a
  synonym, so a render script that still passes it stops with an error instead
  of quietly producing an EARLY page labelled TONE.
- **A page's controls are added and removed, not hidden.** A hidden component
  still has bounds and `tests/ui/LayoutTests` walks every child whether it is
  visible or not, so the layout tests walk this panel **once per page**.
  FREQ / GAIN / Q are rebuilt against the selected node's parameters when the
  **node** changes — BMO DEQ's `bindBand` mechanism — and not when the page
  does. All nine EQ parameters still exist and still automate; the panel shows
  three of them at a time, and the three are **not a uniform control**: the same
  knob at the same angle means 16 Hz–1.6 kHz, 20 Hz–20 kHz or 1 kHz–20 kHz
  depending on the segment above it, and Q's travel is twenty times longer on
  the bell. The readout line prints real values, which is what keeps it honest.
- **No speaker grille.** It was painted texture in a fourth column, raked, and
  it lost its job when the body's corners went square. Nothing on this panel
  slopes now.
- **ATTACK is the one number with no control under it.** It lost its knob in the
  trim, so the onset is printed on the TAIL page's readout line, read off
  `TypeConstants::attack` through the same `constantsFor` call the engine makes.
  It moves when TYPE moves and never when a knob does.

**Do not reintroduce a second width to make room for something** — a fourth page
is what this shape is for. The EQ's ninth and tenth controls did not need one:
a node selector took that page from twelve controls to six, which is what paid
for the screen.

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

**What was built instead, 2026-09-21 and rebuilt 2026-09-22 — the argument above
held, the single sketch did not.** `LingerScreen` **carries the page menu and
draws one of three pictures under it** (§4e), in the module's accent rather than
in LCD green: a second hue on one module is the failure the accent audit was run
to find. `plotBounds` is inside the **232 px** left below the menu, not inside
the whole component, which is what stops a curve being drawn through the menu.

- **EARLY is a time × pan scatter**, and it is the third version of that page: x
  is arrival over the real ER window, y is bearing (hard left at the top), and
  **the radius of the dot is the tap's gain**. It replaced mirrored stems with a
  bearing dash on each, which replaced a symmetric envelope. **The argument is
  VARIATION**: it is a lateral-spread control, so the picture it belongs in is
  one where lateral spread is an *axis* — turning it fans the cluster open and
  shut, which a reader sees without being told what to look at, where on stems
  it moved twenty-one short dashes a few pixels each. **An infill tap draws as a
  faint full-height line and never as a dot**, because its bearing is invented:
  the 21 core bearings come off `TapTables.h`, the infill stands in for a master
  sequence that does not exist yet (10 §3), and a dot would put a made-up
  bearing on the one axis this page exists to show.
- **TAIL draws three decay curves — low, mid and high.** The single envelope
  this replaced **showed none of the page's own controls**: it was drawn at the
  mid decay, and LOW x and HIGH x only ever moved a shaded band behind it, so
  either could be swept end to end and the line a reader was looking at never
  moved. The three are DECAY × LOW x, DECAY, DECAY × HIGH x, the mid one
  heaviest because it is what DECAY says. The readout line names the two outer
  decay times where it named DECAY, which is on a knob in the strip and was the
  one figure there a user could already read. **MOD DEPTH and MOD RATE are still
  not drawn**, and that is what this page still owes.
- **EQ is log frequency 20 Hz–20 kHz over ±24 dB**, drawn as the Reverb EQ's
  three nodes over one summed curve, through `EqNodes::design` — which is
  `dsp::designMatched`, which is the code the engine will run, so the sketch is
  a measurement (§4c). **IN HI-CUT is a washed curtain and not a fourth node
  marker**: one stroke's difference from three filled markers read as a fourth
  node of the same EQ when it is an *input* filter ahead of the EQ and ahead of
  both generators, and at its own 20 kHz default the old open circle sat half
  outside the box. A node marker says two independent things with two strokes —
  **a ring means the knobs edit this node** (three of nine parameters are shown
  at a time, so without it a reader turning FREQ must look away to find out
  which corner moves) and **a fill means the node is shaping the sound**, which
  is a gain off zero *or* a shape that removes without having a gain at all.
  That second clause is the case a gain comparison gets wrong: a cut's GAIN is
  greyed, its parameter may be sitting at 0, and a 24 dB low cut is very much
  doing something.
- **The EQ page draws a spectrum behind its curve** — Frosty's addition,
  2026-09-21 — and it **overrides "redrawn from parameters only" for that page
  alone**. EARLY and TAIL are unchanged and the screen's timer runs only while
  EQ is showing. The tap is at **the point the Reverb EQ acts on**, pre both
  generators, which is where 10 §2 puts the EQ; until there is an engine
  `DspCore::process` is a marked pass-through, so it shows the dry input, which
  is honest rather than broken — **do not move the tap to fix it.** The
  consequence for tooling is that **a render of this page needs `signal=-18`**:
  a parameter-driven screen renders at rest and an analyser does not.

**TAIL is logarithmic time, and that is a deliberate departure from the
0–500 ms window proposed above**: a fixed 0–500 ms window leaves a 20 s decay
off the right-hand edge and puts the whole 0–120 ms onset inside the first two
pixels, where a log axis keeps 45 % of the width for 1–100 ms. 1 ms is where a
reflection stops fusing with the direct sound. **The right-hand end ran to 30 s
at all times until 2026-09-22** — `bmo::kMaxTailSeconds`, the clamp rather than
a setting anybody uses — and at the 1.8 s default the curve finished about 60 %
across with the remaining 40 % a flat line. It follows the tail now, far enough
past it to leave 8 % of the *width* clear, clamped at the same 30 s. What that
costs is comparability, and the readout pays it back: the decades are labelled
inside the box and the bezel line prints absolute seconds.

Everything in this section about *why* a display earns its space, and about
sketch/DSP drift and its layout-test answer, is unchanged and is what
`tests/ui/LayoutTests.cpp` asserts — which now also walks every tick box and the
curtain against `plotBounds` on all three pages, because a clipped marker
shipped once for want of a painted thing having bounds anybody could read.

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
| Onset | First 50 ms in 1 ms windows, no jump above **3 dB** after the ER span; the bloom is monotonic over **0–120 ms** across `TypeConstants::attack` 0→100 (10 §2). **ATTACK has no host lane since the trim (§4a)**, so this is swept by selecting types or by driving `DspCore::Params` directly in the JUCE-free suite, not by writing a parameter — and Plate's 0 is an assertion of its own: the tail is immediate |
| Ringing | Late tail (2× mixing time to −30 dB): spectral flatness **≥0.3**, no 1/3-octave band **>6 dB** over the smoothed mean, envelope autocorrelation **no peak >0.2 at lags 2–200 ms**, every type |
| Modulation | 1 kHz sine, wet, tail only; instantaneous frequency from the phase derivative in 50 ms windows. Peak deviation **≤3 cents** at the top of depth and rate (10 §4). Report its spectrum — a visible rate means chorused, not randomised |
| Pre-delay | First tail sample above −60 dB within **±1 sample**, every rate; **ER taps untouched, full stop** — `kPreLinkFixed` is `false` for every type since the trim (§4a), so the peak lag over the first 100 ms is 0 unconditionally and there is no second case to test; **range cannot go negative** and `latencyForParams` returns 0 throughout (10 §2) |
| Parameter changes | TYPE: 30 ms dip, tables swapped at the minimum — no click, **no allocation**, no second engine. SIZE/PRE-DELAY: 30 ms crossfade, retriggered at 1% accumulated \|ΔS\|, windows summing to one, **ER and late sharing the scheme** (10 §3), no pitch shift on a held sine. Coefficients: no 1 ms energy jump above 3 dB |
| Stability | Matrix orthogonal to 1e−6, `max\|Hᵢ(ω)\| ≤ 1 − 1e−4`. At `damphi` 2.0 / `decay` 20 s (effective T60 40 s): ten minutes then silence, never above +6 dBFS, RMS never growing over any 10 s window |
| Denormals | 60 s of silence after a loud burst with FTZ/DAZ **disabled** — block time must not rise (the ~100× trap), tail reaching exactly 0.0f; this is what 10 §4's ±1e−20 injection is for |
| NaN / silence | ±1.0 square, DC step, denormal input, fuzzed over schema corners at every type — every sample finite; after `reset()`, zeros in gives exactly zeros out |
| Tail report | `tailSecondsForParams` **≥ measured −60 dB time** and **≤30 s**, every type, 44.1/48/96/192 kHz — what makes §2(a) mean anything |
| Bypass | No per-slot enable flag exists (`00` §2, 10 §5), so a removed reverb truncates: assert the wet bus fades over **150 ms** in `reset()` on the envelope slope, and no click into the remaining chain |
| Sample rate | 44.1–192 kHz. *Must not differ:* per-band T60 ±5%, tap times *in ms* ±0.1 ms, pre-delay ±0.1 ms, density crossing ±10%, latency **exactly 0**. *May differ:* sample values (lines re-primed per rate), modal detail above ~15 kHz, memory (linear in rate) |
| Block size | 1/16/32/64/**127**/512/2048 **bit-identical** for fixed parameters; if not, something smooths per block instead of per sample — a bug, not a tolerance |
| Buses | `numChannels` 1 and 2: mono finite and ≤3 dB down by the γ ≥ 0 rule. **Mono→stereo ships in v1** — landed 2026-09-21 on AURORA, `core/product/BusLayouts.h`, covered by `bus_tests`; the reverb sees the mono input duplicated into both channels and is free to decorrelate its tail from it |
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
| **M0** skeleton | Directory, identity and accent rows, `params.h` with the **full 30-parameter schema** (§4) and frozen type order, pass-through adapter reporting zero latency and zero tail, every registration point | Schema green; rack N→N+1; standalone loads; `ui_layout_tests` pass; both appearances hashed — after a build that exited 0 |
| **M1** panel | Real panel — **one 380 px width, three pages** (§4e), the three-picture screen carrying its own page menu, the segmented row, three faders, value strings, presets | Renders reviewed **by the owner**; ratios, gaps, hashes in `testing-notes/ui-pass-reverb-<date>.md`, naming AURORA |
| **M2** ER generator | Image-source tables, Size law and crossfade, order-banded filters, diffuser, VARIATION, hi-cut, the Density bridge — tail silent | The whole ER block of §6 plus the ER-only listening items. **This milestone decides the module** |
| **M3** late network | FDN, absorbent filters, damping over the per-type knees, EQ, pre-delay, SOURCE, modulation — plus the tail-onset and decay-truncation contours, which are now `TypeConstants::attack` and `decayShape` rather than knobs (§4a), and which the engine reads in `ReverbDsp::paramsFrom` | Modal density (incl. the Plate failure), T60, damping, echo density, ringing, modulation, pre-delay, level laws, phasing nulls, clicks |
| **M4** types | Six v1 types and their constant blocks incl. reserved era fields | Every §6 test at every type; order frozen; Plate's line count resolved |
| **M5** shared code | §2(a) as its own reviewed commit; (b)/(c) only if taken, byte-identical with BMO Dwell | Tail report ≥ measured and ≤30 s; existing modules proven unchanged by hash and schema test |
| **M6** acceptance | Invariance, stability, CPU/memory, CALIBRATE, listening | Those §6 blocks, recorded naming AURORA |

**Done** = every milestone's exit test green in all three CI jobs on both
platforms, each after a build that exited 0; no audio, renders or fonts
committed; `AGENTS.md` + `README.md` present and linked; identity row, accent and
name permanent with the collision scan recorded; `measure_reverb` registered;
figures written up naming AURORA; the listening checklist heard; branch from
Kevin's `origin/main`, stacked on nothing unmerged.

**Blocking unknown:** none.

**Decided:** the name, the module id, the accent (`#e694e0`, §3), the type list
with Cavern at index 3 (§1), the **30-parameter count** (§4), and `feed`'s
caption — **SOURCE**, the owner's word on 2026-09-21, beating both 10's
*Diffusion* and this pack's proposed *TAIL FEED*, because "diffusion" means
density everywhere else in the suite and a third word beat both. The panel shape
is decided too: one 380 px width, three pages, no expanded section (§4e).

**Built, and no longer pending:** the 3-node parametric EQ (§4c), with
`eqfilter` as a **four-position** choice rather than the bool this pack
proposed — and **that count is permanent at first ship**, because a choice
normalises as index/(n−1). `eqhifreq`'s range was widened to 1 kHz–20 kHz in the
same pass. The panel was rebuilt around it on 2026-09-22 (§4e): the page menu
moved inside the screen, the persistent row dissolved, and the three levels
became faders.

**Owner confirm, still open:** whether `inhicut` ships as a parameter or becomes
a constant (§4d); whether ER SPREAD greys out in Taps mode or sits inert — *ER
SHAPE is no longer part of that question, having lost its knob in the trim*
(§4a); the MIX law and its default.
