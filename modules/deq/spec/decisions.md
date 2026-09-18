# BMO DEQ — decisions

The spec (`spec-v0.1.md`) is kept as received. Decisions that change it are
recorded here, newest first, with who made them and what evidence they rest on.

## 2026-09-15 — the UI pass: a dimmed control's caption, and what GR shows

From module 2 of the UI pass on **AURORA**; the measurements and the renders
behind both are in `testing-notes/ui-pass-deq-2026-09-15.md`.

### A dimmed caption stays at alpha 0.4, and the contrast figure is accepted

**Frosty, 2026-09-15.** *"Alpha is fine, legibility is low priority if the
applicable function is disabled."*

BMO DEQ is the only module in the suite that ships with dimmed knobs, and at
Init **six of its eight band captions are dimmed** — `refreshEnablement` dims
the five detector knobs whenever DYN is off, which is every band's default, and
GAIN whenever the band is a cut. `core/ui/Controls.cpp:145` dims with a flat
`ink.withAlpha (0.4f)`.

Measured off the render, against a named ground:

| | ink | on `#efefef` | on `#2e2e32` |
|---|---|---|---|
| enabled caption | `#5ecfc0` | 1.64:1 | 7.19:1 |
| **disabled caption** | `#b5e2dc` / `#426f6b` | **1.23:1** | 2.36:1 |

**1.23:1 is the lowest figure measured anywhere in this suite** — under the
1.72–2.00 band the raw legends spend, and under BMO Tune RT's lime at 1.29,
which is the lowest figure previously accepted. It is recorded here so that it
reads as chosen rather than unnoticed, which is the whole point of this file.

Two alternatives were rendered in both appearances and are on the record as not
taken: a disabled caption dropping to `text2` (2.45 / 4.85, and it makes a
disabled caption *darker* than an enabled one on the pale plate, inverting the
hierarchy), and dimming the knob but not the word (1.64 / 7.19, and on the dark
plate a disabled caption then reads exactly like an enabled one). Raising the
alpha alone cannot clear the band on the pale plate: 1.64 is the ceiling,
because that is what the enabled caption measures.

**This decision is the suite's, not only DEQ's.** `PlainKnob::setKnobEnabled` is
used by this module and nothing else today, and the next planned use is BMO
Dimension's DETUNE scope (`ui-pass-checklist.md`, module 4). It is settled for
that too, and should not be re-opened there.

### The analyser is always on, and has no switch

**Frosty, 2026-09-15.** The 2026-09-12 entry left "where the chooser lives" to
this pass. The answer turned out to be that there is nothing to choose.

An analyser you have to find and switch on is one most people never see, and
the argument for a toggle was the cost of running it -- which the tap already
answers: `AnalyserTap::write` returns immediately unless a panel has enabled it,
and `~ResponseView` hands back a null tap, so a session with no DEQ window open
costs the audio thread nothing. The off state that matters is free and
automatic. That left a switch whose only job was to hide a working display.

So `Analyser::isEnabled` asks whether there is a tap, not whether somebody
turned one on, and `AnalyserButton` is deleted.

**What this leaves open, and it is smaller than it was:** the five-colour
preference is still specified in the 09-12 entry and is not built. Its stated
home was "a right-click on the analyser's own toggle", and there is no toggle
now. `Analyser::setTint` and the five tokens exist and work; what is missing is
somewhere to call it from and somewhere to keep the answer. Neutral is the
default and is what ships until then.

**Nothing about the colour preference is urgent** -- the default was chosen
precisely so a panel that never offers the choice is still correct.

### The DEQ switch is not a true bypass -- for the DSP pass

**Found in the UI pass, 2026-09-15, and deliberately left for the DSP pass at
Frosty's call.** Written here so it is not re-discovered as a surprise.

`modules/AGENTS.md` calls the module switch "the module's bypass" and that is
what `DeqDsp.h:69` does with it: `band.enabled = active && on`, so every band
fades out. **But `gainTarget` is computed outside the `active` check**
(`DeqDsp.h:106`), so:

- **OUTPUT still applies with DEQ switched off.** Bypass with OUTPUT at +6 dB
  still boosts 6 dB.
- AUTO self-corrects, because no enabled bands means `staticBroadbandGain`
  finds nothing to compensate and the figure lands at unity. So OUTPUT is the
  whole of it.

**Frosty's requirement, 2026-09-15: the bypass should ALWAYS be a true bypass.**
It should remove any output gain added, and dim every parameter the way the
dynamics section dims when DYN is off.

The second half is a panel change and could have been done here; it was held
back so the two land together, because a panel that greys itself out while the
module is still changing the level would be worse than today's state, not
better. Whoever takes the DSP half should take both.

Worth checking against the rest of the suite while in there: no other module
has an `active` parameter of this shape, so there is no house precedent for
whether a module bypass passes its own output trim. Deciding it once, for all
of them, is probably the real task.

