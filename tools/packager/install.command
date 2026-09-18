#!/usr/bin/env bash
# Install this tester build on macOS. Double-click it, or run it from a
# terminal in the unzipped folder.
#
#   1. refuse early if this Mac cannot run the build at all;
#   2. remove bundles a previous build installed under a name this one no
#      longer uses (superseded.txt), so the DAW does not list both;
#   3. copy VST3 and AU into the user's plug-in folders;
#   4. clear the macOS quarantine flag -- in BOTH plug-in locations.
#
# Steps 1 and 4 exist because "BMO CEQ is damaged and can't be opened" has
# three different causes and only one of them is the one people reach for:
#
#   - the quarantine flag, which these builds always get because they are not
#     signed or notarised (step 4);
#   - the flag being cleared in ~/Library while the plugins are actually in
#     /Library, which is a silent no-op and looks exactly like the command not
#     working (step 4 does both);
#   - an arm64-only build on an Intel Mac, which no amount of xattr fixes and
#     which this script now says out loud instead of letting you chase (step 1).

set -uo pipefail

here=$(cd "$(dirname "$0")" && pwd)
user_vst3="$HOME/Library/Audio/Plug-Ins/VST3"
user_au="$HOME/Library/Audio/Plug-Ins/Components"
sys_vst3="/Library/Audio/Plug-Ins/VST3"
sys_au="/Library/Audio/Plug-Ins/Components"

[ -d "$here/VST3" ] || { echo "No VST3 folder beside this script. Run it from the unzipped package."; exit 1; }

shipping=()
while IFS= read -r n; do shipping+=("$n"); done \
    < <(find "$here/VST3" -maxdepth 1 -type d -name '*.vst3' | sed 's#.*/##; s#\.vst3$##' | sort)

# -- 1. can this Mac run it at all? ----------------------------------------
# lipo is the authority; the header read is a fallback for a stripped system.
probe=$(find "$here/VST3" -type f -path '*/Contents/MacOS/*' | head -1)
if [ -n "$probe" ]; then
    archs=$(lipo -archs "$probe" 2>/dev/null || echo "")
    host=$(uname -m)

    if [ -n "$archs" ] && [ "$host" = "x86_64" ] && ! echo "$archs" | grep -q "x86_64"; then
        echo "This package cannot run on this Mac."
        echo
        echo "  this Mac:      $host (Intel)"
        echo "  this build:    $archs"
        echo
        echo "Non-tag CI runs build arm64 only -- universal binaries cost double"
        echo "to compile and only matter for something someone downloads, so they"
        echo "are built on tag runs alone."
        echo
        echo "macOS reports this as \"damaged and can't be opened\", which is the"
        echo "same words it uses for the quarantine flag, so it is easy to spend"
        echo "an afternoon on xattr over it. xattr will not help here: ask for a"
        echo "package from a tag run, which is universal."
        exit 1
    fi
fi

echo "Installing ${#shipping[@]} plugins."
echo

# -- 2. superseded ---------------------------------------------------------
if [ -f "$here/superseded.txt" ]; then
    removed=0
    while IFS='|' read -r old replacement _rest; do
        # tr -d '\015' -- superseded.txt is pinned to LF in .gitattributes, but
        # strip any stray CR anyway so a hand-edited list cannot break the guard.
        old=$(printf %s "$old" | tr -d '\015' | sed 's/^ *//; s/ *$//')
        replacement=$(printf %s "$replacement" | tr -d '\015' | sed 's/^ *//; s/ *$//')
        [ -z "$old" ] && continue
        case "$old" in \#*) continue ;; esac

        # Never remove something this build also ships under that name.
        for s in "${shipping[@]}"; do
            [ "$s" = "$old" ] && { old=""; break; }
        done
        [ -z "$old" ] && continue

        # Both locations: a superseded bundle left in /Library shadows the new
        # one just as well as one left in ~/Library.
        for victim in "$user_vst3/$old.vst3" "$user_au/$old.component" \
                      "$sys_vst3/$old.vst3"  "$sys_au/$old.component"; do
            if [ -e "$victim" ]; then
                [ "$removed" -eq 0 ] && echo "Removing superseded bundles"
                if rm -rf "$victim" 2>/dev/null; then
                    echo "  - ${victim#"$HOME"/}   (now $replacement)"
                    removed=$((removed + 1))
                else
                    echo "  ! could not remove $victim -- needs sudo; delete it by hand"
                fi
            fi
        done
    done < "$here/superseded.txt"
    [ "$removed" -gt 0 ] && echo
fi

# -- 3. copy ---------------------------------------------------------------
# Each bundle is removed before it is copied. BSD cp -R -- which is what runs
# on macOS -- copies a directory INTO an existing directory of the same name,
# so upgrading in place without this leaves BMO Opto.vst3/BMO Opto.vst3 and a
# plugin the DAW cannot load. GNU cp does not, so the fault does not reproduce
# when this script is tested under Git Bash on Windows.
install_bundle() {
    target="$2/$(basename "$1")"
    rm -rf "$target"
    cp -R "$1" "$2/"
}

mkdir -p "$user_vst3"
echo "Installing -> $user_vst3"
for b in "$here/VST3/"*.vst3; do install_bundle "$b" "$user_vst3"; done

if [ -d "$here/AU" ] && compgen -G "$here/AU/*.component" > /dev/null; then
    mkdir -p "$user_au"
    echo "Installing -> $user_au"
    for b in "$here/AU/"*.component; do install_bundle "$b" "$user_au"; done
fi
echo

# -- 4. quarantine, in both locations --------------------------------------
# Per bundle by name, not by a BMO* glob: LTV Comp is the one product whose
# name does not begin with BMO, and the glob the old README gave left exactly
# that one quarantined and looking broken after everything else worked.
if ! command -v xattr > /dev/null 2>&1; then
    echo "No xattr on this system -- skipping the quarantine step."
    echo "(That is normal off macOS; this script is only needed there.)"
else
    echo "Clearing the quarantine flag"
    cleared=0
    needs_sudo=""
    for name in "${shipping[@]}"; do
        for p in "$user_vst3/$name.vst3" "$user_au/$name.component" \
                 "$sys_vst3/$name.vst3"  "$sys_au/$name.component"; do
            [ -e "$p" ] || continue
            # Writability is the question, so ask it directly rather than
            # reading xattr's exit code -- which is also non-zero when there
            # was simply no flag to remove.
            if [ -w "$p" ]; then
                xattr -dr com.apple.quarantine "$p" 2>/dev/null
                cleared=$((cleared + 1))
            else
                needs_sudo="$needs_sudo$p
"
            fi
        done
    done
    echo "  cleared on $cleared bundle(s), across ~/Library and /Library"

    if [ -n "$needs_sudo" ]; then
        echo
        echo "  These are not writable by you -- almost always the system-wide"
        echo "  /Library copies. Clear them in one go:"
        echo
        printf '%s' "$needs_sudo" | while IFS= read -r p; do
            [ -n "$p" ] && echo "    sudo xattr -dr com.apple.quarantine \"$p\""
        done
    fi
fi

echo "Done. Rescan plugins in your DAW."
echo
echo "Standalone apps were not installed; they are in Standalone/ if you want them."
