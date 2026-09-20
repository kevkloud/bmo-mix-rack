# BMO Dimension — the meter pass

The metering half of the listening pass. `dim-testing-checklist.md` says *what
to listen for*; this says **how to instrument it** so the answers are worth
something.

A tickable version of this lives as an artifact, which is easier to work from
at the desk. Frosty has the link; it is not recorded here.

Dimension has no meter of its own — a goniometer is the meter its panel wants
and is deliberately deferred. **SSL Meter Pro stands in for it.**

---

## 01. Set the bench up

    Source -> BMO Dimension -> Utility (gain match) -> SSL Meter Pro

- Chain the meter **last**, after the gain-match Utility. Metering ahead of the
  trim measures a signal you are about to change.
- Put a **second Utility with Mono** on a bypassable rack ahead of the meter.
  Half of what follows is a mono check and it needs to be one click.
- Set the source to roughly **−18 dBFS RMS / −12 dBFS peak**. Track level, not
  mix-bus level; the 0.2.1 pass learned that the hard way.
- **Look at the panel at its defaults before touching a knob.** Three of the
  worst faults this suite has produced were visible only in the opening state.

### Gain-matching, which is not optional

Dimension has **no output trim** and up to **+15.5 dB** is reachable — SHUFFLE
3.0 × WIDTH 200 % on anti-phase 80 Hz took a 0.5 peak to 2.98. Louder reads as
better, and it will corrupt every subjective call on this list.

**Loop a fixed passage and read integrated LUFS — never peak.** LUFS-I
converges on a loop and reads to 0.1 LU; peaks are erratic and will leave you a
dB out, which is squarely where louder wins. Type the difference into the
Utility, and **write the number down** so it is repeatable across sessions.

**Match the mono sum, not the stereo pair.** Since `L = M+S` and `R = M−S`, the
stereo power sum is

    L² + R²  =  2M² + 2S²

so **side energy raises measured level even when the mid is untouched** — take
the side from zero to equal the mid and a stereo reading rises 3 dB with the
centre completely unchanged. Normalise Dimension on stereo level and you
attenuate to pay for width, pulling down the very mid the module guarantees it
never touches; a mono listener then hears the vocal go quiet.

**For the mono-compatibility tests, do not gain-match at all.** There the level
change *is* the finding.

## 02. Prove the wire before you judge the sound

Ten minutes, and it turns the module's central claim from something a unit test
asserts into something seen in the real host, at the real sample rate and
buffer size. The handoff is candid that the mono-sum test is close to
tautological on its own. This is not.

Duplicate the track: Dimension on one copy, nothing on the other. **Invert the
polarity of one and sum them.** What you hear is the difference between the two
paths, so **silence means they are identical**.

- [ ] At **defaults**, the two paths null to **silence**. Init is a wire;
      anything left over is a fault, not a voicing question.
- [ ] **Sum both to mono**, then turn up WIDTH, DIFFUSE and DETUNE. It must
      **still** null — that is the whole topology.
- [ ] Turn **ROTATE** or **ASYM**. It must now **fail** to null. These are the
      two documented exceptions and they are meant to break the sum.
- [ ] Run **Chorus-Ensemble** through the same mono null. It should leak badly.
      **If it nulls, your mono path is not summing** and nothing above was a
      valid test.

## 03. What each readout means here

    r  =  ( M² − S² ) / ( M² + S² )

Correlation is a direct read of the mid-to-side energy ratio, which is exactly
what this module manipulates. **Side content pushes r down from +1; it never
pushes it up.** So with DETUNE on, r falling and recovering *is* the side signal
beating — the throb, drawn.

- **Correlation** — the primary instrument. **+1** mono, **0** side energy equal
  to mid, **−1** pure anti-phase.
- **Goniometer** — vertical is mono, a spread blob is stereo, **horizontal is
  out of phase**.
- **True Peak** — no output trim, so overs are the module's to cause.
- **Spectrum** — settles the comb comparisons in 05 and 06.

## 04. Baseline, then the throb

- [ ] Dry source, DETUNE out, DIFFUSE 0, WIDTH 100. **Record correlation.**
- [ ] Insert Dimension at defaults. Correlation must be **unchanged**.

| CENTS | side envelope, peak–trough | beat rate | correlation swing observed |
|---|---|---|---|
| 5 | 25.2 dB | ~6 Hz | |
| **10 — default** | **19.6 dB** | **~12 Hz** | |
| 25 | 11.5 dB | ~29 Hz | |
| 10, on a 110 Hz tone | 34.6 dB | ~1.3 Hz | |

