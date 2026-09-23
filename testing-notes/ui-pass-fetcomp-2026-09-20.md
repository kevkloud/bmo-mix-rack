# BMO FET — first visual pass

**On AURORA, 2026-09-20**, on `frosty-add-bmo-fetcomp`, from the `build/` tree
(Visual Studio 17 2022, **Debug**). Renders are `tools/snapshot`, figures are
`tools/inspect/Inspect.exe` on the pixels. `snapshots/` is gitignored —
regenerate rather than look for these files.

`Inspect.exe` had to be rebuilt first: the binary in the tree predated its own
`gaps` mode. `csc -out:Inspect.exe -r:System.Drawing.dll Inspect.cs`.

**The module's DSP is still the placeholder**, so two things in
`docs/fet-comp/11-integration-and-test-plan.md` §4d could not be done and are
not claimed: step 4's GR render "driven to 20 dB+" — `currentGainReductionDb()`
is a flat zero, so the GR needle can only be rendered at rest — and anything
that depends on the meter moving. Both belong with the DSP.

## 1. BMO Opto is byte-identical

`ui::DynamicsMeter` grew an opt-in `setBezelAlpha`, defaulting to the 0.7 it
has always stroked at. Opto never calls it. Hashes before the edit and after,
`signal=-18`:

| render | before | after |
|---|---|---|
| `opto appearance=dark` | `ab3ff3b77116b7a5` | `ab3ff3b77116b7a5` |
| `opto appearance=light` | `878cca7b1a80a551` | `878cca7b1a80a551` |
| `opto appearance=dark ui.meter=GR` | `88a7653a82c19ae0` | `88a7653a82c19ae0` |

BMO Opto is the only `DynamicsMeter` consumer in the tree; LTV Comp's meters
are its own `LevelBar`s and mention the class only in prose.

## 2. The border-alpha gate — eight renders, owner picks

Both variants × both voicing states × both appearances, `signal=-18`, meter in
GR. Rendered with `ui.bezel=stock|full`, which exists so the gate does not need
a rebuild to see; the shipped value is `kBezelAlpha` in `FetcompPanel.cpp` and
was **0.7** when this section was written — §6 records Frosty picking full alpha.

    snapshots/fetcomp-{dark,light}-{blue,black}-{stock,full}.png
    snapshots/fetcomp-bezel-gate-sheet.png     the eight, side by side

**Measured off the pixels**, bezel at design row 960, against `meterFace`
`#464649` (the same colour in both appearances, so the figures are too):

| variant | Blue bezel | Black bezel | pair | Blue on face | Black on face |
|---|---|---|---|---|---|
| stock 0.7 | `#5075aa` | `#151516` | **3.88:1** | 2.00:1 | 1.94:1 |
| full alpha | `#5489d4` | `#000000` | **5.91:1** | 2.65:1 | 2.23:1 |

Every figure 11 §4b predicted by formula is confirmed **except one**: it gives
the full-alpha pair as 5.13:1 and the render measures **5.91:1**. The formula
figure was the low one, so full alpha separates better than the pack claimed,
not worse. Everything else — `#5075aa`, 3.88, 2.00, 1.94, 2.65, 2.23 — lands
exactly.

Both variants read as two states in both appearances on the sheet. **Not
decided here: the owner picks.**

## 3. The accent, measured rather than assumed

`#5489d4` is an approved exception and every figure in 11 §4c is confirmed on a
real render:

| | measured | 11 §4c |
|---|---|---|
| accent on `#2e2e32` | 3.80:1 | 3.80 |
| accent on `#efefef` | 3.09:1 | 3.09 |
| dark-plate knob cap (raw accent) | 3.80:1 | 3.80 |
| `pointer` `#2b2b2e` on that cap | 3.97:1 | 3.97 |
| pale-plate cap, `faceOf` wash | `#a9c4e9`, 1.55:1 | `#aac4ea`, 1.55 |

The pale wash is one channel off the pack's hex and the same ratio.

**The dark plate did not need the render pass 11 expected.** The cap reads as a
solid mid-blue and is plainly the module's colour; 3.80:1 is a low figure for
small text and this is a 84 px disc.

