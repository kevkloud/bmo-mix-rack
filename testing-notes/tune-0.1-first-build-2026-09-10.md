# BMO Tune RT 0.1 -- first plugin build, 2026-09-10

> **Superseded 2026-09-11.** Frosty heard this build in Ableton: it works,
> with some hiccups, and CLASSIC sounded better. HYBRID, Studio, Glide and
> the formant controls were removed; items 3, 4 and 6 below no longer apply.
> See `nrt-tune-handoff-2026-09-11.md` for what was set aside and where.
> This note is kept as the record of what 0.1 was.

Built and checked on **AURORA**, Release, MSVC 2022, JUCE 8.0.15 (the rack's
pin, 91ad83ae, cloned from the local `bmo-mix-rack-333` copy). Nothing has
been heard in a DAW yet; this note is what that session is for.

## What was built

- `build-plugin/products/tune/BmoTuneRT_artefacts/Release/VST3/BMO Tune RT.vst3`
- `.../Release/Standalone/BMO Tune RT.exe` -- the same plugin with its own
  audio settings, for a quick listen without Ableton.
- Plugin code `Btun`, manufacturer `LT3a`, bundle id `com.lt3audio.bmotunert`.
  The schema is frozen from this build: see `tests/dsp/SchemaTests.cpp`.

Not copied into the system VST3 folder. To try it in Ableton, copy the whole
`BMO Tune RT.vst3` folder into `C:\Program Files\Common Files\VST3` and
rescan.

## What was checked, on AURORA

| | |
|---|---|
| DSP suites (9) | pass -- unchanged figures, see `modules/tune/AGENTS.md` |
| Panel test | the hide rule against `isHybridOnly()` both ways; every switch label and every box value fits; nothing overlaps |
| Host check, on the built `.vst3` | loads as "BMO Tune RT" by "LT3 Audio", no MIDI; all 24 parameters by name (25 with the bypass JUCE adds); Key shows `Bb` as `Bb`; a voice 30 c sharp comes out at -0.0005 c from A3; Live reports 0, Studio 441 samples (Auto, 48 kHz); a saved state restores |
| Snapshots | `snapshots/` (gitignored): both modes, both appearances |

## What to try in Ableton

1. **Does it load and hold a session?** Insert on a vocal, change Key, Scale
   and Retune, save, reopen.
2. **Hard tune.** Retune 0, Vibrato 0, Chromatic, then a key and scale. The
   snap is the point of the plugin.
3. **Classic against Hybrid** on the same phrase. Classic is brighter and its
   formants move; Hybrid keeps them.
4. **Formant Keep/Follow (Hybrid).** The keep-or-cut decision is open until
   this is heard. On corrections of a semitone or more the difference should be
   obvious; on small ones it is slight.
5. **Vibrato 0 on a held note with vibrato.** At 0 the note decision follows
   the raw pitch, so a vibrato straddling a note boundary warbles between the
   two notes -- the classic hard-tune sound, kept on purpose. If it is too
   much, the other behaviour is written and tested; which is the default is a
   listening call.
6. **Live against Studio.** Live reports 0 ms to Ableton and runs 0.4 ms
   behind at rest, up to about a period while correcting (1.5 ms at A4).
   Studio reports its worst case (9.19 ms for Auto) and Ableton compensates.
7. **The panel.** Both appearances (the preset strip's menu), the menus near
   the bottom of the screen (they should open upward), Ref A's Custom… entry.

## Known, and not bugs

- **Stereo tracks.** The core is mono: the left channel is corrected and
  copied to the right. On a mono vocal on a stereo track that is what you
  want; on a true stereo source the right channel is discarded.
- **No factory presets.** Which settings deserve names is a listening
  decision. The preset strip saves and loads your own (`.bmotune`).
- **Allocation table.** `Btun` is not yet in the rack's
  `products/AGENTS.md` table -- that is Kevin's repository.
- **pluginval** was not run: it is not on this machine and is a download.
  `bmo-tune-hostcheck` covers loading, identity, parameters, audio, latency
  and state; pluginval would add threading and fuzzing.
