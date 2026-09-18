# True latency and correction lag: BMO Tune RT, Antares, Waves -- 2026-09-11

Measured on **AURORA**, 48 kHz, blocks of 128, uncompensated. Every tuner was
put through the same file and scored by the same code, so the numbers compare.

## How

```
build/tools/Release/bmo-tune-ref stimulus stimulus-48k.wav
build-plugin/tools/Release/bmo-tune-hostrender "C:\Program Files\Common Files\VST3\Auto-Tune Artist.vst3" ^
    stimulus-48k.wav antares.wav --setn "Input Type=0.5" --set "Retune Speed=0" ^
    --set "Humanize=0" --set "Natural Vibrato=0" --set "Flex-Tune=0"
build-plugin/tools/Release/bmo-tune-hostrender "C:\Program Files\Common Files\VST3\WaveShell1-VST3 16.0_x64.vst3" ^
    --type "Waves Tune Real-Time Mono" stimulus-48k.wav waves.wav --set "Speed=0" --set "Note Transition=0"
build/tools/Release/bmo-tune-ref score antares.wav
build/tools/Release/bmo-tune-ref score waves.wav
build/tools/Release/bmo-tune-ref bmo
```

- **The stimulus** (`tools/tune/common/Stimulus.h`): 19.5 s of a synthetic low male
  voice -- in-tune held notes on A2, D3, E3 and A3; the same four notes with
  a 5.5-6.5 Hz vibrato of 40-45 cents to be flattened; and A3 and D3 held 30
  and 35 cents off, marked with sharp level dips. 0.4 s of silence between.
- **True latency**: how late the output is. In-tune notes by waveform
  cross-correlation (the output is the input, delayed); the held-off notes by
  cross-correlating their level envelopes, which a pitch shift leaves alone --
  the delay while correcting. The figure is the worst of all six.
- **Correction lag**: on each vibrato, a hard-tune correction that is L late
  leaves `out - target = L x slope`; L is fitted over the whole vibrato.
  Positive is late; negative means the tuner looks ahead of the audio.
- **The ruler reads true** (`HardTuneTests`): a plain 2.5 ms delay reads
  2.5 ms within 0.05; ideal correctors 0, 2 and 5 ms late read their lag
  within 0.04 ms, with or without 3 ms of audio delay on top.
- Other tuners are loaded by `bmo-tune-hostrender`, a plain JUCE VST3 host:
  both were licensed on AURORA and loaded without a prompt.

## Results

| | Reports to host | True latency, worst | In tune | While correcting | Correction lag, mean / worst | RMS off the note while flattening |
|---|---|---|---|---|---|---|
| **BMO Tune RT** (this commit) | 0 ms | **3.82 ms** | 0.66-1.01 | 2.94-3.82 | 6.22 / 8.95 ms | 6.61 c |
| **Antares Auto-Tune Artist** (Low Male) | 2.33 ms | 6.49 ms | 3.00-6.49 | 5.26-5.95 | **-0.24 / 1.66 ms** | **1.30 c** |
| **Waves Tune Real-Time** 16.0.23.24 (Mono) | 0 ms | 10.62 ms | 5.93-10.50 | 7.33-10.62 | 1.32 / 2.13 ms | 1.73 c |

Per segment:

| segment | BMO | Antares | Waves |
|---|---|---|---|
| in tune A2, delay | 0.85 | 3.00 | 10.50 |
| in tune D3, delay | 0.66 | 4.02 | 7.69 |
| in tune E3, delay | 1.01 | 5.09 | 7.45 |
| in tune A3, delay | 0.89 | 6.49 | 5.93 |
| held +30c A3, delay | 2.94 | 5.26 | 7.33 |
| held -35c D3, delay | 3.82 | 5.95 | 10.62 |
| vibrato A2, lag | 8.95 | 1.66 | 2.13 |
| vibrato D3, lag | 6.41 | -0.20 | 1.15 |
| vibrato E3, lag | 5.52 | -0.56 | 1.08 |
| vibrato A3, lag | 3.98 | -1.84 | 0.92 |

What it says:

- **BMO is the least late of the three**, and the only one close to what it
  reports. Waves also reports 0 and runs up to 10.6 ms late, more on low
  notes (about one cycle of the note plus 1-1.4 ms). Antares reports 2.33 ms and runs 3-6.5.
  Ableton compensates only what is reported, which is why the shoot-out
  exports showed Antares 2-3 ms off and Waves anywhere from 0.75 to 10 ms.
- **BMO's correction is about one cycle late** (4.0 ms at A3, 9.0 at A2) and
  that lag is nearly all its error on a moving voice. Antares spends its
  latency on looking ahead and lands on time or early; Waves is 1-2 ms late.
- **Under the latency rule** (root `AGENTS.md`) the ceiling is Waves' 10.62 ms
  and BMO has 6.8 ms of headroom -- enough to delay the audio by a cycle at
  A2 and put the correction on time, if prediction alone does not get there.

## What BMO is held to

`tools/tune/common/References.h` records both tuners' figures with their settings.
`tests/dsp/HardTuneTests.cpp`:

- every run: true latency <= Waves' (the latency rule); correction lag and
  vibrato residue no worse than today's (6.22 ms, 6.61 c);
- `--target`, disabled in ctest until it passes: vibrato residue <= Antares'
  1.30 c, worst correction lag <= Antares' 1.66 ms. Fails today.

## What is still needed from Frosty (the Antares reference)

The numbers above are from AURORA's own plugins, rendered by our host. To
make them the reference with confidence:

1. **The same render in Ableton, as a cross-check.** Put
   `stimulus-48k.wav` (from `bmo-tune-ref stimulus`) on an audio track at
   48 kHz, turn **Options > Delay Compensation off**, insert Auto-Tune Artist
   with the settings above (Low Male, Chromatic, Retune Speed 0, Humanize 0,
   Natural Vibrato 0, Flex-Tune 0), and export that track from the start of
   the clip as 32-bit float, dither off. The same with Waves Tune Real-Time
   (Speed and Note Transition at minimum, Chromatic). Say the buffer size
   and which machine. `bmo-tune-ref score` on the two exports should agree
   with the table; if it does not, the host differs and Ableton's figure is
   the one to hold to.
2. **Which Antares is the target.** Auto-Tune Pro is installed on AURORA
   too. If Pro is the one to beat, its render goes in `References.h` beside
   Artist's -- one command, once the settings are known.
3. **Any Auto-Tune setting used in real sessions that is not a parameter**
   the host can see -- a low-latency switch, or Tracking moved off 50.
4. **Ears on the blind set** (`shootout-2026-09-11.md`, "The blind set").
