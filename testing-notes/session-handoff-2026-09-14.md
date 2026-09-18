# Session handoff — 2026-09-14, after the 0.2.4 review

**For the next Claude Code session, on either machine. Read this first,
then `review-0.2.4-2026-09-14.md`, then `WORKFLOWS.md`.** Written on
**AURORA** at the end of the session that reviewed the whole tree ahead of
Frosty's per-module Ableton pass. Everything below is the state as it was
left; nothing here needs re-deriving.

---

## 1. Where things are

| | |
|---|---|
| **CI** | green on `cbd0939` (`v0.2.4`) in both runs: **34834557823** (dispatched) and **34834687703** (the tag). Not byte-identical on macOS: the tag run is universal, the dispatch arm64 only. Install macOS from the tag run. |
| **Installed on AURORA** | `BMO-Windows` from **34834557823**, all nine bundles, hashes in `review-0.2.4-2026-09-14.md`. **This is still what is installed.** None of the fixes below are in it. |
| **`integration`** | unchanged at `42439d7`. Worktree `../bmo-mix-rack-333-int`. |
| **`review-0.2.4`** (on the fork) | worktree `../bmo-mix-rack-333-review`, off `integration`. `bf03cca` the docs, `240de6d` the fixes, `f10a155` the checklist update, plus this file. Full Release `ctest` **26 of 26** on AURORA. **Not through CI.** |
| **`tune-phrase-end`** (on the fork) | worktree `../bmo-mix-rack-333-tunefix`, off `review-0.2.4`. `6900bc1`, the Tune candidate. 8 of 8 tune suites. Not through CI. |
| **Blind sets** (gitignored, main worktree `bmo-mix-rack-333/field-audio/`) | `blind-2026-09-14-round9/` (Tune: Antares, shipped guard 6, candidate; `KEY.txt` beside it) and `opto-attack-2026-09-14/` (Opto: shipped, candidate A, candidate B; `KEY.txt` beside it). Neither has been heard. |
| **Memory** | `review-0-2-4-state.md` in the auto-memory folder says the same as this table, shorter. |

The other worktrees (`-deq`, `-tune`, `-tunework`, `-pop`) belong to earlier
sessions and were only read from. `-pop`'s `build-dsp` is the only build of
the shipped Tune tools on this disk; `-int`'s `build-release` Tune tools are
stale despite their timestamps.

## 2. What was done

- Every module's DSP reviewed against its notes, with the tools and test
  binaries run; the misses ranked with file and line in
  `review-0.2.4-2026-09-14.md`.
- Opto's attack at heavy reduction measured on a step
  (`opto-attack-2026-09-14.md`): the first 1–2 ms of every onset pass at
  unity, and Stressed is slower than Tele on attack. Two candidates
  rendered blind; **nothing changed in any tree.**
- Tune's remaining pop diagnosed and a candidate built: the law was
  confirming note jumps with the detector's frozen period echoed back.
  Field tool: Failure 0 ms worst landing 1.95 → 0.76, jumps 7 → 5, dropouts
  unchanged, corpus gross error unchanged with no item worse
  (`tune-blind-round9-2026-09-14.md`).
- Fixed on `review-0.2.4`, all bug-class, none a character change: EQ Phase
  on the blend (Mix 50 % cancelled), EQ Auto Gain on toggle (was inert
  until a band moved), Util polarity/MONO click, Dimension CENTS-at-0
  freeze, Vcomp "Keep The Air" 70 → 35 (a +19 dB sibilance boost into the
  limiter), the measure tool's stale preset table, **the project version
  1.0.0 → 0.2.4** (every plugin in the tester build reports 1.0.0),
  `build.sh --snapshots` rendering four of seven modules, and doc
  corrections (Saturator bell, DEQ shelf Q, Vcomp gate/ARC/limiter text).
- The three checklists stage 4 wanted written before the round:
  `ceq-testing-checklist.md`, `util-testing-checklist.md`,
  `rack-testing-checklist.md`; and `ui-pass-checklist.md` from renders of
  every panel in both appearances.
- `WORKFLOWS.md` lost its "waiting to be batched" section, as it asked.

## 3. A slip, on the record

