# BMO Linger M2 — where it stands, 2026-09-23, on AURORA

Written at a pause, for the session that picks M2 up next. Read it after
`docs/reverb/HANDOFF-linger-dsp.md` (the revised copy is PR #26,
`frosty-linger-dsp-handoff`). Nothing has been heard. Nothing on either M2
branch has been pushed.

## The two branches

M2 was built as two halves joined by one contract, `modules/reverb/dsp/ErTable.h`
(commit `6080dc0`).

| Branch | Worktree | What it holds | State |
|---|---|---|---|
| `frosty-linger-er` | `../bmo-mix-rack-333-lingerer` | the ER engine, bench, hash tool | done and verified; this note |
| `frosty-linger-er-tables` | `../bmo-mix-rack-333-lingertaps` | image-source tables, audits, pinned `ErTableData.inc` | Plate re-voice and Cavern B in progress at the pause; see its own note, `testing-notes/linger-m2-tables-2026-09-23.md` |

The tables branch merges into `frosty-linger-er` and is never PR'd on its own.
Both build and test in a DSP-only tree (`-DBMO_DSP_ONLY=ON`), which has no
plugin products and so cannot install anything.

## Verified on AURORA by the orchestrator, not taken from agent reports

- **Engine, `frosty-linger-er` at `16feda3`.** A clean rebuild exited 0: DSP-only
  18/18, and 5/5 for reverb_dsp, reverb, tail, ui_layout and rack in the full
  tree (named targets only).
- **Var 0–5 are bit-identical across the mono-null change.** Verified by building
  `3c4bb39` in a separate worktree and comparing `measure_reverb hash`: the
  combined hash is `9d2b36f5e9af1e98` on both; Var 6 moved from
  `889a9d8f5231633c` to `6bf1a65e0372ac03`.
- **Bench**, Release, 128 block, 60 s noise, median of 5, 48 / 192 kHz:
  - worst case 0.578 / 2.355 %
  - Var 6 0.398 / 1.588 %
  - Energy mode 0.576 / 2.318 %
  - crossfading every block 1.019 / 4.085 %
  - DENSITY moving 0.907 / 3.653 %

  Every row is inside the 1.5 / 5 % budget. Memory is 290.5 kB at 192 kHz.
- **Spot breaks.** Density ramp made a switch: 2 red. A Room tap moved 0.55 ms
  in the pinned data: pin and Room γ red. Plate's first tap delayed past 1 ms:
  onset, band order and pin red. All green again once restored.

## Owner decisions, 2026-09-23

- **VARIATION 6 is "mono null"**: the ER go into the side only, L = +E and R = −E,
  as in BMO Dimension. A mono instance at Var 6 has no ER. Implemented and
  verified. The comb fields in `ErTable.h` are now dead; remove them at
  integration.
- **Plate is a physical metal plate, not a room.** No room rules apply to it.
  Measured EMT 140 data (16 IRs, research doc section 8) replaced the estimates:
  energy swells 10–13 dB to a peak at 10–25 ms, with ≤4 % inside 5 ms. It is
  being re-voiced to that.
- **Cavern's flam (i) failure is an ear call.** Measured stone spaces never break
  the rule; ours is 4–7 dB hotter than Hamilton Mausoleum. A tool-only
  "Cavern B" (cluster at about −17 dB) is being built to compare by ear. The
  plugin keeps the current Cavern until the owner picks.
- **Hall's early first reflection** (10.7 ms full-band, against 15–30 measured) is
  an M4 fit. No concert hall has been measured yet.
- **Low-mid width**: real stone spaces carry LF 0.02–0.08 at 125–500 Hz, so the
  gap is not unusual. Halls are untested.

## Still open

- **Density level above 60 %**: 0.24–0.29 dB against a spec of 0.2, measured on
  the stand-in table. The prepare-time correction the owner approved moved it
  under 0.01 dB and was not committed (the patch is in the session scratchpad
  only). Re-measure on the real tables first. The recommendation is to accept
  ~0.3 dB and write the figure into `10`/`11`, rather than pay audio-thread
  cost for an inaudible 0.1 dB.
- **M3 flags**:
  - Plate's attack constant of 0 is contradicted by the measured swell.
  - MOD DEPTH at 0.8 ms × 1.2 Hz is ~10.5 cents per pass as a sine, against the
    3-cent bound.
  - The 30 s tail ceiling against 40 s at decay 20 s × damp 2.0.
  - The MIX law. The engine's linear law is provisional.

## Next session, in order

1. Verify the tables branch's last commits (clean rebuild, audit, spot-break
   Plate and Cavern B), or finish whatever its note says is uncommitted.
2. Integrate: merge the tables into `frosty-linger-er`; remove `combDelayMs` and
   `combGain`; make the table-side Var 6 audit "mono sum exactly zero"; clamp
   every type's span to its window at every size (Ambience overruns at 12 m);
   fix `11` §6's "γ ≥ 0 at all seven VARIATION positions" to 0–5; re-measure
   the density level; point the panel's scatter at `erTableFor` and check the
   renders.
3. Build the listening set in the gitignored `packages/reverb-listening/`, from
   Frosty's dry sources in `Documents/BMO-reverb-refs/03-dry-sources/`. Include
   Cavern vs Cavern B, and Blend. Then stop for Frosty.

The shared research doc is "Reverb Research: Spec Gaps",
https://claude.ai/code/artifact/278dd52c-4dd1-45cd-a068-057725d70314. Section 5
is this work; section 8 holds the measured reference IRs.
