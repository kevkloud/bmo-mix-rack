# BMO FET

A FET compressor: fast, coloured, and happiest when it is
working harder than it needs to.

**It has no threshold.** You set INPUT — how hard the signal arrives — and the
compressor takes as much as that drive asks for. OUTPUT puts the level back.
That is the whole of the gain staging, and it is why the two knobs sit side by
side at the top of the panel.

## The controls

| | what it does |
|---|---|
| **INPUT** | how hard the programme hits the cell, and therefore how much reduction there is |
| **OUTPUT** | makeup, by hand |
| **ATTACK** | 1 is the slowest, 7 is the fastest — the knob runs backwards, like the hardware's |
| **RELEASE** | the same direction: 1 is about a second, 7 is 50 ms |
| **RATIO** | 4:1, 8:1, 12:1, 20:1 — and **ALL** |
| **MIX** | blends the compressed signal back against the untouched one |
| **BLUE / BLACK** | two voicings of the same circuit, shown as the border round the meter |
| **2x / 4x** | oversampling; neither lit is off, which is where it starts |

**ATTACK and RELEASE read as two things at once** — "4 (126 us)" — because the
number by itself is a position on a dial, not a time. The position is what the
host automates, so an automation lane moves the same way the knob does.

**ALL is not a ratio.** It is every ratio button pushed in at once, which the
hardware allows and which does something none of the four do on its own: a
standing amount of reduction even in silence, a very steep slope just past
threshold, and a flattening beyond that. It is the setting for a room mic.

**BLUE and BLACK are the same compressor with different constants** — the two
faceplates the family is known by. They are matched in level, so switching is a
change of colour and not of loudness, and Blue is the more coloured of the two
the harder it is driven. The only thing on the panel that says which one is
running is the border around the VU.

## The meter

IN, GR and OUT, one at a time, and it opens on GR because that is the reading
this module is for. The gain-reduction scale runs to 24 dB and the needle pins
there. That is on purpose: this compressor is designed to stay musical well
past that, and past the pin the needle says "a lot" while the number underneath
is still the true one.

## Starting points

**Vocal Front** — 4:1, driven, a medium attack so the consonants still arrive.
**Drum Room All In** — every button in, both knobs fast, for a room mic.
**Bass Hold** — 20:1 and deep, released slowly enough to ride rather than pump.
**Parallel Glue** — worked hard with MIX pulled back, so it adds weight without
taking the front off anything.

The presets do not set OUTPUT: the makeup figures are solved and measured once
the compressor itself is finished, and a number written before then would be a
guess wearing a measurement's clothes.

## Status

**The compressor is real, and nothing about it has been heard.**

`modules/fetcomp/dsp/` is the FET divider law in a feedback loop, solved
implicitly per sample: the four ratios and all-buttons, a programme-dependent
release, the cell's own distortion, the two voicings, the transformer and
amplifier stages, and Off / 2x / 4x oversampling with a delay-matched dry
path. It measures against every figure the specification derives — the ratio
sag, the first-sample overshoot table, the release detents, the published THD
condition — and `testing-notes/fetcomp-dsp-2026-09-21.md` has those numbers,
taken on AURORA.

What that does **not** mean is that it sounds right. Every constant the spec
marks CALIBRATE is a first-pass value sitting in one file,
`dsp/Calibration.h`, and each one is labelled as a guess rather than a fit.
The two voicings are plausible rather than defensible until a Blue and a Black
unit are measured on one bench. No listening pass has happened.

The presets still do not set OUTPUT: those makeup figures are solved and
measured with an ear, which is the next thing this module needs.
`modules/fetcomp/AGENTS.md` says what is decided and what is still owed, and
`docs/fet-comp/` is the full specification.
