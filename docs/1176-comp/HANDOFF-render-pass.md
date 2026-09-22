# Handoff: BMO FET render and visual pass

For a separate session that owns the look of `modules/fetcomp`. Written on
AURORA, 2026-09-20. The session that wrote this keeps the DSP and the de-esser
groundwork; this one keeps the panel.

> **The pass is done, AURORA 2026-09-21.** Write-up and every figure:
> `testing-notes/ui-pass-fetcomp-2026-09-21.md`. Steps 1 and 3 were already
> complete; 2, 4, 5 and 6 are now.
>
> **Step 2's three deviations, all settled on renders:**
>
> - **The voicing colours the controls, not just the bezel.** Blue takes the
>   accent, Black takes the plate's opposite — black on pale, white on dark.
>   The parameter defaults to Black, so the default panel is monochrome.
> - **The accent takes a dark-plate variant**, `#8fb4e6`, because `#5489d4`
>   was chosen against `#efefef` and misses both of `faceOf`'s bands on the
>   dark plate: cap 3.80:1 and pointer 3.97:1 become 6.34 and 6.61, both
>   mid-band. The pale plate keeps `#5489d4`.
> - **MIX ships neutral grey**, `#b0b0b0` / `#585858`. The azure cleared the
>   lifted accent by only 1.05:1; white and black were rendered and rejected
>   for putting the quietest control at over twice the character knobs'
>   contrast.
> - **The switches keep the accent.** `switchAlt` was rendered: `#5489d4`
>   against `#4fb8e8` is 1.58:1 and 16.4° of hue, two blues almost-but-not-
>   quite the same. The deviation is deliberate.
>
> **Step 4 is done and the needle pins:** meter crops at 30.3 dB and 35.4 dB
> GR are byte-identical, and the rest position is genuinely 0 dB — a true
> 0 dB render and the at-rest render produce identical meter crops.
>
> **The hashes below are superseded.** `65a43d6afd63e58b` and the rest do not
> reproduce: the DSP landed and the control colour changed. Current hashes are
> in the new note. Also note `tools/snapshot` appended rather than truncating
> until `c1607d7`, so **any hash taken from a path rendered more than once
> before that commit is suspect** — the tool reported success and left the
> previous image in place.
>
> BMO Opto's three and BMO Saturator's one are re-proved unchanged.
> `core/ui` was not touched.


> **Update, AURORA, 2026-09-21.** Four things below went out of date the day
> after it was written, and the rest still stands.
>
> - **The branch is pushed and PR #22 is open**, `frosty-add-bmo-fetcomp` into
>   `main`, fifteen commits. "Nothing on the build branch is pushed" and the
>   instruction not to push are both spent. CI had never seen the branch until
>   that PR; it does now.
> - **It is no longer stacked on `frosty-fetcomp-groundwork`.** The branch is
>   0 behind `origin/main`, and PR #19's head `c4bea4a` is an ancestor of it,
>   so #22 contains the groundwork pack outright.
> - **The DSP has landed**, `80e6221`. `modules/fetcomp/dsp/` is five headers,
>   committed, and the suite is green again on AURORA: `build-dsp` with
>   `BMO_DSP_ONLY=ON` exits 0 with no error lines, ctest 16/16. The
>   `adoptStateFrom` compile failure recorded below is fixed. The "do not
>   collide with the DSP work" section is therefore about a session that has
>   ended -- the tree is yours -- but the rules in it about the schema, the
>   rack target and the untracked docs are permanent and still apply.
> - **Step 4 is now unblocked.** The GR needle was at rest in every render
>   because the DSP was a placeholder; it is not a placeholder any more.
>
> What the DSP still owes is `HANDOFF-dsp-fixes.md`, and the measurements are
> `testing-notes/fetcomp-dsp-2026-09-21.md`. Neither is about the look.

## Where things stand

