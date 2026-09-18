# Blind listening answers -- round five

What changed since round four: **the correction is no longer late.** The
detector's estimate refers to about one period of the note behind the newest
sample, and the engine reads 4 ms behind it; the gap between those two, times
how fast the singer is moving, was the whole of the error on a moving voice.
The law now carries the estimate forward across that gap. It costs no extra
latency at all -- BMO is exactly as late as it was in round four.

Measured on the reference stimulus, at retune 0:

| | round four | now | Antares |
|---|---:|---:|---:|
| correction lag, mean | 3.19 ms | **0.71 ms** | -0.24 ms |
| correction lag, worst | 6.07 ms | **1.97 ms** | 1.66 ms |
| vibrato residue, mean | 3.35 c | **1.24 c** | 1.30 c |

On these two takes it costs nothing measurable: Failure 44 -> 41 splices at
0 ms and 38 at 20, Fuji 23 -> 22 and 16. Dropouts unchanged on both.

Each group has the dry and three shuffled letters: **Antares, BMO as you heard
it in round four, and BMO now.** The round-four files are the same renders
that were played then, so comparing against your round-four notes is fair.
Don't open `KEY.txt` until every line has an answer.

How to answer: write on each line below, or comment in the Files pane. The
session writes your comments in, then opens the key.

**The audio is in `field-audio/blind-2026-09-12-round5/`** -- four folders,
each with `dry.wav` and `A/B/C.wav`. That folder is gitignored (the material
is not ours to publish and this fork is public), so it may not show in the
sidebar; open it from Explorer or your player. This sheet is the part that
gets committed, the way `testing-notes/deq-blind-answers.md` is.

`KEY.txt` sits beside the audio. Do not open it until every line here has an
answer. The renders it was all built from are in `bmo-tune-rt/field-audio/
shootout-2026-09-11/`, with `blind-manifest-round5.txt`.

## What to listen for

**First, the thing this build changed.** Words and phrases that move --
scoops, runs, the start of a held note after an approach -- should land on the
note sooner, with less of a slide into it. On a held note nothing should have
changed. If anything sounds newly *early*, over-snapped or clipped at the
front of a word, that matters: it is the risk this change carries.

**Second, and more important: the dropouts.** Please say whether these are
what you mean by the word, because the fix depends on it.

What is counted, and what it should sound like: the detector briefly loses
voicing in the middle of a phrase -- 5 to 77 ms -- and while it is lost the
correction fades out over 10 ms and the voice reverts to its own untuned
pitch, then fades back in over a cycle. So it should sound like the tuning
*letting go* for a moment: the note sags to where the singer actually was, and
snaps back. Not a gap in the audio, not silence, not a click. There may also
be a small level bump at the end of one, where the engine slides its read back
to rest.

**This build changes none of it** -- the count is identical before and after,
15 on Failure and 12 on Fuji. It is a voicing problem, not a correction one,
and nothing has been done about it yet.

Where they are, if you want to go straight at them. Same in every letter, and
the same at both speeds -- voicing does not depend on retune speed:

- **Failure**: 2.98, 4.58, 4.65, 6.14, 6.36, 6.55, 6.62, 6.99, 10.03, 10.76,
  12.22, 12.76, 13.18, 13.87, 15.62 s. The clusters at 6.1-7.0 s and
  12.2-13.9 s are the densest.
- **Fuji**: 5.97, 6.33, 9.59, 9.67, 10.43, 10.98, 11.58, 11.62, 15.94, 15.96,
  16.79, 18.63 s.

The long ones are the ones to judge: Failure 6.36 s (77 ms), 4.65 s (75 ms),
12.76 s (71 ms); Fuji 6.33 s (75 ms), 10.98 s (75 ms), 18.63 s (73 ms).
Anything under about 10 ms should be inaudible -- the correction has not
finished fading before voicing comes back.

**Third, whatever is still worst.** Say what would stop you putting it on a
record, whether or not it is on this list.

## Failure 0 ms

Ranking (best to worst): 

- A: best, can tell it's antares
- B: tough to distinguish from C, still same audible pops from earlier testing, slightly worse tracking
- C: tough to distinguish from B, still same audible pops from earlier testing, slightly better tracking

Dropouts -- is that the word, and how bad here? 

## Failure 20 ms

Ranking (best to worst): 

- A: Best, antares
- B: 2nd, same pops, less formant artifacting and better tracking than C
- C: worst, same pops, audible formant shift, worse tracking than B

Dropouts -- is that the word, and how bad here? 

## Fuji 0 ms

Ranking (best to worst): 

- A: Best, smoothest tracking
- B: 2nd best, but close, slightly worse tracking
- C: worst, but close, a few audible tracking issue (more audible corrections)

Dropouts -- is that the word, and how bad here? 

## Fuji 20 ms

Ranking (best to worst): 

- A: 2nd, one slight hiccup toward end, audible tune
- B: best, smoothest
- C: worst, big hiccup toward end, clearly tuned

Dropouts -- is that the word, and how bad here? dropout is not the word, im referring to pops, but they may potentially be cause by dropouts being corrected. needs more testing

## Anything else

The single worst thing left, across all four: Pops on "failure"

Listened on: AURORA, UA Apollo Twin X gen 2, HEDD Type 20 mk2 monitors (Frosty, 2026-09-12)

---

## What the key said

