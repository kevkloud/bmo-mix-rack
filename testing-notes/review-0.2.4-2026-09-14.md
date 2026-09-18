# 0.2.4 — the full review, before the listening pass

Written on **AURORA**, 2026-09-14. Frosty asked for a review of the whole
tree ahead of the Ableton pass: every module's DSP, the handoffs against the
code, Opto's attack at heavy reduction, Tune's pops, and the UI. This is the
record: what was verified, what was found, what to listen for, and what
happens next. Six read-only reviews were run over the tree at `42439d7`
(identical to `cbd0939` in everything but two docs commits), each one
running the existing measurement tools and test binaries under
`build-release/`; the Opto attack was measured with a harness written for
the purpose. Nothing in the tree was changed by the review itself. Fixes go
on `review-0.2.4`, this branch, and are listed at the end.

## The build

CI went green on both runs of `cbd0939` while this was being written: the
dispatched run **34834557823** and the tag's own run **34834687703**, all
four jobs each. The two are not byte-identical on macOS (the tag run builds
universal, the dispatch arm64 only; both are fine, the tag run's artifact is
the one to hand to an Intel Mac). AURORA installed `BMO-Windows` from
**34834557823** at 04:30, over the nine bundles that were there, and verified
each against the artifact:

| bundle | SHA-256 |
|---|---|
| BMO DEQ | `c1200d16504d43431a02063762ebca231f1dc3ade07b44966df174f6824afcfb` |
| BMO Dimension | `2cb2458d45e063fa0028ca4ddfe38e993874c5031d1885ee291e9a7a5ecf5e96` |
| BMO EQ | `742b0384e8801d5d25377dfc4facd5d8c93361c07bdcac7e01e0de08939c08f3` |
| BMO Mix Rack | `f8fbc942bf5acafff300746a11494e5aa39a7760455708b360a5bf20de386dc3` |
| BMO Opto | `f41c8a4046c1426df611c32ece0f2511ee33e3df7b6cb341b2836f189a16b259` |
| BMO Saturator | `bd3b9885a2da35643c5348c2402bfcf7d1d079447a678f6bb840d017a8ff60b9` |
| BMO Tune RT | `2b138eaebe727308e5cc2c5bb395cf0cadcd8b3152a4e8d454597bbcff95e97c` |
| BMO Util | `c82ad337b1ec971a81152dbfff482e3638cd85991020baa18e432dd57ebfaf8b` |
| BMO Vcomp | `eb8f70a206c8bfcfc2d8efb033646230d609fc99b224767fff269c208a93a17a` |

Worth knowing: the `BMO Vcomp.vst3` that was installed before this (03:36)
matched none of the four Vcomp builds on AURORA's disk, so the ear pass in
the Vcomp handoff cannot be tied to a commit. Everything from here is tied to
the table above.

## The misses, ranked

Severity: **B** would embarrass the module in the pass or in Kevin's review
and is fixed on this branch; **S** should be fixed before beta; **N** is on
the record so it is not rediscovered. Each names the file so the fix can be
checked rather than trusted.

### BMO Opto — the attack question, measured

The full write-up is `opto-attack-2026-09-14.md`. In short: both cells share
a 10 ms attack constant, and at the drive a vocal actually sees that means
the first one to two milliseconds of every onset pass at unity gain whatever
the reduction (a 12 to 16 dB spike on the front of each word at CRUSH 75),
and the reduction itself takes 9 to 21 ms to reach two thirds of its value.
**Stressed is slower than Tele**, because Tele's feedback loop divides its
constant by three and Stressed's feedforward does not; the checklist calls
Stressed "grabbier" and on attack it is the opposite. Two candidates are
rendered for Frosty's ears in `field-audio/opto-attack-2026-09-14/`, one
giving Stressed a 3 ms attack, one giving both cells a light-dependent
attack the way a photocell has. Nothing changes until he has heard them.

