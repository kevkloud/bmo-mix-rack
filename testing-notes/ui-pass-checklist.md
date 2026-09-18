# The UI pass — checklist

Written on **AURORA**, 2026-09-14, from renders of every panel at `cbd0939`
in both appearances (`snapshot` and `bmo-tune-snapshot`, with `signal=-18`
so meters read). Stage 3 of `WORKFLOWS.md`: runs alone, after the per-module
listening pass, on branch `ui-pass` off `integration`. Read
`docs/ui-workflow-brief.md` and `testing-notes/ui-editor-handoff.md` first;
the rules a panel inherits are in `modules/AGENTS.md`.

**The loop:** `scripts/build.sh --snapshots`, or one render with
`build/tools/Debug/snapshot.exe <id> out.png appearance=dark|light`, then
`tools/inspect` for any colour or alignment claim. Both appearances, every
time. Hash renders before and after any change that should move nothing.
`ui_layout_tests` must stay green; it pins the shared rows.

## A. Suite-wide, in this order

- [x] ~~**BMO EQ → BMO CEQ rename.**~~ **Done 2026-09-17** with the module's
      own pass: product name and folder, PRODUCT_NAME, `kModuleName`, the
      identity table, README and the packager README, which gained a "BMO EQ
      users" paragraph. `Fsty`, the bundle id, module id `eq` and the schema
      stayed. The second preset hop went in with it -- `PresetInfo::legacy` is
      a list, walked **newest first** rather than oldest as this said, because
      "never overwrite" makes the first folder listed the winner and the newest
      copy is the one the user last edited. `ui-pass-ceq-2026-09-17.md` §2.
