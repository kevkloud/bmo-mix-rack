# BMO Dwell

The rack's delay: **two feedback loops running at once** — the main delay, and a
parallel **lane** you send single words into — with clean, tape and
bucket-brigade characters, an in-loop FX stage on each loop, and **zero reported
latency at every setting**.

The lane is what a throw, a freeze and a build all are here: `send` gates its
input a word at a time, `hold` gates its life (switching it off **clears** it),
`chop` gates its output for rhythmic stutter, and one bipolar `lane_gain` sets
its tail — decaying below centre, holding at exact unity at centre, growing
above it. It is fed from the **dry input**, not from the main delay's output,
and the main delay has **no input gate at all**, so nothing the lane does can
disturb it.

**The lane shares the main delay's voicing**: one CHARACTER, one STEREO, one
pair of cuts, one modulation, one DRIVE, governing both loops. What the lane
keeps of its own is its TIME, its LEVEL, its tail, its three gates and its FX,
which `fx_link` ties to the main's unless you turn it off. **DUCK is
main-engine only.**

**Status: skeleton.** The identity, the permanent parameter schema and the
plumbing are here; the loop is not. `process` passes audio through untouched.
The DSP is stage 2 of `docs/delay/HANDOFF-add-bmo-dwell.md` and is specified in
full in `docs/delay/10-dsp-spec.md`.

## What is here

```
params.h                 ids 0-26, permanent; the four choice lists
dsp/
  DspCore.h/.cpp           parameters in real units; one engine, held twice
  DwellDsp.h               the ModuleDsp adapter, and the zero-latency rule
presets/FactoryPresets.h Init only, until the module has a sound to preset
Module.h/.cpp            the ModuleDef: accent #f094e6, one width, 380
panel/                   BMO Linger's paged handheld: a screen with TONE / LANE /
                         FX tabs, the page's controls, and a foot that never changes
dsp/GainLaws.h           the two loop-gain laws, shared by the engine and the screen
```

The specification lives in `docs/delay/`, not here. **`15-lane-redesign.md` is
the entry point** — read it before `10`, `11` and `13`, which it supersedes in
places. `10` owns DSP meaning and fixed values, `11` owns ids, ranges and choice
lists, `13` owns layout and captions, `14` owns how the CALIBRATE values get
settled. For the schema itself the **code is the reference**:
`./build-ui/tools/Release/measure_dwell.exe schema` prints the live table.

## Trying it

```
cmake -S . -B build-dsp -DBMO_DSP_ONLY=ON
cmake --build build-dsp --config Release --target dwell_dsp_tests
ctest --test-dir build-dsp -C Release -R dwell_dsp --output-on-failure
```

Name an explicit target. An untargeted `cmake --build` builds the plugins and
installs them over whatever is in this machine's VST3 folder.

## The three things worth knowing before you change anything

- **The schema is permanent, and it is twenty-seven rows, ids 0–26.** They all
  fit inside a rack slot's 32 host automation lanes with five spare, so **every
  parameter is automatable everywhere** and nothing uses `SlotOverflow`. Ids,
  their order and
  the index order of the four choice lists are frozen from the first release.
  `fx_type` is the one list still free, and only until ship — three types now,
  Sweep having gone with VOICE. There is **no VOICE, no THROW MODE, no FREEZE
  and no LINK**, and the lane has no voicing parameters of its own: the first
  three were cut on 2026-09-21, and `link` with the lane's six voicing rows on
  2026-09-23. See `params.h` and `AGENTS.md`.
- **Latency is 0 and the delay time is not latency.** `latencyForParams`
  returns 0 at every setting and always will; see `dsp/DwellDsp.h` for why
  reporting TIME would be wrong rather than merely conservative.
- **SYNC, NOTE and LANE NOTE ship disabled.** The slots and NOTE's order are
  permanent now; the feature waits for the tempo plumbing in `docs/delay/12`,
  which is its own pull request. **One SYNC governs both engines** and each has
  its own division — and the two defaults are chosen so that enabling SYNC at
  120 BPM is inaudible (`docs/delay/10` §11.7).
