# Blind listening answers -- round seven: which rest sounds best

**Audio in `field-audio/blind-2026-09-13-round7/`** (gitignored -- open from
Explorer). `KEY.txt` sits beside it; don't open it until this is filled in.

Four letters, four groups. **Every arm is the same build.** The only thing
that differs is the rest -- how far behind the input the engine reads.

## What changed, and why this is the question

You said: *"I'd rather nail correction and then hit a latency wall, than
minimize latency and cap how good correction can sound."* That reversed what
this set was for. The first version asked how LOW the rest could go. This one
asks which rest sounds BEST, and it spans the deeper direction that had been
ruled out on latency grounds without ever being listened to.

Following that instruction immediately found a half-finished law rather than
a trade-off. The correction is carried from where the detector's estimate
refers to, to where the engine reads -- and that gap points **forward** when
the engine reads newer material than the estimate describes, **backward** when
it rests past it. The law only ever did the forward half. That is not an
exotic case: it is the ordinary one above about 280 Hz at today's rest, and
the ordinary one everywhere at a deeper one. Both halves now work.

That is what had made a deeper rest look bad. The vibrato residue used to
bottom out at 6 ms and climb after -- not because the rest was failing, but
because more and more of the range fell on the side the law refused to
correct.

| rest | true latency | correction lag | vibrato residue | Failure splices | Fuji splices |
|---|---:|---:|---:|---:|---:|
| 2 ms | 7.55 ms | 1.38 ms | 1.89 c | 49 | 33 |
| **4 ms (ships today)** | 10.26 | 0.71 | 1.24 c | 36 | 22 |
| **6 ms** | 11.92 | **0.07** | **0.94 c** | **31** | **10** |
| 8 ms | 13.74 | −0.51 | 1.05 c | 31 | 11 |

By measurement 6 ms is the best correction available: the lag is essentially
zero, the residue beats Antares' 1.30 for the first time by a clear margin,
and Fuji's splices more than halve. It costs 1.7 ms of latency.

**But measurement has been wrong twice about what you would hear** -- splice
count and splice landing error both improved while the sound did not. So the
numbers pick the candidates and you pick the winner.

## What to listen for

**Which one sounds best. That is the whole question.** Latency is not being
traded here; it is written down above and it is yours to accept or refuse
after you know which one wins.

Splices especially, since you've called them the biggest enemy -- the deeper
arms should have materially fewer, and Fuji's should more than halve. But also
tracking, formants, and anything that sounds processed on held notes.

You will not feel the latency in these files; they are aligned so only the
tuning compares. That is deliberate: decide what sounds right first, then we
find out whether its latency is liveable.

## Failure 0 ms

Ranking (best to worst): 

- A: 
- B: 
- C: 
- D: 

## Failure 20 ms

Ranking (best to worst): 

- A: 
- B: 
- C: 
- D: 

## Fuji 0 ms

Ranking (best to worst): 

- A: 
- B: 
- C: 
- D: 

## Fuji 20 ms

Ranking (best to worst): 

- A: 
- B: 
- C: 
- D: 

## Anything else

Are any two indistinguishable? Which? 

Fewer pops on any of them than you are used to? 

The single worst thing left: 

Listened on: AURORA, 

---

## Answered, 2026-09-13

Frosty, on all four groups at once rather than per letter:

> **all provided options have audible pops at an unacceptable level**
> **almost indistinguishable from one another**

No ranking was given, and none was needed: the four arms were not
distinguishable, so there is nothing to rank.

## What the key said

| group | A | B | C | D |
|---|---|---|---|---|
| Failure 0 ms | 8 ms | 4 ms | 2 ms | 6 ms |
| Failure 20 ms | 4 ms | 6 ms | 2 ms | 8 ms |
| Fuji 0 ms | 2 ms | 6 ms | 4 ms | 8 ms |
| Fuji 20 ms | 6 ms | 4 ms | 2 ms | 8 ms |

## What that settles

**The rest is not what makes a pop.** Across those four arms the splice count
runs 49 / 36 / 31 / 31 on Failure and 33 / 22 / 10 / 11 on Fuji -- a fourfold
change in window room, better than a threefold change in splice count on Fuji
-- and they are indistinguishable. That closes the axis the last three rounds
have been spent on.

It also closes the case for the deeper rest on correction grounds. 6 ms
measures best (residue 0.94 c against 1.24 at 4 ms, Fuji's splices halved),
but if the difference is inaudible there is no reason to spend 1.7 ms of
latency on it. **The rest stays at 4 ms.**

## What it opened

Five of the six pops Frosty timestamped on 2026-09-12 fire at the SAME
MILLISECOND at every rest -- 42, 55, 16, 17 and 49 ms from his marks, constant
across the whole sweep. Something fires them at a fixed instant, and it is
the detector:

| splice | detector f0 over the 40 ms before | ratio |
|---|---|---:|
| audible, 2.893 s | 166 -> 594 Hz | **3.58** |
| audible, 6.135 s | 281 -> 558 Hz | **1.98** |
| audible, 7.019 s | 286 -> 504 Hz | **1.76** |
| audible, 14.455 s | 186 -> 734 Hz | **3.94** |
| audible, 17.410 s | 219 -> 809 Hz | **3.69** |
| quiet, 0.300 s | 300 -> 306 Hz | 1.02 |
| quiet, 2.616 s | 168 -> 169 Hz | 1.00 |
| quiet, 5.027 s | 182 -> 182 Hz | 1.00 |
| quiet, 11.079 s | 182 -> 182 Hz | 1.00 |

Six against six, cleanly separated, first try -- where the splice count and
the splice landing error were each refuted against these same ears. **The
audible pops are the detector losing the period and the engine splicing on a
period that is not the singer's.** `bmo-tune-field` reports it now: 12 of 37
on Failure at 20 ms, 4 of 16 on Fuji.
