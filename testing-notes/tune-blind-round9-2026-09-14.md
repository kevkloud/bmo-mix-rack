# Blind listening answers -- round nine: the phrase end

> **Outcome, 2026-09-14: the candidate was not adopted.** It lost two of the
> four groups and `0.2.4` shipped as it stood — **guard 6 is still the build**.
> The implementation exists only on the branch `tune-phrase-end`, on the fork
> `badmixesonly/bmo-mix-rack-333`, and was never merged; `modules/tune/` in
> this tree is unchanged. Read to the end before acting on any section: **Where
> this leaves the mechanism** is the one that decides, and it says what a next
> candidate has to do differently.
>
> One finding here outlives the candidate — the field tool's pop and splice
> counts on the Failure take have never excluded source events, so a metric
> that counts the take's own transients will keep rewarding whatever tracks the
> take least. Worth fixing before the next round is scored.

**Audio in `field-audio/blind-2026-09-14-round9/`** (gitignored, in the main
worktree; open from Explorer). `KEY.txt` sits beside it; don't open it until
this is filled in.

Three letters, four groups, the same four as round eight. Built on
**AURORA**, 2026-09-14, branch `tune-phrase-end` off `review-0.2.4`. One of
the three is Antares, as an anchor; one is the shipped 0.2.4 build (guard 6,
the round-eight winner); one is guard 6 plus one change.

## What changed, and why this is the question

Round eight left one pop standing at **6.137 s** on Failure, and the note
called it "a phrase end where clarity falls to 0.05 and the detector keeps
tracking noise -- the voicing item." The 0.2.4 review read the analysis
dump at that moment and found the mechanism, and it is not voicing.

When clarity drops under the detector's floor the period **freezes** where it
was (spec §3.4), but the detector keeps re-presenting that frozen period to
the correction law every evaluation as if it were new. The law confirms a
note jump by "the next estimate agreeing with it" -- and the previous
estimate, echoed back, always agrees. So at a phrase end, where the last
thing the detector read was a formant lobe, a leap of an octave or more was
confirmed by its own echo: at 0 ms the note zips D4 → C#4 → D5 → A5 → D3
inside 20 ms and the engine splices on each, one of them (6.146 s) the
worst-landing splice in the whole take at 1.95. At 20 ms only the first
splice fires.

**The change:** a frozen period is not an estimate. The detector marks each
estimate `fresh` when its period was measured this evaluation, and the law
ignores the hold: the pitch, the note and the correction stay what they were
until a real measurement arrives or voicing closes. Two lines of arithmetic,
no constant, no latency.

| | 0.2.4 (guard 6) | phrase end |
|---|---:|---:|
| Failure 0 ms -- worst splice landing | **1.95** (6.146 s) | **0.76** |
| Failure 0 ms -- splices over 0.5 | 4 of 33 | 3 of 33 |
| Failure 0 ms -- LOST the period | 4 of 33 | 3 of 33 |
| Failure -- detector jump flips (both speeds) | 7 | 5 |
| Failure -- note-name changes | 197 | 180 |
| Failure -- dropouts | 12 | 12 |
| Failure 20 ms -- splices | 27 | 28 |
| Fuji 0 ms -- worst landing / splices | 1.54 / 11 | 0.79 / 10 |
| Fuji 20 ms -- worst landing / splices | 1.55 / 9 | 0.95 / 8 |
| Fuji -- neighbour flips | 41 | 48 |
| corpus mean gross error (72 items) | 1.9344 % | 1.9344 %, no item worse |
| corpus note changes / splices | 1172 / 195 | 952 / 192 |

The 6.146 s splice no longer happens. The one figure that moved the wrong
way is Fuji's neighbour flips, 41 → 48: with the echo gone, a note that used
to be pinned by its own frozen period during a soft syllable can now be
re-decided when the real measurement comes back. Whether that is heard as
steadier or as busier is what this round is for. One ON NOISE splice on
Failure 20 ms (10.752 s, −45 dB) lands worse, 0.69 → 1.04; it is in a quiet
gap and the field tool's scoring of quiet splices is itself on the review's
list.

**Measurement has been wrong three times about what you would hear**, and
was right once, in round eight. So, as before: the numbers picked the
candidate and you pick the winner. Nothing tells you which letter is which.

## What to listen for

**Phrase ends.** The end of the held D4 before the next phrase on Failure,
6.13–6.16 s, at 0 ms especially: a chirp or zip rather than a click. Any
held-note tail into breath or creak. Then the usual: pops anywhere,
tracking through scoops, anything that sounds processed on held notes.

On Fuji, whether soft syllables inside a word sound steadier or busier than
in round eight.

## Failure 0 ms

