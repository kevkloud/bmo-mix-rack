# BMO Linger M3b: listening pass, 2026-10-06

Two sets rendered on **ICE QUEEN** with `measure_reverb render`, every file at
OUTPUT -4 dB, dry references included, kept outside the repository. Heard by
Frosty on 2026-10-06 on **monitors and headphones**, headphone amp in
**stereo**. His answers are quoted as he wrote them.

- Set 1, modulation, from `4505ff4`: 15 files with the same names as their
  counterparts in the M3a set, so each could be compared against M3a.
- Set 2, the Reverb EQ and DARKEN, from `d51bd23`: 18 files, a vocal on Hall
  and a snare on Plate.

## Set 1: modulation

| # | question | answer |
|---|---|---|
| 1 | Flutter and metallic ring on Ambience and Room (vocal, snare tail): gone, reduced, or the same as M3a? | "flutter is gone" |
| 2 | The held note on Hall, Cavern and Plate: is the ring gone, and does the modulation read as wobble or chorus? | "ring is gone, depending on type it reads as wobble, but in a good way" |
| 3 | Chamber, Hall, Cavern, Plate snare tails and the acoustic on Chamber: anything worse than M3a? | "sounds good" |
| 4 | SOURCE 0 / 70 / 100 on the woodblock, against M3a | "sounds better" |

That closes the item M3a's pass left open (flutter and metal at the short
end, re-render after modulation).

## Set 2: the Reverb EQ and DARKEN

| # | question | answer |
|---|---|---|
| 5 | DARKEN at 6 kHz and 2 kHz: does it darken the room as the knob suggests, and is 2 kHz dark enough at the bottom? | "yes it works as expected, but maybe range down to 1khz" |
| 6 | Lo Cut at 250 Hz: does it clear the reverb's low end without thinning the vocal? | "yes" |
| 7 | Low shelf, +6 and -12 dB at 200 Hz | "audible, working as intended" |
| 8 | Bell at 2.5 kHz, +6 and -9 dB, Q 2 | "sounds good" |
| 9 | High shelf +6 dB at 6 kHz, and Hi Cut at 5 kHz | "working as intended" |
| 10 | Bandpass, 300 Hz to 5 kHz | "sounds good" |
| 11 | Snare on Plate, flat against Lo Cut 250 Hz with DARKEN 6 kHz | "close but audibly different" |
| 12 | Flat default against the M3a renders, the 20 Hz high-pass and DARKEN at 20 kHz now always in: duller or thinner? | "i like this one more" |

## Decisions, same day

| | question | answer |
|---|---|---|
| A | Decay truncation: build it or leave it out? | "leave it out. I'm happy where we're at" |
| B | The 150 ms wet fade in `reset()`: record as not buildable in the DSP? | "record as not buildable and recommend making a bypass" |
| C | DARKEN's drawn curve against its sound: the page draws the engine's own curve (1), or leave the picture (2)? | "option 1 if it doesn't increase cpu significantly" |
| D | The spectrum behind the EQ curve: keep it ahead of the EQ, or move it after? | "it should show the output, with EQ applied" |
| E | ATTACK as rising-gain taps on the pre-delay line, values to calibrate by ear? | "yes" |
| F | Merge `main`, re-run every suite, push and open a PR, or hold until ATTACK is in? | "proceed" |

## What was done with them

- **C:** done. The page draws `InputStage::lowPassDbAt` at the rate it is
  drawn at. It is paint; the audio thread's cost did not change.
- **D:** done, read as **the input stage's output**: what the room is given,
  after the high-pass, DARKEN and the EQ. Not the module's output, which
  would show the reverb and the dry signal. My reading of his words; easy to
  move again if he meant the other.
- **A, B:** recorded in `10` section 4, "As built in M3b". Nothing built. The
  bypass he asks for is a rack change (`10` section 8, item 4).
- **5, "maybe range down to 1khz":** not done. The schema froze at 0.2.6 and
  a range is one of the things a saved session references. Raised with him.
- **11:** "close but audibly different" is taken as the EQ doing something
  modest on a snare, not as a fault. Not followed up.
- **E:** next.
