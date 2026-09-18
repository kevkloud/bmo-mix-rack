# The UI pass — BMO Dimension, module 4

**On AURORA, 2026-09-16 and 17.** Branch `ui-pass`, worktree
`../bmo-mix-rack-333-ui`, starting from `fb4efe3` after DEQ and Tune. Four
commits, all local, nothing pushed and no CI. Frosty made every call below from
renders in both appearances; the numbers are what they were taken on.

Read `ui-pass-render-loop.md` for the tools. The name map for this module is in
`modules/dim/AGENTS.md`, and it matters: the panel's words and the code's words
are different now.

---

## 1. Where it ended

    SOURCE     GENERATE
               DETUNE   DRIFT        (bracketed)
    WIDTH      DIMENSION             (148 px)
               BLOOM    BELOW 700 Hz (bracketed)
               TURN     TILT         (bracketed, ends marked L and R)

| | dark | light |
|---|---|---|
| before, at `fb4efe3` | `339dfc8560a3c548` | `4063603b21333eff` |
| after | `489299108f7a0f4b` | `d731d158c693962f` |
| after, GENERATE on | `8c49bc79cb963892` | `7265b2c9282de7f6` |

Largest bare band, dark: **72 px at 180 before, 51 px at 363 after.** EQ's 21
is still the house floor; the rest is the even rhythm itself, one gap plus the
padding around the knobs, and does not come down further without abandoning it.

**Every other panel is byte-identical** across the pass — eq
`ac0d5c76`, sat `270159d1`, util `1225e160`, opto `784ca005`, ltvcomp
`e3d5fc2f`, deq expanded `2eb512e8` and compact `62a1d9ec` (the last two
checked against a render of the committed code, since DEQ's table row predates
its own later commits).

`dim_dsp`, `dim_tests`, `rack_tests` and `ui_layout_tests` pass in Release.

## 2. Frosty's calls, in order

| call | taken on |
|---|---|
| **L and R** at the ends of ROTATE (now TURN), in **Blender 12 pt** | a ladder of Minerva and Blender at 9–13 pt. Minerva's L and R are narrow enough to read as marks. At 12 pt the ink is 8.5 px tall, level with the plus, 8 px clear of the caption |
| **ASYM (now TILT) gets them too, with its DSP sign flipped** | ASYM's + favoured the *left*: the shear is `mid += a * side`. An R there would have been wrong. Frosty chose the flip over printing R-L or leaving − / + |
| the switch is **GENERATE** | "not inherently obvious it's a necessary function" |
| **GENERATE stays centred** over its row | a render with it stacked on the CENTS knob alone |
| captions and **host names agree** | so an automation lane says what the knob says |
| CENTS → **DETUNE**, DIFFUSE → **DRIFT** | see §3 for why not MODULATE |
| SHUFFLE / FREQ → **BLOOM / BELOW**, and BELOW **prints its frequency** like DEQ's knobs | a rendered brainstorm; §3 |
| ROTATE / ASYM → **TURN / TILT** | five alliterative pairs rendered, including Frosty's ASKEW / ASYM |
| WIDTH → **DIMENSION** | "to fit the title better" |
| pairs **bracketed** | none, a well, a bracket, both |
| legends **SOURCE** and **WIDTH** | MONO / STEREO and SOURCE / IMAGE rendered first |
| hero **148 px**, SOURCE tightened | a ladder at 92, 132, 148, 156 |
| RATE / DEPTH host names → **Drift Rate / Drift Depth** | they are DRIFT's LFO and have no controls |

## 3. What was measured and not taken

- **MODULATE cannot fit.** 126.5 px of ink at 15 pt beside DETUNE's 91.5, in
  a 200 px row on a 220 px panel. No split of the row fixes it; the only way to
  keep the word is a 12 pt caption. MOTION, SWEEP, DRIFT, PHASE and MOD all fit.
- **LOW WIDTH cannot fit either**, with any word for the frequency: at every
  split one of the pair overflows (0.3 and 1.2 px at 136). Two-word captions
  that *did* fit — LOW END / XOVER, LOW WIDE / SPLIT — ran into their neighbour
  and read as one phrase.
- **The well** read best on the dark plate (lavender on `#1b1b1f`, 8.63:1)
  and cost the most on the pale one: `#d4a4ff` on the pale well `#d6d6d6` is
  **1.37:1**, against 1.73 on the plate. A bracket sits under no caption.
- **A link bar between the two knobs** was tried first and is not in the
  record as a candidate: the two dotted tracks are 16 px apart, so the bar came
  out as a 4 px stub.
- **MONO / STEREO** read well. Two reasons against, both put to Frosty: DRIFT
  works on a stereo source too, so it is not a mono control; and a MONO legend
  in a suite with Util's MONO switch reads as a mode.
- **ASKEW / ASYM** alliterate tightest but both mean "off balance", so neither
  says which one turns the field and which one leans it.
- **Stacking GENERATE on DETUNE alone**: GENERATE had 2 px to spare in its
  64 px switch against DETUNE's 9, and it passes the fit test. Not taken, for
  layout rather than fit.
- **Growing DIMENSION alone** left the biggest band where it was. The hero's
  box was the knob plus 58, and its caption uses about 22 — so the knob grew
  and the bare plate under its caption stayed. It is now the knob plus 30.

## 4. Faults found, and fixed

- **TILT (ASYM) leaned the wrong way.** No test pinned the direction; the
  existing ones ask only whether it moves off-centre material. `dim_dsp` now
  asserts absolutes: a hard-panned 0.4 tone at +50 % comes out 0.45 on the
  right and 0.35 on the left, mirrored at -50, and 0.50 / 0.30 at +100. All six
  failed against the old sign. **Not heard** — on `dim-testing-checklist.md` §2.
  A session that automated it before now leans the other way; no factory preset
  sets it.
- **A value line lifts its knob.** Showing BELOW's frequency moved BELOW's knob
  and caption 14 px above BLOOM's, and put the readout on the bracket. BLOOM now
  reserves the same line blank (`setShowsValue` with an empty format), and that
  row is 14 px taller. Readout to bracket: 12 px.
- **BELOW printed a bare number.** `shuffle_freq` had no format, so the host
  text was "700". It is `F::Hertz` now — "700 Hz", and `dim_tests` checks it.
  Display only; the range, step and default did not move.

## 5. What changed in shared code

`Knob::EndMarks { lessMore, leftRight }` and `PlainKnob::setEndMarks`
(`core/ui/LookAndFeel.h`, `Controls.h`). Off by default; only TURN and TILT use
it. Every other panel is byte-identical, which is the evidence it moved nothing
else.

## 6. Still open on Dimension

- **The flipped TILT, by ear.** Direction only; the law is unchanged.
- **No output trim, no meter** — a product decision, untouched.
- **1.73:1 lavender captions on the pale plate** — a suite question now.
- **Util's WIDTH** was deliberately the same word as Dimension's. The knobs
  still share a law and a range; the shared word is gone.
- The older notes (`dim-1.0-handoff.md`, `dim-testing-checklist.md` and the
  rest) keep the old names as records. The live docs — `README.md`,
  `AGENTS.md`, `DimPanel.h` — use the new ones.

## Next

**BMO Opto**, module 5.
