# BMO FET visual pass: the voicing colours the panel

**AURORA, 2026-09-21.** Step 6 of `docs/fet-comp/HANDOFF-render-pass.md` —
final hashes and every measured figure. Continues
`testing-notes/ui-pass-fetcomp-2026-09-20.md`, which covers the skeleton and
the bezel-alpha call; this one covers the pass run after the DSP landed.

Rendered with `build-ui/tools/Debug/snapshot.exe` (Debug), measured with
`tools/inspect/Inspect.exe`. Branch `frosty-add-bmo-fetcomp`, PR #22, at
`5c9f243`. **Nothing here has been heard.**

---

## The decisions this pass settled

All three of the handoff's step 2 deviations, on renders, owner's call.

1. **The voicing colours the controls, not just the bezel.** Blue takes the
   accent; Black takes the plate's opposite. The parameter defaults to Black,
   so the default panel is monochrome and turns blue when Blue is selected.
2. **The accent takes a dark-plate variant.** `#5489d4` on the pale plate where
   it was chosen, `#8fb4e6` on the dark one.
3. **MIX ships neutral grey**, `#b0b0b0` dark / `#585858` pale, in both
   voicings.
4. **The switches keep the accent**, not `switchAlt` — rejected on the render.

The module keeps `#5489d4` as its **suite identity** regardless: the rack tab
and header stripe read `ModuleDef::accent` and are not the panel's control
colour. Verified in the rack renders below.

---

## 1. Why the accent needed a dark variant

`#5489d4` was chosen against `#efefef` and measured there. On the dark plate it
misses both of `faceOf`'s documented bands:

| | shipped `#5489d4` | lifted `#8fb4e6` | suite band |
|---|---|---|---|
| cap on plate | 3.80:1 | **6.34:1** | 5.87–6.84 |
| pointer on cap | 3.97:1 | **6.61:1** | 6.13–7.14 |
| caption on plate | 3.80:1 | **6.34:1** | — |

The light-plate cap reads 1.53:1 shipped and 1.25:1 lifted, which looks alarming
until the controls are measured: **Saturator 1.24:1, Opto 1.28:1**. On the pale
plate every module's cap is deliberately low-contrast — `faceOf` mixes it half
way to white and the ink carries legibility — so the band is a dark-plate band,
and the lifted value brings BMO FET *into* line where shipped was the outlier.

