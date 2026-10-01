# BMO Linger — UI pass

**On AURORA**, 2026-09-22, branch `frosty-add-bmo-linger`, rebased onto
`d77723f`. Windows only. **Nothing in this module has been heard**: the DSP is a
marked pass-through, so every figure below is rendered or measured, never
listened to.

This note says plainly which is which. Where something was only reasoned about,
it says so rather than implying a measurement.

## Measured

### BMO Opto's hashes, re-proven

`core/ui` changed by **838 insertions across six files** on this branch — a new
`ui::Fader`, `ConcentricBand::capDiameter`, and additions to `LookAndFeel.h` and
`ModulePanel`. The house rule is that BMO Opto's three hashes guard shared UI
code and must be re-proven whenever `core/ui` moves.

Re-proven twice: once before the rebase and once after.

| render | hash | expected |
|---|---|---|
| `opto signal=-18` | `ab3ff3b77116b7a5` | `ab3ff3b77116b7a5` ✓ |
| `opto appearance=light signal=-18` | `878cca7b1a80a551` | `878cca7b1a80a551` ✓ |
| `opto signal=-18 ui.meter=GR` | `88a7653a82c19ae0` | `88a7653a82c19ae0` ✓ |

**All three exact.** 838 lines of shared UI changed and not one pixel of another
module moved.

The three need three separate render commands — `signal=-18` throughout, and the
GR figure additionally needs `ui.meter=GR`. A bare render reproduces none of
them. Built the harness with
`csc -out:Inspect.exe -r:System.Drawing.dll Inspect.cs`; `hash` is a truncated
SHA-256 over the pixels.

### BMO Linger's own pages

760×1480 each, which is 380×740 at the tool's 2x.

| page | hash |
|---|---|
| `ui.page=early appearance=dark signal=-18` | `718ef841b030c2a9` |
| `ui.page=tail appearance=dark signal=-18` | `22d218738ea7f6cb` |
| `ui.page=eq appearance=dark signal=-18` | `cddd58df5a1eef86` |

**The EQ page needs `signal=-18` or its analyser renders empty.** It is the one
thing on this module that is not parameter-driven. Recorded here because a
reviewer who omits the flag will see an empty screen and think it is broken.

### The height budget, against the 680 a panel's content area has

| block | px |
|---|---|
| bezel (24 well + 258 screen + 4 + 16 readout) | 302 |
| segment row | 26 |
| two knob rows at 79 | 158 |
| rule | 16 |
| level strip | 134 |
| **sum** | **636** |

Four even gaps of 11. The screen's drawing area is 258 − 26 = **232 px**,
against 105 before this pass.

`ModulePanel::kContentHeight` is 688 suite-wide and `RackEditor` sets every
panel to it, so the module's total height cannot change and did not. Everything
above is reallocation inside it.

### Control geometry

- Knob cap **27.6 px** throughout, including the FILTER ring, whose measured cap
  is **27.65** — the face scale is derived from the cap rather than fixed, and
  0.35 needs 78.86 px where bounds are integers.
- Knob component **50 px**, not 46. See the defect below.
- Filter ring component **120×79**.
- Segment row **270 px**, deliberately narrower than the 360 px column grid, so
  LOW / MID / HIGH cannot read as headings over FREQ / GAIN / Q.
- Tightest caption margin: **REVERB, 5.4 px** in the strip's 80 px column. The
  suite already ships one at 1.8.
- Menu words clear their segments by 74–90 px; segment words by 43–59 px.

### Tail reporting

Asserted as absolutes in `tests/plugin/TailTests.cpp`, not measured by ear:
defaults **2.2891 s**; 125 ms / 4 s / 1.50× / 24 m → **6.3332 s**; damping
floored → **3.1291 s**; 20 s decay → **20.1291 s**; worst case clamps at
**30 s**. Rack sum over two slots **8.6223 s**, and the rack clamps at 30 s.

Every other shipped module is asserted to report **exactly 0.0** — at defaults,
at each parameter's extremes individually, and with the whole schema pinned to
each end.

## Defects found, older than this pass

**`kContentHeight` was shadowed.** `ReverbPanel`'s file-local constant was hidden
by the inherited `ModulePanel::kContentHeight` of 688, so the gap arithmetic went
negative and every gap silently fell back to a `switchGap` floor — on the old
face as well as the new one. Renamed `kFaceHeight`.

**A 46 px knob box cannot hold its own track.** A 27.6 cap plus `trackGap` plus
the dot needs radius 23.8 inside a 23 px half-box, so **the top dotted-track dot
has been drawn on the component edge in every module in the suite**, all along.
Linger's knobs are 50 px, which is the smallest box that holds it. *Not fixed
elsewhere* — that is a suite-wide change and belongs in review.

**`snapshot` appended instead of truncating.** Fixed earlier on this branch;
`tests/tools/SnapshotIoTests.cpp` holds the line. Before the fix, re-rendering
over a path left the old image in place and two sessions reviewed stale panels.

## Reasoned about, not measured

- **Every character judgement.** Room is the only type whose constants are real;
  the other five carry `CALIBRATE` placeholders that claim nothing but their
  ordering. Cavern, Chamber, Plate and Ambience have names and a shape to fill.
- **ER MODE's Blend position** is defined on paper and unheard.
- **The three screen drawings** are judged on renders, not on whether they help
  a mix. The tail's three curves were chosen because a single envelope showed
  none of that page's controls — that is an argument, not a listening result.
- **The analyser shows the dry input**, because the DSP is a pass-through and
  that is the only signal there is. It is wired to the point the Reverb EQ acts
  on, so it will be correct when the DSP lands.
- **MOD DEPTH and MOD RATE are not drawn anywhere.** Deliberate: the modulation
  is eight incommensurate smoothed-*random* delay modulators bounded to 3 cents,
  so a ripple would imply a periodic wobble that does not exist, in the wrong
  domain — 0.28 ms on a 1.8 s tail is 0.015%. If it is ever wanted on screen,
  numbers are the honest form.

## Not covered

**macOS is entirely untested.** Every figure here is Windows, on AURORA. CI has
never run on this branch — it has never been pushed.
