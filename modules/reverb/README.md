# BMO Linger

A reverb. **It stays after the note has gone.**

Two spaces in one module — the early reflections that tell you where you are,
and the tail that tells you how big it is — each with its own fader, so you can
have one without the other. No latency.

> **Not finished.** The panel, the parameters and the display are real; the
> processing is a marked placeholder that passes audio through untouched.
> Nothing here has been heard. See `AGENTS.md` and `docs/reverb/`.

## The shape of it

It is built like a handheld. A big screen in a recess at the top, and **the page
menu is on the screen** — **EARLY**, **TAIL**, **EQ** across the top of the
display, the one you are on lit. Tapping one changes two things at once: what the
screen is drawing, and which six controls are on the panel under it.

Under the screen there is a line of print, then a **row of segments** — but only
on two of the three pages. On EARLY it is **ER MODE**; on EQ it is **LOW**,
**MID**, **HIGH**, which chooses which band the three EQ knobs under it are
holding. TAIL has nothing there, and that is the point: if there are segments,
there is something on this page to pick between.

Five controls never move, whatever page you are on: **ER**, **REVERB** and
**MIX** as three faders along the foot, and **TYPE** over **DECAY** in the corner
beside them. Those are the ones you reach for without thinking about which part
of the reverb you are in.

There is one size of window. The module does not expand, because there is nothing
to expand into — a fourth page is what this shape is for.

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
them, and a **FILTER** control that turns either or both of the outer two into
cuts. That is deliberate rather than opportunistic — the argument for taking the
damping frequencies off the panel was "reach for the EQ when what you want is a
frequency", and the EQ had to be worth reaching for.

## Always on the panel

| Control | What it does |
|---|---|
| **TYPE** | Room, Chamber, Hall, Cavern, Plate, Ambience. Small to large, then a plate, then Ambience — which is the one with almost no tail. **Picking a type re-sets the controls that belong to it**, every time: a type is a voicing rather than a label, so SIZE, SOURCE, DENSITY, ER SPREAD, the two modulation controls, IN HI-CUT and the ER and REVERB faders all move to what that type is — and so do the five settings that no longer have a knob at all. Everything else stays where you put it. It is a menu rather than a knob, and it sits in the corner rather than in the grid, because a menu is not knob-shaped. |
| **DECAY** | How long the tail takes to die, 0.1 to 20 s. It is the one knob on the panel that prints its own number, because the three faders beside it print theirs. |
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

Three pictures, one per page, and the line of print under the screen tells you
what you are looking at.

**EARLY** scatters the reflections across the box: **left to right is when each
one arrives, top to bottom is where it comes from** — top is hard left, bottom is
hard right — **and the size of the dot is how loud it is**. `L` and `R` at the
right-hand end say which way round it is, and the ringed dot at the far left on
the centre line is the dry sound everything else is measured from.

Turn **VARIATION** and the whole cluster fans open or closes toward the middle,
which is the one thing that control does. Turn **SIZE** and the window stretches
and shrinks with it — the first reflection stays at the left and the last at the
right. Turn **DENSITY** up and faint vertical lines fill in between the dots:
they are lines rather than dots because those extra reflections do not have a
direction picked out for them yet, and a dot would be claiming one. The print
says how many there are and how wide the window is.

**TAIL** draws **three curves — low, mid and high** — because a tail does not die
at one rate. The heavy one in the middle is what **DECAY** says; the two light
ones either side of it are what **LOW x** and **HIGH x** do to it. Spread them
apart and you have a tail that changes colour as it fades; bring them together
and it fades evenly. Each one starts with the swell the type gives it and then
decays away to nothing.

The time ruler is logarithmic — each labelled line is ten times further along
than the last — because an onset lasts a tenth of a second and a tail can run for
twenty, and no straight ruler shows both. **It ends just after your tail does**,
so the shape fills the box at any setting instead of stopping two thirds of the
way across and ruling a flat line over the rest. What that costs is that the
curves stay roughly put as you turn DECAY while the scale under them moves, which
is why the decades are labelled inside the box and why the print gives the two
outer times in seconds. The print also gives the onset in milliseconds, and
**that is the one number on this panel with no knob under it** — change TYPE and
watch it move.

**EQ** draws the whole chain as one curve. The three EQ bands are marked on it,
and **each marker says two things at once**: it is **filled** when that band is
actually doing something, and it has a **ring round it** when it is the band the
three knobs are holding. So you can see at a glance which band you are about to
move and which bands are shaping the sound, and the two are not the same
question.

