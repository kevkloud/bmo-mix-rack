# modules/fetcomp — BMO FET

A 1176-style FET compressor. What is here is what a contributor would otherwise
have to re-derive: the decisions that are already taken, the two house rules
this module knowingly breaks, and what the placeholder DSP owes the real one.

The long-form spec is `docs/1176-comp/` — `10-dsp-spec.md` for the topology and
the maths, `11-integration-and-test-plan.md` for the parameter table, the test
plan, the panel and the visual verification. **Read those before changing
anything in here.** Nothing in this module was heard or measured at the time it
was built; the numbers in the groundwork pack are derived, and everything
marked CALIBRATE still needs an ear.

## The state it is in

**The compressor is real and measured; nothing about it has been heard.**
`testing-notes/fetcomp-dsp-2026-09-21.md` (AURORA) has every figure.

`dsp/` is five files and they divide the way the spec does:

| file | what is in it |
|---|---|
| `Calibration.h` | **every** constant `10-dsp-spec.md` marks CALIBRATE, and nothing else |
| `FetCell.h` | the divider law and the per-sample implicit solve |
| `Detector.h` | the linear sidechain, the ratio family, the release, all-buttons |
| `Stages.h` | the static colour: transformer poles, LF core, the two amplifiers |
| `DspCore.h` | the assembly, the oversampler, the dry path, the crossfades |

**`Calibration.h` is the file to read first and the file an ear changes.**
Every value in it is a first-pass number chosen from the spec's own tables or
from the middle of a range the spec states only as a direction, and each says
so in its own comment. Nothing in it is fitted to a unit, because there is no
unit. The ones that decide what this sounds like are the cell's
`lambda`/`q`/`mu` — the character and the whole distance between the two
voicings — and `kRatioThresholdDb`, which sets where the compressor starts and
which every drive figure in the spec rides on.

**Three things in the implementation are not what a first reading of the spec
suggests, and each one was arrived at by measuring the version that was.**
They are commented at length where they live, because each is a trap that
looks correct and measures wrong: the release branch condition in
`Detector.h::ReleaseStage::tick`, the plateau's envelope in
`Calibration.h::kAllButtonsPlateauEnvelopeMs`, and the algebraic rather than
`tanh` curve in `Stages.h::SoftStage`.

Everything around the DSP was already real and shipped-shaped: the parameter
schema is permanent, the panel is the panel, the registration is complete, and
the factory presets set everything but makeup.

**What is still not asserted, and where it is recorded rather than forgotten:**
preset level matching (`presets/FactoryPresets.h`, and the closing note in
`tests/plugin/FetcompTests.cpp`) — those makeup figures want an ear, not a
solver.

## ATTACK and RELEASE are the knob position, and they run backwards

**The parameter *is* the hardware's printed position** — 1 slowest, 7 fastest,
continuous between them — and the DSP maps position to time through the laws in
`params.h`.

The reason is automation. Had the parameter stayed in milliseconds ascending
with only the knob drawn reversed, the host's lane and the knob would move in
opposite directions, and a panel cannot fix that, because the lane *is* the
parameter. It costs nothing elsewhere: position is linear and the law is
exponential in position, so the logarithmic sweep falls out with no skew, which
is why these are `floatParam` and not `logParam`.

**It departs from the one house precedent**, LTV Comp's `attack`/`release`
(`modules/vcomp/params.h`), which are `logParam` in ms ascending. Taken
knowingly: that module models no hardware knob and has no direction to honour.

The consequence is that the number alone says nothing, so the value string has
to carry both — "4 (126 us)". That is what `ParamSpec::textFn` exists for; it
is the first parameter in the suite to use it, it is null everywhere else, and
it is read by `ParamSpec::text`, so the panel, the host's lane and a rack slot
cannot print different words. "us" rather than the micro sign because the two
display faces are licensed individually and live outside the repository, so a
glyph outside ASCII is one this suite cannot promise it can draw.

**`docs/1176-comp/11` illustrates the release string as "4 (234 ms)"** against
the 234.5 ms the law gives. That is the figure truncated where this rounds it,
and the example is an illustration of the shape rather than of the rounding.

## Two house rules this module breaks, both documented

