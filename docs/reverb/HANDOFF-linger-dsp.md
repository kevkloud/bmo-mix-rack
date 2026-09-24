# Handoff: BMO Linger's DSP

For a fresh session. Written on AURORA, 2026-09-22; revised on AURORA,
2026-09-23, after PR #25 merged. **The module exists and makes no sound.**
Everything a host touches is built, tested and merged to `main` (PR #25,
`68b1616`); what is left is the reverb itself.

Read this, then `modules/reverb/AGENTS.md`, then `10-dsp-spec.md` in full, then
`11-integration-and-test-plan.md` §6. `README.md` in this folder indexes the
rest. **The schema and the panel are settled — do not reopen them.**

## Where you are in the plan

`11` §7's milestones: **M0 skeleton, M1 panel and M5 shared code are done.**
M2, M3 and M4 are left, in that order, and **each is its own session**. The
UI pass ran one session per module and it worked; run this the same way. A
session ends when its exit test is green, or at the listening checkpoint, and
it updates this file before it stops: what is now real, what moved, and what
the next session inherits.

| | | |
|---|---|---|
| **M2** | ER generator | image-source tables, the Size law and its crossfade, order-banded filters, the diffuser, VARIATION, hi-cut, the Density bridge — tail silent throughout |
| **M3** | late network | FDN, absorbent filters, damping over the per-type knees, the Reverb EQ, pre-delay, SOURCE, modulation, plus the tail-onset and decay-truncation contours |
| **M4** | types | six constant blocks, era fields reserved |

`11` §6 holds the exit test for every one of them. **M2 is the milestone that
decides the module** — the spec says so, and it is where the thesis lives.

### M2 opens with the CPU worst case

`10` §8 says to measure the worst case **first**: DENSITY at 48 taps, three
diffuser stages, 192 kHz. Build that path before tuning anything and run
`measure_reverb bench` against `10` §6's budget (≤1.5% of a core at 48 kHz/128,
≤5% at 192 kHz, Release, median of five, on AURORA). If it does not fit, the tap
count or the stage count is the thing to argue about, and it is far cheaper to
argue before the tables are tuned than after.

### M2 ends at a listening checkpoint, not at M3

M2's exit test in `11` §7 includes "the ER-only listening items", and nothing
else in this module can be judged until the early reflections are. **When M2's
§6 block is green, stop.** Prepare a listening set in the gitignored
`packages/reverb-listening/` (check `git status --short` afterwards) covering
`11` §6's ER-only items: rap vocal at 15–25 ms, snare flam, drum room across
SIZE, acoustic guitar across DENSITY and ER HI-CUT, Ambience as a depth control,
and the mono sums at VARIATION 0 and 6. Write the set up in
`testing-notes/`, naming AURORA, and hand it to Frosty. M3 starts after Frosty
has listened, not before.

**Blend is heard at this checkpoint too**, not at the end. It is an ER-generator
mode, it exists by the end of M2, and its position count is permanent at first
ship — so the last moment to drop it cheaply is the first moment it can be
heard.

### M3 does not start until three decisions are made

These are Frosty's, they are open in `11` §7, and M3's tests depend on them:

- **The MIX law and its default.** `11` §6's level-law test says "pin one MIX
  law, test to ±0.1 dB". There is nothing to pin until it is chosen.
- **The 30 s tail ceiling against a 40 s tail.** `decay` reaches 20 s and
  `damplo`/`damphi` reach 2.0×, so any setting with `decay` × the larger
  multiplier above about 30 s rings longer than `kMaxTailSeconds`
  (`core/dsp/ModuleDsp.h`), and `tailSecondsFor` clamps to 30. `11` §6 asks for
  the report to be both **≥ measured** and **≤30 s** at every type, which cannot
  hold at that corner, and its own stability test drives exactly that corner
  (`decay` 20 s, `damphi` 2.0). Pick one: clamp the effective T60 in the engine
  at the ceiling; restrict "≥ measured" to settings under it; or accept an
  under-report at the corner and write it down. Then fix `11` §6 to match.
- **`inhicut` as a parameter or a constant** (`11` §4d), and **whether ER
  SPREAD greys out in Taps mode or sits inert**. Neither blocks the engine, but
  both are cheaper to settle before the panel is wired to real sound.

### M3 builds everything off `kNumLines`

`10` §4 already records Plate failing its modal-density target at eight lines,
and `10` §8 says the count "may have to rise to 16". `DspCore::kNumLines` is
already the one place the count lives. Keep it that way: the Householder
matrix, the prime delay tables, the absorbent filters and the memory budget are
all sized from it, never from a literal 8, so that M4 raising it to 12 or 16 is
a one-line change plus a re-run of the budget rather than a rewrite.

## What is already real, so you do not rebuild it

- **The schema is frozen at 30 parameters**, order permanent, two host lanes
  spare. `params.h` is the authority and `tests/plugin/ReverbTests.cpp` holds
  it as a golden table.
- **`DspCore::Params` already carries every field the real DSP needs, in real
  units**, and `ReverbDsp::paramsFrom` unpacks the flat `float*` into it. That
  shape was built for you; fill it rather than change it.
- **The per-type block is fourteen constants**, five of which have no host lane
  at all — the engine reads those straight off the row. `TypeConstants` and
  `constantsFor` are in `params.h`.
- **The analyser tap is already at the point the Reverb EQ will act on.** Until
  there is a wet signal it shows the dry input, which is correct and is
  commented as such. **Do not move the tap to fix it.**
- **Latency is zero and that is the shipped figure**, not a placeholder.
- **Tail reporting is live** — `tailSecondsFor` in `DspCore.h` is the only copy
  of the formula, asserted against hand-written seconds in `TailTests`. When
  the real DSP lands, the measured −60 dB time must be **≤** what it reports.

