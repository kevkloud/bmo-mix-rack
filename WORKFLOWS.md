# Workflows — what branches off what, and the commands for each

**For the next Claude Code session, on either machine.** Written 2026-09-11 on
**AURORA**. Frosty set the plan; this file is the map, so a session can start
on one piece without re-deriving how the pieces fit.

This file lives on the **fork** (`badmixesonly/bmo-mix-rack-333`), on the
integration branch. It is not for Kevin's repository: it describes how Frosty's
two machines work, not how the suite is built.

**Start from `testing-notes/session-handoff-2026-09-14.md`** — the state after
the 0.2.4 review, what is installed, the two blind sets waiting on ears, and
the order of everything that follows.

---

## The rules that apply to every workflow below

- **Push to the fork only.** `origin` is `badmixesonly/bmo-mix-rack-333`.
  Nothing goes to `kevkloud/bmo-mix-rack` except through Frosty, as a pull
  request he opens. Never push a `frosty-*` branch there yourself.
- **Name the machine** in every note, handoff and commit body that records
  where a build, test, measurement or listening result happened: "on AURORA"
  or "on ICE QUEEN". Root `AGENTS.md` says how to tell which you are on.
- **Ask before pushing, and batch.** A CI round trip is about 22 minutes on
  Windows, and the workflow's concurrency group cancels a running build on the
  same ref.
- **Don't touch another worktree.** `..\bmo-mix-rack-333-deq` and
  `..\bmo-tune-rt` belong to other sessions.
- Frosty decides character, colour, version numbers and anything a listener
  would notice. Offer options with measured trade-offs; don't pick quietly.

## A new worktree needs two things before it builds

Neither travels with a branch, and both fail loudly:

```
scripts/set-font-dir.sh "C:/Users/thesp/OneDrive/Documents/FONTS"   # AURORA's path
git submodule update --init libs/JUCE
```

---

## The order, and why it is this order

**Frosty set the shape on 2026-09-11: finish DEQ and Tune on the fork, then
one round of testing across every module -- look and sound -- and only then
pull requests to Kevin.** Nothing reaches `kevkloud/bmo-mix-rack` until the
suite is finished and heard, so `integration` is where the work lives until
the end.

```
main (Kevin's, mirrored on the fork)
 └── integration ─────────────────────────────────────────────┐
      ├── add-bmo-deq        DEQ, plus the macOS fix          │  done
      ├── add-bmo-tune       Tune, in the repo not the rack   │  done
      │
      │  STAGE 1 -- finish the two new products
      ├── deq-topology       serial vs parallel, then the     │  parallel;
      │                      rest of DEQ's open list          │  ears at the
      ├── bmo-tune-work      the hiccups, then the lag        │  end of each
      │
      │  STAGE 2 -- the DSP work on the older modules
      ├── ceq-latency        BMO CEQ's latency work           │  parallel,
      ├── opto-high-gr       behaviour at high reduction      │  DSP only
      │
      │  STAGE 3 -- one pass over everything a listener sees
      └── ui-pass            layout, colour, controls, and    │  alone
                             the BMO CEQ rename               │

   STAGE 4 -- one build, one round of testing: every module, look and sound
   STAGE 5 -- pull requests to Kevin, one per piece, opened by Frosty
```

**Stage 1 before stage 3.** DEQ's topology answer decides whether DEQ's panel
gains a control, and Tune's remaining work can still move what its panel
shows. Laying either panel out first means laying it out twice.

**Stage 3 before stage 4.** A listening pass belongs on the build that ships.
Testing before the UI pass means testing twice.

**Stage 5 last, and that is a change.** An earlier draft of this file had each
piece going to Kevin as it became ready. It doesn't: the suite is finished and
tested here first. Two consequences worth knowing --

- Branches off `integration` carry DEQ and Tune with them, so a pull request
  from one would drag both in. At stage 5 each piece is rebased onto `main`
  and sent on its own, in the order DEQ, Tune, then the UI pass.
- The fork's `main` still tracks Kevin's exactly. Keep it that way: it is what
  makes those rebases possible.