The first push of the Tune commit swept in `corpus/`, `renders/` and
`reports/` — the scorer's working folders, which nothing ignored — and
`renders/` held renders of the Failure and Fuji takes. The commit was
rewritten to its five files within minutes, the three folders are in
`.gitignore` now (on `tune-phrase-end`; carry it to `integration`), and the
branch was force-pushed. The old commit `6f504bc` is unreachable on the fork
but its objects exist until GitHub garbage-collects them. If Frosty wants
them purged sooner, that is a support request to GitHub.

## 4. What happens next, in order

Frosty's plan, restated: **each module heard one at a time on the installed
build, then the fixes, then the UI pass, then stage 4 and stage 5.**

1. **Frosty's per-module Ableton pass on the installed `cbd0939`**, one
   module at a time, each against its checklist, naming the machine:
   `ceq-`, `util-`, `rack-`, `dim-`, `opto-`, `deq-`, `vcomp-testing-checklist.md`
   and Tune's handoff. Each carries a "what this review adds" list; the
   review note has the cross-module version. Known in that build, so not
   worth reporting twice: Keep The Air spits, EQ Phase cancels at Mix 50 %,
   EQ Auto Gain is inert until a band moves, Util's switches click,
   Dimension's CENTS 0 is not off, every plugin says 1.0.0.
2. **The two blind sheets.** Tune round nine:
   `../bmo-mix-rack-333-tunefix/testing-notes/tune-blind-round9-2026-09-14.md`.
   Opto attack: `field-audio/opto-attack-2026-09-14/ANSWERS.md`. Answers
   written before `KEY.txt` is opened, per the protocol. If the Tune
   candidate wins, the next candidate on the same mechanism is the voicing
   release counter (`Detector.cpp`, the in-band frame reset), deliberately
   left out so the round tests one thing. If an Opto candidate wins, it
   goes in as a constant (A) or five lines (B) in `Detector.h` with
   absolute attack assertions and the thirteen preset levels re-solved.
3. **Merge.** `review-0.2.4` into `integration`; `tune-phrase-end` after
   it if it won; then one CI dispatch on `integration`
   (`gh workflow run build.yml --repo badmixesonly/bmo-mix-rack-333 --ref integration`
   — always `--repo`, `gh` resolves to Kevin's repository without it),
   both machines install by hash, and a short second listen on what
   changed. Delete the review and tunefix worktrees after the merge.
4. **The remaining DSP items that need a design, not a line** — pick up
   in the order the review note ranks them: Vcomp's THRU cap
   (`measure_vcomp balance` judges it), Vcomp's band-split crossfade, DEQ's
   MID↔SIDE and DYN-off glides, DEQ's `clampShelfQ` firing on any mouse-up,
   the rack's rebuild-under-lock, host bypass with latency
   (`processBlockBypassed`), session restore marking "Init *", the
   Saturator `bodyGain` sign (needs one render Frosty has), the CEQ
   rename's second preset hop, the README and root AGENTS.md still
   describing three modules, and the fork-PR font-secret gate in
   `build.yml` before stage 5.
5. **The UI pass**, alone, on `ui-pass` off `integration`, from
   `ui-pass-checklist.md`: CEQ rename, `utilGain` (three options with
   numbers there; Frosty picks), Vcomp taking the output section, Dimension
   L/R marks and the DETUNE switch's scope, DEQ's solo and analyser in the
   panel, Tune's empty middle, contrast assertions.
6. **Stage 4** as `WORKFLOWS.md` describes it, then **stage 5**: Frosty's
   pull requests to Kevin, DEQ, then Tune, then the UI pass, each rebased
   onto `main`.

## 5. Rules that still hold

- Name the machine on every result. All of the above is AURORA.
- Nothing is dispatched to CI, merged into `integration`, or sent to Kevin
  without Frosty's say. Branches on the fork are the sharing channel and
  start no build.
- Measure, and then Frosty hears it before it is called fixed. The Tune
  and Opto candidates are candidates until he has ranked them.
- Field audio, blind sets, renders of the takes, the corpus and the
  licensed fonts are never committed. `.gitignore` now agrees about the
  scorer's folders; check it before the first commit of any session that
  runs `score-corpus.sh`.
- The schema is frozen; a new parameter goes at the end of `specs()`.
