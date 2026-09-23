# BMO Dwell — the visual pass (stage 3)

**2026-09-23, on AURORA.** Worktree `../bmo-mix-rack-333-dwell`, branch
`frosty-add-bmo-dwell` at `414897c`, clean before and after. Release build tree
`build-ui/`, every target named on the command line — `snapshot` first, then the
twenty-two test executables. No plugin or rack target was built, so the
installed 0.2.5 set on this machine is untouched.

Nothing in the panel was changed. This note is what was looked at, what was
measured, and what I would put in front of Frosty before he opens it in a host.

## What was rendered, and where it is

`snapshots/dwell-visual/` — gitignored (`.gitignore:26`), so none of it is
stageable. Twenty-three PNGs, plus the thirty-five crops and comparison sheets cut from them.
Every render is `build-ui/tools/Release/snapshot.exe dwell <out> view=… appearance=… [param=value …]`.

| render | state | pixel hash |
|---|---|---|
| `face-dark.png` / `face-light.png` | compact, defaults | `0dcc9a93250ac4a6` / `d3785fc64aef284e` |
| `reveal-dark.png` / `reveal-light.png` | expanded, defaults | `b8103486fe3c4203` / `796132a6c4a62562` |
| `tail-freeze-dark.png` | `lane_gain=0` | `9073f7fc94909099` |
| `tail-build-dark.png` | `lane_gain=60` | `aae2c9bbc5b51815` |
| `lit-gates-dark.png` | `send=1 hold=1 chop=1` | `2267ed99a688d6ce` |
| `lit-fx-dark.png` | `fx=1 fx_type=1 lane_fx=1 lane_fx_type=2 fx_link=0` | `d3e0fe4d33215fd7` |
| `face-fx-lit-dark.png` | compact, `fx=1` | — |
| `face-dot-dark.png` | compact, `drive=50` — the state dot | `7b63e6939e46c56e` |
| `extremes-dark.png` | every travel on a rail | `256fef8cbef73229` |
| `rack-dark.png` / `rack-light.png` | `chain=deesser,eq,dwell,fetcomp,dim` | `c4eb38418ebc767a` / `1326f35ccd42d656` |

The tail's three regions, the lit gates and the lit FX stages were rendered in
**both** appearances; the table lists the dark hash where the pale one carried
nothing new. `tail-freeze-light`, `tail-build-light`, `lit-gates-light`,
`lit-fx-light`, `face-fx-lit-light` and `face-dot-light` are beside them.

**Every one of these was looked at before anything was measured off it.** Three
of the numbers below only exist because a render raised the question first.

## What the renders say

**The face reads as nine controls and not as a strip with things hidden in it.**
CHARACTER above the first rule, TIME as the hero with SYNC on its centre line,
FEEDBACK and MIX as a pair over their two numbered strips, STEREO, the two cuts,
the FX gate, the arrow on the foot. Nothing is cramped and nothing is stranded.

**The reveal's three columns are cut on the same two lines** — the layout dump
puts all three rules at `y 69..84` and `y 452..467`, which is the thing `15`
said was worth redoing the lane for, and it is exact rather than nearly exact.

**The tail's three regions work as a caption rather than as three modes.** At
−40 % THROW is in the accent and FREEZE and BUILD are in the secondary ink; at
0 % it is FREEZE, at +60 % BUILD, and the knob's catch mark sits under the
middle word. Reading the three together does say "this knob has two other
places to be", which is what that band was for.

**The lit states read on both plates**, and the FX captions follow their type:
`AMOUNT (SMEAR)` / `(DEPTH)` / `(BITS)`, with the lane's own rule changing
between `FX - TIED` and `FX` as FX LINK goes off.

**Nothing is clipped and nothing overlaps.** `AMOUNT (SMEAR)` is the widest
caption on the panel and its ink ends 18 design px above the panel foot. At the
rails — `2000 ms`, `100 %`, `-24.0 dB`, `-100 %` — every value string still fits
its box. In the dump, TIME's box ends at x 199 and SYNC's begins at 200, and the
lane's FX LINK ends at 830 on a column that ends at 830: tight by construction,
not by accident, and correct.

