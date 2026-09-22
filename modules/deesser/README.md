# BMO Defang

A de-esser. **It takes the bite out of your recordings.**

Five controls, one band, no latency. Set where the sibilance lives, how far it
has to stand out before the module acts, and how deep the cut may go.

> **Not finished.** The panel, the parameters and the metering are real; the
> processing is a marked placeholder that passes audio through untouched. See
> `AGENTS.md` and `docs/deesser/`.

## The face

| Control | What it does |
|---|---|
| **FREQ** | Where the sibilance is: 2 to 10 kHz. The band centre in Bell, the corner in Shelf. Male voices usually land around 3 to 6 kHz and female voices around 6 to 8. |
| **Q** | How narrow the band is, 0.7 to 6. Narrow stays clear of the vowels underneath; wide covers a voice whose sibilance moves about. A shelf ignores anything past 2, where a shelf stops being a shelf. |
| **THRESH** | How far the band has to stand out before the module acts, -24 to +24. **It reads "+3.0 dB over", and the word matters** -- see below. |
| **RANGE** | The deepest the cut may go, 1 to 18 dB. Most work is done at 2 to 6. |
| **SHAPE** | **BELL** takes out a notch at FREQ. **SHELF** takes everything above FREQ down together. |
| **LISTEN** | **Hold it** to hear what is being taken out, on its own. Let go and you are back to the whole signal. |

## THRESH is not a level

Every other threshold you have set is in dBFS: a line the signal crosses, which
you have to move again when the singer moves. **This one is not.** It is how
far the sibilance stands out *above the rest of the track* — which is why the
value reads "+3.0 dB over" rather than "+3.0 dB".

Turn the whole take up 6 dB and nothing about this changes: the band goes up
6 dB and so does what it is measured against. **Set it once for a voice and it
holds across the take**, quiet verse and loud chorus alike.

Zero sits at where a typical vocal balance already is, so start there and move
it a few dB either way: **up** if it is catching words that were not hissing,
**down** if esses are getting through.

## The picture

The box at the top draws the band you have set: a notch for BELL, a step down
for SHELF, at the depth RANGE allows. It is the *deepest* cut, drawn to scale
— the module only goes that far when an ess is loud enough to ask for it. It
is a drawing of your settings, not a display of the signal.

## The meter

The needle shows **how much is being taken out of the band right now**, 0 to
24 dB, with IN and OUT beside it for levels either side.

It reads the band, not the whole mix. A 6 dB cut on a narrow band barely
changes the broadband level, so a meter that showed the overall change would
sit near zero while the module was working hard — which is the opposite of
useful. What you see is the work being done.

## Using it

Start on **BELL**. Hold **LISTEN** and turn **FREQ** until the esses are the
loudest thing you can hear; let go. Bring **THRESH** down until the meter moves
on the sibilance and stays still on everything else. Set **RANGE** for as much
as it takes and no more — 2 to 6 dB does most jobs, and if you are reaching for
18 the problem is probably the microphone.

**SHELF** is for a source that is already bright all over, where a notch would
be heard as a notch. It takes the whole top end down together, so it dulls if
you lean on it: shallower RANGE, and a low Q.

There is no wet/dry control, and no mix knob is coming: a partial blend of a
cut like this is just a shallower cut, which is what RANGE already is.

## Presets

**Init** is the defaults. **Male Vocal** and **Female Vocal** are the same
setting an octave apart. **Bright Vocal Shelf** is the shelf, used shallow, for
a source that was recorded bright. **Dialogue** is narrow and gentle, because a
lisp is worse than a surviving ess when someone is being listened to rather
than mixed. **Overheads Safe** is held well back, for cymbals, where the job is
mostly to stay out of the way.
