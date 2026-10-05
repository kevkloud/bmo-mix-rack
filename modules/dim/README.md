# BMO Dimension

A stereo imager that works on the side signal only.

Split to mid/side, do everything to the side, sum back. The mid path is left as
a plain wire, so — apart from two controls that say so on the tin — anything
this module does disappears in a mono sum instead of comb-filtering it.

## The three stages

| Stage | What it does | In the spirit of |
|---|---|---|
| **Generate** | Two detuned voices, one up and one down, their *difference* injected into the side signal | pitch-shift wideners and vocal doublers |
| **Diffuse** | A modulated all-pass cascade on the side signal | classic stereo choruses, phasers |
| **Image** | Width, Gerzon bass shuffler, rotation, asymmetry | stereo imagers |

Generate is the only stage that *manufactures* signal rather than shaping it,
and the only one that is not mono-safe — which is why it is the one on a
switch. It is also what makes the module do anything at all on a mono source:
a mono track has no side signal, and an all-pass of zero is zero.

## Controls

The panel is in two sections, and the host names match the captions — an
automation lane is called what the knob is called. The parameter IDs underneath
(`width`, `shuffle`, `detune_on`, ...) are the permanent part and never
followed the words; the old names are in brackets.

### SOURCE — making width from nothing

**GENERATE** (Detune On) — switches in the generate stage. A mono source has
no side signal, so without this nothing below it has anything to work on.

**DETUNE** (the CENTS knob) — how far apart the two voices are pitched, in
cents. Around 10 is the classic setting. The range stops at 25 rather than
the 50 a pitch-shift widener commonly allows, because past about 25 it stops widening and starts sounding
out of tune.

**DRIFT** (Diffuse) — how much side signal goes through the swept all-pass
network. It works on any side content, not only what GENERATE makes. Its speed
and depth are **Drift Rate** and **Drift Depth** (Rate, Depth), fixed at
0.40 Hz and 50 % with no knobs: neither was audible enough to earn one. Slow on
purpose — this is a widener, and an audible sweep is a different job.

### WIDTH — shaping the width that is there

**DIMENSION** (Width) — the side signal scaled, 100 % being unity. The hero
knob. Same law and range as BMO Util's WIDTH, deliberately. It comes after
GENERATE, DRIFT and BLOOM, so at 0 it silences all three.

**BLOOM** / **BELOW** (Shuffle / Shuffle Freq) — Gerzon's bass shuffler,
widening the low end alone to correct for the ears hearing stereo as narrower
in the bass. BLOOM at 1.0 is off; the usual guidance for a shuffler puts the useful
range at 1.6–2.5. BELOW is the frequency it works under, printed on the panel, and the
usual recommendation is 600–700 Hz.

**TURN** (Rotation) — the whole soundfield turned, without changing the
relative levels of anything standing on it. Positive degrees move the image
**right**, like a pan knob. Its ends are marked L and R. **At its ends it
breaks mono:** at full L a centre source lands hard left, 3 dB down in a mono
sum, and anything hard-panned *left* comes out in anti-phase and disappears
from a mono sum entirely (measured at −180 dB); full R does the same to
anything hard-panned right. It is the one control here that can take
something out of the mono sum altogether — everywhere else the centre comes
through to within −143.5 dBFS.

**TILT** (Asymmetry) — left against right, with centre material left exactly
where it is. This is not a balance control and not a pan; a dead-centre vocal
does not move at any setting. It is the reason this module is not just a width
knob with a crossover. Positive settings favour the **right**, the same way
TURN turns: material on the right comes up and material on the left goes down.
(It leaned left until 2026-09-16, when the sign was flipped to agree.) Its ends
are marked L and R.

**OUTPUT** — a trim on both channels after everything else, ±24 dB, 0 by
default. Added 2026-10-03; a session or preset saved before it plays exactly
as it did, and no factory preset sets it.

## Presets

Init is a wire — a stereo imager that widened the moment you inserted it would
be making a decision you have not made yet.

The rest split on one line: whether your source already has side content.

- **Wide Vocal**, **Mono to Stereo**, **Thicken** turn GENERATE on, so they work
  on a mono track.
- **Bass Shuffle**, **Diffuse Pad**, **Narrow** only scale and steer what is
  already there, so they need a stereo source to do anything.

## Things worth knowing before you use it

- **DIMENSION at 0 silences GENERATE, DRIFT and BLOOM** — it sits downstream
  of all three. TURN still works there, because it comes after DIMENSION and
  turns the centre itself, and TILT works whenever TURN is away from 0.
- **A knob that does nothing where the others stand is dimmed**, and comes back
  the moment it would do something again. Its value is kept and still
  automates. DETUNE is dimmed until GENERATE is on; BELOW while BLOOM is at
  1.0, where there is no shuffling for it to place; and at DIMENSION 0,
  DETUNE, DRIFT, BLOOM and BELOW, and TILT too unless TURN is away from 0.
  On a fresh instance that means DETUNE and BELOW start dimmed.
- **Extreme BLOOM and DIMENSION together can add real level** — up to +7.2 dBFS
  peak from a source at -18 dBFS RMS. **OUTPUT** (±24 dB, 0 by default) is there to
  take it back; it is a hand-set trim, not an automatic match.
- **On a mono track it is a wire**, by design. There is no image to work on.
  OUTPUT still works there: it is a level, not part of the image.
- **The detune stage throbs.** The two voices beat against each other, so the
  width pulses — roughly 12 Hz at the default, slower and deeper on bass. This
  is a known open question and is the main thing the module is being listened
  to for.

## Building

The two display faces are licensed and are not in this repository. Point your
working copy at them once:

    scripts/set-font-dir.sh <path-to-your-font-folder>

See `assets/fonts/README.md` for the full resolution order.

---

Implementation notes, the measurements behind the voicing, and what is still
open are in [`AGENTS.md`](AGENTS.md) and
[`../../testing-notes/dim-1.0-handoff.md`](../../testing-notes/dim-1.0-handoff.md).
