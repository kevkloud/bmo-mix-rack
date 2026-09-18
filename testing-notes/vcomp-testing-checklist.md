# LTV Comp — listening checklist

Build under test: `v0.2.4` / `cbd0939` on `integration`, CI run
34834557823 (`BMO-Windows`), installed on **AURORA** 2026-09-14; the SHA-256 is
in `review-0.2.4-2026-09-14.md`. One round of listening covers DEQ, Vcomp and
Tune together.

**Heard once, 2026-09-14, on AURORA: "flying colors"**, with five changes asked
for and made the same day (`vcomp-handoff.md` has the table). Every number in
it is still a round number at a shape — the curve, the ARC scales, the gate's
ratio, all eight presets — so this is not yet a regression pass looking for
faults; it is the pass that decides what the module sounds like. Disagreeing
with a number here is the point, not a bug report.

Offline renders and the numbers behind them are in
`packages/vcomp-listening/` (local, gitignored). Regenerate with
`measure_vcomp presets|gate|bands|curve --outdir <dir>`.

## 1. Does it work at all

- Loads standalone and in the rack, no crashes, clicks or silence.
- AMOUNT at 0 is a **wire** — A/B against bypass should be indetectable
  at any input level. If it is not, that is a real DSP fault, not taste.
- AMOUNT audibly does something at every part of its travel, including
  the first quarter. The first build was dead below about 25% and that is
  exactly the fault to listen for again.

## 2. AMOUNT — the one that matters

The claim is that AMOUNT buys **density, not level**: turning it up
should make the vocal sit further forward without getting louder.

- Sweep it slowly on a dry lead vocal. Does the loudness stay put while
  the *character* changes?
- Does the top of the knob sound like a different, more aggressive
  compressor than the bottom — or just like the same one working harder?
  Three things move together (threshold, knee, ratio 1:1→8:1) precisely
  so that it should be the former.
- Where on the dial does it stop being useful and start being an effect?
  That number decides whether the sweep ranges are right.

## 3. The gate

It is there because the makeup lifts room tone as much as it lifts the
voice. Drag the handle on the IN meter.

- Set it against a real noisy take — bleed, breaths, room. Does it clean
  up the silences without you hearing it work?
- **Does it ever bite the front of a word?** This is the expensive
  failure and the one to hunt for. Offline it moves the first 20 ms by at
  most 0.06 dB at every threshold, but a synthetic phrase is not a singer.
- **Heard 2026-09-14 and kept**: 6:1 into a 60 dB floor, opening over 3 ms.
  It was 3:1/50 dB opening in 0.5 ms, which was too polite; going deeper then
  made the fast open *click*, because the gate suddenly had 58 dB to open
  from rather than 16. If it still ticks, the structural fix is a slew limit
  rather than a longer ramp — see `Gate.h`.
- Does putting the threshold *on the meter* work as an interaction, or
  would you rather have a knob? This is the first control in the suite
  that is neither knob nor switch.

## 4. ARC, and the timing controls

- **ARC was made bolder on 2026-09-14 and kept.** Slow branch 1 s -> 2 s,
  charge 400 ms -> 240. At the shipped default it had been sitting about
  20 dB below the signal, which is why it could not be heard at all; it is
  now around 12 dB at AMOUNT 70.
- With COMPLEX off, ARC is always on. Does the release feel like it is
  paying attention — quick after a consonant, slower after a long held
  line? That difference is the whole of it.
- Turn COMPLEX on and switch ARC off (preset **Manual**). Is the
  difference audible, and is ARC the better default?
- Standard mode runs a fixed 5 ms attack that a standard-mode user cannot
  change. Is 5 ms right for a vocal, or does it want to be faster
  (denser, more RVox) or slower (more transient through)?
- Turning COMPLEX on with untouched knobs is supposed to be **silent**.
  Verify by ear: flip it back and forth on a held note.

## 5. LOW THRU / HIGH THRU

These split bands out of the compressor's reach — not a sidechain filter. The
makeup applies to **everything**, compressed or not (Frosty's spec,
2026-09-14), so an uncompressed band rises with it: +6 dB of low end at
AMOUNT 30, +25 dB at 90. Usable low to middling and self-defeating above
that, which is a property of the request rather than of the build.

**That is the build you are listening to, and it is already fixed elsewhere.**
`vcomp-thru-cap` holds the thru band at the gain the curve would have given it,
which takes the tilt from 4.6/8.8/12.9/17.7 dB across the knob down to
2.4/1.7/1.1/0.6 — and none of that is in `cbd0939`. So the runaway itself is
known and does not need reporting twice; everything below is still worth
hearing, because what a fix has to not break is the feature working at all.
See `testing-notes/vcomp-thru-cap-2026-09-14.md`.

- **LOW THRU** on a chesty male vocal: does the weight stay while the
  midrange levels? Preset **Keep The Chest**.
- **HIGH THRU** on a bright/sibilant source: does the air stay open over
  a held-down body? Preset **Keep The Air**.
- Sweep each one through its range with AMOUNT high. Anywhere it sounds
  hollow, phasey or like an EQ rather than like less compression is worth
  flagging — the crossover measures flat to 0.00 dB, so anything heard
  there is the *band split concept* not suiting the source, which is a
  real finding.
- Is SC HPF still earning its place next to LOW THRU, or do the two feel
  redundant in use? They do different things on paper.

## 6. The meters

- Three bars, IN / GR / OUT. Is GR growing **right to left** readable, or
  does it fight the two bars above and below it that grow left to right?
- Are the ticks (every 12 dB) enough to set the gate against?
- Does GR in azure read as "working" rather than "in trouble"? It is
  deliberately not the amber/red of the level bars.
- Is the meter block big enough? It replaced Opto's needle VU and is
  considerably shorter.

## 7. Presets

All eight are AMOUNT positions with names, not ear-tuned settings. None
of them set OUTPUT or GATE.

- Do **Lift / Forward / In Front** actually read as three useful
  settings, or as one setting at three depths?
- Anything obviously louder or quieter than the others? They are
  level-matched by construction rather than by hand, so a preset that
  sounds off means the *auto makeup* is off, not the preset.
- **Push OUTPUT into the limiter.** It exists now (instantaneous, zero
  latency, last in the chain, ceiling −0.1 dBFS). What colour it has is
  odd-order edge, never warmth. Note whether OUT pinned at −0.1 with a
  quiet GR bar reads as "working" or "broken": the limiter's own reduction
  is not metered.
- **Keep The Air** sat at AMOUNT 70 in the 0.2.4 build, which is a +19 dB
  boost of everything above 6 kHz into the limiter; it is 35 on
  `review-0.2.4`, the same figure and the same reason as Keep The Chest.
  If the installed build is 0.2.4, expect esses to spit.

## 8. Against the references

The point of the module. `measure_vcomp gen voice --out x.wav` writes the
harness's own source so the *same file* can go through both.

- **RVox**: is BMO's one knob as immediately useful? Is it as dense at
  the top?
- **RComp**: does ARC hold up against the real thing?
- **DC1A**: is BMO as easy to be right with?

## 9. Naming (lowest priority)

"AMOUNT", "LOW THRU", "HIGH THRU", "SC HPF", "COMPLEX" and the product
name itself are all first drafts. The accent (`#a2a8ff` periwinkle) has
not been signed off either — see `products/AGENTS.md`.

---

Feed back whatever you notice, however informally. The numbers are cheap
to change right now and expensive after the first release.
