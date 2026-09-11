# Dimension listening pass — bench state, 2026-09-09 (stefr machine)

What was set up and mechanically settled before the listening pass, so the
subjective session spends its time only on what needs ears.

**PR #4 remains in draft.** The gating question — §1's throb — is unanswered.
Nothing here clears it.

---

## 0. The current listening/dim test, and nothing else

Four documents describe this pass. Work them in this order, and treat the
artifact as authoritative where they disagree, because it is the only one
carrying the measured numbers:

| | what it is | state |
|---|---|---|
| **Meter Pass artifact** | tickable, and now pre-filled with §6's measurements | **current — start here** |
| `dim-meter-pass.md` | the same structure as prose | current, but its correlation column is still blank |
| `dim-testing-checklist.md` | what to listen for | current |
| this file | bench state and what needs no ears | current |

    https://claude.ai/code/artifact/7e52e5d4-1be1-42f8-a3dc-1473f8821d10

`~/Downloads/dimtestingchecklist.md` is **byte-identical** to
`testing-notes/dim-testing-checklist.md`, so there is no second version to
reconcile — but prefer the repo path, since the loose copy will not follow the
branch.

One stale line in the checklist's own header: it says the build under test is
"`add-bmo-dimension`, on top of `ee6721d` plus the review pass". That is two
merges out of date. The build under test is now `c957ebf` — see §4.

---

## 1. Correction: the handoff's machine section does not describe this box

`session-handoff-2026-09-09.md` §4 says the machine is `AURORA\thesp` and that
handoffs naming `C:/Users/stefr/...` are stale. **On this machine that is
backwards.** This is `stefr`, and its toolchain is current and working:

    git 2.55.0.windows.5, cmake 4.4.3, gh 2.100.0, Ableton Live 12 Suite
    -- BMO fonts: C:/Users/stefr/Documents/FONTS (.bmo-fontdir)

    cmake -S . -B build      # configures clean
    bash scripts/build.sh    # 12/12 ctest at c957ebf

Treat §4 and §5 of that handoff as describing a *different* machine. In
particular its "installed and verified this session" list was verified there,
not here — §2 below is this machine's inventory.

## 2. Reference plugins actually present here

| needed by | plugin | state |
|---|---|---|
| meter pass §01/§03 | SSL Meter Pro | **installed** — the goniometer stand-in |
| meter pass §05 | Ableton Chorus-Ensemble | **installed** (stock Live 12 Suite) |
| checklist §1, meter pass §06 | CLA Vocals | **installed** — Waves Plug-Ins V13 |
| checklist §1 | Eventide MicroPitch | **not installed** |

MicroPitch's absence does not block anything: checklist §1 says "MicroPitch
**or** CLA Vocals", and meter pass §05 is explicit that Chorus-Ensemble is not
a MicroPitch stand-in but its counterexample.

### The Fuji reference — found, and it will not meet §01's level target

    D:\VISUAL\PLUGINS\MIX RACK\SATURATOR\fuji NOT SATURATED.wav

The same source Opto and Saturator were tested with, so continuity of source
holds. 44.1 kHz, 16-bit stereo, 20.20 s.

