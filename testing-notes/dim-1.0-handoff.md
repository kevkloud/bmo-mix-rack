# BMO Dimension 1.0 — handoff

Module six: a stereo imager. What this branch adds, what was measured to
justify it, and what is still open. Written for someone reading the diff
cold.

Two commits built it and reviewed it. Every number below came from driving
the real DSP, not from a simulation of it — the harnesses are throwaway, but
the assertions worth keeping were folded into `tests/dsp/DimDspTests.cpp`.

**Status: 12/12 ctest green, VST3 and standalone build on both platforms,
and the module has never been heard in a DAW.** That last part is the
important one and is section 6.

---

## 1. What it is

Three stages in series, **all of which process the side signal only**, with
the mid path left as a plain wire:

| Stage | Does | Modelled on |
|---|---|---|
| Generate | Two opposed detuned voices, their difference injected into S | MicroPitch, CLA Vocals |
| Diffuse | Modulated all-pass cascade on S | Dimension D / phaser |
| Image | Width, Gerzon bass shuffler, rotation, asymmetry | Waves S1 |

The topology is the whole point. Because `L + R = 2M`, side-only work
cancels in the mono sum **by construction rather than by testing**.

Identity: id `dim`, plugin code `Bdim`, bundle `com.lt3audio.bmodimension`,
presets `.bmodim`, design width 220 (matching Opto), accent lavender
`#d4a4ff`.

### The detune stage is mono-safe, which the research said was impossible

The research this was built from concluded that a detuner is a modulated
comb and therefore cannot be mono-compatible. That is true of the
*conventional wiring* — detuning left against right — and not of detuning as
such. Feeding the mid to **both** voices and injecting only their
**difference** into S means the mono sum never sees it.

Measured with every stage running, at three sample rates:

| | worst `(L+R)` error |
|---|---|
| 44.1 kHz | 2.4e-7 |
| 48 kHz | 2.4e-7 |
| 96 kHz | 2.4e-7 |

That is float rounding. The mid path is literally untouched.

**Read the headline test honestly, though.** `dim_dsp` asserts the mono sum
at every setting, and on its own that assertion is close to tautological:
the mid path is a wire, so `L + R = 2M` whatever the side chain does. It is
a good guard against someone later wiring into the mid path — which is what
it is for — and it is not evidence that the DSP sounds good.

### Latency is zero in every configuration

A pitch shifter needs a window, and this one uses 30 ms. But the mid path is
a wire and the detune voices only ever *add* to the side signal, so nothing
the host receives is a delayed copy of what it sent. There is no alignment
for PDC to restore. It is zero with the stage in and with it out, so
switching never renegotiates.

---

## 2. Rotation and asymmetry break the mono sum, deliberately

Both are identity at their defaults, so a Dimension left alone is
mono-exact. Turned, they are the two documented exceptions, and
`dim_dsp` asserts that they **do** break the sum — an exception nobody has
written down is indistinguishable from a bug.

They are not the same shape, and the difference matters:

- **Rotation** moves a centre source off centre, and is meant to.
- **Asymmetry** does not. A source with no side content passes it untouched.
  Only material that is already off centre changes level, and the mono sum
  moves for that reason rather than because the centre moved.

---

## 3. Asymmetry was rebuilt from the S1 manual

It shipped in the first commit as an unequal output trim — `outL *= 1+a`,
`outR *= 1-a` — described in its own comment as "a reading, not a port". It
was a conventional balance control, and a dead-centre 0.5/0.5 source came
out **0.75/0.25**.

The S1's manual specifies the control in three sentences:

> "does not affect central mono in-phase sounds in any way, but adjusts the
> relative level of left and right sounds"
> "differs from conventional balance control in that it keeps center sounds
> in the center"
> "changes relative balance of left & right both in stereo and in mono"

Centre untouched forbids the mid→side term. A balance that moves in mono
requires the side→mid term. That leaves exactly one linear answer, a shear —
`mid += a * side`, side left alone. The shipped code had **both** terms.

### Why the shear, and what it costs

Writing the family as `mid += a·side; side += b·mid`, `b` is free and buys
centre drift with far-side width at a fixed rate. All four settings below
move the balance by the same −2.50 dB at half knob:

| `b` | centre drift | far side |
|---|---|---|
| `0` — shear | **0.00 dB** | 133 % | ← shipped |
| `a/2` | +2.18 dB | 117 % |
| `a(a/aMax)²` | +1.09 dB | 125 % |
| `a` — balance | +4.44 dB | 100 % | ← was |

Two things decided it. The drift under the old law was **not confined to the
extremes** — 0.87 dB at a tenth of the knob and 2.18 dB at a quarter, i.e. a
lead vocal audibly off centre at a mild setting. And the shear's cost reads
better as **width** than as a leak: the side it attenuates also spreads, 114 %
at a quarter and 133 % at half, which on a module whose headline control is
WIDTH is its own vocabulary. The mono sum lands on the intended figure
either way; nothing cancels.

