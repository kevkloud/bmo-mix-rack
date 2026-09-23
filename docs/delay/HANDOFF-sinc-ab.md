# Handoff: run the BMO Dwell sinc A/B with Frosty

Paste everything under the line into a **fresh session opened on the Dwell
worktree**, `C:\Users\thesp\OneDrive\Documents\REPO\bmo-mix-rack-333-dwell`.
Written on AURORA, 2026-09-23. Everything is already prepared; this session
runs the test and records the answer.

---

You are running a **blind listening test** with Frosty and then applying his
decision. The measuring, rendering and shuffling are all done — do not redo
them. Machine is **AURORA**; name it in anything you write.

## Do these three things first, before anything else

**1. Give Frosty the folder, formatted to paste straight into Explorer.**
Print it on its own line, with backslashes, nothing else on the line:

    C:\Users\thesp\OneDrive\Documents\REPO\bmo-listening\dwell-sinc-ab-2026-09-23\blind

Twenty files, 44 MB, outside every git tree. `KEY.txt` and `HOW-TO-LISTEN.txt`
are one level up, in `...\dwell-sinc-ab-2026-09-23\`. The labelled set is in
`...\labelled\` if he wants it afterwards.

**2. Open the answers file in a pane** so it is in front of him while he
listens — `mcp__ccd_view__show_pane` on
`testing-notes/dwell-sinc-ab-2026-09-23.md`. It is committed and has blanks
waiting under "## Answers".

**3. Pin this session in the sidebar** with `mcp__ccd_sidebar__set_pinned`, so
he can leave and come back to it mid-test without hunting.

## What he is deciding

Clean's interpolator reads through a **32-tap sinc**. It is the single largest
cost in the module — why Clean is the *most* expensive character despite doing
the least, and why the heaviest case is Clean + Diffuse rather than
bucket-brigade.

**32 was never chosen for Dwell.** It came from `modules/tune/dsp/SincTable.h`,
where 32 was measured for reads at a rate other than 1, with the kernel doubling
as the anti-alias filter. Dwell's clean read is at rate 1, so that reasoning
never applied here.

| taps | clean bare | heaviest | alias floor | 18 kHz | warble |
|---|---|---|---|---|---|
| 8 | −40 % | −30 % | −90.8 | −2.68 dB | 2.66 dB |
| 12 | −39 % | −29 % | −89.7 | −0.77 dB | — |
| 16 | −26 % | −17 % | −89.0 | −0.13 dB | 0.13 dB |
| 24 | −13 % | −8 % | −88.1 | 0.00 | — |
| 32 | — | — | −87.7 | +0.001 | 0.002 dB |

**Aliasing does not discriminate** — every width clears `10` §4's −60 dBFS
acceptance by more than 27 dB. This is a top-end question, not a grit one.
**12 is the interesting candidate**: within 2 % of 8 taps' cost for a quarter of
its top-end loss.

## Running it

- The set is **blind**: `A-repeats-1..5`, `B-longtail-1..5`, `C-chorus-1..5`,
  `D-driven-1..5`, shuffled per scene. **Do not open `KEY.txt`, and tell him
  not to, until his answers are written down.** Then reveal and record both.
- Levels are matched to 0.07 dB or better. Nothing clips.
- What to listen for, briefly: **A and B** the tail going dull over echoes six
  to fifteen, not the first echo; **C** the moving read, a slow shimmer on the
  pad's sheen that should not be there; **D** whether drive masks or exposes it.
  `HOW-TO-LISTEN.txt` has the long version.
- **"They all sound the same" is a real and useful answer** and should be
  recorded as such, not pushed back on.

## Recording it

Write his words into `testing-notes/dwell-sinc-ab-2026-09-23.md` under
"## Answers" — his phrasing, not a tidied paraphrase. Fill in the decision
block with the width, the reasoning and what he listened on. Then commit.

## Applying it

- The switch is **`BMO_DWELL_SINC_TAPS`** in `modules/dwell/dsp/DelayEngine.h`.
  **32 is in the tree**; change it only if he picks something else.
- Whatever he picks, **`10` §1 gains the reason the width is what it is** — the
  thing it has never had. Say it was chosen by listening, on what, and on which
  machine.
- If the width changes: rebuild and re-run the suite, and **re-run the CPU
  figures**, because `11` §4k's baseline was measured at 32 taps and the
  regression guard is ×1.15 against it. A width change makes the module faster
  than its own baseline, which is fine, but the baseline should be restated
  rather than left looking breached in the other direction.

## Rules that apply here

- **Never put audio in the repository.** The listening set is deliberately
  outside every git tree. Read `git status --short` before staging anything.
- Build with named targets only — an untargeted `cmake --build` installs plugin
  targets and has overwritten the installed 0.2.5 set on this machine before.
- `build-dsp` runs `BMO_DSP_ONLY` and cannot compile the JUCE side; build
  `build-ui`'s `dwell_tests` too after touching `DelayEngine.h`.
- A green ctest counts only after a build that exited 0.

## State

Branch `frosty-add-bmo-dwell`, 31 ahead of `origin/main`, 0 behind, unpushed,
no PR. 22/22 green with all ten modules. The DSP is complete and the visual
pass is done.

**After the A/B**, two things remain before a pull request: Frosty's call on the
seven knobs that print no value (`testing-notes/dwell-visual-pass-2026-09-23.md`,
item 1 — `13` §4 specified Hertz on the cuts and dB on DUCK, so it is a spec
deviation rather than taste), and then the PR itself, which produces installable
artifacts without needing a merge.
