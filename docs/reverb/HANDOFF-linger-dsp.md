# Handoff: BMO Linger's DSP

For a fresh session. Written on AURORA, 2026-09-22; revised on AURORA,
2026-09-23, after PR #25 merged; revised on ICE QUEEN, 2026-09-24, with M2
built; **revised on ICE QUEEN, 2026-10-02: M2 is heard and merged, and M3 is
next.** The early reflections play and the tail is silent. M2 merged to `main`
as PR #27 (`655e929`) on 2026-10-01, with the small-room fix after it (#30).
Every decision M3 waited on is made. **Start at "M3 runs in two halves"
below.**

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

### M2 is built, and stopped where it should

On ICE QUEEN, 2026-09-24, branch `frosty-linger-m2-er` from `origin/main`
`aa4416d`. `testing-notes/linger-m2-er-2026-09-24.md` is the record; read it
before this section. In one paragraph: `modules/reverb/dsp/ErGenerator.h` is
the runtime, `ImageSource.h` the offline image-source generator with the
audit, `TapTables.h` holds six printed tables that the tests re-derive and
pin, and `DspCore.h` feeds the generator and applies the faders. The whole
of `11` §6's ER block is asserted in `tests/dsp/ReverbDspTests.cpp` and is
green; `build-dsp` 18/18, `build-full` 16/16, both after builds that exited
0. The CPU worst case was measured first: 0.87 % of one core at 48 kHz/128
and 3.5 % at 192 kHz, against 1.5 % and 5 %.

**What moved against this file and the spec, each recorded in the note:**

- The ER flatness rule is now the owner's of 2026-09-24: octave-smoothed,
  250 Hz–8 kHz, within 6 dB about the tilt at DENSITY 100 %, asserted and
  green (4.6 dB). 11 §6's 1/3-octave ±3 dB is printed beside it for the record.
- The early lateral fraction is asserted on the ER bus, not ISO's figure with
  the direct sound: the −15.3 dB tap ceiling makes ISO's figure unreachable.
- The two flamming rules are applied as **ceilings** in the generator, the
  way 10 §3 applies Kuttruff's, on the energy heard through the band poles.
- The MIX law is the owner's of 2026-09-24: dry = min(1, 2(1 − mix)),
  wet = min(1, 2 mix), default 50 %, pinned at five points. The bus suite's
  reverb rows were regenerated for the default.
- VARIATION 6 is built to 05 §9.3 (mono-flat, not mono-empty); 10 §3's
  "vanish in mono" sentence and the panel label it asks for are wrong.
- The window clamp is not applied (the Size law stays linear, as the panel).
- The 150 ms wet fade in `reset()` is not done.
- The panel still draws Room's table for every type.

*Since then (2026-09-26 to 2026-10-02): heard at the checkpoint below, every
owner decision made, merged as #27, and the cleanup PR described under "M3
runs in two halves" landed the rest. The three bullets above that are not done
— the window clamp, the `reset()` wet fade, Room's table drawn for every type —
are still not done; the first two are M3's (see below), the third M4's.*

### M2 opens with the CPU worst case

*Done: 0.872 % at 48 kHz/128 and 3.508 % at 192 kHz/128, worst case, median
of five, Release, on ICE QUEEN.* `10` §8 says to measure the worst case
**first**: DENSITY at 48 taps, three diffuser stages, 192 kHz. Build that path
before tuning anything and run `measure_reverb bench` against `10` §6's budget
(≤1.5% of a core at 48 kHz/128, ≤5% at 192 kHz, Release, median of five). If
it does not fit, the tap count or the stage count is the thing to argue about,
and it is far cheaper to argue before the tables are tuned than after.

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

~~**Blend is heard at this checkpoint too.**~~ **Heard and cut, 2026-09-26.**
Frosty listened to the M2 set on ICE QUEEN (HEDD Type 20 MK2). Blend sounded
like "a slightly worse" Taps, so ER MODE is Taps / Energy, and VARIATION
defaults to 4 ("2 isnt enough to feel"). Verdicts on every item are in
`testing-notes/linger-listening-set-2026-09-24.md`. **Ambience** failed the first
set, whose ER levels were too low (the cluster sits 6.7 dB under the dry even
at ER 0 dB). Heard again in stereo at ER 0 it **moves the voice back,
subtly**, which passes `11` §6's M2 exit condition; how far it should go is
M4's. ER HI-CUT is 12 dB/octave from the same day. (A 250 Hz shift noted that day was
the analyser's window moving, not the sound; see the listening-set note.)

### M3 runs in two halves, after a cleanup PR

Frosty, 2026-10-02. **Branch each piece fresh from `origin/main`** once the one
before it has merged, in its own worktree and build tree.

**0. The cleanup PR (`frosty-linger-cleanup`)** — so M3's PR is about the tail
and nothing else:

- ER SPREAD dims in Taps, under the new house rule (a control a mode makes
  inert is dimmed — `modules/AGENTS.md`), and the engine stops rebuilding the
  table on a SPREAD move it never reads. One function, `erSpreadIsLive`, for
  both.
- IN HI-CUT is captioned **DARKEN** on the panel; the id stays `inhicut`.
- `measure_reverb bench <rate> <block> density|hicut` measures what
  automating those controls costs, and the DENSITY-automation cost flagged on
  2026-09-30 is dealt with there — and ER HI-CUT automation, which was worse
  (`testing-notes/linger-cleanup-2026-10-02.md`).
- `11` §7's "owner confirm" paragraph is closed out, and this file revised.

