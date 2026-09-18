# BMO Tune RT — the host pass

**Written on AURORA, 2026-09-16, at the end of the UI pass on this module.**
Stage 4 of `WORKFLOWS.md` names "Tune's handoff" as this module's checklist,
but `tune-handoff.md` is superseded and is about the engine. This is the
host-side list, and it exists mainly to carry **one open question that only a
session in Ableton can answer**.

The panel itself is settled and recorded in `ui-pass-tune-2026-09-15.md`.
What is *heard* belongs to the blind rounds — `tune-blind-round8-2026-09-14.md`
is the latest — and the latency rule (10.62 ms) governs everything.

---

## 1. The open question: does Tune need a note readout?

**Frosty, 2026-09-16: decide this in the Ableton pass, on whether it is
necessary — not on whether it would look good.**

Today the panel shows what you are tuning *to* (the keyboard, Key, Scale,
Pitch Range, Ref A) and how hard (Retune, Vibrato, Relax). It shows nothing
about what it is *hearing*. Every competing tuner does.

**The data exists and the seam does not.** `TuneCore` already computes
`note`, `pitchIn`, `target`, `appliedCents`, `ratio` and `lag`
(`modules/tune/dsp/TuneCore.h:34`). None of it reaches a panel, because
`ui::ModuleContext` has no field to carry it.

What to actually do in the host, in this order:

- Track a real vocal with the plugin open and **notice whether you look at the
  panel at all.** If your eyes stay on the DAW, that is the answer.
- The cases where a readout would earn its place, if any: setting Pitch Range
  on a voice near a range boundary; confirming Ref A is doing something at
  432 or 415; catching the detector on a scooped or creaky entry, where the
  open question is whether it has locked to the wrong octave. That last one is
  a **diagnostic** use, not a performance one, and would argue for a small
  readout rather than a large one.
- If it is not necessary, say so here and the layout stays as it is. **The
  panel no longer needs it to look right** — the section was rebuilt on
  2026-09-16 and the space it would have filled is gone. That is what makes
  this a feature question rather than a layout one, and why it can be answered
  honestly rather than to fill a hole.

**Cost if it is wanted.** A sixth callback on `ModuleContext` and a float out
of `TuneCore`. This is the **third** module in the UI pass to want a context
field — BMO DEQ took a signed `gainReductionDb` and a `sampleRate` — and
`ui-pass-deq-2026-09-15.md` §7 item 9 already flags that as "worth one
decision rather than three". If the answer here is yes, that decision comes
first.

## 2. Host behaviour, while you are in there

Nothing below is known-broken; this is the list a host pass would cover
anyway, and none of it has been done on the shipped build.

- Loads and scans in Ableton, standalone and as VST3. No crash on rescan.
- **Latency reporting.** Tune RT is Live-only and reports **0** to the host
  while running 0.4 ms behind at rest (`params.h`, Frosty 2026-09-11). Check
  Ableton agrees and that delay compensation does not double-count it.
- **Automation.** `Relax` is the display name as of 2026-09-16; the parameter
  id is still `flex`. Confirm the lane reads Relax, and that a session saved
  before the rename still recalls its automation — the id is what a session
  references, so it should, and that is exactly why it is worth one check.
- Retune Speed is a stepped choice list shown in milliseconds. Confirm host
  automation of it lands on steps rather than between them.
- The keyboard's per-note switches are parameters (`note_c` … `note_b`).
  Confirm they automate and recall.
- Preset save and recall from the plugin's own strip, and that the machine-wide
  dark/light preference carries to an open editor within a second.

## 3. What is already settled — do not re-open in the host

- **The lime on the pale plate, 1.29:1.** Frosty kept it with the figure known.
- **The disabled ♭ at 1.27:1.** Settled by BMO DEQ's precedent, 2026-09-15.
- **No rest dot on RETUNE.** Frosty, 2026-09-16.
- **The bottom section's arrangement.** Three layouts were rendered and two
  rejected; see `ui-pass-tune-2026-09-15.md` §5.
