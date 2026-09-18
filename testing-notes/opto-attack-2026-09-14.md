# BMO Opto — attack at heavy reduction, measured

> **Outcome, 2026-09-14: nothing here was adopted.** Three blind rounds could
> not distinguish either candidate from the shipped build, and the 10 ms attack
> stays in both cells. The implementation of candidate B exists only on the
> unmerged branch `opto-attack-b`; `modules/opto/` in this tree is unchanged.
> Read to the end before acting on any section — "What is implemented here"
> describes that branch, not this one. The last section is the one that
> decides.

Written on **AURORA**, 2026-09-14, for the `opto-high-gr` workflow in
`WORKFLOWS.md`. Frosty asked, before the 0.2.4 listening pass, about "attack
times on heavy gain reduction of 10 dB plus". This is what the shipped cells
actually do, measured on the shipped `modules/opto/dsp/Detector.h` at
`cbd0939`, and two candidates rendered for his ears. Nothing here is a change
to the tree.

## Method

A step: 1 kHz at −50 dBFS for 1 s, then −6 / −12 / −18 dBFS for 1.5 s, then
−50 again. 48 kHz, mono, 512-sample blocks, LINK off, LEVEL 0, Tele's drive on
(it has no off switch), Stressed's COLOR off. Reduction is dry minus wet in
0.5 ms RMS windows. The harness is a 150-line C++ file that includes the real
`DspCore.h`; it lives in this session's scratchpad and is worth lifting into
`tools/measure/opto` as an `attack` command when that tool is next opened.

## What ships

GR in dB reached N ms after the step. `t63`/`t90` are ms to reach that
fraction of the settled reduction. `leak` is how far the peak of the first
2 ms of output sits above the settled output peak.

| mode | CRUSH | in | final GR | 1 ms | 2 ms | 5 ms | 10 ms | 20 ms | 50 ms | t63 | t90 | leak |
|---|---|---|---|---|---|---|---|---|---|---|---|---|
| Tele | 50 | −12 | 7.7 | −0.1 | 0.1 | 2.9 | 5.2 | 6.5 | 7.1 | 9 | 32 | 7.8 |
| Tele | 75 | −12 | 12.3 | 1.3 | 4.1 | 8.1 | 10.2 | 11.2 | 11.7 | 5 | 17 | 12.4 |
| Tele | 100 | −12 | 17.2 | 6.6 | 9.9 | 13.8 | 15.6 | 16.3 | 16.7 | 3 | 10 | 17.2 |
| Tele | 75 | −18 | 8.5 | 0.1 | 0.9 | 4.0 | 6.2 | 7.4 | 8.0 | 8 | 27 | 8.5 |
| Stressed | 50 | −12 | 9.7 | 0.0 | 0.0 | 0.5 | 3.3 | 6.1 | 8.4 | 21 | 66 | 9.7 |
| Stressed | 75 | −12 | 16.5 | 0.0 | 1.4 | 6.4 | 10.0 | 12.9 | 15.2 | 11 | 42 | 16.5 |
| Stressed | 100 | −12 | 23.3 | 4.1 | 7.9 | 13.2 | 16.8 | 19.7 | 22.0 | 7 | 31 | 23.3 |
| Stressed | 75 | −18 | 11.1 | 0.0 | 0.0 | 1.4 | 4.7 | 7.5 | 9.7 | 18 | 59 | 11.1 |

Three things follow, and the first two are what the ear will hear.

1. **The first millisecond or two of every onset passes at unity gain,
   whatever the reduction.** `leak` equals the final GR in every row: a
   one-pole with a 10 ms time constant has done nothing by the first peak.
   At CRUSH 75 on a −12 dBFS peak that is a 12 dB (Tele) or 16 dB (Stressed)
   spike on the front of each word relative to the settled level. On a vocal
   it reads as a tick or a spit on hard consonants. Every compressor without
   lookahead does this; the LA-2A does it; it is louder here because CRUSH
   at 75 is deep. Only lookahead removes it, and Opto declares zero latency.

2. **The reduction arrives slowly at the drive a vocal actually sees, and
   Stressed is slower than Tele.** Both cells have the same 10 ms attack
   constant, but Tele is a feedback loop, which makes its effective time
   constant `tau / (1 + slope)` = 3.3 ms at 3:1, while Stressed is
   feedforward and gets the full 10 ms. So at CRUSH 50 on a −12 dBFS peak,
   Tele reaches 63 % of its reduction in 9 ms and Stressed in 21 ms; 90 %
   takes 32 ms and 66 ms. The checklist describes Stressed as "grabbier";
   on attack it is the opposite. Both real units are faster than this at
   the level they are usually hit at.

3. **The reduction keeps creeping for hundreds of milliseconds.** The
   detector rectifies and compares each sample to the envelope, so the
   envelope only rises on the crests of each cycle and sags between them
   at the release rate; as `chargeDb` builds over 300 ms the release
   lengthens, the sag shrinks, and the envelope creeps up. That is why the
   50 ms column is still short of the final figure in every row. In Tele
   it is the T4 cell's character; in Stressed it is a side effect.

