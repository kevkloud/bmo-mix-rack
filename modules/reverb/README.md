# BMO Linger

A reverb. **It stays after the note has gone.**

Two spaces in one module — the early reflections that tell you where you are,
and the tail that tells you how big it is — each with its own fader, so you can
have one without the other. No latency.

> **Not finished.** The panel, the parameters and the display are real; the
> processing is a marked placeholder that passes audio through untouched.
> Nothing here has been heard. See `AGENTS.md` and `docs/reverb/`.

## The face

Seven controls and a picture. Everything else is one click away.

| Control | What it does |
|---|---|
| **TYPE** | Room, Chamber, Hall, Large Hall, Plate, Ambience. Small to large, then a plate, then Ambience — which is the one with almost no tail. |
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

## The picture

The box at the top draws what you have set, from left to right in time: the
sound itself at the left edge, the early reflections as a cluster of lines, the
gap your pre-delay leaves, and then the tail's envelope decaying away.

**The time axis is logarithmic, from 1 ms to 30 s.** It has to be. A reflection
arrives 7 ms in and a tail can run for twenty seconds, and no straight ruler
shows both. Each vertical line is ten times further along than the last.

The tall reflections are loud ones; a reflection that leans above the centre
line is to your right and one that leans below is to your left. As you turn
DENSITY up, more of them fill in between — and the ones that were already there
do not move or change level, which is what stops the sweep clicking.

The tail is drawn as a shape rather than a line because it does not decay at
one rate: the pale band is the range between its fastest and its slowest band,
which is what the **LOW x** and **HIGH x** controls set. A wide band means a
tail that changes colour as it dies.

Nothing in the box is measured from the audio. It is drawn from the controls,
so it costs nothing and cannot affect the sound.

## Expanded

The arrow on the header opens three more groups. In a rack the module opens
compact; on its own it opens expanded.

**EARLY** — how the reflections are made.

| Control | What it does |
|---|---|
| **ER MODE** | **Taps** places reflections where a room would. **Energy** replaces them with a shaped cloud. **Blend** is the two ideas at once — and it is the one setting nobody has listened to yet. |
| **DENSITY** | From a handful of distinct reflections to a dense early wash, with no step in between and no level change across the sweep. |
| **ER SHAPE** | In Energy mode, how the cloud builds: fast and sharp at the bottom, slow and sustained at the top. |
| **ER SPREAD** | How long it sustains for, 5 to 200 ms. |
| **ER HI-CUT** | Takes the top off the reflections. **This is the boxiness control.** |
| **VARIATION** | Seven different reflection patterns, narrow to wide. **Var 6 is the widest — and the reflections disappear completely if the track is summed to mono.** Every other position survives a mono sum. |
| **LINK ER** | Off, the reflections arrive with the dry signal and only the tail is pre-delayed. On, they move together. |
| **SOURCE** | What feeds the tail: the dry signal at one end, the early reflections at the other. Turn it up and the tail inherits the room's own timing and colour. |

**TAIL** — how it decays.

| Control | What it does |
|---|---|
| **ATTACK** | How long the tail takes to bloom in, 0 to 120 ms. At zero it arrives with the reflections; turned up it swells in behind them. |
| **DECAY SHAPE** | At the top it is a natural decay. Turned down it truncates — gated at the bottom end. |
| **LOW x FREQ / LOW x** | Below the frequency you set, the tail decays this much faster or slower than DECAY says. Over 1.00x the low end rings longer, which is what a large room does. |
| **HIGH x FREQ / HIGH x** | The same above its frequency. Under 1.00x the top dies first, which is also what a real room does. |

**TONE & OUT** — everything else.

| Control | What it does |
|---|---|
| **EQ LOW / EQ HIGH** and their frequencies | Two shelves on what feeds the reverb, +12 to −24 dB. At the bottom each reads `Cut`. |
| **IN HI-CUT** | Darkens what feeds *both* generators, before the shelves. |
| **WIDTH** | How wide the tail is. The reflections have their own width; this does not touch them. |
| **MOD DEPTH / MOD RATE** | A slow random movement in the tail that stops it ringing on one note. It is random rather than a sweep, so it should not sound like a chorus. |
| **OUTPUT** | Trim. |

## Presets

Named for what you would put them on. **Vocal Depth, No Tail** is the one worth
trying first: the tail is switched off entirely and the reflections alone put
the singer further back.