**Predicted, not measured.** The dB and Hz columns are measured from the DSP.
The correlation column is not: each trough returns to about +1 whatever the
setting, but the peak depends on WIDTH and on the source, so there is no single
number to predict. **If the swing disagrees with the dB column, that is itself a
finding.**

- [ ] Mono vocal, DETUNE on, WIDTH ~130: **shimmer, or tremolo?** The verdict
      that gates the PR. Watch correlation while deciding.
- [ ] **Sweep CENTS.** The measurement says the throb gets *faster and
      shallower* as CENTS rises — the gentle setting throbs hardest, backwards
      from what a user expects. **If your ear disagrees, say so.**
- [ ] Repeat on **bass or a low pad** — 34.6 dB at 110 Hz, beating ~1.3 Hz. Is
      it unusable on low material?

## 05. Against Ableton Chorus-Ensemble

Not a stand-in for MicroPitch — the opposite. Chorus-Ensemble is the
**conventional wiring**, detuned voices panned against each other, which is
precisely the topology Dimension rejects. It is the counterexample.

- [ ] Duplicate the track, Chorus-Ensemble on the copy, **Ensemble** mode.
- [ ] Gain-match the two before comparing anything.
- [ ] **Sum both to mono.** Chorus-Ensemble should **comb** — visible notches
      and level loss. Dimension should show **nothing at all**.
- [ ] Record the mono-sum level change for each. Dimension's should be 0.0 dB
      with ROTATE and ASYM at their defaults.
- [ ] **Is the trade in the direction we want?** Theirs combs in mono and does
      not throb. Ours throbs and does not comb. Neither is free.

## 06. Against CLA Vocals

The checklist's own named reference. Its Pitch send is the detune doubler,
wired conventionally like Chorus-Ensemble.

- [ ] CLA Vocals on a duplicate, **Pitch send up**, everything else neutral.
- [ ] Accept that this is **not apples-to-apples** and note where it is not —
      CLA Vocals is a whole chain, and its compression, EQ and reverb all move
      level and tone.
- [ ] Gain-match, then A/B in stereo. Which reads wider at matched level?
- [ ] **Sum to mono.** The same comb check as 05.
- [x] On a mono vocal, compare the **throb** specifically — steady width, or
      audible tremolo? Not "shimmer or tremolo": shimmer is not the target and
      CLA Vocals is a reference for how the problem was solved elsewhere, not a
      standard to match. *2026-09-09: not an audible throb, no shimmer, no high
      end added. Passes.*

## 07. What the meter should and should not say

### Want to see

- **Correlation pumps** with DETUNE on, and stays above 0 throughout. The
  pumping is the design, not a fault.
- Each trough returns to **about +1** — the image collapsing to mono and
  reopening. Expected; its *depth* is what you are judging.
- **Mono sum: 0.0 dB change** and no spectral notches, at any WIDTH, DIFFUSE or
  DETUNE, with ROTATE and ASYM at their defaults.
- The goniometer **breathes** between a vertical trace and a spread blob.
- At ASYM 25–50 %, a centre vocal **does not move**. Exactly.
- True Peak stays under the ceiling across the whole preset sweep.
- Chorus-Ensemble and CLA Vocals **do** notch in mono.

### Do not want to see

- **Correlation reaching 0 or below** at a sane setting — side energy has met or
  passed mid; it will sound hollow and collapse in mono.
- **Any** mono-sum level or spectral change with ROTATE and ASYM at defaults.
  The mid path is meant to be a plain wire, so movement is a leak into mid: a
  real bug, not a voicing call.
- The goniometer trace going **horizontal** — anti-phase, not width.
- Correlation **pinned at +1** with DETUNE on: the stage is doing nothing. Check
  WIDTH is not parked at 0, which silently disables everything above it.
- True Peak overs that appear only on a preset — a preset level problem, and all
  thirteen across BMO Opto and the Saturator drifted at once before.
- A centre vocal that **moves** under ASYM — the one thing Gerzon's control is
  defined as not doing, and the exact fault this pass rebuilt it to fix.
- A verdict recorded with **no settings attached**. It cannot be acted on later.

## Recording a finding

For anything flagged: the **setting**, the **source**, **what you heard** in your
own words before any theory about why, and whether it is bad **at useful
settings or only at extremes**. That last one is what decided the ASYM rebuild —
the old law's drift was not confined to the extremes, and that is what made it a
defect rather than a limitation.