Ranking (best to worst):

- A:
- B:
- C:

## Failure 20 ms

Ranking (best to worst):

- A:
- B:
- C:

## Fuji 0 ms

Ranking (best to worst):

- A:
- B:
- C:

## Fuji 20 ms

Ranking (best to worst):

- A:
- B:
- C:

## Anything else

Fewer pops than round eight on any of them? Which?

Are any two indistinguishable? Which?

Anything new or worse at phrase ends?

The single worst thing left:

Listened on: AURORA,

---

## If the candidate wins

It ships as it is on `tune-phrase-end`: `PitchEstimate::fresh` in
`Detector.h`, set once per evaluation in `Detector::evaluate`, and the gate
in `CorrectionLaw::tick`. `tune_correction`'s hand-built estimates are
measurements and default to fresh. The ratchet constants in
`HardTuneTests.cpp` did not move (8 of 8 tune suites green on the branch).
Still open on the same phrase-end mechanism, and deliberately not in this
arm so the round tests one thing: the voicing release counter is zeroed by
any single frame in the 0.60–0.85 band (`Detector.cpp:328`), so a decaying
note whose clarity flickers to 0.61 never closes. That is the next
candidate if this one is heard as better.

---

# Heard, 2026-09-14, on AURORA — the candidate loses, and does not ship

Frosty's rankings, written before `KEY.txt` was opened; the sheet is
`field-audio/blind-2026-09-14-round9/ANSWERS.md`. Decoded:

| group | 1st | 2nd | 3rd |
|---|---|---|---|
| Failure 0 ms | **phrase end** | guard 6 (0.2.4) | Antares |
| Failure 20 ms | **phrase end** = guard 6 | — | Antares |
| Fuji 0 ms | Antares | guard 6 | **phrase end** |
| Fuji 20 ms | guard 6 | Antares | **phrase end** |

**Candidate against the shipped build: one win, one tie, two losses.** It does
not ship, and `tune-phrase-end` does not merge as it stands.

What he said, in his words:

- Failure 0 ms — phrase end "best, one small pop on *surprise*"; guard 6 "2nd,
  multiple pops"; Antares "worst, multiple pops".
- Failure 20 ms — phrase end and guard 6 tied, "small pop on *guess*"; Antares
  "worst" despite "the best *tracking*, no pop on *guess*", because of "two big
  pops toward the end of the sample".
- Fuji 0 ms — Antares "best"; guard 6 "2nd best, more hunting than A"; phrase
  end "**clear worst, missing notes**".
- Fuji 20 ms — guard 6 "best"; Antares "2nd best"; phrase end "**worst, bad**".

## The measurement called this, and was ignored

This file predicted the failure before the round was cut:

> The one figure that moved the wrong way is Fuji's neighbour flips, 41 → 48:
> with the echo gone, a note that used to be pinned by its own frozen period
> during a soft syllable can now be re-decided when the real measurement comes
> back. Whether that is heard as steadier or as busier is what this round is
> for.

Busier, and worse: "missing notes" at 0 ms and "worst, bad" at 20 ms. The
mechanism is right about Failure and wrong about Fuji, and Fuji is the softer
material where the frozen period was doing useful work. Ignoring the hold
everywhere is too blunt — it throws away a pin that a soft syllable needs in
order to fix a confirmation that only a phrase end abuses.

## The "by" correction

After listening, Frosty noticed the pop on **"by"** is in the **dry** Failure
recording — it is in the take, not in any candidate, and is not a pop-testing
event. It does not rescue the candidate:

- On Failure it *helps* the candidate (it removes the "medium pop" from its
  0 ms note and the "big pop" from guard 6's), and the candidate already won
  or tied both Failure groups.
- The two losses are on **Fuji**, a different take, where "by" does not occur.

A crude click check over 6.00-6.30 s of Failure 0 ms agrees that the source
carries the transients there: the dry, guard 6 and the candidate all show a
d2/rms spike at 6.21 s of 1.56, 1.47 and 1.51 respectively — indistinguishable,
and present in the dry. **The field tool's pop and splice counts on the Failure
take have never excluded source events**, which is worth fixing before the next
round is scored: a metric that counts the take's own transients will keep
rewarding whatever tracks the take least.

## Where this leaves the mechanism

Not dead, but not this shape. The next candidate has to stop the echo
confirming a note jump **without** un-pinning a note that a soft syllable is
holding — the obvious form is to apply the `fresh` rule only where the law is
confirming a *jump*, and leave a held note pinned by its frozen period as it is
today. That is a narrower change than the one measured here and it keeps
everything the Failure groups liked.

`0.2.4` ships as it stands: **guard 6 is still the build**, and it beat Antares
in three of the four groups.
