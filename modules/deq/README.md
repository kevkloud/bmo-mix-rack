# BMO DEQ

A parametric EQ whose bands can move their own gain with the signal — a
dynamic EQ — at **zero samples of latency**, with the filter accuracy near
Nyquist that other plugins buy with oversampling (and pay for in latency).

**Status: built, not yet heard in a DAW.** Twelve bands, each with its own
dynamics and mid/side placement, in a panel with two widths: compact (320) in a
rack, full (600) standalone, switched from the bar above the panel. The DAW
pass is `testing-notes/deq-testing-checklist.md`; serial vs parallel can be
compared by ear first with `testing-notes/deq-topology-listening.md`.

## Changing a band's shape dips the whole output

A change of SHAPE is not a crossover. The **whole output** -- every band and
the signal under them, not only the band changing -- fades to zero over
20 ms, the band takes its new shape at the bottom, and the output fades back
in over 8 ms. Measured 2026-10-03 at 48 kHz: a 10 kHz tone with a band at
100 Hz changing shape goes to zero, under -3 dB for 19.8 ms and under -20 dB
for 2.8 ms; a 0 dB Bell changed to a shelf dips the same; two bands changed
15 ms apart hold it under -3 dB for 33.2 ms.

The new shape warms up during the fade on a 640 ms record of each band's
input, allocated in prepare() for all twelve bands: 2.95 MB at 48 kHz and
11.8 MB at 192 kHz, per instance. A band changing alone uses all of it;
bands changing together share one band's catch-up work and look back less.
A slow shape still settles after the change: into a +24 dB Bell at Q 40 and
30 Hz, 10 to 12 s to come within -63 dB of an instance that always had it,
which is that bell's own time constant (1.7 s). `AGENTS.md` has the costs
and what is still outside the switch criteria.

## What is here

```
params.h      the 159 parameters, the rack's lane map, the shelf and cut Q caps
Module.*      the ModuleDef the product and the rack both drive
dsp/          the audio path (JUCE-free)
  Filters.h     the matched-Z design, the analogue prototypes and the SVF,
                which live in core/dsp/ (Design.*, Prototype.h, Svf.h) and
                are named into this module here
  Dynamics.h    detector and gain computer
  DspCore.*     bands, M/S, topology, smoothing, the switch crossovers
  AutoGain.h    AUTO: the static curve's broadband level
  DeqDsp.h      the ModuleDsp adapter: parameter values in, DspCore out
panel/        the panel at both widths, the curve, the analyser
presets/      the factory presets (Init only, for now)
reference/    test-only: the cookbook designs and measurement helpers
spec/         the spec as received, its review, and the decisions since
```

## Trying it

```
cmake -B build-dsp -DBMO_DSP_ONLY=ON
cmake --build build-dsp --config Release
ctest --test-dir build-dsp -C Release -R deq
build-dsp/tools/Release/measure_deq cramp        # accuracy against the analogue ideal
build-dsp/tools/Release/measure_deq topology     # how bands combine
build-dsp/tools/Release/measure_deq curve bell 16000 4 12
build-dsp/tools/Release/measure_deq render source.wav out --blind 7   # serial vs parallel, by ear
```
