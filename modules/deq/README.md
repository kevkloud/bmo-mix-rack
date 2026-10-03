# BMO DEQ

A parametric EQ whose bands can move their own gain with the signal — a
dynamic EQ — at **zero samples of latency**, with the filter accuracy near
Nyquist that other plugins buy with oversampling (and pay for in latency).

**Status: built, not yet heard in a DAW.** Twelve bands, each with its own
dynamics and mid/side placement, in a panel with two widths: compact (320) in a
rack, full (600) standalone, switched from the bar above the panel. The DAW
pass is `testing-notes/deq-testing-checklist.md`; serial vs parallel can be
compared by ear first with `testing-notes/deq-topology-listening.md`.

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