- **N** The Tele drive's DC blocker emits an 8 ms, −56 dBFS sub-20 Hz blip
  when a loud passage ends. Inaudible; recorded because it fools a naive
  release measurement.
- **N** Release, re-read past that blip, matches the 0.2.1 handoff.

### BMO Tune RT

- **S** `modules/tune/dsp/CorrectionLaw.cpp:234` — **the 6.137 s pop is the
  law confirming a note jump with the detector's frozen period.** When
  clarity drops below `clarityLo` the detector stops updating `heldPeriod`
  and re-presents the same period every evaluation; `confirmPitch` cannot
  tell that echo from a fresh estimate, so a jump is "confirmed" by the
  value that proposed it. On Failure at 0 ms this is a D4→C#4→D5→A5→D3 zip
  inside 20 ms and the worst-landing splice in the take (1.95 at 6.146 s);
  at 20 ms only the first splice fires. Guard 6 correctly did not veto it,
  so this is the phrase-end item the round-8 note left open, and it is not
  in the engine. Fix: a frozen period is never an estimate — gate the law's
  use of `e.period` on `e.clarity >= clarityLo` (or a `fresh` flag set at
  `Detector.cpp:288`), and optionally require `clarityHi` to confirm a jump
  of more than three quarters of a semitone. Costs no latency. Frosty hears
  it before it is called fixed.
- **S** `Detector.cpp:328` — the voicing release counter is zeroed by any
  single frame in the 0.60–0.85 hysteresis band, so a decaying note whose
  clarity flickers to 0.61 never closes and the law keeps reading garbage.
  In-band frames should hold the counter, not reset it. Re-measure the
  dropout count (12 on Failure, 11 on Fuji) when it changes.
- **S** `tools/tune/field/main.cpp:187-196` — frames under −45 dB are
  skipped before they are added to the periodic list, so a splice in a
  quiet gap is labelled by whichever louder frame is nearest, up to tens of
  ms away (0 ms Failure, 10.722 s at −52 dB labelled "on pitch"). And the
  ON NOISE exclusion the round-8 note retires: score three ways instead of
  dropping, on pitch / on noise loud / on noise quiet, using the ±25 ms
  level the `--splices` listing already computes, with the audible line
  calibrated on the two Fuji splices Frosty heard.
- **N** Guard 6 is bounded as claimed (upward only, 100 cents, 2 ms hold,
  out of the environment, on by default). Two things to write beside it:
  the hold is really "N evaluations" (four at 500 Hz, one at or below
  125 Hz), and expiry accepts the disputed candidate, so it delays a lobe
  that wins for longer than 2 ms rather than stopping it. On a veto the
  period is the held one but `clarity` is the candidate's; take the min.
  Tested at 48 kHz only; the takes are 44.1.
- **N** `HardTuneTests.cpp:356` — the ratchet constants (0.76 ms / 1.27 c)
  are already rounded up and then multiplied by 1.05 again, so the
  every-run residue ceiling is 1.33 c, above Antares' 1.30. The budget
  table itself is exactly as the note says and is asserted every run.
- **N** `TuneDsp.h:40-49` — **stereo input: L is processed, R is
  overwritten with L.** Fine for a mono clip on a stereo track; anything
  stereo before Tune loses its right channel silently. The tools average
  the channels, so tool and plugin disagree on stereo material.
- **N** `SingleModuleProcessor` has no `reset()` override, so Live's device
  on/off replays up to `rest + T` of stale ring on re-enable, and the 4 to
  13 ms time jump on bypass is inherent to the Live contract. One line to
  add the reset; the jump is the same as Waves.
- **N** Below E2 (Bass and Instrument declare 55 Hz): safe and bounded, no
  overrun; hop 4.5 ms, delay up to ~22 ms while correcting, and no test
  exercises correction below 110 Hz.
- **N** `build-release/` Tune tools in the `-int` worktree are stale despite
  their timestamps (they reject `--splices`); the only guard-6 build of the
  tools on this disk is `../bmo-mix-rack-333-pop/build-dsp`. Rebuild before
  re-scoring anything from `-int`.

