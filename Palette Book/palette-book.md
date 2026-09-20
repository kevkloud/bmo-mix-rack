# BMO Palette Book

Every colour the suite draws with, measured on the ground it is actually
drawn on — and the direction chosen off the back of it.

> **This is evidence, not a registry. Accents are allocated in
> `products/AGENTS.md`.**
>
> Two things below are known stale and are left in place because the
> measurements around them are still good. `#d4a4ff` is listed against BMO
> Opto: Opto gave that colour up in 0.2.2 when its panel went greyscale, and
> BMO Dimension has it now. And the 7:1 figures were taken on a `#202024`
> plate, where `darkTokens()` ships `#2e2e32` — on the plate that actually
> ships nothing clears 7:1, though everything clears 4.5:1 comfortably. A
> ratio without its plate named is not an absolute, which is the house rule
> this document is otherwise built on.
>
> Reading this file as an allocation list is what nearly cost BMO Dimension
> the lavender. Check `products/AGENTS.md` for what is spoken for.

Measured 2026-09-08 on branch `ui-editor`, from the literal values in
`core/ui/Tokens.h`, `modules/*/Module.cpp` and
`modules/opto/panel/OptoPanel.cpp`. Panel observations are read from 2×
`tools/snapshot` renders of all four modules and the rack chain
`util,eq,sat,opto` — not from the source.

Contrast is WCAG 2.x relative luminance, sRGB. The house rule from
`docs/ui-workflow-brief.md` applies throughout: **assert absolutes, never
comparisons.** "Better than it was" is the trap a relative release test fell
into for a whole release.

---

## 1. The finding

**The suite has no dark end.** Ink on the faceplate runs from 1.15:1 to
4.37:1, and everything a person actually reads sits in the bottom half of
that. The only dark value in all four modules is the Opto meter face
`#3a3a3a`, added last release to fix exactly this problem, and never carried
back out.

**Fifteen of sixteen ink/ground pairs fail. The one that passes is the one
that was measured.**

### Every ink, on its real ground, worst first

| Ink | Ground | Where it is used | Ratio |
|---|---|---|---|
| `#ffffff` pointer | `#ead2ff` Opto cap | knob pointer | **1.39:1** |
| `#e0b040` meterHigh | `#d6d6d6` well | output meter bar | **1.38:1** |
| `#ffffff` pointer | `#f8c6da` EQ cap | knob pointer | **1.49:1** |
| `#7fd0f2` trackFill | `#efefef` plate | selected preset, popup highlight | **1.50:1** |
| `#7fc98a` Util accent | `#efefef` plate | section legend, 11 pt | **1.72:1** |
| `#d4a4ff` Opto accent | `#efefef` plate | raw accent as ink | **1.73:1** |
| `#b4b4b4` hairline | `#efefef` plate | every section rule | **1.80:1** |
| `#efa552` Sat accent | `#efefef` plate | section legend, 11 pt | **1.80:1** |
| `#4fb8e8` track | `#efefef` plate | **every knob caption**, 15 pt | **1.95:1** |
| `#f08cb4` EQ accent | `#efefef` plate | section legend, 11 pt | **2.00:1** |
| `#4cacdc` switchAlt | `#efefef` plate | selected frequency legend | **2.22:1** |
| `#ffffff` | `#a6a6a6` switchOff | text on a disengaged switch | **2.43:1** |
| `#9a9a9a` text2 | `#efefef` plate | unselected frequency legend | **2.45:1** |
| `#9c71c3` | `#efefef` plate | Opto captions, hardcoded in the panel | 3.28:1 |
| `#6f6f6f` text1 | `#efefef` plate | header and preset bar only | 4.37:1 |
| `#ffffff` | `#3a3a3a` meter face | VU needle and scale | **11.37:1** |

Note the two states of a switch: white on the engaged fill measures
1.98–2.55:1 depending on module, and white on the disengaged grey measures
2.43:1. The label is equally hard to read either way, so **on and off are
separated by hue alone** — which fails for a colourblind user and fails in a
screenshot.

### The structural greys are five shades of one grey

| Token | Hex | vs plate |
|---|---|---|
| `plate` | `#efefef` | — |
| `plateEdge` | `#e4e4e4` | 1.07:1 |
| `well` | `#d6d6d6` | 1.30:1 |
| `hairline` | `#b4b4b4` | 1.80:1 |
| `outline` | `#9e9e9e` | 2.33:1 |

`plate` to `well` spans `0x19`. A recess at 1.30:1 against its own surround
is not a recess.

---

## 2. What the rack render shows that the source did not

Three of these are new; the rest confirm what the arithmetic predicted.

