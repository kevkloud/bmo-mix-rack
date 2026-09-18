# BMO CEQ (BMO EQ) — Ableton testing checklist

Build under test: `v0.2.4` / `cbd0939` on `integration`, CI run
**34834557823** (`BMO-Windows`), installed on **AURORA** 2026-09-14; the
VST3's SHA-256 is in `review-0.2.4-2026-09-14.md`. This is the "what to
listen for" in a DAW, in priority order, not a build or install guide.
Drafted from a code and measurement review, not from an ear: the numbers
are measured, the verdicts are Frosty's.

The module is still called BMO EQ in this build. **It is BMO CEQ from the UI
pass on** (2026-09-17); nothing about the sound changes with the name, and a
session saved under either old name still opens.

**Two of its sixteen parameters are not on the panel**: High Cut and Mix.
Reach them through Live's configure mode or the automation lanes (rack lanes
p09 and p14). Telephone depends on High Cut.

Auto Gain and Oversampling were on that list until the UI pass, which gave
each a control -- `AUTO` on the switch row, and an `OVERSAMPLING` row of
2x / 4x / 8x under LO-CUT with Off the position none of them lights. **Mix is
deliberately not on the panel** (Frosty, 2026-09-17) and will not be; the
parameter stays only because removing it would shift the two after it in every
saved session. So §6 below is a host-only test of a control the panel does not
offer, and §2's oversampling checks can be driven from the panel now.

## 1. Does it work at all
- Loads standalone and in a rack slot, no crashes, glitches or silence.
- Init at INPUT 0 on a −18 dBFS RMS track should be very nearly a wire:
  0.08 % THD measured. If Init sounds coloured at unity, say so.
- Every band audibly does something at every selector position; ±16 / ±18
  reach where the legend says.

## 2. Latency and oversampling (the one module whose default is not zero)
- Default 2x reports 40 samples (0.83 ms at 48 k); 4x 60; 8x 70. A
  duplicate dry track against Init should null after PDC.
- Change Oversampling while playing: expect a click and a PDC re-sync.
  Note how bad it is. Do not automate this control.
- Off vs 2x vs 4x with HF +6 at 16 kHz: three slightly different top ends,
  not just "cleaner" (a 62 kHz roll-off pair engages only at 4x and 8x).
  Pick the one that should be the default and say why.

## 3. Switching and smoothing
- Turn a frequency selector while a note sustains: a glide, not a click.
- EQL, Phase, Low Cut Off→45 Hz, High Cut Off→6 kHz mid-note: any click?
  These are hard switches today.
- HI-Q on/off mid-note.

## 4. The network, which is what makes it a console EQ
- MID +12 at 1.6 kHz and HF +8 at 12 kHz together: the overlap should
  boost less than the two alone (band interaction). Does it read as
  civil, or as "the knob stopped working"?
- MID at +3, +9, +18: the bell should narrow as it rises (proportional Q,
  5.3 → 1.1 octaves measured). Is +18 at 7.2 kHz with HI-Q a presence
  peak or a whistle? HI-Q is an unpublished ×2 and is a guess.
- LOW shelf 60 Hz +8 on a bass: muddy or tight?
- LO-CUT 45/70/160/360: no bump at the corner. Does 18 dB/oct feel right
  on a vocal at 70?

## 5. The iron
- INPUT +10 / OUTPUT −10 on a drum bus (Drum Bus Iron): bass thickens
  (2nd and 3rd harmonic at 40–100 Hz), top stays clean. Is that the 1084
  story?
- INPUT +10 on a full-scale kick: about 4 % at 40 Hz. Fuzz or weight?
- EQL off still runs the iron and keeps the latency: A/B the colour with
  Mix (host-only) or the DAW, not with EQL.

## 6. Mix, Phase, Auto Gain (host-only; reach them from the parameter view)
- Mix 50 % with Phase ON: this build cancels, because Phase is applied to
  the wet path only (known bug, fix on `review-0.2.4`). Confirm, then leave
  Phase off for the rest of the pass.
- Auto Gain: toggling it alone does nothing until a band moves (known bug,
  same branch). Judge it after a band move: does the level stay put across
  a +16 LF boost? On Telephone it will boost hard. Is that useful?

## 7. Presets
- All eleven are level-matched to ±0.5 dB on pink noise, not by ear. Any
  preset clearly louder or quieter than Init is a real fault.
- Vocal Air / Vocal Presence / Presence Lift on the same vocal: three
  useful settings, or one at three depths?

## 8. Naming (lowest priority)
- "EQL" for EQ In, "LO-CUT", "HI-Q": note anything that reads wrong once
  heard in use. "BMO CEQ" itself is decided; the legends are not.

---
Feed back whatever you notice, however informally. Name the machine.