**Knob captions keep the suite's convention and are the raw accent**, which
11 §4c asks to avoid. The premise there is that captions go through
`accentTextOn` and come back a different blue — true until 0.2.3, when
`PlainKnob` swapped the stepped colour onto section legends and left the raw
accent on captions. So E-as-caption measures 3.80 dark / 3.09 pale against the
5.87-dark / 1.72–2.00-pale a shipped raw caption runs: better than the band on
the pale plate, worse on the dark one, over 3:1 on both, and it reads.
`accentTextOn` is used only for the meter's hot zone, and this panel draws no
rule legends at all. **Worth an owner glance**, since it is the one place this
module reads the pack's instruction against the code rather than with it.

## 4. Layout

`ui_layout_tests --dump`, design px, panel 260 × 688:

    INPUT   10,14  120x120     OUTPUT  130,14  120x120
    ATTACK  10,144 120x120     RELEASE 130,144 120x120
    4:1/8:1   y 274   12:1/20:1 y 308   ALL y 342
    meter   17,378 226x102     IN/GR/OUT y 484, 70 px each
    BLUE/BLACK y 520           MIX 10,556 240x78     2x/4x y 644

The IN/GR/OUT row uses the full `Tokens::switchWidth`: 226 px fits inside 260,
so this panel drops the narrow-switch exception `modules/AGENTS.md` records for
BMO Opto's 220.

`Inspect.exe gaps`: **largest bare band 29 px at 118..147**, identical in both
appearances. Against the README's ranking that sits between BMO CEQ's 21 and
DEQ expanded's 30 — tight, and nowhere near BMO Opto's 48 or the Saturator's 66.

## 5. Suites

`ctest --test-dir build -C Debug`: **18/18 passed**, including the new
`fetcomp_dsp`, `fetcomp`, and `rack` and `ui_layout` with the module in them.
`ctest --test-dir build-dsp -C Release` (DSP-only, Tune on): **16/16 passed**,
`tune_hardtune_target` disabled as always.

---

# Render and layout pass

**On AURORA, 2026-09-20**, second session, working to
`docs/fet-comp/HANDOFF-render-pass.md`. Built in a **separate `build-ui/`
tree** (Debug, snapshot + `ui_layout_tests` targets only), because another agent
was implementing the real DSP in `build/` and `build-dsp/` at the same time. No
tracked source file was edited in this pass; renders and measurements only.

## 6. The full-alpha edit, compiled and shipped

The uncommitted `kBezelAlpha = kFullBezelAlpha` edit had never been compiled
when the handoff was written. It compiles. Shipped hashes, `signal=-18`:

| render | hash |
|---|---|
| `fetcomp appearance=dark` (Black, the default) | `65a43d6afd63e58b` |
| `fetcomp appearance=light` | `cdb63925bd4911bd` |
| `fetcomp appearance=dark ui.meter=IN` | `7c5c9d79601dfb6d` |
| `fetcomp appearance=dark ui.meter=OUT` | `eb631cc29987997e` |

`ui.meter=GR` hashes **identically to the default** dark render, which is
correct rather than a fault: `FetcompPanel.cpp` opens the meter in GR
deliberately, so the flag asks for the state the panel is already in. IN and
OUT both differ, so the flag is live.

The default render shows a **black** bezel because `params.h` makes Black the
default voicing. Blue was rendered separately to measure it.

**Measured on the shipped renders**, bezel at render row 960 against `meterFace`
`#464649`: Blue `#5489d4`, Black `#000000`, pair **5.91:1**, Blue on face
2.65:1, Black on face 2.23:1. Every full-alpha figure in §2 above is confirmed
on the render that actually ships.

**BMO Opto re-proved unchanged**, which §4d step 6 requires once full alpha
wins: `ab3ff3b77116b7a5` / `878cca7b1a80a551` / `88a7653a82c19ae0`, all three
identical to §1.

## 7. Caption gaps — measured on every box, and the suite's "30" is conditional

Method: crop each control's box out of the render and run `Inspect gaps` at
`scale 1`, so the bare band between the knob face and the caption ink is read in
**render px**. Validated before use — util VOLUME comes back at exactly the 30
that `SatPanel.cpp` documents.

