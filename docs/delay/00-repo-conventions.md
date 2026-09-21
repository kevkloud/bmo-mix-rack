# Repo conventions delta for a new delay module

Recon done on AURORA, 2026-09-20. No files modified. General conventions are
in `docs/1176-comp/00-repo-conventions.md` (branch `frosty-fetcomp-groundwork`,
read via `git show`) — reference it, not repeated here. This covers only the
delay-specific delta.

## 1. Reusable DSP already in the repo

- `core/dsp/Oversampler.h` — `Halfband2x` (81-tap half-band FIR, linear
  phase) cascaded by `Oversampler` for 1x/2x/4x/8x (lines 22-254), whole-sample
  group delay by construction (lines 164-173). Reusable for an in-loop
  antialiasing stage; not a delay line/interpolator itself.
- `modules/tune/dsp/SincTable.h` — `SincTable<Taps>` (lines 28-127): polyphase
  Kaiser-windowed sinc, 256 phases + linear interpolation between phases, reads
  a power-of-two ring buffer at a fractional index (`read()`, lines 82-106);
  unity gain at every phase, exact delay at cutoff=1.0. `SincBank` (137-167)
  picks a kernel by read-rate ratio. Closest thing in the repo to a
  production fractional-delay reader (built for pitch shift, but the
  kernel/ring mechanics transfer directly). High reuse value.
- `modules/dim/dsp/DspCore.h`, `DetuneVoice` (lines 61-139) — crossfading
  delay-line pitch shifter: two taps half a window apart, raised-cosine
  crossfade, linear-interpolated read (105-117). Good reference for a
  modulated delay tap and for click-free stage in/out switching (see
  `setParams` comments, 288-329). `AllPassChain` (152-175, 6-stage first-order
  allpass) is a candidate for feedback-loop diffusion/phasing.
- `modules/eq/dsp/DspCore.h` (~106-124) and `modules/sat/dsp/DspCore.h`
  (~106,300) — both keep a `dryDelay` ring sized to the oversampler's latency
  so the dry side of Mix matches the wet side exactly ("a partial blend combs
  and a full bypass fails to null"). Pattern to copy, sized to whatever fixed
  processing latency a delay module reports (not its wet delay time).
- `modules/sat/dsp/Shaper.h` — ADAA shaper. Splits `shape(x) = x + residual(x)`
  and anti-aliases only the residual (line 82, 168-173), carrying the previous
  antiderivative between samples with a midpoint fallback (109-173). Strong
  candidate for a feedback-path saturator in a delay's repeat loop.

## 2. Host tempo / transport

`bmo::ModuleDsp` (`core/dsp/ModuleDsp.h:18-70`) receives only:
`prepare(sampleRate, maxBlockSize, numChannels)`, `setParams(values, count)`,
`process(channels, numChannels, numSamples)`. No tempo/transport/time-signature
anywhere in the interface, and no `juce::AudioPlayHead` reference exists
anywhere in the repo (zero hits in `core/`, `modules/`, `products/`).
`ModuleEngine::process` (`core/product/ModuleEngine.h:70-78`), the only caller
of `dsp->process`, carries no host-time context either.

Tempo sync needs new plumbing at three points: (1) **processor** —
`SingleModuleProcessor`/`RackProcessor` are the only classes touching
`juce::AudioProcessor`; one would call `getPlayHead()->getPosition()` once per
block; (2) **rack** — `RackProcessor::processBlock` would fetch position once
and pass it to each slot's `ModuleEngine`; (3) **module** — `ModuleEngine` and
`ModuleDsp` need a new call (e.g. `setTempo(...)`) alongside `setParams`, since
no existing module has anything like this today.

## 3. Tail length

`getTailLengthSeconds()` is implemented and hardcoded to `0.0` in all three
host-facing processors: `core/rack/RackProcessor.h:124`,
`core/rack/SlotOverflow.h:62`, `core/product/SingleModuleProcessor.h:41`. No
module reports a nonzero tail today; a feedback delay will need a real value.

## 4. Latency vs. wet delay time

`ModuleDsp::latencyForParams` (`ModuleDsp.h:34`) is explicitly computed from
parameter values, not DSP state, for fixed processing delay (oversampling) —
see `Oversampler.h:86-87`, `eq/DspCore.h:106-107` ("reported to the host so
plugin delay compensation can undo it"). `RackProcessor::totalLatency()`
(`core/rack/RackProcessor.cpp:399-408`) sums every slot's `engine->latency()`
(`ModuleEngine.h:80-86`), which calls `latencyForParams`. Nothing subtracts a
"musical" delay. Precedent for keeping intentional time offset out of the
reported figure: `modules/dim/dsp/DspCore.h:56-59` — its 30 ms window is
explicitly not latency, "part of the effect, not a delay through the module."
A delay module must follow the same rule: only fixed processing latency goes
into `latencyForParams`; wet delay time never does.

## 5. Stereo handling conventions

No single pattern. `modules/vcomp` runs per-channel state but is always
linked — "compressed independently is a wandering image" (`DspCore.h:120-121,
144-147,204-206`). `modules/dim` is the mid/side reference: split, process
side only, sum back (`DspCore.h:212-518`), passes through on a mono bus
(391). `modules/eq` keeps a fixed `std::array<EqNetwork,2>`, independent
per-channel, no mid/side (`DspCore.h:110`). A delay (esp. ping-pong/cross-feed)
must pick and document one explicitly.

## 6. `Bdly` reservation

`products/AGENTS.md:149`: `` `Bcmp` compressor, `Bdly` delay, `Brvb` reverb. ``
— in "Reserved for later products (not built, do not reuse)."

## 7. Identity-row pattern

`products/AGENTS.md:9-23`, "The identity table" — **Permanent. Allocate here
before the first build.** Columns: Product | Module id | Plugin code | Bundle
id | Presets (e.g. `BMO Saturator | sat | Bsat | com.lt3audio.bmosaturator |
.bmosat`, line 16). A delay module needs its own row allocated first.

## 8. Accent hue gaps

Existing accents (`modules/*/Module.cpp`, hue approx.): sat `#efa552` ~32°,
util `#7fc98a` ~129°, deq `#5ecfc0` ~172°, BMO FET `#5489d4` ~215° (branch
`frosty-add-bmo-fetcomp`), vcomp `#a2a8ff` ~236°, dim `#d4a4ff` ~272°, eq
`#f08cb4` ~336°. De-esser will also claim one, unallocated as of this recon.

Free gaps, largest first: **~0–15° (red)**, **~55–80° (yellow/gold)**,
**~295–315° (magenta/violet)** — each clear of the crowded 172°–236°
teal-to-blue-violet cluster, leaving room for both de-esser and delay.