The quadratic was written specifically to test whether drift confined to the
extremes might beat the widening. It delivers that, but its width climbs to
126 % and then falls back to 100 % at the top, so the knob undoes one of its
own side effects near the end. Rejected for that. **`b = a/2` is the
fallback if the widening reads badly**, not the quadratic. All three are
written up at the shear in `modules/dim/dsp/DspCore.h`.

The coefficient is capped at half scale. Both laws degenerate at the top
otherwise: the shear turns the far side into pure anti-phase content, and
the balance silenced a channel outright — which is what the module shipped
doing at ASYM 100 %.

### Rotation and shuffle were checked against the same manual and are right

Rotation: *"no effect on the internal sound balances of stereo mixes, only
on their positioning"* — that is a pure rotation, which is what is
implemented. The manual does **not** specify a sign, and ours moves the
image **left** for positive degrees, which is the opposite of a pan knob.
Free choice, one line, left alone.

Shuffle: the manual gives 1–3, 350–1400 Hz, best 1.6–2.5, recommended
2.0–2.5 at ~650 Hz. `params.h` matches exactly and the "Bass Shuffle" preset
sits on the recommendation.

---

## 4. Three defects the test suite could not see

All three were live in the first commit and all three passed 12/12.

**The detune stage replayed stale audio.** The voice buffers hold 30 ms and
nothing cleared them. Burst at 0.9, switch the stage out, wait a second,
switch it back in over silence → **peak side 0.8985**, a full-scale pop.
Cleared on the way in; now 0.000.

**A mono instance combed the channel.** `isBusesLayoutSupported` accepts a
mono layout, and with L folded into R every stage ran on a signal whose side
is zero by definition — so the generate stage manufactured side content and
summed it straight back into the one channel. That is the comb the topology
exists to avoid: **+1.17 dB and 0.671 of sample error.** A stereo imager on
a mono bus is a wire, and it returns early now. Bit-exact.

**Asymmetry moved the centre.** Section 3.

### Why the suite missed all of them

Every pre-existing mono-sum test feeds a source that **already has side
content**, so none could catch a centre that moves. None touched a mono
layout, and none touched a switch transition. Four assertions added, one per
defect, plus one that asymmetry still does its job off centre.

---

## 5. The packager silently dropped the module

`tools/packager/package.sh` drove a hardcoded product list. BMO Dimension
was not in it, so the tester package — and the Windows artifact CI builds
from that script — contained every module except this one. The same list had
already dropped BMO Opto once, for the same reason: adding a module touches
`modules/`, `products/` and the registry and never touches that file, so
nothing fails when it is forgotten.

It discovers products by globbing the build tree now, and the README
contents line is generated from what actually staged. **Prefer that shape
for anything new that needs to know the set of products.**

`modules/AGENTS.md` gained the full list of shared files a new module has to
touch, with a column for how each one fails — most fail silently.

---

## 6. What is still open — and the one that matters

### The generate stage makes width that throbs, and nobody has heard it

Two opposed voices, their *difference* feeding the side signal, so they beat
and the side signal periodically nulls. Side envelope peak-to-trough on a
1 kHz tone:

| CENTS | depth | beat rate |
|---|---|---|
| 5 | 25.2 dB | ~6 Hz |
| **10 (default)** | **19.6 dB** | ~12 Hz |
| 25 | 11.5 dB | ~29 Hz |
| 10, on a 110 Hz tone | **34.6 dB** | ~1.3 Hz |

Mean side level barely moves across those — average width is steady, the
*modulation* is deep. Note it gets faster and shallower as CENTS rises,
which is backwards from what a user would expect: the gentle setting throbs
hardest.

In the conventional wiring the mid takes the complementary sum, so when the
side nulls the mid peaks and the image **swings**. Here the mid is a wire,
so when the side nulls the image **collapses to mono and reopens**. That is
the price of the mono-safety, it is undocumented anywhere else, and no test
can settle whether the width reads as steady or as an audible tremolo.

**Settled by ear 2026-09-09: it passes.** Not an audible throb, a slight
tremolo at most, and no shimmer or added high end. MicroPitch is a research
reference for how others solved this, never a target — the shimmer is the part
this module does not want, so width without it is the design working, not a
shortfall. See `testing-notes/dim-bench-state-2026-09-09.md`.

### Smaller, all in the checklist

- **WIDTH at 0 silently disables everything above it** — WIDTH is downstream
  of the generate stage, so DETUNE does nothing with WIDTH parked low.
  Measured: peak side 0.00000.
- **No output trim, and up to +15.5 dB available.** SHUFFLE 3.0 × WIDTH
  200 % on anti-phase 80 Hz took a 0.5 peak to 2.98. Every other module has
  an output stage.
- ~~RATE and DEPTH are dead at the DIFFUSE 0 % default~~ **Settled 2026-09-09:
  both lost their controls and are fixed at their defaults, 0.40 Hz and 50 %.**
  The parameters stay in `params.h`; the IDs are permanent. Seven knobs and a
  switch now. `PlainKnob::setKnobEnabled` remains unused suite-wide.
