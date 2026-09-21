# BMO Defang — the Ableton pass

**For the session, or the person, who gives this module its first ears.**
Written on AURORA, 2026-09-21, from the branch that became
[PR #21](https://github.com/kevkloud/bmo-mix-rack/pull/21).

Everything shipped so far is **measured**. The arithmetic is tested to 231
checks, the renders are deterministic, and not one sample has been heard. This
file is the list of what the ear still owes, and it is the only way the module
gets finished: none of the numbers below can be settled by another test.

---

## 1. What to build, and the two ways to break something else

Branch `frosty-add-bmo-defang`, worktree `../bmo-mix-rack-333-defang`.

    cmake --build build --config Release --target BmoDefang_VST3

That installs **"BMO Defang.vst3"**, a bundle no other product owns, so it
overwrites nothing. Rescan plugins in Live and it appears under LT3 Audio.

**Release, not Debug.** The Debug build is what every render in this pass used
and it is correct, but it is not what a CPU meter or a busy session will feel
like, and the point of this pass is how the thing behaves in use.

**Two traps, both already sprung once on this module:**

- **Never build the rack target, and never build the tree without naming a
  target.** `COPY_PLUGIN_AFTER_BUILD` is on for every product, so one bare
  `cmake --build build` installs *all nine bundles* over whatever is there.
  That is exactly how the installed 0.2.5 rack — the one mid its own Ableton
  pass — got overwritten during the skeleton session. It still needs
  reinstalling from its CI artefact; see `install-0.2.5-aurora-2026-09-19.md`.
- **A green ctest means nothing after a failed compile.** The old binary is
  still there and ctest will happily run it and print 18/18. Check the build
  exited 0 before believing any count.

---

## 2. What is unheard, which is all of it

Every constant below is in **one place**, `modules/deesser/dsp/DspCore.h`, in
the block headed "The internal constants". They are `static constexpr`, so
changing one is an edit and a rebuild — no schema change, no preset migration,
nothing to coordinate. They are free to move right up until first ship and
frozen the moment it happens.

The table is what each one does and **what wrong sounds like**, which is the
part no test can tell you.

| constant | ships at | too low sounds like | too high sounds like |
|---|---|---|---|
| `kProminenceRefDb` | **0.0** | THRESH 0 already de-essing everything | THRESH 0 doing nothing; the knob works but starts late |
| `kKappa` | 0.6 | misses esses in a consistently bright take — the detector adapts them away | fires on cymbals, bright acoustic, anything with air |
| `kAttackMs` | 0.8 | the first of the ess gets through | onsets dulled; consonants lose their edge |
| `kReleaseFastMs` | 30 | flutter — it lets go inside the ess and re-grabs | HF pumping; ambience ducking after every word |
| `kReleaseSlowMs` | 120 | a long bright passage chatters | a passage stays dull long after it stopped being bright |
| `kSlowEngageMs` | 150 | ordinary esses reach the slow branch and hold the HF down | the slow branch never engages and long sibilants chatter |
| `kHoldMs` | 5 | an /s/-/t/ cluster is treated as two events and flutters | short gaps between words get held down |
| `kHysteresisDb` | 1.5 | the tail of an ess is released too eagerly | the module stays engaged into the next vowel |
| `kKneeDb` | 6 | the cut snaps audibly on a 60 ms event | the cut is vague; THRESH stops feeling like a threshold |
| `kSlope` | 4 | loud esses barely more reduced than quiet ones | a lisp — the ess is removed rather than reduced |
| `kRefGateDb` | −55 | room tone and breaths read as enormous prominence | quiet passages stop being de-essed at all |
| `kBandGateDb` | −60 | breaths trigger it | quiet esses are missed |
| `kRefHighPassHz` | 150 | a kick or a bass note raises the reference and masks the ess | the reference stops representing the voice |

**`kProminenceRefDb` is the one to settle first**, and it is the only one with
no defensible starting value. The spec fits it from a real take's measured
prominence distribution (10 §10.1); no take has been measured, so it ships at
0, which means "THRESH 0 is wherever this detector happens to call zero".
Everything else in the table is a first pass with a reason behind it. This one
is a placeholder wearing a number.

Its symptom is easy to read and easy to misattribute: if THRESH has to sit at
−12 on every source to do anything, `kProminenceRefDb` is too high by about
twelve. If it de-esses hard at 0 on material that does not need it, too low.
**Find the offset that puts a normal vocal at about THRESH 0**, and the knob's
whole −24…+24 travel becomes useful instead of half of it.

---

## 3. What to put through it

- **A close-mic'd vocal, male and female.** 01 §6 puts male concentrations at
  3–6 kHz and female at 6–8. The BITE readout under the ribbon suggests where
  it is hearing them; it is within a seventh from 4 kHz up, and biases high
  below that — see `tests/dsp/DeesserDspTests.cpp` for the measured figures.
- **Something bright that is not a voice**: cymbals, a strummed acoustic, a
  bright synth pad. This one should come back **largely untouched**. If it
  ducks, `kKappa` is too high, and that is the failure 02 §4 names.
- **A consistently bright take** — an aggressive pop vocal, a heavily
  air-boosted mix. The opposite failure: if the esses come through untouched
  here, `kKappa` is too low and the detector has adapted them into the
  background.
- **A consonant cluster** — "best stuff", "just stop" — for `kHoldMs`.
- **A phrase that ends in ambience or reverb tail**, for HF pumping.
- **Hard-panned sibilance**, for the image. It must not wander; the detector
  is power-summed across channels precisely so it cannot.

Hold **LISTEN** on each of these. It outputs `H(x) − x`, the band's own
contribution — what is being taken away and nothing else. If it sounds like a
voice with a dip in it, something is wrong with the path, not with the tuning.

---

## 4. Two questions the spec left open, for the ear

**The shelf's overshoot.** Even with Q capped at `kShelfMaxQ = 2.0`, the high
shelf keeps a visible overshoot above its corner — about 2.5 dB at RANGE 8.
That is what the filter does and what BMO DEQ does. 10 §10.5 asks whether the
shelf wants a lower internal ceiling, and it was deliberately not re-decided
without ears. Compare BELL and SHELF on the same source at the same depth.

**Whether ADAPT earns a control.** `kKappa` ships fixed. If the pass finds
that different sources want genuinely different values — and not just a
different THRESH — that is the evidence for exposing it, and it appends after
`shape` as parameter index 5 without disturbing anything. If one value serves
everything, it stays internal and the schema is simpler forever. 10 §11 is the
argument; this pass is the evidence.

---

## 5. Tools, and what they will not do

    measure_deesser constants              the values in the build, so the
                                           number being argued about is the
                                           number running
    measure_deesser latency                zero at every setting, reported and
                                           measured by impulse
    measure_deesser detect [ms] [ms] [x]   gain reduction over time on a
                                           synthetic ess, with its peak

`detect` is how three false starts on the render's stimulus were diagnosed,
and it is genuinely useful for watching the ballistics — but **it does not do
the thing §10.1 needs.** It runs a synthesised burst and prints reduction; it
does not take a real take and print its prominence *distribution*, which is
what fitting `kProminenceRefDb` properly requires. That mode is unwritten. It
wants a WAV reader (`tools/measure/Wav.h` exists) and an afternoon, and it
would turn the first question in §2 from a listening judgement into a
measurement. Worth doing before guessing twice.

For the panel rather than the sound:

    snapshot deesser out.png signal=-18 stimulus=ess

`stimulus=ess` exists because a de-esser does nothing to a tone — the detector
asks how far the band stands above the signal, not how loud it is.

---

## 6. What to write down

`testing-notes/ableton-defang-<date>.md`, **naming the machine** (AURORA or
ICE QUEEN). What was heard, on what material, at what settings, and which
constants were changed to what. Say plainly which claims are ears and which
are numbers — this module has a lot of the second kind already and needs the
first.

If a constant moves, the commit that moves it should say what it sounded like
before and after. A value with no story attached is one the next person will
change back.