- [ ] **`utilGain` placeholder** (`core/ui/Tokens.h`, `#9c71c3`): Util's
      VOLUME reads as a Dimension control in a rack (hue 272° against
      Dimension's 271.6°) and measures about 3.6:1 on the dark plate, under
      every shipped accent. Options with numbers: (1) Util's own green
      `#7fc98a`, so VOLUME is simply the module's colour like every other
      module's headline knob; (2) the utility azure `#4fb8e8`, the colour
      every panel's trim knobs already wear, arguing VOLUME is a trim;
      (3) a fresh hue in the 40–70° gap (gold `#e8c95a`, 8.33:1 dark) that
      nothing else uses. Frosty's call; (1) is the one that needs no new
      colour.
- [ ] **Contrast assertions** (`docs/ui-workflow-brief.md` §4): a pure
      function of the tokens, no rendering. Every (ink, ground) pair the
      tokens permit gets a floor. Cheapest open item in the brief.
- [ ] **Rest-dot check** on every knob whose default is not at an end.
- [ ] **Header bars and accents** read as different modules side by side:
      DEQ teal next to the azure trims, Vcomp periwinkle next to Dimension
      lavender, Tune lime on the pale plate (1.29:1, Frosty kept it).
- [ ] Every caption fits in both appearances (`ui_layout_tests`), and no
      hex outside `Tokens.h`.

## B. Per panel, what the renders showed

### BMO Util (160 design, 320 rendered)
Module 6 of the pass, 2026-09-17 on AURORA. `ui-pass-util-2026-09-17.md` has
the numbers and the rejected candidates.
- [x] ~~VOLUME colour (above).~~ **Settled 2026-09-14**, the utility azure
      `#4fb8e8` (`aff0282`). Not re-opened this pass.
- [x] ~~Two large empty bands … either the knobs grow or the panel says why.~~
      **The knobs grew** (`a2e5a63`): one 150 px row for all three, so VOLUME
      stopped being the smallest knob on the module named after it, and the
      band under MONO went from 46 px to 22. The worst band is now 38 px above
      VOLUME, against EQ's 21 and BMO Opto's 48.
- [x] **PAN's ends read L and R** (`db63acb`), the marks Dimension's TURN and
      TILT take. WIDTH keeps its minus and plus: it rests at 100 of 0..200, so
      it cuts and boosts around its rest dot, which is what those marks are for.
- [x] **WIDTH dims while MONO is on** (`db63acb`) -- the sum lands before the
      mid/side stage, so it has nothing left to scale. The full dim, caption
      included, and that is on the record: a knob whose name stays bright while
      its face goes pale reads as broken rather than asleep.
- [x] **VOLUME and PAN print their values; WIDTH reserves the line and prints
      nothing on it** (`a2e5a63`), so the three names stay level.
- [x] **Names 11 px closer to their knobs** (`a2e5a63`), matched by measurement
      to BMO Opto (38 render px face to caption) and LTV Comp (39).
- [x] ~~The rack's shared switch rows: ØL/ØR sit below the row EQ and the
      Saturator use.~~ **Settled 2026-09-17, Frosty, on a six-slot rack render:
      the placement is right as it stands and Util does not move.** The numbers,
      so nobody re-finds this: EQ and the Saturator put their switches on the
      shared output row, 575..600, with a trim knob under it at 602..679. Util
      adopts neither half of that section -- it has no output stage -- so its
      polarity pair centres in the body the section reserves, 595..620 and
      637..662 against a body of 574..683. The two land 20 px apart *because*
      one is a switch row with a knob under it and the other is a pair with the
      whole body to itself. The lower rule, which is the alignment that carries
      the rack, does line up: row 566 on all three, test-pinned.

### BMO CEQ (280)
Module 8 of the pass and the last, 2026-09-17 on AURORA.
`ui-pass-ceq-2026-09-17.md` has the numbers, the preset chain and the shapes
that were turned down. This entry said "only the rename" and the pass found
three controls with no home, which is the argument for walking every panel.
- [x] ~~Settled; the most finished panel in the suite. Only the rename.~~
- [x] **Renamed to BMO CEQ**, with the second preset-migration hop under it: a
      list of legacy folders walked **newest first** (not oldest, as the
      handoff said -- see the note for why that is the same rule stated
      backwards), `.bmoceq`, the empty-folder gate replaced by a one-shot
      `.migrated` marker, and the chain tested for the first time.
- [x] ~~Four parameters have no control.~~ **Two now**: Oversampling took a
      named section of 2x / 4x / 8x, and Auto Gain took the switch-row place
      HI-Q left. High Cut and **Mix** stay host-only, Mix deliberately --
      Frosty took a rendered MIX knob off on sight, and the parameter survives
      only because removing it would shift the two after it in saved sessions.
- [x] **HI-Q moved to the mid bell**, square, in the margin the dial was not
      using. It needed `SwitchButton::setLabelSize`: a switch's label is 62% of
      its height, so a square one cannot fit its own text at any size.
- [x] The bands paid for the section: rows 112 -> 95, dial 59.5 -> 50.5 design
      px. Four other shapes were rendered and turned down; see the note.
- [ ] Low-cut crowding is parked at Frosty's request; leave it.
- [ ] The legends EQL / LO-CUT / HI-Q: still not looked at, lowest priority.

### BMO Saturator (260)
Module 7 of the pass, 2026-09-17 on AURORA. `ui-pass-sat-2026-09-17.md` has the
numbers and the rejected candidates. The entry below said "settled" and the pass
found four things anyway, which is the argument for walking every panel.
- [x] ~~Settled. DRIVE, TONE, MIX, three switches, both sections taken.~~
- [x] **DRIVE's name is 28 pt**, against the suite's 15, on the one control the
      module is named for. Rendered at 20, 24, 28 and 32. Its row grew by what
      the caption takes, so the face did not pay for its own name.
- [x] **Caption gaps match the suite**: DRIVE and the pair sat 74 and 68 render
      px off their faces against the 30 that BMO Util, BMO Opto's MAKEUP and LTV
      Comp all measure. Both are 30 now, by Util's `setCaptionLift`. INPUT and
      OUTPUT were already right and were not touched.
- [x] **Each section centres its own ink**, rather than the middle centring as
      one block with the slack pooled above DRIVE and under MIX.
- [x] **Oversampling has a control** -- a rule and three switches, 2x / 4x / 8x,
      with Off the position none of them lights. It had been on the schema and
      nowhere on the panel since 0.2.0. `checkSatOversampling` pins the row and
      the radio behaviour, since a render of the default state cannot tell a
      working loop from a broken one: nothing is lit either way.
- [x] DRIVE's row gives that section 24 px so it has air. The face goes 136
      design px to 121; Frosty's call against keeping the row whole.
- [ ] **BMO EQ has the same gap**: oversampling, no control, and it defaults to
      2x rather than Off. That is module 8's, not this one's.
- [ ] Host names `Sat In` and `Auto Gain` against the captions SAT and AUTO.
      Frosty, 2026-09-17: fine as they are, unlike Dimension's. Left alone.
- [ ] The white pointer on the pale knob faces measures 1.42:1 in light. Frosty
      called it okay. It is suite-wide, not the Saturator's.

### BMO Opto (220)
Module 5 of the pass, 2026-09-17 on AURORA. `ui-pass-opto-2026-09-17.md`
has the numbers and the rejected candidates.
- [x] Opening state checked first: GR at rest clears its printed `0`.
- [x] **GR scale** evenly ticked every 2 dB above 6 (`30443bf`).
- [x] **IN/OUT scale** is GR's arc mirrored, from -24 to +3 (`db91b67`).
- [x] `snapshot` renders a metered Opto reproducibly (`bfbafc4`).
- [x] Settled by the 2026-09-11 decisions: TELE / ELD / COLOR names, LINK
      stays. Engaged colour red in Tele, amber in Stressed, both renders
      confirm.
- [x] ~~Takes no output section on purpose … decide once whether that is
      right now that Vcomp also has an output knob.~~ **Settled 2026-09-14:
      it is right, and Opto does not move.** LTV Comp's knob turned out to be
      a makeup stage rather than an output trim, so it is captioned MAKEUP and
      keeps off the shared line for the same reason this one does. The two
      compressors agree, and neither takes the section. See the LTV Comp entry
      for the rejected candidate and its numbers.

### BMO Dimension (220)
Module 4 of the pass, 2026-09-16/17 on AURORA. `ui-pass-dim-2026-09-17.md`
has the numbers and the rejected candidates.
- [x] ~~L / R end marks on ROTATE and ASYM.~~ **Done**, on both, Blender at
      12 pt. ASYM's + turned out to lean *left*; its DSP sign was flipped so
      the R is true (test-pinned, **not yet heard**).
- [x] ~~DETUNE switch scope.~~ **Settled by renaming and bracketing**: the
      switch is GENERATE, centred over DETUNE and DRIFT, and each pair has a
      bracket. Stacking it on DETUNE alone was rendered and not taken.
- [x] ~~FREQ caption is generic.~~ **Renamed**: SHUFFLE / FREQ are BLOOM /
      BELOW, and BELOW prints its frequency. The knob stays.
- [x] Every control renamed, host names to match: GENERATE, DETUNE, DRIFT,
      DIMENSION, BLOOM, BELOW, TURN, TILT, plus Drift Rate and Drift Depth
      for the two hidden ones. IDs unchanged.
- [x] Two legends, SOURCE and WIDTH. DIMENSION grown 92 -> 148 px and the
      SOURCE block tightened.
- [ ] No output trim and no meter. The DSP review measured +3.5 dB peak
      on Wide Vocal and +19 dB reachable; a trim is a schema append (new
      parameter at the end of `specs()`, default 0, `kVersionHint` bump),
      so it is a product decision, not a panel one. Untouched.
- [ ] Light mode: the lavender captions measure 1.73:1 on the pale plate.
      A suite question now (Dim is mid-pack), waiting to be asked as one.

### BMO DEQ (320 compact / 600 expanded)
- [ ] Solo and the analyser exist in the engine and are tested, and the
      panel has neither. `spec/decisions.md` reads as if both shipped.
      Either wire them in this pass or say in the decisions file that they
      wait.
- [ ] GR bar shows the deepest cut across bands; an upward band shows
      nothing. Label or redesign.
- [ ] The response view draws the 48 kHz design whatever the rate; up to
      about 1 dB off in the top octave at 44.1 or 96 k. Needs the rate in
      `ModuleContext` or a note.
- [ ] Compact view: band tabs 7–12 in a second row, the dynamics block
      greyed when DYN is off. Reads well; check the 320 width in a rack
      next to EQ.

### LTV Comp (260)
- [x] ~~**Takes neither section.** OUTPUT floats mid-panel … take the output
      section (the knob is a trim by function).~~ **Settled 2026-09-14, and
      the item's premise was wrong twice over.**

      The knob is **not a trim**: it is makeup on top of AMOUNT's automatic
      makeup, which the parameter's own spec comment always said and the
      caption never did. It is captioned **MAKEUP** now — the same control as
      BMO Opto's, down to range, step and default. So neither compressor takes
      the shared output line, and they agree for a stated reason instead of by
      accident. The second wrong premise was "Opto and Vcomp disagree": the
      layout dump shows neither has ever had a rule at 566 or a knob at
      602..679.

      The both-take-it candidate was built and rendered before this came out,
      and is on the record as rejected on its own numbers. It gives the rack a
      real bottom rail — three rules, three switch rows, three knobs on one
      line — but it moves this panel's empty band out of the foot (190 px,
      under COMPLEX, which explains it) and into the middle (**262 px**, where
      nothing does), and it costs BMO Opto the head/foot mirror, since
      LINK/COLOR stacked want 60 px and the shared switch row is 28.

      **Do not re-raise "give the freed height to the meter block."**
      `VcompPanel.h` records that re-flowing standard mode into that space was
      considered and rejected: it is the price of controls that stay put when
      COMPLEX toggles, the same reasoning as BMO Opto's old hide-and-shuffle
      COLOR switch.
