# Host tempo and tail plumbing

Written on AURORA on 2026-09-20. Design only; nothing was changed. Evidence:
`docs/delay/00-repo-conventions.md` §2–4, `docs/delay/10-dsp-spec.md` §7, §9,
read against the sources cited.

## 1. Touch points

| Where | Line today | Change |
|---|---|---|
| `core/dsp/ModuleDsp.h` | 49–64 (`currentGainReductionDb`, `setSolo`) | Two new **non-pure** virtuals beside them: a tempo setter and a tail query, bodies empty / `return 0.0`. |
| `core/product/ModuleEngine.h` | 70–78 `process` | Store the block's tempo; forward it right after `setParams` (line 73), before `process`. |
| `core/product/ModuleEngine.h` | 81–86 `latency()` | A `tailSeconds()` twin, same shape: read `paramSet.readAll` into a local, ask the DSP. |
| `core/product/SingleModuleProcessor.cpp` | 81–95 `processBlock` | Read the playhead once, after `ScopedNoDenormals`, before `engine.process` (94). |
| `core/product/SingleModuleProcessor.cpp` | 47–53 `handleAsyncUpdate`, 56–63 `prepareToPlay` | Cache tail in an atomic next to `reportedLatency`. |
| `core/product/SingleModuleProcessor.h` | 41 | `getTailLengthSeconds()` returns the cached atomic. |
| `core/rack/RackProcessor.cpp` | 456–475 `processBlock` | Read the playhead once at the top; pass the same value to every slot in the existing loop (472–474), under the same `ScopedTryLock`. |
| `core/rack/RackProcessor.cpp` | 399–408 `totalLatency`, 410+ `handleAsyncUpdate` | A `totalTail()` twin; cache it in an atomic when latency is recomputed. |
| `core/rack/RackProcessor.h` | 124, 159 | Tail returns the atomic; declare `totalTail()` by `totalLatency()`. |
| `core/rack/SlotOverflow.h` | 61–62 | **No change** — it never processes audio. |

Both new virtuals have default bodies, so no existing module is edited, no
parameter or spec is added, presets and session state are untouched. The vtable
grows, but every module compiles into the same binary — no external ABI breaks.

Signatures, matching `10-dsp-spec.md` §7 and `11-integration-and-test-plan.md` §2:
`virtual void setTempo (double bpm, bool valid, bool playing) noexcept {}` and
`virtual double tailSecondsForParams (const float*, int) const { return 0.0; }`

## 2. What the data carries

Recommend the minimum: **bpm, valid, playing**. `valid` is `false` whenever the
host gave no playhead, no position, or no bpm — the module must not have to
distinguish those cases.

Left out deliberately:

- **PPQ position** — Dwell is free-running, not beat-anchored (§7). Carrying a
  quantity nothing consumes invites sample-accurate beat alignment, which
  needs loop/cycle handling and a resync policy too.
- **Time signature** — note values are fixed ratios of a quarter note (§7);
  the denominator matters only for bar-length divisions, which Dwell has none.
- **Sample/second position, loop points, record state** — no consumer.

A later beat-anchored module gets its own defaulted virtual, not a wider one.

## 3. Real-time safety

Read once per block on the audio thread: `getPlayHead()`, then
`getPosition()`, then hand the three scalars down. No allocation, no locking,
no atomics — the values live in the call frame and in plain engine members,
written and read only by the audio thread. The rack reads the playhead **once**
and gives every slot the same figures, so two synced modules cannot disagree
within a block.

Fallbacks: no playhead (standalone, offline harnesses), playhead but no
position, position but no bpm → `valid = false` in every case. A stopped
transport is `playing = false`, `valid` still true. The DSP's own policy (hold
last valid bpm; fall back to TIME if none was seen; never flush on stop) is
§7's and stays in the module.

## 4. Tail

Same shape as latency, for the same reason: computed **from parameters**, not
DSP state, so the host can be told before the audio thread catches up
(`ModuleDsp.h:31–34`). Module returns seconds from `tailSecondsForParams` → `ModuleEngine::tailSeconds()`
reads the ParamSet → rack folds the slots → processor reports the cache.

**Rack rule: sum, then clamp.** The chain is serial, so slot *n*'s ringing
output excites slot *n+1*, which rings for its own tail: the worst case is
additive, exactly as latency is (399–408). `max` under-reports and truncates an
offline render. Clamp to 30 s, §9's single-module ceiling, so eight
self-oscillating delays cannot ask for a four-minute flush.

`getTailLengthSeconds()` may be called from any thread, including mid-rebuild,
so it reads a cached `std::atomic<double>` refreshed in `handleAsyncUpdate` and
`prepareToPlay` alongside `reportedLatency` — never walks the slots inline.

## 5. Tests

- A fake `juce::AudioPlayHead` returning a canned `PositionInfo`, installed
  with `setPlayHead`, plus a probe `ModuleDsp` recording the last
  `(bpm, valid, playing)` seen. Assert: values arrive once per block, before
  `process`; a null playhead and a bpm-less position each yield
  `valid == false`; a stopped transport yields `playing == false`, bpm intact.
- Rack case: probes in two slots see identical values in the same block.
- Tail: probe reports a known figure, two slots sum, the clamp holds, and the
  processor's `getTailLengthSeconds()` matches after the async update.
- **Regression:** run an existing module (`util`, `eq`) over a fixed signal
  with and without a playhead installed, `memcmp` the buffers — bit identical,
  their `setTempo` being the inherited no-op. `tests/plugin/TestUtil.h` already
  has the harness.

## 6. Risks and landing order

Risks are small and of one kind: a defaulted virtual is easy to forget to
call. The rack's `ScopedTryLock` early-return (469–470) means a block during a
module swap delivers no tempo — correct, as it delivers no audio either. Hosts
differ on whether bpm is present while stopped; `valid` absorbs that. A nonzero
tail changes offline-render length in every host, so it stays 0 until a module
returns one.

Order, and **yes, it lands as its own small PR before the delay module**:

1. `ModuleDsp` virtuals + `ModuleEngine` forwarding, defaults only — no
   behaviour change, bit-identical output, mergeable alone.
2. Playhead read in both processors; probe tests.
3. Tail folding, caching and the clamp; still 0.0 in practice.
4. Dwell consumes both.