### BMO Vcomp

- **B** `modules/vcomp/presets/FactoryPresets.h:78` — **"Keep The Air" is
  AMOUNT 70 with HIGH THRU 6 kHz, which is a +19 dB boost of everything above
  6 kHz.** The thru band takes the full makeup (18.9 dB at 70) with no
  reduction, then drives the limiter, so every ess lands 4 dB over and is
  clipped. The level-match test passes only because the harness voice has
  almost no energy above 6 kHz. "Keep The Chest" was cut from 70 to 35 for
  exactly this reason and the comment says so; "Keep The Air" was not.
  Fixed here the same way, to 35. Frosty may want it elsewhere.
- **S** ~~The THRU runaway~~ — **done on 2026-09-14**, on branch
  `vcomp-thru-cap` off this one, exactly as recommended below: the thru path
  takes `makeup − kneeReductionDb(kReferenceDb − kThruBodyOffsetDb, curve)`.
  Measured against the three candidates in `modules/vcomp/AGENTS.md` and
  against a flat cap at 3, 6 and 9 dB; it wins on tilt and on pumping at once.
  Tilt 4.6/8.8/12.9/17.7 dB across the knob becomes 2.4/1.7/1.1/0.6, pumping
  −1.5 becomes −0.03, and "Keep The Chest" at AMOUNT 70 goes +8.14 dB to
  +1.36. `testThruMakeupCannotRunAway` holds it; 59 checks green, 7 of 7 DSP
  suites. **Not heard, and the presets were left at 35.** The original
  recommendation, which stands as written:
  The THRU runaway, quantified from code and `measure_vcomp balance`:
  tilt +17.7 dB at AMOUNT 90, and the mid band recedes 2.65 dB because the
  boosted low end is what the limiter now acts on. Of the three candidates
  in `modules/vcomp/AGENTS.md`, the review recommends a curve-derived cap:
  the thru path gets `makeup − kneeReductionDb(kReferenceDb − 6, curve)`,
  the net gain the curve gives a signal at body level, which holds the tilt
  near +2 dB across the knob with no dynamics. One line in `DspCore.h:257`.
  Frosty's spec was "everything takes the makeup", so this is his call.
- **S** `DspCore.h:163,253` — the band split engages as a hard switch per
  block: dragging LOW THRU off its rail or toggling COMPLEX with a THRU
  preset loaded swaps the direct path for its LR4 allpass from zero state.
  A click on any held note. Needs a 5–10 ms crossfade.
- **S** `Gate.h:73-86` — the expander's knee is centred on the handle, so a
  syllable 1 dB above GATE is already shaved 3.2–3.8 dB. The panel promises
  "the level it will act at"; shift the knee to end at the threshold.
- **S** `tools/measure/vcomp/main.cpp:313` — the tool's preset table still
  has "Keep The Chest" at 70. Fixed here.
- **S** `tests/dsp/VcompDspTests.cpp` — four of the five ear-pass changes
  are not pinned: gate depth asserts ≥ 20 dB (the old 3:1 gave 36), the
  open-time test measures 5–20 ms so 0.5 and 8 ms both pass, ARC asserts
  > 1 dB (the old scales passed), nothing asserts the thru band receives
  makeup. Five mis-indented blocks at 318, 357, 391, 419, 444 that Kevin
  will see first. Fixed here: indentation; the absolute assertions are
  listed for the next Vcomp session.
