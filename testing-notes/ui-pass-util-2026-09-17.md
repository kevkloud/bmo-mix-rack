# The UI pass — BMO Util, module 6

**On AURORA, 2026-09-17.** Branch `ui-pass`, worktree
`../bmo-mix-rack-333-ui`, starting from `3b52707` after BMO Opto. Two commits,
both local, nothing pushed and no CI. Frosty drove it from his phone over
Remote Control; every call below was made on renders in both appearances.

Read `ui-pass-render-loop.md` for the tools.

---

## 1. Where it ended

| | dark | light |
|---|---|---|
| before | `1225e16081194ecf` | `179fe57b060ba655` |
| after `db63acb` (PAN, MONO) | `73ec14fc9a80d9ee` | `3ab6cb928ba513b9` |
| **after `a2e5a63` (final)** | **`ce603457cbf3bc73`** | **`ee71528c39941701`** |
| final, `mono=1` | `7b1c566c8df65964` | `f91161aa5277371a` |

`signal=` moves nothing on this module and never has: it has no meter, so a
bare render and `signal=-18` are byte-equal. The baselines above are bare.

Largest bare band, dark: **46 px at 520** before, **38 px at 0** after — the
worst band is now the air above VOLUME rather than the plate under MONO.
Against the suite: EQ 21, Util 38, Opto 48, Dimension 51, Saturator 66.

BMO EQ, the Saturator, BMO Opto and Dimension hash unchanged, which is the
check that mattered here because two of the four changes are in `core/ui`.

Tests: `ui_layout`, `util`, `util_dsp`, `rack`, `deq`, `dim`, `opto`, `eq`.

## 2. What changed, and why

**PAN's ends read L and R** (`db63acb`). They were a minus and a plus, which
says hard left is less than hard right. `Knob::EndMarks::leftRight` already
existed from Dimension's TURN and TILT.

**WIDTH keeps its minus and plus.** Frosty's call, and it is the interesting
one: Width runs 0..200 and rests at 100, so it reduces *and* increases around
its rest position, which is what `lessMore` is for — and the rest dot on the
track already marks where that is. A dot at the minimum was considered and
would have put two dots of different meaning on one knob.

**WIDTH dims while MONO is on** (`db63acb`). `UtilDsp` sums to (L+R)/2 before
the mid/side stage, so with MONO engaged the side signal is zero and turning
WIDTH does nothing at all. Polled at 15 Hz like BMO Opto's mode, so host
automation and a preset land where a click does. MONO itself is never dimmed —
DEQ's pass settled that dimming the way in reads as a door locked.

**The dim is the full shipped one, caption included.** On the pale plate that
takes WIDTH's caption from 1.72:1 to 1.25:1, and a ladder holding the caption
at full strength (1.72:1, face dimmed alone) measured better and was **not**
taken. Frosty: a knob whose name reads bright while its face has gone pale
reads as a knob that has broken; the whole control fading says on purpose.
Recorded at the call site so it does not get "fixed" to the ratio.

**One knob row, 150, for all three** (`a2e5a63`). VOLUME had 126 against the
other two's 150 — the control the module is named for was the smallest thing
on the panel. The 24 px came out of the plate under MONO, 46 px down to 22.

**VOLUME and PAN print their values; WIDTH reserves the line and prints
nothing** (`a2e5a63`), the way Dimension's BLOOM does, so the three names stay
on one line. VOLUME reads `0.0 dB`, PAN reads `C`, `L 50`, `R 50`. No number on
WIDTH: its percentage names nothing a listener has a word for.

**The names sit 11 px higher** (`a2e5a63`), which is **measured, not chosen**:
face bottom to caption ink is 38 render px on BMO Opto's MAKEUP and 39 on LTV
Comp's, and this panel sat at 61. At a lift of 11 it measures 39 — LTV's
exactly, half a design pixel off Opto's. `PlainKnob::setCaptionLift` is new,
defaults to zero, and no other panel moved.

## 3. Rejected, with the numbers

| candidate | why not |
|---|---|
| caption undimmed, face dimmed alone (1.72:1 vs 1.25:1) | reads as a broken knob rather than a sleeping one — Frosty |
| a dot at WIDTH's minimum, plus at its maximum | the default is at centre, not the minimum; and the rest dot is already a dot |
| MONO moved down 8 px, rule left alone | evens the two bands at 39/38 but pulls MONO away from WIDTH, which it belongs with; superseded by the knob row |
| VOLUME at 160 | 14 px left under MONO |
| VOLUME at 170 | MONO on the rule, and a knob cannot pass 140 px on a 160 px panel anyway |
| rows at 155, lift 6 | buys 6 px of knob back by spending the foot gap down to 11 |
| caption lift 18 | the name is up against PAN's L and R |
| caption lift 12, in a six-slot rack | reads as a module drawn to a different rule from its neighbours |

**The lower rule never moved.** Frosty asked whether it lines up with the other
modules first: it does, `LayoutTests` pins it at row 566 with EQ's and the
Saturator's, and calls it one of three hand-matched alignments holding the rack
together. MONO moved instead — and then the knob row made even that
unnecessary. **The polarity pair needed no recentring either**: it already
centres in the reserved body by construction, 595..620 and 637..662 against a
body of 574..683.

## 4. The switch rows, settled

Frosty, 2026-09-17, on a six-slot rack render: **the placement is right and
Util does not move.** The checklist had it as an open item because Util's
polarity pair sits about 20 px below the row EQ and the Saturator put their
switches on.

Measured, it is not a misalignment but two different things:

| | switches | knob under them |
|---|---|---|
| BMO EQ (EQL, Ø, HI-Q) | 575..600 | OUTPUT 602..679 |
| Saturator (SAT, Ø, AUTO) | 575..600 | OUTPUT 602..679 |
| **BMO Util (ØL, ØR)** | **595..620, 637..662** | none |

EQ and the Saturator adopt the output section: a switch row with a trim knob
beneath it. Util reserves the section and adopts neither half — it has no
output stage — so the pair centres in the body the reservation leaves,
574..683. The alignment that actually carries the rack is the rule above them,
and that is row 566 on all three and pinned by `LayoutTests`.

## 5. Still open on Util

- **Light-plate captions at 1.72:1** — the suite-wide raw-legend question, the
  same one Dimension and DEQ are waiting on. Not a Util decision.
