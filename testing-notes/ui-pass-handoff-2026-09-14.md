# The UI pass — session brief

**For the session that takes on the UI pass. Read this, then
`ui-pass-checklist.md`, then `docs/ui-workflow-brief.md` and
`testing-notes/ui-editor-handoff.md`.** Prepared on **AURORA**, 2026-09-14.
The checklist says *what* to look at, panel by panel. This says where
everything is, what is already decided, and what you are not allowed to
decide yourself.

---

## 1. Everything is ready

| | |
|---|---|
| **Branch** | `ui-pass`, off `integration` at `e4082c3`. Worktree `../bmo-mix-rack-333-ui`. Nothing on it yet but this file. |
| **Installed on AURORA** | all nine VST3 bundles from CI run **34891007014**, version **0.2.4**, hashes in `install-0.2.4-2026-09-14.md`. What is installed is what this branch is off. |
| **Renders** | already made, in `snapshots/` of this worktree (gitignored, so they are on AURORA only): every panel in **both appearances**, plus DEQ compact *and* expanded. Rendered from `integration`'s Release build with `signal=-18` so meters read. |
| **Build to render with** | `../bmo-mix-rack-333-int/build-release/tools/Release/snapshot.exe` and `.../tools/tune/Release/bmo-tune-snapshot.exe` — already built, no need to build this worktree until you change code. |

Re-render everything:

```
S=../bmo-mix-rack-333-int/build-release/tools/Release/snapshot.exe
for m in eq sat util opto dim deq vcomp rack; do
  for a in dark light; do "$S" $m snapshots/$m-$a.png appearance=$a signal=-18; done
done
"$S" deq snapshots/deq-compact-dark.png  appearance=dark  signal=-18 view=compact
"$S" deq snapshots/deq-compact-light.png appearance=light signal=-18 view=compact
```

**`view=compact|expanded` is the flag for DEQ's two widths, not a size
override.** A single DEQ opens *expanded*, so a bare `snapshot deq` gives you
the 600; `view=compact` gives the 320 a rack shows. Passing `600 420` as width
and height just stretches the compact panel and looks plausible — that mistake
was made while preparing this and the files were thrown away.

Tune is a separate tool and takes no module argument:
`bmo-tune-snapshot.exe snapshots/tune-dark.png appearance=dark`.

---

## 2. Triage — this is the part the checklist does not do

The checklist is one flat list. It is not: a third of it is Frosty's to decide,
a third is yours to implement, and one item is a product decision that changes
the schema. Sorting them is most of the planning.

### A. Blocked on Frosty — do not decide these yourself

Gather the evidence, render the options, put the numbers in front of him.

- **`utilGain`** — three options with measurements already in the checklist.
  His call, and option (1) needs no new colour.
- **Vcomp's names** (AMOUNT, LOW THRU, HIGH THRU, SC HPF, COMPLEX) and the
  **accent `#a2a8ff`**, which is permanent once shipped.
- **Opto and Vcomp disagreeing about the output section.** Two compressors in
  one suite should agree; which way they agree is a taste call.
- **Dimension's lavender captions at 1.73:1** on the pale plate. Raised
  before, still undecided.
- **DEQ's solo and analyser** — wire them in, or say in `spec/decisions.md`
  that they wait. Either is defensible; shipping a decisions file that reads
  as if they exist is not.
- **Tune's empty middle third.**
- **EQ's four parameters with no control** (High Cut, Mix, Auto Gain,
  Oversampling) — intended or not, on the record.
- **The rack's pink header** (it is EQ's accent).

### B. Yours to implement

- **BMO EQ → BMO CEQ rename.** The biggest single item and the one with a
  trap: it needs the **second preset-migration hop**, and the 0.2.4 review
  spelled out the design (`PresetInfo` grows a list of legacy
  (folder, extension) pairs walked oldest first and without overwriting; drop
  `migrateLegacy`'s "only if the new folder is empty" gate, which strands
  anyone who saved one preset before the copy ran; `products/eq/Product.h`
  lists BMO EQ then FrostyEQ). **The sandbox the tests use disables migration
  entirely, so the chain is untested today** — that is the real work here, not
  the rename.
- **Contrast assertions** — a pure function of the tokens, no rendering. The
  cheapest open item in the whole brief; do it first and it guards everything
  after.
- **Dimension's L / R end marks** on ROTATE and ASYM (`dim-ui-pass-plan.md` §1).
- **Dimension's DETUNE switch scope** — `PlainKnob::setKnobEnabled` exists and
  is unused suite-wide; this is its case.
- **Vcomp takes the output section**, and the freed height goes to the meter
  block.
- **Vcomp's IN caption hit-test** (clicking the caption sets the gate to −60;
  hit-test the well, not the row).
- **DEQ's GR bar** label or redesign — it shows the deepest cut and an upward
  band shows nothing.
- **Rest-dot check** on every knob whose default is not at an end.
- **Caption fits** in both appearances.

### C. Not a panel decision

- **Dimension's output trim.** A new parameter is a schema append — at the end
  of `specs()`, default 0, `kVersionHint` bump. The DSP review measured
  +3.5 dB peak on Wide Vocal and +19 dB reachable, so the case is real, but it
  is a product decision and it does not belong in a UI pass unless Frosty asks
  for it here.

---

## 3. Rules you inherit

- **The schema is frozen.** A new parameter goes at the end of `specs()`,
  never in the middle. Parameter IDs, their order, ranges, steps and defaults
  are permanent (root `AGENTS.md`).
- **No hex outside `core/ui/Tokens.h`.**
- **Every ratio you quote must be against a named ground, from
  `tools/inspect`** — not from reading a render.
- **Both appearances, every time.** Half the open items in the checklist are
  light-mode only.
- **`ui_layout_tests` must stay green**; it pins the shared rows. Note that it
  only checks panels that are in its product list — a green suite after adding
  a module is not evidence the module was tested (that is how Vcomp shipped
  with clipped captions).
- **Hash renders before and after any change that should move nothing.**
- **Name the machine** on every result you record.
- **Check `git status --short` before `git add -A`** — root `AGENTS.md`,
  "Before your first commit: never commit audio". `snapshots/` is gitignored;
  keep it that way.

---

## 4. Where the rest of the work stands

So you do not re-derive it:

- **0.2.4 is merged and green.** `integration` at `e4082c3`, CI run
  34891007014 green on all four jobs, full Release ctest 26/26 on AURORA.
- **Frosty's per-module Ableton pass has NOT happened yet** — he could not
  give his ears on 2026-09-14. Stage 3 nominally runs after it. The UI pass
  can start regardless; just expect his listening notes to arrive mid-pass and
  possibly add items.
- **Two candidates were heard and rejected** this session: Tune's phrase-end
  fix (lost both Fuji groups) and Opto's attack candidates (three rounds, no
  distinguishable difference). Neither is merged. Do not resurrect either as
  part of a UI pass.
- **Open elsewhere, not yours:** Fuji has 5 of 11 splices over 0.5 landing
  error where Failure now has none, which is the real remaining Tune target;
  `OptoDspTests` does not pin the attack at all; the Vcomp THRU presets are
  still at AMOUNT 35 pending Frosty's ear.

## 5. Done means

`ui-pass-checklist.md` §C, plus: `testing-notes/ui-pass-<date>.md` with
before/after render hashes and **Frosty's calls recorded at the call sites**,
not only in the note.
