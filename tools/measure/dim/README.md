# measure_dim

Offline measurement for BMO Dimension. Links `bmo_dim_dsp` directly — no host,
no GUI, no JUCE — the same shape as `measure_eq`, `measure_sat` and
`measure_opto`, and built by the same `bmo_add_dsp_tool` line in
`tools/CMakeLists.txt`.

    bash scripts/build.sh            # or: cmake --build build --target measure_dim
    ./build/tools/Debug/measure_dim.exe <mode> ...

| mode | what it does |
|---|---|
| `source` | mid/side content of a WAV — **run this before testing with a file** |
| `pass` | the meter pass offline: nulls, correlation, the throb table, the re-engage burst, the DETUNE switch steps, headroom, WIDTH gating |
| `corr` | correlation of a DAW bounce, level-gated |
| `comb` | per-frame band deviation between two renders — finds a *moving* comb |

## Why each mode exists

Each one is the answer to a specific way of being wrong, and all four
happened during the 2026-09-09 listening pass.

### `source` — a mono file cannot test ASYMMETRY

Section 2 of the checklist was nearly run on a file that could not exercise it.
`songb NOT SATURATED.wav` looks like a stereo vocal and is a mono one in a
stereo container: side peak `0.0000305`, which is exactly one 16-bit LSB, with
55.5 % of samples bit-identical between channels.

ASYMMETRY is a shear — `mid += a * side` — so with no side content there is
nothing to shear. It measured −102 dBFS, which reads as "the control did
something" and is the dither. `pass` now calls that case out by name rather
than reporting a pass.

    measure_dim source "songb NOT SATURATED.wav"

The file name is written by code; the file keeps its own name on disk, and
the key outside the repository maps the two.

### `pass` — the meter pass without a DAW

Runs `testing-notes/dim-meter-pass.md` §02 and §04 plus the measurable half of
§3 and §4 through the real DSP at a real block size.

Its correlation figures predicted the host bounce closely — **18.4 % of windows
anti-phase offline against 16.1 % measured in Live** — so it is a usable
stand-in when a DAW pass is not available.

It is **not** a stand-in for the ear. That same 16.1 % sits in the meter pass's
"do not want to see" column, and the module was cleared by ear anyway. The
metric described the signal correctly and mispredicted what it would cost.

The ear can miss things in the other direction too. The DETUNE switch checks
were added after review, not after a listen. Gated, the stage dropped out in
one sample — a **0.49 step on a 0.5 tone, 32× the tone's own largest move** —
and switching back in stepped **0.18 (11.7×)** about 15 ms later, off the edge
of a cleared buffer. Nobody heard either on the pass. All three switch lines
(off, on from fully off, on mid-fade) now read at or under `1.00x -> no
click`. A number above 1.5× is a click whether or not anyone has heard it.

### `corr` — gate on level, or the number lies

Ungated, the dry mono Song B file reads `min r = −0.2583`, which looks like real
side content and is the LSB noise in the gaps between phrases. Gated at
−80 dBFS the same file reads `0.9999` with a swing of `0.0017`, which is true.

    measure_dim corr "Bounce DIMENSION.wav" --gate -80

### `comb` — a moving comb is invisible to an average

The one that earned its keep. §05 predicts Host stock chorus A will comb in mono
and Dimension will not. A time-averaged spectrum of the two mono sums showed
**no notches at all**, which §07 says would invalidate the whole comparison.

Wrong instrument, not a wrong premise. A modulated chorus sweeps its notches,
so over twenty seconds they average away to nothing. Per 85 ms frame:

| mono sum | mean | std dev | worst frame |
|---|---|---|---|
| Host stock chorus A | +0.77 dB | **2.53 dB** | **−19.78 dB** |
| Vocal doubler A | −0.03 dB | 0.78 dB | −4.16 dB |
| **Dimension** | 0.00 dB | 0.00 dB | 0.00 dB |

**A long-term average will not find a moving comb. Measure per frame.**

    measure_dim comb "Bounce DIMENSION mono sum.wav" "Bounce ENSEMBLE mono sum.wav"

## Things it cost a wrong answer to learn

- **Sample rates must match.** Live resamples on import, so a project at 48 kHz
  and a 44.1 kHz source file cannot null and any cross-correlation between them
  is noise. `comb` refuses rather than reporting nonsense.
- **Gate every windowed statistic.** Silence between phrases is where dither
  lives, and an ungated correlation reports the dither.
- **A size check is not a build check.** A Debug build of a plugin is the same
  size as the Release one to the byte. See `testing-notes/dim-bench-state-2026-09-09.md`
  §4 — `scripts/build.sh` installs Debug plugins over the CI artifact, and only
  a hash catches it.