1. **Knob captions are azure on every module except Opto.** INPUT, DRIVE,
   TONE, MIX, GAIN, PAN, WIDTH and OUTPUT are all `#4fb8e8`, sitting directly
   under orange, green and pink knobs. Side by side it reads as a mistake
   rather than a system. **Opto's purple captions are the only ones that
   belong to their module.**

2. **The section rules do not line up across modules.** `AGENTS.md` states
   the fixed 688 px content height exists so "a rack of them reads as one
   surface with the section rules lining up across modules." In the render,
   Util's first rule, EQ's and Sat's all sit at different heights. The stated
   goal is not delivered by the implementation.

3. **Opto is ahead of the others, not behind them.** Its captions match its
   module, its switches are the shared 70×26, its meter is the only readable
   element in the suite. What drifted is the *code* — a hardcoded hex in a
   panel file, a bespoke meter class, no section rules. The look is the model
   to copy.

4. **The EQ MID legend collides with the rest-position dot** — `1k6 · 3k2`,
   with the pink track dot landing between the two labels.

5. **The Opto VU scale overlapped and the face was ~45 % empty.** Fixed; see
   section 5.

6. **Switch widths are 56 / 62 / 70 / 70 px** across four adjacent modules.

7. **Panels butt plate-to-plate with no divider.** Only the slot bar carries
   a 1 px edge. "Reads as one instrument" has overshot into "cannot tell
   where a module ends."

8. **The dBFS meters are placed three different ways** — Util centred, EQ and
   Sat right of OUTPUT, Opto none — and the 9 pt `dBFS` label at 2.45:1 is
   the only thing saying the meter is clickable.

---

## 3. One token is doing five jobs

`pointer` is `#ffffff`, and it is used for the knob pointer, the selector-ring
annulus, the text on an engaged switch, the VU needle, and the scale ticks.
Those five want different values the moment the plate stops being pale, which
is why no single edit fixes the pointer today, and why the theme switch cannot
land before the split.

| Current use | Ratio | Should become |
|---|---|---|
| knob pointer on a pale cap | 1.39:1 | `pointer` — dark in light, light in dark |
| text on an engaged switch | 1.98:1 | `onAccent` — black or white, whichever reads |
| selector-ring annulus | 1.15:1 | `ringFace` |
| VU needle and ticks | 11.37:1 | `meterInk` — already correct, keep |

Alongside it, `accent` stays a *fill* and gains a derived `accentText` for
ink. `core/AGENTS.md` says "Tokens are the only place colours live"; Opto had
to break that rule because the token it needed did not exist, and hardcoded
`#9c71c3` in `OptoPanel.cpp`. Deriving `accentText` and `onAccent` from the
accent rather than typing them alongside it means module six never hand-rolls
a hex.

---

## 4. The direction: Option A, "Repair"

Chosen 2026-09-08. Fix the ink, keep every surface. Pale caps stay exactly as
they are; the pointer flips to dark and captions and legends move to a derived
per-module `accentText`.

| | now | Option A |
|---|---|---|
| Knob pointer | 1.39:1 | **10.19:1** — dark on the same cap |
| Knob caption | 1.95:1 | **4.5:1** — and it matches the module |
| Section legend | 1.72:1 | **4.5:1** — same derived colour |
| Text on switchOff | 2.43:1 | **4.94:1** — one grey darkened |
| Surfaces | — | unchanged |

### Derived `accentText`, per module — as shipped

These are the values `ui::accentTextOn` actually produces, read out of a
running panel rather than predicted. They differ by a step or two from the
first pass of this document, which walked the accent's RGB down by 1 % at a
time; the implementation interpolates toward black in 2 % steps instead. Same
intent, same floor, slightly different landing point.

| Module | Accent | `accentText` | on plate |
|---|---|---|---|
| BMO EQ | `#f08cb4` | `#975871` | 4.62:1 |
| BMO Saturator | `#efa552` | `#8f6331` | 4.57:1 |
| BMO Util | `#7fc98a` | `#497450` | 4.69:1 |
| BMO Opto | `#d4a4ff` | `#7b5f94` | 4.67:1 |
| *utility knobs* | `#4fb8e8` `track` | `#317290` | 4.64:1 |

`#7b5f94` supersedes the hardcoded `#9c71c3` in `OptoPanel.cpp`.

The last row is a distinction the first pass of this document missed. A
caption follows **its knob's own colour system**, not the module accent
regardless: character knobs — drive, tone, a band's gain — are drawn in the
module's colour and their captions go with them, but a utility knob is
deliberately the same pale blue in every module, so INPUT and OUTPUT read as
the same control wherever they appear. Setting those in the accent put pink
text on BMO EQ's blue cap and orange on the Saturator's — the same mistake
this section exists to fix, only inverted.

