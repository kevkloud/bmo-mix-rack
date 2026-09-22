# BMO Defang — skeleton and first renders

**On AURORA, 2026-09-20.** Branch `frosty-add-bmo-defang`, worktree
`../bmo-mix-rack-333-defang`, from Kevin's `origin/main` plus the de-esser docs
(`7301150`). One local commit, nothing pushed, no CI.

**Nothing here has been heard.** The DSP is a marked pass-through placeholder,
so every figure below is a render or a build, never a listening result. The
listening pass, the `P_ref` fit and the reference-blend decision are all still
ahead — `docs/deesser/11-integration-and-test-plan.md` §5.

Tools per `testing-notes/ui-pass-render-loop.md`.

---

## 1. Build and tests

Both trees configured fresh in this worktree. **The build exit code was checked
before any ctest count was believed**, which is the trap
`ctest-stale-binary-trap`: a failed compile leaves the old exe and ctest runs
it happily.

| tree | configure | build | ctest |
|---|---|---|---|
| `build/` Debug (VS 2022, multi-config) | exit 0 | exit 0 | **18/18 passed** |
| `build-dsp/` `BMO_DSP_ONLY=ON` | exit 0 | exit 0 | **8/8 passed** |

New suites: `deesser_dsp` (JUCE-free, 8 cases) and `deesser` (schema, value
strings, latency, state, presets). `rack` went 7 modules to 8. `ui_layout`
gained `checkDeesserPanel`.

JUCE was cloned locally from the main tree's submodule rather than from the
network — `--reference` refused it, the main tree's copy being shallow. Fonts
come from `.bmo-fontdir`, which points at the licensed folder outside the repo.

## 2. What ui_layout caught, and what only a render caught

Two real defects, and **the second is the one worth recording**.

**`THRESHOLD` overflowed its caption box by 19.8 px.** Caught by
`checkCaptionsFit` on the first run — the MAKEUP → MAKEU class of fault, which
once survived a whole release. The caption is now `THRESH`, which is what BMO
DEQ's band already prints; the *parameter* is still named "Threshold" where a
host shows it.

**The high shelf was not a shelf, and every test passed.** At the default Q of
2.5 the sketch drew a resonant dip below the corner and a climb back above it.
The schema test passed, the layout test passed, the arithmetic was correct —
**only looking at the render showed it.** `modules/deesser/params.h` now carries
`kShelfMaxQ = 2.0` and `effectiveQ`, the same rule and the same figure BMO DEQ
carries for the same reason. Q stays one parameter whatever the shape, 0.7 to 6
on the knob in both, and the shelf's own limit is applied behind it.

Confirmed on a render: at the "Bright Vocal Shelf" preset's Q of 0.9 the shelf
is clean and monotone (`deesser-dark-shelf-preset.png`).

**Still open, and it is the DSP pass's call, not this one's.** Even capped at
2.0 the shelf shows a visible overshoot below its corner — about 2.5 dB at
RANGE 8. That is what the RBJ shelf does at that Q and it is what DEQ does too,
so the picture is honest rather than wrong. Whether the shelf wants a lower
internal ceiling is `10-dsp-spec.md` §10.5's open question; it was **not**
re-decided here.

## 3. Renders — AURORA, `snapshot` at 2x, 520 x 1480

| render | hash |
|---|---|
| `deesser-dark-bell.png` | `3802788d6cbcff0e` |
| `deesser-light-bell.png` | `88d2ba496aa108c2` |
| `deesser-dark-shelf.png` | `6c74d57f5a10295c` |
| `deesser-light-shelf.png` | `7b72b9fb46432d26` |
| `deesser-dark-listen.png` | `5f3f79e14ba749cf` |
| `deesser-light-listen.png` | `16216900bea8237f` |

Also rendered, not hashed: `deesser-dark-shelf-preset.png` (the shelf at Q 0.9,
THRESH +3), `deesser-dark-female.png`, and
`rack-dark-with-deesser.png` — the module in a rack beside Util, CEQ and DEQ,
**as a render only**. The rack plugin target was not built for it; `snapshot`
hosts the rack product in-process.

All renders are in `snapshots/`, which is gitignored. Nothing was committed.

## 4. Shared code — BMO Opto is byte-identical

The one shared hunk this branch carries is `textParam` / `textFn` in
`core/state/ParamSpec.h` and `core/state/Parameters.h`, reproduced
**byte-identically** from the BMO FET skeleton (`c142f37`) so that either
branch can land first — verified by `git hash-object` against
`c142f37:<path>`, `4842c175970768a4a990031a4ffffa9673a241e6` and
`f9deb507276bbe0bccf92f1dae37f21f46d56d80`. Nothing in `core/ui` was touched;
the meter takes the stock 0.7 bezel.

Re-proved anyway, because the rule is that a shared change proves the renders:

| render | recorded | measured here |
|---|---|---|
| `opto`, `signal=-18`, dark | `ab3ff3b77116b7a5` | `ab3ff3b77116b7a5` ✓ |
| `opto`, `signal=-18`, light | `878cca7b1a80a551` | `878cca7b1a80a551` ✓ |
| `opto`, `ui.meter=GR`, dark | `88a7653a82c19ae0` | `88a7653a82c19ae0` ✓ |

## 5. The accent, measured rather than computed

