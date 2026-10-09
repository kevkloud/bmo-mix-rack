# BMO Linger M3b: the input stage (2026-10-06, on ICE QUEEN)

Branch `frosty-linger-m3b`, on top of the modulation commit `4505ff4`. Built
and measured on **ICE QUEEN**, Release, Visual Studio 17 2022, LTO off, test
targets only. Nothing here has been heard yet.

## What was built

`modules/reverb/dsp/InputStage.h`: a fixed 20 Hz high-pass (one pole), DARKEN
(`inhicut`, one pole, 2-20 kHz when this was written and **1-20 kHz since
2026-10-07**, `5fa2db5`) and the three Reverb EQ nodes, in that order,
on the mid of the input, ahead of both generators (`10` section 2). The dry
path does not pass through it. `10` section 4, "As built in M3b", lists where
it departs from the spec.

## Figures

| what | figure |
|---|---|
| High-pass at 20 Hz, 44.1 to 192 kHz | within 0.013 dB of -3.01 |
| DARKEN at its corner (2, 9, 20 kHz; 1 kHz added 2026-10-07), 44.1 to 192 kHz | -3.01 dB to 4e-14 |
| Running stage against its design, 10 Hz to 15 kHz, three settings, 48 and 96 kHz | within 2.5e-6 dB |
| Through the engine, stage in against stage out: early reflections | within 5.3e-7 dB of the design |
| The same, the tail's direct feed (modulation off) | within 7.3e-7 dB |
| Largest sample step, 97 Hz at -18 dBFS: steady flat / during the move to +12 dB and DARKEN 2 kHz / settled / on the way back | 0.00156 / 0.00542 / 0.00568 / 0.00524 |
| Slowest EQ the schema allows (20 Hz bell, Q 40, +12 dB): above -60 dB re the impulse for | 12 ms |
| The same: exactly zero after | 78.2 s, never through a subnormal |
| MIX 50 %, both faders off, busiest EQ, DARKEN 2 kHz | output is the input, sample for sample |

## What DARKEN does away from its corner

The corner is exact at every rate. The rest of the curve is a digital pole's
and not the analogue one the EQ page draws:

| rate | corner | worst gap to 20 kHz | at | engine at 5 kHz | engine at 10 kHz |
|---|---|---|---|---|---|
| 44.1 kHz | 20 kHz | 0.66 dB | 12.3 kHz | -0.51 | -1.57 |
| 48 kHz | 20 kHz | 0.53 dB | 12.4 kHz | -0.46 | -1.45 |
| 96 kHz | 20 kHz | 0.11 dB | 12.8 kHz | -0.30 | -1.07 |
| 192 kHz | 20 kHz | 0.03 dB | 12.8 kHz | -0.27 | -0.99 |
| 48 kHz | 10 kHz | 1.53 dB | 20 kHz | -1.07 | -3.01 |
| 48 kHz | 2 kHz | 2.58 dB | 20 kHz | -8.49 | -13.57 |
| 96 kHz | 2 kHz | 0.62 dB | 20 kHz | -8.58 | -14.01 |

The page draws -0.26 dB at 5 kHz and -0.97 at 10 kHz for a 20 kHz corner. So
the default at 48 kHz is half a dB darker at 10 kHz than drawn, and the
default differs by 0.46 dB at 10 kHz between 48 and 192 kHz. With a low
corner the gap is 17 to 20 dB down the skirt. Open: draw the engine's law on
the page.

## CPU

`measure_reverb bench <rate> <block> worst`, median of five, before and after
in one sitting:

| | without the stage | with it |
|---|---|---|
| 48 kHz / 128 | 1.285 % | 1.325 % |
| 192 kHz / 32 | 4.990 % | 5.126 % |

The budget is 1.5 % at 48 kHz. Frosty accepted 5.18 % at 192 kHz on
2026-10-03 and said it is not a budget. The same code measured 5.19-5.21 %
without the stage the day before, so the machine moves this figure by more
than the stage costs.

## Tests

New, in `reverb_dsp_tests`, "The input stage: what the room is given": the
rows in the first table, plus bit-identical output at blocks of 1, 16, 127,
512 and 2048 through two moves, and a flat EQ playing the same samples
wherever its nodes sit.

Shown failing with the code broken, all four breaks in one build: the stage
taken out of the engine's path (both engine rows, 12.1 dB off), a one-sample
glide (steps of 0.284 and 0.262), and the flush removed (never zero, and a
subnormal). The fourth break, the flat mix left to the closed form, was
**not** caught: the difference is under a float's precision. The test says
so.

Four early-reflection rows now measure the generators with the stage out
(`renderBare`): tap gain as a DC sum, energy after the span, the 5 ms energy
windows, the DENSITY sweep. With the stage in they read 0.8 to 11 dB off the
table, which is the high-pass and not the generator.

`bus_tests`: both of Linger's rows regenerated with `--print`, old values in
the file. Defaults: mono RMS -18.2164 -> -17.9323 dB. At 0.63: -29.2429 ->
-29.2109 dB. No other row moved and no tolerance was touched.