The release, read from 20 ms after the hold ends (past a small DC blip from
Tele's drive stage, see below), matches the 0.2.1 handoff: Tele at CRUSH 50
is half released in 0.33 s and 90 % in 0.74 s; at CRUSH 100 it holds 50 %
for 1.1 s. Stressed at CRUSH 75 and above still holds more than half its
reduction 2 s after a 1.5 s hit at −12 dBFS, which is the dosage memory the
handoff describes and is by design.

**A DC blip, for the record, not for action.** Tele's drive stage adds an
even-order term and DC-blocks it at 20 Hz. When a loud passage ends, the
blocker's stored offset comes out as an 8 ms sub-20 Hz transient around
−56 dBFS. Inaudible; it only matters because it makes a naive release
measurement in the first 20 ms read wrong.

## Two candidates, rendered for the ear

Neither is in the tree. Both are one-line changes to `Detector.h`, patched
into scratchpad copies and measured with the same harness.

- **Candidate A — Stressed's attack constant 10 ms → 3 ms.** Tele untouched.
  The Distressor is a VCA unit whose attack is a knob; its Opto setting is
  about release. Stressed at CRUSH 50 / −12 then reaches 63 % in 6 ms and
  90 % in 20 ms (was 21 / 66), and matches Tele's speed at every setting.
  Leak unchanged.
- **Candidate B — both cells get a light-dependent attack**: the time
  constant is `10 ms / (1 + overdrive / 10 dB)` with a 1 ms floor, where
  overdrive is how far the rectified sample sits above the envelope. A CdS
  photocell does respond faster the harder it is lit, so this is the
  physically defensible version for Tele. Tele at CRUSH 75 / −12 reaches
  63 % in 2 ms and 90 % in 10 ms (was 5 / 17); at CRUSH 100 the leak drops
  17.2 → 14.5 dB. Stressed moves about half as far as under A. At CRUSH 35
  nothing changes, because there is little overdrive.

| | Tele 75 / −12 t63 / t90 | Stressed 50 / −12 t63 / t90 | Stressed 75 / −12 t63 / t90 |
|---|---|---|---|
| shipped | 5 / 17 ms | 21 / 66 ms | 11 / 42 ms |
| candidate A | 5 / 17 | 6 / 20 | 3.5 / 12.5 |
| candidate B | 2 / 10 | 11.5 / 50 | 5.5 / 29.5 |

**Blind set:** `field-audio/opto-attack-2026-09-14/` in the main worktree
(gitignored). Two groups, Tele 75 and Stressed 75, three letters each, the
Failure take through each variant, LINK on, COLOR off, RMS-matched to the
dry. `ANSWERS.md` is the form, `KEY.txt` the decode. Max reduction on that
take is 16.3 dB in Tele and 20.8 to 22.4 dB in Stressed, so this is the
"10 dB plus" case Frosty asked about.

## What this does not settle

Whether any of it should change is Frosty's call, on those renders. The
notes say the 10 ms attack has "no source supporting it moving"; candidate B
is the case that a photocell's attack does move with light, and candidate A
is the case that the Distressor's does not need to be slow at all. If a
candidate wins, it goes in as a constant (A) or a five-line change (B) in
`Detector.h`, with `testAttackReachesReductionInTime`-style absolute
assertions on the table above, and the thirteen preset levels re-solved
with `BMO_PRINT_PRESET_LEVELS` because a faster attack takes slightly more
average gain.

---

# Heard, 2026-09-14, on AURORA — candidate B wins, and the control fired

Frosty's rankings, written before `KEY.txt` was opened; the sheet is
`field-audio/opto-attack-2026-09-14/ANSWERS.md`. Decoded:

| group | 1st | 2nd | 3rd |
|---|---|---|---|
| Tele 75 | **candidate B** "best, slight advantage" | candidate A "almost best, tie" | shipped "slight pumping" |
| Stressed 75 | **candidate B** "best" | shipped = candidate A, tied | — |

**Candidate B placed first in both groups.** Candidate A never beat the shipped
build anywhere it differs from it.

## The Tele group is a null, and it says so itself

Candidate A changes the Distressor cell only, so in **Tele** it is the shipped
build. The two files were rendered separately and are byte-identical:

    Tele 75/B.wav  4034f625b1f48e2e89d3e848c51c39d2b2fa4620f6210c22bdea74c3da6d7e29
    Tele 75/C.wav  4034f625b1f48e2e89d3e848c51c39d2b2fa4620f6210c22bdea74c3da6d7e29

They were ranked 3rd ("slight pumping") and 2nd ("almost best, tie"). One file,
two placings, one position apart — which is the same margin that separated
candidate B from them. **So the Tele separation is at or below the resolution
of this comparison and cannot carry a decision.** That is the control working
exactly as a control is supposed to: it did not tell us the listener is
unreliable, it told us this particular difference is too small to rank, which
is a fact about the change and not about the ear.

## The Stressed group is real

Three genuinely different files, and the ranking is 1 / 2 = 2:

- **candidate B best** — the light-dependent attack, both cells.
- **shipped and candidate A tied for second.** Candidate A is the Distressor
  attack at 3 ms against the shipped 10 ms, which is the larger change of the
  two on paper (t63 11 ms → 3.5 ms, t90 42 ms → 12.5 ms). Heard flat against
  the shipped build.

That last line is the finding worth keeping: **the big simple number did
nothing and the small programme-dependent one was heard.** Candidate A makes
the cell uniformly faster; candidate B is fast only on the first millisecond of
a hit and back to 10 ms as it settles. What was audible was not "faster", it
was "faster *at the onset only*".

## Recommendation

**Do not ship candidate B yet, and do not merge it into `integration`.** Round
three decides it.

The case as it stands is one clean separation at p = 1/6 and one null at a
setting where there is measurably more to hear, with nothing to explain the
difference. A one-in-six result with no supporting story is not enough to
change how the module sounds, and the argument that previously made it look
like more than that has been withdrawn above.

Nothing here is lost by waiting. The branch keeps the implementation, the
renders exist, and round three is already cut and costs Frosty two groups of
four. If Stressed 50 separates the same way again, that is 1 in 36 across two
rounds and it goes in with the preset re-solve behind it. If it does not, the
shipped attack stays and this branch is deleted.

The rest of the 0.2.4 merge does not wait on any of this: `opto-attack-b` is
the only branch holding an undecided change, and it is not in the merge set.

## Still not done

- Absolute t63/t90 assertions on `OptoDspTests`. The suite passed unchanged
  with the attack rewritten, which means it never pinned the attack at all.
- **The thirteen preset levels have not been re-solved.** A faster attack takes
  slightly more average gain, so preset levels on this branch are a little off.

---

# Round three heard, 2026-09-14 on AURORA — candidate B is rejected

Frosty's rankings, written before `KEY.txt` was opened, all four letters
answered in both groups this time.

## Stressed 75

| rank | letter | what it was |
|---|---|---|
| 1 | A | **candidate B** #2 |
| 2 | D | shipped 0.2.4 #2 |
| =3 | B | shipped 0.2.4 #1 |
| =3 | C | **candidate B** #1 |

## Stressed 50

| rank | letter | what it was |
|---|---|---|
| 1 | A | shipped 0.2.4 #1 |
| 2 | B | **candidate B** #2 |
| =3 | C | **candidate B** #1 |
| =3 | D | shipped 0.2.4 #2 |

## Both groups interleave, so both are null

In each group, each variant has one copy near the top and one copy at the
bottom. Neither duplicate pair groups. Under the criterion the design was
built on — both copies of one variant in the top two — **neither group
qualifies, and it is not close.**

Round two's clean separation at Stressed 50 did not repeat. It reversed: there
candidate B took ranks 1 and 2, here the shipped build takes rank 1 and
candidate B's two copies land 2nd and tied-last. Two rounds of the same
comparison on the same material giving opposite orders is what chance looks
like, and a p = 1/6 result failing to repeat is the ordinary outcome of a
p = 1/6 result.

Stressed 75 is now null twice, which at least is consistent.

## The verdict

**Candidate B is rejected. The shipped 10 ms attack stays, in both cells.**

`opto-attack-b` is **not merged** and is kept only as the record. The branch
holds the implementation, the three rounds and this verdict; nothing from it
goes into `integration` except this note.

Nothing in `modules/opto/` changes as a result of any of this. The fixed attack
and the two comments explaining why it is fixed stand as they were — and they
were right: no source supported the attack moving, and three rounds of blind
listening could not hear it move either.

## What the exercise was worth

It cost three rounds and it prevented a change to how a shipped module sounds
on the strength of a result that was noise. Round one put candidate B first in
both groups and would have shipped it. What stopped that was the control:

- **Round one**: one group turned out to be two byte-identical files ranked a
  place apart, which made that group unrankable and left one group of evidence.
- **Round two**: the duplicate-pair design gave a clean separation at CRUSH 50
  — p = 1/6, reported as suggestive rather than settled.
- **Round three**: the same test, repeated, reversed.

The design earned its keep. An ordinary three-letter round, run twice, would
have shown candidate B first in round one and first in round two and the change
would have gone in.

**The lesson to carry into the next listening test of a small difference:**
enter at least one variant twice. The gap between two identical files is the
noise floor of that comparison, measured on the same material in the same
sitting by the same ears, and without it there is no way to tell a small real
difference from a small imagined one. Both Opto rounds that looked positive
were positive by less than that floor.

## Not done, and now not needed

The absolute t63/t90 assertions and the thirteen re-solved preset levels were
gated on this round. They are not needed: nothing is changing.

Worth keeping from it anyway: **`OptoDspTests` passed unchanged with the attack
rewritten in both cells**, which means the suite does not pin the attack at
all. That is a real gap whatever the verdict, and it is not fixed here.
