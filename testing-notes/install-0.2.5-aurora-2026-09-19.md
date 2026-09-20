# 0.2.5 installed on AURORA — tag run 35486489889

Installed on **AURORA** on 2026-09-19 over the nine bundles that were there,
using the package's own `install.ps1`.

**This is a tag build, not a pull-request build.** `v0.2.5` points at
`1d6356d`, and run **35486489889** was green on all four jobs — DSP, Each side
alone, macOS, Windows. It is the build the Ableton pass on 0.2.5 runs against.

**It is also the first universal package since the split.** Tag runs alone set
`CMAKE_OSX_ARCHITECTURES=arm64;x86_64`, so `BMO-macOS` is **104 MB** here
against the 49 MB arm64-only package a branch run produces. This is the first
0.2.x package an Intel Mac can load. Both artifacts expire **2026-12-19**.

## What is installed, verified by hash

All nine verified **byte-identical** to the `BMO-Windows` artifact after the
copy — the inner `Contents/x86_64-win/<name>.vst3` of each bundle:

| bundle | SHA-256 |
|---|---|
| BMO CEQ | `23212d1592eef567746babce3d17fc3a95e627f56b9c98f15728ee6903aec6cc` |
| BMO DEQ | `089880742307e4cb26990e442d4da55b98c74d4c036ba7d507a9a77d374ca6c0` |
| BMO Dimension | `ce38ea6f4194870072f1f0a99be8dbcc203923363fe91cdfb512c6f46076d67b` |
| BMO Mix Rack | `40f742c59a3ae114062d6de98772ed7d6b09ee34219ef7c030e6dcbc61b1406d` |
| BMO Opto | `d8518f4843ff057b7f80676a71912f6f5ee6c7a0afe081790014c4ec17a1d732` |
| BMO Saturator | `5ee8f8a07c60209675503fb96dbadc19e406d815023f131af9eff596e9a7778f` |
| BMO Tune RT | `819166f4d6c3cd34891cdc209a9ae094a6b9bc49f4a39d29d5e042ade627c6fe` |
| BMO Util | `a1e2957b044cb86aa2966a86adad2f0975d809bb2e7abf781d13ba0ee5c48289` |
| LTV Comp | `b8af8d955990924acac8df9e8b98a3d9245b220b8c202017ba781489503b6af9` |

`Version` in every `moduleinfo.json` reads **0.2.5**. VST3 only; no standalone
builds were installed, though the artifact carries nine of them.

## Two renames took effect on this machine

AURORA was still carrying the pre-rename names. `superseded.txt` removed both:

| was | is now | sessions |
|---|---|---|
| `BMO EQ.vst3` | **BMO CEQ** | **open as before** — the plugin code stayed `Fsty` through both renames on purpose |
| `BMO Vcomp.vst3` | **LTV Comp** | **do not carry over** — the identifier moved `Bvcp` → `Ltvc` |

So any Live session that loaded **BMO Vcomp** will not find LTV Comp in its
place and needs it re-inserted on those tracks. Presets do come across, copied
on first run. This is the intended product change, not a fault, but it is the
one thing on this machine that a listening session can trip over.

`FrostyEQ.vst3` was not present, so nothing was removed for it.

## The installer needed no elevation, which is the point of #11

`install.ps1` ran from a **non-elevated** shell and wrote to
`C:\Program Files\Common Files\VST3` without complaint. That is the first
real-world confirmation on AURORA of the fix in #11: the old script asked
whether the user was an Administrator, which is a proxy for the question, and
refused here even though the folder is writable. It now probes the folder.
Exit code 0, nine installed, two superseded removed.

## Method notes, for whoever installs next

- **Install the artifact last**, after any local building. `scripts/build.sh`
  builds Debug, which takes the `BMO_INSTALL_AFTER_BUILD TRUE` branch and
  overwrites this same folder with Debug binaries.
- **Verify by hash, not size or timestamp.** That Debug clobber leaves file
  sizes matching the artifact exactly; only a hash catches it. See
  `dim-bench-state-2026-09-09.md` §4, which is the session it happened in.
- A `.vst3` bundle is a **directory**. In PowerShell, `BaseName` on a
  `DirectoryInfo` does *not* strip the extension, so building the inner path
  from it yields `<name>.vst3.vst3` and every bundle reads as missing. Use
  `$dir.Name -replace '\.vst3$',''`.

The package is at `Downloads\BMO-0.2.5` on AURORA for copying to ICE QUEEN.
ICE QUEEN's own install is a separate record; nothing here describes it.
