# BMO Dwell (`Bdly`) — panel and UI direction

> **SUPERSEDED — read `15-lane-redesign.md` and the panel as built first.**
>
> This document is the direction written on 2026-09-20 for a panel that was
> **rejected three times**. Its diagnosis is in `15`: 20 parameters in a 280 px
> column is 7.1 controls per 100 px, the densest panel in the rack. What
> replaced it is a **nine-control face** with everything else revealed, and the
> schema underneath it changed as well — **THROW, THROW MODE, BUILD, FREEZE and
> VOICE do not exist**, and the lane replaced them (`10` §11).
>
> It is kept because three parts of it are still good and are not written down
> anywhere else: **§6a's account of how DEQ's expansion mechanism actually
> works** (widths, the session-only `view` flag, what is and is not a
> parameter), **§4's caption and readout conventions**, and **§7's split
> between what is permanent at ship and what stays free**. Everything about
> rows, counts, widths and performance buttons is history.
>
> The specific corrections are marked **CORRECTED 2026-09-22** in place, so no
> figure here can be mined without meeting the note that says it is wrong.
> Where this document and the built panel disagree about layout, **the built
> panel wins** and this one is not the record.

Written on AURORA on 2026-09-20. Direction only, no implementation. Evidence:
`docs/delay/00-repo-conventions.md`, `docs/delay/10-dsp-spec.md` (cited 00/10),
`core/ui/Tokens.{h,cpp}`, `core/ui/ModulePanel.h`, `core/ui/Controls.h`,
`modules/sat/panel/SatPanel.cpp`, `modules/vcomp/Module.cpp`. No control is
labelled DWELL. The creative controls arrive with a matching amendment to 10.

## 1. Control hierarchy

**CORRECTED 2026-09-22.** The performance pair and the VOICE ring below are
**both gone**. THROW and FREEZE were replaced by the lane's gates — SEND, HOLD,
CHOP and the bipolar LANE GAIN (`10` §11) — and **VOICE was cut outright**
(`15`, README item 18), so there is no resonance to hang on HIGH CUT, no
`ui::ConcentricBand` ring, no voicing legend and no fourth-row question. The two
cuts are plain one-poles with ordinary Hertz readouts. The face is nine
controls; see §2's correction for what is on it.

- ~~**Performance:** THROW and FREEZE — two wide lit buttons on their own row
  under the primary trio, both shipped enabled in v1 (DECIDED, Frosty
  2026-09-20). Reachable without hunting, lit from across the room.~~
- **Primary (largest faces):** TIME/NOTE, FEEDBACK, MIX. Time, how long it
  lasts, how much you hear. *(Still true.)*
- **Secondary:** DRIVE, MOD RATE, MOD DEPTH; then LOW CUT, HIGH CUT, DUCK.
  DUCK is the one non-loop control in the loop section. *(The "(+ VOICE)" that
  rode on HIGH CUT is struck.)* **From 2026-09-23 all of these except DUCK
  govern both engines** (`10` §11.3), which the panel has to say somewhere: a
  knob that moves two delay lines should not look like one that moves one.
