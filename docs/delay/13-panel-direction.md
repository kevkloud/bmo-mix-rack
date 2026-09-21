# BMO Dwell (`Bdly`) — panel and UI direction

Written on AURORA on 2026-09-20. Direction only, no implementation. Evidence:
`docs/delay/00-repo-conventions.md`, `docs/delay/10-dsp-spec.md` (cited 00/10),
`core/ui/Tokens.{h,cpp}`, `core/ui/ModulePanel.h`, `core/ui/Controls.h`,
`modules/sat/panel/SatPanel.cpp`, `modules/vcomp/Module.cpp`. No control is
labelled DWELL. The creative controls arrive with a matching amendment to 10.

## 1. Control hierarchy

- **Performance:** THROW and FREEZE — two wide lit buttons on their own row
  under the primary trio, both shipped enabled in v1 (DECIDED, Frosty
  2026-09-20). Reachable without hunting, lit from across the room.
- **Primary (largest faces):** TIME/NOTE, FEEDBACK, MIX. Time, how long it
  lasts, how much you hear.
- **Secondary (two trios):** DRIVE, MOD RATE, MOD DEPTH; then LOW CUT, HIGH
  CUT (+ VOICE), DUCK. DUCK is the one non-loop control in the loop section.
- **VOICE** — the creative filter voicing (10 §11's name) — rides as a `ui::ConcentricBand`
  ring on HIGH CUT rather than taking a fourth row: the repo already hangs a
  second value on one position that way, the voicing belongs to the cut it
  colours, and a fourth row costs 100 px the middle lacks. Its legend prints
  the voicing, not a number.
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

Width **280** (BMO EQ's; 260 content). `ModulePanel` gives 688 design px: 94
in, 126 out, **468 in the middle**. Slack re-centres, per `modules/sat`.

| # | Row | px | Contents |
|---|---|---|---|
| 1 | rule | 16 | legend **DELAY** |
| 2 | primary trio | 148 | TIME/NOTE (+ SYNC), FEEDBACK, MIX |
| 3 | performance | 32 | THROW, FREEZE — wide lit buttons |
| 4 | switch row | 28 | CLEAN / TAPE / BUCKET |
| 5 | rule | 16 | legend **LOOP** |
| 6 | trio | 100 | DRIVE, MOD RATE, MOD DEPTH |
| 7 | trio | 100 | LOW CUT, HIGH CUT (+ VOICE ring), DUCK |
| 8 | switch row | 28 | STEREO / PING-PONG / DUAL |

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

THROW and FREEZE are ordinary bools in the permanent schema. Everything here
is panel behaviour over that one bool.

- **Mouse:** true momentary. Down sends 1, up sends 0; release also fires on
  drag-out, deactivation and focus loss, so nothing is stranded lit.
- **Automation:** the host writes the same bool, so a drawn envelope behaves
  like a press. The panel follows the parameter, never the reverse.
- **Latch:** modifier-click (or right-click menu) holds it on. Latch is
  panel-only, never in `PARAMS`; presets, `prepare` and bypass force 0.
- **Lit state:** accent at full strength with a glow, not the ordinary switch
  tint — must not read as a CHARACTER/STEREO selection.
- **BUILD:** while THROW is held, feedback ramps from the knob's value toward
  the self-oscillation stretch and falls back on release. The knob does not
  move (the parameter stays authoritative); the ramp shows as a travelling
  highlight on its track.
- **FREEZE ships enabled in v1**, its own button and slot, never folded into
  THROW's travel (DECIDED, Frosty 2026-09-20); order is permanent.

## 4. Captions and readouts

`ParamFormat` covers almost everything: `Milliseconds` (TIME); `Percent`
(FEEDBACK, MOD DEPTH, MIX, DRIVE, VOICE — DRIVE matching BMO Saturator);
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

Recommended: **one GR lane for DUCK**, reusing `ui::DynamicsMeter` in its
gain-reduction mode, right of row 7. Ducking is the only thing here you cannot
hear as itself, and it needs no new plumbing — it reads the
`currentGainReductionDb()` hook already on `bmo::ModuleDsp`: an atomic scalar,
no analyser tap, no lock. The same lane can carry the BUILD ramp.

Rejected: a tap/echo-decay display. It wants a repeat model on the UI thread
and new state crossing threads, and the tail is audible already.

Cost: **15 Hz**, as `modules/opto`, `modules/util` and `modules/tune` do,
rather than the 30 Hz in `core/ui/Controls.cpp` — half the repaints, ample for
a 180 ms release, quick enough that a held THROW lights without lag.

## 6. Accent

Dark plate `#2e2e32`, pale `#efefef`. Shipped accents run 5.87–7.19:1 dark and
1.72–2.00:1 pale — relative luminance ≈0.41–0.49.

| candidate | hue | dark | pale | note |
|---|---|---|---|---|
| **`#b2bb54` olive-gold** | 65° | 6.5:1 | 1.83:1 | widest free gap, 33° clear of Saturator |
| `#f79a8e` coral | 7° | 6.4:1 | 1.86:1 | only 25–31° from Saturator and EQ |
| `#f094e6` orchid | 306° | 6.5:1 | 1.84:1 | 30° from EQ's pink; may read as EQ in a rack |

`11-integration-and-test-plan.md` §1 derives a different three (`#f0938c`,
`#e694e0`, `#e6e278`); six candidates in all, and one accent is chosen once.

**Recommend `#b2bb54`.** `core/ui/Tokens.h` rejected a gold at 1.41:1 pale —
that was a *light* gold; darkened into the band the objection lapses. Render
panel and rack, both appearances, THROW lit, before allocating.

## 6a. The FX section, and how expansion works here

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

**Consequence for Dwell: the module can only grow sideways.** The mechanism is a
second *width*; there is no `expandedHeight`. So the FX section is a **right-hand
column, not extra rows**.

- **Compact 280** (§2, the rack default) keeps the eight rows exactly as drawn and
  adds **FX** as one `ui::SwitchButton` at the right end of row 4, beside the
  CHARACTER trio — a switch, so it reads as a state, not a knob. Lit in the accent.
- **Expanded 460** (280 + 180, a multiple of 20 as DEQ's two widths are; the
  standalone default) keeps rows 1–8 at their compact geometry and re-centres the
  180 px gained into an FX column running beside rows 5–8, under its own rule and
  legend **FX**: a vertical list of seven `ui::SwitchButton` cells for `fxType`
  (70 px each, the row maximum does not apply down a column), then one secondary
  64 px knob, **FX AMOUNT**, under them.
- **Caption per type.** `Percent` is the readout throughout, with the caption line
  naming what the percent moves: `AMOUNT (smear)` Diffuse, `(bits)` Crush,
  `(blend)` either octave, `(seam)` Reverse, `(depth)` Pan, `(sweep)` Sweep. One
  format, seven captions — all free later (§7).
- With FX off the column greys (`setKnobEnabled`) rather than vanishing, so the
  width never changes underneath a user; the DSP stage is skipped regardless
  (10 §11a).

## 7. Permanent once shipped

Permanent: plugin code, module id, bundle id, preset extension, accent (the
`products/AGENTS.md` row), panel width, and every parameter's id, **order**,
range, step and default — including the NOTE strings and their order (choices
store as indices), SYNC as its own bool rather than positions inside NOTE,
THROW and FREEZE as plain bools, and MIX's dry-held law, baked into every
saved value.

Free later: row heights, face sizes, captions and lifts, legends, readout
wording, the 50 tick's label, meter scale and rate, latch behaviour, whether
the self-oscillation stretch or the BUILD ramp is drawn, and anything derived
from the accent.

## 8. Open decisions

1. VOICE as a concentric ring on HIGH CUT, or a fourth knob row (100 px + rule)?
2. Accent `#b2bb54` — or does the de-esser get first refusal on the gold gap?
3. Does THROW latch on modifier-click, or stay strictly momentary?
