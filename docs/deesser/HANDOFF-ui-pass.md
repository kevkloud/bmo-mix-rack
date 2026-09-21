# Handoff: BMO Defang UI pass

For a fresh session whose job is to take the BMO Defang panel to a testable
state. Written on AURORA, 2026-09-20. Read this, then `docs/deesser/README.md`
and `docs/deesser/11-integration-and-test-plan.md`.

## Where to work

- Worktree: `../bmo-mix-rack-333-defang` (next to the main repo folder), branch
  `frosty-add-bmo-defang`. It starts from Kevin's `origin/main` and carries the
  de-esser docs (`frosty-deesser-groundwork`, tip `7301150`) plus the module
  skeleton. Nothing is pushed.
- Work only in that worktree, with its own build trees. Other sessions own the
  main repo folder (BMO FET DSP), `-fetui` (BMO FET panel) and `-delay`
  (BMO Dwell). Do not write, build, stash or switch branches in theirs.
- Every branch starts from Kevin's `origin/main`. Do not stack this branch on
  another unmerged branch. The one shared hunk is `textParam`/`textFn` in
  `core/state/ParamSpec.h` and `core/state/Parameters.h`, reproduced
  byte-identically from the BMO FET skeleton (`c142f37`) so either PR can land
  first. Leave it byte-identical.
- Never build or install the rack plugin target: the installed 0.2.5 rack is
  mid Ableton pass on this machine. `rack_tests` may be built and run. The
  standalone product is allowed.
- Never commit renders, audio or fonts. Renders go in `snapshots/`
  (gitignored). Fonts come from the `.bmo-fontdir` pointer, not from the repo.

## Decided, do not reopen