- ~~**VOICE** — the creative filter voicing (10 §11's name) — rides as a
  `ui::ConcentricBand` ring on HIGH CUT rather than taking a fourth row.~~
- **Switch rows (`ui::SwitchButton`, `Tokens::switchWidth` 70):** CHARACTER
  (CLEAN / TAPE / BUCKET), STEREO (STEREO / PING-PONG / DUAL). Three cells is
  the row maximum here. SYNC is one switch inside the TIME cell, not a row of
  its own — it belongs to that knob.
- **TIME and NOTE share one position:** one box, one caption, two parameters;
  SYNC decides which is live and which greys (`setKnobEnabled`). The caption
  reads **TIME** off, **NOTE** on. Readout `1 ms` … `2000 ms` (ms throughout,
  never seconds) vs `1/8D`, `1/4T`, `1/1`. SYNC on with no valid host tempo
  (10 §7): the note name stays, drawn in `text2`, so the fallback is visible.
  SYNC's slot is permanent now; the switch ships disabled until 12's tempo
  plumbing lands (DECIDED, Frosty 2026-09-20).

## 2. Layout

**CORRECTED 2026-09-22 — the eight-row compact layout below is superseded.**
The face is **nine controls** — CHARACTER, TIME with SYNC, FEEDBACK, MIX,
STEREO, LO CUT, HI CUT, FX — at 3.2 per 100 px against the rejected panel's 7.1
(`15`). Everything else is **revealed, not gated**: every parameter stays live
and is read at all times. The reveal is **not the single 460 px column §6a
describes**: it grew to three columns at 980 px — 260 face + 260 for the main
delay's depth + 400 for the lane — and then the **2026-09-23 pullback took seven
controls out of it**, the lane's whole second voicing and LINK, so 980 is
oversized for what is left and **the width is being redrawn**. No width in this
document is the current one; `modules/dwell/Module.cpp` is. The rows, the
performance row and the row px below are the rejected panel's and are kept only
as the record of what was measured.

Width **280** (BMO EQ's; 260 content). `ModulePanel` gives 688 design px: 94
in, 126 out, **468 in the middle**. Slack re-centres, per `modules/sat`.

| # | Row | px | Contents |
|---|---|---|---|
| 1 | rule | 16 | legend **DELAY** |
| 2 | primary trio | 148 | TIME/NOTE (+ SYNC), FEEDBACK, MIX |
| 3 | performance | 32 | THROW, FREEZE — wide lit buttons (**both cut**) |
| 4 | switch row | 28 | CLEAN / TAPE / BUCKET |
| 5 | rule | 16 | legend **LOOP** |
| 6 | trio | 100 | DRIVE, MOD RATE, MOD DEPTH |
| 7 | trio | 100 | LOW CUT, HIGH CUT, DUCK (the VOICE ring is cut) |
| 8 | switch row | 28 | STEREO / PING-PONG / DUAL |

The sketch below is **the rejected panel**, kept as the record. THROW, FREEZE
and the VOICE ring in it are all cut.

```
 ---- DELAY ------------------------
   (TIME/NOTE)  (FEEDBACK)   (MIX)
     [SYNC]                   '50
   [   THROW   ][   FREEZE   ]
   [ CLEAN ][ TAPE ][ BUCKET ]
 ---- LOOP -------------------------
   (DRIVE)   (MOD RATE) (MOD DEPTH)
   (LOW CUT) ((HIGH CUT)) (DUCK)[GR]
   [STEREO ][PING-PONG][  DUAL  ]
```

Faces near 78 px primary, 64 secondary. Print ping-pong's mono sum (10 §8)
under the STEREO row, not in a tooltip.

## 3. Momentary behaviour

**CORRECTED 2026-09-22.** THROW and FREEZE are gone; the bools this section
describes are now **SEND (13), HOLD (15) and CHOP (16)**, and the behaviour
below still applies to them — it was always behaviour over a plain bool. Two
differences that matter: **HOLD is not momentary**, it latches by nature because
switching it off *clears* the lane (`10` §11.4), and **BUILD is not a button at
all** — it is the upper region of the bipolar LANE GAIN knob, so the travelling
highlight described below has nothing to draw.

SEND, HOLD and CHOP are ordinary bools in the permanent schema. Everything here
is panel behaviour over that one bool.

- **Mouse:** true momentary. Down sends 1, up sends 0; release also fires on
  drag-out, deactivation and focus loss, so nothing is stranded lit.
- **Automation:** the host writes the same bool, so a drawn envelope behaves
  like a press. The panel follows the parameter, never the reverse.
- **Latch:** modifier-click (or right-click menu) holds it on. Latch is
  panel-only, never in `PARAMS`; presets, `prepare` and bypass force 0.
- **Lit state:** accent at full strength with a glow, not the ordinary switch
  tint — must not read as a CHARACTER/STEREO selection.
- ~~**BUILD:** while THROW is held, feedback ramps from the knob's value toward
  the self-oscillation stretch and falls back on release.~~ **Struck**: BUILD is
  the region of LANE GAIN above its detent, not a button, and it moves the
  lane's loop gain rather than the main's FEEDBACK.
- ~~**FREEZE ships enabled in v1**, its own button and slot.~~ **Struck**: what
  holds is LANE GAIN's **sticky centre detent**, and what the panel has to draw
  is that detent — the one position where the lane holds at exact unity
  (`10` §11.2). Where the caption THROW / FREEZE / BUILD changes with the
  region, that is a caption, not three states.

## 4. Captions and readouts

`ParamFormat` covers almost everything: `Milliseconds` (TIME); `Percent`
(FEEDBACK, MOD DEPTH, MIX, DRIVE — DRIVE matching BMO Saturator; **VOICE is cut
and comes off this list**, and LANE GAIN joins it as a *bipolar* percent);
`Hertz` (LOW CUT, HIGH CUT, MOD RATE, giving `850 Hz` / `2.10 kHz` free);
`Decibels` (DUCK, unsigned, positive meaning more reduction — the convention
`currentGainReductionDb` already uses). NOTE is a `choiceParam` whose strings
are the readout, exactly as 11 §3 fixes them: dotted `D`, triplet `T`, no spaces.

**MIX is not a plain crossfade.** Dry holds unity 0–50% and fades only across
50–100% on 10 §9's cosine taper, the wet rising as `sin(πm)` to full at 50%. The panel must say so or the knob lies: a numbered legend tick at
**50**, and a caption line reading `MIX  (dry held to 50)`. The readout stays
one plain percent across the travel, and 50% is the natural default — full wet
added, dry intact.

**FEEDBACK past unity** (0–100%, g = 1 at ≈97%, 10 §3, DECIDED Frosty 2026-09-20):
keep the unit. Draw the 97–100% stretch in the meter "hot" colour with a numbered
tick at the onset. Colour and a tick, not a word.

## 5. Visual feedback

**CORRECTED 2026-09-22 — `ui::DynamicsMeter` cannot be had at this width.** It
is a needle VU whose radius is `width/2 − 8 − 19.5`, so in the narrow lane this
section asked for it **computes a negative radius**, and the narrowest box it
draws anything readable in is about half the panel. The recommendation below was
geometrically impossible, not merely tight. **What shipped is a horizontal bar,
18 px, filling from the left with hairline quarter marks** — a scale without
printing numbers beside a knob whose own readout is already in dB. §7 puts
"meter scale and rate" among the things free after ship, so this is a layout
choice and not a schema one. The 15 Hz rate below is unchanged and is what
shipped. There is also **no BUILD ramp to carry**: BUILD is a region of LANE
GAIN, not a ramp the panel animates.

Recommended: **one GR lane for DUCK**, reusing `ui::DynamicsMeter` in its
gain-reduction mode, right of row 7. Ducking is the only thing here you cannot
hear as itself, and it needs no new plumbing — it reads the
`currentGainReductionDb()` hook already on `bmo::ModuleDsp`: an atomic scalar,
no analyser tap, no lock.

Rejected: a tap/echo-decay display. It wants a repeat model on the UI thread
and new state crossing threads, and the tail is audible already.

Cost: **15 Hz**, as `modules/opto`, `modules/util` and `modules/tune` do,
rather than the 30 Hz in `core/ui/Controls.cpp` — half the repaints, ample for
a 180 ms release, quick enough that a held SEND lights without lag.

## 6. Accent

**SETTLED 2026-09-21 — the accent is the orchid `#f094e6`**, third in the table
below. **Measured off a rendered panel rather than computed: 6.49:1 on the dark
plate and 1.81:1 on the pale**, both mid-band (`15`, README item 16;
`products/AGENTS.md` carries the allocation row). The olive-gold this section
recommends was rejected outright by Frosty, and the warning below that an orchid
"may read as EQ in a rack" was put to him with renders before he chose. The
candidates and the arithmetic are kept as the record of what was weighed; **the
choice is not open**, and the figures in the table are formula-derived rather
than measured, so the two measured numbers above are the ones to quote.

Dark plate `#2e2e32`, pale `#efefef`. Shipped accents run 5.87–7.19:1 dark and
1.72–2.00:1 pale — relative luminance ≈0.41–0.49.

| candidate | hue | dark | pale | note |
|---|---|---|---|---|
| **`#b2bb54` olive-gold** | 65° | 6.5:1 | 1.83:1 | widest free gap, 33° clear of Saturator |
| `#f79a8e` coral | 7° | 6.4:1 | 1.86:1 | only 25–31° from Saturator and EQ |
| `#f094e6` orchid | 306° | 6.5:1 | 1.84:1 | 30° from EQ's pink; may read as EQ in a rack |

`11-integration-and-test-plan.md` §1 derives a different three (`#f0938c`,
`#e694e0`, `#e6e278`); six candidates in all, and one accent is chosen once.

~~**Recommend `#b2bb54`.**~~ **Overruled** (Frosty, 2026-09-21: "i hate this
color"), as was a pale gold that measured out of band. The rule the
recommendation ended on was the right one and is what settled it: render panel
and rack, both appearances, a gate lit, before allocating.

## 6a. The FX section, and how expansion works here

> **The mechanism below is correct and is the reason this document is kept.**
> The widths and the column are not, and they have moved twice. The 280 → 460
> single column was never enough: the second width has to carry the main
> delay's depth *and* the lane, which took it to 980 across three columns on
> 2026-09-22. Then the **2026-09-23 pullback** deleted the lane's second
> voicing and LINK, so the reveal shrank again and **the width is open**. What
> the arrow does, what is a parameter and what is a session-only flag are
> unchanged throughout, and that is the part worth reading here.

**How BMO DEQ does it — read this first.** A module declares a second *width*:
`ModuleDef::expandedWidth` (`core/product/ModuleDef.h:46`, with
`isExpandable()` at `:67` and `widthFor(bool)` at `:71`); DEQ sets 320 / 600 in
`modules/deq/Module.cpp:16-17`. The state is **not a parameter and not panel
state**: it is a product/host-level flag —
`SingleModuleProcessor::expanded` (`core/product/SingleModuleProcessor.h:67-90`,
defaulting to expanded, so **standalone opens wide**) and `RackProcessor`'s
per-slot `expanded` (`core/rack/RackProcessor.h:100-107`, `.cpp:90-100`,
forced compact for a new slot at `.cpp:143`, so **a rack opens compact**). It is
saved with the session as a `view` attribute on the module's PARAMS element,
written by `getStateInformation` only and **never by `captureState`**, so it is
absent from presets (`core/product/SingleModuleProcessor.cpp:138-165`;
`core/AGENTS.md:38-46`). In the rack it rides on the module's carried state, so it
follows the module through chain edits. The switch is `ui::ExpandButton`
(`core/ui/ExpandButton.h:9-20`) on the host's bar — the standalone header
(`core/product/ProductEditor.cpp:18`) or the rack slot bar
(`core/rack/RackEditor.cpp:22`, `toggleSlotView`) — **never on the panel**. The
panel is told nothing: `DeqPanel::isShowingExpanded()` is
`width >= def.expandedWidth` (`modules/deq/panel/DeqPanel.cpp:152-154`) and it
picks `layoutCompact` or `layoutExpanded` from that (`.cpp:372-380`). The rack
re-lays out from `RackEditor::slotWidth(slot)` and `refit()`
(`core/rack/RackEditor.h:96-106`).

**DECIDED (Frosty, 2026-09-20): tied but not the same.** Dwell adds a small
on-panel arrow, absent from DEQ, that opens and closes the expanded view — the
panel must be able to request the host's expand flag, a touch point beyond
the mechanism above. `fx` (id 17, the FX switch below) stays the sound;
clicking it on from compact opens the view once, then the arrow alone toggles
it, touching no parameter.

**Consequence for Dwell: the module can only grow sideways.** The mechanism is a
second *width*; there is no `expandedHeight`. So the FX section is a **right-hand
column, not extra rows**.

- **Compact 280** (the rack default) carries the **nine-control face**, FX among
  them as one `ui::SwitchButton` — a switch, so it reads as a state, not a knob.
  Lit in the accent. *(The "eight rows exactly as drawn" this said is §2's
  superseded layout.)*
- ~~**Expanded 460** (280 + 180) … an FX column running beside rows 5–8.~~
  **CORRECTED, twice.** One FX column was never going to hold two FX sections;
  it went to 980 across three columns on 2026-09-22, and the 2026-09-23 pullback
  then removed the lane's second voicing and LINK, so **the width is open
  again**. `modules/dwell/Module.cpp` carries whatever it currently is.
- **Caption per type.** `Percent` is the readout throughout, with the caption
  line naming what the percent moves. **CORRECTED 2026-09-22 — there are three
  types and three captions**: `AMOUNT (smear)` Diffuse, `(depth)` Pan/Tremolo,
  `(bits)` Crush. `(blend)` for the octaves and `(seam)` for Reverse went when
  those were cut on 2026-09-21; **`(sweep)` went with Sweep on 2026-09-22, and
  Sweep went because VOICE did** — it swept VOICE's resonant centre, and there
  is no such filter any more (`10` §11a). One format, three captions — all free
  later (§7).
- With FX off the column greys (`setKnobEnabled`) rather than vanishing, so the
  width never changes underneath a user; the DSP stage is skipped regardless
  (10 §11a).

## 7. Permanent once shipped

Permanent: plugin code, module id, bundle id, preset extension, accent (the
`products/AGENTS.md` row), panel width, and every parameter's id, **order**,
range, step and default — including the NOTE strings and their order (choices
store as indices), SYNC as its own bool rather than positions inside NOTE,
**SEND, HOLD and CHOP as plain bools and LANE GAIN as a bipolar float rather
than a three-way choice** (corrected 2026-09-22 — it was THROW and FREEZE), and
MIX's dry-held law, baked into every saved value.

Free later: row heights, face sizes, captions and lifts, legends, readout
wording, the 50 tick's label, meter scale and rate, latch behaviour, whether the
self-oscillation stretch is drawn, and anything derived from the accent. *(The
BUILD ramp is not among them: there is no ramp to draw.)*

## 8. Open decisions — all three are closed

1. ~~VOICE as a concentric ring on HIGH CUT, or a fourth knob row?~~ **Closed by
   removal**: VOICE is cut (`15`, README item 18).
2. ~~Accent `#b2bb54` — or does the de-esser get first refusal on the gold gap?~~
   **Closed**: the gold was rejected, the de-esser took the rose near 4°, and
   Dwell has the orchid `#f094e6` (§6).
3. ~~Does THROW latch on modifier-click, or stay strictly momentary?~~ **Closed
   by removal**: THROW is gone. The question survives in a different shape for
   **SEND**, which is meant to be automated a word at a time, and is a panel
   decision rather than a schema one — **flagged, not decided here**.