Do **not** search for it by the name "Fuji": the two folders that match on
this machine — `C:\Users\stefr\Documents\FUJI` and `D:\VISUAL\FUJI` — are
Fujifilm camera directories of JPG/RAF. The audio lives under
`D:\VISUAL\PLUGINS\MIX RACK\`, which is also where every other module's test
material and the D: handoffs sit.

Measured, so the level question is settled before the bench is built:

| | whole file | loudest 3 s window (from 5.50 s) |
|---|---|---|
| peak | −9.35 dBFS | −9.35 dBFS |
| RMS | −29.87 dBFS | −27.59 dBFS |
| crest | | **18.24 dB** |

**§01 asks for roughly −18 dBFS RMS / −12 dBFS peak, and this file cannot be
both.** That target implies a 6 dB crest; this source has 18.24 dB. Trimming
to −18 RMS is +9.59 dB, which puts the peak at +0.24 dBFS — clipped.

That is not a blocker, because what §01 actually protects against is *louder
reading as better between A and B*. The absolute number is headroom guidance;
the matching is the part that matters, and §01 already says to do it by
integrated LUFS on a loop. Pick one and write it down:

- **Leave the source alone** and gain-match only the A/B legs. Simplest, and
  the peak is already conservative.
- **Trim about +6 dB** to sit near −21 RMS / −3 peak, if a hotter feed is
  wanted into the module.

Either way, note that Dimension has no output trim and up to +15.5 dB is
reachable, so leave real headroom downstream.

## 3. Where the D: handoff is, and why it differs from the repo one

    D:\VISUAL\PLUGINS\MIX RACK\DIMENSION\.md\HANDOFF 9.8.26.md

The containing folder is literally named `.md`, so it is hidden from a plain
`ls` and from most globbing — which is why a search for `*DIMENSION*` at the
top of `D:` finds nothing. The filename is dated and is renamed on each edit,
so the date will move; look for whatever single file is in that folder.

`testing-notes/dim-1.0-handoff.md` in the repo is **an adaptation of it, not a
copy**, matching what `opto-0.2.1-handoff.md` and `docs/ui-workflow-brief.md`
already do: the repo version is written for someone reading the diff cold,
with D: paths, artifact URLs and session scaffolding stripped.

So the two are *supposed* to diverge. Do not "sync" them, and do not treat
content missing from the repo copy as an omission — check whether it is
session scaffolding that was deliberately dropped.

## 4. Build under test

BMO-Windows artifact from run `34397185286`, green at `c957ebf` — the branch
tip, which already merges Kevin's `main` (`8602f32`) and so carries PR #5's
rest-dot fix and PR #6.

All six bundles installed to `C:\Program Files\Common Files\VST3\`, binary
sizes verified against the artifact. Dimension is in the package, confirming
the packager reads the build tree rather than a hardcoded list.

Note: that directory was **user-writable** — no elevation needed, contrary to
the handoff's "(needs admin)".

The build it replaced was dated Sep 8 and predated the rest-dot fix. Anything
observed on the panel before today should be re-checked.

### `scripts/build.sh` silently replaces the tester install with a Debug build

This happened here, and it is by design rather than a bug. `CMakeLists.txt:35`:

    # Only development builds install into the user's plugin folders. A
    # Release build is for packaging and must not clobber what is being tested.
    if(CMAKE_BUILD_TYPE STREQUAL "Release")
        set(BMO_INSTALL_AFTER_BUILD FALSE)
    else()
        set(BMO_INSTALL_AFTER_BUILD TRUE)

`scripts/build.sh` builds **Debug**, so it takes the `TRUE` branch and
copy-after-build overwrites `C:\Program Files\Common Files\VST3\` with Debug
binaries. The comment's worry is the Release path; the Debug path is the one
that actually clobbers.

Sequence that caught it: artifact installed 15:50, `scripts/build.sh` run for
ctest and the panel re-render at 15:52, and all six installed bundles were
Debug from that moment. Sizes still matched the artifact to the byte, so a
size check does not catch this — **only a hash does.**

**Therefore: install the artifact LAST, after any building.** And verify by
SHA-256 against the artifact, not by timestamp or size:

    Get-FileHash "C:\Program Files\Common Files\VST3\BMO Dimension.vst3\Contents\x86_64-win\BMO Dimension.vst3"

`build.sh`'s header already warns that Live holds bundles open so the copy "can
fail quietly and leave you auditioning a stale binary". This is the mirror of
that: with Live closed the copy *succeeds* quietly, and leaves you auditioning
a Debug binary. A Debug DSP build is not what the PR ships and should never be
what a voicing verdict is formed on.

## 5. Settled without ears — do not spend listening time on these

- **Rest dots agree with pointers on all nine knobs.** Re-rendered; the
  rest-dot commit (`79840ae`) had deferred Dimension's render to "when the
  branches meet", and they have. They cannot disagree by construction: the dot
  is drawn from `getDoubleClickReturnValue()` through the same
  `valueToProportionOfLength` mapping as the pointer.

  **A caution for the panel half of §4:** at full-panel scale RATE's pointer
  reads as sitting at ~40 % while its dot sits near minimum. Cropped and
  magnified, both are at lower-left and agree — RATE's default of 0.40 in
  [0.05, 5.0] is 7 % of sweep. This is §7's "measure a render, never judge one
  by eye" happening again. Crop before believing anything about a pointer.

- **§4 ROTATE sign — confirmed.** `m2 = m·cosθ − s·sinθ`, `s2 = m·sinθ + s·cosθ`
  (`DspCore.h:381`). At +30° a centre source gives L = 1.366, R = 0.366:
  the image moves **left**. Still a free choice, still one line.

- **§4 WIDTH-0 gating — confirmed.** `side *= widthSm.tick()` (`DspCore.h:372`)
  sits downstream of generate, diffuse and shuffle, so WIDTH 0 nulls all of
  them. The question is whether it trips a user up, not whether it happens.

- **§4 dimming inactive knobs — the control exists.** `PlainKnob::setKnobEnabled`
  is at `core/ui/Controls.cpp:126` and is unused across the whole suite.

- **§6 contrast — measured at exactly 1.73:1.** Caption ink `#d4a4ff` on plate
  `#efefef` (L* 74.7 on 94.4). The checklist's figure is right. Unchanged
  policy from 0.2.3, not a Dimension decision; §6 stays a gut-check only.