**The listening and the DSP measuring are different things.** Measuring needs
no UI and happens inside stages 1 and 2. Ears want the finished panel, which
is stage 4 -- except where a decision is gated on ears, like DEQ's topology,
and there the rendering is prepared early and only the listening waits.

---

## What the control audit settled, 2026-09-11

The UI pass waits on *answers*, not on finished branches: the only thing that
forces a panel to be laid out twice is a change to **which controls exist**.
Three such questions were open. None of them needed ears, and Frosty settled
two.

| question | answer | effect on the UI pass |
|---|---|---|
| **Opto: hard-patch LINK always-on?** | **No — LINK stays a control** (Frosty, 2026-09-11) | none. Opto's panel is final as it stands |
| **Opto: are TELE / ELD / COLOR the names?** | **Yes, locked** (Frosty, 2026-09-11) | none. Caption widths can be measured as final |
| **DEQ: ship a serial/parallel switch?** | **Held for the blind test** (Frosty, 2026-09-11) | **DEQ's control layout waits.** Everything else in the pass proceeds |

Both Opto answers are in `testing-notes/opto-0.2.1-handoff.md` §7, which is
where they were open questions.

**What the audit also found, by inspection rather than by decision:**

- **The three DSP workflows change no control by themselves.** Opto's
  release-at-high-reduction work, DEQ's topology work and CEQ's latency work
  are all interior: `modules/<id>/dsp` and that module's own test file.
- **CEQ's latency work cannot touch a control without a schema event.**
  `Oversampling` is a frozen choice list with a frozen default of 2x -- BMO
  EQ is the one module in the suite whose default is not zero-latency. Adding
  a rate, dropping one, or changing the default all mean retiring an id, which
  is a deliberate act and not something that happens quietly mid-pass. So it
  does not gate the pass.
- **A panel's button labels are not its schema.** Opto's TELE and ELD are UI
  strings over a `Mode` parameter whose choices are `Tele` and `Stressed`.
  Renaming a button is free; renaming a choice is not.

### What that means for the UI pass

Four panels are fully settled by these answers -- Saturator, Util, Opto and
Dimension -- and BMO CEQ is settled too, since its oversampling cannot move
without a schema event. The two that are not are the two being finished first:
~~**DEQ**~~ -- settled 2026-09-12, serial with no switch, so its panel is now
fixed too -- and **Tune**, whose own work can still move what its panel shows.

Which is the reason the order puts the UI pass at stage 3 rather than running
it now: waiting costs nothing, and starting early costs two panels laid out
twice. What can be done at any time is the shared work that no control set can
invalidate -- `core/ui` tokens, the `utilGain` recolour, derived tokens, and
the contrast and text-fit assertions from `docs/ui-workflow-brief.md`.

## Dependency audit

What each workflow touches, and what that means. Confirm the "can it change
controls" column when you open the work — it is the question that decides the
order, and it is answered from the notes, not from the code.

| Workflow | Touches | Shares files with | Can it change controls? |
|---|---|---|---|
| `add-bmo-tune` | `modules/tune`, `products/tune`, `tools/tune`, `tests/*/tune`, the four CMakeLists, `products/AGENTS.md` | DEQ, in the CMakeLists and the identity tables | no |
| `add-bmo-deq` | `modules/deq`, `products/deq`, the rack's registry, `RackTests.cpp`, the same CMakeLists | Tune, as above | no — the macOS fix is a test fault |
| `deq-topology` | `modules/deq/dsp`, `tests/dsp/DeqDspTests.cpp`, `testing-notes/deq-topology-listening.md` | nothing else | **answered 2026-09-12: no** — serial, no user-facing switch, so DEQ's panel is settled |
| `ceq-latency` | `modules/eq/dsp`, `tests/dsp/EqDspTests.cpp` | nothing else | no — `Oversampling` is frozen schema, so a change is a retirement, not a side effect |
| `opto-high-gr` | `modules/opto/dsp`, `tests/dsp/OptoDspTests.cpp` | nothing else | no — confirmed by inspection, 2026-09-11 |
| `sat-voicing` | ears, then maybe `modules/sat/dsp/Filters.h` | nothing else | no — it is a retest of Kevin's change |
| `ui-pass` | `core/ui/*`, every `modules/*/panel`, `tests/ui/LayoutTests.cpp`, `tools/snapshot` | **every module at once** | it *is* the control work |
| `dim-controls` | Dimension's panel | the UI pass, heavily | yes — fold it into the pass |
| `bmo-tune-work` | `modules/tune/dsp`, `tools/tune`, `tests/dsp/tune` | nothing in the rack | yes, but only Tune's own |

