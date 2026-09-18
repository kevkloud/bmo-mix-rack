# The UI pass — the render loop, and every module's starting numbers

**Prepared on AURORA, 2026-09-15, on branch `ui-pass` at `1c5299f`.** Written
so a session can start on any module without re-deriving where the tools are
or what "before" looked like. The companion files are
`ui-pass-handoff-2026-09-14.md` (what is decided and what is Frosty's),
`ui-pass-checklist.md` (what to look at, panel by panel) and
`ui-editor-handoff.md` §6 (how to be wrong, and how not to be).

**Nothing here needs CI.** That is the whole point of the file. A CI round
trip is ~22 minutes; the loop below is measured at **4.3 seconds**.

---

## 0. Read this before the checklist — parts of it are already answered

The pass is running one module at a time. LTV Comp (module 1) is done to the
point where it is waiting on Leteveon; **BMO DEQ (module 2) is done to the point
where it is waiting on Frosty** — one defect fixed, five decisions gathered, in
`ui-pass-deq-2026-09-15.md`. Read that before touching DEQ, and read its §5
before re-finding three things that look like faults and are not. BMO Tune RT
(module 3) is in `ui-pass-tune-2026-09-15.md`. **BMO Dimension (module 4) is
done** -- its record is
`ui-pass-dim-2026-09-17.md`, and its controls have new names. **BMO Opto
(module 5) is done** (`ui-pass-opto-2026-09-17.md`, the meter's two scales),
and so is **BMO Util (module 6)** (`ui-pass-util-2026-09-17.md`, one knob row,
two value readouts, captions matched to the compressors). Next are the
Saturator and then CEQ. Nothing is pushed and CI has not been touched this
pass.

Both older documents were written before most of these commits, so check here
first:

| the older document says | actually |
|---|---|
| `ui-pass-checklist.md` §B, BMO DEQ: the **GR bar**, **solo and the analyser**, the **48 kHz response view**, the **compact 320 in a rack** | All four measured and put to Frosty, none decided — `ui-pass-deq-2026-09-15.md` §3 and §6. The GR readout defect found alongside them is **fixed** (`71d6d01`) |
| `rest-dot-finding.md` §5: "a control defaulting to 2–3% of its range would sit inside \[the suppression threshold]. There is no such control today" | **Stale.** DEQ band 1's FREQ defaults to 30 Hz — 5.9% of a log sweep, not its 20 Hz minimum — and is suppressed. So is band 12's. Correct behaviour, but the note's claim is no longer true |
| `ui-pass-checklist.md` §A: **`utilGain` placeholder**, three options, Frosty's call | **Settled** — option (2), the utility azure `#4fb8e8`, Frosty 2026-09-14, in `aff0282`. `core/ui/Tokens.h:160`. The token is themeable now too |
| the handoff's "blocked on Frosty": **Vcomp's names and accent** | Still open, but **ordered**: Frosty wants control placement final *first*, then names, then the accent last. Do not re-open the names before the layout |
| snapshot id `vcomp` | **`ltvcomp`** — the CLI keyword followed the module id. Flagged in the session note as not confirmed with Frosty |
| `ui-pass-2026-09-14.md`: "six commits sit on `ui-pass`" | eleven, as of `1c5299f` |
| BMO Util "(320 wide)" | design width is **160**; see §4. **Fixed in the checklist 2026-09-17**, along with the rest of Util's entry — its pass is done |

**The one-module rule.** Frosty asked for the pass to take one module at a
time, which is why several live questions are parked rather than forgotten —
Dimension's DETUNE scope and **Tune's empty middle are modules 4 and 3**, and
the raw-legend contrast question is a *suite* question that waits until it can
be asked as one. **BMO DEQ is next, not Tune** -- corrected 2026-09-15. Reverse publish order
is by when a module was added, and `git log --diff-filter=A` on each
`params.h` puts DEQ at 09-10 **21:32** and Tune at 09-10 **17:23**. They landed
the same day about four hours apart, which is how they got swapped. The order
is LTV Comp, DEQ, Tune, Dimension, Opto, then the 09-05 three -- which Frosty
ordered on 2026-09-17 as **Util, Saturator, then CEQ**.

Tune's empty middle, when its turn comes, is a candidates-for-Frosty item and
not something to decide in the edit.

---

## 1. The loop, measured

Everything runs from the `ui-pass` worktree, `../bmo-mix-rack-333-ui`, which
has its own warm `build-release` configured with **both** `BMO_BUILD_RACK` and
`BMO_BUILD_TUNE` ON. Renders come from *that* build, not from
`../bmo-mix-rack-333-int`, because that one is `integration` and does not have
this branch's panel changes in it.

| step | command | measured on AURORA |
|---|---|---|
| edit a rack panel, rebuild | `cmake --build build-release --config Release --target snapshot` | ~1 s up-to-date; a panel `.cpp` recompiles in seconds |
| edit `TunePanel.cpp`, rebuild | `cmake --build build-release --config Release --target bmo-tune-snapshot` | **4.3 s** |
| render | see §2 | under a second |
| measure | `tools/inspect/Inspect.exe ...` | instant |

Verified end to end: touching `modules/tune/panel/TunePanel.cpp`, rebuilding
and re-rendering gives back the **same pixel hash**, `8076f67b0802f196`. The
loop does not perturb what it measures.

Build the *target*, not the tree. `cmake --build build-release` with no
`--target` builds nine plugin bundles as well and is the 20-minute thing this
file exists to avoid.

---

## 2. The two renderers

They are two different tools and they do not take the same flags.

### The rack modules

```
./build-release/tools/Release/snapshot.exe <id> out.png [width height] [k=v ...]
```

Ids: `eq  sat  util  opto  dim  deq  ltvcomp  rack`.

**The id is `ltvcomp`, not `vcomp`.** The module folder is still
`modules/vcomp`; only the product renamed. Typing `vcomp` is refused.

| flag | what it does |
|---|---|
| `appearance=dark\|light` | the other palette, for this process only — it cannot flip an open plugin |
| `signal=<dBFS>` | runs a 1 kHz tone through first, so meters read instead of resting. **`-18` is what every baseline below used** |
| `theme=<file.json>` | overlays a flat token→hex palette, so a candidate colour renders without writing the machine-wide `Themes/Default.json`. A missing file is fatal |
| `view=compact\|expanded` | DEQ's two widths. Standalone opens **expanded**, so a bare `snapshot deq` is the 600; `view=compact` is the 320 a rack shows |
| `ui.<key>=<value>` | panel state with no parameter behind it. Today only `ui.meter=IN\|GR\|OUT` on Opto. Refused by every panel is fatal |
| `chain=util,eq,sat,opto`, `N.id=value` | rack only: the slots, and a parameter of slot N (1-based) |

`view` is a flag, not a size override. Passing `600 420` as width and height
stretches the compact panel and looks plausible — that mistake has been made
here once already.

### BMO Tune RT

```
./build-release/tools/tune/Release/bmo-tune-snapshot.exe out.png [width height] [k=v ...]
```

No module argument — it is one product. Takes `appearance=` and its own
parameters (`key=Bb`, `scale=Minor`, `retune_ms=12`; Retune Speed by its
milliseconds, never by step index).

**It does not take `signal=` or `theme=`.** Confirmed by running them: both
come back `unknown parameter`, which is fatal by design. The tool was forked
from `tools/snapshot` before either landed. Two consequences for the Tune pass
specifically:

- Tune's panel has no meters, so `signal=` costs nothing. Fine.
- **The candidate-colour loop does not exist for Tune.** Trying an accent on
  the rack modules is a `theme=` JSON and no rebuild; on Tune it is an edit to
  `core/ui/Tokens.h` and a 4.3 s rebuild. Cheap either way, but it is a
  different move, and worth knowing before offering Frosty a colour ladder.

Both tools refuse an unknown parameter or a mistyped choice name rather than
ignoring it. That is deliberate: a render that quietly answered a different
question is worse than no render, and nothing downstream can tell it from the
right one.

---

## 3. Measuring, not squinting

`tools/inspect/Inspect.exe`, from the same worktree — the `gaps` mode is new
on this branch and is not in `integration`'s copy.

```
scan <png> row|col <n>      run-length: start, end, width, exact hex
hist <png> x y w h [n]      exact-colour histogram, with L* per colour
crop <png> x y w h scale out.png
sheet out.png <png> <label> [...]
hash <png> [...]            SHA-256 of the pixels
ratio <a> <b> [...]         contrast and L*, every pair both ways
gaps <png> [min] [header] [scale]   bands of bare plate down a panel
```

The three that carry this pass:

- **`gaps`** answers "is this panel too airy", which `ui_layout_tests --dump`
  cannot. The dump prints control *boxes* and boxes are nearly contiguous; the
  ink inside them is not, because a knob box carries padding above its face and
  below its caption. Read from the dump alone Util has no empty band worth the
  name; rendered, its worst was 46 px against BMO EQ's 21 — 38 since its pass,
  and at the top of the panel rather than under MONO.
- **`ratio`** is how every colour claim in this pass has to be stated. A ratio
  without a named ground is not a measurement.
- **`hash`** settles "this change should move nothing" instead of arguing it.

`gaps` infers the plate rather than assuming it, which matters now that LTV
Comp is on the LTV line: it comes back `#3a3a3e` there against `#2e2e32`
everywhere else, and it got that right without being told.

---

## 4. Every module's baseline, at `1c5299f`

Rendered on AURORA, 2026-09-15, both appearances, `signal=-18`, into
`snapshots/_prep-<id>-<appearance>.png` of the `ui-pass` worktree.
`snapshots/` is gitignored, so these are on AURORA only — re-render rather
than look for them elsewhere.

These are the **before** side. Hash again after any change that should move
nothing.

| module | design w | render w | pixel hash dark | pixel hash light | largest bare band (dark) |
|---|---|---|---|---|---|
| BMO CEQ | 280 | 560 | ~~`ac0d5c764049b912`~~ `cc76b8c7987bb940` | ~~`b57148059978ac2c`~~ `1ad53a0f0035f763` | 17 px at 427 |
| Saturator | 260 | 520 | `270159d1c8ccccb5` | `e076b68d1d065d98` | 66 px at 91 |
| BMO Util | **160** | 320 | ~~`1225e16081194ecf`~~ `ce603457cbf3bc73` | ~~`179fe57b060ba655`~~ `ee71528c39941701` | ~~46 px at 520~~ 38 px at 0 |
| BMO Opto | 220 | 440 | ~~`784ca005ff68a747`~~ `ab3ff3b77116b7a5` | ~~`9a87504c6cf57c33`~~ `878cca7b1a80a551` | 48 px at 82 |
| Dimension | 220 | 440 | `339dfc8560a3c548` | `4063603b21333eff` | 72 px at 180 |
| BMO DEQ | 600 (expanded) | 1200 | `8b15e62102cad9a6` | `325623156690ffec` | 30 px at 536 |
| BMO DEQ | 320 (compact) | 640 | `35df0fbe474b0551` | `20e67b4acc38959a` | 20 px at 340 |
| LTV Comp | 260 | 520 | `e3d5fc2f21d11822` | `8901aa58c7466207` | 184 px at 486 |
| BMO Tune RT | 360 | 720 | `8076f67b0802f196` | `10738b95c82711ef` | **110 px at 273** |

**BMO CEQ's row was re-taken 2026-09-17**, at the end of the last module's
pass, and the module is renamed. `snapshot`'s keyword is still **`eq`**: the
*module* id never moved, only the product's display name. The panel gained an
OVERSAMPLING row and an AUTO switch, HI-Q went up to the mid band, and the
bands paid 17 px each for it. The rack's hash moves with it, because the rack
has CEQ in it. See `ui-pass-ceq-2026-09-17.md`.

**BMO Util's row was re-taken 2026-09-17**, at the end of its own pass
(`db63acb`, `a2e5a63`): one knob row for all three, values under VOLUME and
PAN, and the captions 11 px closer. `signal=` still moves nothing on it — no
meter — so the row is a bare render either way. With MONO on it is
`7b1c566c8df65964` / `f91161aa5277371a`, and WIDTH is dimmed there. See
`ui-pass-util-2026-09-17.md`.

**BMO Opto's row was re-taken 2026-09-17**, twice over: `bfbafc4` made a
metered render reproducible (the old hash was one of two or three images the
tool could produce), and `db91b67` moved the VU scale. See
`ui-pass-opto-2026-09-17.md`. Bare Opto is `910e41b64ed45b5c` /
`2394065f3f2b8a74`. The same tool fix moved **DEQ and the rack with a
signal** and nothing else; every other row stands.

**Every render is 2× the design size**, and that is the tool's doing, not the
editor's: `createComponentSnapshot (bounds, false, 2.0f)` at the foot of
`tools/snapshot/main.cpp`. The editor itself builds at 1× — `ProductEditor`
takes `scale = 1` when there is no window yet. So design height is
`28 + 24 + 688 = 740`, every render is 1480 tall, the panel starts at design-y
52, and **the band positions above are design pixels**, which is what
`gaps` prints.

**`ui-pass-checklist.md` had BMO Util as "(320 wide)", which is its render
width, not its design width.** `modules/util/Module.cpp:14` is `160`. Every
other parenthetical in the checklist is a design width, so Util's was the one
that did not mean what its neighbours meant. **Fixed there 2026-09-17** — it
reads "160 design, 320 rendered" now.

DEQ has two rows, because it is two panels: the compact 320 needs
`view=compact` and hashes differently. Both taken 2026-09-15.

**The expanded pair moved at `71d6d01`** and is now `13e5c1b6526f7e37` dark,
`6b4aba73320e1d63` light — the GR cell went from 36 px to 46 so its readout
stopped clipping. The compact pair is unchanged by that commit, which is the
point: the bar prints no value at 320. The table keeps the pre-commit figures
because every other row in it is `1c5299f`.

One more thing about DEQ specifically: **`signal=` moves nothing on this
module** at Init. Every band ships off, DEQ has no level meters, and the GR bar
is all a signal could reach — so a bare render and `signal=-18` are byte-equal.
To make the meter read, switch a band on and give it dynamics
(`b7_on=1 b7_dyn=1 b7_thr=-30`), and remember that setting any parameter at all
puts `Init *` in the header and changes the hash.

**Two numbers to read before reacting to them:**

- **Tune's 110 px at design-y 273** is the empty middle third the checklist
  already names, and it measures identically in both appearances. A second
  band of 81 px sits at 589, under RETUNE's readout.
- **LTV Comp's 184 px at 486 is already explained** — do not re-find it.
  `ui-pass-2026-09-14.md` records that the panel is laid out for its advanced
  mode while **standard is the default**, so the foot is bare plate: 190 px in
  standard against 48 px with COMPLEX on. The 184 measured here is that same
  band after the intervening commits. It is Frosty's open item 1 on LTV Comp,
  not a new finding.

EQ's 21 px is the house floor: "no empty band over 16 px anywhere on the
panel" was measured on EQ and is what the others are being read against.

---

## 5. What this loop does not cover

Know the edges before trusting it.

- **`ui_layout_tests` is a separate check and must stay green.** It pins the
  shared rows as absolutes. It only walks panels in its product list, so a
  green suite after adding a module is not evidence the module was tested —
  that is how Vcomp shipped with clipped captions.
- **A meter's *face* is painted, not placed**, so nothing automated guards it.
  `ui_layout` asserts component bounds and the scale tables; the GR face's
  appearance is a render question, every time.
- **Contrast assertions do not exist yet.** They are a pure function of the
  tokens, need no rendering, and are the cheapest open item in the whole brief
  (`docs/ui-workflow-brief.md` §4). Doing them first guards everything after.
- **This is a picture, not a plugin.** Hosts, resizing, DPI and anything a DAW
  does belong to stage 4 on a CI build, and so does every listening result.

---

## 6. The habits that are not optional

Condensed from `ui-editor-handoff.md` §6, which is the long version and is
worth reading once. Every one of these was learned by being wrong.

- **Render to a unique filename.** A stale read of a path only re-rendered in
  the reader's head cost a full debugging cycle chasing a bug that did not
  exist. `crop` and `sheet` now refuse to overwrite; the other modes trust you.
- **Both appearances, every time.** Half the faults on the last pass existed in
  one only. A change measured on the pale plate has not been checked.
- **Check the opening state specifically.** Three of the worst faults ever
  found here were visible only in Init — the rest dot on the band marker,
  COLOR reading off while the DSP held it on, and the GR needle resting on the
  `0`. It is the first thing anyone sees.
- **Scan a line, do not squint at a crop.** Two inks 1.01:1 apart look like a
  ring and are a slab. No amount of looking produces that number.
- **Render, do not compute.** Two dark-cap schemes measured fine and shouted.
- **Judge a meter on the whole panel.** Four rounds of candidates were compared
  on crops; the first full-panel render settled it immediately.
- **Assert absolutes, not comparisons.** "Better contrast than before" is the
  trap that passed a broken release.
- **No hex outside `core/ui/Tokens.h`**, and Frosty decides anything a listener
  would notice — offer options with numbers, do not pick quietly.
- **Name the machine** on every result recorded.
