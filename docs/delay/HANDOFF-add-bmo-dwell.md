# Handoff: add-bmo-dwell

Paste everything under the line into a new session opened on this repo. It
starts the build of BMO Dwell from the groundwork pack in `docs/delay/`. It
follows the shape BMO FET used: first commit a panel and a permanent schema,
DSP to follow. Written on AURORA, 2026-09-20.

Before starting: work in **its own git worktree** on a new branch
`frosty-add-bmo-dwell` off **`origin/main`** — never stacked on another
unmerged branch. Then merge the module's own docs branch,
`frosty-delay-groundwork`, into it so the pack is present (that branch is local
only; its worktree is `../bmo-mix-rack-333-delay`). If the build needs a small
shared hunk that lives on someone else's unmerged branch (for example
`textParam` in `core/state/ParamSpec.h` from the BMO FET skeleton), reproduce
it byte-identically and say so in the commit body, so either PR can land first.
Read `WORKFLOWS.md` — "The rules that apply to every workflow below" and "A new
worktree needs two things before it builds" — before the first build. Commit
locally; do not push or open a PR until Frosty says so.

---

You are orchestrating. You dispatch agents and you read their returns, the
README and a top-level listing; agents read the specs and write the code. Token
budget is tight: agents return the files they touched, <=150 words of bullets
and any blocking unknown — never pasted file contents. Opus for code and spec
questions, Sonnet for searches and mechanical edits.

GOAL
Add the delay module **BMO Dwell** (module id `dwell`, plugin code `Bdly`) to
the rack, built from `docs/delay/`. Start at `docs/delay/README.md`: its
"Decided" list is settled — do not relitigate it. `10` owns DSP meaning and
fixed values, `11` owns parameter ids, ranges and choice lists, `13` owns
layout and captions, `14` owns how CALIBRATE values get settled.

HARD RULES
- **The schema is permanent once shipped.** Parameter ids 0–19 exactly as `11`
  lists them, in that order. Choice lists are append-only after ship. Only the
  `fxType` candidate list may still change, and only before ship. No agent
  edits `params.h` after stage 1 without Frosty's say-so.
- **Never build or install the rack plugin target on AURORA** while the 0.2.5
  Ableton pass is on. Build the DSP-only tests and the snapshot / layout tools
  only, per `WORKFLOWS.md` "Building and testing" and "Looking at a panel
  without a 20-minute build".
- One agent per build tree. If a DSP agent and a panel agent run at once, the
  panel agent builds in its own `build-ui/`. Never two builds in one tree.
- A green ctest counts only after a build that exited 0 — a failed compile
  leaves the old test binary and ctest runs it. Check the build's exit code.
- Zero reported latency at every setting; the wet delay time is never latency.
- No hardware or software brand or product names in code, docs or UI strings.
  No control is labelled DWELL. Name the machine in every testing note.
- SYNC and NOTE ship **disabled**: slots and note-list order go in now, the
  feature waits for the tempo plumbing in `12`, which is its own workflow and
  its own PR — not part of this one.

ASK FROSTY BEFORE STAGE 1 (one message, then proceed)
1. Is the USPTO-only name clearance in `20` enough to commit the identity row?
2. Accent: `11` and `13` list six candidates between them. The de-esser has
   taken rose `#ea9f9a` (~4°), which rules out `11`'s red candidate `#f0938c`.
   Pick from the gold gap (`#b2bb54`, `#e6e278`) or magenta (`#e694e0`), and
   check it against both contrast rules the way BMO FET's accent was.

STAGE 1 — skeleton, identity, schema, panel (one commit)
Identity row and registration touch points per `11`; `modules/dwell/` with its
`AGENTS.md`; `params.h` with ids 0–19; a pass-through DSP that reports zero
latency; the compact panel per `13` with the FX button on the main panel, the
small expand arrow, and the 280 → 460 px expanded column via
`ModuleDef::expandedWidth` as BMO DEQ does it. The arrow needs one new touch
point: a panel asking its host to flip the session-only expand flag (`13` §6a).
Automation and preset changes to `fx` never resize the module. Layout tests and
a snapshot in both themes. Commit message in the house style: "dwell: BMO Dwell
arrives as a panel and a permanent schema, DSP to follow".

STAGE 2 — DSP, in this order, tests written alongside each step from `11` §4
a. Delay line, clean mode, feedback law, the dry-held-to-50% MIX law (dry path
   nulls bit-exactly at MIX 0, 25 and 50%), 2000 ms maximum.
b. Time-change laws, tape and bucket-brigade modes, in-loop LOW CUT / HIGH CUT,
   DC blocker, shaper, safety clip; self-oscillation bounded above ~97%.
c. **The lane** — a second engine with its own TIME, SEND gating its input, HOLD
   gating its life (off **clears**), CHOP gating its output, the bipolar LANE
   GAIN and LANE LEVEL, LINK and FX LINK; then ducking; then stereo modes.
   **Corrected 2026-09-22: THROW, BUILD, FREEZE and VOICE are cut** (`15`,
   `10` §11) — the main loop loses its `s` input gate entirely, and the proof
   test is that its output is bit-identical between a throw-held and a
   throw-never render.
d. In-loop FX candidates, cheapest first (Crush, Pan/Tremolo, then Diffuse),
   **one stage per engine, no shared state**. FX off must be bit-identical to
   the loop without the FX stage, **per path**. **Corrected 2026-09-22: Octave
   up, Octave down and Reverse were cut on 2026-09-21 and Sweep on 2026-09-22**,
   so nothing allocates a second buffer and the list is three.
Each step: build exits 0, tests green, sample-rate (44.1–192 kHz) and
block-size invariance, denormal / NaN / silence robustness, one local commit.

STAGE 3 — visual pass
Render, snapshot and inspect per `docs/1176-comp/11-integration-and-test-plan.md`.
Look at the render before measuring it. Record in `testing-notes/` with the
machine name.

STAGE 4 — calibration and listening
Run `14`: measurements first, then blind, level-matched rounds. Every FX
candidate is heard against Off, **per engine**; failures are removed from
`fx_type` before ship.
Freeze the values `14` §5 lists. Nothing here is "heard" until Frosty has heard
it on a named machine.

FINISH
Reply with: the commits made, what is measured versus heard, the CALIBRATE
values still open, and the decisions Frosty owes. Then stop.