**Why the UI pass cannot be parallel.** Every panel is built on the same
`core/ui` tokens, controls and look-and-feel, and one layout test walks all of
them. The last pass was 45 commits across every panel. Five branches editing
panels at once means five sets of conflicts in the same files.

**Why the DSP workflows can be.** They touch one module's `dsp/` and its own
test file. No two of them meet, and none of them meets the UI pass.

**Tune is independent of all of it** — different folders, and the build
switches keep it that way — but its *panel* is drawn with the same shared UI
code, so Tune's UI work belongs in the UI pass, not in `bmo-tune-work`.

---

## The commands, per workflow

### Starting any workflow

```
git -C <worktree> fetch origin
git worktree add -b <branch> ../bmo-mix-rack-333-<short> origin/integration
```

Then the two setup lines above, then build.

### Building and testing

```
# everything, Release, with the plugins                  (~20 min cold)
cmake -S . -B build && cmake --build build --config Release --parallel
ctest --test-dir build -C Release --output-on-failure

# DSP only: no JUCE, no plugins                          (~2 min)
cmake -S . -B build-dsp -DBMO_DSP_ONLY=ON
cmake --build build-dsp --config Release --parallel
ctest --test-dir build-dsp -C Release --output-on-failure

# the two sides on their own — what keeps Tune out of the rack
cmake -S . -B build-tune -DBMO_DSP_ONLY=ON -DBMO_BUILD_RACK=OFF
cmake -S . -B build-rack -DBMO_DSP_ONLY=ON -DBMO_BUILD_TUNE=OFF

# Debug, and installs over this machine's VST3 folder — say so before Frosty listens
scripts/build.sh
scripts/build.sh --snapshots       # also renders every rack panel to snapshots/
```

`ctest` names Tune's suites `tune_*`. `tune_hardtune_target` is **disabled on
purpose** — it is the open hard-tune work, and the change that makes it pass
enables it.

### Looking at a panel without a 20-minute build

```
build/tools/Release/snapshot.exe <eq|sat|util|opto|dim|deq|rack> out.png [param=value ...]
build/tools/tune/Release/bmo-tune-snapshot.exe out.png [retune_ms=12 appearance=dark]
```

### Measuring, per module

```
build/tools/Release/measure_eq.exe      # and measure_sat, measure_opto, measure_dim
build/tools/Release/measure_deq.exe     # on add-bmo-deq
build/tools/tune/Release/bmo-tune-latency.exe --range all
build/tools/tune/Release/bmo-tune-ref.exe bmo
build/tools/tune/Release/bmo-tune-field.exe
```

### CI on the fork

CI does not run itself on a feature branch. Dispatch it, and give Frosty the
run id:

**But it DOES run itself on a version tag.** `build.yml`'s triggers are `push`
on `branches: [main]` **and `tags: ['v*']`**, plus pull requests and
`workflow_dispatch`, all with `paths-ignore: ['**.md']` — so a docs-only push
starts nothing, and pushing `v0.2.4` on 2026-09-14 started a second full build
beside the dispatched one, on the same commit. That is the workflow behaving as
written; this file had never said so. The concurrency group is keyed on
`github.ref`, so a tag run and a branch run do **not** cancel each other.
Expect two, and cancel one yourself if the runner time matters.

```
gh workflow run build.yml --repo badmixesonly/bmo-mix-rack-333 --ref <branch>
gh run list --repo badmixesonly/bmo-mix-rack-333 --limit 5
```

Jobs: **DSP** (Linux, seconds), **Each side alone** (Linux, the two switches),
**macOS** and **Windows** (the plugins, ~20-25 min). The Windows job's
`BMO-Windows` artifact is what ICE QUEEN installs, by hash.

#### Installing an artifact

