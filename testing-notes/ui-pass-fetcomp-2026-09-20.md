# BMO FET — first visual pass

**On AURORA, 2026-09-20**, on `frosty-add-bmo-fetcomp`, from the `build/` tree
(Visual Studio 17 2022, **Debug**). Renders are `tools/snapshot`, figures are
`tools/inspect/Inspect.exe` on the pixels. `snapshots/` is gitignored —
regenerate rather than look for these files.

`Inspect.exe` had to be rebuilt first: the binary in the tree predated its own
`gaps` mode. `csc -out:Inspect.exe -r:System.Drawing.dll Inspect.cs`.

**The module's DSP is still the placeholder**, so two things in
`docs/1176-comp/11-integration-and-test-plan.md` §4d could not be done and are
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
is **0.7**.

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