- Branch `frosty-add-bmo-fetcomp`, stacked on `frosty-fetcomp-groundwork`
  (PR #19, docs only). Nothing on the build branch is pushed.
- `c142f37` is the module skeleton: permanent schema, real panel, registration,
  presets, placeholder DSP. Built and tested on AURORA: 18/18 ctest in `build/`
  (Debug), 16/16 in `build-dsp/` (Release).
- One uncommitted edit on top: `modules/fetcomp/panel/FetcompPanel.cpp` now
  ships the VU bezel at **full alpha** (`kBezelAlpha = kFullBezelAlpha`;
  `ui.bezel=stock` still renders 0.7). Frosty took that call on the renders. It
  has not been compiled yet.
- Renders so far, in `snapshots/` (gitignored, never commit them):
  `fetcomp-{dark,light}-{blue,black}-{stock,full}.png` and
  `fetcomp-bezel-gate-sheet.png`. The GR needle is at rest in all of them
  because the DSP was a placeholder.
- The UI pass note is `testing-notes/ui-pass-fetcomp-2026-09-20.md`. The tools
  and the step order are in `docs/1176-comp/11-integration-and-test-plan.md`
  section 4 (`tools/snapshot`, `tools/inspect`, `ui_layout_tests --dump`,
  `.bmo-fontdir`). `scripts/build.sh --snapshots` was broken and is fixed in
  `c142f37`.

## Already done by the render session (AURORA, 2026-09-20, about 18:20)

Build, render and measure only; no tracked file edited. Step 1 of the pass
below is therefore complete.

- The full-alpha edit compiles. Shipped FET hashes at full alpha: dark
  `65a43d6afd63e58b`, light `cdb63925bd4911bd`, IN `7c5c9d79601dfb6d`, OUT
  `eb631cc29987997e`. Renders are `snapshots/fetcomp-ship-*.png`.
- Measured on the shipped renders: Blue `#5489d4`, Black `#000000`, pair
  5.91:1, Blue on face 2.65:1, Black on face 2.23:1.
- BMO Opto re-proved unchanged: all three hashes match.
- Layout dump and gaps unchanged; largest bare band 29 px at 118..147 in both
  appearances.
- **The suite is not green right now.** `rack_tests` failed to compile against
  the DSP agent's half-written files (`DspCore.h:380` calls
  `StaticStages::adoptStateFrom`, which `Stages.h` did not yet declare). ctest
  then ran the stale `rack_tests.exe` and printed 18/18, which is not
  evidence. The DSP agent has been told; a green only counts after a build
  that exits 0.

## Do not collide with the DSP work

An agent is implementing the real DSP **in this same working tree** and is
building in both `build/` and `build-dsp/`. Until it reports back:

- Do not touch `modules/fetcomp/dsp/`, `tests/dsp/FetcompDspTests.cpp`,
  `tools/measure/fetcomp/`, or `modules/fetcomp/params.h`. The schema is
  permanent: no id, order, range, default or choice-order changes, ever.
- Do not build in `build/` or `build-dsp/` at the same time as it. Either wait
  for it, or configure a separate tree (for example `build-ui/`) and build only
  the snapshot tool and the UI tests there.
- Do not commit, switch branches or stash in this tree while it runs. Better:
  work in your own git worktree of `frosty-add-bmo-fetcomp` and hand back a
  branch or a patch.
- Never build or install the rack plugin target. The installed 0.2.5 rack is
  mid Ableton pass on this machine. The standalone `BmoFet_VST3` product is
  allowed; it installs a Debug "BMO FET.vst3" and overwrites nothing else.
- The de-esser and delay docs (`docs/deesser/`, `docs/delay/`) are untracked
  here on purpose. Leave them out of every commit.

## Decided, do not reopen

- Accent `#5489d4`, an owner-approved exception to the hue and contrast rules.
- Voicing shows as the VU bezel: accent blue for Blue, literal black for Black,
  full alpha. Only the bezel changes; the hot zone keeps the accent.
- The VU is the shared `ui::DynamicsMeter`. Its GR range stays 24 dB and pins.
  The bezel alpha is an opt-in setter that defaults to 0.7. **BMO Opto's three
  hashes must not move:** `ab3ff3b77116b7a5` (dark), `878cca7b1a80a551`
  (light), `88a7653a82c19ae0` (GR dark). Re-prove them after any change to
  `core/ui`.
- Attack and release are knob positions 1-7, 7 fastest; the value string shows
  the position and the time, in ASCII ("us", not the micro sign).
- No hardware or third-party brand names in code, docs or UI strings.

## The pass

1. Build the current tree, including the full-alpha edit, and re-render the
   default set: both appearances, both voicings. Confirm the full-alpha pair
   still measures about 5.9:1 between the two bezels on the meter face.
2. Open deviations from the skeleton, for Frosty to settle on renders:
   - Knob captions use the raw accent (suite convention since 0.2.3) where
     11 section 4c suggested neutral text. Measured 3.80:1 dark, 3.09:1 pale.
     Render both treatments side by side.
   - Dark-plate knob cap: cap on plate 3.80:1 and pointer on cap 3.97:1,
     against the suite's 5.87-6.84 and 6.13-7.14. Propose a fix that stays
     inside `modules/fetcomp/panel/` and render it.
   - Active switches light in the accent rather than `switchAlt` (16.4 degree
     hue clash). Render the alternative if there is a sensible one.
3. Run the suite's layout checks on the panel: `ui_layout_tests`, caption
   lifts and gaps with `Inspect gaps`, contrast with `Inspect ratio`. A caption
   lift only holds for the box it was measured on, so measure every box.
4. Once the DSP lands, render the states that need it: GR showing at about 5,
   15 and beyond 24 dB (needle pinned, no wrap), all-buttons ratio, each
   oversampling choice, and the value strings at positions 1, 4 and 7.
5. Check the module in the rack context as a render only (snapshot tool), not
   by building the rack plugin.
6. Record final hashes and every measured figure in the UI pass note, naming
   AURORA. Send Frosty the renders; decisions on look are theirs.

## Reporting

Say plainly what was rendered, what was measured and what was only reasoned
about. If a tool could not run, say which and why. Commit locally on the build
branch only when no other agent is mid-build, and do not push.
