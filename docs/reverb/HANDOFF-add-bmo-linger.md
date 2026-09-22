# Handoff: add BMO Linger (panel first, renders, open questions)

For a fresh session. Written on AURORA, 2026-09-21. The groundwork pack is
done; this session builds what is needed to put real renders in front of
Frosty and settles the open questions with them. Read this, then
`docs/reverb/README.md`, then `11-integration-and-test-plan.md`, then the
parameter, metering, tail and type sections of `10-dsp-spec.md`.

## Where to work

- The pack lives on `frosty-reverb-groundwork` (tip `27f515f` plus this file),
  in the worktree `../bmo-mix-rack-333-reverb`. Local only, not pushed.
- Build work goes on a NEW branch, `frosty-add-bmo-linger`, started from
  Kevin's `origin/main`, with the docs branch merged in. Every branch starts
  from `origin/main`; never stack on another unmerged branch. Give it its own
  worktree (for example `../bmo-mix-rack-333-linger`) and its own build trees.
- Other sessions own other folders: the main repo folder (BMO FET DSP),
  `-fetui` (BMO FET panel), `-defang` (BMO Defang), `-delay` (BMO Dwell). Do
  not write, build, stash or switch branches in theirs. Reading is fine.
- Shared hunks that live on unmerged branches are reproduced byte-identically,
  never inherited by stacking. `textParam`/`textFn` in
  `core/state/ParamSpec.h` and `core/state/Parameters.h` comes from the BMO FET
  skeleton `c142f37` (BMO Defang's `86095a5` carries the same bytes; check
  with `git hash-object`). Say so in the commit body.

## Build rules, learned the hard way

- **Never run an untargeted build.** A Debug configure installs every product
  after build, so a bare `cmake --build` overwrites the plugins installed in
  the system VST3 folder. It happened on 2026-09-20 and replaced the 0.2.5 set
  mid Ableton pass. Always pass `--target <names>`, or configure with the
  install-after-build option off (confirm the option name in `CMakeLists.txt`).
- Never build or install the rack plugin target. `rack_tests` may be built and
  run. The standalone product is allowed, and installs "BMO Linger.vst3".
- A green ctest only counts after a build that exits 0. A failed compile leaves
  the old exe in place and ctest runs it.
- Never commit renders, audio or fonts. Renders go in `snapshots/`
  (gitignored). Fonts come from the `.bmo-fontdir` pointer; copy the pointer
  from the main folder, not the font files.
- BMO Opto's hashes guard shared UI code: `ab3ff3b77116b7a5` (dark),
  `878cca7b1a80a551` (light), `88a7653a82c19ae0` (GR dark). They are rendered
  at `signal=-18`; a bare render does not reproduce all three. Re-prove them if
  anything in `core/ui` changes.
- Name the machine in every note. Do not push or open a PR until Frosty says.

## Decided, do not reopen

- Display name **BMO Linger**, module id `reverb`, bundle
  `com.lt3audio.bmolinger`, presets `.bmoreverb`, plugin code `Brvb`, BMO line.
- Thesis: Reference B's sound with Reference A's functionality, and an
  early-reflections section good enough to use alone. Two generators, separate
  ER and Reverb faders, pre-delay moves the tail only and lives in the wet
  path, one ER density control, a feed-the-tail control, decay-time multipliers
  for damping, no allpass in the ER path, zero reported latency.
- v1 types, append-only, in this order: Room, Chamber, Hall, Cavern, Plate,
  Ambience.
- No third-party product or brand names in code, docs or UI strings.

## Open questions this session settles with Frosty

Bring each one as a concrete proposal with a render or a number, not as a
survey. All are permanent once shipped.

1. **Accent.** Only about 298-309 degrees passes the hue rule, and BMO Dwell
   wants the same window (check `frosty-delay-groundwork` for what it took).
   Candidates: `#dd93dd`, `#eb8ae9`, `#e8a2e5`, `#e694e0`. Make a colour mock
   the way BMO FET and BMO Defang had one (single self-contained HTML in the
   scratchpad, real palette values, both plates, next to its nearest shipped
   accents, full-rack row), then confirm on real renders. If both modules
   cannot fit, say which one takes an exception and by how much.
2. **Thirty parameters and the panel split.** 11 proposes 7 controls plus a
   display on the main face and the rest in an expanded section (EARLY / TAIL /
   TONE & OUT). Render the main face and the expanded section so Frosty can
   judge it. `inhicut` (index 25) is marked "owner confirm": keep or cut.
   Nothing can be reordered or removed after first ship, so freeze the order
   only when Frosty has seen it.
3. **The display.** 11 asks whether a static time-domain sketch (ER taps plus
   decay envelope, redrawn from the parameters, like BMO Defang's band sketch)
   earns its space. Render it with real parameter values at two or three
   settings.
4. **Shared-code changes.** Tail-length reporting is needed in v1
   (`tailSecondsForParams` on `ModuleDsp`, default 0.0, the rack sums its
   slots). It is a change to shared code: make it its own small commit with
   proof that every existing module is unchanged. Mono-in to stereo-out is
   deferred with the fallback in 10. Host tempo is out of v1 and should land
   once, byte-identically, with BMO Dwell. Confirm this plan with Frosty before
   touching shared code.
5. **Era colour.** Out of v1, fields reserved per type. Confirm or promote.

## The work, in order

1. Create the branch and worktree; configure; prove a targeted build of an
   existing test target works before adding anything.
2. Settle question 1 on a mock and questions 2, 3 and 5 on paper with Frosty,
   far enough to freeze the schema.
3. Skeleton, panel first, as BMO FET (`c142f37`) and BMO Defang (`86095a5`)
   were done: module files, permanent schema, registration touch points,
   identity and accent rows, factory presets named by use, module `AGENTS.md`
   and `README.md`, schema and layout tests, snapshot id, a `measure_reverb`
   stub. The DSP is a marked placeholder that passes audio through; its
   parameter struct already carries everything the real DSP needs, in real
   units.
4. Whatever tools the renders need: snapshot states for the expanded section,
   each type, the display at several settings, both appearances, and the module
   in the rack context (as a render, never by building the rack plugin).
5. Visual pass with the documented tools (`tools/snapshot`, `tools/inspect`,
   `ui_layout_tests --dump`); measure every box. Look at the renders and fix
   what is wrong. Send them to Frosty; decisions on look are theirs.
6. Record hashes and every measured figure in
   `testing-notes/ui-pass-reverb-<date>.md`, naming the machine. Say plainly
   what was rendered, what was measured and what was only reasoned about.
7. Commit locally in repo style (`reverb: <clause explaining why>`), staging
   explicit paths. Then write the DSP handoff: ER generator first, late network
   second, types last, each with its exit test from 11.

## Token discipline

Frosty runs these sessions with an orchestrate-only ruleset: dispatch agents,
have each write to files and return a short summary, never paste file contents
back, do not re-read what an agent summarised. Opus for spec and code, Sonnet
for research. Every agent brief that builds must carry the build rules above.
