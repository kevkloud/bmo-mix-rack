# The UI pass — module 3, BMO Tune RT

**On AURORA, 2026-09-15 and 2026-09-16. Branch `ui-pass`, off `0d22530`.**
Module 1 (LTV Comp) is in `ui-pass-2026-09-14.md`, module 2 (BMO DEQ) in
`ui-pass-deq-2026-09-15.md`. Tools and the before-numbers are in
`ui-pass-render-loop.md`.

**Three checklist items closed by measurement with no edit. The fourth — the
empty middle third — went to a rebuild of the bottom section and of the
keyboard, both on Frosty's calls.**

| item | outcome |
|---|---|
| the key dot reads as the key | **closed** by measurement — 4.75:1, same in both appearances. §2 |
| lime on the pale plate, 1.29:1 | **confirmed to the figure**, Frosty kept it. Do not re-open. §3 |
| the disabled ♭ at 1.27:1 | **settled by DEQ's precedent**, not re-raised. §4 |
| the empty middle third | **rebuilt.** 110 px of bare plate → 48. §5 |
| the keyboard's black keys | **rebuilt** — they were never drawn. §6 |

| render | before | after |
|---|---|---|
| dark | `8076f67b0802f196` | **`079ad28197e70235`** |
| light | `10738b95c82711ef` | **`f7ec785d9e09db73`** |

Full `ctest` in Release on AURORA: **26 of 26**, `tune_hardtune_target`
disabled on purpose as always.

---

## 1. Baselines, and what the tests already cover

The before-hashes above are unchanged from `1c5299f`, which is worth
recording: BMO DEQ widened `ModuleContext` twice and grew `snapshot` a `rate=`
flag between then and now, and **Tune's render did not move.**

`bmo-tune-snapshot` takes no `signal=` and no `theme=` — see
`ui-pass-render-loop.md` §2. Tune has no meters, so the first costs nothing;
the second means a candidate colour here is a `Tokens.h` edit and a 4.3 s
rebuild rather than a JSON.

**Tune is not in `ui_layout_tests`** — that suite walks the rack's products.
It has its own, `tests/plugin/tune/PanelTests.cpp`, run as `tune_panel`. It
already asserts what a render would otherwise have to catch by eye: every
parameter has a visible control, every switch label fits, every value of Key /
Scale / Pitch Range fits its box, every Retune Speed value fits its readout,
nothing escapes the panel and no two controls overlap.

So this pass was only ever the part a test cannot see: colour, and the space.

## 2. The key dot — closed

`#4c5f27` on the lime key `#b6e35d`: **4.75:1, ΔL\* 47.2**, and **identical in
both appearances**, because the keyboard was lime on both plates. It is the
root marker by construction — `pitchClassOfKey (choiceOf (key))` places it.

§6 changed its ground, and it is measured again there.

## 3. The lime, on the pale plate — confirmed, not re-opened

`#b6e35d` on `#efefef` is **1.29:1** (ΔL\* 9.6), and **9.10:1** on `#2e2e32`.
Exactly the figure the checklist carries, and Frosty kept it with the figure
known (`add-bmo-tune`, `c5d6126`).

This figure is why §6's scale band is not a single colour — see there.

## 4. The disabled ♭ — settled by precedent, not raised again

At Init the key is C, and `TunePanel.cpp` reads

    sharpButton.setEnabled (hasSpelling ({ s.natural, +1 }));
    flatButton .setEnabled (hasSpelling ({ s.natural, -1 }));

C♯ is a key in the list and C♭ is not, so **♯ is enabled-but-off and ♭ is
disabled.** That is correct behaviour, and worth writing down because a panel
whose two stacked twins draw differently at rest is the shape of
`ui-editor-handoff.md` §6's "check the opening state specifically". Here the
check passes.

| | ink on fill | pale plate | dark plate |
|---|---|---|---|
| ♯, enabled and off | `#ffffff` on `#6f7076` | 4.94:1 | — |
| **♭, disabled** | `#dbdbdc` on `#c3c3c5` | **1.27:1** | 2.96:1 |

**Same fault BMO DEQ measured at 1.23:1 and Frosty settled on 2026-09-15: the
flat alpha stays.** Same mechanism, and Tune's figure is the better of the
two. Not put to him again. If the disabled state is ever re-opened it is one
decision for the suite, not one per module, and both modules now have their
number.

## 5. The bottom section, rebuilt

### What the measurements killed first

`gaps` gave 110 px under the rule, 11 px, and 81 px at the foot — 202 px of
bare plate in a 416 px section. The checklist offered "either the knobs come
up, or a note / pitch readout goes there". **The first is not available:**

    group ink   382..588,  centre 485
    section     272..688,  centre 480

The group was already centred within 5 px. Moving it relocates the hole.

**Growing the knobs is a weak lever, and it runs out.** Retune 96 → 116 with
the smalls at 52 bought 22 px of 202. At Retune 132 / smalls 60 the suite's own
test refuses it —

    FAIL: VIBRATO and RETUNE do not overlap
    FAIL: FLEX and RETUNE do not overlap

— because three knobs on a clock are bound by the panel's **360 px width**,
not its height. Sizing was never going to answer this; ordering did.

### Frosty's calls, 2026-09-16

1. **Retune at the top of the stack**, directly under the rule, with its
   millisecond value beneath it; Vibrato and Relax as a row of modifiers at
   the foot. Retune is what the plugin is for, so it reads first.
2. **The modifiers a size down** — face 48 against Retune's 112 — so they read
   as subordinate rather than as peers.
3. **FLEX reads RELAX**, and the automation lane follows the label.

