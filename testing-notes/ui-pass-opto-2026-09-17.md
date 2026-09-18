# The UI pass — BMO Opto, module 5

**On AURORA, 2026-09-17.** Branch `ui-pass`, worktree
`../bmo-mix-rack-333-ui`, starting from `9ba632a` after Dimension. Three
commits, all local, nothing pushed and no CI. Frosty drove it from his phone
over Remote Control; every call below was made on renders in both
appearances.

Read `ui-pass-render-loop.md` for the tools.

---

## 1. Where it ended

Frosty was happy with the whole panel except the meter's scales. Nothing but
the two scale tables moved: no layout, no colour, no caption.

| | dark | light |
|---|---|---|
| before, `signal=-18` (re-taken with `bfbafc4`) | `f20ae990919ef160` | `b45bd44ceedfcb7f` |
| after, `signal=-18` | `ab3ff3b77116b7a5` | `878cca7b1a80a551` |
| before, bare | `cc9e6031480ca04c` | `d6a10e5409ca3986` |
| after, bare | `910e41b64ed45b5c` | `2394065f3f2b8a74` |
| after, `ui.meter=GR` | `88a7653a82c19ae0` | `03bc225b30b17611` |

Largest bare band, dark: **48 px at 82**, unchanged. The six other panels hash
as before.

## 2. The opening state, first

Frosty's carryover: LTV Comp's gate had repeated Opto's needle-through-the-zero
exactly, so check the opening state first. It was clean before anything was
touched: at rest on GR the needle parks hard left and the printed `0` sits
outside the arc, above it. The fix is `kLabelRing` in `DynamicsMeter::paint`,
figures outside the arc for any scale. It is still clean after: both scales now
print their first figure at the park point (GR `0`, VU `-24`), and both clear.

## 3. The render tool, before any of it (`bfbafc4`)

`ui-pass-deq-2026-09-15.md` §9 left this for whoever owned Opto: `snapshot opto`
with a signal was not reproducible, so a matching hash from it proved nothing.
Confirmed here, 6 renders of the baseline and 2 of a third image in 8.

`7ae1b51` had settled meters to convergence and assumed a constant feed. There
were three faults:

- `Meter` publishes the **RMS of the last block**, and 512 samples of 1 kHz at
  48 k is 10.67 cycles, so every block read a little differently. The block is
  now whole cycles (480 at 48 k, 441 at 44.1 k).
- An opto is **still moving 240 ms into a tone**, which is all the settle
  lasted. Ten seconds of tone now pre-roll through the DSP first.
- **24 ticks is too few for a hash.** One extra tick moves the needle tip about
  0.02 px, invisible and still enough to change anti-aliased bytes. Now 64.

After: 12/12 identical in each of four Opto states. Old tool against new,
eq/sat/util/dim/ltvcomp are byte-identical bare and with a signal; only DEQ
with a signal (its analyser draws the tone) and the rack with a signal (one
anti-aliased column in LTV Comp's slot) move. Hand-ticking the timers was tried
and cannot work from the tool: the meters inherit `juce::Timer` privately.
A metered render now takes about 2.6 s.

## 4. GR scale (`30443bf`)

Frosty: "the 3–12 range has some wonkiness about it." Measured: the ticks
above 6 sat at 9, 15, 21 with gaps of .095, .110, .080, .075, .092, .088 of
the sweep, widest at 9..12.

| | above 6 | verdict |
|---|---|---|
| A | log curve continuing 0..6 | a hole after 6, 18 and 24 nearly collide |
| B | one log curve, whole dial | the same, milder |
| C | even 3 dB gaps (.09) | clean; recommended; the tick rhythm halves at 6 |
| D, E, F | C plus 1 dB ticks to 12 / to 24 / to 12 then 2 dB | a comb denser than 0..6 beside it |
| **G** | **a tick every 2 dB at .06** | **taken** — Frosty's own variant of E |

G's 2 dB gap is exactly the 5-to-6 gap, so the arc carries on past 6 at the
spacing it arrived with. 0..6 is untouched; 12 moves .665 → .640.

## 5. IN/OUT scale (`db91b67`)

Measured before: gaps .10, .17, .14, .09, .08, .09, then .05, .06, .07 up to 0
and .05 after — widening toward 0 and snapping narrow above it.

| | | verdict |
|---|---|---|
| H | true VU law, 0 VU at .71 | low end crowds, top sparse |
| J | 5 dB ticks, then an even 1 dB ruler from -7 | recommended at first |
| K | GR mirrored exactly, -21..+3 | right shape; prints -21, -15, -9, -3 |
| **L** | **GR mirrored, from -24** | **taken** |

Frosty asked for -24 and GR's arc and spacing. GR is read near its start and a
VU near 0, so the shape is **mirrored**: GR's 0..5 placement becomes -2..+3,
then a tick every 2 dB to -24 at .055 (GR's is .06 — the cost of 3 dB more than
an exact mirror holds). Printed: 3, 0, -6, -12, -18, -24.

Consequences, stated to Frosty before the call: 0 VU moves from .85 to .735 of
the sweep, so the same signal reads further right; silence parks at -24 VU
(-42 dBFS) instead of -30.

`ui_layout_tests`' `checkScale` passes unchanged: ends at +3, strictly
increasing, inked figures at least .10 apart.

## 6. Open

- Nothing on Opto. The checklist's settled items stand as they were.
- The raw-legend contrast question is still the suite's, not Opto's (Opto is
  2.00:1, joint best).

## 7. Next

Frosty's order for the 09-05 three: **BMO Util, then the Saturator, then BMO
CEQ.**
