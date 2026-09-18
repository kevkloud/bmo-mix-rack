# BMO Tune RT -- handoff, 2026-09-10, AURORA

Everything a cold start needs. Read this, then `modules/tune/AGENTS.md`.

**The DSP is done and measured. Nothing has been heard in a DAW, and there is
no plugin yet.** The next real step is a listening pass on renders from the
CLI, then the JUCE wrapper.

## 1. Where it is

- Repository: `C:\Users\thesp\OneDrive\Documents\REPO\bmo-tune-rt`, a
  repository of its own. Local git only -- **no remote, nothing pushed**, by
  Frosty's decision on 2026-09-10. Pushes wait for Frosty's approval.
- Branch `main`, each commit a coherent stage with its evidence in the
  message.
- The shared code comes from Kevin's main, pinned as a submodule at
  `libs/bmo-mix-rack` (9c4a948, after PR #8). Nothing in the rack was changed.
- Licence: the rack's `LICENSE` (AGPL-3.0), copied verbatim from Kevin's
  `main`. **Kevin's README says "MIT, see LICENSE"**, which contradicts the file
  it points at; this repository's README says AGPL-3.0 to match its file. That
  line is Kevin's to settle.

## 2. Decisions taken with Frosty, 2026-09-10

| Question (spec Part IV) | Decision |
|---|---|
| Where it lives | its own repository, reusing Kevin's main (submodule), following every suite convention |
| MIDI (spec §4.6) | **dropped**: the vocal is the only thing tracked; key and scale are parameters |
| Scales | Chromatic, Major, Minor |
| Formant Correct | **pending** -- see `modules/tune/AGENTS.md`, "Open" |
| Push target | local only for now |
| Pitch range floor (Q3) | 80 Hz default and Auto; 55 Hz in Bass and Instrument |
| Latency contract (Q4) | **Live** default; Studio as the option |
| Flex (§4.3a) | an ordinary parameter, default 0; not the plugin's focus -- hard tuning and retune speed are |
| Licence | follow Kevin's main |

The two patents the spec flagged for Flex are Smule's karaoke patents, not
Antares' -- checked at Google Patents. The research digest itself is not on
AURORA, so the spec's citation could not be traced back to it.

## 3. Build and check

```
bash scripts/build.sh              # Release, eight suites, ~30 s
bash scripts/build.sh --corpus     # + 72-item corpus, ~15 s
build/tools/Release/bmo-tune-latency --range all
build/tools/Release/bmo-tune-bench --quick
```

AURORA has CMake 4.4 and MSVC 2022. **No clang and no Python**: every tool is
C++, and rtsan/TSan/UBSan are wired for a clang toolchain but not run.

## 4. Next steps, in order

1. **Listen.** Render real vocals through `bmo-tune-cli` with both engines at
   retune 0, 20 and 50, and the vibrato-0 warble case (§5 below). Record what
   was heard, with settings and the machine, before proposing any change.
2. **Decide vibrato 0's note decision** (§5) on what was heard.
3. **The JUCE wrapper**, as a product on Kevin's `core/product`: a
   `ModuleDef` for Tune RT, `SingleModuleProcessor` hosting `TuneDsp`, a panel
   on `ui::ModulePanel`. With MIDI gone nothing new is needed from the rack.
   Allocate the plugin code, bundle id, preset extension and accent first --
   in the rack's `products/AGENTS.md` table, which is Kevin's file, so ask.
4. **pluginval, host matrix, Ableton pass** -- Frosty's, on the wrapper build.
5. **Real corpora** (PTDB-TUG etc.) need downloading; ask first.

## 5. Open questions

- **Vibrato 0 warbles across a note boundary** -- deliberate, asserted, and a
  listening call. See `modules/tune/AGENTS.md`.
- **The LPC formant stage** is out of the signal path, with the reasons and
  what would bring it back recorded in `modules/tune/AGENTS.md`. PSOLA alone
  meets the formant gate.
- **192 kHz / 32-sample p99** is lumpy (~30 % of a block) because one
  refinement lands in one block. Fine at 48 kHz.
- **Kevin's README MIT line** vs his AGPL `LICENSE`.

## 6. Method lessons from this session

- **The spec's numbers were measured, not trusted, and several were wrong**:
  the 16-tap kernel's error, the flex curve's smoothness, a period of PSOLA
  lookahead, and the patents' owner. Each correction is in the code with its
  measurement, where the next person will look.
- **The corpus found what the unit tests could not.** A +1200-cent glitch at
  note ends came out of scoring 72 items, not from any test written for it.
  Run the corpus after any DSP change.
- **Measurement tools have bugs of their own.** The latency rig first picked
  arbitrary cross-correlation peaks on a periodic tone, then measured a
  detector bias as latency; the formant ruler first could not see past 10 %.
  Each fault is recorded at the point of use so it is not reintroduced.
