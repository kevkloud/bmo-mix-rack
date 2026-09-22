# BMO Linger

A reverb. **It stays after the note has gone.**

Two spaces in one module — the early reflections that tell you where you are,
and the tail that tells you how big it is — each with its own fader, so you can
have one without the other. No latency.

> **Not finished.** The panel, the parameters and the display are real; the
> processing is a marked placeholder that passes audio through untouched.
> Nothing here has been heard. See `AGENTS.md` and `docs/reverb/`.

## The shape of it

It is built like a handheld: a screen in a recess at the top with a line of
print under it, three round keys — **EARLY**, **TAIL**, **EQ** — and then the
controls. The keys change two things at once: what the screen is drawing, and
which five, six or twelve controls are on the panel under it.

Three controls never move, whatever key you are on — **SIZE**, **PRE-DELAY**,
**DECAY** — and four more sit along the foot: **ER**, **REVERB**, **MIX**, and
the **TYPE** menu in the bottom-right corner. Those are the ones you reach for
without thinking about which part of the reverb you are in.

There is one size of window: three columns of knobs, 120 px narrower than it
used to be. The module does not expand, because there is nothing to expand
into.

## Six controls left, and six others arrived

Six were taken off this panel before it shipped, and none of them went away —
each is now part of what a **TYPE** *is*, set by the type you pick rather than
by you:

- **ATTACK**, the swell the tail comes in on.
- **DECAY SHAPE**, the curve it dies on.
- **ER SHAPE**, how the early cloud builds.
- the two damping **frequencies**. The amounts stayed. Reach for the EQ when
  what you want is to choose a frequency.
- **LINK ER**, which is now simply always off — the reflections travel with the
  dry signal, which is the reference behaviour and what every type shipped.

The reasoning is the same each time: these are what makes a Plate a Plate and a
Cavern a Cavern, not what an engineer dials mid-session. A panel with six fewer
of them is a panel you can read.

**And then the EQ grew into the room they left.** The two shelves became a
proper three-band parametric: a **Q** on each band, a **MID** bell between
them, and a **FILTER** key that turns the outer two into a low cut and a high
cut. That is deliberate rather than opportunistic — the argument for taking the
damping frequencies off the panel was "reach for the EQ when what you want is a
frequency", and the EQ had to be worth reaching for.

## Always on the panel

| Control | What it does |
|---|---|
| **TYPE** | Room, Chamber, Hall, Cavern, Plate, Ambience. Small to large, then a plate, then Ambience — which is the one with almost no tail. **Picking a type re-sets the controls that belong to it**, every time: a type is a voicing rather than a label, so SIZE, SOURCE, DENSITY, ER SPREAD, the two modulation controls, IN HI-CUT and the ER and REVERB faders all move to what that type is — and so do the five settings that no longer have a knob at all. Everything else stays where you put it. It is a menu rather than a knob, and it sits in the corner rather than in the grid, because a menu is not knob-shaped. |
| **SIZE** | How big the space is, 0.5 to 80 m. It moves the reflections apart and the tail with them; it does not glide, so a held note does not bend while you turn it. |
| **PRE-DELAY** | How long before the tail arrives, 0 to 250 ms. **The dry signal is never delayed**, so this cannot comb against what you put in. |
| **DECAY** | How long the tail takes to die, 0.1 to 20 s. |
| **ER** | How loud the early reflections are, or **Off**. |
| **REVERB** | How loud the tail is, or **Off**. |
| **MIX** | Dry against the two of them together. |

**ER and REVERB are two faders, not one.** That is the whole point of the
module. Turn REVERB off and ER becomes a distance control: the source moves
back without anything washing over it, which is the thing a reverb usually
cannot do to a close vocal or a dry snare. Turn ER off and you have a tail with
no room attached to it.

**"Off" means off.** At the bottom of its travel a fader reads `Off` rather
than `-40.0 dB`, because the bus is silent rather than quiet.

## The screen

Three pictures, one per key, and the line of print under the screen tells you
what you are looking at.

**EARLY** draws the reflections as a row of upright lines, one per reflection,
standing where they arrive and as tall as they are loud. The window is the real
one — the first reflection at the left and the last at the right — so it
stretches and shrinks as you turn SIZE. Turn DENSITY up and fainter lines fill
in between, and the ones already there do not move or change height, which is
what stops the sweep clicking. The print says how many there are and how wide
the window is.