**1. M3a — a tail you can hear.** *Built on ICE QUEEN, 2026-10-02, on
`frosty-linger-m3a`: `testing-notes/linger-m3a-late-network-2026-10-02.md`
is the record and `10` §4's "As built in M3a" lists the six departures
(Hadamard mixing among them, so the line count is now a power of two).
It stops at the listening checkpoint below; M3b waits for Frosty's ears.*
The plan it followed, in this order:

1. Your own baseline (build exit code, counts, machine).
2. **The CPU worst case first**, as M2 did: M2's worst-case ER plus the full
   late network at 192 kHz/128, measured at `kNumLines` = 8 **and** 16 before
   anything is tuned. Build at 8; the 16 figure says whether Plate's fix (see
   below) is affordable, and it is cheapest to know before tuning.
3. `kMaxTailSeconds` 30 → 40 as its own `core/` commit, with `TailTests` and
   `11` §6's "≤30 s" → "≤40 s" moving in the same commit. Every module's tail
   report moves, so name them in the commit body (BMO Dwell's too if #35 has
   merged by then).
4. The FDN on `kNumLines`, the absorbent filters and damping over the
   per-type knees, and pre-delay (tail only, `kPreLinkFixed` false).
5. `11` §6's late-tail rows that these touch: modal density (Plate red at 8 is
   expected and written down, not hidden), ringing, pre-delay ±1 sample, the
   stability corner (`decay` 20 s × `damphi` 2.0, now inside 40 s), "tail ≥
   measured", parameter changes without clicks.

**Then stop for a listening set** in gitignored `packages/reverb-listening/`:
decay and damping across types, the ER-to-tail handover (the thing M2 could
not let anyone hear), PRE-DELAY, and Plate at 8 lines on a vocal. Ask about
the headphone amp's mono switch before a pass that follows a mono check.

**2. M3b — the rest of the late block.** The Reverb EQ on the wet path,
SOURCE, modulation (≤3 cents, its spectrum reported), the tail-onset contour
over `TypeConstants::attack` and the decay-truncation contour, the 150 ms wet
fade in `reset()`, and whatever of `11` §6 is still red. Its own listening
items, then M4.

### The decisions M3 needed — all made

These were Frosty's, open in `11` §7, and M3's tests depend on them.
*All of them are made:*

- ~~**The MIX law and its default.**~~ **Decided 2026-09-24:** dry =
  min(1, 2(1 − mix)), wet = min(1, 2 mix), default 50 %, 100 % verb only for
  a send. Pinned in `reverb_dsp_tests` at 0 / 25 / 50 / 75 / 100 %.
  **The ER flatness rule was decided the same evening**: octave-smoothed,
  250 Hz–8 kHz, within 6 dB about the tilt at DENSITY 100 %, asserted.
- ~~**The 30 s tail ceiling against a 40 s tail.**~~ **Decided 2026-09-26:
  raise the ceiling** (Frosty: "raise it"), so `kMaxTailSeconds` goes to 40 s
  in M3 and "≥ measured" can hold at the corner. That is a `core/` change and
  every module's tail report moves with it, so it lands with M3's tests, not
  before. The original question: `decay` reaches 20 s and
  `damplo`/`damphi` reach 2.0×, so any setting with `decay` × the larger
  multiplier above about 30 s rings longer than `kMaxTailSeconds`
  (`core/dsp/ModuleDsp.h`), and `tailSecondsFor` clamps to 30. `11` §6 asks for
  the report to be both **≥ measured** and **≤30 s** at every type, which cannot
  hold at that corner, and its own stability test drives exactly that corner
  (`decay` 20 s, `damphi` 2.0). Pick one: clamp the effective T60 in the engine
  at the ceiling; restrict "≥ measured" to settings under it; or accept an
  under-report at the corner and write it down. Then fix `11` §6 to match.
- ~~**`inhicut` as a parameter or a constant**~~ **Decided 2026-09-26: a
  parameter, under a clearer name** (Frosty), and the name is **DARKEN**
  (2026-09-29), on the panel from the cleanup PR.
- ~~**Whether ER SPREAD greys out in Taps mode or sits inert.**~~ **Decided
  2026-10-02: dimmed**, and as a rule for every module, not just this one: a
  control a mode makes inert is dimmed (`modules/AGENTS.md`).

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

**`ermode`'s Blend position was cut on 2026-09-26** after it was heard at the M2
checkpoint: it sounded like a slightly worse Taps. ER MODE is two positions,
and the count cannot change after first ship.

Every owner decision M3 depends on is made (see "The decisions M3 needed"
above). New ones will come up; they are not yours to make, but they are yours
to ask for.

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
- **Establish your own baseline before you change anything.** On ICE QUEEN,
  2026-10-02, at `origin/main` `a0e5ca2`: `build-dsp` 18/18 (plus
  `tune_hardtune_target`, disabled by design). Anything merged since makes the
  count stale. Build the test targets, check the exit code, and record the
  count and the machine in your first note. **Name the targets**: in a fresh
  Visual Studio tree, `ctest -N` cannot name an unbuilt executable, so take
  the list from the generated `*_tests.vcxproj` files instead.
- **Re-prove BMO Opto's hashes if anything under `core/ui` changes** —
  `59d85c014da98432` dark, `313df8cc740e9aa3` light, `393f13e24fbf96c3` GR dark
  (`appearance=` and `surface=simple` named; see
  testing-notes/opto-reference-hashes-2026-10-01.md).
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
- ~~**Blend**~~ — heard at the M2 checkpoint and cut, 2026-09-26.
