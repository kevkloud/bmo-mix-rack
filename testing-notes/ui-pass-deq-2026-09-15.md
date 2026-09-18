# The UI pass — module 2, BMO DEQ

**On AURORA, 2026-09-15. Branch `ui-pass`, worktree `../bmo-mix-rack-333-ui`,
off `d7aba69`.** Module 1 (LTV Comp) is in `ui-pass-2026-09-14.md` and is
waiting on Leteveon; do not change it. The tools and the before-numbers are in
`ui-pass-render-loop.md`, the triage in `ui-pass-handoff-2026-09-14.md`.

**Two defects fixed, both of them things the panel had been getting wrong since
its first build and nothing had ever measured. One number bigger than the item
that led to it. Seven decisions put to Frosty — four answered the same day.**

| | |
|---|---|
| GR readout clipped to `−12.` | **fixed**, `71d6d01`, and asserted |
| disabled caption at 1.23:1 | **settled** — stays at alpha 0.4, Frosty 2026-09-15. §4 |
| GR bar blind to upward bands | **fixed**, `d7aba69` — taken reads down from the top, added up from the bottom. §3 |
| a horizontal GR bar | raised by Frosty, **not taken**; its constraint measured and left with the next DSP edit. §3 |
| solo and the analyser | open. §6 |
| the 48 kHz response view | **fixed** — `ModuleContext` carries the rate; `snapshot` grew `rate=`. §9 |
| the compact 320 in a rack | open. §6 |

Every settled call is recorded at the call site, in
`modules/deq/spec/decisions.md`, 2026-09-15.

---

## 1. Baselines, and both compact numbers, which nobody had taken

Rendered from this worktree's `build-release`, `signal=-18`, both appearances.
`snapshots/` is gitignored — these are on AURORA only, re-run the tool.

| | before | `71d6d01` | **`d7aba69`, current** |
|---|---|---|---|
| expanded dark | `8b15e62102cad9a6` | `13e5c1b6526f7e37` | **`9478e51947a89a6c`** |
| expanded light | `325623156690ffec` | `6b4aba73320e1d63` | **`d693be037330d695`** |
| compact dark | `35df0fbe474b0551` | unmoved | **unmoved** |
| compact light | `20e67b4acc38959a` | unmoved | **unmoved** |

The compact pair is new: `ui-pass-render-loop.md` §4 records the expanded row
and says the 320 "is a separate baseline nobody has taken yet". It is taken —
and it has not moved once across this whole session, because both commits grew
the GR cell and the compact panel prints no figure in it.

**The rest of the suite has not moved either** — the check every commit here
that touches shared code has to pass. eq, sat, util, dim and ltvcomp rendered
in both appearances and hashed against the `1c5299f` baselines: **ten of ten
byte-identical.**

**BMO Opto is excluded, and that is not the same as passing.** Its render is
not reproducible run to run, so a matching hash from it is a coin flip rather
than evidence — see §9, which corrects the claim `d7aba69` made.

`signal=-18` renders **byte-identically to a bare render** on this module, in
both views. That is correct rather than a broken flag: every band ships off, so
Init is a wire, DEQ has no level meters, and the GR bar is the only thing a
signal could move. To see the meter you have to switch a band on *and* give it
dynamics — see §3.

Largest bare band is **30 px at 536..565** expanded and **20 px at 340..359**
compact, identical in both appearances and unchanged by this session's commit.
DEQ is the two tightest panels in the suite. There is no airiness question here.

---

## 2. Fixed: the GR readout said "−12."  (`71d6d01`)

The gain-reduction bar's cell on the full panel was **36 px** — the 12 px bar
and a margin — and the figure printed under it needs **45.9**. It has been
drawing `−12.` instead of `−12.0` since the module's first build, in both
appearances, and no test looked at it.