`docs/deesser/11-integration-and-test-plan.md` §2 asks for the accent to be
confirmed with `Inspect.exe ratio` on a real render rather than by formula.
Done, both appearances. The caption ink is the raw accent in both, which is
what `PlainKnob` does.

| | measured | §2 predicted |
|---|---|---|
| `#ea9f9a` on dark plate `#2e2e32` | **6.39:1**, ΔL\* 53.6 | 6.39 ✓ |
| `#ea9f9a` on pale plate `#efefef` | **1.84:1**, ΔL\* 21.8 | 1.84 ✓ |

Other figures off the same renders:

| what | dark | light |
|---|---|---|
| knob cap | `#ea9f9a` raw accent | `#f4cfcc` wash, 1.25:1 on plate |
| band-sketch curve on its well | `#f4cfcc` on `#1b1b1f`, **11.97:1** | `#7a5350` on `#d6d6d6`, **4.54:1** |
| meter bezel on `meterFace` `#464649` | 4.45:1 | 4.45:1 |

The sketch's ink is `ui::accentInk` against the well, not the raw accent, which
is why it clears in both directions; the pale-plate cap's 1.25:1 is the
suite-wide `faceOf` wash and not this module's doing.

**Naming.** The handoff calls the accent "rose"; §2's table calls it **muted
coral** and reserves "rose" for candidate F, `#f99a94`, which was not chosen.
The hex is the same either way. The Accents row uses §2's name.

### Gaps

`Inspect gaps`, identical in both appearances: bands of 25, 33, 13, 37, 30, 21,
24 and 15 design px. **Largest bare band 37 px at design-y 257-294**, between
the FREQ/Q row and the THRESH/RANGE row. BMO Opto's largest is 48 px, so this
panel has no dead zone worth acting on.

## 6. Looked at, and what changed because of it

Every render above was opened and read, not just hashed.

- **The FREQ marker was a full-height rule and read as a divider**, splitting
  the sketch into two halves it does not have. It is now a drop line hung under
  the curve, drawn under the stroke, at 0.35 alpha.
- The shaded area between the curve and unity went 0.22 → 0.28 alpha; at 0.22
  the notch read as a line rather than as something taken out.
- The sketch is deliberately **to scale** — 0 dB near the top, −20 dB at the
  floor — so at RANGE 8 the lower half of the box is air. That is the rest of
  the range, and it is what makes moving RANGE visible. Left as it is.
- LISTEN lights in `switchAlt` azure and is unmistakable against the rose
  (`deesser-dark-listen.png`). Every switch on the panel takes `switchAlt`:
  the accent is at hue 3.8° and the azure at 198.8°, so no exception to
  `modules/AGENTS.md`'s switch table was needed or taken.
- `+3.0 dB over` sets correctly on the value line under THRESH
  (`deesser-dark-shelf-preset.png`), ASCII throughout, as the licensed faces
  require.

## 7. Asserted, not rendered

- **Latency 0 at 3 × 3 × 3 × 3 × 2 parameter combinations**, plus across a
  re-prepare and a reset. Permanent; there is no lookahead and no oversampling.
- **Listen's lifecycle**: `onHeld(true)` → `setSolo(0)`, `onHeld(false)` →
  `setSolo(-1)`, the panel's destructor clears it, `ui.listen=on|off` drives
  the same code, and an unrecognised value is refused. State XML carries no
  listen or solo attribute.
- **The band sketch against numbers** (`checkDeesserPanel`): the bell is RANGE
  deep at FREQ and unity at 200 Hz and 19 kHz, it follows RANGE and FREQ, and
  the shelf is unity below its corner and reaches RANGE above it.
- **The absences**: no `adapt`, `kappa`, `mix`, `attack`, `release`,
  `lookahead`, `oversampling`, `link`, `listen`, `sidechain`, `width`, `mode`
  or `sensitivity` parameter, and `shape` is last so anything new can only
  append.
- Value strings at both ends of every knob, ASCII-checked.

## 8. Not done here

- **Not heard.** No listening pass, no `P_ref` fit, no reference-blend
  decision. `measure_deesser` is registered and answers `latency` and
  `constants`; every detector mode arrives with the DSP.
- **Not loaded in a host.** The standalone product builds; it has not been
  opened in Live on this machine.
- Frosty has not reviewed the renders. Every call above is a defect fix or a
  documented rule, not a taste decision.

## 9. One thing went wrong, and it is not in the diff

**The installed plugins on AURORA were overwritten.** A full
`cmake --build build` was run instead of naming targets. Debug builds install
into `C:/Program Files/Common Files/VST3/` by default — `BMO_INSTALL_AFTER_BUILD`
is TRUE for any non-Release build (root `CMakeLists.txt`) — so **BMO Mix Rack,
CEQ, DEQ, Dimension, Opto, Saturator and Util were replaced with Debug builds
of this branch at 21:31 on 2026-09-20**, and a new `BMO Defang.vst3` was
installed alongside them.

The 0.2.5 rack that was mid Ableton pass on this machine is therefore **not the
binary that was being tested**. Reinstall it from its CI artefact before the
pass continues. Nothing in the repository was harmed and no test result above
depends on it.

Everything after that point was built by naming targets
(`deesser_dsp_tests deesser_tests rack_tests ui_layout_tests snapshot
measure_deesser`), none of which is a plugin target, so nothing further was
installed.