- Identity: display name **BMO Defang** ("takes the bite out of your
  recordings"), module id `deesser`, plugin code `Bdes`, bundle
  `com.lt3audio.bmodefang`, presets `.bmodeesser`, BMO line.
- Accent: rose `#ea9f9a`. It passes the hue rule and the contrast bands; no
  exception.
- Schema, permanent, five parameters in this order: `freq` 2-10 kHz default
  6.5 kHz (log); `q` 0.7-6 default 2.5 (log); `thresh` -24..+24 default 0,
  reading like "+3.0 dB over"; `range` 1-18 dB default 8; `shape` "Bell" /
  "High Shelf", Bell default. No id, order, range, default or choice-order
  change, ever. New parameters can only be appended.
- ADAPT is not a v1 control. Its blend is one internal DSP constant. It is
  listed as "Potential, needs testing" in `10-dsp-spec.md` and could only be
  appended after `shape`. Do not leave panel space reserved for it unless
  Frosty asks.
- Listen is a momentary, non-automated, unsaved UI state, following BMO DEQ's
  band-solo precedent. It is not a parameter.
- No spectrum analyser in v1. The panel has a static band sketch that redraws
  from `freq`, `q`, `shape` and `range`.
- The GR meter is the shared `ui::DynamicsMeter`: 24 dB scale, stock 0.7
  bezel. It will show peak band reduction. Do not change `core/ui` unless it
  is unavoidable, and if you do, re-prove BMO Opto's hashes:
  `ab3ff3b77116b7a5` (dark), `878cca7b1a80a551` (light), `88a7653a82c19ae0`
  (GR dark).
- No hardware or third-party product names in code, docs or UI strings.

## State of the skeleton

See "Skeleton report" at the end of this file; it is filled in from the agent
that built it. The DSP is a marked pass-through placeholder, so the GR needle
sits at rest and listen passes audio through. The real DSP is a separate job,
confined to `modules/deesser/dsp/`, and must not be started from this session
without Frosty saying so.

## The pass, to a testable state

1. Build from a clean footing. The build must exit 0 before any ctest count
   means anything: a failed compile leaves the old exe and ctest runs it.
2. Render both appearances, both shapes, and listen active. Send Frosty the
   renders; decisions on look are theirs.
3. Run the suite's checks on every box: `ui_layout_tests` (and `--dump`),
   caption lifts and gaps with `Inspect gaps`, contrast with `Inspect ratio`.
   A caption lift only holds for the box it was measured on.
4. The band sketch: check it against the numbers. Bell centre and width follow
   `freq` and `q`; the shelf corner follows `freq`; depth follows `range`; the
   frequency axis is log over at least 1-16 kHz; it reads in both appearances.
5. Value strings: `thresh` at -24, 0 and +24; `freq` in Hz and kHz; `q`;
   `range`. ASCII only, as the licensed fonts require.
6. Listen: momentary behaviour, its lit state, that it is not saved with a
   preset or a snapshot, and that it releases on panel close.
7. Check the module in the rack context as a render only.
8. Factory presets load and show sensibly on the panel.
9. Record hashes and every measured figure in
   `testing-notes/ui-pass-deesser-2026-09-20.md`, naming AURORA. Say plainly
   what was rendered, what was measured and what was only reasoned about.
10. Commit locally in repo style (`deesser: <clause explaining why>`), staging
    explicit paths. Do not push or open a PR until Frosty says so.

"Testable" means: builds clean, schema tests green, layout checks pass, renders
reviewed by Frosty, and the standalone product loads in a host with every
control moving its parameter. It does not mean the module de-esses; that waits
for the DSP.

## Skeleton report

**Filled in on AURORA, 2026-09-20.** The module exists, builds, registers and
renders. Figures, hashes and what went wrong are in
`testing-notes/ui-pass-deesser-2026-09-20.md`.

- **Built and green.** `build/` Debug 18/18 ctest, `build-dsp/` 8/8, both after
  a build that exited 0. New suites `deesser_dsp` and `deesser`; `rack` went 7
  modules to 8; `ui_layout` gained `checkDeesserPanel`.
- **Identity and accent as decided**, both rows in `products/AGENTS.md`. The
  accent's 6.39:1 and 1.84:1 were confirmed with `Inspect ratio` on real
  renders in both appearances, exactly as §2 predicted. Note the naming: §2
  calls `#ea9f9a` **muted coral** and reserves "rose" for the candidate that
  was not chosen; the table row uses §2's name.
- **Schema as decided**, five parameters, `thresh` a `textParam` reading
  "+3.0 dB over". The shared `textParam` hunk is byte-identical to `c142f37`,
  verified by `git hash-object`.
- **Panel**: band sketch, four knobs, BELL/SHELF, GR meter with IN/GR/OUT, and
  a momentary LISTEN. Nothing in `core/ui` touched; BMO Opto's three hashes
  re-proved unchanged.
- **DSP is a marked placeholder** and a deliberate wire — it does not even
  filter. `DspCore::Params` carries all five parameters in real units, and the
  internal constants 10 §9 lists are declared there as named constants so the
  DSP pass inherits the decisions rather than the numbers.

**Two things the next session should know.**

1. **A render found a fault every test passed over.** At the default Q of 2.5
   the high shelf drew a resonant dip below its corner and a climb back above
   it. `params.h` now carries `kShelfMaxQ = 2.0` and `effectiveQ`, matching BMO
   DEQ. Q stays one parameter whatever the shape. Even capped, the shelf keeps
   a visible overshoot — about 2.5 dB at RANGE 8 — which is what that filter
   does and what DEQ does. **Whether the shelf wants a lower internal ceiling
   is 10 §10.5's open question and was not re-decided here.**
2. **The installed plugins on this machine were overwritten**, including BMO
   Mix Rack, by one full `cmake --build` before targets were being named. A
   Debug build installs by default. The 0.2.5 rack mid Ableton pass needs
   reinstalling from its CI artefact. Build by naming targets.

**Still open from the list above**: step 2's "send Frosty the renders" and step
7's review are his calls and have not happened; the module has not been opened
in a host; nothing has been heard.
