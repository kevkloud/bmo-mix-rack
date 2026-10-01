# BMO Opto's reference hashes, re-baselined — 2026-10-01

**On AURORA, 2026-10-01.** Rendered and hashed on the laptop, Windows, the
licensed display faces loaded. Nothing was rendered on ICE QUEEN or on macOS;
see "What was not done" at the end. Markdown only: no code changed, and no
render is in the repository.

## What the guard is for

Handoffs and notes quote three hashes for BMO Opto and tell the next author to
re-prove them after any change under `core/ui`. Opto is the panel that uses
the shared pieces most — the knob, the switch, the preset bar, the needle
meter `ui::DynamicsMeter` — so a render that has not moved is evidence that a
change did not reach any other panel. It is evidence only if the render is
taken under conditions that cannot change by accident, which is why the
commands below name everything.

The old triple was `ab3ff3b77116b7a5` (dark), `878cca7b1a80a551` (light) and
`88a7653a82c19ae0` (GR dark). `testing-notes/ui-borders-2026-09-26.md` said
they would stop reproducing and left the re-baseline open; this is it.

## Why they moved

PR #28 (`f92ead1`, merged 2026-09-30) changed the look on purpose, and the
border-corner fix before it had already moved Opto's meter:

- knob tracks became eleven sparse dots, with no default mark;
- `DynamicsMeter`'s face radius moved to fill the frame exactly
  (`kFaceRadius + bezelThickness / 2`, from the 2026-09-26 corner fix);
- captions take their knob's ink in light, and the plus and minus follow the
  track's ink; borders changed;
- there are now **two surfaces**, `surface=simple` (the default) and
  `surface=textured`, a per-machine preference.

Nothing was wrong. The old figures describe a panel that no longer exists.

## The render conditions