Unzip it and run the installer beside the bundles — `install.command` on
macOS, `install.ps1` elevated on Windows. It removes bundles an earlier build
left under a name this one no longer uses (`tools/packager/superseded.txt`),
copies everything into place, and on macOS clears the quarantine flag that
makes an unsigned plugin report itself as **"damaged and can't be opened"**.
That message is Gatekeeper, not a broken build; nothing in CI signs or
notarises. `tools/packager/README.md` has the detail, including why the mac
package from a non-tag run is arm64 only.

Verify by **hash, never by size** afterwards: `scripts/build.sh` installs a
Debug build over the same folder, so a local build silently replaces the
artifact a listening result belongs to.

#### The last green run on `integration`

**34834557823** (dispatched) and **34834687703** (started by the `v0.2.4`
tag), both at `cbd0939`, 2026-09-14, all four jobs. They are not
byte-identical on macOS: the tag run builds universal, the dispatch arm64
only. AURORA installed the `BMO-Windows` artifact of 34834557823 the same
day; the hashes are in `testing-notes/review-0.2.4-2026-09-14.md`. The
DEQ-plus-Tune batching that this section used to describe is done.

### Finishing a workflow

1. Full `ctest` in Release passes on the machine you are on.
2. Push the branch to the fork, dispatch CI, wait for all four jobs.
3. Write what you measured into `testing-notes/`, naming the machine.
4. Tell Frosty. Merging into `integration`, and anything that reaches Kevin,
   is his.

No pull request is opened along the way. Every branch ends in `integration`,
and Kevin sees the work at stage 5, after the suite has been heard.

---


## Stage 4 — the round of testing, look and sound

One build, every module, on both machines. It happens once, after the UI pass,
and it is the gate before anything reaches Kevin.

- Build it in CI on `integration`, not locally, so both machines install the
  same bytes: `gh workflow run build.yml --repo badmixesonly/bmo-mix-rack-333
  --ref integration`, then the `BMO-Windows` and `BMO-macOS` artifacts.
- Record the artifact's run id and each plugin's SHA-256 in the testing note,
  and check the hashes again after the session. The DETUNE host checks were
  once attributed to the wrong build for want of that.
- The per-module checklists already exist and are what to work through:
  `dim-testing-checklist.md`, `opto-testing-checklist.md`,
  `deq-testing-checklist.md`, `vcomp-testing-checklist.md`,
  `saturator-voicing-retest.md`, and for Tune `tune-host-checklist.md`
  (written 2026-09-16; `tune-handoff.md` is superseded and is about the
  engine). What has no checklist yet -- BMO CEQ, BMO Util, the rack itself --
  needs one written before the round, not during it.
- **One decision is carried into this round rather than taken before it:**
  whether BMO Tune RT needs a readout of the note it is hearing. Frosty,
  2026-09-16 -- answer it in Ableton, on whether it is *necessary*. The panel
  no longer needs it to look right, so it is a feature question rather than a
  layout one. `tune-host-checklist.md` §1 has what to look for and the cost.
- **Look and sound are separate passes over the same build.** Render every
  panel with `snapshot` and `bmo-tune-snapshot` in both appearances and read
  them side by side; then listen. A panel fault found by eye costs nothing to
  fix; the same fault found after a listening session costs the session.
- Name the machine on every result.

## Stage 5 — the pull requests

Frosty opens them, in this order, each rebased onto `main` so it carries only
its own work: **DEQ**, then **Tune**, then the **UI pass**. Kevin reviews each
on its own. `WORKFLOWS.md` itself never goes: it is the fork's file.

**Check every branch for audio before the PR is opened**, on the rebased
branch:

```
git diff --stat main...<branch> | grep -iE '\.(wav|aif|aiff|flac|mp3|otf|ttf)$'
```

It must print nothing, and the result goes in the PR checklist. Audio and the
licensed fonts have been swept into a commit twice (both 2026-09-14, both
rewritten and force-pushed within minutes); the two affected commits are
unreachable, so no merge can carry them forward, but the check is cheap and the
mistake is not. Root `AGENTS.md`, "Before your first commit: never commit
audio", has the detail and the commit ids.

## The workflows, one by one

### `add-bmo-deq` — DEQ, and the macOS failure

