# BMO Linger — groundwork pack

Spec, theory, math and test direction for an algorithmic reverb module. No
implementation code here; the dev team writes it from this pack. Assembled on
AURORA, 2026-09-20/21.

**The schema, the type list, the panel and three shared-code changes have since
been built** on AURORA, on `frosty-add-bmo-linger`. `modules/reverb/params.h`
and `modules/reverb/AGENTS.md` are the source of truth for what exists; this
pack is the reasoning behind it, reconciled to the code on 2026-09-22. **The DSP
is a marked placeholder and nothing has been heard** — not one setting.

## Decided

Display name **BMO Linger** · module id **`reverb`** · bundle
**`com.lt3audio.bmolinger`** · presets **`.bmoreverb`** · plugin code **`Brvb`**
· accent **`#e694e0`** · BMO line. Permanent from first ship. The id and the name
differ deliberately, as `deesser` is to BMO Defang.

The thesis: Reference B's sound with Reference A's functionality, and an ER
section good enough to use alone. Third-party products appear only under neutral
labels.

## Index

- [00-repo-conventions.md](00-repo-conventions.md) — reverb-specific delta: reusable code, what the host gives a module, free accent hues.
- [01-reference-software.md](01-reference-software.md) — the two reference plugins, and what to take.
- [02-reference-hardware.md](02-reference-hardware.md) — classic hardware reverbs; a shortlist of characters.
- [03-early-reflections.md](03-early-reflections.md) — what engineers do with early reflections, and the requirements.
- [04-design-approaches.md](04-design-approaches.md) — late-reverb architectures and ER generators, with costs.
- [05-er-psychoacoustics-citations.md](05-er-psychoacoustics-citations.md) — citation dossier and folklore table.
- [10-dsp-spec.md](10-dsp-spec.md) — 8-line FDN tail, image-source taps into an allpass-free diffuser, density, six types, budgets.
- [11-integration-and-test-plan.md](11-integration-and-test-plan.md) — identity, shared-code changes, accent, parameter order, panel split, tests, milestones.

## Open decisions

1. ~~**Accent, and who gets violet.**~~ **Settled 2026-09-21 on AURORA: Linger
   takes `#e694e0`** (V4, the window centre), chosen by Frosty from a rendered
   proof sheet rather than from hex. Linger spends the only admissible arc, so
   BMO Dwell needs a different hue — see 11 §3 for the arithmetic and for the
   Tune RT omission in Dwell's own conventions doc.
2. ~~**Thirty parameters on one module.**~~ **Settled 2026-09-21: thirty, with
   two spare lanes of a slot's 32 — and it is not the thirty it started as.**
   The control-set trim cut six — `attack`, `decayshape`, `damplofreq`,
   `damphifreq` (the owner's call), `ershape` and `prelink` (Claude's,
   accepted) — and **none of them left the design**: each is now a constant in
   the per-type block, so the acoustics in 10 are untouched and only their
   status as user controls changed. Every cut was a float or a bool, so all six
   re-append safely if listening disagrees. The Reverb EQ then spent six of the
   eight lanes that bought, the same day (decision 3), so the count is back to
   thirty. 11 §4 is the authoritative table, 11 §4a the record of the cut. The
   type list is append-only and settled: Room, Chamber, Hall, Cavern, Plate,
   Ambience — **Large Hall was cut and Cavern took index 3**. Still open:
   `inhicut` (IN HI-CUT) is marked "owner confirm" — accept or cut it before
   first ship (11 §4d), now at index 25.
3. ~~**The Reverb EQ becomes three parametric nodes.**~~ **Built 2026-09-21/22,
   committed and green.** The four shelf parameters *became* low shelf · bell ·
   high shelf — the change was purely additive, no id changed meaning — each
   with FREQ, GAIN and Q, shapes fixed with no selector anywhere. The schema is
   **30**, two lanes spare. The filter DSP was reproduced byte-identically from
   the unmerged BMO Defang branch, never stacked on it, with the five blobs
   hashed in `modules/reverb/AGENTS.md`. **Two things differ from what was
   approved:** `eqfilter` is a **four-position choice** (`Off` / `Lo Cut` /
   `Hi Cut` / `Bandpass`) and not a bool, because taking a tail's bottom off and
   taking its air off are separately useful — and **that count is permanent at
   first ship**, since a choice normalises as index/(n−1); and `eqhifreq` was
   widened from 1000–2100 Hz (1.07 octaves, inherited rather than chosen) to
   **1 kHz–20 kHz, opening at 6 kHz**, so the three nodes now open at
   200 Hz / 1 kHz / 6 kHz. 11 §4c.
4. ~~**Three shared-code changes.**~~ **Two of the three shipped in v1 on
   2026-09-21.** Tail-length reporting is in, with a 30 s clamp at the module
   *and* at the rack, which sums its slots rather than maxing them. **Mono-in to
   stereo-out is in** — Frosty's call, reversing the deferral: it is a
   bus-contract change in both processors, not a DSP one, and BMO Dimension's
   mono path cannot be reused for it (Dimension early-returns on a mono bus by
   design). Host tempo stays out of v1 and should land once, byte-identically,
   with BMO Dwell.
5. ~~**The panel split.**~~ **Settled 2026-09-21 and rebuilt 2026-09-22: a paged
   handheld, one width, 380 px.** `ModuleDef::expandedWidth` is 0 and the module
   is not expandable; three columns, three pages **EARLY / TAIL / EQ** keyed
   `ui.page=` (the third was TONE and `tone` is now refused, not aliased), and
   no speaker grille. **The rebuild:** the screen is **258 px** and its top
   26 px is the page menu, drawn inside the display — the three round page keys
   are gone from the plate; a **26 px segmented row** under the bezel carries
   `ermode` on EARLY and LOW / MID / HIGH (UI state, `ui.node=`) on EQ, and
   nothing on TAIL, where its absence means there is nothing to sub-select; the
   **persistent row is dissolved**; one FREQ / GAIN / Q set is repointed by the
   node segments; FILTER is a legend ring (OFF / L / H / B); and **ER, REVERB
   and MIX are faders**, which is what "two absolute faders" has meant since the
   groundwork pack, with TYPE over DECAY in the strip's fourth column. WIDTH
   still sits on TAIL. 11 §4e. **Do not reintroduce a second width** — a fourth
   page is what the shape is for.
6. **Era colour is not in v1.** Each type reserves the fields so a later
   voicing switch changes no ordinals.

## Known soft spots

- Nothing public documents Reference B's internals; its character is fitted by ear (CALIBRATE).
- The flagship hardware's control ranges were not confirmed from a manual.
- The "ER only, tail off" depth technique is second-hand; no primary source found.
- No CPU budget exists in the repo; 10's is an estimate, and this is the rack's heaviest module.