## What is decided and must not be reopened

Identity, accent `#e694e0`, the six types and their order
(Room · Chamber · Hall · Cavern · Plate · Ambience), the 30-parameter schema and
its order, the four-position FILTER, the panel and its three pages. All of it
cost a long session with the owner and most of it is permanent at first ship.

Two things are permanent in the strict sense — changing them after release
remaps recorded automation: **the count of `type` (six) and of `ermode`
(three)**, because `juce::AudioParameterChoice` normalises as index/(n−1).

## What is NOT decided, and is yours

**Room is the only type whose constants are derived** — traced to the spec,
not heard; nobody has listened to them either. Every other value in
`kTypeConstants` is marked `CALIBRATE` and claims nothing but its ordering.
Chamber, Hall, Cavern, Plate and Ambience have names, a shape and no numbers.
Fitting them is M4 and it is a listening job, not a desk job.

**`ermode`'s Blend position is defined on paper and unheard** — image-source tap
times and pans from Taps, with the Energy generator's Shape/Spread envelope
replacing the physical gain law, energy-renormalised. If it does not survive
contact with ears, say so before first ship; the position count cannot change
after. It is heard at the M2 checkpoint above.

Frosty's decisions still open are listed under "M3 does not start until
three decisions are made" above — they are not yours to make, but they are
yours to ask for.

`10` §7's CALIBRATE rows are the full list of what has to be fitted rather than
derived.

## Where to work

**Branch fresh from `origin/main`.** PR #25 merged as `68b1616`, so
`frosty-add-bmo-linger` is finished history; do not continue on it. If an
earlier milestone's branch is still unmerged when you start, ask Frosty
whether to wait or to continue on that branch — never cut a new branch from it.

Give it its own worktree and its own build tree. Other sessions own the other
folders; reading is fine, writing is not.

**A new worktree has no JUCE submodule**, and `git submodule update --init`
fails with "transport 'file' not allowed". Use
`git -c protocol.file.allow=always submodule update --init libs/JUCE`.

## Build rules — every one of these comes from a real incident

- **Never run an untargeted build.** A Debug configure installs every plugin
  product after build, so a bare `cmake --build` overwrites the plugins in the
  system VST3 folder. It happened on 2026-09-20 mid-Ableton-pass. **Always pass
  `--target <names>`**, and do not write a command that starts one and kills it
  — I did that on 2026-09-22 and it was luck, not safety, that nothing installed.
- **Never build or install a plugin product target.** Test targets, `snapshot`
  and `measure_*` are fine.
- **A green ctest only counts after a build that exited 0.** A failed compile
  leaves the old exe and ctest runs it. Capture the code properly:
  `cmake --build ... > /tmp/b.log 2>&1; echo "EXIT=$?"`. **Piping to `tail`
  makes `$?` the exit status of `tail`** — that has fooled this project twice.
- **Establish your own baseline before you change anything.** It was 24/24
  with Defang merged on 2026-09-22, but FET (#22) and Linger (#25) merged after
  that, so the count is stale. Build the test targets, check the exit code, and
  record the count and the machine in your first note.
- **Re-prove BMO Opto's hashes if anything under `core/ui` changes** —
  `ab3ff3b77116b7a5` dark, `878cca7b1a80a551` light, `88a7653a82c19ae0` GR dark.
  They need three separate render commands, `signal=-18` throughout and
  `ui.meter=GR` for the third; a bare render reproduces none of them. Build the
  harness with
  `csc -out:Inspect.exe -r:System.Drawing.dll Inspect.cs` and use `hash`.
- **A shared hunk on an unmerged branch is reproduced byte-identically, never
  stacked on**, and the hashes are verified after copying rather than asserted.
  That discipline is why Defang's merge cost this branch twelve list entries
  instead of a fight over shared DSP.
- **Renders**: `snapshot` truncates correctly now, so re-rendering a path is
  safe. **The EQ page needs `signal=-18`** or its analyser draws empty.
- Never commit renders, audio or fonts. Name the machine in every note.
- Do not push or open a PR until Frosty says.

## How to work

Frosty runs these sessions orchestrate-only: dispatch agents, have each write to
files and return a short summary, never paste file contents back. **Verify every
agent's claims yourself** — rebuild, re-run, and look at the renders. On this
project agents have reported renders they never wrote, passed a suite through 23
wrong-coloured controls, and written a test that guarded a bug against repair.
Every one was caught by rebuilding and looking, and none by reading the report.

Assert absolutes, not comparisons — the house rule from
`tests/dsp/OptoDspTests.cpp`, where a relative test passed for a whole release
while both sides of it were wrong. Prove a new test non-vacuous by breaking what
it guards and watching it redden.

## The listening work, when there is something to hear

Nothing in this module has been heard. These are the questions that cannot be
settled from a desk, and Frosty has said he will bring references:

- **Ambience against Room at small SIZE.** Its ordinal rests on being
  distinguishable. He reaches for it often.
- **Chamber against Room and Hall at matched SIZE** — it was kept on the
  argument that small-and-reflective is off the SIZE diagonal.
- **Whether a scaled Hall still reads as a hall**, which is what cutting Large
  Hall assumed.
- **Plate at eight lines on a vocal.** `10` §4 already records Plate failing its
  own modal-density target at N=8; expect red until 12 lines or 2× τ̄.
- **Whether 3 cents of modulation reads as wobble on a held note.** If it does,
  `04` §3's time-varying orthogonal matrix modulation is the escape hatch — and
  a user has no MOD DEPTH to escape with, because it is a per-type constant now.
- **Blend**, before its position is frozen by shipping — at the M2
  checkpoint, with the other ER-only items.
