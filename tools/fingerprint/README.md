# audio_fingerprint

Loads every built VST3 as a host does, runs the same seven seconds of fixed
audio through each, and prints one line per render ending in a hash of the
output. The last line is a hash of all the lines above it.

```
build/tools/audio_fingerprint build
build/tools/audio_fingerprint "build/products/util/BmoUtil_artefacts/Release/VST3/BMO Util.vst3"
```

## What it is for

Comparing **two builds of one commit on one platform**. CI builds pull requests
and `main` without link-time optimisation and release tags with it (`BMO_LTO`
in the root `CMakeLists.txt`), so the plugins from a `main` run are not
bit-for-bit the plugins a tag ships. If both runs print the same `fingerprint:`
line, they make the same samples from this input, at every setting it tried.

CI prints it in the **Audio fingerprint** step of the macOS and Windows jobs
and uploads it as `BMO-<platform>-fingerprint`. To compare a release build
against the `main` build of the same commit, read the two steps side by side,
or run the workflow by hand with **lto** ticked on the commit in question.

## What it is not

- Not a test that the audio is right. `tests/` does that. No hash is written
  down to be matched, because every DSP change moves them on purpose.
- Not comparable across platforms. macOS and Windows print different hashes
  and that means nothing.
- Not a listening result. It says two builds agree, sample for sample, on this
  input. It says nothing about how either sounds.

## What a line says

```
BMO Opto | 48000/512 | seed1 | in 2 out 2 | latency 0 | peak -7.31 dBFS | 0123456789abcdef
```

Product, sample rate and block size, the starting state, the channels the host
was given, the latency reported, the output's peak, and the hash. `default` is
every parameter at its default; `seedN` puts every parameter at a fixed
pseudo-random position. The rack is rendered empty and with two chains that
between them hold every module.

- `thru` means the output is the input, untouched. Expected for an empty rack;
  anywhere else the line proves nothing about the plugin.
- `UNSTABLE` means two renders of the same build disagreed, so its output
  depends on something other than its input. The tool exits 1. Find out why
  before comparing anything.

## When two builds disagree

`dump=<dir>` writes every render as raw 32-bit floats. Run it on both builds,
then:

```
build/tools/audio_fingerprint diff "a/BMO Opto 48000-512 seed1.f32" "b/BMO Opto 48000-512 seed1.f32"
```

It prints how many samples differ and the largest difference in dBFS. Point
`dump=` outside the repository: raw audio is still audio, and none is
committed here.

## When a module is added

Add its id to one of the two chains in `main.cpp`, or the rack's lines will not
cover it. Standalone products are found by looking through the build folder
and need nothing.
