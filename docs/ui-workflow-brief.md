# Brief: a safe, fast loop for UI and design work

**For a fresh Claude Code session on `bmo-mix-rack-333`.** The goal is a way to
build, see and change BMO's panels and design language *quickly*, and with a
structural guarantee that none of it can move the audio.

Written after a session that made six UI changes and had to spend a
22-minute CI build to look at each one. Every problem named below actually
happened; nothing here is hypothetical.

---

## Read this first: two things already exist

Don't propose building these. They're in the tree.

**1. `tools/snapshot` already renders panels to PNG, headless.**

```
snapshot <eq|sat|util|opto|rack> out.png [width height] [param=value ...]
```

Its own header comment says it exists "so a layout change can be reviewed in a
pull request rather than described in one." For the rack it takes
`chain=util,eq,sat,opto` and `N.id=value` to set a slot's parameter. This is
most of the hard part already solved.

**2. `Tokens.h` already supports live theming.** Every colour the suite draws
with is a named token, overridable from a flat JSON file at
`LT3 Audio/Themes/Default.json`, polled on a slow timer so editing it
recolours an open plugin. Non-colour tokens (radii, stroke weights, gaps) are
deliberately *not* themable so a theme can't break a layout.

So the raw materials are good. What's missing is the loop around them.

---

## The seam that makes UI-only work possible

This is the key architectural fact, and it's better than it looks.

`core/ui/ModulePanel.h` defines what a panel is built against:

```cpp
struct ModuleContext
{
    ParamSet& params;
    const ModuleDef& def;
    std::function<float()> peak, rms;
    std::function<float()> inputPeak, inputRms, gainReductionDb;
};
```

**A panel needs no DSP.** It needs a parameter set, a module definition, and
five float callbacks for its meters. `ModuleDef::createDsp` is itself a
`std::function`, so it can be null. A module's `params.h` (parameter specs)
and `presets/FactoryPresets.h` are plain data with no DSP dependency.

That means a build target can exist that compiles `core/ui`, the panels, and
the parameter/preset headers — and **cannot link a single line of DSP**. Not
by convention. By what's in the link line.

---

## What went wrong this session, concretely

Design the loop against these. They're the real failure modes.

| what happened | why the current loop allowed it |
|---|---|
| The VU meter's hot zone was `#97ddff` on `#d6d6d6` — **1.02:1**. The one element meant to be seen peripherally was invisible. | Nothing measures contrast. It shipped, and was only caught by someone computing ratios by hand. |
| `MAKEUP` rendered as `MAKEU`. | `PlainKnob` drew its caption inside the knob's own width. No check that text fits its box. |
| Opto's panel had **three different button classes** — `TextButton` for TELE/ELD and IN/GR/OUT, `SwitchButton` for LINK/COLOR — so one panel had three looks. | Nothing enforces that a control of a given kind is the same control everywhere. |
| Modules hardcode colours: `kLabelColour #9c71c3`, `kMeterHotColour`, `kMeterFaceColour`. | The accent (`#d4a4ff`) is unreadable under white text, and there's no *derived* token for "the accent, darkened to a legible contrast". So each module hand-rolls its own hex, and the next one will too. |
| A layout instruction was ambiguous and needed a round-trip with the user before writing any code. | No way to show two candidate layouts side by side cheaply. |
| Every one of the above cost a full CI build to see. | `snapshot` is behind `if(NOT BMO_DSP_ONLY)` and links the products, so seeing a panel means building the plugins. **~22 minutes on Windows.** |

There is **no C++ toolchain on the target machine** — no cmake, ninja, cl or
clang. Verify this before assuming a local build is possible. It shaped every
decision above and it will shape yours.

---

## Proposals, in the order I'd do them

### 1. A UI-only build target — the whole point

`-DBMO_UI_ONLY=ON` builds `core/ui`, `modules/*/panel`, `modules/*/params.h`,
`modules/*/presets`, and JUCE's GUI modules. Nothing else. Panels are
instantiated against a **stub context**: a real `ParamSet` built from the
module's real specs, a synthetic `ModuleDef` with `createDsp = nullptr` and
the module's real accent, and meter callbacks driven by a scriptable value
source rather than by audio.

Two things fall out of this, and the second is the important one:

- It's fast, because it isn't building JUCE's audio stack, the DSP, or five
  plugin wrappers.
- **A UI-only change is provably a UI-only change**, because the artifact
  that renders it cannot link DSP. That's the guarantee being asked for, and
  it's structural rather than a promise.

### 2. Scriptable meter and state injection

The stub context should let a spec file say "input RMS is −18 dBFS, gain
reduction is 6 dB, mode is ELD, COLOR is disabled". Meters are the hardest
part of the panel to review, and today they can only be seen by playing audio
through a DAW. Being able to pin a needle at a stated value and screenshot it
would have caught the 1.02:1 hot zone immediately, because someone would have
been *looking* at a meter reading +2 VU.

