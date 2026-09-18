# Blind listening answers -- round six

**The audio is in `field-audio/blind-2026-09-12-round6/`** (gitignored, open
it from Explorer). `KEY.txt` sits beside it -- do not open it until every
line here has an answer. This sheet is the part that gets committed.

**Four letters this time, not three.** Antares, the BMO you heard on
2026-09-11, and two versions of the current build.

## What changed, and why

Your seven timestamps decided this round. All seven landed on a splice, which
settled that there is no second mechanism -- but only seven of thirty-eight
splices were audible, so the **count** was never the thing. What separates a
pop from a silent splice is how well the jump *lands*.

A splice jumps the read by a whole detected period, on the assumption that
one cycle away is the same waveform. It is, when the period is right. A few
per cent out -- a scoop, a jittery note -- and the landing is out of phase,
the crossfade joins two points that are not the same point, and the waveform
steps. So the period now only proposes: the engine matches the waveform
either side and jumps where it actually repeats.

Measured, worst landing error over a run of repeated splices with the period
deliberately wrong:

| period | before | now |
|---|---:|---:|
| correct | 3.1e-10 | 3.1e-10 |
| 3 % too long | 0.325 | **0.00033** |
| 3 % too short | 0.326 | **0.00026** |

On your take, landing error on pitched material at 20 ms: mean 0.36 to 0.31,
worst 1.97 to 1.79. And on the six pops you timed, one large win and one
loss: **2.893 s, the worst splice in the whole take, goes 1.97 to 0.37**;
7.019 goes 0.68 to 1.01. The other four move by less than 0.1.

An octave-wrong period still cannot be rescued this way, and that is the
population your pops mostly sit in -- 15 of 38 splices are within 50 ms of a
note change of seven semitones or more. That is the detector's to fix and it
is next.

## The fourth letter

One arm has a **1 ms note transition**: the target slews to a new note
instead of stepping to it. You have never had this -- the target has always
stepped, 341 times in 19 seconds on your take, and each step is an instant
step in the resampling ratio and so in the formants. It is the standing
suspect for the "audible formant shift" you heard last round and the
"transition steps on faster words" from round one. Waves has this as a knob
of its own and will not go below 0.1 ms.

The numbers cannot judge it: smoothing the transition makes splices *worse*
(10 ms nearly doubles them), and at 1 ms it is free but buys nothing
measurable. Formant smoothness is not splice count. **This is the one thing
in the set that only your ears can settle.**

## What to listen for

1. **Pops, against round four.** Same take, same places. Fewer? Softer? Your
   2.85 s one should be markedly better; 7.00 s may be worse.
2. **Formants and transitions**, between the two current arms. One steps to
   each new note, one takes a millisecond. On fast words especially.
3. **Anything that got worse.** The splice change moves where the read lands,
   which is the whole signal path -- if something is duller, grainier or
   less present, that matters more than the pops.

## Failure 0 ms

Ranking (best to worst): 

- A: 2nd worst same pops as before
- B: best, fewer/softer pops, some audible tracking
- C: 2nd best same pops as before
- D: worst, most pops

Pops -- fewer or softer than before? only on B, same or worse on others

## Failure 20 ms

Ranking (best to worst): 

- A: tied with B
- B: tied with A
- C: Best no noticeable pops
- D: worst

Pops -- fewer or softer than before? 

## Fuji 0 ms

Ranking (best to worst): 

- A: Indistinct from B
- B: indistinct from A
- C: worst, one big pop toward end
- D: 2nd worst, some small/soft pops

Pops -- fewer or softer than before? 

## Fuji 20 ms

Ranking (best to worst): 

- A: 2nd best
- B: Best
- C: worst, bad tracking on held notes
- D: 3rd, better tracking than A&C but small pops

Pops -- fewer or softer than before? 

## Anything else

Did any two letters differ in formants or transition smoothness, and which
did you prefer? 

The single worst thing left, across all four: 

Listened on: AURORA, UA Apollo Twin X gen 2, HEDD Type 20 mk2 monitors (Frosty, 2026-09-13)

---

## What the key said

| group | A | B | C | D |
|---|---|---|---|---|
| Failure 0 ms | **BMO now** | Antares | round four | BMO now + 1 ms |
| Failure 20 ms | round four | **BMO now** | Antares | BMO now + 1 ms |
| Fuji 0 ms | round four | Antares | BMO now + 1 ms | **BMO now** |
| Fuji 20 ms | **BMO now** | round four | BMO now + 1 ms | Antares |

Heard, in order:

| group | order |
|---|---|
| Failure 0 ms | Antares > round four > **BMO now** > +1 ms |
| Failure 20 ms | Antares > (round four = **BMO now**) > +1 ms |
| Fuji 0 ms | (round four = Antares) > **BMO now** > +1 ms |
| Fuji 20 ms | round four > **BMO now** > Antares > +1 ms |

## Both changes failed

**The splice landing fix: 0 wins, 1 tie, 3 losses** against the build it
replaces. "Same pops as before" on Failure, and worse than round four on both
Fuji groups. It is not earned and it should come out.

**The 1 ms note transition: worst of four, in all four groups.** No ambiguity
at all, and it kills the formant hypothesis outright -- smoothing the note
transition does not trade formant smoothness against splices, it is simply
worse. It also drew the only "bad tracking on held notes" of the round.

## The metric was wrong again, in the same way

The landing error said this change was an improvement: mean 0.36 -> 0.31 on
Failure at 20 ms, worst 1.97 -> 1.79, and on Fuji 0.74 -> 0.68 and 0.56 ->
0.48. The ear says no change on Failure and a loss on Fuji. **The measure
moved the right way and the sound moved the wrong way.**

That is the second measure to fail this exact test in two days. The splice
COUNT failed it first -- 120 to 38 with no audible change -- which is what the
landing error was built to replace. Neither predicts what Frosty hears.

What is left standing, and it is not nothing: every pop is a splice (seven of
seven timestamped), and only some splices are audible. What separates them is
still unknown, and two plausible answers have now been measured and found not
to be it. The next attempt should not be a third guess dressed as a metric --
it should start from the four timestamps whose splices did NOT improve and ask
what is physically different about them.
