# BMO Dwell

The rack's delay: one feedback loop with clean, tape and bucket-brigade
characters, an in-loop FX stage, and **zero reported latency at every
setting**.

**Status: skeleton.** The identity, the permanent parameter schema and the
plumbing are here; the loop is not. `process` passes audio through untouched.
The DSP is stage 2 of `docs/delay/HANDOFF-add-bmo-dwell.md` and is specified in
full in `docs/delay/10-dsp-spec.md`.

## What is here

```
params.h                 ids 0-19, permanent; the four choice lists
dsp/
  DspCore.h/.cpp           parameters in real units; the ring goes here
  DwellDsp.h               the ModuleDsp adapter, and the zero-latency rule
presets/FactoryPresets.h Init only, until the module has a sound to preset
Module.h/.cpp            the ModuleDef: accent, 280 compact / 560 expanded (a
                         redesign proposal -- 13 §6a still says 460)
panel/                   the compact panel and the FX column
```

The specification lives in `docs/delay/`, not here. `10` owns DSP meaning and
fixed values, `11` owns ids, ranges and choice lists, `13` owns layout and
captions, `14` owns how the CALIBRATE values get settled.

## Trying it

```
cmake -S . -B build-dsp -DBMO_DSP_ONLY=ON
cmake --build build-dsp --config Release --target dwell_dsp_tests
ctest --test-dir build-dsp -C Release -R dwell_dsp --output-on-failure
```

Name an explicit target. An untargeted `cmake --build` builds the plugins and
installs them over whatever is in this machine's VST3 folder.

## The three things worth knowing before you change anything

- **The schema is permanent.** Ids, their order and the index order of the four
  choice lists are frozen from the first release. `fxType` is the one list
  still free, and only until ship. See `params.h` and `AGENTS.md`.
- **Latency is 0 and the delay time is not latency.** `latencyForParams`
  returns 0 at every setting and always will; see `dsp/DwellDsp.h` for why
  reporting TIME would be wrong rather than merely conservative.
- **SYNC and NOTE ship disabled.** The slots and NOTE's order are permanent
  now; the feature waits for the tempo plumbing in `docs/delay/12`, which is
  its own pull request.