## What was measured

`tools/inspect/Inspect.exe`, off the pixels, never from the formula.

### The accent, on both plates

| ink | ground | ratio | L\* |
|---|---|---|---|
| `#f094e6` knob face | `#2e2e32` dark plate | **6.49:1** | 73.2 |
| `#f094e6` knob face | `#efefef` pale plate | **1.81:1** | 73.2 |

Both mid-band against the shipped 5.87–7.19 dark and 1.72–2.00 pale, and both
land exactly on the figures `modules/dwell/Module.cpp` and `15` already claim.

### The other inks

| what | dark | pale |
|---|---|---|
| section legend (`#f7c9f2` / `#90598a`) | 9.40:1 | 4.59:1 |
| value readout (`#9a9aa4` / `#9a9a9a`) | 4.85:1 | 2.45:1 |
| knob caption, 15 pt — the raw accent | 6.49:1 | 1.81:1 |
| caption of a **disabled** knob (`#5c4b5d` / `#e2d1e0`) | 1.69:1 | 1.27:1 |
| **disabled SYNC's label** on its own fill | 2.96:1 | **1.27:1** |

Captions are at `kCaption` 15.0 pt throughout — the suite default — and take the
**raw** accent, which is why the pale figure is 1.81 and not 4.59. That is not
Dwell's choice: `core/ui/Controls.cpp` takes it deliberately, names the same
1.72–2.00 band, and says in as many words that it was Frosty's call on a render
and is not to be "fixed". BMO Saturator's caption ink measures L\* 73.5 against
Dwell's 73.2 on the same plate, so this panel is doing exactly what the one next
to it does.

### Dead bands, per column

`Inspect.exe gaps`, each column cropped out of the reveal so that ink in one
column cannot fill a gap in another. Identical in both appearances.

| column | worst empty band | where (panel-local design y) |
|---|---|---|
| face (and the compact panel) | **31 px** | 40..70, between CHARACTER and the DELAY rule |
| main-delay depth | 49 px | 530..578, above the FX rule |
| lane | 49 px | 530..578, the same line |

Plus a deliberate **71 px** bare band at the top of the depth column: that is the
CHARACTER row, which belongs to the face, and the column starts at the DELAY
rule under it.

Against the suite (`tools/inspect/README.md`): DEQ compact 20, BMO EQ 21, DEQ
expanded 30, BMO Util 46, BMO Opto 48, BMO Saturator 66, BMO Dimension 72.
**The face is the tightest panel in the suite after the DEQ/EQ pair, and the two
open columns sit with Util and Opto.** Four layouts of pulling density out did
not overshoot: nothing here is airier than Saturator, let alone Dimension.

## In the rack

`chain=deesser,eq,dwell,fetcomp,dim` — BMO Defang, BMO CEQ, **BMO Dwell**, BMO
FET, BMO Dimension — both appearances, with CEQ deliberately in the slot
immediately to Dwell's left. Sampled off the rendered rack:

| module | knob face | L\* | hue |
|---|---|---|---|
| BMO Defang | `#ea9f9a` | 72.6 | 3.8° |
| BMO CEQ | `#f08cb4` | 69.9 | 336.0° |
| **BMO Dwell** | `#f094e6` | 73.2 | 306.5° |
| BMO Dimension | `#d4a4ff` | 74.7 | 271.6° |

**The orchid holds as its own colour.** Against Defang's rose, FET's blue and
Dimension's lavender it is never in question. Against CEQ's pink — the 29.5°
neighbour `13` §6 warned about, and the reason this check was asked for — the
knob faces separate: Dwell's is plainly the more violet and the more saturated,
and CEQ's dial faces are duller besides. Side by side at full size I would not
confuse a Dwell slot for a CEQ slot.