- [ ] The meter block: three bars with 12 dB ticks. Decide whether GR
      right-to-left reads, and whether the gate handle on IN needs a
      printed threshold (there is no numeric readout of it anywhere).
- [ ] Clicking the IN caption sets the gate to −60 (hit-test the well, not
      the row).
- [ ] Names are first drafts: AMOUNT, LOW THRU, HIGH THRU, SC HPF, COMPLEX,
      and the product name. Accent `#a2a8ff` not signed off.

### BMO Tune RT (360)
- [ ] The middle third of the panel is empty: keyboard and selectors at
      the top, three knobs at the foot, a rule and nothing between. Either
      the knobs come up, or a note / pitch readout goes there (the DSP has
      nothing exposed for one today; it would be a sixth callback and a
      float from `TuneCore`).
- [ ] The keyboard's dot on C is the key indicator; check it reads as such
      in both appearances and at a glance.
- [ ] Lime on the pale plate, 1.29:1: Frosty kept it, so only re-open with
      a render that reads badly.

### The rack
- [ ] Input knobs, switch rows and output knobs on the shared rows for
      every module that takes them; a count of who does and does not, on
      the record.
- [ ] Slot bar: the DEQ expand button, the `<` `>` `x` controls, module
      names in the accent. The rack's own header is pink (EQ's accent);
      decide whether the rack should have its own.

