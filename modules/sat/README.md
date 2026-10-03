# BMO Saturator

A saturator fitted to a measured reference: an asymmetric curve that carries
the character, two harmonic generators that place new energy above 2.5 kHz,
and a voicing equaliser (TONE) that does most of what the reference does to
the top end.

## Controls

| Control | What it does |
|---|---|
| **Input** | How hard the signal arrives at the curve; the curve is level-dependent, so this is a second drive. -24 to +24 dB. |
| **Drive** | How strong the curve is, 0-100. Even at 0 it is the curve, just nearly linear. |
| **Tone** | The voicing, a bell near 7 kHz up to +13.5 dB with a 40 Hz high-pass, from nothing at 0 to the fitted shape at 100. Default 55. |
| **Mix** | Wet against the dry signal, delay-matched so a partial mix never combs. |
| **Output** | Level after everything, -24 to +24 dB. |
| **Sat In** | The stage in or out; out, the signal still passes the oversampling. |
| **Phase** | Flips what leaves the plugin, dry included. |
| **Auto Gain** | Matches output level to input over about 1.5 s. Slow on purpose: it moves the level, not the dynamics. |
| **Oversampling** | Off, 2x, 4x, 8x. Off reports no latency; the others report 40, 60 and 70 samples. |

Every switch fades over 10 ms rather than stepping. Changing the
oversampling dips the output briefly, because the latency changes with it.

## Presets

Eleven, Init first. Every preset but Init sets its own Tone, so presets made
before the default moved to 55 sound as they did.