The input high-cut is **not** a fourth marker but a **shaded area running from
its corner off the right-hand end**, because it is a different control in a
different place: it darkens what goes *into* the reverb, ahead of the EQ, where
the EQ darkens the reverb. They are in series, so the curve is all four together
rather than four separate lines. The area between the curve and the flat line is
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
| **ER MODE** (the segments) | **Taps** places reflections where a room would. **Energy** replaces them with a shaped cloud. **Blend** is the two ideas at once — and it is the one setting nobody has listened to yet. |
| **DENSITY** | From a handful of distinct reflections to a dense early wash, with no step in between and no level change across the sweep. |
| **ER SPREAD** | How long the early cloud sustains for, 5 to 200 ms. |
| **ER HI-CUT** | Takes the top off the reflections. **This is the boxiness control.** |
| **VARIATION** | Seven different reflection patterns, narrow to wide — and the screen fans open and shut as you turn it. **Var 6 is the widest — and the reflections disappear completely if the track is summed to mono.** Every other position survives a mono sum. |
| **SOURCE** | What feeds the tail: the dry signal at one end, the early reflections at the other. Turn it up and the tail inherits the room's own timing and colour. |
| **SIZE** | How big the space is, 0.5 to 80 m. It moves the reflections apart and the tail with them; it does not glide, so a held note does not bend while you turn it. It is on this page because it is what the picture's own ruler is made of. |

## TAIL — how it behaves once it is there

| Control | What it does |
|---|---|
| **LOW x** | Below the type's low crossover, the tail decays this much faster or slower than DECAY says. Over 1.00x the low end rings longer, which is what a large room does. |
| **HIGH x** | The same above the high crossover. Under 1.00x the top dies first, which is also what a real room does. |
| **MOD DEPTH / MOD RATE** | A slow random movement in the tail that stops it ringing on one note. It is random rather than a sweep, so it should not sound like a chorus. Neither of these two shows on the screen yet. |
| **WIDTH** | How wide the tail is. The reflections have their own width; this does not touch them. |
| **PRE-DELAY** | How long before the tail arrives, 0 to 250 ms. **The dry signal is never delayed**, and neither are the reflections — this moves the tail and only the tail, which is why it is on this page. |

## EQ — what goes into the reverb, and what comes out

Three bands on what feeds the reverb, each with a frequency, a gain and a Q. The
shapes are fixed — low shelf, bell, high shelf — and there is no shape menu, on
purpose: a menu whose length can never change again after release is worse than
three bands that do one job each.

**There is one set of FREQ, GAIN and Q, and the LOW / MID / HIGH segments above
them choose which band they are holding.** All three bands are always live and a
host can automate all nine; the panel shows you one at a time and the marker with
the ring round it on the screen is the one you are on.

**The three knobs are not the same control on each band**, and the print under
the screen is what keeps you honest about it: FREQ reaches 1.6 kHz on the low
shelf, the whole band on the bell and from 1 kHz up on the high shelf, and Q
stops at 2.0 on the two shelves where it runs to 40 on the bell. Same angle,
different number.

| Control | What it does |
|---|---|
| **LOW** + FREQ + GAIN + Q | A low shelf, 16 Hz to 1.6 kHz, +12 to −24 dB. At the bottom it reads `Cut`. |
| **MID** + FREQ + GAIN + Q | A bell, and the wide one: **20 Hz to 20 kHz**, so it reaches anywhere. Q goes to 40 for a notch. |
| **HIGH** + FREQ + GAIN + Q | A high shelf, **1 to 20 kHz**, so it reaches air. |
| **FILTER** | A dial with the four positions written round it — **OFF**, **L**, **H**, **B**. L turns **LOW** into a low cut, H turns **HIGH** into a high cut, and B does both — which is what a low cut plus a high cut is. The frequencies and the Qs mean the same thing whichever shape a band is in — a corner and a resonance — so only that band's GAIN changes, and it greys out when you are on that band, because a cut has no gain to set. **It keeps what you set it to**: come back off the cut and the shelf is where you left it. The MID bell is untouched in all four. |
| **IN HI-CUT** | Darkens what feeds *both* generators, ahead of the EQ. **This is not the same as EQ HIGH in filter mode**: this one is on the way in, that one is on the reverb. |
| **OUTPUT** | Trim. |

At its defaults the EQ does nothing at all — every gain is 0 dB and FILTER is
off — so a fresh instance is not quietly coloured.

## Presets

Named for what you would put them on. **Vocal Depth, No Tail** is the one worth
trying first: the tail is switched off entirely and the reflections alone put
the singer further back.
