# BMO Opto — Ableton testing checklist

Build under test: `v0.1.0` / commit `837ed31`, from a GitHub Actions run
on `add-bmo-opto` (VST3 bundles under Actions → the run's Artifacts
section). This is the "what to listen for" in a DAW, in priority order —
not a build or install guide.

## 1. Does it work at all
- Loads in a track (via BMO Mix Rack → add an Opto module to a slot),
  no crashes, no glitches/clicks/silence.
- CRUSH and LEVEL both audibly do something across their full range.

## 2. Mode — the core thing this session built
- **Tele** vs **Stressed** should sound like two genuinely different
  units, not one curve with different numbers.
- Tele: does it feel like the Opto unit A-style thing — smoother, does the
  release genuinely seem to get *slower* the longer/harder you drive it
  (not just a fixed release time)?
- Stressed: does it feel more like a VCA/Comp unit B opto thing — grabbier,
  simpler release, no "memory" of how long it's been driven?

## 3. Color drive stage
- Tele: always-on (no off switch by design) — does the warmth read as
  subtle/asymmetric, or too much/too little?
- Stressed: toggle on/off — does it read as grittier/odd-harmonic, tape-like?

## 4. Link (stereo)
- On a stereo source, does turning Link on vs off make an audible,
  sensible difference (shared gain reduction vs. channels drifting
  independently)?

## 5. Presets
- Factory preset LEVEL values are back-solved to pass a loudness-matching
  test, not ear-tuned — flag any preset that sounds clearly louder/quieter
  than Init at the same settings.
- Init / manual knob turns are unaffected by any of this — if something
  sounds wrong there, it's a real DSP issue, not a preset issue.

## 6. Naming (lowest priority — just gut-check while you listen)
- "Tele" / "Stressed" / "Color" are placeholders. Note anything that
  feels obviously mis-named once you've heard it in use.

---
Feed back whatever you notice, even informally — doesn't need to map
cleanly to these sections.