| box | cell, design px | face to caption ink |
|---|---|---|
| **fet INPUT / OUTPUT / ATTACK / RELEASE** | 120x120 | **26** |
| **fet MIX** | 240x78 | **15** |
| util VOLUME | 140x150 | 30 |
| opto COMP / MAKEUP | 136x150 | 30 |
| sat INPUT / OUTPUT | 240x78 | 15 |
| deq FREQ | 84x113 | 22 |
| deq THRESH | 96x101 | 16 |

Identical in both appearances.

**The suite figure of 30 is a property of the 150-tall character-knob cell, not
of the suite.** `SatPanel.cpp` states it as "the suite sits at 30", and for the
cells it was measured on — util's 140x150 and Opto's 136x150 — it does. Across
the rest of the rack the gap tracks the cell, and the Saturator's own INPUT and
OUTPUT, which that comment calls "already at 30 and not touched", measure **15**
on a 240x78 cell.

So, against precedent grouped by geometry:

- **MIX needs nothing.** 15 px on a 240x78 cell is the Saturator's number on the
  same cell, exactly.
- **The four round knobs need nothing either.** 120x120 has no precedent in the
  rack, and 26 sits on the curve between DEQ's 22 at 84x113 and the 30 of the
  150-tall cells.

`setCaptionLift` is therefore **not indicated anywhere on this panel**, which is
the opposite of what module 7 found and the reason the handoff asks for every
box rather than one.

## 8. Contrast, measured on the shipped renders

Inks taken by `Inspect hist` over each box rather than assumed, then `ratio`:

| ink | ground | dark | light |
|---|---|---|---|
| knob caption `#5489d4` (raw accent) | plate | 3.80:1 | 3.09:1 |
| knob value `#9a9aa4` / `#9a9a9a` | plate | 4.85:1 | **2.45:1** |
| unlit switch label `#ffffff` | switch face `#6f7076` | 4.94:1 | 4.94:1 |
| lit switch label `#142133` | accent `#5489d4` | 4.56:1 | 4.56:1 |

The unlit switch face is `#6f7076` in both appearances, so its figure is too.

Two things for an owner glance:

- **The value string measures 2.45:1 on the pale plate**, the lowest figure on
  the panel and below the caption it sits under. ATTACK and RELEASE are the two
  controls whose value string carries real information — the knob position *and*
  the time — so this is the one number here worth a decision. It is suite-wide
  rather than this module's: the ink is the shared value colour.
- **The lit switch is the raw accent**, confirmed on the pixels, which is
  handoff step 2's third open deviation.

## 9. Suites

`ui_layout_tests` passes and its dump is unchanged from §4 — every box, rule
count and switch row identical. `Inspect gaps` on the whole panel is also
unchanged: **largest bare band 29 px at 118..147**, both appearances.

**No full-suite green is claimed for this pass.** `ctest` in `build/` printed
18/18 while `rack_tests` had in fact failed to compile against the DSP agent's
in-flight `modules/fetcomp/dsp/` — `DspCore.h:380` calls
`StaticStages::adoptStateFrom`, which `Stages.h` did not declare at the time —
so ctest ran a stale `rack_tests.exe`. That green is not evidence and is not
counted here. `build-ui/` built its two targets clean, exit 0.

## 10. A verified green, and how nearly it was not one

§9 above could claim no green. There is one now, taken in the isolated worktree
`bmo-mix-rack-333-fetui` on AURORA, which carries the **committed** DSP and so
is unaffected by the half-written files in the main tree:

    cmake --build build-ui --config Debug --parallel   exit 0, zero error lines
    ctest --test-dir build-ui -C Debug                 exit 0, 28/28 passed

`tune_hardtune_target` disabled as always. This tree configures Tune as well, so
it runs 28 tests where the main tree's `build/` runs 18 — `fetcomp_dsp`,
`fetcomp`, `rack` and `ui_layout` are all in it and all pass.

