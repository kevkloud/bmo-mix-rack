# scripts/build.sh never compiled anything on macOS

**2026-09-21, on the MacBook Pro** — `Kevin's MacBook Pro (2)`, macOS 26.6.2,
arm64. Branch `frosty-buildsh-mac-empty-args`, off `origin/main` at `5bc8e21`.

> **This machine has no name in the convention.** Root `AGENTS.md`, "Which
> machine you are on", covers ICE QUEEN and AURORA, both Windows, and says to
> read the machine's name out of its own `~/.claude/CLAUDE.md` and to ask
> rather than guess when it is not there. It is not there. This is a third
> machine — the Mac — and it needs a name of its own before the next note is
> written against it. Nothing below is about ICE QUEEN or AURORA.

## What happened

A first `scripts/build.sh` on this machine, after pulling `main` up to
`5bc8e21`, printed one line and stopped:

    scripts/build.sh: line 40: build_args[@]: unbound variable

Nothing was compiled. No target, no test, no copy into the plugin folder.

## Why

macOS ships **bash 3.2.57** as `/bin/bash`, and it is the only bash on `PATH`
here. In bash 3.2, expanding an empty array under `set -u` — which line 10
sets — is an unbound-variable error, not an empty expansion. Bash 4.4 and
later made it an empty expansion.

Line 40 was:

    cmake --build build --parallel "${build_args[@]}"

and `build_args` is empty on a single-config generator:

    if grep -q '^CMAKE_CONFIGURATION_TYPES:' build/CMakeCache.txt; then
        build_args=(--config "$config")
    else
        build_args=()          # <- empty, and then expanded under set -u
    fi

The generator here is **Ninja**, which is single-config, so the script took
that branch and died on the next line. The multi-config branch fills the array
and never triggers it.

## Why nothing caught it

Two reasons, and both are worth knowing:

- **Windows never takes the empty branch.** Visual Studio is multi-config, so
  `build_args` is `(--config Debug)` there. ICE QUEEN and AURORA are both
  Windows. The bug is reachable only on a single-config generator, which in
  practice means a Mac or a Ninja/Make build.
- **CI never runs this script.** `.github/workflows/build.yml` calls
  `cmake --build` directly at lines 32, 51, 57 and 151. All four jobs can be
  green — and were, for the `v0.2.5` tag run — while `scripts/build.sh` is
  broken for every developer on a Mac.

The multi-config handling dates from 0.2.3, and the comment above it records
fixing this script *for* Windows. The empty-array case is the other half of
that change.

## The fix

`--parallel` is seeded into the array instead of being passed alongside it, so
the array is never empty:

    build_args=(--parallel)

    if grep -q '^CMAKE_CONFIGURATION_TYPES:' build/CMakeCache.txt; then
        build_args+=(--config "$config")
    fi

    cmake --build build "${build_args[@]}"

### Both generators, exercised under bash 3.2

With `cmake` and `ctest` stubbed to echo their arguments, and a
`build/CMakeCache.txt` written to look like each generator in turn:

| generator | before | after |
|---|---|---|
| Ninja (single-config) | `line 40: build_args[@]: unbound variable`, exit non-zero, nothing built | `cmake --build build --parallel` |
| Visual Studio (multi-config) | `cmake --build build --parallel --config Debug` | `cmake --build build --parallel --config Debug` |

The Windows command line is **unchanged, argument for argument**. The macOS one
goes from an abort to the command it always meant to run. `ctest` is untouched
either way: `--test-dir build -C "$config" --output-on-failure`.

## The real build, after the fix

`scripts/build.sh` ran to completion on this machine: **1232 targets**, then
**26 of 26 tests passed** in 38.03 s (`tune_hardtune_target` is disabled and
did not run). Exit 0.

## Two things found on the way, not fixed here

- **`build/` is configured `RelWithDebInfo`, not Debug.** `build.sh` sets
  `config=Debug` and passes `-DCMAKE_BUILD_TYPE` only when it configures a
  build directory for the first time. This tree's cache was written on
  2026-09-08 with `RelWithDebInfo`, so the script builds that, runs
  `ctest -C Debug` (ignored on a single-config generator), and reports nothing
  about the mismatch. What it installs is a RelWithDebInfo build while the
  script's comments describe a Debug one.
- **`package.sh` would have staged a bundle the rename retired.** It discovers
  its product list with `find "$build" -type d -name '*.vst3'` (line 39), and
  this incremental tree still held a `BMO EQ.vst3` from 2026-09-08 beside the
  new `BMO CEQ.vst3` in the same `BmoEq_artefacts` directory. A package built
  from it would have shipped `BMO EQ` as a product — and because the installers
  refuse to remove any superseded name that the build also ships, that package
  would have **declined to remove the very bundle the rename supersedes**. The
  stale artefacts were deleted; a `--fresh` configure or a clean tree also
  avoids it.

## Bundles on this machine now

The nine products of this build are installed in
`~/Library/Audio/Plug-Ins/{VST3,Components}`, all reporting **0.2.5**:
BMO CEQ, BMO DEQ, BMO Dimension, BMO Mix Rack, BMO Opto, BMO Saturator,
BMO Tune RT, BMO Util, LTV Comp.

Removed, per `tools/packager/superseded.txt`, from both the VST3 and the
Components folder: **BMO EQ** (dated 2026-09-08) and **FrostyEQ** (2026-09-01).
No `BMO Vcomp` was ever installed here. Nothing superseded was in
`/Library`; both plug-in folders are now clean of every name on that list.

**BMO Tune RT was copied in by hand**, which its own `products/tune/CMakeLists.txt`
says to do: it sets `INSTALL_AFTER_BUILD FALSE` so a development build does not
clobber the installed 0.1 it is being compared against by ear. There is no
installed 0.1 on this machine — there was no Tune bundle here at all — so the
opt-out was protecting nothing and its absence would just have left Tune out of
the host. The flag was **not** changed.

These are locally built RelWithDebInfo bundles, not the universal package from
the `v0.2.5` tag run. **They are fine to look at and not what a listening
result should be recorded against** — Stage 4's rule is that both machines
install the same bytes from CI.