### Option B, "Re-value", declined for now

Saturating the caps back toward the true accent (EQ `#c57394`, Sat `#c48743`,
Util `#68a571`, Opto `#ae86d1`) gives the knob more presence — cap-on-plate
goes from 1.24:1 to 2.65:1. But the white pointer on a saturated cap only
reaches 3.05:1, so B needs the dark pointer anyway. That makes the cap change
taste rather than legibility, and it belongs in its own decision rather than
riding along with this one.

---

## 5. Dark mode

**The accents were always dark-mode colours.** On a `#202024` plate all four
raw accents clear 7:1 with no adjustment at all:

| Module | Accent | on `#efefef` | on `#202024` |
|---|---|---|---|
| BMO EQ | `#f08cb4` | 2.00:1 | **7.05:1** |
| BMO Saturator | `#efa552` | 1.80:1 | **7.87:1** |
| BMO Util | `#7fc98a` | 1.72:1 | **8.21:1** |
| BMO Opto | `#d4a4ff` | 1.73:1 | **8.16:1** |

The palette was designed for a dark plate and has been shipping on a pale one.

### The dark set, tuned

The first pass spaced these by contrast ratio and they came out flat. **WCAG
ratio is the wrong tool below about L\* 20**: from `plate #202024`, the
maximum contrast obtainable by going *darker* — all the way to pure black — is
**1.29:1**. The +0.05 term in the ratio formula dominates down there, so every
dark grey measures "the same" as every other.

Perceptual lightness separates them properly. Spacing the dark set by CIE L\*
against the light set's own intervals:

| Token | Hex | L\* | step from plate | light set's step |
|---|---|---|---|---|
| `well` | `#1b1b1f` | 9.9 | −9.2 | −8.8 |
| `plate` | `#2e2e32` | 19.1 | — | — |
| `plateEdge` | `#37373b` | 23.2 | +4.1 | −3.8 |
| `hairline` | `#5e5e62` | 40.0 | +20.9 | −21.1 |
| `outline` | `#727276` | 48.2 | +29.1 | −29.3 |

`plateEdge`, `hairline` and `outline` invert direction — lighter than the
plate rather than darker — which is what a dark surface has to do to read as
raised. The intervals are the same size.

`text1 #e6e6ea` reads 10.86:1 on the tuned plate, `text2 #9a9aa4` 4.85:1.

### The meter face

`meterFace` was `#3a3a3a`, hardcoded in `OptoPanel.cpp`, and is now a token at
**`#464649`**. White numbers were never what limited it — they read 9.41:1
here and would survive two steps lighter again. The limit is the **hot zone**:
the amber marking 0 VU and above is 4.68:1 on this face and 4.51:1 one step
lighter, and the red washes visibly toward pink as the face comes up. The
meter is as light as its own warning colour allows, not as light as its
numbers allow.

Being a token is what lets the dark theme leave it *above* the plate — a
window that reads as lit rather than as a hole punched in the panel. It was
the last raw hex in the suite.

### How it is built — shipped

**Dark mode is not a second design. It is a second binding of one token set.**
`darkTokens()` in `core/ui/Tokens.cpp` is the whole palette; every panel,
control and derivation was already reading tokens, so nothing else changed.

The preference is **machine-wide**, in `LT3 Audio/UI.json` beside the themes —
not a plugin parameter. A parameter would touch `specs()`, which `AGENTS.md`
freezes as permanent and append-only and which every golden schema test pins,
it would be automatable, and it would save a *look* into every session. The
existing 1 Hz poll in `ProductEditor` and `RackEditor` propagates a change to
every open editor — standalone and in a rack, this plugin and the one in the
next track — within a second, without any of them holding a reference to the
others.

The choice is in the preset dropdown rather than on a button of its own:
there is no room on a 160 px panel for a control used twice a year, and that
menu is already where everything about the plugin rather than about the sound
has ended up.

A theme file still works and is now an **overlay** on whichever base is
chosen, so picking dark and then hand-editing two colours does what it reads.

### Knobs: the cap and the caption trade places

A module's colour appears twice on every knob — on the cap and in the caption
under it — and one of them is the full accent while the other is a wash of it.
**Which is which flips between the appearances.**

| | knob cap | caption / legend |
|---|---|---|
| light plate | wash, `faceOf` | accent stepped down, `accentInk` |
| dark plate | **accent at full strength** | **wash** |

Measured on the dark plate: caps 5.87–6.84:1 with the pointer at 6.13–7.14:1
on top of them, captions 9.07–9.73:1 — against the 5.87–6.84:1 the raw accent
managed as text. **Both directions come out better than they went in**, which
is why this is a swap rather than a compromise. `knobFace` swaps with them, so
a utility knob is the raw track blue on dark and a wash of it on light.