| | |
|---|---|
| machine | AURORA (the laptop), user folder `C:\Users\thesp` |
| date | 2026-10-01 |
| tree rendered | `17593e2`, branch `frosty-host-tempo-plumbing` |
| stands for | `main` at `a531a13` (merge of #30, which carries #28) |
| snapshot tool | `build-full/tools/Release/snapshot.exe`, Release, built 2026-10-01 11:32, SHA-256 `9ed4d1d23b2a709b5e7560ef8d2f397c8eb3ab308f9f7fc2e72856e350a082c0` |
| hash tool | `tools/inspect/Inspect.cs`, built with `csc` 4.8.9221.0 (`Framework64/v4.0.30319`) into a scratch folder |
| fonts | the licensed display faces, through the `.bmo-fontdir` pointer (the faces are in the renders; they are not in the repository) |

**Why `17593e2` stands for `main`.** It is `a531a13` plus five commits. `git
diff --stat origin/main..HEAD` in that worktree lists eight files and nothing
else: `core/AGENTS.md`, `core/dsp/ModuleDsp.h`, `core/product/HostTempo.h`,
`core/product/ModuleEngine.h`, `core/product/SingleModuleProcessor.cpp`,
`core/rack/RackProcessor.cpp`, `tests/CMakeLists.txt` and
`tests/plugin/TempoTests.cpp`. All of it is host tempo and audio-side
plumbing and its tests. No UI, panel, token, asset or snapshot-tool file is
in the difference, and the snapshot never runs a host tempo, so the pictures
cannot differ from `main`'s. Anyone who wants the proof taken on a tree with
no difference at all can re-run the commands below on `main`; the figures
should be identical.

## The commands, exactly

Run from the root of the tree, each to a fresh filename:

```
snapshot opto d.png appearance=dark  surface=simple signal=-18
snapshot opto l.png appearance=light surface=simple signal=-18
snapshot opto g.png appearance=dark  surface=simple signal=-18 ui.meter=GR
Inspect.exe hash d.png          # prints 16 hex, then the size and the path
```

and the same three with `surface=textured`.

## Old triple against new

The old triple does **not** reproduce. Run with the old three commands
(`appearance=` named, no `surface=` key; the tool renders Simple whenever
`surface=` is missing, whatever the machine prefers), and
again with `surface=simple` added, which is the same picture and the same
hash:

| render | old (until 2026-09-26) | now, no `surface=` | now, `surface=simple` |
|---|---|---|---|
| `opto appearance=dark signal=-18` | `ab3ff3b77116b7a5` | `59d85c014da98432` | `59d85c014da98432` |
| `opto appearance=light signal=-18` | `878cca7b1a80a551` | `313df8cc740e9aa3` | `313df8cc740e9aa3` |
| `opto appearance=dark signal=-18 ui.meter=GR` | `88a7653a82c19ae0` | `393f13e24fbf96c3` | `393f13e24fbf96c3` |

## The Opto guard, six hashes

Every command names both `appearance=` and `surface=`. This is the guard from
here on.

| render | `surface=simple` | `surface=textured` |
|---|---|---|
| dark, `signal=-18` | `59d85c014da98432` | `5fb39ed5f61c3e78` |
| light, `signal=-18` | `313df8cc740e9aa3` | `e74f3e5e694ada42` |
| dark, `signal=-18 ui.meter=GR` | `393f13e24fbf96c3` | `cf22d91710adc669` |

The Simple three are what a handoff should quote. The Textured three are for
anyone who touches the Textured plate, which Simple does not draw.

## BMO Saturator

The old hash `d42e23747e1ea2fc` is `sat`, dark, with no other key; it sits in
the 2026-09-21 table of `testing-notes/deq-ring-accent-2026-09-21.md` beside
its light twin `f990b0b8599ae90e`, taken as `snapshot sat <file>.png
appearance=dark|light`. The old figure was not re-derived on a pre-#28 tree
today; the command is read from that table.

Now, `snapshot sat <file>.png appearance=<a> surface=<s>`:

| render | old | `surface=simple` | `surface=textured` |
|---|---|---|---|
| `sat` dark | `d42e23747e1ea2fc` | `34d76168b5ad5fae` | `d089af7b0bf0a6e0` |
| `sat` light | `f990b0b8599ae90e` | `ae55deda205bbcb0` | `9709d2d989901238` |

## Every product, no other keys

`snapshot <product> <file>.png appearance=<a> surface=<s>` and nothing else.
Opto's row has no `signal=`, so it is not the guard above: a bare Opto render
is a different picture from the one at `signal=-18`.

| product | simple dark | simple light | textured dark | textured light |
|---|---|---|---|---|
| `eq` | `142b26db17258d2c` | `6f818a9a918e9913` | `5fe37d83dfc39f03` | `6fd6f46278e08a9e` |
| `sat` | `34d76168b5ad5fae` | `ae55deda205bbcb0` | `d089af7b0bf0a6e0` | `9709d2d989901238` |
| `util` | `4eba7596aa1f187e` | `0ea9bab9603bddf6` | `ec80369b3532f1ea` | `227686202bee55f4` |
| `opto` | `522e93ef8eac5ce9` | `8f572288a676f34f` | `78b124b2aa2acb63` | `d44e827430ce4d52` |
| `dim` | `38031a27421f82d6` | `dcdf92ec8b40c36e` | `8fc163e93a3c6b04` | `62f5f8225d89630c` |
| `deq` | `d2600e31a9849e4c` | `0819754417aeb487` | `eee94acabe5d3a9a` | `a311b27d0ccfacf1` |
| `ltvcomp` | `8bd87fbdfd6af6f9` | `39a01da4525be00d` | `3ce2d7c83ddca2c9` | `ef9203614ce5efa1` |
| `deesser` | `2623be80ad5db0db` | `f89ba282b47ae9b2` | `f8366945d96698bb` | `9508a1600e1151e8` |
| `fetcomp` | `d52d6def3192a5b4` | `26a224ebca017bfc` | `c5902e7458b85a13` | `154319bc273f4580` |
| `reverb` | `d0c4bb8764224acb` | `ec41c06e9802ba46` | `a30b3d8d37925fe4` | `a637c6e589004377` |
| `rack` | `bee2db8f58407408` | `06ac4d342b7ffcb4` | `bee2db8f58407408` | `06ac4d342b7ffcb4` |

44 hashes. Two things to read from it:

- **The rack row is the empty rack** (the header, the preset bar and the `+`
  button, no module), so it does not change with the surface and it is
  unchanged since 2026-09-21: `bee2db8f58407408` and `06ac4d342b7ffcb4` are
  the same figures `deq-ring-accent-2026-09-21.md` recorded. It proves
  nothing about a module inside a rack. A rack proof needs `chain=`, for
  example `chain=util,eq,sat,opto,dim,fetcomp`, and was not taken today.
- Every other row moved, in every appearance and surface, as #28 says it
  should.

## Determinism

Each of the 44 renders above, and each of the Opto and Saturator renders in
the two tables before them, was made **twice, to two different filenames**,
and hashed. All pairs are identical. The Opto triple was also hashed with and
without `surface=simple` to show they are the same render.

## The two rules

1. **Name `appearance=` and `surface=` in every hash proof.** A render that
   names neither takes the machine's default, so its hash is luck, not
   evidence. The tool renders Simple when `surface=` is missing, so a
   Simple proof happens to hold without the key — but a note that does not
   say which surface it proved cannot be checked, and on a machine that
   prefers Textured the habit would be wrong.
2. **Render to a fresh filename.** Until `c1607d7` the snapshot tool appended
   to an existing PNG instead of truncating it, and a re-render onto an old
   name showed the old picture. Hash from a name that did not exist before.

Also still true from before: `signal=-18` is part of Opto's guard, and the
GR one needs `ui.meter=GR`. These are Windows renders with the licensed fonts
hashed by `Inspect.exe`; a render made with stand-in fonts or on another
platform will not match, and that is not a failure.

## What was not done

- **Not rendered on ICE QUEEN.** The figures are AURORA's. The expectation is
  that ICE QUEEN, with the same fonts and a Release build of the same tree,
  gives the same hashes; that has not been checked.
- **Not rendered on macOS.** Text rasterises differently there, so these
  hashes are Windows hashes and will not be the macOS ones.
- Not taken on `main` itself, but on `17593e2` (above).
- No rack with modules in it was hashed.
- The old Saturator figure was read from the 2026-09-21 table, not reproduced
  on a pre-#28 tree.
- The historical tables in `ui-pass-*.md`, `ui-pass-render-loop.md` and
  `ui-borders-2026-09-26.md` are left as they are: they record what was true
  on their dates.