Branch exists, three commits on `main`. Fork run 34562668139 failed on macOS
only: `deq_dsp` "T2 ceiling", `lowCut f0=0 Q=0 g=0`, limit 1.89, got 3.31.
Windows and Linux pass. **The all-zero band is the lead** — either the case's
parameters are never set on that path, or the failure message prints the wrong
fields. Find the cause; do not widen the limit. Green on all three jobs, with
the run id, is what done looks like.

### `add-bmo-tune` — Tune in the repository, not in the rack

Done on AURORA, 2026-09-11: history merged, tools and tests relocated, build
switches, identity in the tables. Not yet pushed. What is left is Frosty's
call on the lime accent's 1.29 contrast on the pale plate
(`products/AGENTS.md`), and a CI run.

### `deq-topology` — serial or parallel

**Settled 2026-09-12: serial, no user-facing switch.** 57 blind pairs over
seven sources, then six level-matched dynamic pairs; the record is
`testing-notes/deq-blind-2026-09-11.md` and the method is
`testing-notes/blind-listening-protocol.md`. What is left of DEQ in stage 1 is
its Ableton checklist, which is a host test rather than a decision.

`testing-notes/deq-topology-listening.md` §0 renders the blind set; §2 is
Frosty's ear. **Do not open the key.** Render, check the tool's table for a
case that never engaged ("max GR 0.0" tests nothing), tell Frosty where the
files are, and stop.

### `ceq-latency` — BMO CEQ's latency work

BMO CEQ is the one module in the suite whose default is not zero-latency: its
oversampling sets `latencyForParams`, and at the 2x it ships on that is 40
samples. Since 2026-09-17 the panel says so — the oversampling row lights one
of its three switches at Init.

**The rename landed with the UI pass**, 2026-09-17. The plugin code `Fsty`, the
bundle id, the module id `eq` and the schema do **not** change, because that is
what makes existing sessions open. The preset **extension did** change, to
`.bmoceq` — this page used to say both extensions stay, which was wrong — and
the `BMO EQ` and `FrostyEQ` folders are copied across on first run. See
`testing-notes/ui-pass-ceq-2026-09-17.md`.

### `opto-high-gr` — behaviour at high reduction

Start from `testing-notes/opto-0.2.1-handoff.md` and
`opto-testing-checklist.md`. House rule from this module's own history:
**assert absolutes, not comparisons** — a relative release test passed for a
whole release while both modes were broken.

### `sat-voicing` — the voicing bell, by ear

`testing-notes/saturator-voicing-retest.md`. Kevin's `51a263b` changed the
voicing bell and it has never been heard on either machine. Ears, on the
build that ships.

### `ui-pass` — everything a listener sees

The big one, and the one that has to be alone. It runs at stage 3, after DEQ
and Tune are finished, so every panel including DEQ's can be laid out once. Read
`docs/ui-workflow-brief.md` and `testing-notes/ui-editor-handoff.md` first —
45 commits of prior art, what was tried and thrown away, and the loop that
makes this cheap. In scope: layout, colour and controls across every rack
module and Tune; Dimension's controls audit; the `utilGain` placeholder
(`core/ui/Tokens.h`, `#9c71c3`, about 3 degrees from Dimension's accent, so in
adjacent slots they read as the same purple); and the BMO CEQ rename. Out of
scope: anything that changes what a control *does*.

### `bmo-tune-work` — Tune's own testing and finish

Runs parallel to all of it. **Start from
`testing-notes/tune-handoff-2026-09-13.md`**: the current state, which build
is still best by ear (round four, `31b30ef`), and the list of avenues that
have been measured and thrown away -- do not re-derive those. Behind it,
`testing-notes/tune-handoff.md` for how the engine works and what rounds three
and four found, and `testing-notes/tune-latency-review-2026-09-11.md` for the
alignment finding and the latency rule.

Open, in order: what makes a splice audible (the blocker -- two metrics have
been refuted); the detector's octave and twelfth errors on scoops; the
live-monitoring budget, which is Frosty's to set and which the per-note
latency rule cannot go green without. The latency rule governs every change,
and it is a curve -- Waves' measured delay at each note,
`references::ceilingMsAt` -- not the scalar 10.62 ms it was written as until
2026-09-11.