**It is the MAKEUP → MAKEU class, one container over.** `checkCaptionFits`
walks `PlainKnob` captions; `checkSwitchLabelsFit` walks `SwitchButton`s and, since
DEQ's first render clipped BELL to "BEL", bare `ToggleButton`s too. The GR bar
is a `Component` that paints its own caption and readout by hand, so all three
checks went past it. Every time this bug has appeared it has been in whatever
container nobody had got round to measuring yet.

So the measurement ships with the fix:

- `GainReductionBar::valueOverflow()` — the widest word the bar can draw,
  against the box it actually has.
- `GainReductionBar::widestValue()` — fixed at the bar's own range, not
  sampled. `DspCore::currentGainReductionDb` returns the deepest **single**
  band rather than a sum, and a band's offset is bounded by its own range
  parameter. A cell sized to the reading somebody happened to see when they
  looked is a cell that clips later. *(It was `−24.0` at this commit; §3 made
  the value signed, so it now measures both ends and `+24.0` wins.)*
- `checkDeqPanel` asserts it at both widths, and **was seen to fail on the
  fault first**: `deq GR bar's widest word ("−24.0") overflows its 36 px cell
  by 9.9 px`.

The compact panel shows no value, so only `GR` is measured there and its 36
stands — both compact renders are byte-identical across the change.

**BMO DEQ also gets `checkTrimKnobHeights`**, which until now ran on LTV Comp
alone. That was the carried-forward item: `removeFromTop` clamps rather than
overflows, so a panel that no longer fits shrinks silently and the layout suite
still passes. DEQ is the other content-dense panel — thirteen controls a band,
twelve bands, a curve and a meter, and the compact half does all of it in 320 —
and OUTPUT is its one trim knob.

---

## 3. Fixed: the GR bar was blind to half of what it meters  (`d7aba69`)

The checklist said "GR bar shows the deepest cut across bands; an upward band
shows nothing. Label or redesign." Measured, then redesigned.

Same panel, same signal, one parameter's **sign** changed:

```
b7_on=1 b7_dyn=1 b7_freq=1000 b7_thr=-30 b7_ratio=4 signal=-6
```

| | before | after |
|---|---|---|
| `range=-12` | 88 px of `#4fb8e8` from the top, then 90 of well | unchanged |
| `range=+12` | 180 px of well. Nothing | 88 px from the **bottom**, readout `+12.0` |
| DYN off | 180 px of well. Nothing | unchanged |

**Rows 2 and 3 were the same picture.** A band moving the signal by 12 dB drew
exactly what a band doing nothing drew, which is the one thing a meter must
never do.

**Frosty, 2026-09-15:** keep the vertical bar where it is, read gain taken away
down from the top and gain added up from the bottom.

Both directions run the **full** height for the full range, so they share the
track rather than splitting it — neither costs the other any resolution. That
is what decided it against the centre-out bipolar candidate, which was built,
rendered at all three states in both appearances, and halves both. It is safe
because only one fill can exist at a time: the source is one band's offset and
never a sum, so which end a fill grows from *is* the sign.

