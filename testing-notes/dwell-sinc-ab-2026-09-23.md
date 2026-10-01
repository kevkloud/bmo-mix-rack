# BMO Dwell — the sinc A/B: what to decide, and somewhere to write it

**Prepared on AURORA, 2026-09-23. Answered 2026-10-01: 24 taps** (see the
decision at the end). It was written as a form for Frosty to listen and fill
in; the answers and the decision are below.

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

*Frosty's words, written before the key was opened.*

**A — repeats:** I can't honestly point out a difference.

**B — long tail:** Again, can't honestly point at a difference.

**C — chorus / moving read:** 4 stood out as slightly nicer, couldn't say why.

**D — driven:** 3 felt like it cut short, maybe measure and confirm this in
case I'm hallucinating.

**Could you tell any of them apart at all?** D - Driven 3 and C - chorus 4.
Chorus 4 for positive reasons, driven 3 negative.

**Lowest width you would ship:** Lowest should be passing a mono centered
signal unchanged.

## The key, opened after the answers

| scene | 1 | 2 | 3 | 4 | 5 |
|---|---|---|---|---|---|
| A-repeats | 16 | 8 | 32 | 24 | 12 |
| B-longtail | 24 | 8 | 32 | 12 | 16 |
| C-chorus | 32 | 12 | 8 | **16** | 24 |
| D-driven | 8 | 12 | **16** | 32 | 24 |

**Both files that stood out were 16 taps**, one heard as nicer and one as
worse. The same width going opposite ways in two scenes, with A and B heard as
identical, reads as chance, not as something the width does. Each blind file
was checked against its labelled original by SHA-256 before the measurement
below, so the key is the files that were heard.

### "Driven 3 cut short", measured

Measured on AURORA, 2026-10-01, with a scratch reader (not in the repo) over
the labelled renders. It takes 250 ms windows of the mid channel, full band
and a first-difference high band. For D at 16 taps against 32:

- **Envelope:** identical to 0.1 dB in every window from 4 s to the end of the
  file (−19.9 down to −50.1 dBFS on both). The high band is within 0.1 dB the
  same way.
- **Tail length:** above −60 dBFS for the full 8 s at every width. Nothing
  ends early.
- **Residual against 32:** −34.9 dB relative to the signal. 24 taps is −43.5,
  12 is −31.1 and 8 is −26.9, so 16 sits where its width says it should.

**So D-driven-3 does not cut short.** Whatever stood out was not in its level
or its length. That matches the 16-tap pad standing out for the opposite
reason.

Also measured: **the side channel is digital silence in all twenty files**
(L = R exactly, −240 dB). A centred mono source stays centred mono through
Clean at every width; the kernel is shared, so no width can pull the image off
centre. That part of the criterion does not discriminate between widths. What
does is "unchanged", below.

## The decision

**Width chosen:** **24 taps.**

**Reasoning:** Nothing in the set could be told apart reliably, so the ear
does not set a floor. Frosty's rule does: the narrowest width shipped must
still pass a centred mono signal unchanged. On the 2026-09-22 measurements
(`414897c`) that is 24, at 0.00 dB at 18 kHz, against 16's −0.13 dB and
12's −0.77. 32 adds nothing that can be measured or heard over 24 (+0.001 dB).
24 costs 13 % less on Clean bare (73.8 against 85.2 ms per 10 s) and 8 % less at
the heaviest (272 against 297).

Taken by Claude from Frosty's answers, at his request ("please use these
answers to decide the width"). Reading his last answer as "the lowest width
that is still transparent" is an interpretation; if he meant something else,
this is the line to change.

**Date, and on what (headphones / monitors):** Oct 1 2026, headphones, on
AURORA.

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
`modules/dwell/dsp/DelayEngine.h`; it is 24 since 2026-10-01.
