# modules/dim/ — BMO Dimension

A stereo imager. Read `modules/AGENTS.md` first for what every module owes the
suite; this file is only what is specific to this one, and only the parts that
cannot be read off the code.

The long-form record is `testing-notes/dim-1.0-handoff.md` — every number, how
it was measured, and what was tried and rejected. It is not repeated here.

## The panel's words are not the code's

Renamed in the UI pass, 2026-09-17 (Frosty). The panel caption and the host
name agree; the parameter ID, the `Index` enum and the DSP still use the
original words, and must — the IDs are permanent. This file and the DSP
comments use the code's words.

| panel / host | ID | code and older notes say |
|---|---|---|
| GENERATE / Generate | `detune_on` | DETUNE switch, detune stage |
| DETUNE / Detune | `detune` | CENTS |
| DRIFT / Drift | `diffuse` | DIFFUSE, the diffuse stage |
| — / Drift Rate, Drift Depth | `rate`, `depth` | RATE, DEPTH (no controls) |
| DIMENSION / Dimension | `width` | WIDTH |
| BLOOM / Bloom | `shuffle` | SHUFFLE, the shuffler |
| BELOW / Below | `shuffle_freq` | FREQ, Shuffle Freq |
| TURN / Turn | `rotation` | ROTATE |
| TILT / Tilt | `asymmetry` | ASYM, the shear |

The panel's two legends are SOURCE (generate and diffuse) and WIDTH (the
image stage).

## The invariant everything else rests on

Three stages run in series — generate, diffuse, image — and **all three work on
the side signal only.** The mid path is a plain wire.

Because `L + R = 2M`, side-only work cancels in the mono sum **by construction
rather than by testing.** That is the design, not a happy result, and it is the
one property to protect when changing anything here. If you find yourself
writing into `mid`, stop: you are about to spend the reason this module exists.

**Two exceptions are deliberate**, both identity at their defaults:

- **Rotation** turns the whole soundfield, so it moves centre material off
  centre. It is meant to. **At either end of TURN it also cancels material
  outright**, and that is the one real exception to the mono promise. The
  range is ±45°, a 45° rotation of the mid/side pair -- not the "quarter
  turn" `params.h` used to call it. At −45 (the L end) the output is
  `L = (L + R)/√2`, `R = (R − L)/√2`: a centre source lands hard left and
  3.01 dB down in the mono sum, a source hard-panned **left** comes out in
  anti-phase and **vanishes from the mono sum (−180.4 dB, measured)**, and a
  hard-right one comes up 3.01 dB. +45 is the mirror image. Everywhere else
  in the module the mid is preserved to −143.5 dBFS at worst (every other
  control at its maximum, on a chorused source). The range stays, by the
  owner's call on 2026-10-04; the docs say what it does instead.
- **Asymmetry** does not move the centre. A source with no side content passes
  it untouched; only material already off centre changes level. The mono sum
  moves for *that* reason, not because the centre moved.

`dim_dsp` asserts that both **do** break the sum. An exception nobody has
written down is indistinguishable from a bug.

### Read the headline test honestly

`dim_dsp` asserts the mono sum at every setting, and **on its own that
assertion is close to tautological** — the mid path is a wire, so `L + R = 2M`
whatever the side chain does. It is a good guard against someone later wiring
into the mid path, which is what it is for. It is **not** evidence the DSP
sounds good, and it should not be quoted as though it were.

## Five things a green suite did not catch

The first four were live in the first commit with 12/12 passing; the fifth
was put there by the fix for the first. Each has an assertion now. The point
of listing them is that the *class* of fault survived a full suite — and that
three of the five are DETUNE switch transitions.

| fault | what it did | why nothing caught it |
|---|---|---|
| detune buffers never cleared | 30 ms of stale audio burst out on re-engage — 0.8985 peak over silence | no test touched a switch transition |
| mono instance combed itself | generate manufactured side content and summed it back into the one channel: +1.17 dB, 0.671 sample error | no test used a mono layout |
| asymmetry moved the centre | a dead-centre 0.5/0.5 source came out 0.75/0.25 | every mono-sum test fed a source that *already had* side content |
| DETUNE switched off in one sample | the voices' difference, which is the whole side signal on a mono source, dropped out at once: a 0.49 step on a 0.5 tone, 32× the tone's own largest move | the switch test ran over silence, where a step has nothing to step from — and the listening pass did not hear it |
| DETUNE switched on into cleared buffers | clearing the buffers (the fix for row one) left a zeroed region the two voices reached ~15 ms later, a few samples apart; for those samples one had signal and the other did not: a 0.18 step, 11.7× | same blind spot as the row above; this was in the build the listening pass cleared |

**DETUNE fades out and comes straight back in** — instant on is Frosty's call
from the 2026-09-10 ear test. The fade-out is the same 8 ms as every other
control. What makes instant-on click-free:

- **The voices never stop.** They run with the stage in or out, so their
  buffers always hold the last 30 ms of live audio — never stale (row one),
  never zeroed (row five). Do not move them back inside the level check to
  save the CPU; `dim_dsp` fails if you do.