- **S** `modules/vcomp/AGENTS.md` and the checklist contradict the code in
  the same file (0.5 ms gate open, ARC 2/5, gate 3:1/50, "the limiter RVox
  has and this does not"). Fixed here.
- **N** `VcompPanel.cpp:233-319` takes neither input nor output section, so
  OUTPUT does not sit on the shared line in a rack. UI pass.
- **N** `LevelBars.cpp:252-259` — clicking the IN caption sets the gate to
  −60; hit-test the well. `VcompPanel.cpp:224-230` re-asserts a switch lock
  30 times a second and repaints it each time.
- **N** The limiter's own reduction is not metered: OUT pinned at −0.1 with
  a quiet GR bar will look wrong when OUTPUT is pushed. Sample-peak only;
  a true-peak meter will show overs.
- Verified: chain order, OUTPUT before the limiter, nothing after it,
  ARC's arithmetic and constants, makeup static in AMOUNT only, LR4
  crossover flat and clamp unreachable at 44.1 k, gate inert at the rail,
  zero latency pinned, begin/end gestures on the handle, no allocation
  after prepare, every preset in range, no test vacuous under the limiter.

### BMO DEQ

- **S** `testing-notes/deq-testing-checklist.md` §3b says "the curve never
  shows a resonant shelf"; a Q-2 shelf overshoots about +6 dB and dips −6
  on ±24 (±4.6 on ±12), measured. Frosty will turn Q up on a shelf and file
  the bump as a bug. Fixed here: the doc. Whether the shelf cap should be
  0.71–1.0 instead is a product call.
- **S** `modules/deq/dsp/DspCore.cpp:142, 301-310` — MID ↔ SIDE is a hard
  swap per sample on a live band; STEREO ↔ either glides. A click. Fix:
  glide β to 0 under the old placement and back under the new.
- **S** Solo and the analyser exist in the engine and are tested; **the
  panel has neither** (no call to `setSolo`, nothing reads the tap).
  `spec/decisions.md` reads as if both shipped. When they are wired: solo
  is a hard output switch and needs the `enable` fade; `AnalyserTap::prepare`
  reallocates while a panel timer may be inside `read()`.
- **S** `DspCore.cpp:186-207` — DYN off, ABOVE/BELOW flip and a shape
  change are 8-sample steps of `offsetDb`, not glides; a de-esser holding
  −9 dB switched off mid-ess steps in 0.17 ms.
- **S** `panel/DeqPanel.cpp:236-263` — `clampShelfQ` arms on mouse-up
  anywhere on the panel and then writes Q back as a host gesture, so a
  Live automation lane holding a shelf's Q above 2 is overwritten by
  clicking a tab. The header comment says the opposite. Restrict to the
  Q knob, the shape dial and the curve.
- **S** No test guards the three silent design fallbacks in `Design.cpp`
  (193, 216-232, 311-322); ceilings run at 44.1 k only, which is how the
  macOS-only T2 failure got through. Today none fires at any of the four
  rates.
- **N** The T2 fix in `e2ca1b3` is a real fix (Schur-Cohn on coefficients,
  exact at the low cut's double zero), not a tolerance move.
- **N** AUTO evaluates 12 designs and 48 points on the audio thread per
  block while a control moves; a quarter of a 32-sample block at 96 k.
- **N** `ResponseView.h:94,99` draws the 48 kHz design whatever the rate;
  the GR bar shows the deepest cut only, so an upward band shows nothing.
- **N** Rack lanes: no On/Dyn/Shape lane, so a cut band cannot be
  automated out in a rack; a rack preset lands compact by design, which
  the checklist §1 does not say.
- Verified: zero latency by construction, T1 across six rates and block
  sizes, overflow round-trip of all 158, no allocation on the audio path,
  Init all defaults in range, stereo link of the detector, no fast-math.

### BMO Dimension

- **S** `tests/plugin/DimTests.cpp` has no preset level check, against the
  rule in `modules/AGENTS.md` step 6 that every other module obeys. Wide
  Vocal measures **+3.5 dB peak, +1.9 dB stereo power** over Init on a mono
  vocal (mono sum unchanged, so a mono-sum test would be tautological);
  nothing downstream catches it because there is no trim. Fixed here: the
  test, measuring stereo power, passing today and recording the numbers.
- **S** `modules/dim/dsp/DspCore.h:86-92, 119-127` — **CENTS at 0 with
  DETUNE on is not off**: `phaseInc` goes to 0 and the two voices freeze
  wherever their sweep was, leaving a static comb whose level depends on
  when the knob got there (0.30 RMS after CENTS 10 → 0, louder than at 10).
  Fix: a smoothed gain on the injected difference, target
  `clamp(cents, 0, 1)`, so 0 fades to silence in 8 ms. Fixed here, with a
  test.
- **S** (listen) DIFFUSE on already-panned material is a 0.4 Hz autopanner:
  ±7.3 dB swing at 70 % on a hard-left 1 kHz tone. "Diffuse Pad" is one of
  the presets the README says needs a stereo source. Swirl or instability
  is Frosty's call; the README should say which.
- **S** (listen) DETUNE on an already-wide source puts a beating ghost of
  every panned element in the opposite channel at about −7.5 dB. The three
  DETUNE presets are effectively mono-source presets and nothing says so.
- **S** No output trim; +19 dB reachable (SHUFFLE 3, WIDTH 200, ASYM 100
  on anti-phase 80 Hz), +4.9 dB from Wide Vocal on a 1 kHz tone. A trim is
  a schema append; product decision.
- **N** `README.md` says WIDTH 0 turns the module off; ROTATE is downstream
  of it (centre source, WIDTH 0, ROTATE +45 → hard right). The README also
  still calls the throb open, which `AGENTS.md` settled on 09-09.
- **N** `tools/measure/dim/main.cpp:487` prints "34.6 dB" as the published
  110 Hz throb; 10 ms windows give 39.9, 50 ms give 25.4. Say both.
- **N** Four DSP assertions are comparisons that could pin the documented
  numbers (shuffle ×3, asymmetry −2.5 dB, rotation 0.1464/0.5464, side RMS
  0.125); no 44.1/96 k test; no block-size test (bit-exact when probed).
- **N** `dim-testing-checklist.md` §3: the 8 ms fade-out / instant-on
  switch is click-free by measurement and **has never been heard in a
  host**. That is the one Dimension item in the pass that gates a claim.
- Verified: side-only by construction (rotation and asymmetry are the two
  documented exceptions, identity at default), latency honest at 0,
  block-size bit-exact, rates agree, smoothing on every continuous
  control, no allocation after prepare.

### BMO EQ (BMO CEQ), BMO Saturator, BMO Util

- **B** `modules/eq/dsp/DspCore.cpp:243-269` — **Phase flips the wet path
  only.** At Mix 50 % with Phase on and a flat EQ the output is (−wet +
  dry)/2, near silence; at Mix 0 the button does nothing. The Saturator
  had this bug and fixed it (`sat/dsp/DspCore.cpp:281-296`). Mix is not on
  the panel but is on the automation lane. Fixed here, with a test at
  Mix 50.
- **B** `DspCore.cpp:190-202` — Auto Gain's target is set after the
  `sameSettings` early return and `sameSettings` ignores `autoGain`, so
  toggling Auto Gain does nothing until the next band move, and then the
  level jumps. Fixed here, with a test.
- **S** `modules/sat/dsp/DspCore.h:306` — `bodyGain = -3.0f` under a
  comment that says "it is +3 now" and that the sign "has to be re-fitted
  rather than assumed". The body band measures 2.9 dB under the reference
  at Drive 40 where the target is −0.7, the band this generator exists to
  fill, in the direction a wrong sign produces. Not proven; one AUTO-off
  render with the sign flipped through `measure_sat compare` settles it.
  Until then: "hollow in the low mids at default" is the thing to listen
  for.
- **S** Hard switches click: Util's polarity and MONO (per-sample sign
  flip, no ramp, `UtilDsp.h:100-136`; gain, pan and width get the 5 ms
  smoother); EQ's EQL, Low Cut and High Cut Off→On and Phase; Sat's SAT
  and Phase. Util's fixed here (the sign rides the smoother). EQ's and
  Sat's are noted; the selector positions themselves glide.
- **S** `testing-notes/opto-0.2.1-handoff.md` §6 and §7 say the Saturator
  bell's tanh soft-limit and the Q 1.40 / +13.5 change coexist. **They do
  not**: Kevin's `51a263b` (2026-09-07) removed the soft-limit in favour of
  the Q/gain fit. The live response is the bell alone, and the ten preset
  levels re-solved with the limiter in still land within 0.005 dB now,
  which confirms it was inert. Fixed here: the handoff says so;
  `sat-voicing` is a listening round and nothing else.
- **N** EQ oversampling: the reported latency is exact (40 / 60 / 70) and
  the dry path is delayed by exactly that; the switch itself resets every
  stage without clearing the dry ring, so it clicks and misaligns for up to
  70 samples. Setup control, not to be automated. The top end differs by
  factor, not only in aliasing (a 62 kHz roll-off engages only at 4x and
  8x). The comment justifying 2x by "the 1073 model's 16 kHz shelf" is
  stale; the 1073 was removed.
- **N** Four EQ parameters have no panel control: High Cut, Mix, Auto
  Gain, Oversampling. Telephone and Mix Bus Sheen depend on two of them.
- **N** There is no module bypass anywhere and no per-slot bypass in the
  rack; EQL and SAT switch a circuit out and keep the latency.
- **N** Saturator even/odd balance crosses at about Drive 75 (+21.7 dB
  even-dominant at 20, −9.6 at 100); Guitar Grit and Ruined are odd boxes.
  Aliasing at the default (Off) has a −54 dB floor. `DriveTables.h:250`
  `kMakeupDb` is a dead table 1.4 dB off its own tool; delete it before
  Kevin asks.
- **N** The CEQ rename needs `PresetInfo` to grow a list of legacy pairs
  walked oldest first (FrostyEQ, then BMO EQ), same extension for the
  second hop; `EqTests.cpp:260` updated. Do not drop the FrostyEQ hop.
- **N** Util's pan is a balance law (−6 dB far side, no centre rise);
  width has no headroom guard above 100. Both documented and tested.
- Verified: preset levels (Sat within 0.005 dB, EQ ±0.5 on pink, Util),
  LC network bounded at every extreme, denormals covered by both hosts,
  `sat_dsp_tests` 197,495 checks green.

### The shared layer, packaging and CI

- **B** `CMakeLists.txt:2` — **every plugin reports version 1.0.0.** The
  project version was 1.0.0 from the first commit and `bmo_add_plugin` passes
  none of its own, so JUCE stamps `1.0.0` into every bundle; the 0.2.4 tester
  build calls itself 1.0.0 in Live's plugin info and to Kevin. Fixed here:
  0.2.4, with a comment. Worth a CI step on tag runs that fails if the tag
  and the project version differ.
- **S** No `processBlockBypassed` in `SingleModuleProcessor` or the rack, so
  the VST3 wrapper's own Bypass parameter passes audio through with no delay
  while the plugin reports latency (EQ at its 2x default, Sat above Off, any
  rack holding them): a host that honours it plays the bypassed signal early
  and the toggle is a splice. Whether Live's device activator routes through
  that parameter is a test item. The fix is a delay line sized to the max
  latency in each processor.
- **S** The CEQ rename's second migration hop, exactly: `PresetInfo` grows a
  list of (folder, extension) legacy pairs, `migrateLegacy` walks all of
  them without overwriting and drops its "only if the new folder is empty"
  gate (which strands anyone who saved one preset before the copy ran),
  `products/eq/Product.h` lists BMO EQ then FrostyEQ, and the sandbox that
  the tests use disables migration entirely, so the chain is untested today.
- **S** ~~`README.md:4-14` says "three modules today"~~ -- **fixed on
  2026-09-14** on `vcomp-thru-cap`. The README table now lists all seven
  modules with their widths, root `AGENTS.md` lists seven modules and all nine
  plugin codes, and `products/AGENTS.md:93` says 159 parameters, agreeing with
  the schema table, `modules/deq/AGENTS.md` and `RackTests.cpp`.
- **S** `.github/workflows/build.yml:78-115` — the plugin jobs fail at
  "Restore fonts" on a pull request from a fork, by design of the step. Every
  stage-5 PR from the fork to Kevin's repo will show macOS and Windows red
  unless the job is gated on `head.repo == repository` or Kevin pushes the
  branches himself. `timeout-minutes: 60` against a 46-minute Windows job is
  thin.
- **S** `scripts/build.sh:71-76` renders four of seven modules and a
  four-module chain; `WORKFLOWS.md` says it renders every panel. Fixed here.
- **S** `core/rack/RackProcessor.cpp:122-193, 466-469` — a rebuild holds the
  chain lock through teardown, construction, a 159-parameter `applyXml` and
  `prepare`, while the audio thread passes dry audio at unity; then every
  engine restarts cold. On the Vocal Chain preset that is a level jump, a
  time slip of the reported latency, then a click. `core/AGENTS.md` calls it
  "a few samples of dry signal". Build the new slots outside the lock and
  swap under it.
- **S** Session restore fires the parameter storm without the preset
  manager's `loading` flag, so every reopened set shows "Init *"; the loaded
  preset's name is not saved at all.
- **N** `RackProcessor.cpp:189` does call `updateHostDisplay` with parameter
  info changed when a slot's module changes, but `SlotParameter::assign`
  resets the value without notifying, and Live is lazy about title changes.
  Test item; the fallback is a `setValueNotifyingHost` per reassigned lane.
- **N** Latency is never set from the audio thread; automating EQ's
  Oversampling resets the oversamplers on the audio thread and clicks
  (consider making that choice non-automatable). `ParamSpec::text` appends
  the unit and `label()` returns it too, so Cubase shows "dB dB"; Live shows
  text only. Vcomp's Gate at its rail prints a dB figure rather than "Off".
- **N** State versions are written and never read; a 0.2.4 rack set opened in
  Kevin's `main` build silently drops its DEQ and Vcomp slots on the next
  save. `RackProcessor::setStateInformation` takes a `MessageManagerLock`.
- **N** Fonts: embedded at build, silent system-font fallback at runtime if
  unparseable; nothing checks CI's decoded secrets against the licensed
  files on either machine. Add a hash to the Restore step.
- **N** Dimension's VST3 subcategory is "Fx|Tools"; imagers are "Fx|Spatial".
  Cosmetic in Live.
- **N** `LayoutTests.cpp` lays out every module standalone, but inside a rack
  only util, eq, sat, opto and deq; dim and vcomp are never laid out in a
  rack, and vcomp's OUTPUT row is not checked against the shared rows.
- Verified from the CI logs: both artifacts carry all nine products (Windows
  9 VST3 + 9 standalone; macOS 9 VST3 + 9 AU + 9 apps); the tag run's macOS
  artifact is universal (108 MB) and the dispatch run's arm64 only (52 MB),
  so an Intel Mac installs from the tag run; Windows binaries identical
  between runs. Registry size 7, all seven banks, the 40-parameter overflow
  test, SlotOverflow lifetime ordering, the identity table against every
  `Product.h` and `CMakeLists.txt`, and the two build switches, all as
  claimed.

## The three checklists that did not exist

Written, on this branch: `ceq-testing-checklist.md`,
`util-testing-checklist.md`, `rack-testing-checklist.md`. And the UI pass
has its own now, `ui-pass-checklist.md`, from renders of every panel in
both appearances.

## What each module's Ableton pass should specifically listen for

The per-module checklists carry the detail. The items this review adds, in
one place:

- **Opto**: the front of words at CRUSH 75 in both modes, then the blind
  set in `field-audio/opto-attack-2026-09-14/`.
- **Tune**: Failure at 0 ms, 6.13–6.16 s, the phrase end; any held-note
  tail at 0 ms for a chirp rather than a click; "spills" on Fuji; device
  on/off mid-phrase; a stereo source into Tune.
- **Vcomp**: Keep The Air on a sibilant take (with the fix, it is 35;
  without, 70); LOW THRU swept with AMOUNT above 60; a click when LOW THRU
  leaves its rail on a held note; the gate handle 1–3 dB under the
  quietest wanted syllable; OUTPUT pushed into the limiter.
- **DEQ**: MID→SIDE on a live +12 dB bell; DYN off mid-ess; a shelf at
  Q 2 (that bump is the shelf, not a fault); all bands off must null;
  32-sample buffer with AUTO on while dragging a frequency.
- **Dimension**: DETUNE toggled on a loud held note (never heard in a
  host); Thicken vs Wide Vocal vs Mono to Stereo, and Wide Vocal's level;
  DETUNE and Diffuse Pad on a mix with a hard-panned element; CENTS to 0
  while playing.
- **EQ**: Init at unity for colour; selector glides; EQL / Phase / cuts
  for clicks; the +12 / +8 overlap; oversampling Off / 2x / 4x at 16 kHz.
- **Saturator**: the low mids at default against the reference; Drive
  70–80 for where warmth turns to grit; TONE 0 vs 100; AUTO across a
  verse/chorus brightness change.
- **Util**: polarity and MONO on a bass note for the click; pan at 50
  against Live's Utility; width 200 mono-summed.
- **Rack**: chain edits while playing; PDC with EQ oversampling in a slot;
  a DEQ band past lane 32 through save and reload; 8 × EQ at 8x CPU.

## Steps forward — the checklist

1. [x] CI green on `cbd0939`, both runs.
2. [x] `BMO-Windows` from 34834557823 installed on AURORA, hashes recorded.
3. [x] Review of every module's DSP against its notes; misses listed above.
4. [x] Three missing checklists written; UI-pass checklist written.
5. [x] Opto attack measured; two candidates rendered blind.
6. [ ] **Frosty's per-module Ableton pass on this build**, one module at a
       time, each against its checklist, naming the machine. Fill in the
       Opto blind form and the Tune timestamps.
7. [x] The **B** fixes and the cheap **S** fixes on this branch (`240de6d`):
       EQ phase and auto gain, Vcomp Keep The Air and the tool table, Util's
       switch ramps, Dimension's CENTS 0 and preset level test, the project
       version, the snapshot script, the doc corrections. DSP-only 15 of 15
       and the full Release `ctest` 26 of 26 on AURORA. Not yet through CI
       and not yet installed: the installed build is still `cbd0939`.
8. [ ] The ear-gated changes, each on its own branch with renders:
       - [x] **Tune's frozen-period confirmation**, branch `tune-phrase-end`
             off this one, blind set `field-audio/blind-2026-09-14-round9/`
             against Antares and the shipped guard 6; the numbers are in
             `tune-blind-round9-2026-09-14.md`. Frosty ranks it.
       - [x] **Opto's attack**, two candidates rendered in
             `field-audio/opto-attack-2026-09-14/`; nothing in any tree.
       - [ ] Vcomp's THRU cap; Dimension's DIFFUSE/DETUNE wording or
             behaviour; the Saturator's `bodyGain` sign, which needs one
             render Frosty has.
9. [ ] The click fixes that need a design: Vcomp's band-split crossfade,
       DEQ's MID↔SIDE glide and DYN-off glide, EQ's and Sat's switches.
10. [ ] Merge into `integration`, dispatch CI, install the new bytes, a
        second short listening round on what changed.
11. [ ] **The UI pass**, alone, on `ui-pass`, from `ui-pass-checklist.md`:
        the CEQ rename, `utilGain`, Vcomp's output section, Dimension's
        marks, DEQ's solo and analyser, Tune's empty middle, contrast
        assertions.
12. [ ] Stage 4 as `WORKFLOWS.md` describes it: one CI build on
        `integration`, both machines install by hash, look and sound.
13. [ ] Stage 5: Frosty's pull requests to Kevin, DEQ, then Tune, then the
        UI pass, each rebased onto `main`.
