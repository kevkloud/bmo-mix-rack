# The field tool was scoring silence — and it moved the target

Found and fixed on **AURORA**, 2026-09-14, on branch `field-quiet-splices` off
`review-0.2.4`. Tool change only; no DSP is touched and no verdict is revisited
here. 15 of 15 DSP suites green.

## What started it

After round nine, Frosty noticed the pop on "by" is in the **dry** Failure
take, and asked whether the scoring had been counting the take's own events as
the engine's. The first answer given was too broad and was wrong: the tool
already gates landing error on whether the ruler calls the dry **periodic**
there, and that gate was added for exactly this reason (a comment in
`tools/tune/field/main.cpp` records Frosty's timestamps catching it in
September). Aperiodic source events were already excluded.

The narrower answer is the real one, and it was already on the 0.2.4 review's
list as "the field tool's scoring of quiet splices".

## The bug

Landing error is normalised by the two reads' own RMS. It measures the step
**relative to the waveform it sits in**, which is the right way to ask "did
this jump land in phase" and says nothing at all about whether that waveform is
loud enough to hear.

So a splice in a phrase gap, where the take is 30 dB under the voice, can land
at 1.26 — and 1.26 was **the worst landing on the whole of the Failure take**.

    11.742 s   landing 1.26   -54.2 dB   on pitch

Periodic, so the existing gate passed it. Inaudible, so it should never have
been the number anyone read.

## The fix

A second split, by level, against the take's own median voiced level rather
than full scale so it travels to a quietly recorded take:

    ON PITCH             periodic and within 25 dB of the voice -- the only
                         column that predicts a pop
    on pitch, in a gap   periodic but under that
    on noise             the ruler finds no pitch there

Nothing is dropped; quiet splices are still counted and still listed, they are
just not allowed to set the headline. The per-splice listing and the statistics
now share one `levelAt` lambda, so they cannot disagree about how loud a moment
was.

`kQuietBelowVoiceDb` is 25 dB and is not a knife edge on this material: the
Failure take's splices run -14.4 to -27.9 dB and then one at -54.2, so the
threshold at -48.5 sits in a 26 dB gap with nothing near it.

## What it changes, which is more than a tidy-up

Shipped guard 6, the 0.2.4 build, on the round-nine dry files:

| take | before | after |
|---|---|---|
| Failure | ON PITCH worst **1.26**, over 0.5: **1 of 19** | ON PITCH worst **0.49**, over 0.5: **0 of 18** (+ one in a gap) |
| Fuji | — | ON PITCH worst **1.52**, over 0.5: **5 of 11**, none in a gap |

**Failure has no audible bad landing at all**, and Fuji has five. The two takes
had looked comparable; they are not, and the difference was hidden by one
inaudible splice sitting at the top of Failure's column.

That is worth sitting with. Rounds three through nine were largely spent
driving down a Failure figure that was mostly this one splice, on the take that
turns out to be the *healthy* one — while Fuji, with five genuinely loud bad
landings, was the take the ear kept complaining about. Round nine's own verdict
fits: the candidate improved Failure and was ranked **last on both Fuji
groups**, and Frosty's words for Fuji were "missing notes" and "worst, bad".

## What this does not do

- **It does not revisit any verdict.** Guard 6 is still the shipped build and
  the phrase-end candidate is still rejected; this changes which numbers to
  believe next time, not what was heard last time.
- **It does not re-score the corpus.** `score-corpus.sh` and
  `bmo-tune-score` are untouched and may have their own version of this
  problem; worth a look before the next round.
- **It does not choose a next candidate.** But it does say where to point one:
  Fuji, and the five splices over 0.5 that are on pitch and in the voice.