**1. The accent.** `#5489d4` is 16.4° from the utility azure and 20.9° from
LTV Comp's periwinkle, and misses both contrast bands (3.80 dark, 3.09 pale).
It is an owner-approved exception: no blue could have passed the hue rule,
because the azure-to-periwinkle gap is only 37.3° wide. The full note is in
`products/AGENTS.md`'s Accents section. Do not relitigate the arithmetic.

**2. Every switch on this panel lights in the accent.** The table in
`modules/AGENTS.md` gives the module accent to a bypass, a mono and a summing
choice, and `switchAlt` to anything else — and ratio, voicing and oversampling
are all "anything else". `switchAlt` is the utility azure at 198.8°, which is
16.4° from this module's accent, so an "anything else" switch here would be a
second blue nobody could tell from the first. It is the third exception in the
suite after BMO Opto's and LTV Comp's, and the first taken for hue rather than
for a greyscale panel.

**What is *not* an exception:** the accent stays off small text. Legends and
captions that would go through `accentTextOn` take `text1`/`text2` instead,
because E has to be stepped further than any shipped accent and comes back a
visibly different blue. The knob captions keep the suite's convention — a
character knob's caption is the raw accent, not a stepped one, since 0.2.3 —
and were measured on a render rather than assumed.

## The meter is not widened, and the voicing is its border

`ui::DynamicsMeter` keeps `kGrRangeDb = 24` and the needle pins past it. That
is accepted against a 30 dB design target: 24 dB is plenty to read by, and past
it the needle says "a lot" while `currentGainReductionDb()` still reports the
true figure. **BMO Opto's render must stay byte-identical**, which is why the
full-alpha bezel arrived as an opt-in `setBezelAlpha` defaulting to the 0.7 the
class always drew at, rather than as an edit to the literal in `Controls.cpp`.

The voicing is shown as the bezel and nowhere else: the accent for Blue,
**literal black** for Black. That pair was chosen over a neutral grey, Opto's
silver, a white-ish frame and no border at all, because the two states land on
opposite sides of the meter face — the accent sits 2.00:1 above `meterFace` and
black 1.94:1 below it — so they separate by luminance rather than by hue, which
is the failure the suite has already fixed once on its switch colours. The
silver pair collapses to 1.42:1 and is refused.

**The bezel alpha was a gate, and it is settled: full alpha, owner's call on
renders, 2026-09-20 on AURORA.** Both variants were rendered in both voicing
states and both appearances and Frosty picked full. `kBezelAlpha` in
`panel/FetcompPanel.cpp` is the shipped value and the one place to change it;
it now reads `kFullBezelAlpha`, and `snapshot fetcomp out.png ui.bezel=stock`
still renders the rejected candidate without a rebuild. The pair separates at
**5.91:1** measured on the shipped render, against the 5.13 the groundwork
predicted by formula — better than claimed, not worse. BMO Opto was re-proved
byte-identical at `ab3ff3b77116b7a5` / `878cca7b1a80a551` / `88a7653a82c19ae0`,
which is what taking the opt-in setter rather than editing `Controls.cpp` was
for. `testing-notes/ui-pass-fetcomp-2026-09-20.md` sections 2 and 6 are the
record.

## What the panel does not have

No section rules. This is one compressor, the same argument BMO Opto and LTV
Comp make: a rule is a divider and a panel that is a single idea has nothing to
divide. It takes neither shared section and reserves neither.

No threshold knob, no sidechain high-pass, no stereo-link switch. Stereo is
always linked, which is the reasoning LTV Comp records for having no LINK. The
last two could be appended at the end of `specs()` later with defaults that
leave old sessions unchanged; neither can be inserted mid-list.

## Where its pieces live outside this folder

`products/fetcomp/` (three files) · `products/rack/Registry.cpp` and its link
line · `products/AGENTS.md` (identity and accent rows) ·
`modules/CMakeLists.txt` · `tests/CMakeLists.txt` ·
`tests/dsp/FetcompDspTests.cpp` · `tests/plugin/FetcompTests.cpp` ·
`tests/plugin/RackTests.cpp` (registry size and the bank table) ·
`tests/ui/LayoutTests.cpp` · `tools/CMakeLists.txt` ·
`tools/measure/fetcomp/main.cpp` · `tools/snapshot/main.cpp` ·
`scripts/build.sh`. The table in `modules/AGENTS.md` says how each one fails if
it is missed, and most of them fail silently.