- **Engaging from fully off restarts both voices at the same point of their
  sweep.** Fed the same input, they are then identical, so their difference —
  the width — starts at exactly zero and grows as the opposite detunes pull
  them apart. That is why the level can jump straight to 1.
- **Engaging during a fade-out's tail** (within ~110 ms) does neither: the
  voices are mid-sweep and contributing, so it glides back up from where the
  fade had got to.

`dim_dsp` asserts off, on-from-off, on-mid-fade and instant, and each part
above has been removed in turn to confirm a test fails. `measure_dim pass`
prints all three switch steps.

The mono guard is the early return at the top of `DspCore::process`. **A stereo
imager on a mono bus has to be left as a wire** — `isBusesLayoutSupported`
accepts mono, and folding L into R gives a signal whose side is zero by
definition.

## What the panel dims, and why

The suite rule (`modules/AGENTS.md`): a control a mode makes inert is dimmed,
never locked or written. Four controls decide it here, and the panel asks
the functions beside `DspCore` in `dsp/DspCore.h` rather than restating them:

| knob | dead when | function |
|---|---|---|
| DETUNE | GENERATE off, or DIMENSION 0 | `centsIsLive` |
| DRIFT | DIMENSION 0 | `diffuseIsLive` |
| BLOOM | DIMENSION 0 | `shuffleIsLive` |
| BELOW | DIMENSION 0, or BLOOM 1.0 | `shuffleFreqIsLive` |
| TILT | DIMENSION 0 **and** TURN 0 | `asymmetryIsLive` |

DIMENSION multiplies the side after generate, diffuse and the shuffler, so
at 0 they are all dead. TURN comes after it and turns mid into side, so TURN
is never dead, and TILT, which reads the side after TURN, comes back to life
the moment TURN leaves 0. DRIFT is **not** dead with GENERATE off: it works
on any side content, and a stereo source has some. GENERATE is a switch and
is never dimmed -- it is the way back in -- even at DIMENSION 0, where it
too reaches nothing; that one is the owner's to decide.

At the defaults GENERATE is off and BLOOM is 1.0, so **DETUNE and BELOW
come up dimmed on a fresh instance.** That is the rule applied, not a
side effect.

`dim_dsp` renders every knob at both ends of its range in all sixteen
combinations of the four deciders: where the function says dead the two
renders are the same bits, and where it says live they differ. **BELOW at
BLOOM 1.0 is the one exception**: dead to float rounding, not bit for bit,
because the shuffler's `z * 1 + (s - z)` is not always exactly `s`. Measured
at 5.96e-8, one float step at 0.5. Making it exact would move the default
output, which is held bit-identical. `ui_layout_tests` holds the panel to a
table written out by hand, standalone and in a rack.

## Asymmetry is a shear, and the fallback is named

Gerzon's control, from the S1 manual, which constrains it in three sentences
quoted at the point of use in `dsp/DspCore.h`. Centre untouched forbids the
mid→side term; a balance that moves in mono requires the side→mid term. That
leaves exactly one linear answer:

    mid += a * side;    // side left alone

Writing the family as `mid += a·side; side += b·mid`, `b` is free. All four
settings move the balance by the same −2.50 dB at half knob:

| `b` | centre drift | far side | |
|---|---|---|---|
| `0` — shear | **0.00 dB** | 133 % | shipped |
| `a/2` | +2.18 dB | 117 % | **the fallback** |
| `a(a/aMax)²` | +1.09 dB | 125 % | rejected |
| `a` — balance | +4.44 dB | 100 % | what it was |

**If the far-side widening reads badly in a mix, the fallback is `b = a/2`.**
It is **not** the quadratic: that one's width climbs to 126 % and falls back to
100 % at the top, so the knob undoes one of its own side effects near the end.

The coefficient is capped at half scale. Both laws degenerate above that — the
shear turns the far side into pure anti-phase content, and the balance silenced
a channel outright, which is what this shipped doing at ASYM 100 %.

**The coefficient is the knob negated**, so + favours the right, as ROTATE
does. With `a` positive the shear lifts the left (whose side is positive), so
the knob leaned the image left until 2026-09-16, when the panel was about to
print an R at that end. Frosty flipped the DSP rather than the letters. The
sign is a free choice, not derivable from the manual, so `dim_dsp` pins it
with absolutes: a hard-panned 0.4 tone at +50 % comes out 0.45 on the right
and 0.35 on the left.

## Latency is zero, and stays zero

A pitch shifter needs a window and this one uses 30 ms, but the mid path is a
wire and the detune voices only ever *add* to the side signal. Nothing the host
receives is a delayed copy of what it sent, so there is no alignment for PDC to
restore. It is zero with the stage in and with it out, so switching never
renegotiates. Do not "fix" `latencyForParams` to report the window.

## Two orderings, both right, neither derived from the other

- **`params.h` order is reach-for-first.** It is permanent, and it is what a
  host's automation list shows.
- **Panel order is signal order** — generate, diffuse, image.

