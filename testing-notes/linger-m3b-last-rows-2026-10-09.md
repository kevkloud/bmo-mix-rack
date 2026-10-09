# BMO Linger M3b: the last three test rows (2026-10-09, on ICE QUEEN)

Branch `frosty-linger-m3b`, with `main` (eee69ef) merged in. Built and
measured on **ICE QUEEN**, Release, LTO off, test targets only. These are
`11` section 6's echo density, denormal and end-to-end pitch rows, and they
close M3b's build. No DSP code changed.

## Echo density and mixing time

Normalised echo density (Abel and Huang, 20 ms window) first reaching 0.9,
from the impulse to the window's middle. Each type at its own SIZE, SOURCE
and ATTACK, tail only, modulation off, 48 kHz.

| type | mixes at | at SOURCE 0 | with ATTACK off | Polack's sqrt(V) |
|---|---|---|---|---|
| Room, 12 m | 154.1 ms | 168.1 ms | 129.1 ms | 25.9 ms |
| Chamber, 18 m | 184.9 ms | 191.9 ms | 148.9 ms | 47.6 ms |
| Hall, 34 m | 258.3 ms | 260.3 ms | 218.3 ms | 123.5 ms |
| Cavern, 55 m | 322.5 ms | 327.5 ms | 263.5 ms | 254.0 ms |
| Plate, 22 m | 86.8 ms | 86.8 ms | 86.8 ms | 64.3 ms |
| Ambience, 8 m | 83.4 ms | 129.4 ms | 80.4 ms | 14.1 ms |

**No type meets `10` section 4's target of sqrt(V) ms.** They are over it by
1.3 times (Cavern, Plate) to 6 times (Room, Ambience). `10` section 4 puts
mixing time down to the mean delay per type, which is M4's to voice, so the
figures are printed and pinned and the target is not asserted. ATTACK is part
of the gap by design: it holds the tail back.

Asserted: every type reaches 0.9; none mixes later than its figure by more
than a tenth; SOURCE at its default never mixes later than SOURCE 0, and is
at least 10 ms sooner on Room and Ambience. Plate is the one type SOURCE
does not help (equal to the millisecond).

## Denormals

Flush-to-zero and denormals-are-zero off (where the test can set them), a
0.5 s burst, then a minute of silence, DECAY 1 s.

| | first 5 s (the burst) | slowest 5 s of the silence |
|---|---|---|
| defaults | 52.5 ms | 55.1 ms |
| every filter ringing (Plate, Energy mode, both cuts, a Q 20 bell, DARKEN 1 kHz, ATTACK 65 %) | 67.2 ms | 68.1 ms |

The output is exactly zero for the last 5 s and never a subnormal, both
rows.

## A held note through the tail

A 1 kHz sine at -18 dBFS through the tail alone, its frequency read off the
phase in 50 ms windows for 40 s after 4 s of settling. Windows under a
quarter of the median level are left out of the peak and the RMS.

| modulation | RMS | peak | largest line in the deviation's spectrum |
|---|---|---|---|
| default | 1.68 cents | 11.9 cents | 3.3 % of the power, at 0.225 Hz |
| MOD DEPTH 0.8 ms, MOD RATE 1.2 Hz | 2.93 cents | 17.3 cents | 2.7 %, at 0.275 Hz |
| MOD DEPTH 0.8 ms, MOD RATE 0.35 Hz | 3.00 cents | 16.5 cents | 3.8 %, at 0.15 Hz |

**`11` section 6 asks for a peak within 3 cents on this measurement, and it
is not met.** Each line is held under 3 cents (2.94 measured). The tail is
eight of them summed and heard again on every pass, and the phase of a sum of
paths wanders further than any one path. It cannot be met by bounding the
lines. The other half of the row holds: the deviation has no line in its
spectrum, so it is randomised and not a chorus. Frosty heard the held note on
2026-10-06 and called it wobble "in a good way".

Asserted: the RMS no more than it measured (2.2 cents at the default, 3.6 at
the corners), and no one rate holding a tenth of the power. The peak is
reported and not asserted.

## Each row, shown failing

Two broken builds, sources restored after each and the suite re-run green.

| row | how it was broken | what failed |
|---|---|---|
| no type mixes later than it measured | the input diffusers' gains at zero | Room, Chamber, Plate and Ambience (Room 199 ms). Hall and Cavern stayed inside their tenth. |
| SOURCE mixes Room and Ambience 10 ms sooner | the tail fed the direct signal whatever SOURCE says | both rows |
| the silence is exactly zero, and never a subnormal | the tail's flush removed | both rows, twice each, and eighteen older exact-silence rows |
| the silence never runs slow | the same build | **not this row**: unflushed, the slowest 5 s took 80 and 95 ms against 50 and 61, 1.6 times, and the bound is 8. On this processor subnormals are not the tens-of-times trap the row was written for; the two rows above are what caught the fault. The bound is left where it is, as a backstop on machines where the trap is real. |
| the held note's RMS | the slope bound tripled | the 1.2 Hz corner, at 5.6 cents |
| no one rate holds a tenth of the wander | every line on one square LFO | all three rows, at 22 to 37 % |

"SOURCE at its default never mixes later than SOURCE 0" for the four types
other than Room and Ambience was not shown failing: nothing simple makes
SOURCE slow the tail down.

## Suites

build-dsp 24/24, build-full 48/48 runnable, `reverb_dsp_tests --long`
passes, all builds exit 0, on the merged head.

## CPU, measured again on a quiet machine

`measure_reverb bench <rate> <block> worst`, medians of five, three
interleaved passes, every pass within 0.012 points of the others. ICE QUEEN,
2026-10-09, nothing else running.

| | 48 kHz / 128 | 192 kHz / 32 |
|---|---|---|
| `main` 5e9cf91 (QA's build, `cl /O2`) | 1.085 % | 4.447 % |
| PR at `5fa2db5`, without ATTACK (QA's build) | 1.347 % | 5.342 % |
| this branch, ATTACK forced off (the suite's Release build) | 1.318 % | 5.195 % |
| this branch, as it is (the suite's Release build) | 1.325 % | 5.232 % |

QA's executables and the suite's are built with different flags and differ
by about 0.15 points at 192 kHz on the same code, so each pair is compared
within itself. M3b without ATTACK costs 0.26 points at 48 kHz and 0.90 at
192 kHz over `main`; ATTACK costs 0.007 and 0.037. The busy-day figures in
the earlier notes (5.6 to 6.9 %) were the machine. As built: 1.33 % at
48 kHz / 128 against a 1.5 % budget, and 5.23 % at 192 kHz / 32 against the
5.18 % Frosty accepted for M3a.
