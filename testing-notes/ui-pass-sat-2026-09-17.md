# The UI pass — BMO Saturator, module 7

**On AURORA, 2026-09-17.** Branch `ui-pass`, worktree `../bmo-mix-rack-333-ui`,
starting from `e3983ec` after BMO Util. Local only, nothing pushed and no CI.
Frosty drove it from his phone over Remote Control; every call below was made on
renders in both appearances.

Read `ui-pass-render-loop.md` for the tools.

---

## 1. Where it ended

| | dark | light |
|---|---|---|
| before | `270159d1c8ccccb5` | `e076b68d1d065d98` |
| after the caption and centring work (`8e56cd1`) | `6a2218d7b6bd28e3` | `237d1ac8657d7a93` |
| after the oversampling section | `0a94a1b7dbdb1b6d` | `4777584d5a82c801` |
| after, `oversampling=8x` | `27e1d91ad8614d39` | — |

Largest bare band, dark: **66 px → 43**. The six other panels and the rack hash
as before; `ui_layout_tests` and `sat_tests` are green.

**`signal=` moves nothing on this module.** No meters, so a bare render and
`signal=-18` are byte-equal, the same as BMO DEQ at Init. Every figure here is a
bare render.

## 2. The opening state, first

Clean, before and after. DRIVE's pointer rests on its 40% dot, TONE and MIX sit
at 100%, and the new switch row is the one thing on the panel whose default state
is *nothing lit* — which is Off, and is covered below because a render cannot
tell that from a broken control.

## 3. DRIVE's name (`8e56cd1`)

Frosty: much larger. It was the suite's 15 pt, the same as the two knobs you
reach for after it, on the control the module is named for.

| | | verdict |
|---|---|---|
| A | 20 pt | passed over |
| B | 24 pt | passed over |
| **C** | **28 pt** | **taken** |
| D | 32 pt | passed over |

At 28 the word is 144 design px of the 240 the panel has, nowhere near the width
that would shrink it to fit.

**The face does not pay for the name.** A caption comes off the top of its knob's
cell before the knob squares itself in what is left, so a larger caption shrinks
the dial it belongs to. The row grows by exactly what the caption takes instead,
and DRIVE's face measured 272 render px in every candidate, including today's.

## 4. The caption gaps (`8e56cd1`)

Frosty: match the other controls to Util and the compressors. Measured face to
caption ink, in render px:

| | before | after |
|---|---|---|
| DRIVE | 74 | 30 |
| TONE, MIX | 68 | 30 |
| INPUT, OUTPUT | 30 | 30, untouched |
| BMO Util, BMO Opto's MAKEUP, LTV Comp | 30 | — |

By `PlainKnob::setCaptionLift`, which BMO Util's own pass added a day earlier.
**A lift is measured against the box it is applied to**: DRIVE wanted 25 at the
full row and 20 after §6 trimmed it, where that same 25 measured 20 px rather
than 30. Re-measure rather than re-derive whenever a row or a caption size moves.

## 5. Centring, per section (`8e56cd1`)

Frosty: recentre per section, input and output sections excluded. The middle is
two sections with a rule between them and it had been centring as one block, so
the slack pooled above DRIVE and under MIX.

| | DRIVE | pair | verdict |
|---|---|---|---|
| E | 44 / 61 | 44 / 49 | boxes centred; the ink still reads high |
| **F** | **52 / 53** | **46 / 47** | **taken** |

A knob's box carries more air above its face than its lifted caption leaves under
it, so centring the boxes is not centring what you see. F's nudges are measured
constants and only true for the sizes above.

## 6. The oversampling section

Frosty: it needs a control. The parameter has been on the schema since 0.2.0 and
nowhere on the panel — host automation only, which is also still true of BMO EQ
(module 8, and its default is 2x rather than Off).

| | | verdict |
|---|---|---|
| a | BMO EQ's LO-CUT pattern: a dial with its positions round it | cost a 76 px row and a 20 px rule |
| b | a third knob in the pair row, printing its value | TONE and MIX down to a third of their faces |
| c | a small dial in the bare plate beside OUTPUT | cost nothing; read as a second trim |
| **d** | **a rule and three switches, side by side** | **taken** |

Frosty took the section from a, the switches from his own ask: **2x / 4x / 8x,
with Off the position none of them lights.** 20 px of rule and 28 of switches, at
the suite's switch width and gap, so the row reads as the same kind of control as
SAT / Ø / AUTO under it. They light in `switchAlt` — anything-else by the table
in `modules/AGENTS.md` — the same as AUTO.

**Three switches over one choice parameter**, in radio behaviour like BMO Opto's
TELE / ELD and its meter's IN / GR / OUT: the click sets the parameter and the
parameter lights the switches, so a click and host automation cannot disagree.
Clicking the lit one is the way back to Off, Off being the position with no switch
of its own.

`checkSatOversampling` in `tests/ui/LayoutTests.cpp` pins the geometry **and** the
behaviour. The behaviour half matters because nothing else can see it: if the
parameter loop broke, no switch would ever light, and the default render is a row
with nothing lit either way. `checkSwitchLabelsFit` already covered the labels —
it learned to walk plain toggles when BMO DEQ's clipped to "BEL".

## 7. The 24 px

The section is paid for out of the middle's spare 55 px, so no control gave up
anything for it. What it could not buy was *slack*: 7 px left over is not enough
for a section to centre its ink in.

| | DRIVE | switch rows apart | DRIVE face |
|---|---|---|---|
| keep the row whole | 35 / 47 | 12 px | 136 design px |
| **row gives up 24 px** | **43 / 47** | **18 px** | **121** |

Frosty took the trim, 2026-09-17.

## 8. Open

- **Host names** `Sat In` and `Auto Gain` against SAT and AUTO: Frosty says fine,
  unlike Dimension's. Not changed.
- **The light pointer**, white on a pale face at 1.42:1: Frosty says okay, and it
  is suite-wide rather than this panel's.
- **The voicing bell has still never been heard here** —
  `saturator-voicing-retest.md`, untouched by this pass, which was UI only.
- **BMO EQ's oversampling** has no control either. Module 8.

## 8a. What oversampling costs, measured

Frosty asked whether the defaults on CEQ and DEQ add latency. Taken by compiling
`core/dsp/Oversampler.h`'s own `oversamplerLatency` and running it on AURORA, not
by reading the constants:

| | samples at base rate | at 48 kHz |
|---|---|---|
| Off | 0 | 0 |
| 2x | 40 | 0.83 ms |
| 4x | 60 | 1.25 ms |
| 8x | 70 | 1.46 ms |

- **BMO DEQ: none, in any mode.** No oversampling parameter at all, and
  `DspCore::latencySamples()` is a constexpr 0. `Design.h` names oversampling as
  what costs TDR Nova its latency, so this is the design working.
- **BMO EQ / CEQ: 40 samples at its default**, because it defaults to 2x rather
  than Off -- `modules/eq/params.h:86` calls it the one module in the suite whose
  default is not zero-latency, for the 1073 model's 16 kHz shelf. Whether that
  default is still right is module 8's question, and a product one.
- **The Saturator still defaults to Off.** What changed today is that the panel
  can now reach the other three, where only a host could before.
- **A rack reports the sum** of its slots (`tests/plugin/RackTests.cpp`), so a
  CEQ and a Saturator on 8x in one rack is 110 samples.

## 9. Next

BMO CEQ, the last of the 09-05 three, including the BMO EQ → BMO CEQ rename and
the second preset-migration hop (`ui-pass-handoff-2026-09-14.md` §2B).
