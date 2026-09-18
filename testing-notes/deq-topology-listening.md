# BMO DEQ — serial vs parallel, by ear

**The house protocol for a pass like this is `blind-listening-protocol.md`.**
This file is the argument and the cases; that one is how a blind round is run,
what to check before anyone listens, and the shape of the answers sheet.

**What this settles:** whether serial band summing, chosen on 2026-09-10 from the
measurements in `modules/deq/spec/topology-options.md`, holds up when listened
to. It runs **before there is a plugin**: `measure_deq render` pushes any WAV
through the real DSP in both topologies and writes the results as files to
play in anything. No DAW build, no CI, no Ableton implementation needed.

What a test can settle is settled already. `deq_dsp` holds serial to "the
response is the band curves added in dB" (0.001 dB), "a low cut still cuts
under a boost", and "static bands do not depend on their order" (−200 dB).
What is left is what only ears can answer: **does the difference matter on
real material, and is anything about serial worse?**

## 0. Setup

```
cmake -B build-dsp -DBMO_DSP_ONLY=ON
cmake --build build-dsp --config Release --target measure_deq
build-dsp/tools/Release/measure_deq render <source.wav> <out-folder> --blind 7
```

- **Keep the output folder path short.** Windows limits paths to 260
  characters, and the tool refuses a folder it cannot use rather than write
  nothing.
- `--blind <seed>` names each pair `<case>-A.wav` / `<case>-B.wav` and writes
  which is which to `key.txt`. **Do not open the key until the table below is
  filled in.** Without `--blind` the files are named `-serial`, `-parallel`
  and `-difference`. Use that for the second pass.
- Output is 32-bit float, so nothing clips in the file. The tool prints each
  render's peak; turn playback down if a boost case is hot.
- Use real material, and use the same sources as earlier passes where they
  exist: continuity is most of what makes figures comparable. A vocal (the
  Fuji render, if it has been re-bounced on this machine), a full mix, and a
  kick-heavy loop cover the cases. A file with no low end cannot test case 1;
  one with no sibilance cannot engage case 7.
- **Check the tool's table before listening.** "max GR 0.0" on a dynamic case
  means its detector never engaged on that file (threshold −30 dB on the
  band's sidechain). That pair is then static, and listening to it tests
  nothing about dynamics.

## 1. The cases, and what the numbers say you should hear

Serial and parallel, worst difference from `measure_deq topology`:

| # | case | what differs | expected |
|---|---|---|---|
| 1 | `lowcut-under-shelf` | low cut 80 Hz + low shelf +6 @ 100 | rumble at 20 Hz: −18 dB serial, **−0.5 dB parallel** |
| 2 | `stacked-cuts-12` | two −12 dB cuts at 1 kHz | −24 dB serial, −6 dB **inverted** parallel |
| 3 | `stacked-cuts-6` | two −6 dB cuts at 1 kHz | −12 dB serial, **−52 dB notch** parallel |
| 4 | `stacked-boosts` | two +6 dB boosts at 3 kHz | +12 dB serial, +9.5 dB parallel |
| 5 | `surgical` | −12 @ 2.5 k Q 8, −9 @ 3.1 k Q 8 | up to 6.8 dB between them |
| 6 | `vocal-control` | four separated, moderate bands | **0.25 dB — should be indistinguishable** |
| 7 | `deess-dynamic` | dynamic −10 @ 7 k over static HS +4 @ 8 k | ~4 dB at full excursion |
| 8 | `tamer-dynamic` | dynamic −12 @ 2.5 k over static +3 @ 2 k | ~5 dB at full excursion |

Case 6 is the control. If the pair in case 6 *can* be told apart, something
other than the topology is being heard (a level difference, playback order,
a click at the file start). Fix that before trusting any other row.

## 2. Blind pass

For each case: play A and B, as often as needed, level-matched by the file
(the tool does not normalise, on purpose — level *is* the difference in most
cases). Record which one matches the "expected" column's serial behaviour,
and whether the difference was obvious, subtle or not heard.

| case | heard a difference? | which is serial (guess) | confidence | notes |
|---|---|---|---|---|
| 1 lowcut-under-shelf | | | | |
| 2 stacked-cuts-12 | | | | |
| 3 stacked-cuts-6 | | | | |
| 4 stacked-boosts | | | | |
| 5 surgical | | | | |
| 6 vocal-control | | | | |
| 7 deess-dynamic | | | | |
| 8 tamer-dynamic | | | | |

Then open `key.txt` and mark the guesses right or wrong.

## 3. Sighted pass: is anything about serial *worse*?

With `-serial` / `-parallel` / `-difference` names. The measurements already
say serial does what the curves show. What they cannot say:

- **Cases 4 and 2/3: does serial's full stacking ever sound like too much?**
  Parallel's one real advantage is that stacked boosts compress (+12 and +12
  give +17, not +24). If a stacked serial boost sounds harsh where the
  parallel one sounded right, that is the argument for the hybrid (cut filters
  serial, gain bands parallel), and it should be recorded.
- **Cases 7 and 8: does the dynamic band catch the right amount?** In serial,
  a band's gain reduction is its own. In parallel, it depends on the static
  band beside it. Listen to the `-difference` file: it is exactly what one
  topology does that the other does not.
- **Order.** The tool's last column is serial against serial with the bands
  in reverse order. Static bands are exactly order-independent. Dynamic bands
  differ only while their gain is moving, measured at −86 to −88 dB on test
  material and down to −37 dB with very fast (0.5 ms) overlapping bands. The
  number is printed for your source; nothing to listen for unless it is
  above about −60 dB.

## 4. What the result decides

- **Serial confirmed:** delete `Topology::parallel` and its tests, and
  revise spec C4 to serial. No user switch.
- **Serial's stacking judged too much in real use:** the hybrid is the
  measured fallback. It fixes parallel's low-cut failure exactly (case 1) and
  keeps the soft stacking of gain bands. Say so, and it gets built and put
  through this same pass.
- Anything surprising — a click, a level jump, a case that behaves unlike its
  row above — is a bug report, not a topology result. Note the case and the
  timestamp.