### 3. Contact sheets from a spec file

Extend `snapshot` (or a sibling that uses the UI-only target) to take a small
spec — module, size, parameter values, meter values, theme file — and render a
grid of states in one pass. What that unlocks:

- Every panel, every mode, one image. Inconsistencies between modules become
  visible instead of theoretical.
- **Candidate layouts side by side**, which is the cheap answer to the
  ambiguity problem above.
- Light/dark or alternate theme JSONs rendered together.

### 4. Design assertions as tests

These are the two bug classes that shipped, and both are mechanically
checkable:

- **Contrast.** For every (ink, ground) pair the tokens permit, assert a
  minimum ratio. The needle-on-face pair, label-on-plate, text on an engaged
  switch. This is a pure function of `Tokens.h` and needs no rendering at all
  — it could run in the DSP-only job in milliseconds.
- **Text fits.** After `resized()`, assert every caption's measured width is
  inside its bounds. `MAKEUP` → `MAKEU` was a five-character overflow that no
  human noticed for a full release.

Note the house rule this session established the hard way, in
`tests/dsp/OptoDspTests.cpp`: **assert absolutes, not comparisons.** A
relative release test passed for an entire release while both modes were
broken, because it only compared them to each other. "Contrast is better than
it was" is the same trap. Assert a ratio.

### 5. A DSP fingerprint, so "doesn't affect signal processing" is provable

Render a fixed signal through each module at fixed parameters and hash the
output. Store the hashes. Any PR that changes one has changed the audio.

This is the direct mechanical answer to the brief's title. A UI change that
trips the fingerprint is a bug, and one that doesn't is *proven* inert — no
reviewer judgement required. It also protects the reverse case, which bit us
this session: a DSP change silently moved all thirteen preset makeup levels,
and nobody knew because the level test's ±3 dB tolerance meant a pass printed
no number.

### 6. Derived tokens, so modules stop hardcoding hex

Add tokens computed from the accent rather than alongside it — an
`accentText` (the accent darkened to a fixed contrast against the plate) and
an `onAccent` (whichever of black/white reads on the accent). BMO Opto already
needed exactly this and hardcoded `#9c71c3`; without it, module six hand-rolls
its own. Consider also widening the structural greys: `plate #efefef` to
`well #d6d6d6` is only `0x19` apart, so recesses don't read as recessed.

---

## Constraints to respect

- **Never let a UI tool break a plugin build.** `tools/measure/renders` is
  deliberately outside CMake for this reason — read its README. A UI harness
  that CI compiles is fine; a UI harness that can fail the plugin build is
  not.
- **Ask before pushing, and batch.** A CI round trip is ~22 minutes on
  Windows, the workflow's concurrency group cancels an in-progress run on the
  same ref, and CI does not auto-run on feature branches. Accumulate changes,
  then ask.
- **Name the repository on every `gh` command.**

      gh workflow run build.yml --repo badmixesonly/bmo-mix-rack-333 --ref <branch>

  There are two remotes — `origin` is `badmixesonly/bmo-mix-rack-333` and
  `upstream` is `kevkloud/bmo-mix-rack` — and **`gh` picks `upstream`**. On
  8 Sep a `gh workflow run` without `--repo` tried to dispatch against
  Kevin's repository and was stopped only by a 403 for want of admin rights
  there. Nothing had gone wrong with the push: `git push origin <branch>`
  names its remote and went to the right place. It is `gh` alone that
  resolves elsewhere, and it does so silently.

  Left as a flag deliberately. Do not paper over it with an alias, a
  `gh repo set-default`, or a wrapper — whether this working copy should have
  an `upstream` remote at all is Frosty's question for Kevin, and a fix that
  hides the two-remote setup would hide the question with it.

  **It has a running cost, and this is it.** GitHub does not pass repository
  secrets to a pull request opened from a fork, so the Windows and macOS jobs
  fail on every PR raised that way — not because a secret is wrong, but
  because it arrives empty. It is not a thing to fix in the workflow. Either
  the branch lives in Kevin's repository and the PR is same-repo, or the PR
  merges on DSP, "Each side alone" and the author's local suite.
  `assets/fonts/README.md` has the detail and both options written out.
- **Frosty decides character and version numbers.** Layout, colour and preset
  character are his calls — offer real options with measured trade-offs
  (contrast ratios, dB) rather than picking one quietly.
- `gh` is installed but not always on PATH, and its `--jq` breaks under
  PowerShell quoting — run it through bash.

---

## The one-line version

The architecture already separates UI from DSP cleanly — `ModuleContext` is
five callbacks and a parameter set. What's missing is a build target that
*proves* it, a way to see a panel in seconds instead of 22 minutes, and two
cheap assertions that would have caught both UI bugs that shipped this
release.