- **A goniometer is the meter this panel wants** and is deliberately absent.
  `ui::ModuleContext` hands a panel five `std::function<float()>` and
  `ModuleEngine` fills them from `Meter` classes that reduce a block to a
  scalar, so there is no path carrying L/R sample *pairs*. That is a core
  change touching every module's wiring and should be argued on its own. A
  **correlation meter** would fit the existing contract exactly — one float
  in [−1, +1], one callback, no new infrastructure — and is the same reading
  a goniometer gets used for here. Noted at the top of `DimPanel.h`.

---

## 7. Two documents were drifting, and one is now a registry

**Accents are allocated in `products/AGENTS.md` from this branch on.** They
were only ever recorded in the Palette Book, which is a measurement
write-up rather than a registry and had drifted from the code twice. Reading
it as an allocation list is what nearly cost this module its accent. The new
table carries hue and both plates' contrast, because separation from the
*other* accents is the constraint that actually binds — there are more
legible colours than distinguishable ones.

The Palette Book now says so at the top and names both stale facts:

- **It lists `#d4a4ff` against BMO Opto.** Opto gave that colour up in 0.2.2
  when its panel went greyscale so the only colour on it could mean
  "engaged". Confirmed on a 2× render of both modes: grey header, grey caps,
  grey captions, colour only on lit switches and the meter hot zone. Its red
  `#e0685a` and amber `#e0b040` are **states, not an accent** — they never
  touch a cap, a caption or the header bar. That is why the lavender was
  free for Dimension.
- **The 7:1 accent figure is plate-specific and stale.** It was measured on
  a `#202024` plate; `darkTokens()` ships `#2e2e32`. On the plate that
  actually ships nothing clears 7:1 — EQ 5.87, Sat 6.55, Util 6.84,
  Dimension 6.80, track 6.02. All clear 4.5:1 comfortably, so nothing is
  broken, but "assert absolutes, never comparisons" is the house rule this
  protects and a ratio without its plate named is not an absolute.

**`Bpar` and the teal `#5ecfc0` are reserved, not spent** — for a future BMO
Parametric, which is not being built. `products/AGENTS.md` also records that
BMO EQ's name is the wrong way round for that future: BMO EQ is a Neve 1084
model with stepped frequency selectors and no continuous Q, so it is the
*specific* product wearing the generic word, while a parametric EQ would be
the general-purpose one wearing a name that reads as a variant. The
suggestion there is to rename BMO EQ — "Console EQ" over "Vintage EQ" — with
the full change list, what must not change, and the one real code change it
implies. **Nothing has been renamed. It is a proposal.**

> **Since superseded (2026-09-10):** "BMO Parametric" is now **BMO DEQ**,
> and it inherits `Bpar` and the teal. **BMO CEQ** is proposed as BMO EQ's
> new name, but no name has been chosen and nothing has been renamed.
> `products/AGENTS.md` is current; this
> paragraph records what was true when the handoff was written.

---

## 8. Building this locally

The two display faces are licensed and not in the repository, so a build
needs to be pointed at them. Resolution order is in `assets/fonts/README.md`
and unchanged by this branch; `.bmo-fontdir` is the per-working-copy record
and is gitignored.

This branch was developed and measured with:

    -- BMO fonts: %USERPROFILE%/Documents/FONTS (.bmo-fontdir)

That path is local to one machine and is deliberately not committed — set
your own once with `scripts/set-font-dir.sh <path>` and every build in that
working copy reads from there. If the configure step reports
`default, no local font folder recorded`, CMake will stop naming the missing
face rather than silently substituting whatever is installed.

### The fork-PR trap, and a diagnostic that lied about it

CI restores the faces from the repository secrets
`FONT_TG_MINERVA_BLACK_B64` and `FONT_TG_BLENDER_B64`. **GitHub does not
pass repository secrets to a pull request opened from a fork**, so such a
run fails in about 37 seconds at the "Restore fonts" step on every platform
— whether or not the secrets are set correctly.

This already happened once, on run `34282410673`. The runner header read
`Secret source: None`, and the step printed:

    The font secrets are not set on this repository.

That was **false**. They are set; pushes to `main` build fine either side of
that run. The step cannot tell an unset secret from a withheld one — both
arrive as an empty string — and it guessed the wrong one, which sent the
diagnosis looking at secret configuration instead of at where the PR came
from.

The message now names both causes, prints the event and both repository
names so they can be compared, and points at `Secret source: None` as the
tell. No behaviour change; it fails in exactly the same place.

**The practical consequence: a branch has to be pushed to the repository
that owns the secrets and the PR opened from there.** A cross-fork PR cannot
go green, and re-running it will not help.

---

## 9. Where the numbers came from

Everything quoted here is reproducible from `tests/dsp/DimDspTests.cpp` plus
short throwaway harnesses driving `bmo::dim::DspCore` directly — it is
JUCE-free, so it links against nothing. There is no `tools/measure/dim`; the
suite has `measure_eq`, `measure_sat` and `measure_opto`, and Dimension
should probably grow one before its voicing is argued about again.