## 6. The offline meter pass — measured, not predicted

`DspCore.h` is self-contained (485 lines, all inline, only `<algorithm>`,
`<array>`, `<cmath>`, `<vector>`), so it compiles standalone against the real
source file with **no repo build target and no repo changes**. Everything here
is the real DSP on the real listening-test material at 44.1 kHz in 512-sample
blocks. The tickable artifact now carries these in its note fields.

### The wire holds, on real audio

| meter pass item | result |
|---|---|
| 02 n1 defaults null | **−234.80 dBFS** peak residual, both channels — bit-exact |
| 02 n2 mono null, WIDTH 200 / DIFFUSE 100 / DETUNE on / SHUFFLE 3.0 | **−156.54 dBFS**, mono level delta **−0.0000 dB** |
| 02 n3 ROTATE +30 breaks the null | −26.81 dBFS residual, −1.249 dB mono level — correct |
| 3 DETUNE re-engage over silence | **0.00000000** peak — the 0.90 burst is gone |
| 4 headroom, SHUFFLE 3.0 × WIDTH 200 on anti-phase 80 Hz | 0.5 → **2.983** (+15.51 dB); published 2.98 |
| 4 WIDTH 0 gating | peak side **0.00000000** |

### Correlation, which the artifact had as "predicted, not measured"

50 ms windows over the loudest 6 s, gated above −80 dBFS mid, CENTS 10:

| WIDTH | median r | share of windows r < 0 | min r |
|---|---|---|---|
| 70 % | 0.73 | 1.8 % | −0.67 |
| **100 % (default)** | **0.52** | **6.1 %** | −0.82 |
| **130 % (the checklist's own setting)** | **0.30** | **18.4 %** | −0.89 |
| 200 % | −0.12 | 70.2 % | −0.95 |

Dry reference: mean r 0.9999, swing 0.0017.

**Meter pass §07 lists "correlation reaching 0 or below at any sane setting"
under Do Not Want To See, and it is reached at the default width and above.**
Read that carefully before acting on it — §07 gives two reasons for the rule
and only one survives here:

- *"it will collapse badly in mono"* — **does not apply.** The mono sum is
  proven exact to −156 dBFS with everything engaged. Collapsing to mono
  returns the dry mid, by construction.
- *"it will sound hollow"* — **still open, and is exactly the ear question.**

So this is not a bug and not a verdict. It is the number §04 asks the verdict
to be attached to, and it says the throb is deeper than "pumping above zero".

### Direction of travel — the published claim is confirmed

The gentle setting really does throb hardest, on both measures. Tones:
CENTS 5 = 25.6 dB, 10 = 19.6 dB, 25 = 11.5 dB. Real source, share of windows
with r < 0 at WIDTH 130: CENTS 5 = 21.1 %, 10 = 18.4 %, 25 = 16.7 %.

One disagreement: **110 Hz at CENTS 10 measured 40.7 dB against a published
34.6 dB**, 6.1 dB deeper. The three 1 kHz figures match to 0.4 dB, so the
method agrees everywhere else; likely a window-length difference against a
1.3 Hz beat. Worth reconciling, not alarming.

### The source cannot serve section 2

`fuji NOT SATURATED.wav` is **mono in a stereo container** — side peak
0.0000305, exactly one 16-bit LSB, and 55.5 % of samples are bit-identical
L == R. That makes it ideal for §1's throb test, which asks for a mono vocal,
and **useless for §2 and for 02 n3's ASYM leg**: the shear is `mid += 0.25·side`
at ASYM 50 %, and with no side content it measured −102 dBFS, which is LSB
noise rather than the control working.

**Before §2, find a stereo mix with hard-panned material and a centre vocal.**
Nothing about ASYMMETRY can be judged on this file.

## 7. The Ableton pass — what came back, 2026-09-09 evening

Frosty's session in Live 12.4.5, project `DIM TEST true mono`, bounced at
48 kHz. **The dry reference is 44.1 kHz, so nothing here is measured against
it** — Live resampled on import and no null against it is valid. A matching
re-export was in progress when this was written.

### Confirmed in the host

- **The wire holds.** Perfect null at Init against a polarity-flipped dry
  duplicate. Frosty then swept **every** parameter, not just the three the
  checklist names: width, detune, diffuse, **rate, depth, shuffle freq and
  shuffle** all keep the null. ROTATE and ASYM break it, as they must.
  That is a broader test than §02 asks for and it passed.
- The **−1.5 dB** he had to apply to the dry to null is his gain-match Utility,
  not the module: Dimension at Init is bit-exact offline (−234.80 dBFS).
  Worth confirming the Utility was the source before this is written up.

### Correlation in the host matches the offline prediction

| | median r | min r | max r | windows r < 0 |
|---|---|---|---|---|
| **DIMENSION** bounce | 0.264 | −0.970 | 0.997 | **16.1 %** |
| offline prediction, W130 | 0.30 | −0.89 | — | 18.4 % |
| ENSEMBLE bounce | 0.785 | −0.084 | 1.000 | **0.6 %** |
| CLA bounce | 0.516 | −0.416 | 0.997 | **2.8 %** |

The offline model is validated against the real host. And the comparison the
checklist wanted is now numeric: **Dimension spends roughly 27× more time
anti-phase than Chorus-Ensemble, and 6× more than CLA Vocals.**

### The comb is real, and it moves — a correction

A time-averaged spectrum of the mono sums showed **no** notches for either
reference, which §07 says would invalidate the whole comparison. That was the
wrong instrument, not a wrong premise: a modulated chorus sweeps its notches,
so they average away. Measured per 85 ms frame instead, against Dimension's
mono sum as the reference:

| mono sum | mean dev | std dev | 5th pct | worst frame |
|---|---|---|---|---|
| ENSEMBLE | +0.77 dB | **2.53 dB** | −3.42 dB | **−19.78 dB** |
| CLA | −0.03 dB | 0.78 dB | −1.20 dB | −4.16 dB |

Chorus-Ensemble's mono sum wanders ±2.5 dB band-to-band with individual frames
**20 dB down**. Dimension's does not move at all, ever, by construction.
**That is the trade the module exists to make, and it is now demonstrated
rather than argued.** CLA's Pitch send is far gentler than Ensemble here.

**Use a long-term average to look for a moving comb and you will find nothing.
Measure per frame.**

### The verdict that gated the PR — §04 b3, answered

> "It isn't an audible throb, sounds good, slight tremolo I guess? No high end
> added, no shimmer." — mono vocal, DETUNE on, CENTS 10, WIDTH ~130

**The gate is cleared.** The failure mode the branch was held in draft for — an
audible tremolo/flutter of the width — does not reproduce as a problem.

Read this against the measurement, because they disagree in an instructive way:
**16.1 % of windows measured anti-phase, and §07 lists that under Do Not Want
To See — but the ear cleared it.** The metric found a real property of the
signal and was wrong about what it would cost. Worth remembering the next time
a correlation figure is treated as a verdict rather than as a description.

**"No high end added, no shimmer" is the second half of the pass, not a
caveat.** Width without shimmer is the intended result. MicroPitch was a
research reference — how the problem has been solved elsewhere — and was never
the target; the shimmer is the part of it this module deliberately does not
want. Getting width without it is the design working.

Note that the test documents get this backwards. `dim-testing-checklist.md` §1
asks "does it read as MicroPitch-style shimmer, or as an audible
tremolo/flutter", and `dim-meter-pass.md` §06 repeats it — both phrase shimmer
as the good outcome and leave no way to record "widened, no shimmer, correct".
Corrected in both. **A checklist that names a reference product in the question
invites the reader to score against that product rather than against the
brief.**

### ROTATE was backwards, and is fixed

Confirmed by ear on a stereo source: "-" moved the image right and "+" moved it
left, the opposite of a pan knob. Matches the code exactly — `+30°` on a centre
source gave L = 1.366, R = 0.366.

**Fixed** by negating the angle in `setParams` and `snap`
(`modules/dim/dsp/DspCore.h`). The S1 manual fixes the rotation law but says
nothing about the knob's direction, so the sign was always a free choice. No
test asserted it — the DSP tests only check that rotation *changes* the mono
sum, which is sign-agnostic — so nothing needed updating.

### ASYMMETRY, finally testable

On a proper stereo source, with DETUNE out, **ASYM and ROTATE both function as
expected.** That closes §2, which the mono Fuji file could not exercise at all.

### New finding: RATE and DEPTH do not earn their panel space

Frosty's call from the listening pass: neither control makes an audible
difference worth the space. **Lock both at their defaults and remove the
controls from the panel.**

Note the mechanics: `kRate` and `kDepth` must **stay in `specs()`** — parameter
IDs are permanent and append-only (`modules/dim/params.h`), and removing them
would break state compatibility. This is a panel change only: drop the two
controls from `modules/dim/panel/`, leave the parameters at their defaults of
0.40 Hz and 50 %. No state or schema version bump.

It also answers §4's open "worth dimming inactive knobs?" more decisively than
dimming would — with DIFFUSE at its 0 % default these two were two of the three
dead knobs on a fresh insert.

## 8. Deferred to after merge, on the record

**§5 Presets — all seven, never auditioned on any build.** Frosty's call
2026-09-09: correctable after merge, provided it is written down. This is that.

The risk being accepted: preset levels in this suite have drifted before, *all
thirteen at once* across BMO Opto (three) and the Saturator (all ten) —
see `testing-notes/opto-0.2.1-handoff.md` §4. (This section first said all
thirteen for Dimension too, and on the Saturator alone; Dimension ships seven,
and the thirteen spanned two modules. Corrected in review.) The specific unrun checks are whether any
preset jumps in level against Init at the same settings, and whether True Peak
goes over on any of them. Dimension has no output trim and +15.5 dB is
reachable, so nothing downstream catches an over.

Also deferred, and lower stakes:

- **§3 host mechanics** — ~~loading as a BMO Mix Rack slot module, DETUNE
  toggled repeatedly~~ **done 2026-09-10, see below.** Still open: WIDTH
  automated across its range. The DSP equivalent passes offline.

> **2026-09-10 — DETUNE switch checks, in the host.** Frosty, on **ICE QUEEN**
> (desktop), right after PR #4's review:
>
> - **toggles over loud, sustained material** — pass
> - **double-tap** (off and on quickly) — pass
> - **DETUNE On automation**, including in a Mix Rack slot — pass
>
> *"all passed with flying colors."*
>
> **Which build.** ICE QUEEN's only installed BMO Dimension at the time was a
> CI build — 4,203,008 bytes, SHA-256 `10AD74B9…904D06BD` — installed
> 2026-09-09 20:22, 20 s after the CI run for `bc89293` finished. That is
> **before the review fixes**; every build containing them finished after
> 01:04 on 2026-09-10. So these passes most likely cover the **pre-review
> switch**: a hard cut going out (0.49 on a 0.5 tone, 32×) and an instant
> switch-on with the ~15 ms tick (0.18, 11.7×). Neither was audible in real
> use, which is worth knowing in itself. It also means the **8 ms fade-out has
> not been heard**. The same session's *"ear test passes, but i prefer instant
> on"* was then said of a build that was already instant-on. Frosty to confirm.
>
> `frosty-dim-detune-instant-on` keeps the fade-out and makes switch-on
> instant with no tick: 1.00×, against 9.87× on main. (All the ×-figures
> above were measured offline with `measure_dim pass` on ICE QUEEN,
> 2026-09-10.)
>
> **2026-09-11 — the fades, heard: all pass.** Frosty, on ICE QUEEN, in
> Ableton, ran the whole of `dim-testing-checklist.md` §3's switch list on
> **BMO Dimension `710dd46`** (`6774E4CC…`, which contains both the fade-out and
> the instant-on). The Mix Rack slot items used rack `E08B0A5B…`, from the fork's
> `900efdd` DEQ build. The hashes were checked on ICE QUEEN before and after the
> test. *"Dimension all pass."* This settles the ear-review flag. It also
> settles Frosty's instant-on preference, now on the build that has it rather
> than the pre-review one.

- **§4 panel questions**, which are now stale: they described a nine-knob
  layout and it is seven knobs and a switch since RATE and DEPTH went. Re-ask
  them against the current render.
- **§6 light mode** gut-check. Measured at 1.73:1 and unchanged from 0.2.3.
- **§04 b5** has a measurement but no ear verdict: bass with DETUNE on read
  0.0 % anti-phase, so the 40.7 dB depth seen on a 110 Hz *tone* does not
  carry to real bass. Whether it is musically usable down there is unrecorded.
- **The output stage.** No trim, +15.5 dB reachable, every other module has
  one. A design question, not a defect, and it interacts with the preset check
  above.

## 9. Untouched, and still needs ears

Everything the checklist calls "what only ears can answer":

- **§1 the throb** — shimmer or tremolo, on a mono vocal at CENTS 10. **This
  gates the PR.** Record what you heard, with settings, before proposing a fix.
- **§2 ASYMMETRY** — receding, or phasey/hollow.
- **§3** the host-level checks: ~~DETUNE toggled over a quiet passage~~
  (done 2026-09-10 over loud material, see §8), mono track, WIDTH automation.
  The DSP-level equivalents pass in ctest; what is untested is the real host at
  real buffer sizes.
- **§5 presets** — whether the three DETUNE presets separate.
- **meter pass §02** the polarity-null bench in the real host, and §04–06 the
  A/B comparisons. §6 above settles the DSP half of §02 offline; what it cannot
  settle is the host at your buffer size, and the Chorus-Ensemble leg.
