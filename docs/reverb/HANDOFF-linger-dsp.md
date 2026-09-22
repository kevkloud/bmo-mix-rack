# Handoff: BMO Linger's DSP

For a fresh session. Written on AURORA, 2026-09-22. **The module exists and
makes no sound.** Everything a host touches is built, tested and in review as
PR #25; what is left is the reverb itself.

Read this, then `modules/reverb/AGENTS.md`, then `10-dsp-spec.md` in full, then
`11-integration-and-test-plan.md` §6. `README.md` in this folder indexes the
rest. **The schema and the panel are settled — do not reopen them.**

## Where you are in the plan

`11` §7's milestones: **M0 skeleton, M1 panel and M5 shared code are done.**
M2, M3 and M4 are yours, in that order.

| | | |
|---|---|---|
| **M2** | ER generator | image-source tables, the Size law and its crossfade, order-banded filters, the diffuser, VARIATION, hi-cut, the Density bridge — tail silent throughout |
| **M3** | late network | FDN, absorbent filters, damping over the per-type knees, the Reverb EQ, pre-delay, SOURCE, modulation, plus the tail-onset and decay-truncation contours |
| **M4** | types | six constant blocks, era fields reserved |

`11` §6 holds the exit test for every one of them. **M2 is the milestone that
decides the module** — the spec says so, and it is where the thesis lives.

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

**Room is the only type whose constants are real.** Every other value in
`kTypeConstants` is marked `CALIBRATE` and claims nothing but its ordering.
Chamber, Hall, Cavern, Plate and Ambience have names, a shape and no numbers.
Fitting them is M4 and it is a listening job, not a desk job.

**`ermode`'s Blend position is defined on paper and unheard** — image-source tap
times and pans from Taps, with the Energy generator's Shape/Spread envelope
replacing the physical gain law, energy-renormalised. If it does not survive
contact with ears, say so before first ship; the position count cannot change
after.

`10` §7's CALIBRATE rows are the full list of what has to be fitted rather than
derived.

## Where to work

PR #25 is open and unmerged at the time of writing.

- **If #25 has merged**, branch fresh from `origin/main`. Every branch starts
  there.
- **If it has not**, continue on `frosty-add-bmo-linger`. That is continuing the
  same work, not stacking a new branch on unmerged work, which is the thing the
  house rule forbids.

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
- Baseline at the time of writing is **24/24** with Defang merged.
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
- **Blend**, before its position is frozen by shipping.