### Band solo is a right-click held, and sidechain listen waits

**Frosty, 2026-09-15.** The 2026-09-12 entry below settled what solo *is* --
the band's output, momentary, never a parameter -- and left where it lives to
this pass, "when the space is visible". The space is visible and there is none:
the tab strip fills its row at both widths (expanded tabs run x 16..584 in a
10..590 content area; compact tabs are 10..310 exactly), so twelve solo buttons
were never possible and a thirteenth control has nowhere to go.

**So it is a gesture: right-click and hold, on a band's tab or on its node.**
The right button rather than a plain hold, because a plain hold cannot be told
from a slow click. It does not change the selection -- auditioning a band is not
the same as going to work on it -- and it solos only a band that is on, because
soloing one that is off is silence.

**Right-clicking a node that is already being dragged solos it too**, which is
the case that decided how it is built. JUCE does not deliver a second mouseDown
while a button is held (`MouseInputSourceImpl::setButtons`: "ignore secondary
clicks when there's already a button down"), but it does update the button state
and send a drag -- so the gesture exists only in `mouseDrag`'s modifiers. The
obvious implementation, in `mouseDown`, compiles and never fires; there is a
test that fails on exactly that.

The panel clears any solo when it is destroyed. A solo is held by a mouse button
and a window can close while one is down, and nothing saves solo -- so an engine
would otherwise stay soloed with no panel on screen to release it, which is the
support ticket this decision exists to avoid.

**Sidechain listen waits, and the reason is the detector rather than the
space.** `DspCore.cpp` builds the detector with `sidechainFor (s.shape, hz, q,
rate)` -- the **band's own** frequency and Q. DEQ's detector is not
independently tunable, so band solo and sidechain listen are always the same
frequency region and differ only by before-gain-reduction against after. On a
module where the detector can be aimed away from the band -- a 200 Hz band
ducking off 3 kHz -- they answer genuinely different questions and listen earns
a control. Here it does not.

**If a detector frequency is ever added, this comes back with it.** That is the
condition, not "when there is room".

### The GR bar is read from both ends

**Frosty, 2026-09-15**, on rendered candidates: keep the vertical bar where it
is, and **read gain taken away down from the top, gain added up from the
bottom**. Implemented in `d7aba69`.

The fault it fixes, measured on the panel first: one band at 1 kHz, `signal=-6`,
`thr=-30`, `ratio=4`, and only RANGE's sign changed.

| | before | after |
|---|---|---|
| `range=-12` | 88 px of `meterGr` from the top | unchanged |
| `range=+12` | 180 px of well. Nothing | 88 px of `meterGr` from the bottom |
| DYN off | 180 px of well. Nothing | unchanged |

**The second and third rows used to be the same picture** — a band boosting
12 dB drawn identically to a band doing nothing — which is the one thing a
meter must never do. `currentGainReductionDb` was `max (0, -offsetDb)` and
discarded the upward half.

**Both directions run the full height for the full range**, so they share the
track rather than splitting it and neither costs the other any resolution. That
is what decided it against the centre-out bipolar bar, which was also built and
rendered and halves both. It is safe because only one fill can exist at a time:
the source is one band's offset and never a sum, so which end a fill grows from
*is* the sign, and the two can never collide.

**The value is signed now — positive is gain taken, negative is gain added.**
`ModuleDsp::currentGainReductionDb`, `ModuleContext::gainReductionDb` and
`DspCore::currentGainReductionDb` all said ">= 0", and all three said it because
it happened to be true rather than because anything required it. A compressor
only cuts, so BMO Opto and LTV Comp still never return the other sign; a panel
that reads this value and shows only reduction should clamp rather than assume.

`DeqDspTests` pins it with the mirror of the T5 end-to-end case: the same band
with only `rangeDb` flipped, asserting **-10.5** rather than "is negative" — a
sign test passes on any wrong magnitude.

The cell went 46 → 49 px, because "+24.0" measures 2.8 px wider than "-24.0".

### The horizontal bar, for whoever takes the next DSP edit

**Raised by Frosty, 2026-09-15, and not taken now.** A horizontal centre-out
bar, growing one way for a cut and the other for a boost. Measured here so the
constraint travels with the idea rather than being found while drawing it: on
the 600 the shared output switch row has **216 px** free either side of
DEQ/AUTO, which at ±24 dB is 4.5 px/dB and better resolution than the vertical
bar has. On the 320 the same gaps are **76 px**.

So it fits the full panel and not the compact one, and the two views would stop
agreeing about what the GR meter *is* — settle that before it is drawn. It would
also put a meter on the suite's shared switch line, which `checkOutputSection`
pins across every module, making it a suite-layout question as well as a DEQ
one.

## 2026-09-12 — band solo, and the analyser

Both are new since the spec. Both were asked for before the UI pass rather than
after it, which is the right order: solo may move the parameter layout, and the
analyser changes what the panel *is*, so neither can be laid out around after
the fact.

**Band solo: the band's output, momentary** (Frosty, 2026-09-12). Soloing a
dynamic band's output gives the moving version — the band's filtered
contribution with its gain reduction applied live — so on a de-esser you hear
the sibilance actually being grabbed. That covers most of what a sidechain
listen is for.

**Sidechain listen: worth having, not required.** It costs nothing in latency:
the detector's sidechain is its own zero-latency filter on the dry input and
does not tap the band's filter, and nothing in the path can delay
(`latencySamples()` is a `constexpr 0`, protected by block-size invariance).
Its real cost is a second momentary state and a control on a busy panel. Build
solo so listen is a variant of it; decide whether it gets a control in the UI
pass, when the space is visible.

**Both momentary, neither a parameter.** A solo left on in a saved session is a
support ticket, and twelve solo parameters would move the lane allocation for
no automation anyone wants. This makes solo the suite's **first non-parameter
path from editor to engine** — worth building carefully, because Opto and
Dimension will want it.

**Analyser: post-EQ, one drawn tap, two taps in the plumbing.** Post reads as
pre when the bands are off, which covers "what am I working on" at the cost of
having to stop processing to see it — a toggle-and-look, not a comparison.
Building the data path with two taps and wiring one means a pre curve later is
a UI change rather than a DSP change. The tap is a read-only copy into a
lock-free FIFO: no latency, no DSP state touched, and its cost belongs in the
throughput budget. It must not run with the editor closed.

**Analyser colour: a preference with five options** (Frosty, 2026-09-12), and
not the accent by default-only. Measured against the well, `#1b1b1f`:

| option | hex | hue | on well | nearest claimed hue |
|---|---|---|---|---|
| Accent (DEQ's teal) | `#5ecfc0` | 172.0° | 9.13:1 | it *is* the accent — no separation from the curve |
| Orange | `#ef8b4a` | 23.6° | 6.92:1 | 8° from BMO Saturator |
| Gold | `#e8c95a` | 46.9° | 10.57:1 | 5° from Opto's amber state |
| Pink | `#e6949f` | 352.0° | 7.44:1 | 16° from BMO EQ |
| **Neutral (default)** | `#aeb4c0` | 220.0° | 8.25:1 | claims nothing |

**Neutral is the default** (Frosty, 2026-09-12): it collides with nothing, it
never competes with the teal curve in front of it, and a panel that ships in
someone else's colour is a panel that has made a claim on their behalf. The
other four are there for people who want one.

Pink is the true complement of the teal — 352.0° against 172.0° — lifted from
the `#cf5e6d` the complement gives at the teal's own saturation and lightness,
which measures only 4.48:1 and is the faintest thing on the panel. The hue is
the complement's; the lightness is the suite's legibility.

**None of these is an accent**, and the table belongs beside Opto's red and
amber in `products/AGENTS.md` for the same reason: a colour that means
something inside one panel, that never touches a cap, a caption or a header
bar, and that claims no hue for the module. Say so where it is recorded, or a
later module will read DEQ as owning 23.6°.

**How the preference is stored: the `view` pattern.** An expandable module's
view is an attribute on the saved session's PARAMS element, written by
`getStateInformation` and never by the `captureState` a preset is made from
(`core/product/ModuleDef.h`). The analyser's colour and its on/off follow it
exactly: saved with the session, absent from presets, not automatable. The
five are named tokens in `core/ui/Tokens.h` so a theme file can still override
them, which is what every other colour in the suite allows.

**Open, for the UI pass:** where the chooser lives. Five options do not want
five buttons on a panel this busy; a right-click on the analyser's own toggle
is the cheap answer, and "Accent" rather than "Teal" is the right name for the
first option so the whole thing generalises when Dimension or Opto wants an
analyser.

## 2026-09-11 (later) — Frosty, on the first build's renders

**The panel is mockups A and C, not a reading of them.** The first build had
drifted: shape as a row of five switches, small knobs, THR / ATK / REL, a
readout over the curve. Rebuilt to the mockups' own geometry (their rows,
rules and cell order; `DeqPanel.cpp` gives the mockup y for each). Deviations
kept, each for a reason:

- The mockups' M/S amount cell holds the band's **ON** switch (the lean set
  has no M/S amount, and ON had no home in either mockup).
- Lit switches follow the suite's table (`modules/AGENTS.md`): DEQ in the teal,
  everything else -- ON, MID, DYN, AUTO -- in `switchAlt`. The mockups lit them
  all teal, which was a drawing shortcut, not a proposal.
- Compact: the second rule is at 360, not 384, because with values under
  every knob the two dynamics strips need the room the dropped M/S knob left.
- Knob faces are a little under the mockups' at the small sizes: the suite's
  dotted track is 10 px outside the cap, not the mockup's 6.

**Knobs show their values.** Every band knob, both widths; the only module in
the suite that does. Full: the host's text ("2.10 kHz", "-2.0 dB"). Compact:
mockup C's short form ("2.10k", "-2.0"). The readout over the curve is gone.

**AUTO is built.** BMO EQ's rule -- the reciprocal of the static curve's mean
magnitude, 48 log points 20 Hz - 20 kHz -- as parameter 158 (`auto_gain`,
appended; nothing moved). Not compensated: dynamic movement (a de-esser made
up by its own output is not a de-esser) and side bands. Capped at +-18 dB.
`dsp/AutoGain.h`.

**Shelf Q stops at 2** (`kShelfMaxQ`). At or under 2 a shelf is within 0.87 dB
of the analogue ideal everywhere; above it, up to 6 dB out near Nyquist. The
host parameter keeps 0.1-40 (one Q parameter a band, whatever its shape); the
engine and curve run a shelf at no more than 2, and the panel writes the knob
back down to 2 after a user action leaves a shelf above it.

**Init is the only preset** for now, confirmed.

## 2026-09-11 — Frosty

**Two widths.** The panel is compact (320, mockup C) and full (600, mockup A).
**A rack opens it compact; standalone opens it full.** Either can be switched
per instance, and the choice is kept with the session but not with presets.
The switch is **on the host's bar** -- the standalone header and the rack's
slot bar -- not on the panel. Mockups: `panel-mockups.html` (the "BMO DEQ
Panel Options" artifact).

**Rack lanes.** The 32 host lanes a rack slot has go to: output, then bands
1-6 by frequency, gain, Q, threshold and range, then the DEQ in/out switch.
Everything else is off the rack's grid (automatable standalone only).

**Band controls: the lean set, plus direction.** Per band: on, shape,
frequency, gain, Q, placement (stereo / mid / side), dynamics on, direction
(above / below threshold), threshold, range, ratio, attack, release -- 13, and
158 parameters in all (159 once AUTO was appended, above). No continuous
M/S blend; knee fixed at 6 dB; peak
detection. Direction was added after the options offered had left it out; the
spec requires both directions (§5.6).

**Twelve bands.** Supersedes spec A6's 24-band minimum. The engine's fixed
array stays larger (`kMaxBands`), so this is a product limit, set in
`params.h`.

## 2026-09-10 — Frosty

**Identity: BMO DEQ, "DEQ" for short.** BMO DEQ takes over the slot reserved
for BMO Parametric (the teal), as allocated on `main` by PR #8: module id `deq`,
plugin code `Bpar`, bundle id `com.lt3audio.bmodeq`, presets `.bmodeq`.

**No parameter limit for BMO DEQ.** Delivered on `main` as `SlotOverflow`: a
module's first 32 parameters take the slot's host lanes, the rest are held off
the grid.

**Serial band summing. Settled by ear 2026-09-12; it supersedes spec C4
(parallel).** Evidence: `topology-options.md` for the measurements,
`testing-notes/deq-blind-2026-09-11.md` for the listening. Serial is the only
option whose response is its band curves added in dB, and the only one in
which a low cut still cuts under an overlapping boost. Latency and CPU are
identical.

57 blind pairs, seven sources, all seven controls indistinguishable. Every
audible difference ran the way the measurements predicted, and nothing was
reported as a fault on either topology. Round one's preferences for parallel
were all cases of it doing less; round two matched the two for *amount* so
that only the shape of the dynamic catch differed, and four of six pairs were
then indistinguishable while the other two were preferred as serial.

**No user-facing topology switch** (Frosty, 2026-09-12). What parallel was
preferred for is reachable in serial by asking for less — 0.76 dB for stacked
cuts, 0.63 for stacked boosts, exactly for the amount of dynamic reduction —
and nothing serial does is reachable in parallel at all.

`Topology::parallel` stays in `DspCore` for now, for `measure_deq render`.
Deleting it is a separate call, and it costs the ability to A/B this again.

## 2026-09-10 — from the review (`review-v0.1.md`), in the code, not yet ruled on

These deviate from the spec's wording because the wording could not be met or
measured. They stand until the spec is revised to match or overrules them:

- T2 absolute targets gated at f0 ≤ 200 Hz; the rest held by the comparative
  gate and regression ceilings.
- T3's pole invariant stated against the prototype's poles, not the knob
  values.
- T5 overshoot defined as "never past the static target"; timing measured on
  the linear envelope, with release held to the two-stage cascade.
- T6 blend continuity read back from the audio.
- T7 denormals checked deterministically, not by timing.
- The time-constant convention is tau (§12 Q2), matching BMO Opto.
- The SVF structure with matched-Z coefficients (§12 Q1).

