#!/usr/bin/env bash
# Stage every product's plugin bundles into one folder for testers.
#
#   tools/packager/package.sh <build-dir> <out-dir>
#
# Locates the artefacts rather than hardcoding their paths, which have moved
# under renames before. Produces:
#
#   <out>/VST3/*.vst3          both platforms
#   <out>/AU/*.component       macOS
#   <out>/Standalone/*         .app on macOS, .exe on Windows
#   <out>/install.command    macOS installer
#   <out>/install.ps1        Windows installer
#   <out>/superseded.txt     old bundle names both installers remove
#   <out>/LICENSE.txt
#   <out>/README.txt           where to put things
#
# Signing and notarisation are still out of scope, so macOS quarantines
# what it unzips and the installer clears the flag. See README.md.

set -euo pipefail

build=${1:?build dir}
out=${2:?out dir}
root=$(cd "$(dirname "$0")/../.." && pwd)

rm -rf "$out"
mkdir -p "$out/VST3" "$out/Standalone"

# Discovered from the build tree, not listed here. A hardcoded list has now
# silently dropped a whole product twice -- BMO Opto, and BMO Dimension after
# it -- because adding a module touches modules/, products/ and the registry
# and never touches this file, so nothing fails when it is forgotten. The
# artefacts are the same thing the rest of this script already goes looking
# for; asking them what exists cannot drift from what was built.
products=()
while IFS= read -r name; do
    products+=("$name")
done < <(find "$build" -type d -name '*.vst3' | sed 's#.*/##; s#\.vst3$##' | sort -u)

if [ ${#products[@]} -eq 0 ]; then
    echo "no VST3 bundles found under $build"
    find "$build" -name '*_artefacts' | head
    exit 1
fi

found=0

for name in "${products[@]}"; do
    vst3=$(find "$build" -type d -name "$name.vst3" | head -1)
    if [ -n "$vst3" ]; then cp -R "$vst3" "$out/VST3/"; found=$((found + 1)); fi

    au=$(find "$build" -type d -name "$name.component" | head -1)
    if [ -n "$au" ]; then mkdir -p "$out/AU"; cp -R "$au" "$out/AU/"; fi

    app=$(find "$build" -type d -name "$name.app" | head -1)
    if [ -n "$app" ]; then cp -R "$app" "$out/Standalone/"; fi

    exe=$(find "$build" -type f -name "$name.exe" | head -1)
    if [ -n "$exe" ]; then cp "$exe" "$out/Standalone/"; fi
done

if [ "$found" -eq 0 ]; then
    echo "no VST3 bundles found under $build"
    find "$build" -name '*_artefacts' | head
    exit 1
fi

cp "$root/LICENSE" "$out/LICENSE.txt"

# The installers and the one list they share. Both platforms read
# superseded.txt, so a rename is recorded once rather than twice.
cp "$(dirname "$0")/install.command" "$out/"
cp "$(dirname "$0")/install.ps1"    "$out/"
cp "$(dirname "$0")/superseded.txt" "$out/"
chmod +x "$out/install.command"

# Names the products that actually got staged, for the same reason the array
# above is discovered: a second hand-maintained list is a second thing to
# forget.
contents=$(printf '%s, ' "${products[@]}")
contents=${contents%, }

# Two heredocs, and the split is not cosmetic: the second one has to stay
# quoted, because the Windows install path ends in a backslash and an
# unquoted heredoc would read that as a line continuation and eat the blank
# line after it.
cat > "$out/README.txt" <<TXT
BMO (Bad Mixes Only) by LT3a -- tester build

Contents
  VST3/         $contents
  AU/           the same, as Audio Units (macOS only)
  Standalone/   each product as an app, for a quick look without a DAW
TXT

cat >> "$out/README.txt" <<'TXT'

Install
  Easiest: run the installer beside this file. It removes bundles an
  earlier build left under a name this one no longer uses, copies
  everything into place, and on macOS clears the quarantine flag.

    macOS     double-click install.command
    Windows   right-click install.ps1 -> Run with PowerShell (as
              Administrator; the shared VST3 folder is under Program Files)

  By hand, if you would rather:
    macOS
      VST3  ->  ~/Library/Audio/Plug-Ins/VST3/
      AU    ->  ~/Library/Audio/Plug-Ins/Components/
      These are not signed or notarised yet, so macOS will say a plugin
      is "damaged and can't be opened". It is almost never damaged.
      Three things wear those words:
        1. the quarantine flag, on anything unzipped from a download;
        2. the flag cleared in ~/Library while the plugins are in
           /Library, which matches nothing and looks like a no-op;
        3. an arm64-only build on an Intel Mac, which no xattr fixes.
      Covering 1 and 2 by hand:
        for d in ~/Library/Audio/Plug-Ins /Library/Audio/Plug-Ins; do
          sudo xattr -dr com.apple.quarantine "$d"/VST3/BMO*.vst3 "$d"/VST3/LTV*.vst3 2>/dev/null
          sudo xattr -dr com.apple.quarantine "$d"/Components/BMO*.component "$d"/Components/LTV*.component 2>/dev/null
        done
      Both globs matter: LTV Comp is the one bundle whose name does
      not begin with BMO. For 3, run  uname -m  -- if it says x86_64
      you need a package from a tag run, which is universal.
    Windows
      VST3  ->  C:\Program Files\Common Files\VST3\
    Then rescan plugins in your DAW.

Renamed products
  Each of these shipped under an older name. The installer removes the
  old bundle for you; by hand, delete it yourself or the DAW lists both.

  BMO Vcomp  ->  LTV Comp
    The first product on the LTV line rather than a BMO one. Your presets
    are copied into the LTV Comp folder the first time it runs. Sessions
    are NOT carried over: the plugin's identifier changed with the name,
    so a session that loaded BMO Vcomp will not find LTV Comp in its
    place. Re-insert it on those tracks.

  BMO EQ  ->  BMO CEQ
    The console EQ, so the name says what it is next to BMO DEQ. Same
    plugin, same sessions, same sound. Your presets are copied into the
    BMO CEQ folder the first time it runs.

  FrostyEQ  ->  BMO CEQ
    The same product, one rename earlier. Sessions that used FrostyEQ
    open with BMO CEQ, and presets come across from either old name.

Presets
  ~/Library/Audio/Presets/LT3 Audio/<product>/   (macOS)
  %APPDATA%\LT3 Audio\<product>\Presets\          (Windows)
  A theme file can be dropped at LT3 Audio/Themes/Default.json; see the
  repository README.
TXT

echo "staged into $out:"
find "$out" -maxdepth 2 | sort