**The first attempt at this reported success and had failed.** The build was run
as `cmake --build ... | tail -6; echo "BUILD_EXIT=${PIPESTATUS[0]}"`; the shell's
own status came from the `echo` and was 0, while `PIPESTATUS[0]` was **1**. Worse,
`tail -6` had thrown away every line that said why. This is the same fault as §9
wearing different clothes — a pipeline's exit status is the *last* command's, so
piping a build into `tail` or `grep` discards its failure along with its errors.

**Redirect a build to a log and test `$?` directly**, then grep the log. The
second run, done that way, exited 0 with zero error lines; the first failure did
not reproduce and was parallel-build contention.

---

# Step 4, unblocked: the meter driven by the real DSP

**On AURORA, 2026-09-20, 20:30.** The DSP session had ended leaving its work
**uncommitted** in the main tree at `c142f37`. Those files were copied out into
the worktree `bmo-mix-rack-333-fetui` and built there; **the main tree was not
written to**. Everything below therefore describes the uncommitted DSP as it
stood at 19:18, not anything committed.

**It builds and it passes.** `cmake --build build-ui --config Debug --parallel`
exit **0**, zero error lines; `ctest` exit **0**, **28/28**. The
`StaticStages::adoptStateFrom` breakage of §9 is fixed at `Stages.h:267`, and
`currentGainReductionDb()` now returns a real `reportedReductionDb` instead of
the flat zero that blocked this step all session.

## 11. GR is live, and it pins rather than wraps

`input=` drives the reduction; `signal=-18` throughout, meter in GR, dark.

| INPUT | needle reads |
|---|---|
| +8 | ~5 dB |
| +10 | ~6 dB |
| +20 | ~12 dB |
| +25 | ~15 dB |
| +30 | ~18 dB |
| +45 and above | hard on 24 |

**The pin is proved on pixels rather than by eye.** A whole-panel hash cannot
answer this -- the INPUT knob itself rotates between renders, so every panel
hash differs whatever the needle does. Cropping to the meter alone (render
`34,860 452x204`) and hashing that:

| render | meter crop |
|---|---|
| `input=30` | `486546804e5e2b66` |
| `input=45` | `5c717d7c8f32292d` |
| `input=50` | `5c717d7c8f32292d` |
| `input=55` | `5c717d7c8f32292d` |
| `input=60` | `5c717d7c8f32292d` |

Identical across a **15 dB spread of extra drive**, and different below it. The
needle tracks up to the limit, stops there, and does not wrap -- which is what
11 §4d step 4 and the M6 "pinned-GR stability" line ask for.

## 12. The rest of step 4

- **All-buttons** is a different curve, not a steeper one, and the render shows
  it: at the same `input=25` that reads ~15 dB on 4:1, ALL reads ~18.
- **The value strings** at the three positions, all ASCII and all backwards the
  way `params.h` says (7 is fastest): position 1 is `1 (800 us)` / `1 (1100
  ms)`, position 4 `4 (126 us)` / `4 (235 ms)`, position 7 `7 (20 us)` / `7 (50
  ms)`.
- **Oversampling** renders at 2x and 4x; the latency table the measure tool
  prints is 0 / 40 / 60 samples at Off / 2x / 4x, zero at the default.

## 13. The DSP's own testing note does not exist

`testing-notes/fetcomp-dsp-2026-09-20.md` is referenced from **four** committed
or working documents -- `11-integration-and-test-plan.md:539`,
`docs/fet-comp/README.md:56`, `modules/fetcomp/AGENTS.md:17` and
`modules/fetcomp/README.md:69` -- and **is not in the tree**. By those
references it carries "every figure", "every CALIBRATE" value, and "the three
places the plan turned out to be unachievable as written".

The three departures are at least named in `modules/fetcomp/AGENTS.md`: the
release branch condition in `Detector.h::ReleaseStage::tick`, the plateau
envelope in `Calibration.h::kAllButtonsPlateauEnvelopeMs`, and the algebraic
rather than `tanh` curve in `Stages.h::SoftStage`. **The figures behind them are
lost with the session that measured them.** Nothing here reconstructs them, and
nothing should pretend to: they want re-measuring, and the calibration constants
want an ear.