**TAIL** draws the tail's shape: the swell the type gives it, then the decay
away to nothing. The time ruler is logarithmic, from 1 ms to 30 s, because a
onset lasts a tenth of a second and a tail can run for twenty — each vertical
line is ten times further along than the last, and no straight ruler shows
both. It is drawn as a band rather than a line because it does not die at one
rate: the band is the range between its fastest and its slowest, which is what
**LOW x** and **HIGH x** set. A wide band means a tail that changes colour as
it fades. The print gives the decay, the onset in milliseconds, and where the
whole thing actually ends. **The onset is the one number on this panel with no
knob under it** — change TYPE and watch it move.

**EQ** draws the whole chain as one curve with four marks on it — the three EQ
bands, and then the input high-cut. They are in series, so what you see is all
four together rather than four separate lines. **The three EQ bands are solid
dots and the input cut is a hollow one**, because it is a different control in
a different place: it darkens what goes *into* the reverb, ahead of the EQ, and
the EQ darkens the reverb. The area between the curve and the flat line is
shaded, which is a gentle lens for a shelf and a pair of wedges running off the
bottom for a cut — so **FILTER** is unmistakable at a glance. The print gives
the three EQ frequencies and says `LO CUT` and `HI CUT` when they are cuts.

**There is a spectrum analyser behind the curve on the EQ page**, so you can
see what you are shaping. It is the only thing on this panel measured from the
audio; EARLY and TAIL are drawn from the controls alone and cost nothing.

> While the processing is a placeholder the analyser shows the signal going
> **in**, unchanged, because that is all there is — the reverb is not built
> yet. It is reading the right point; there is just nothing happening at it.

## EARLY — how the reflections are made

| Control | What it does |
|---|---|
| **ER MODE** | **Taps** places reflections where a room would. **Energy** replaces them with a shaped cloud. **Blend** is the two ideas at once — and it is the one setting nobody has listened to yet. |
| **DENSITY** | From a handful of distinct reflections to a dense early wash, with no step in between and no level change across the sweep. |
| **ER SPREAD** | How long the early cloud sustains for, 5 to 200 ms. |
| **ER HI-CUT** | Takes the top off the reflections. **This is the boxiness control.** |
| **VARIATION** | Seven different reflection patterns, narrow to wide. **Var 6 is the widest — and the reflections disappear completely if the track is summed to mono.** Every other position survives a mono sum. |
| **SOURCE** | What feeds the tail: the dry signal at one end, the early reflections at the other. Turn it up and the tail inherits the room's own timing and colour. |

## TAIL — how it behaves once it is there

| Control | What it does |
|---|---|
| **LOW x** | Below the type's low crossover, the tail decays this much faster or slower than DECAY says. Over 1.00x the low end rings longer, which is what a large room does. |
| **HIGH x** | The same above the high crossover. Under 1.00x the top dies first, which is also what a real room does. |
| **MOD DEPTH / MOD RATE** | A slow random movement in the tail that stops it ringing on one note. It is random rather than a sweep, so it should not sound like a chorus. |
| **WIDTH** | How wide the tail is. The reflections have their own width; this does not touch them. |

## EQ — what goes into the reverb, and what comes out

Three bands on what feeds the reverb, each with a frequency, a gain and a Q,
one band per row. The shapes are fixed — low shelf, bell, high shelf — and
there is no shape menu, on purpose: a menu whose length can never change again
after release is worse than three bands that do one job each.

| Control | What it does |
|---|---|
| **EQ LOW** + FREQ + Q | A low shelf, 16 Hz to 1.6 kHz, +12 to −24 dB. At the bottom it reads `Cut`. |
| **EQ MID** + FREQ + Q | A bell, and the wide one: **20 Hz to 20 kHz**, so it is the only band that reaches the presence region. Q goes to 40 for a notch. |
| **EQ HIGH** + FREQ + Q | A high shelf, 1 to 2.1 kHz. |
| **FILTER** | Turns **EQ LOW** into a low cut and **EQ HIGH** into a high cut. The frequencies and the Qs mean the same thing in both modes — a corner and a resonance — so only the two GAIN knobs change, and they grey out, because a cut has no gain to set. **They keep what you set them to**: switch FILTER back off and both shelves are where you left them. The MID bell is untouched either way. |
| **IN HI-CUT** | Darkens what feeds *both* generators, ahead of the EQ. **This is not the same as EQ HIGH in filter mode**: this one is on the way in, that one is on the reverb. |
| **OUTPUT** | Trim. |

At its defaults the EQ does nothing at all — every gain is 0 dB and FILTER is
off — so a fresh instance is not quietly coloured.

## Presets

Named for what you would put them on. **Vocal Depth, No Tail** is the one worth
trying first: the tail is switched off entirely and the reflections alone put
the singer further back.