Two earlier arrangements were rendered and rejected by him: a row of three
equal knobs with the number below (largest band 96 px), and the same stack
with Retune at the *foot* (35 px, but you scan past the modifiers to reach the
point).

    bare bands   110/11/81  ->  31/15/17/48/35     largest 110 -> 48

For scale across the suite: EQ 21, DEQ 30, **Tune 48**, Util 46, Opto 48,
Sat 66, Dim 72, LTV Comp 184. The 48 is the separator between the hero block
and the modifier row and is deliberately larger than the 35 at the foot.

### A defect found on the way, and it was in the shipped panel

Knob captions were placed at `track * 0.74f + 8.0f`, but the `−` and `+` are
drawn **on** the track at its widest. At face 96 that cleared by about 7 px; at
112 the marks land inside the caption's own width and RETUNE was struck
through. Now `track + 4.0f`, which clears the whole track at any face.

### The fit test had to follow the type

`kRetuneReadoutSize` is 15 → **38**, and it drives the drawn size *and*
`retuneReadout()`. Raising the drawn size alone would have left the test
measuring 15 pt and passing a readout that overflowed — the trap
`PlainKnob::captionOverflow` exists to avoid. It reports 104.9 px to spare.

### The rename

`Relax` is now the caption **and** the parameter's display name
(`modules/tune/params.h`). The id stays `flex`, which is what a saved session
references, so automation written against it keeps working — only the label
moves. `AGENTS.md`'s permanent list is IDs, order, ranges, steps and defaults;
display names are not on it, and no test pinned `"Flex"`. Checked before the
edit.

## 6. The keyboard — the black keys were never drawn

**The cause was one line.** `lit ? kAccent : base` painted *every* in-scale key
in the raw accent, black keys included, so `blackKey` was only ever reached by
notes **outside** the scale. At Init the scale is Chromatic, so every key was
in it — and the only black keys anyone had ever seen were faded ones. The
black key colour was not too light. It was never used.

Frosty's call, 2026-09-16: keep the piano's own colours and let the lime mark
the scale.

| | colour | against its ground |
|---|---|---|
| white key | `knobTint` `#ffffff`, both appearances | — |
| black key | `#1b1b1f` dark / `#464649` light | **17.17:1** / 9.41:1 against the white keys beside it |
| scale band, black key | `#b6e35d` raw | 11.56:1 dark, 6.33:1 light |
| scale band, white key | `#667f34` | **4.52:1** |
| root dot | as the band, on its own key | as above |

**The band is one colour expressed two ways.** `ui::accentTextOn (kAccent,
fill)` steps the accent off whichever key it lands on, because §3's figure is
exactly the problem: raw lime is 9.10:1 on a black key and **1.29:1 on a white
one**, so a single lime band would have been invisible on five keys in seven.

White keys are `knobTint`, which has no dark variant — the same decision
`meterFace` carries, and for the same reason: an instrument graphic looks like
itself in either appearance. Only the black key follows the plate, because it
has to stay darker than what surrounds it.

**The greying still exists and now means something.** Out-of-scale keys still
fade toward the plate, but that now happens only to notes genuinely
unavailable rather than being the permanent look of every black key. Rendered
and checked at Chromatic, C major and C minor; C minor is the case that shows
it both ways, with E, A and B grey while E♭, A♭ and B♭ stay black and keep
their bands.

**One number to carry forward: the black key is 1.27:1 against the dark
plate.** It reads because it sits among white keys, not because it stands off
the panel. If the keyboard ever loses its white surround, that figure is the
problem.

## 7. Still open

- **The rest-dot check does not apply to Tune.** All three knobs default to an
  end of their range — Vibrato 0 of 0–150, Relax 0 of 0–100, Retune step 0. No
  interior default exists to collide with anything.

  **RETUNE now draws no rest mark at all** (Frosty, 2026-09-16). Its dot sat
  on the minus, and the shared clearance test had been hiding it: that test is
  a *pixel* gap converted to an angle, so at face 96 it suppressed the dot and
  at 112 the wider track cleared it by a fraction and drew both, which read as
  a doubled minus. `Knob::setRestMark` is the opt-out, off for RETUNE only.
  Vibrato and Relax rest at an end too and are small enough that the shared
  test still hides theirs.

  The whole suite was re-rendered behind that change, since it is in
  `LookAndFeel.cpp`: **twelve of twelve byte-identical** across eq, sat, util,
  dim, deq and ltvcomp in both appearances. BMO Opto is excluded for the
  reason `ui-pass-deq-2026-09-15.md` §9 gives — its render is not reproducible
  run to run, so a matching hash from it is a coin flip rather than evidence.
- **35 px at the foot** is the largest band after the separator.
- **A detected-note readout** is now the host pass's question, not this one's.
  Frosty, 2026-09-16: decide it in Ableton, on whether it is *necessary*. It is
  written up with what to look for and what it would cost in
  **`tune-host-checklist.md` §1**, and `WORKFLOWS.md` stage 4 carries it.

  In short: `TuneCore` computes `note`, `pitchIn`, `target`, `appliedCents`,
  `ratio` and `lag` (`TuneCore.h:34`) and none of it reaches a panel, because
  `ModuleContext` has no field for it. This was the third module in the pass
  to want a context widening — `ui-pass-deq-2026-09-15.md` §7 item 9 calls
  that "worth one decision rather than three". **The rebuilt section no longer
  needs a readout to look right**, which is what makes it answerable on merit.

## 8. Housekeeping

- Rendered, measured and tested on **AURORA**. `snapshots/` is gitignored, so
  no render here travels with the branch — re-run the tool.
- **Next module is BMO Dimension**, 4th, per `ui-pass-render-loop.md` §0.