Measured after, at design x 562 (the bar's centre), render rows 966..1145:

| | fill |
|---|---|
| DYN off | none |
| `range=-12` | render 968..1055, **88 px**, from the top |
| `range=+12` | render 1056..1143, **88 px**, from the bottom |

Mirror-exact. `snapshots/_dq-v2sheet-dark.png` and `_dq-v2sheet-light.png`.

**The value is signed now, and three doc comments were wrong about that.**
`ModuleDsp::currentGainReductionDb`, `ModuleContext::gainReductionDb` and
`DspCore::currentGainReductionDb` all said ">= 0" — true of every module in the
suite, but true by accident rather than by requirement. All three now state the
sign and say the same thing: a compressor only cuts, so BMO Opto and LTV Comp
never return the other sign, and a panel showing only reduction should clamp
rather than assume.

`DeqDspTests` pins it with the mirror of the T5 end-to-end case: the same band
with only `rangeDb` flipped, asserting **-10.5** and not "is negative", because
a sign test passes on any wrong magnitude — the trap `OptoDspTests` was written
against.

**The cell went 46 → 49 px, and that is its own small lesson.** "+24.0"
measures 2.8 px wider than "-24.0": the plus is the wider glyph. `widestValue`
measures both ends rather than taking the obvious one, and `checkDeqPanel` was
seen to fail at 46 before the number moved. Assuming which is wider is how the
next "-12." gets written.

**This touched a DSP file and two shared headers, so it had to prove it moved
nothing else.** Every other panel rendered in both appearances and hashed
against the `1c5299f` baselines — eq, sat, util, opto, dim, ltvcomp — all twelve
**byte-identical**. Full Release `ctest` 26/26.

**Still open on this meter, and not raised with Frosty yet:** both fills are
`meterGr`, the azure that means "gain reduction" everywhere in the suite, and
one of them is now gain *addition*. Direction and the readout's sign carry the
distinction; a second colour would carry it harder. Left alone deliberately —
it is a colour call, and the pass has one of those open already (§4).

**The horizontal bar Frosty raised is not taken** and is recorded in
`spec/decisions.md` for whoever takes the next DSP edit, with its constraint
measured: the shared switch row has **216 px** free either side of DEQ/AUTO on
the 600 and **76 px** on the 320, so it fits the full panel and not the compact
one, and the two views would stop agreeing about what the GR meter is.
---

## 4. The headline: a dimmed caption is **1.23:1** on the pale plate

This started as the checklist's carried item — DEQ's raw legend at 1.64:1,
"worse than Dimension's 1.73 and nobody has raised it". It is confirmed
(`#5ecfc0` on `#efefef`, 1.64:1, ΔL\* 17.9) and it is **not the interesting
number**.

BMO DEQ is the only module in the suite that ships with dimmed knobs.
`refreshEnablement` dims the five detector knobs whenever DYN is off — which is
every band's default — and GAIN whenever the band is a cut. So **six of the
eight band captions on a freshly opened DEQ are in the disabled state.**

`core/ui/Controls.cpp:145` dims a caption with a flat `ink.withAlpha (0.4f)`.
Measured off the render, not computed:

| | ink | on the pale plate | on the dark plate |
|---|---|---|---|
| enabled caption | `#5ecfc0` | 1.64:1 | 7.19:1 |
| **disabled caption, today** | **`#b5e2dc` / `#426f6b`** | **1.23:1** | 2.36:1 |

**1.23:1 is the lowest figure measured anywhere in this suite** — below the
1.72–2.00 band the raw legends spend, and below Tune's lime at 1.29, which is
the number Frosty looked at and kept.

The flat alpha is the fault, and it is one-sided. On the dark plate the accent
is *lighter* than the plate so dimming walks it down to a workable 2.36; on the
pale plate the accent is *darker* than the plate, so the same 0.4 walks it into
the plate. And the pale side cannot be rescued by the alpha, because the
accent itself is the ceiling:

| alpha | pale plate | dark plate |
|---|---|---|
| 0.40 (today) | 1.23:1 | 2.36:1 |
| 0.50 | 1.29:1 | 2.94:1 |
| 0.60 | 1.35:1 | 3.59:1 |
| 0.70 | 1.42:1 | 4.34:1 |
| 1.00 (no dim) | **1.64:1** | 7.19:1 |

Three candidates rendered, both appearances, in
`snapshots/_dq-dims-l.png` and `_dq-dims-d.png`:

- **A — as it is.** 1.23 / 2.36.
- **B — a disabled caption drops to `text2`.** 2.45 / 4.85. Clears the band in
  both appearances and is a named themeable token. Rendered, it is the most
  legible and the least honest: at `text2` the word is *darker* than an enabled
  caption on the pale plate, so disabled reads as more emphatic than enabled.
- **C — the knob dims, the word does not.** 1.64 / 7.19. The face, the dotted
  track, the pointer, the ∓ symbols and the value line all still carry the
  disabled state at their own alphas; only the name stays readable. Rendered,
  this works on the pale plate and is too strong on the dark one, where a
  disabled caption becomes indistinguishable from an enabled one.

**The bind, stated plainly, because it is the actual decision.** On the pale
plate the *enabled* caption is 1.64:1 by Frosty's own call — `Controls.cpp:128`
records the 0.2.3 swap that cost it and says "Do not 'fix' it". So a disabled
caption that clears the raw-legend band must be **darker than the enabled one**,
which inverts the hierarchy. Any option that keeps the order caps out at 1.42.
The only way to have both is to raise the enabled caption too, and that reopens
a decision already taken.

### Settled: A stands

**Frosty, 2026-09-15:** *"Alpha is fine, legibility is low priority if the
applicable function is disabled."*

So the 1.23:1 is accepted rather than unnoticed, and it is written down at the
call site — `modules/deq/spec/decisions.md`, 2026-09-15 — with the two rejected
candidates and their numbers, so it reads as chosen.

**And it is settled for BMO Dimension too.** `setKnobEnabled` is used by DEQ and
nothing else today, and the checklist's Dimension item ("dim CENTS when DETUNE
is off — `PlainKnob::setKnobEnabled` exists and is unused suite-wide, this is
its case") is module 4. That module now inherits this answer and should not
re-open it — which also means module 3 is no longer blocked on anything here.

---

## 5. Checked and correct — do not re-find these

Recorded so the next session does not spend the time again. Three of them looked
like faults at 1:1 and were not; each was settled by `scan` or `hash`, never by
looking harder. The habit in `ui-pass-render-loop.md` §6 is load-bearing.

- **The SHAPE dial.** Read at full-panel scale it appears to point at BELL while
  band 1 is a Low Cut. It is not: rendered at all five shapes and hashed, the
  baseline is byte-identical to `b1_shape=Low Cut`, the pointer tracks, and the
  active legend lights in the accent. `snapshots/_dq-shapesheet.png`.
- **A render with any parameter set hashes differently from a bare one, even
  set to its own default.** The header reads `Init *` rather than `Init` —
  correct dirty-preset behaviour, and nothing to do with the panel. It cost a
  detour here. **Compare like with like**: a baseline for a change made with
  `k=v` must itself be rendered with a `k=v`.
- **The GR well is not a filled bar.** At 1:1 it reads as a light slab in dark
  mode. Scanned, it is `#1b1b1f` on `#2e2e32` (and `#d6d6d6` on `#efefef`) — a
  recess, empty, correct.
- **The band-1 node clears the printed `0`.** This was the Opto
  needle-through-the-zero check, done first: at Init the node ring sits right of
  the `0` on the response line with clean plate between them, in both
  appearances. `snapshots/_dq-nodesheet.png`. A band that is off draws a hollow
  ring, one that is on draws a filled node with its range stem — the two states
  are distinguishable.

### The rest dots are right, and they update `rest-dot-finding.md`

Checked against the rule, not the render: every DEQ knob whose default is
mid-sweep shows a dot under its own pointer (GAIN and OUTPUT at 12 o'clock,
Q at 32.7%, THRESH 60%, RANGE 37.5%, RATIO 23.1%, ATTACK 51.5%, RELEASE 53.1%).

FREQ is the one that varies, because its default is the band's own spread
frequency. **Bands 1 and 12 show no rest dot at all**, and that is the
suppression rule firing correctly — band 1's 30 Hz is 5.9% along a log sweep,
10.6° from the `−`, against a threshold of 11.8° at that knob's track radius;
band 12's 18 kHz is 98.5% and 1.9° from the `+`. Bands 2–11 all show one.
Rendered and confirmed at 1, 2 and 12: `snapshots/_dq-restdots.png`.

**`rest-dot-finding.md` §5 needs one line changed.** It says the nearest thing
to a collision that is not one is Dimension's RATE at 20.8 px clear, and that
"a control defaulting to 2–3% of its range would sit inside it. There is no such
control today." There is now, and it post-dates the note: DEQ band 1's FREQ
defaults to 30 Hz, which is *not* its minimum of 20, and is suppressed anyway.
The panel marks no rest position for a control that has one. That is the
threshold working as specified rather than a defect, but the note's claim is
stale and should not be read as current.

---

## 6. Blocked on Frosty

§3 and §4 were put to him on 2026-09-15 and both are answered. These three were
not, and they are the reason **BMO DEQ is not complete**. Ranked by what each
one costs to leave alone.

1. **Solo and the analyser.** Confirmed exactly as the checklist has it. Both
   are in the engine and tested — `DeqDsp::setSolo`, `DeqDsp::analyser()`,
   `DspCore` carries `AnalyserTap pre, post` and an atomic solo band — and
   `modules/deq/panel/` contains **not one reference to either**. Meanwhile
   `spec/decisions.md` records them in detail as settled: momentary solo, the
   suite's first non-parameter control, post-EQ analyser, a five-option colour
   preference, a right-click on the analyser's own toggle. Wire them in this
   pass, or say in the decisions file that they wait. Shipping a decisions file
   that reads as if they exist is the one option that is not defensible.
2. ~~**The response view draws the 48 kHz design whatever the rate.**~~
   **Fixed** — see §9. `ModuleContext` carries the rate now.
3. **The compact 320 in a rack.** Rendered next to BMO EQ and BMO Util, both
   appearances (`snapshots/_dq-rack-dark.png`, `_dq-rack-light.png`). It holds
   its width and reads as its own module. Two things to look at rather than
   fix: the GR bar reads as an unexplained pale slab at that size with its `GR`
   flush to the panel edge, and §4's dimmed captions are at their worst here,
   because a rack is where a panel gets glanced at rather than read.

---

## 7. Next steps — what is unaddressed, and in what order

Written for Frosty to add to. **Nothing below is started.** The three in §6 are
his and are not repeated here; these are the ones that have an owner as soon as
a call is made.

### On BMO DEQ, before the module can be called done

| # | what | size | why now |
|---|---|---|---|
| 1 | **Solo and the analyser** (§6.1) | large | It is the only open item that changes what the panel *is*, so everything else on this module is provisional until it lands. `spec/decisions.md` currently reads as if both shipped |
| 2 | **The GR bar's colour** | small | Both fills are `meterGr`, the azure that means "gain reduction" across the suite, and one of them is now gain *addition*. Direction and the readout's sign carry it; a second colour would carry it harder. LTV Comp hit the same problem and took `meterGrWarm`, so there is a precedent and a spare token |
| 3 | **The response view's sample rate** (§6.2) | medium | The only item here a user meets in normal use: at 44.1 or 96 k the drawn curve is up to 1 dB out in the top octave. Below item 1 only because that one changes the panel's shape and this does not. Needs the rate through `ModuleContext` -- the same widening §3 just did for the GR value |
| 4 | **`GR` flush to the panel edge on the 320** | small | Noticed in the rack render. The caption sits hard against the right margin at compact width. Cosmetic, one number |
| 5 | **DEQ's teal at 1.64:1** | — | Confirmed, and the worst raw legend in the suite. It is a *suite* question, not DEQ's, so under the one-module rule it waits until it can be asked as one. Do not re-raise it here |

### Suite items this module surfaced, for after the pass

| # | what | size | note |
|---|---|---|---|
| 6 | **Contrast assertions** | medium | `docs/ui-workflow-brief.md` §4, still the cheapest open item in the brief. §4 above changed what they have to say — see below |
| 7 | **Nothing tests a rest dot** | medium | `rest-dot-finding.md` §5 has said so since it was written, and its "no control defaults to 2–3% of its range" claim went stale on this module |
| 8 | **A horizontal GR bar** | medium | Frosty's, 2026-09-15. Recorded in `spec/decisions.md` with its constraint measured; goes with whichever DSP edit comes next |
| 9 | **`ModuleContext` is being widened one field at a time** | — | §3 widened `gainReductionDb`, item 3 wants the sample rate, and Tune's empty middle wants a pitch readout. Three modules now want something from the context that is not in it. Worth one decision rather than three |

**On item 6 specifically, because §4 changed it.** The assertions are a pure
function of the tokens and the alpha and need no rendering, so the only reason
they are not written is that the floor they assert was the thing §4 was
deciding. Now that 1.23:1 is **accepted**, a blanket "every (ink, ground) pair
clears X" would be a red suite rather than a guard. Two honest shapes, and the
second is better:

1. Set the floor below 1.23 and catch only what is worse. Cheap, and nearly
   useless — nothing in the suite is worse.
2. Assert per pair against a table with the decision beside each figure: raw
   legend ≥ 1.72 except DEQ's 1.64 and Tune's 1.29, disabled caption ≥ 1.23.
   Then a failure means a number moved *from what was signed off* rather than
   from a generic minimum, and adding a row means naming who agreed to it.
   That is `OptoDspTests`' "assert absolutes, not comparisons" applied to
   colour.

### Housekeeping

- **Nothing pushed. 27 commits on `ui-pass` locally**, CI untouched this pass.
  A round trip is ~22 minutes and the concurrency group cancels an in-progress
  run on the same ref, so this wants batching — and `d7aba69` is the first
  commit of the pass to touch DSP and shared headers, which is the kind of
  change worth getting a CI opinion on before it goes much further.
- `snapshots/` is gitignored, so no render in this pass travels with the branch.
  Re-run the tool.

---

## 8a. The analyser — half built, inert, and where to pick it up

**Paused mid-way on Frosty's call, 2026-09-15. It is committed deliberately
rather than left in the working tree**, after a `git stash` in this session
briefly took an hour of uncommitted work out from under the build. It compiles,
every test passes, and it draws **nothing**: the analyser defaults off, so DEQ's
four baselines and the other five panels' ten renders are all byte-identical
across it. Nothing here is load-bearing yet.

**The good news that changed the size of this job:** it is panel work. Both
processors already populate `ModuleContext::setSolo` and `ModuleContext::analyser`,
`DspCore` already carries the solo band and two `AnalyserTap`s, and the tap is
already prepared at 0.35 s per rate. No DSP needs writing.

### Done

- **Four colour tokens** in `core/ui/Tokens.h` — `analyserOrange`, `analyserGold`,
  `analyserPink`, `analyserNeutral`, registered as themeable, with Frosty's
  2026-09-12 table and the reasoning in the doc comment. The fifth option is the
  module's own accent and correctly has no token.
- **`modules/deq/panel/Analyser.h`** — the FFT (4096, Hann, 11.7 Hz bins at
  48 k), per-bin fast-up/slow-down smoothing, and `buildPath`, which walks
  **pixel columns rather than bins** because a log axis leaves gaps below
  200 Hz and aliases above 5 kHz if you walk bins. It maps Hz through the
  view's own `xFor`, so the spectrum cannot drift from the grid it sits on.
- **`AnalyserButton`** — the toggle, with the five-option chooser on
  right-click, per decisions.md.
- **`ResponseView`** owns the analyser, drives it from its timer, and draws the
  shape behind the grid at alpha 0.28.
- **`DeqPanel`** hands it the context's tap, which is what enables it.

### Left

1. **The button is not placed.** `AnalyserButton` exists and is never
   constructed or added to `ResponseView`. That is the next thing to write, and
   it is why nothing is visible.
2. **Session state.** On/off and the colour must follow the `view` pattern —
   saved with the session, absent from presets, not automatable. The pattern is
   `SingleModuleProcessor::getStateInformation` writing `kViewAttribute` onto
   the PARAMS element, and the rack's per-slot equivalent. Both processors and
   `ModuleContext` need a small generic accessor pair; **do not** give `core` a
   DEQ-specific `analyserOn()`.
3. **No render has ever shown it.** Nothing here has been looked at. Enable it
   and render with `signal=` before believing any of the above.
4. **Decide the default.** Off today, because that is what makes this commit
   inert. Whether a fresh DEQ opens with the spectrum on is Frosty's, and it is
   the one thing here a user meets without being told.

### The one design decision taken, and why

**The toggle goes in the well, not on the output switch row.** `DeqPanel.h`
states that every control on this panel changes the sound — it is the stated
reason the compact/expanded switch lives on the host's bar and never here. The
analyser changes nothing anyone hears, so putting it beside DEQ and AUTO would
quietly make that sentence false. A display control belongs on the display.

---

## 8. Where this leaves the pass

**BMO DEQ is not complete.** The three items in §6 are Frosty's, and the first
of them changes what the panel is. Leave the module open.

**BMO Tune RT, module 3, is not blocked.** §4 is answered and settles
Dimension's inherited copy of the same question; §3 is done. Nothing on this
module holds module 3 up.

*Everything above measured on **AURORA**.*

---

## 9. Fixed: the curve was drawn at 48 kHz whatever the rate

`ResponseView` built its `DesignGrid` from a hardcoded `kDisplayRate = 48000.0`,
and had no way to do otherwise: a panel sees `ModuleContext`, and
`ModuleContext` had no sample rate in it. The curve is evaluated from the same
matched-Z design the DSP runs, and that design is rate-dependent by
construction, so the drawn response and the audible one parted company at every
rate that was not 48 k.

**This could not be fixed inside `modules/deq/`.** Five files outside it move,
all additive, and no other panel reads any of it:

| file | what |
|---|---|
| `core/product/ModuleEngine.h` | publishes the rate it was prepared at, atomic |
| `core/ui/ModulePanel.h` | `ModuleContext::sampleRate`, polled, 0 = not prepared |
| `SingleModuleProcessor.cpp`, `RackProcessor.cpp` | fill it in |
| `tools/snapshot/main.cpp` | `rate=<Hz>`, or none of this can be rendered |

Polled rather than read once in the constructor, because a host can re-prepare
a plugin with its editor open — change the device rate, or render offline at
96 k with the window up — and the curve has to follow it there.

**`rate=` had to exist before the fix could be checked**, which is why a tool
change rides along. It is read ahead of the flag loop because `prepareToPlay`
happens before it, the same out-of-order handling `theme=` already needs, and
the 1 kHz test tone's phase increment now follows it too — otherwise `rate=`
would quietly move the tone as well as the rate and a meter reading would be
answering a different question from the one asked.

### What it measures

Curve row at render column 1050, band 11 as a high shelf at 16 kHz, +18 dB,
Q 1.4:

| rate | rows |
|---|---|
| 22 050 | 270..274 |
| 44 100 | 335..337 |
| 48 000 | 335..337 |
| 96 000 | 333..336 |

**Read these honestly.** 22 050 is the proof the rate is plumbed at all — 65 px
away, and no amount of coincidence puts it there. 96 k moves 2 px. **44.1 and
48 k measure identically at this column**: their difference is real but
sub-pixel here, so the fix is not visible at the rate most people work at, and
the checklist's "up to about 1 dB in the top octave" is a dB claim that this
render neither confirms nor refutes. What is now true is that the curve is
drawn at the rate the module is running at, instead of at a number.

- `rate=48000` and no flag at all hash identically (`f7a588b54e043285`), so the
  flag is not a silent behaviour change.
- DEQ's four Init baselines are **unmoved** by this commit, which is what a
  48 kHz default has to mean.

### A correction to §3's verification

§3's commit claimed "eq, sat, util, opto, dim and ltvcomp all twelve
byte-identical". That was true of that run and is **not sound evidence for
Opto**: rendered four times here, `snapshot opto` comes back as two alternating
images (`5a0d3617a9160788` and `784ca005ff68a747`, the latter being the recorded
baseline). So the run happened to land on the baseline.

The other five are reproducible and their half of the claim stands. **Opto is
not this pass's module and was not touched** — this is recorded only because a
byte-identical check is the evidence every commit here leans on, and for one
panel that evidence is a coin flip. `ui-pass-2026-09-14.md` records this
non-reproducibility as fixed in `7ae1b51` by settling to convergence; whatever
that fixed, it is back or was never complete. For whoever owns BMO Opto.
