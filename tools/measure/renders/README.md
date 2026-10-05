# Render measurement harness

Reads WAV renders and answers the questions an ear pass raises but cannot
settle: how fast a compressor gives the gain back, and where a saturator's
band energy actually sits.

Written in C# and **deliberately not wired into `tools/CMakeLists.txt`**.
It is not part of the build and CI never compiles it. Two reasons:

- The machine this gets run on has no C++ toolchain, so a C++ tool here
  could only ever run in CI -- and CI cannot see the renders, which live in
  the Ableton session in the owner's reference folder, not in the repository.
- Adding it to the build would mean a broken analysis script could fail a
  plugin build. Nothing here should ever be able to do that.

## Running it

    csc -out:Analyze.exe Analyze.cs        # Framework64/v4.0.30319/csc.exe
    ./Analyze.exe <mode> ...

| mode | what it does |
|---|---|
| `env`    | recovers each render's gain envelope against a dry file, finds the phrase gaps, and reports how much reduction is still standing when the next phrase starts |
| `band`   | RMS-normalises every file to a reference and prints a band energy table, **with rows above 9 kHz** |
| `sim`    | a port of `modules/opto/dsp/Detector.h`, so release constants can be fitted against a real render instead of guessed |
| `hold`   | mirrors `reductionAtEndAndAfter` in the DSP tests, so a CI test's outcome can be predicted without building |
| `preset` | ports `TestUtil.h`'s `voice()` and reports the makeup a preset needs |
| `bell`   | fits a peaking filter's f0/Q/gain against a measured band-by-band gap |

## Things it cost a wrong answer to learn

- **Bin every file at its own sample rate.** The reference vocals are 44.1k
  PCM and the Ableton bounces are 48k float. Binning everything at the
  reference's rate stretches each bounce's frequency axis by 8%, which reads
  as a phantom -8 dB shelf in the top band.
- **Walk the RIFF chunk list.** Ableton writes a `JUNK` chunk ahead of `fmt`,
  so a fixed offset reads the wrong bytes.
- **`sim` and `preset` are detector-only.** They do not model DspCore's drive
  or Color stages. `sim` reproduces measured release behaviour closely --
  it hit 65% against a rendered 63% before anything was changed. `preset` is
  good enough to rank options and **not** good enough to set a LEVEL value:
  it lands ~8 dB out at deep settings. Preset levels still come from CI's
  `BMO_PRINT_PRESET_LEVELS`.
- **Recovery over 100%** in `env` is not a bug. The gain envelope is
  recovered from the render, so it can overshoot slightly; `sim` reads
  reduction directly and floors at 0. 93-100% from `sim` and 105-114% from
  `env` are the same behaviour.
