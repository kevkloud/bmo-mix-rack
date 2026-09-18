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
dsp/          the audio path (JUCE-free)
  Prototype.h   the analogue filters every band is measured against
  Design.*      matched-Z coefficient design
  Svf.h         the filter structure that runs them
  Dynamics.h    detector and gain computer
  DspCore.*     bands, M/S, topology, smoothing
reference/    test-only: the cookbook designs and measurement helpers
spec/         the spec as received, and its review
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