**One place it does not separate**, and it is worth Frosty's eye: the accent
strip along the top of each slot bar. It is two or three pixels of flat colour
with no face, no caption and no shape to it, and with the two slots adjacent
Dwell's strip and CEQ's read as the same pink. It is the thinnest use of the
accent in the suite and the only one that is nothing but the colour. Not a
defect, and not something I would change without being asked.

## What I would put in front of Frosty

Nothing below was changed. None of it is a defect; each is a judgement.

1. **Seven of the seventeen knobs print no value.** DRIVE, RATE, DEPTH, DUCK,
   LO CUT, HI CUT and both AMOUNTs are laid out on `kPairRow` (knob + caption)
   where TIME, FEEDBACK, MIX, TAIL, LANE TIME and LANE LEVEL get a value row.
   `13` §4 assumed Hertz on the cuts and Hertz on RATE — "`850 Hz` / `2.10 kHz`
   free" — and dB on DUCK. As built, a reader cannot tell 6 kHz from 18 kHz on
   HI CUT without dragging it. The row heights are §7-free after ship, so this
   can move later; it is 14 px a row if it moves.
2. **At defaults the pale plate shows both FX columns' bottom captions at
   1.27:1.** FX ships off, the reveal greys rather than hides (deliberate, `13`
   §6a, so the width never changes underneath anyone), and core draws a disabled
   caption at alpha 0.4 over an accent that is already at 1.81 on that plate.
   On the dark plate the same state is 1.69:1 and reads as "off". On the pale
   one it reads as nothing at all.
3. **SYNC ships disabled, and its label measures 1.27:1 on the pale plate.**
   Dark is 2.96:1 and fine. It is core's disabled-switch treatment (fill at
   alpha 0.35, ink at 0.4), but Dwell is the only module that ships a control
   permanently in that state, so it is the only panel where anyone sees it. A
   user on the light theme sees a grey box with no word in it.
4. **The lit glow still barely reads on the pale plate** — measured before, open
   in `15`, and confirmed here: on dark the bloom ramps 31 px out of the switch,
   on pale it is nearly invisible and the *fill* carries the lit state on its
   own. It does carry it. The fix `15` proposes is a darker halo.
5. **The FX-link bracket reads like a column divider before it reads like a
   tie.** It is a hairline down the whole 172 px of the bottom band with a spur
   into the gutter, and at a glance it looks like the panel has one interior
   divider in the one place a three-column panel would not want one. It does the
   job once you have noticed it points at the column it is following. A shorter
   bracket — the rule line and a few px down — would say the same thing.

## Two things that are wrong but are not the panel

Neither was fixed: neither is visual, and this was the look-at-it pass.

- **`modules/dwell/params.h`'s header comment is a schema behind the code it
  documents.** It opens "twenty-six parameters, 0-25, settled 2026-09-22" and
  puts `fx_link` at id 25; the enum, both `static_assert`s and the panel say
  **twenty-seven, ids 0–26**, with `lane_note` at 22. The inline row comments
  carry the same drift — "`// 22, 23, 24` — the lane's FX stage" is ids 23–25,
  and "`//== Id 25, the last row`" is 26. `15` is correct and says 27. The
  asserts mean the code cannot be wrong; only the reader can be.
- **`tools/CMakeLists.txt` carries three `target_link_libraries(snapshot …)`
  lines and `tools/snapshot/main.cpp` three usage banners**, each naming a
  different subset of the products. Merge residue from the modules that landed
  in parallel. It is harmless — the calls accumulate, so everything links — but
  the usage banner now tells a reader three different lists of what `snapshot`
  takes, and only the union is true.

## Verification

- `cmake --build build-ui --config Release --target snapshot` — **exit 0**.
- `cmake --build build-ui --config Release --target <the 22 test executables>` —
  **exit 0**, no errors in the log. Named explicitly; nothing was installed.
- `ctest --test-dir build-ui -C Release` — **22/22 passed**, 66.6 s, including
  `dwell`, `rack` and `ui_layout`. The layout tests were not touched because
  nothing in the layout was.
