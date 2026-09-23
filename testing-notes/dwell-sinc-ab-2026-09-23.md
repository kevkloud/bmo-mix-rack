# BMO Dwell — the sinc A/B: what to decide, and somewhere to write it

**Prepared on AURORA, 2026-09-23. The answers are not filled in yet.**
This is a form. Frosty listens, writes here, and the decision gets committed.

## What is being decided

Clean's interpolator reads through a **32-tap sinc**, and that is the single
largest cost in the module — it is why Clean is the *most* expensive character
despite doing the least, and why the heaviest case is Clean + Diffuse rather
than bucket-brigade.

**32 was never chosen for Dwell.** `10` §1 argues sinc against Hermite but says
nothing about the width beyond reusing `modules/tune/dsp/SincTable.h`. Thirty-two
*was* measured — in Tune, for reads at a rate other than 1, where the kernel
doubles as the anti-alias filter. Dwell's clean read is at rate 1, so that
reasoning does not transfer.

**The question: can you hear the difference, and at which width does it start?**

## What the measurements already settle

Measured on AURORA, 48 kHz. CPU is ms per 10 s of audio.

| taps | clean bare | heaviest | alias floor | 18 kHz | 18 kHz warble |
|---|---|---|---|---|---|
| 8 | 51.0 (−40 %) | 207 (−30 %) | −90.8 | −2.68 dB | 2.66 dB |
| 12 | 52.2 (−39 %) | 212 (−29 %) | −89.7 | −0.77 dB | — |
| 16 | 62.9 (−26 %) | 246 (−17 %) | −89.0 | −0.13 dB | 0.13 dB |
| 24 | 73.8 (−13 %) | 272 (−8 %) | −88.1 | 0.00 | — |
| 32 | 85.2 | 297 | −87.7 | +0.001 | 0.002 dB |

**Aliasing does not discriminate.** Every width clears `10` §4's −60 dBFS
acceptance by more than 27 dB, and the modulated and moving-TIME junk is
identical to a tenth of a decibel. So this is *not* a grit question.

**The cost of fewer taps is entirely top end**: droop per repeat, and — under a
moving read — warble, because a narrow kernel's gain at the top depends on the
read phase.

**The CPU curve is not linear.** 12 taps is within 2 % of 8 taps' cost while
giving up a quarter as much top end. If anything is free, it is 12.

## The files

**Blind set** (shuffled, key sealed):
`…/scratchpad/dwell-taps/blind/` — `A-repeats-1..5`, `B-longtail-1..5`,
`C-chorus-1..5`, `D-driven-1..5`. **`KEY.txt` is in the same folder — do not
open it until the answers below are written down.**

Labelled set and the full method: `…/scratchpad/dwell-taps/listening/`, with
`HOW-TO-LISTEN.txt`.

Levels are matched to 0.07 dB or better; nothing clips. Every TIME is
deliberately a fractional number of samples — **at a whole sample all five
render bit-identical**, and the module's own default of 375 ms is exactly
18000 samples at 48 kHz, so an A/B at defaults proves nothing.

## What to listen for

- **A and B** — the tail going dull, repeat by repeat. Ignore the first echo;
  listen to echoes six through fifteen. B is the harder test.
- **C** — the moving read. Listen for a slow shimmer on the pad's sheen that
  should not be there. This is where a narrow kernel gives itself away.
- **D** — driven, where saturation may mask or expose it.

## Answers

*Rank or describe freely; "all the same" is a real and useful answer.*

**A — repeats:**

**B — long tail:**

**C — chorus / moving read:**

**D — driven:**

**Could you tell any of them apart at all?**

**Lowest width you would ship:**

## The decision

**Width chosen:**

**Reasoning:**

**Date, and on what (headphones / monitors):**

## What follows from each answer

- **32 stays** — nothing changes; the module keeps its cost and this note
  records that the width was tested rather than assumed.
- **24 or 16** — a modest win, 8–26 % off Clean, no measurable top-end cost at
  24 and −0.13 dB at 16.
- **12** — the large win: 39 % off Clean bare and 29 % off the heaviest case,
  for −0.77 dB at 18 kHz. Clean would stop being the most expensive character.
- **8** — 40 %, but 2.68 dB of droop and 2.66 dB of warble under a moving read;
  the measurements say this is the one you should be able to hear.

Whatever is chosen, `10` §1 gains the reason the width is what it is — which is
the thing it has never had. The switch is `BMO_DWELL_SINC_TAPS` in
`modules/dwell/dsp/DelayEngine.h`; 32 is in the tree until this says otherwise.
