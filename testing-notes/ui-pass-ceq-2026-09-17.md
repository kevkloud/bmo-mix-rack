# The UI pass — BMO CEQ, module 8 and the last

**On AURORA, 2026-09-17.** Branch `ceq-ui-pass`, off `ui-pass` at `4f8fa2e`
after the Saturator, worktree `../bmo-mix-rack-333-ui`. Local only, nothing
pushed and no CI. Frosty drove it from his phone over Remote Control; every
call below was made on renders in both appearances.

Read `ui-pass-render-loop.md` for the tools.

---

## 1. Where it ended

| | dark | light |
|---|---|---|
| before | `ac0d5c764049b912` | `b57148059978ac2c` |
| after the rename (header text only) | `bcc12f2ac9ede28c` | `2868aebe2574da5a` |
| **after the panel work** | **`cc76b8c7987bb940`** | **`1ad53a0f0035f763`** |

Largest bare band, dark: **21 px → 17**. `signal=` moves nothing on this
module and never has — no meters — so every figure here is a bare render.

The other seven panels hash exactly as their own session notes recorded them
(Saturator `0a94a1b7` / `4777584d`, Util `ce603457` / `ee71528c`, Dimension
`48929910` / `d731d158`, Opto bare `910e41b6` / `2394065f`, DEQ `0f95f147`).
That is the check that mattered, because this pass touched `core/ui` twice.
The rack moves, as it must: it has CEQ in it.

Tests: `eq`, `eq_dsp`, `ui_layout`, `rack`, `deq`, `sat`, `util`, `opto`,
`dim`, `vcomp`.

## 2. The rename, and the preset chain under it

**BMO EQ is BMO CEQ** — Frosty chose it 2026-09-11 and it landed here.
`Fsty`, the bundle id `com.lt3audio.frostyeq`, the module id `eq` and the
whole schema stay: that is what makes old sessions open. The bundle becomes
`BMO CEQ.vst3`, so the packager README gained a "BMO EQ users" paragraph
beside the FrostyEQ one.

The real work was underneath. `PresetInfo` carried **one** legacy folder and
this product has now been renamed twice, so it carries a list:

- **Newest first** — `BMO EQ`, then `FrostyEQ`. The 0.2.4 review and the
  UI-pass handoff both said *oldest* first, which is the same rule stated
  backwards: with "never overwrite", oldest first hands a name that exists in
  both old folders to the **FrostyEQ** copy, from before the user's later
  edits, and drops the BMO EQ one. `EqTests` pins the direction; reversing the
  order in `products/eq/Product.h` fails it.
- **Extension `.bmoceq`**, Frosty's call. `WORKFLOWS.md:358` had said both
  extensions stay; that line is now wrong and is fixed.
- **One-shot, marked.** The old "only if the new folder is empty" gate is gone
  — it stranded anyone who saved a single preset before the copy ran — and a
  `.migrated` marker replaces it, so a preset the user **deletes stays
  deleted**. Frosty's call, on the alternative of re-copying every launch.
- **It is tested now, which it never was.** `migrateLegacy` used to return
  early whenever the test sandbox was in use, so the one thing that has to work
  on a stranger's machine was the one thing the suite never ran. Old folders
  resolve through the same helper as the current one, and `EqTests` walks both
  hops, the same name in both folders, the marker, a deleted preset staying
  deleted, and a user's own file not being overwritten.

The same hole is still open for LTV Comp's single hop — `VcompTests.cpp` says
so — and could now be closed the same way.

## 3. What the panel gained

**Oversampling has a control** (`OVERSAMPLING` on a rule, then 2x / 4x / 8x).
It had been on the schema since 0.2.0 and nowhere on the panel, reachable only
by host automation. Same shape the Saturator took a day earlier, and the same
radio-over-one-choice-parameter arrangement: a click sets the parameter and the
parameter lights the switches, so a click and host automation cannot disagree.
Clicking the lit one is the way back to Off.

**This module defaults to 2x, not Off**, so one switch is lit at Init — the
first time the panel has ever said that CEQ starts with 40 samples of latency.

**AUTO has one too**, in the switch row.

**HI-Q went back to the mid bell**, where the control it affects is. It had
been on the output switch row since 0.2.3 because the common height left no
*row* for it beside the band — but it never needed a row, only the margin,
which a band leaves 65 px of. AUTO took the place it left, so the row is still
three switches at the suite's own width rather than four narrowed to 58.

**Mix does not get a control and will not.** Frosty, on sight of one: it is not
something this module should offer. The parameter stays in the schema — removing
it would shift Auto Gain and Oversampling and break every saved session that
names them — so it is host-only and that is now deliberate rather than an
oversight. High Cut stays host-only too.

## 4. What the bands paid, and the shapes that were turned down

The section costs 50 px and this panel had **9 px spare** where the Saturator's
had 55, so it came out of the bands: **112 px rows → 95**, and a band's dial
from **59.5 design px to 50.5**.

| candidate | | verdict |
|---|---|---|
| the section unnamed, stacked over the switch row | costs 20 px less | passed over — this is the one panel whose rules carry legends, so an unnamed row is the odd thing here |
| shorter rows, 85 and 78 | | dead end: the freed height only inflates the LO-CUT dial, which nothing wants |
| frequencies in a column beside the dial, dial enlarged | dial **68.5**, bigger than it has ever been | **not what Frosty meant** — and it gives up the alignment, since a label no longer sits on the ray its position points down |
| section names beside the dials, the two rules between the bands dropped | dial **56.5**, within 3 px of today's | passed over: Frosty kept the straight stack |
| MID slid left so the dial and HI-Q centre as a pair | | passed over as unbalanced |

## 5. HI-Q is square, and why that needed a change in the look and feel

A switch's label is set at **62% of its own height**. That is right for a row of
70 x 26 switches and wrong for a square one: the text grows with the height
faster than the width grows, so **"HI-Q" overflows a square switch at every
size** — 11.3 px over at 40, 16.0 at 54. `checkSwitchLabelsFit` caught both.

So `SwitchButton::setLabelSize` pins the size, and HI-Q keeps the one the
switch row uses. Opt-in: every other switch keeps the derived size and nothing
else in the suite moves.

The square is **40 px, 30 px in from the margin** — Frosty, from a ladder of
40/46/54 at two insets. The 30 is measured, not derived: a band's ink is not
symmetric about its centre, because the frequencies occupy the left half of the
ring and the gain the right, so the widget's own half-width is 58 px on the
left while the ink reaches only 30 on the right. The dial's ink ends at design
x 170 and the content edge is 270, so a 40 px switch centred in what is left
sits 30 px from each. **Re-measure if the band row ever changes.**

`ConcentricBand::setDialOffset` exists for the same reason: the mid band's cell
stops short of the switch so neither takes the other's clicks, and the dial is
pushed back onto the panel's centre line with INPUT, LO-CUT and OUTPUT.

## 6. Open

- **Nothing heard.** This was a UI pass; the oversampling default (2x, 40
  samples) is a product question the Saturator's note also raises.
- **The rest of `ceq-testing-checklist.md` stands**, including the Phase-on-wet
  bug at Mix 50%, which now matters less because no one can reach Mix from the
  panel.
- **The legends** EQL, LO-CUT, HI-Q: still not looked at, and still lowest
  priority.
- **Light-plate captions** at 2.00:1 — the suite-wide raw-legend question, not
  this module's.

## 7. Next

The pass has no module 9. What is left is the suite-wide list in
`ui-pass-checklist.md` §A — contrast assertions first, as the cheapest — and
then the look-and-sound round before any of this goes to Kevin.