| group | A | B | C | order heard |
|---|---|---|---|---|
| Failure 0 ms | Antares | round four | **BMO now** | Antares > BMO now > round four |
| Failure 20 ms | Antares | round four | **BMO now** | Antares > round four > **BMO now** |
| Fuji 0 ms | round four | **BMO now** | Antares | round four > **BMO now** > Antares |
| Fuji 20 ms | **BMO now** | Antares | round four | Antares > **BMO now** > round four |

**The prediction is two-all against the build it replaced, not the win the
measurements predicted.** Better on Failure 0 ms (by a margin Frosty called
hard to distinguish) and clearly better on Fuji 20 ms (a "big hiccup toward
end" became a "slight" one). Worse on Fuji 0 ms (slightly), and worse on
Failure 20 ms with a complaint that is new to this build: **"audible formant
shift"**, plus worse tracking.

Measured, the same change took the correction lag from 3.19 to 0.71 ms mean
and the vibrato residue from 3.35 to 1.24 cents. None of that reliably
reached the ear, and on one group it went backwards. It is not earned yet.

Both BMOs still beat Antares on Fuji 0 ms. Antares won both Failure groups
and Fuji 20 ms.

## Pops are not dropouts

Frosty, on the word: *"dropout is not the word, im referring to pops, but they
may potentially be cause by dropouts being corrected. needs more testing."*
The single worst thing left, across all four: **pops on Failure.**

So every "dropout" figure in these notes -- 15 on Failure, 12 on Fuji -- counts
something Frosty was not talking about: the detector briefly losing voicing.
The thing that matters is the **splices**, which is what the engine calls its
whole-period jumps and what a listener calls a pop.

**Tested, and the two are mostly unrelated.** Failure at retune 20 ms, 38
splices against 15 voicing gaps: only 4 splices fall within 100 ms of a gap
(2.89, 6.13, 7.02, 10.74 s). The rest are 0.1 to 2.7 seconds away. Correcting
the voicing dropouts would leave at least 34 of 38 pops exactly where they
are.

## What the pops actually split into

Of the 38 splices on Failure at 20 ms:

- **15 sit within 50 ms of a note change of seven semitones or more** -- the
  detector jumping an octave or a twelfth, so the engine splices by a period
  that is not the singer's. 16.16 and 16.17 s are the clearest: two splices
  10 ms apart, across a 21-semitone note change. This is the population
  `modules/tune/AGENTS.md` already suspected ("each a splice by a wrong
  period").
- **23 have no such change anywhere near them.** These are the sustained-
  correction splices: the mean pull the engine was holding when a splice
  fired is 55.6 cents against 40.6 everywhere else, and on this take the
  singer sits on D# in D major, permanently about 50 cents from either
  allowed note. The read drifts at the correction rate and jumps a period
  whenever it runs out of window, at |1 - rho| x fs / T per second, which no
  amount of window widening changes.

Two different faults needing two different fixes, and the larger half is the
one the notes have not been chasing.

---

## The pops, timestamped by ear (2026-09-12, AURORA)

Frosty in Ableton, on `field-audio/pop-hunt-2026-09-12/`: the pops "most
audible that I catch every pass", read cold without the session's list.

| heard | round four | BMO now |
|---:|---|---|
| 1.560 | splice 1.50 s (61 ms) | splice 1.50 s (58 ms) |
| 2.850 | splice 2.89 s (42 ms) | splice 2.89 s (42 ms) |
| 6.080 | splice 6.13 s (55 ms) | splice 6.13 s (55 ms) |
| 7.002 | splice 7.02 s (16 ms) | splice 7.02 s (16 ms) |
| **14.012** | **splice 14.02 s (6 ms)** | **none -- nearest 443 ms** |
| 14.438 | splice 14.47 s (35 ms) | splice 14.45 s (17 ms) |
| 17.360 | splice 17.41 s (49 ms) | splice 17.41 s (49 ms) |

**Every audible pop is a splice.** Seven of seven, six within 61 ms. There is
no second mechanism to go looking for, and the splice counter is measuring
the right event.

**But only seven of thirty-eight splices are audible.** That is the number
that matters, and it was not being measured: the count has been the target
all along, and four splices in five make no sound. Driving the count down is
not the same work as driving the pops down, and round four is the proof --
120 splices to 38, and Frosty still said "still too many audible pops".

**The prediction removed one of the seven.** 14.012 is a splice in round four
and is simply not there now. That is a real win the blind test did not
surface, because at 14.4 there is another one right behind it.

Small caveat to settle: Frosty recalls 2.850 and 6.080 as round-four-only,
but both builds splice at 2.89 and 6.13. Either the splices survive and only
their audibility changed, or the recollection slipped. Worth one check.

### Which splices are the audible ones

Within +/- 70 ms of a heard pop:

| | splices | heard | hit rate |
|---|---:|---:|---:|
| wrong-period (a 7+ semitone note change within 50 ms) | 15 | 5 | 33 % |
| sustained-correction drift | 23 | 3 | 13 % |

The wrong-period ones are about two and a half times likelier to be heard,
which is the mechanism one would predict: a jump by the singer's true period
lands in phase and the half-period crossfade hides it, while a jump by an
octave-wrong period lands out of phase and steps the waveform. On five
against three this is a direction, not a proof, and it should not be leaned
on harder than that.

**It does reverse the steer given earlier in this file.** The larger
population -- the 23 drift splices -- is the quieter one. The 15 the notes
already suspected are where the noise is, and they are the detector's octave
and twelfth errors on scoops.