Three shapes were built before this one and the first two were rejected on
sight, not on numbers:

| tried | why it went |
|---|---|
| mix the accent toward the dark plate | the Saturator's orange landed on a **brown**; pink and green survived, orange did not |
| the accent with saturation ×1.35 | measured fine — every module held ≥4.5:1 cap-to-pointer, EQ tightest at 4.86 — but the caps shouted |
| pale caps in both, pointer inverted | correct and dull; the module's colour never got to be the loud thing on a dark panel |

The **pointer** inverts with them either way: white on the pale plate at
1.39–1.49:1, near-black on the dark one. Verified off the renders — 172
near-black pointer pixels and no white ones in dark, exactly the reverse in
light, and light mode came through the swap with **zero** changed pixels.

`faceOf()` mixed toward a literal `Colours::white` until 0.2.2, which made it
the one part of the palette a theme could not reach. That literal is `knobTint`
now.

---

## 6. Done on this branch

- **VU meter geometry** (`c4f4440`). The radius came from
  `jmin(width/2, height)`, but a 124° sweep is limited by width alone, so on
  any face taller than half its width the arc was sized to the wrong
  dimension — 0.2.1 shipped with roughly 40 % of the meter empty above the
  needle. The radius now comes from the width and the drawn block is centred
  in whatever face it is given. The scale opened at −20, which is also where
  the needle rests in silence, so an idle meter left the needle lying across
  its own leftmost numeral; it now opens on an unprinted point at −30. −7 and
  −3 lost their numbers, and an unnumbered tick at −15 fills the low-end
  stretch.

- **The token split and Option A's ink** (`3d743d9`). `pointer` was one token
  doing five jobs; it is now `pointer` (dark, on a knob cap), `ringFace` and
  `meterInk`, with the other two rewritten as derivations — `accentTextOn`
  and `onAccentOf`. Measured on the result:

  | | was | now |
  |---|---|---|
  | knob captions | 1.95:1 | 4.57–4.69:1 |
  | section rule legends | 1.72:1 | 4.57–4.69:1 |
  | knob pointer | 1.39:1 | ~9.5:1 |
  | selected band legend | 2.22:1 | 4.62:1 |
  | band selector marker | 1.49:1 | derived against the ring |
  | text on a switch | 1.98:1 | derived from its fill |
  | text on `switchOff` | 2.43:1 | 4.94:1 |

  BMO Opto no longer names a colour anywhere and follows a theme change with
  the rest of the suite.

- **Opto's panel rhythm** (`0e633ac`). Three equal thirds with a block centred
  in each spent 132 px of slack as uneven centring. Blocks are now placed from
  the top on one derived gap. Measured off the render: 30 px between blocks
  and 32 px under COLOR, where they were 36 / 46 / 28 / 0 — COLOR had been
  sitting on the panel's bottom edge. The meter and its IN/GR/OUT row are one
  block, 9 px apart rather than 26.

All nine ctest suites pass at each of the three. No parameter, spec, preset
or DSP file has been touched on this branch.

## 7. Still open

- **Opto's two remaining optical bands** — 58 px under TELE, 54 px under the
  meter buttons. Even by construction, but a `PlainKnob` block is 150 px tall
  around about 105 px of ink: `faceScale` 0.62 draws a 57 px circle inside a
  92 px component. Closing them means deciding how large COMP and MAKEUP
  should be beside the other three modules, which is weight, not spacing.
- **Dark mode.** The token split is the prerequisite and it has landed, so
  what is left is the second binding, the moon icon, and a machine-wide
  preference file beside the theme JSON.
- **Rule alignment across modules.** Some should, some should not; needs a
  module-by-module pass before the row grid moves.
- **The reusable VU meter base**, with swappable colour and its own ruleset,
  so a dynamics module six does not inherit a bespoke class. Cheaper now:
  `DynamicsMeter` takes three callbacks and three colours and no longer holds
  a hardcoded hex.
- **EQ legend crowding** — the MID rest-dot collision and the five-legend
  low-cut selector on a 13 px face radius.
- **Switch geometry**, 56 / 62 / 70 / 70 across four adjacent modules.
- **Rack module separation**, and the three placements of the dBFS meter.
- **Meter modes cannot be snapshotted.** IN and GR are UI state rather than
  parameters, so `tools/snapshot` can only ever render OUT. The VU fixes above
  were verified in OUT only. `docs/ui-workflow-brief.md` §2 is the fix.

---

*LT3a · BMO Mix Rack · branch `ui-editor` · 2026-09-08*
*Interactive version with rendered swatches:
Frosty has the link.*
