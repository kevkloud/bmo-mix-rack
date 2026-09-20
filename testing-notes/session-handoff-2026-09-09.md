# Handoff — 2026-09-09, for a fresh session

Everything from this session that a cold start needs. Read this, then
`dim-meter-pass.md`.

**The one blocking task is the listening pass.** Everything else is done or
waiting on Kevin.

---

## 1. Next steps, in order

1. **Land PR #5 first.** It fixes the rest dot on every knob in the suite, and
   five of Dimension's nine knobs move. Running the listening pass before it
   lands means judging a panel that is about to change — checklist §4 asks
   whether the panel reads as a set of controls.
2. **Merge Kevin's `main` into the Dimension branch** once #5 is in, so PR #4
   picks the fix up. Verified clean already, on a local preview branch.
3. **Re-render Dimension's panel** and look at it:
   `./build/tools/Debug/snapshot.exe dim snapshots/dim.png`
4. **Grab the BMO-Windows artifact** from PR #4's newest green run and install
   it — see §4.
5. **Open the meter pass** (`dim-meter-pass.md`, or the artifact linked in it)
   and work it top to bottom. Sections 01 and 02 before anything subjective.
6. **Verify the DSP decision.** The throb is the one question that gates the
   PR: on a mono vocal at CENTS 10, does it read as MicroPitch-style shimmer or
   as an audible tremolo of the width? Record what you *heard*, with settings,
   before proposing any fix.
7. **Take PR #4 out of draft** if the throb reads acceptably. If it does not,
   the remedies are design decisions — decorrelate the voices, drop one, feed
   the pair at unequal depths — and `dim-1.0-handoff.md` §6 is explicit that
   the symptom gets recorded before anyone picks one.

## 2. Where the work is

| PR | branch (on **Kevin's** repo) | state |
|---|---|---|
| **#4** Dimension | `frosty-add-bmo-dimension` | **draft**, green at `e799458`, 32 files |
| **#5** rest dot | `frosty-fix-rest-dot` | open, **critical**, CI re-running after a doc fix |
| **#6** font script | `frosty-fix-set-font-dir` | open, **all green** |

Neither #5 nor #6 has a review yet.

Local branches, all pushed except the preview:

- `dim-module-docs` → PR #4
- `fix-rest-dot-marks-default` → PR #5
- `fix-set-font-dir-without-coreutils` → PR #6
- `dim-plus-restdot-preview` — **local only, disposable.** The #4 + #5 merge,
  used to preview Dimension's panel with correct dots. Delete it or redo it.
- `main` — a clean mirror of Kevin's. **Keep it that way**; do not commit here.

## 3. Remotes — easy to get wrong

    origin    badmixesonly/bmo-mix-rack-333   (the fork)
    upstream  kevkloud/bmo-mix-rack           (Kevin's)

**All three PRs are on Kevin's repo, so pushes go to `upstream`.** A push to
`origin` leaves the PRs untouched. GitHub withholds secrets from fork PRs, so
only a branch in Kevin's repo can build green at all.

Per `ui-editor-handoff.md` §7, do **not** add a `gh repo set-default`, alias or
wrapper — hiding the two-remote setup hides an open question for Kevin. Pass
`--repo kevkloud/bmo-mix-rack` explicitly on every `gh` command.

## 4. This machine

Rebuilt 2026-09-09 as AURORA. Handoffs naming ICE QUEEN's user folder
describe the **old** machine and are stale.

Installed and verified this session: Git for Windows 2.55.0.3, CMake 4.4.3, VS
Build Tools 2022 (MSVC 19.44), Windows SDK 10.0.26100, GitHub CLI 2.100
(authenticated), Ableton Live 11 + 12 Suite.

    cmake -S . -B build
    bash scripts/build.sh            # builds and runs ctest — 12/12 on the dim branch
    ./build/tools/Debug/snapshot.exe dim snapshots/dim.png

Fonts resolve from `.bmo-fontdir` → `%USERPROFILE%/OneDrive/Documents/FONTS`.
A correct configure prints:

    -- BMO fonts: %USERPROFILE%/OneDrive/Documents/FONTS (.bmo-fontdir)

**Installing a tester build:** take the **BMO-Windows** artifact from the
Actions run, unzip, copy the `.vst3` bundles into
`C:\Program Files\Common Files\VST3\` (needs admin), rescan in Ableton.

**A new install needs the Claude desktop app restarted** before a new tool is on
the shell's PATH — the shell inherits the app process's environment.

## 5. Missing for the listening pass

- **MicroPitch is not installed.** CLA Vocals was being installed at the end of
  the session — confirm it is there.
- **No "Fuji" test render on this machine.** Re-bounce it or fetch it from the
  old machine. Use the same vocal the 0.2.1 Opto work used; continuity of
  source is most of why those figures were comparable.
- **SSL Meter Pro is installed** and is standing in for the goniometer
  Dimension does not have.

## 6. Open, and not for this session to settle alone

- **The rest-dot sweep.** PR #5 fixes the drawing. What it does not do is test
  it: `ui_layout` asserts component bounds, and a rest dot is *painted* rather
  than placed, so nothing could have caught this. A test that a knob's drawn
  rest mark agrees with its parameter default would be cheap. See
  `rest-dot-finding.md` §5.
- **A level-matching test utility** was specced but not built — deliberately,
  because `RackTests.cpp` asserts an exact registry size and a second product
  in flight would conflict with PR #4. Build it stacked on the Dimension branch
  after #4 lands. The design must be **measure-then-freeze**, never a
  continuous RMS leveller: that is what AUTO was, and it voided a release's
  worth of Saturator measurements.
- **EQ has a `Mix` parameter with no control on its panel.** Noticed in
  passing, unexplained. Worth confirming it is deliberate.
- **Two remotes**, still awaiting Kevin's view on whether the fork is
  deliberate.

## 7. Two method lessons from this session

- **Measure a render; never judge one by eye.** A DIFFUSE pointer that looked
  like it sat at maximum measured at exactly its documented minimum, and a mark
  that read as a "default dot" turned out to mark zero. Both would have been
  reported as bugs on impression alone. `tools/inspect/` is the tool; a quick
  equivalent is PowerShell over `System.Drawing`. Always use a **control** —
  a render difference can be caused by the *act* of passing a parameter rather
  than its value, since any parameter marks the preset bar `Init *`.
- **Absence in our own code is not absence.** Double-click-to-default was
  reported as "not wired at all" after grepping `core/` and `modules/`. It has
  always worked: `juce::SliderParameterAttachment` sets it. `libs/JUCE` is
  vendored and greppable — search it before saying a behaviour does not exist.
