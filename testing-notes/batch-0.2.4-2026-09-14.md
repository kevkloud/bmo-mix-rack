# 0.2.4 — the three-module batch, built for testing

Written on **AURORA**, 2026-09-14, at the end of the session that found the
pops. This is what is in the build Frosty will test, what was verified before
it went to CI, and what is open.

## What it is

`v0.2.4` tags **`cbd0939`**, which is the commit both CI runs build and the
commit AURORA verified. Integration's tip moves past it with docs commits like
this one, which start no build (`paths-ignore` covers `**.md`). CI run
**34834557823** (dispatched) and **34834687703** (started by the tag itself --
see `WORKFLOWS.md`, the triggers include `tags: ['v*']`). Same commit, so
either one going green is the answer. Three products land together:

| | what changed |
|---|---|
| **BMO DEQ** | already on integration since 2026-09-12: solo, the analyser tap, serial topology settled by ear, the macOS T2 ceiling fix |
| **BMO Vcomp** | new: the compressor, the gain computer lifted into `core/dsp`, the limiter, the voicing, its checklist and handoff |
| **BMO Tune RT** | the pops, diagnosed and fixed — guard 6, the early exit retired, the low-band sums stepped, and the live-monitoring budget |

## What was verified before it was pushed

On AURORA, on the exact commit CI is building:

- **26 of 26** full Release suites, plugins included
- **15 of 15** DSP-only suites
- corpus mean gross error 1.9344 %, unchanged, no item worse
- a render of Failure and Fuji at 20 ms from this tree is **bit-identical** to
  the files Frosty ranked in round eight (`90bcf29c1fc35464`,
  `1837d485f0bc68c4`) — what shipped is what he heard

`tune_hardtune_target` remains disabled. Its one open check is Tune's worst
correction lag at A2, 1.97 ms against Antares' 1.66; every other vibrato is
inside half a millisecond.

## Read the earlier CI run correctly

Run **34818965850**, dispatched at 07:40 on `8567765` (DEQ + Vcomp, before
Tune), shows as **failure** in `gh run list`. It is not a test failure:

```
DSP:             success   (1m18s)
Windows:         success   (42m54s)
Each side alone: success   (57s)
macOS:           CANCELLED (15m1s, no step records)
```

The macOS job was cancelled fifteen minutes in with no steps recorded, which
is an infrastructure outcome rather than a red test. Worth watching all the
same: macOS is this repository's fragile job — DEQ's only CI failure ever was
macOS-only (`deq_dsp` "T2 ceiling", fixed in `e2ca1b3`).

## The tag, and where it sits — read this before trusting it

`v0.2.4` is on **`integration`**, and the four tags before it are all on
**`main`**. That is a departure and it is deliberate:

- `WORKFLOWS.md` says the fork's `main` tracks Kevin's exactly, because that is
  what makes the stage-5 rebases possible. DEQ, Vcomp and Tune do not reach
  `main` until stage 5, so a `v0.2.4` on `main` would break that.
- So this tag names **the build to test**, not a release to Kevin. If it should
  become a release tag on `main` at stage 5, move it then — one command.

Also worth knowing: **there is no `v0.2.3`, anywhere.** The docs cite 0.2.3 as
a shipped release in a dozen places and `main` is 98 commits past `v0.2.2`. The
convention had already lapsed once before this.

## What is open

1. **Frosty's plan for tomorrow**: a run-through of every plugin one at a time,
   clean workflows for each, then the full UI pass — layout, meters, colour,
   knobs, labels. That is stages 3 and 4 of `WORKFLOWS.md`, in the right order.
2. **Three checklists do not exist yet** and stage 4 wants them written
   *before* the round, not during it: **BMO CEQ**, **BMO Util**, and **the rack
   itself**. Every other module has one.
3. **Tune, next by ear**: Failure at 20 ms, the one group of four where Antares
   still wins, and the pop at **6.137 s** that guard 6 does not clear — a
   phrase end where clarity falls to 0.05 and the detector keeps tracking
   noise. That is the voicing item, not the octave one.
4. **`bmo-tune-field`'s ON NOISE exclusion is wrong** and should be re-scored
   rather than deleted. Frosty heard a pop "at the word spills" on Fuji that
   the tool discounts by design; guard 6 removed the two splices it was
   discounting (15.950 s and 19.457 s, landing 1.63 and 1.60). Fourth measure
   retired against these ears, after splice count, splice landing error and the
   rest.
5. **Buying latency back.** Wanted, not owed. The budget is now BMO's own curve
   and that curve is very nearly `liveRest + T`, so getting under it means
   changing what a splice **is**, not tuning a constant.

## Rules that still hold

- Name the machine. All of the above is AURORA.
- Nothing reaches Kevin until the suite is finished and heard — stage 5, and
  Frosty opens those pull requests himself, one per product, rebased onto
  `main`.
- Field audio, blind sets and the licensed fonts are never committed.
