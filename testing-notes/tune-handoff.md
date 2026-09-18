# Handoff: BMO Tune RT after rounds three and four

Written on **AURORA**, 2026-09-11, at the end of the session that moved Tune
into the rack repository, heard two blind rounds, and found what was causing
the pops. **This is written to be reviewed**, so it says what is shaky as
plainly as what is done.

Branch **`bmo-tune-work`**, off `integration`, worktree
`../bmo-mix-rack-333-tunework`. Read `WORKFLOWS.md` on `integration` first --
it sets the branch map, the stages, and the rule that nothing reaches Kevin
until the whole suite is finished and heard. The commits here are
**unpushed** (Frosty's call: batch with DEQ, one CI run), and
`origin/integration` has moved on again, so a merge comes before the push.

> **Superseded for everything after 9b18577 by
> `testing-notes/tune-handoff-2026-09-13.md`**, which has rounds five and six,
> the list of avenues measured and thrown away, and which build is still the
> best by ear (this one). Read that for the state; read this for how the
> engine works and what rounds three and four found.
>
> **Revised the same day by `testing-notes/tune-latency-review-2026-09-11.md`.
> Read that alongside this.** It was asked for as a review of this handoff and
> it changes two of its conclusions:
>
> - **The 4 ms rest is not a splice fix, it is an alignment.** What it does is
>   move the engine's read toward where the detector's estimate actually
>   refers to -- one period back. The splice curve it was chosen on had
>   stopped responding; the lag it was read as incidentally improving is the
>   thing it was really doing. A constant pays that debt at about 290 Hz and
>   nowhere else, which is why the lag tracks the period, and why Fuji cleared
>   while Failure did not.
> - **The latency rule is a curve and BMO is over it above C3.** Waves' delay
>   is proportional to the note; BMO's rest is a constant. "6.53 ms against
>   Waves' 10.62 ms ceiling, level with Antares' 6.49" below is a comparison
>   of three numbers all taken at one note, and it flatters BMO: read per
>   note, BMO is the latest of the three over most of the range. The stimulus
>   and both gates have been fixed; `tune_hardtune` is red on it, by design.
>
> Nothing in the numbers below was wrong as measured -- the tables stand.
> What changed is what they mean, and the 38 remaining Failure splices are now
> expected not to yield to more window.

## What happened today, in order

| | what | commit |
|---|---|---|
| 1 | Tune moved into the rack fork; every baseline number re-measured and identical | `c09c864` |
| 2 | The field tool's CSV carries splices per evaluation -- the instrument the rest of the day needed | `2c295c5` |
| 3 | **Round three heard**: the dwell beat round two at 0 ms and came last at 20 ms | `c629af2` |
| 4 | A hold is billed the pull it cannot afford | `3d365a7` |
| 5 | **The read window widened to a 4 ms rest** -- the actual fix | `31b30ef` |
| 6 | **Round four heard**: BMO above its own last build everywhere, above Antares on Fuji | `be9b186` |

## The finding, in one paragraph

A splice is the engine's read pointer running out of window and jumping a
whole period; a splice is what Frosty hears as a pop or a click. The window
was as narrow as it could be -- the read rested 0.40 ms behind the input,
about one period of room either way -- so any correction held against a
singer who had moved spent the room and spliced. That is why the flat 40 ms
dwell, meant to stop hunting, made things worse at retune 20 ms: a slow glide
holds the wrong target longer. Resting 4 ms back gives the read room, and the
pops largely go.

## The numbers, and how to get them again

At `be9b186`, DSP-only Release, on AURORA. The takes are **44.1 kHz** and
gitignored; they live beside the old standalone repository.

```
cmake -S . -B build-dsp -DBMO_DSP_ONLY=ON
cmake --build build-dsp --config Release --parallel
ctest --test-dir build-dsp -C Release --output-on-failure          # 14 of 14
build-dsp/tools/tune/Release/bmo-tune-ref bmo
build-dsp/tools/tune/Release/bmo-tune-field "../bmo-tune-rt/field-audio/shootout-2026-09-11/failure/Antares Failure DRY.wav" --set key=D --set scale=Major --set retune_ms=20
bash scripts/score-corpus.sh                                       # 1.9344 %
```

| | round three's build | now |
|---|---|---|
| Failure splices, 0 ms | 139 | **44** |
| Failure splices, 20 ms | 120 | **38** |
| Fuji splices | 96 | **22** |
| correction lag | 6.21 ms mean, 8.96 worst | **3.19, 6.07** |
| RMS tuning error | 6.61 c | **3.35 c** |
| true latency | 3.66 ms | **6.53 ms** (in tune 4.49), reported 0 |
| corpus, 72 items | 1.9344 % | 1.9344 %, no item differing |

The latency rule holds: 6.53 ms against Waves' 10.62 ms ceiling, and level
with Auto-Tune Artist's 6.49 ms. `bmo-tune-latency --range all` shows every
voice cell resting at exactly 4.000 ms.

## What the ears said

Round three (`blind-2026-09-11-round3`): Failure 0 ms Antares > BMO now >
round two; **Failure 20 ms Antares > round two > BMO now, last**, "pops and
skips at the beginning of every word"; Fuji 0 ms BMO 0.1 > BMO now > Antares.

Round four (`blind-2026-09-11-round4`, after the window): Failure 0 ms
Antares > **BMO now** > round three; Failure 20 ms Antares > **BMO now** >
round three, "still too many audible pops"; Fuji 0 ms **BMO now, "perfect"**
> Antares > round three.

Both sets align by onset, so round four moved the new renders about 3.9 ms:
what was ranked is the tuning, not the added delay. Both were answered before
the key was opened.

## What to look at hard, if you are reviewing

1. **The dwell billing rule (`3d365a7`) earns almost nothing.** It wins back
   2 splices of 13. It is in because the law is right -- a hold is billed the
   pull past `noteHoldFreeCents`, times its duration -- and because the tests
   now state that law instead of a constant. A reviewer could fairly argue it
   should have been reverted once the window fixed the real problem. It was
   kept, not defended by results.
2. **`noteHoldFreeCents = 60` and `noteHoldBudgetCentMs = 320` were chosen to
   keep a test passing**, not from a measured optimum. The settings that
   scored best on splices (50/240: 103) broke Frosty's round-two vibrato
   hold. That conflict is mostly moot now -- the window took the splices --
   but those constants carry that history, not a principle.
3. **The flip counter undercounts bursts.** `bmo-tune-field` counts a flip
   only when the return is the *next* change (`tools/tune/field/main.cpp`),
   so A to B to C to A inside 80 ms registers as nothing. Around 17.41 s on
   Failure the note changes six times in 13 ms and none of it counts. Every
   flip number in these notes and in the commits is a lower bound, and it is
   blindest exactly where the trouble is densest. Changing the definition
   rewrites every recorded number, so it was left alone -- but it should be
   decided, not inherited.
4. **The corpus does not exercise any of this.** It scored 1.9344 % before
   and after both changes, with no item differing, because it holds no
   near-boundary holds and no real scoops. It is a real guard against
   regressions elsewhere; it is not evidence that these changes are good.
5. **Two takes, one singer each.** Every conclusion rests on Failure and
   Fuji. The window result is large enough not to be luck, but the splice
   counts across settings were not monotonic (at 20 ms: 118 at budget 1200,
   120 at 640, 121 at 320, 110 at 160), which says the metric is sensitive
   to small changes and should not be read to the last unit.
6. **The 4 ms rest has never been felt.** The blind sets align it away, and
   the installed VST3 on AURORA is still 0.1, so nobody has tracked through
   it. 6.53 ms is inside the rule and level with Antares, but that is an
   argument on paper about something a singer notices.

## Pick up here, in order

1. **Failure's remaining splices** -- 38 at 20 ms, and audible. Start from
   the 66 jump flips, mostly on scoops; `bmo-tune-field --csv` now carries
   splices per evaluation, so they can be located rather than guessed at.
   Build the case synthetically, fail a test on it, then fix.
2. **Ableton, with the 4 ms rest**, so Frosty feels the latency he has so far
   only seen measured. Needs a plugin build installed over the 0.1 on AURORA.
3. **Dropouts and weak phrase ends** -- 15 on Failure, 12 on Fuji under 80
   ms: the voicing hysteresis (0.85 on / 0.60 off), the -55 dB gate, the
   10 ms release.
4. **`tune_hardtune_target`**, still disabled and still failing, but far
   closer: the high vibrato reads 2.48 ms against Antares' -0.24 ms, where it
   read 5.5. The window bought most of that; the low A2 vibrato, at 6.07 ms,
   is what is left, and the lag tracks the period.
5. **Waves' ceiling across block sizes** -- measured at 128 and 2048 only.
   Sweep 32 to 2048 and keep the lowest, since the rule is built on it.

## Rules that still hold

- Name the machine in anything that records where something happened:
  AURORA (laptop, `C:\Users\thesp`) or ICE QUEEN (desktop, `C:\Users\stefr`).
- Measure, never judge by eye or ear alone; nothing is fixed that a test did
  not first fail on; Frosty hears every fix before it is called fixed.
- The schema is frozen; new meaning gets a new id (`kRetiredIds`).
- No FFT in the correction path; nothing allocates after `prepare()`; the
  host is told 0; true latency never over Waves' (the latency rule).
- Field audio, blind sets and the licensed fonts are never committed.
- Pushes only with Frosty's say, to the fork's `bmo-tune-work`. Ask first,
  and batch: a CI round trip is about 22 minutes.
