# BMO Util — Ableton testing checklist

Build under test: `v0.2.4` / `cbd0939` on `integration`, CI run
**34834557823** (`BMO-Windows`), installed on **AURORA** 2026-09-14; SHA-256
in `review-0.2.4-2026-09-14.md`. A utility has no sound of its own, so this
is a correctness pass: every control against the arithmetic it claims, and
nothing else. Drafted from a code review.

Order of operations: GAIN → polarity → MONO → WIDTH → PAN. Zero latency.

## 1. Does it work at all
- Loads standalone and in a rack slot; Init is a wire (A/B against
  bypass nulls).
- Six controls, all audibly do what they say across their range.

## 2. Gain
- −24 … +24, no zipper on an automated sweep (5 ms smoothing).
- Pad −6 preset is exactly −6 dB on a meter.

## 3. Pan, which is a balance law, not constant power
- Centre: both channels unity. Pan 50 R: left drops 6 dB, right does
  not rise. Hard right: left silent.
- Next to Live's Utility (constant power) on the same bus: does the
  missing centre dip read as wrong, or as "a utility that leaves the
  middle alone"? This is the one design call on the panel worth a verdict.

## 4. Width, mid/side
- Width 0 = mono. Width 100 = wire. Width 200 = side doubled.
- Mono compatibility: at any width, sum to mono (MONO on, or Live's
  Utility after it) and the level must not change. Width cannot touch the
  mono sum by construction; if it does, that is a real fault.
- Width 150–200 on a hard-panned or very wide source: there is no headroom
  guard, peaks can rise 1.5×. Watch the meter after it.
- Width 200 on a real stereo bus: phasey or hollow? That is the algorithm
  showing, not a fault, but note where it stops being useful.

## 5. Mono
- MONO on: (L+R)/2 on both sides. Then turn WIDTH: nothing should change.
- Flip MONO on a held bass note: click? It is a hard switch in this build
  (fix on `review-0.2.4`).

## 6. Polarity
- Phase L alone on a stereo source: image collapses or hollows as expected.
  Phase L+R: identical to a polarity flip on the track.
- Flip either mid-note on a bass: click? Hard switch in this build, same
  fix.
- Mono track: only GAIN and Phase L do anything (pan, width and mono are
  stereo-only). Confirm Phase R does nothing on a mono track and that
  nothing crashes.

## 7. Presets
- Mono Check, Flip Polarity, Side Only, Narrow, Pad −6: each does only the
  thing it is named for and nothing carries over from the previous preset.

## 8. Look (do not judge)
- The VOLUME knob is a placeholder colour (`#9c71c3`, Dimension's hue,
  under the suite's contrast floor). It is on the UI-pass list; note only
  if it actively misleads next to a Dimension slot.

---
Feed back whatever you notice, however informally. Name the machine.
