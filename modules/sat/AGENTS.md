# modules/sat -- BMO Saturator

Read root `AGENTS.md` and `modules/AGENTS.md` first. This file holds what the
Saturator's code does not say on its own.

```
params.h                  nine parameters; "tone" is last because it arrived last
dsp/DspCore.h/.cpp        the whole signal path, JUCE-free; the "Character" struct
                          holds the fitted constants and says where each came from
dsp/Shaper.h              the fitted asymmetric curve, anti-aliased by ADAA
dsp/Filters.h             the voicing bell, the high-pass, the DC blocker, splits
dsp/DriveTables.h         DRIVE's range, and the old makeup table (tools only)
dsp/SatDsp.h              the ModuleDsp wrapper: values by Index -> DspCore::Params
panel/SatPanel.*          the panel
presets/FactoryPresets.h  eleven presets, Init first
```

Tests: `tests/dsp/SatDspTests.cpp` (the character: curve, asymmetry, band
placement, crest factor, Auto Gain's level match), `tests/dsp/SatSwitchTests.cpp`
(what happens while a control moves), `tests/plugin/SatTests.cpp` (schema,
presets, preset levels).

## Rules

- **TONE's default is 55**, not the 100 the module shipped with: the owner's
  decision of 2026-10-03, before the 0.2.6 schema freeze, so the default state
  is a safe starting point. At 100 the voicing is a +13.5 dB bell near 7 kHz.
  **Every factory preset but Init names its own TONE**, and so does every
  rack chain that holds a Saturator (`products/rack/Product.h`); SatTests and
  RackTests fail a preset that leaves it to the default, because that preset
  would move the next time the default does. `DspCore::Params` still starts
  TONE at 100; that is the DSP's own initial value, seen only by the DSP tests
  and the measurement tools, never by a host.
- **Every switch fades** (`core/dsp/SwitchFade.h`, `DspCore::kSwitchFadeMs`,
  10 ms). Phase ramps the polarity through zero; Sat In crosses between the
  stage and a wire inside the oversampled region, and a stage brought back from
  fully out starts from rest; a change of oversampling dips the whole output
  to zero and back, because the latency changes and no blend exists. Idle,
  none of these is read at all, so a module with nothing switching renders
  bit-identically to one without them. Keep it that way: test `isMoving()` /
  `isIdle()` and take the plain path, never multiply by an idle gain.
- **The oversampling change warms the new path inside one callback** (141
  samples of replay at the new factor, `switchOversampling`). At 192 kHz with
  32-sample blocks, Off -> 8x, that callback measured about 240 us, 1.4 blocks.
  The suite is moving to a warm-up spread across the fade down; follow it.
- **Control periods run on the stream.** The smoothers and Auto Gain's
  detector advance once per `kSubBlock` (32) samples of audio, wherever the
  host's blocks fall: a period a block ends inside carries on into the next
  call. That is what makes the output bit-identical at every host block size
  (SatSwitchTests section 3). Do not tick anything per `process()` call.
- **Auto Gain is a 1.5 s level match, never a compressor.** SatDspTests holds
  that switching it on moves the crest factor by under 0.25 dB. Its reading
  survives `prepare()` and `reset()`; an instance that has heard nothing starts
  at unity and glides. Priming a fresh instance from its first 20 ms was tried
  and measured: the early estimate runs hot on a programme's onset (the
  stage's filters, starting from rest, put out less than they will) and broke
  the crest-factor test, so it was left to the owner.
- **Polarity and Output apply to the blend**, after Mix, so the dry path sees
  them and an asymmetric curve is never fed a flipped signal.

## Held for the owner (2026-10-03 review)

Auto Gain's reaction to a loud burst (about -1.7 to -2.0 dB for 2.9 s after);
TONE's response depending on the oversampling choice (up to 2.85 dB at
16 kHz); the panel not dimming DRIVE, TONE and AUTO while Sat In is off;
NaN handling, which waits on a shared guard.
