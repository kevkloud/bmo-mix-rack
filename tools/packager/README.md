# The packager

Stages every product's plugin bundles into one folder a tester can unzip, and
puts an installer beside them.

```
tools/packager/package.sh <build-dir> <out-dir>
```

CI runs it as `tools/packager/package.sh build out` and uploads `out/` as the
`BMO-macOS` / `BMO-Windows` artifact. Packaging inherits the workflow's
implicit `if: success()`, so a failing test withholds the build.

## What ends up in the package

| | |
|---|---|
| `VST3/*.vst3` | both platforms |
| `AU/*.component` | macOS only |
| `Standalone/*` | `.app` on macOS, `.exe` on Windows |
| `install.command` | the macOS installer — double-clickable |
| `install.ps1` | the Windows installer — needs Administrator |
| `superseded.txt` | the old bundle names the installers remove |
| `README.txt` | generated; what a tester reads |
| `LICENSE.txt` | |

## Two lists, and why only one of them is written down

**The product list is discovered**, by finding `*.vst3` under the build tree.
It is not written anywhere in this folder. A hardcoded list silently dropped a
whole product twice — BMO Opto, then BMO Dimension — because adding a module
touches `modules/`, `products/` and the registry and never touches the
packager, so nothing fails when it is forgotten.

**The superseded list cannot be discovered**, because it names bundles this
build no longer produces. That is `superseded.txt`, and it is the one list here
that is maintained by hand. Both installers read it, so a rename is recorded
once rather than once per platform.

Its format is one renamed product per line:

```
BMO Vcomp | LTV Comp | renamed 2026-09-14
```

Add a line whenever a product's **bundle filename** changes. A rename that
keeps the filename needs nothing here.

### The guard

An installer refuses to remove anything that is also a product in the build it
is installing. Without that, a typo in `superseded.txt` — or a name that comes
back — deletes a plugin the tester just installed. Only exact bundle names are
ever removed; nothing globs, and nothing outside the two plug-in folders is
touched.

## macOS: "damaged and can't be opened"

Three different causes wear these same words, and only the first is the one
people reach for.

1. **The quarantine flag.** These builds are not signed or notarised, so macOS
   quarantines anything unzipped from a download. `install.command` clears it.
2. **The flag cleared in the wrong place.** macOS has two plug-in locations —
   `~/Library/Audio/Plug-Ins/` and `/Library/Audio/Plug-Ins/`. An `xattr`
   aimed at the first when the plugins are in the second matches nothing,
   exits quietly, and looks exactly like the command not working. The
   installer clears both, and prints a `sudo` line for anything it could not
   write.
3. **An arm64-only build on an Intel Mac.** No amount of `xattr` helps. The
   installer checks this first and stops with an explanation rather than
   letting you chase the flag.

By hand, covering both locations:

```
for d in ~/Library/Audio/Plug-Ins /Library/Audio/Plug-Ins; do
  sudo xattr -dr com.apple.quarantine "$d"/VST3/BMO*.vst3 "$d"/VST3/LTV*.vst3 2>/dev/null
  sudo xattr -dr com.apple.quarantine "$d"/Components/BMO*.component "$d"/Components/LTV*.component 2>/dev/null
done
```

Both globs are needed. **LTV Comp is the one bundle whose name does not start
with `BMO`**, and the `BMO*`-only command this README used to give left it
quarantined and looking broken after everything else worked.

To tell the three apart on the affected machine:

```
uname -m
lipo -archs "/Library/Audio/Plug-Ins/VST3/BMO CEQ.vst3/Contents/MacOS/BMO CEQ"
```

`x86_64` from the first against `arm64` from the second is cause 3.

## macOS: architecture

Non-tag CI runs build **arm64 only**; universal binaries cost double to compile
and only matter for something someone downloads, so `CMAKE_OSX_ARCHITECTURES`
gains `x86_64` on tag runs alone. An arm64-only build will not load on an Intel
Mac, and no amount of clearing quarantine changes that — take the mac package
from a tag run for those.

## Upgrading in place

`install.command` removes each bundle before copying it. BSD `cp -R`, which is
what runs on macOS, copies a directory *into* an existing directory of the same
name: upgrading without the removal leaves `BMO Opto.vst3/BMO Opto.vst3`, which
the DAW cannot load. GNU `cp` does not do this, so the fault does not reproduce
when the script is tested under Git Bash on Windows.

## Sessions and presets, under a rename

These are two different questions and they have different answers.

- **Presets** come across. `PresetInfo` carries a list of the product's former
  folders and extensions, walked newest first, and the copy is one-shot per
  machine via a `.migrated` marker — so a preset the user deletes stays
  deleted. See `core/state/PresetManager.cpp`.
- **Sessions** come across only if the plugin's identifier did not change.
  BMO CEQ kept FrostyEQ's `PLUGIN_CODE` (`Fsty`) and bundle id through two
  renames on purpose, so old sessions still open. **LTV Comp did not** — it
  moved from `Bvcp` to `Ltvc` with a re-derived bundle id, so a session that
  loaded BMO Vcomp will not find LTV Comp in its place. The generated
  `README.txt` says so under "Renamed products".