This is the mechanism the suite already has, pointing the other way.
`accentTextOn` darkens the four suite accents hard for the pale plate because
they clear 7:1 on the dark one raw (`Tokens.cpp`: *"The accents need no dark
variant at all"*). BMO FET's blue is the opposite case.

**Everything the module tints moves together.** Lifting only the knobs would
have left the bracket, meter and switches at `#5489d4` — two blues 1.58:1 apart
on one panel, which is the same clash that ruled out `switchAlt`.

---

## 2. MIX: three treatments rendered, one shipped

MIX wore the suite's utility azure `#4fb8e8`. That cleared the old accent by
1.58:1 and the lifted one by **1.05:1** — the same lightness, separated by hue
alone — so it stopped reading as a different kind of control.

| treatment | dark | pale | verdict |
|---|---|---|---|
| azure `#4fb8e8` | 1.05:1 from the accent | — | **rejected**, no separation |
| white / black | 13.52:1 / 18.26:1 | | **rejected**, see below |
| grey `#b0b0b0` / `#585858` | **6.23:1** | **6.19:1** | **shipped** |

White and black were rendered and rejected on the render: they put the quietest
control at over twice the character knobs' 6.34:1, making the set-and-forget
knob the loudest thing on the panel — against the panel's own stated intent
that the trim treatment is "what keeps it from reading as a fifth headline
control". The greys sit level with the character knobs.

The pointer needed nothing. `pointer` is `#2b2b2e` on the dark plate and white
on the pale one, exactly backwards from the cap here: 6.51:1 and 7.11:1.

**MIX is now the only knob in the suite not wearing the azure.** Deliberate,
and written down at the call site so it is not "fixed" back.

---

## 3. Switches: the alternative was rendered and is worse

`11 §4c` expects `switchAlt`. BMO FET lights its active switches in the accent
instead.

- `#5489d4` against `switchAlt` `#4fb8e8`: **1.58:1**, 16.4° of hue.
- Label legibility is a wash: 4.56:1 shipped, 4.51:1 on the alternative.

The render shows the cost: a cyan switch beside an accent-blue bracket, two
blues almost-but-not-quite the same, which reads as a mistake rather than a
distinction. **Deviation recorded as deliberate.**

---

## 4. Final hashes

Every render 520×1480 unless stated. Each line is the exact command's result —
a hash is worthless without the condition that produced it.

**Panel, at rest** (`snapshot fetcomp <out> appearance=<a> voicing=<v>`):

| appearance | voicing | hash |
|---|---|---|
| dark | Blue | `16b70c7b1fab5683` |
| dark | Black | `bb78274962b2cc15` |
| light | Blue | `c4beb3c439e93f86` |
| light | Black | `b2b675cbedd576f1` |

**Meter modes** (dark, Black, `ui.meter=<m> signal=-18 input=10 ratio=20:1`):

| mode | hash |
|---|---|
| IN | `3654e725a2fc69bf` |
| GR | `7a5bd3f2eda2cba6` |
| OUT | `61cf00eb70b3959c` |

**Knob positions** (dark, Black, at rest, `attack=N release=N`):

| position | hash |
|---|---|
| 1 | `62664f9ceb806d82` |
| 4 | `bb78274962b2cc15` (= the at-rest default) |
| 7 | `81b25eb07968c34d` |

**Other states** (dark, Black, at rest):

| state | hash |
|---|---|
| oversampling Off | `bb78274962b2cc15` |
| oversampling 2x | `e4fdeab1da643d95` |
| oversampling 4x | `d00799a5a2ddcd7f` |
| all-buttons ratio | `b2ea0ef11331b6a8` |

**Rack context** (`snapshot rack <out> chain=eq,fetcomp,sat`), 1680×1480:

| appearance | hash |
|---|---|
| dark | `f67521438b7c0674` |
| light | `f0ce9cb8577e2aeb` |

### The 2026-09-20 hashes are superseded, not broken

`HANDOFF-render-pass.md` records `65a43d6afd63e58b` dark, `cdb63925bd4911bd`
light, `7c5c9d79601dfb6d` IN and `eb631cc29987997e` OUT. **Those do not
reproduce and should not be expected to.** Two things changed legitimately since:
the DSP landed, so the GR needle moves where it was pinned at rest against a
placeholder; and the control colour changed with this pass. Treat them as
history.

Separately, and worth knowing when reading any hash recorded before
2026-09-21: `tools/snapshot` appended instead of truncating, so re-rendering
over an existing path left the *previous* image in place while reporting
success. Fixed in `c1607d7`. Any hash taken from a path rendered more than once
before that commit is suspect. The hashes above were all taken on the fixed
tool.

### Protected renders re-proved unchanged

- BMO Opto `ab3ff3b77116b7a5` dark, `878cca7b1a80a551` light,
  `88a7653a82c19ae0` GR dark — all at `signal=-18`.
- BMO Saturator `d42e23747e1ea2fc`.

`core/ui` was not touched by this pass; everything is inside
`modules/fetcomp/panel/`.

---

## 5. Contrast, every box

`11 §3` asks for every box because a caption lift only holds for the box it was
measured on. Measured at `signal=-18 input=10 ratio=20:1`, Black voicing.

| box | dark | light |
|---|---|---|
| INPUT caption | 6.34:1 | 3.09:1 |
| OUTPUT caption | 6.34:1 | 3.09:1 |
| ATTACK caption | 6.34:1 | 3.09:1 |
| RELEASE caption | 6.34:1 | 3.09:1 |
| MIX caption | 13.52:1 | 18.26:1 |
| active switch label | 4.63:1 | 4.56:1 |
| inactive switch label | 4.94:1 | 4.94:1 |
| meter ink on face | 9.41:1 | — |

The light caption figure of 3.09:1 is the accent exception the README already
records; it is unchanged by this pass, which only lifted the dark plate.

**Gaps.** Largest bare band **47 px at design-y 403..450**, identical in both
appearances and **identical to the pre-change render** — this pass moved no
pixels, only colours. `ui_layout_tests` passes.

Note the handoff records "largest bare band 29 px at 118..147". That figure is
stale: it was measured on the skeleton before the ratio-bus rework (`c0fc24a`),
not changed by anything here.

---

## 6. Two consequences, both visible and neither a defect

**Black voicing on the pale plate gives grey knob faces, not black ones.**
`faceOf` mixes every cap half way to white on that plate, so Black's faces come
out `#7f7f7f`. Captions, switches, dotted tracks and the bracket are all
genuinely black. Making the faces literally black means bypassing `faceOf`,
which is `core/ui` and would put Opto's and Saturator's hashes in play. Not
done.

**In Black voicing, MIX separates from the controls only weakly:** 1.78:1 on
the pale plate and 2.17:1 on the dark one, against 3.98:1 and 1.02:1 in Blue.
A neutral has nothing to be neutral against on a greyscale panel, so there MIX
is told apart by size and position rather than by colour. Accepted on the
render.

---

## 7. What this pass did not do

- **Nothing has been heard.** Every colour decision here was taken on renders
  and measurements. The listening pass is `packages/fetcomp-listening/` and the
  Ableton pass.
- The **neutral-caption treatment** `11 §4c` suggested was not built: the
  caption colour comes from `panelAccentFor` in `core/ui` with no panel-side
  lever, so it cannot be done inside `modules/fetcomp/panel/`. It is also no
  longer needed on contrast grounds — the dark caption went 3.80:1 to 6.34:1 as
  a consequence of the accent lift.
- **PR #23** (`frosty-ableton-pointer-contrast`) may overlap the knob-contrast
  question this pass settled. Not checked; worth a look before either lands.
