# BMO Dimension — Ableton testing checklist

Build under test: `frosty-add-bmo-dimension`. VST3 bundles come from the GitHub
Actions run's **BMO-Windows** artifact — Dimension is in the package now, which
it was not in `ee6721d` (`tools/packager/package.sh` drove a hardcoded product
list and had already dropped BMO Opto the same way once; it reads the build
tree now).

**Take the artifact from the newest green run and install it LAST**, after any
local building. `scripts/build.sh` builds Debug, and `CMakeLists.txt:35` makes
every non-Release build copy itself over `C:\Program Files\Common Files\VST3\`
— so a build silently replaces the tester install with a Debug binary. **The
file sizes match, so only a hash catches it.** Quit Live first; it holds
loaded bundles open, which protects them and leaves the rest clobbered.

This is "what to listen for", in priority order. The things a test can
settle are settled — 12/12 ctest, mono sum exact to 2.4e-7 at 44.1/48/96 kHz,
zero latency, no denormal tail. What is left is what only ears can answer.

## 1. The width throb — the big one

The generate stage runs two voices, one up and one down by CENTS, and
injects their **difference** into the side signal. Two tones that close beat
against each other, so the side signal periodically nulls and reopens.
Measured, side envelope peak-to-trough on a 1 kHz tone:

| CENTS | depth | beat rate |
|---|---|---|
| 5 | 25.2 dB | ~6 Hz |
| **10 (default)** | **19.6 dB** | ~12 Hz |
| 25 | 11.5 dB | ~29 Hz |
| 10, on a 110 Hz tone | 34.6 dB | ~1.3 Hz |

Average width is steady — mean side level barely moves across those. It is
the *modulation* that is deep.

In the conventional wiring (voice up to L, voice down to R) the mid picks up
the complementary sum, so when the side nulls the mid peaks and you hear the
image **swing**. Here the mid is a wire, so when the side nulls the image
**collapses to mono and reopens**. That is the price of the mono-safety, and
nobody has heard it yet.

- On a **mono vocal**, DETUNE on, CENTS 10: does the width read as steady, or
  as an audible tremolo/flutter? **Width is the target and shimmer is not** —
  MicroPitch is a research reference for how others solved this, not a
  standard to match, and its high-end sparkle is the part this module does not
  want. Widened with no shimmer is the pass.
  *Answered 2026-09-09: not an audible throb, slight tremolo at most, no high
  end added, no shimmer. Passes.*
- Does it get better or worse as CENTS goes up? The measurement says the
  throb gets *faster and shallower* — which is backwards from what a user
  would expect, since the gentle setting throbs hardest.
- On **bass or a low pad**: the beat is slowest and deepest down there
  (34.6 dB at 110 Hz). Is it unusable on low material?
- Compare against **MicroPitch or CLA Vocals** on the same source. Those comb
  in mono and this does not — is the trade audible in the direction we want?

If this reads badly, the fix is a design decision, not a bug fix: the voices
could be decorrelated, or one voice dropped, or the pair fed at unequal
depths. Flag what you hear before anyone changes it.

## 2. ASYMMETRY — does the far side widening read as depth or as phasiness

Rebuilt this pass, from the S1 manual rather than from a guess. It was a
plain balance control and moved a dead-centre source (0.5/0.5 at +50% came
out 0.75/0.25), which is the one thing Gerzon's control is defined as not
doing. It is now a shear: the centre never moves at any setting.

The cost is that the side it attenuates also **widens** — 114 % at a quarter
of the knob, 133 % at half, 200 % at the top. No linear law can hold the
centre and also pin hard-panned material at the edges.

- On a mix with **hard-panned guitars and a centre vocal**: at ASYM 25–50 %,
  does the vocal stay put? (It should, exactly.)
- Does the attenuated side read as **receding** — quieter and less tightly
  localised — or as **phasey/hollow**?
- Check it in **mono**. The sum is intended to change here, and does; nothing
  should cancel.

If the widening reads badly there is a documented fallback at the shear in
`modules/dim/dsp/DspCore.h` — `b = a/2` halves it at the cost of 2.18 dB of
centre drift at half knob. **Not** the quadratic blend, which is also written
up there and whose width is non-monotonic.

## 3. Does it work at all

> **✅ Ear review of the DETUNE fades — cleared 2026-09-11, on ICE QUEEN.**
> This flag was raised 2026-09-10 because the 8 ms fade-out (`a5e91be`) and
> the instant-on (`cfdeeff`) had only been measured, never heard. Frosty ran
> every item below in Ableton: *"Dimension all pass."* That covers the fade-out,
> instant-on, double-tap, silence re-engage, DETUNE automation, a Mix Rack
> slot, and the feel of 8 ms and of instant-on. Which builds, checked by
> SHA-256 on ICE QUEEN before and after the test:
>
> - **BMO Dimension** — main `710dd46`,
>   `6774E4CC757F169D23DD57B84A3AC06E00A2864E3CAC56F8C98015A079B3AFA2`
> - **BMO Mix Rack**, for the slot items — fork `add-bmo-deq` `900efdd`,
>   `E08B0A5B5780D79C1D48F0F9F55453700AD189EC01767E5C50B71D5F8DD4D8D1`.
>   The rack compiles its modules in, so its Dimension is the DEQ build's copy:
>   the same DSP as `710dd46`, a different binary.
>
> Re-open this if the DETUNE switch path changes. The steps it guards are also
> held offline by `dim_dsp`, and printed by `measure_dim pass`.

- Loads in a track, and in BMO Mix Rack as a slot module. No crashes.
- **Toggle DETUNE over sustained, loud mono material** — a held vocal note or
  a pad — at several points. A quiet passage cannot show a switch step; this
  item said "quiet passage" until 2026-09-10 and missed two. DETUNE fades out
  over 8 ms and comes straight back in. Listen for a click going out, and for
  a bump or tick about 15 ms after going in (what the old cleared-buffer
  version did, 0.18 on a 0.5 tone).
- **Double-tap DETUNE** (off and on within ~110 ms): that path glides back up
  instead of restarting, and should be just as clean.
- **Off for a second or more, then on over a quiet passage.** The voice
  buffers used to hold 30 ms of stale audio and burst it back out on
  re-engage — 0.90 peak over silence. Should be silent.
- **Automate DETUNE On** at real buffer sizes, as a track insert and in a
  Mix Rack slot.
- **Put it on a mono track.** It should be a wire — a stereo imager has no
  image to work on. It used to comb the channel instead.
- Automate WIDTH across its range: no stepping, no zipper.

## 4. The controls, as controls

Nothing here is a bug; it is whether the panel reads.

- **WIDTH at 0 silently disables everything above it.** WIDTH is downstream
  of the generate stage, so DETUNE does nothing with WIDTH parked low.
  Measured: peak side 0.00000. Does that trip you up in use?
- **CENTS is paired with DIFFUSE** on one row under the DETUNE switch, and
  the switch gates only CENTS. Does the row read as one gated pair?
- ~~RATE and DEPTH do nothing at the DIFFUSE 0 % default~~ **Settled
  2026-09-09: neither was audible enough to earn the space, so both lost their
  controls and are fixed at their defaults.** The panel is seven knobs and a
  switch now. `PlainKnob::setKnobEnabled` is still unused suite-wide.
- ~~ROTATE +30° moves the image LEFT~~ **Settled. Confirmed backwards by ear
  2026-09-09, sign negated, and the fix confirmed by ear on the `bc89293`
  build 2026-09-10 — + moves the image right, like a pan knob.** Measured on a
  dead-centre source: +30° gives L 0.1464 / R 0.5464, −30° the mirror, 0°
  exactly centred. `tests/dsp/DimDspTests.cpp` now asserts the direction, and
  that assertion fails against the old sign.
- **No output trim, and up to +15.5 dB available**: SHUFFLE 3.0 × WIDTH 200 %
  on anti-phase 80 Hz took a 0.5 peak to 2.98. Every other module has an
  output stage. Does Dimension need one?
- **The DETUNE switch lights `switchAlt` blue**, per the table in
  `modules/AGENTS.md` — it is not a bypass, mono or polarity. It is the only
  non-lavender thing on the panel. Does it pull the eye wrongly?

## 5. Presets

Named for the job. The three that turn DETUNE on are the ones that work on a
mono track; the rest need a stereo source to do anything.

- Do "Wide Vocal", "Mono to Stereo" and "Thicken" separate from each other,
  or are they three points on one line?
- "Bass Shuffle" sits on the S1 manual's own recommendation (2.0 @ 650 Hz)
  — does that read as more spacious, or just louder in the low end?
- Anything that jumps in level against Init at the same settings.

## 6. Light mode (low priority)

The panel is nine captions in lavender, and on the pale plate that is
1.73:1. This is *not* new and is not a Dimension decision — captions carry
the raw accent by Frosty's call in 0.2.3, documented at
`core/ui/Controls.cpp` ("do not fix it"), and the whole suite sits in the
1.72–2.00:1 band. Dimension is the first panel where *every* caption carries
it, so it is the worst case of an accepted trade. Gut-check only.