They are independent. Changing one to match the other breaks something.

## The accent

Lavender `#d4a4ff`, 6.80:1 raw on the dark plate, 64.4° from BMO EQ's pink. It
was free because BMO Opto gave it up in 0.2.2 when its panel went greyscale.
**The Palette Book still listing it against Opto is a stale document, not a
claim on the colour** — accents are allocated in `products/AGENTS.md` now.

## Open, and deliberately so

- **The seven factory presets have never been auditioned.** Not one, on any
  build. The listening pass on 2026-09-09 covered §1–§4 and §02–§06 of the
  meter pass and stopped short of §5. **Deferred knowingly, on Frosty's call:
  presets can be corrected after merge.** What is being accepted is a real
  risk, so it is worth stating exactly: preset levels in this suite have
  drifted before — *thirteen at once*, BMO Opto's three and all ten of the
  Saturator's (`testing-notes/opto-0.2.1-handoff.md` §4) — and the check
  that was skipped is whether any preset jumps in level against Init at the
  same settings, or puts True Peak over the ceiling. Up to +15.5 dB is
  reachable, and OUTPUT (2026-10-04) is a hand-set trim, not a catch: no
  preset sets it, so nothing pulls a loud preset back on its own.
  **Audition all seven before this is called finished**, and treat a level
  jump as a preset bug rather than a voicing choice.

- **The width throb.** Two opposed voices whose *difference* feeds S, so they
  beat and S periodically nulls. In the conventional wiring the mid takes the
  complementary sum and the image swings; here the mid is a wire, so the image
  **collapses to mono and reopens**. 19.6 dB peak-to-trough at the default,
  34.6 dB on a 110 Hz tone, and it gets *faster and shallower* as CENTS rises —
  backwards from what a user expects. **Settled by ear 2026-09-09: not an
  audible throb, a slight tremolo at most, and no shimmer or added high end —
  which is the wanted result, since width without shimmer is the brief.** The
  measurement disagreed: 16.1 % of windows in the host bounce read anti-phase,
  which the meter pass lists under Do Not Want To See. It described the signal
  correctly and predicted the wrong cost.
- **WIDTH at 0 silently disables everything above it**, DETUNE included, since
  WIDTH is downstream of generate. Measured: peak side 0.00000.
- ~~No output trim, and up to +15.5 dB available~~ **Settled 2026-10-04 (the
  owner): OUTPUT, ±24 dB in 0.1 dB steps, default 0, appended as the
  eleventh parameter and rack lane 11.** At the extremes the module reaches
  +7.2 dBFS peak and +15.3 dB of side gain. It is the equaliser's trim --
  per sample, in dB, landing exactly -- copied into `dsp/DspCore.h` because
  modules do not include one another, and at 0 dB with nothing moving it is
  out of the path, so everything saved before it plays bit for bit as it
  did. It applies on a mono instance too: it is a level, not imaging. An
  automatic level match was considered and not chosen: it needs a detector,
  and "matched" is ambiguous for a widener -- in the mono sum, which this
  module leaves alone, or in the stereo power it exists to change.
- ~~RATE and DEPTH are dead at the DIFFUSE 0 % default~~ **Settled 2026-09-09:
  neither was audible enough to earn its space, so both lost their controls and
  are fixed at their defaults.** The parameters stay in `params.h` — IDs are
  permanent and append-only, and a session that automated them must still load.
  A stronger answer than `setKnobEnabled` dimming, which the panel uses for
  the controls above that a mode leaves dead.
- ~~ROTATE +30° moves the image left~~ **Settled 2026-09-09: the sign is
  negated in `setParams`, so + moves the image right like a pan knob.**
  Confirmed backwards by ear on a stereo source before the change.
- **A goniometer is the meter this panel wants** and is deliberately absent —
  `ui::ModuleContext` carries five `std::function<float()>` and no path for L/R
  sample *pairs*. A **correlation meter** fits the existing contract exactly
  and is the same reading. See the note at the top of `panel/DimPanel.h`.

## Where the numbers come from

`tests/dsp/DimDspTests.cpp` and **`tools/measure/dim`**, which drives
`bmo::dim::DspCore` directly — it is JUCE-free, so it links against nothing.

This file used to say there was no such tool and that the module should grow
one "before its voicing is argued about again". The voicing was argued about
on 2026-09-09 and four throwaway harnesses were written to settle it, so the
tool now exists with those folded in. `tools/measure/dim/README.md` says what
each mode is for; each one is the answer to a specific way of being wrong, and
all four happened during that pass:

- **`source`** — is the file even usable for the test? A mono source cannot
  exercise ASYMMETRY, and one nearly got used for it.
- **`pass`** — the meter pass offline. Predicted the host bounce to within
  2 points of the anti-phase share.
- **`corr`** — correlation of a bounce, level-gated, because ungated it
  reports the dither between phrases.
- **`comb`** — per-frame band deviation. A time-averaged spectrum cannot see
  a *moving* comb and reported none where there was a 19.78 dB one.