## C. Done means
- [ ] `scripts/build.sh --snapshots` green, snapshots looked at in both
      appearances.
- [ ] Every ratio quoted against a named ground, from `tools/inspect`.
- [ ] `testing-notes/ui-pass-<date>.md` with before/after hashes and
      Frosty's calls recorded at the call sites.

## D. All eight panels, walked and recorded

**Frosty's call, 2026-09-17, from the review of pull request #9: not all eight
were fully tested, so all eight are to be.** The pass was described as covering
every rack panel with its own record; six of them have one. This section is the
count, and it is the gate on calling the pass done.

The eight are the rack's seven modules and the rack's own panel. BMO Tune RT is
**not** one of them — it is not in the rack — but it has a record and its §B
items stay open, so it is listed here for completeness rather than as a gate.

| panel | own record | §B items | still owed |
|---|---|---|---|
| BMO Util | `ui-pass-util-2026-09-17.md` | 7 done, 0 open | — |
| BMO CEQ | `ui-pass-ceq-2026-09-17.md` | 5 done, 2 open | the EQL / LO-CUT / HI-Q legends; low-cut crowding is parked at Frosty's request |
| BMO Saturator | `ui-pass-sat-2026-09-17.md` | 6 done, 3 open | host names `Sat In` / `Auto Gain` against the captions; the 1.42:1 white pointer in light |
| BMO Opto | `ui-pass-opto-2026-09-17.md` | 6 done, 0 open | — |
| BMO Dimension | `ui-pass-dim-2026-09-17.md` | 5 done, 2 open | — |
| BMO DEQ | `ui-pass-deq-2026-09-15.md` | 0 done, 4 open | **every §B item**; the record predates them |
| **LTV Comp** | **none** | 1 done, 3 open | **a record, plus the meter block, the IN-caption hit-test, and the names — AMOUNT, LOW THRU, HIGH THRU, SC HPF, COMPLEX and the product name are all first drafts, and the accent `#a2a8ff` is not signed off** |
| **The rack** | **none** | 0 done, 2 open | **a record, plus the shared-row count and the slot bar** |

- [ ] BMO DEQ's four §B items closed, or re-recorded as decided.
- [ ] **LTV Comp walked**, with `ui-pass-ltvcomp-<date>.md`. Its names and
      accent need Frosty, and the product needs Leteveon's approval before any
      of it is settled.
- [ ] **The rack walked**, with `ui-pass-rack-<date>.md`.
- [ ] The three items in §C above, which are still unticked for the pass as a
      whole.
