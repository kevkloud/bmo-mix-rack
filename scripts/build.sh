#!/usr/bin/env bash
# Developer build: configure (once), build everything, run the tests.
#
#   scripts/build.sh              build + test
#   scripts/build.sh --snapshots  also render every panel into snapshots/
#
# Live holds plugin bundles open while it runs, so the copy-after-build step
# can fail quietly and leave you auditioning a stale binary. Quit Live first.

set -euo pipefail
cd "$(dirname "$0")/.."

config=Debug

if [[ ! -f build/CMakeCache.txt ]]; then
    # CMAKE_OSX_ARCHITECTURES only means anything to the Apple toolchain, and
    # CMAKE_BUILD_TYPE only to a single-config generator. Both are harmless
    # elsewhere, but passing them where they do nothing is how a script starts
    # looking like it knows something it does not.
    args=(-DCMAKE_BUILD_TYPE="$config")
    [[ $OSTYPE == darwin* ]] && args+=(-DCMAKE_OSX_ARCHITECTURES=arm64)

    cmake -B build "${args[@]}"
fi

# Visual Studio and Xcode are multi-config: they ignore CMAKE_BUILD_TYPE, want
# the configuration named on every command, and put binaries in a per-config
# subdirectory. Makefiles and Ninja are single-config and do none of that. The
# cache says which kind this build directory is.
#
# Until 0.2.3 this script assumed single-config throughout, so on Windows it
# built the wrong way round, ran ctest against a build it could not find tests
# in, and then looked for the snapshot tool one directory above where it is.
if grep -q '^CMAKE_CONFIGURATION_TYPES:' build/CMakeCache.txt; then
    build_args=(--config "$config")
else
    build_args=()
fi

cmake --build build --parallel "${build_args[@]}"

# -C is required on a multi-config build and ignored on a single-config one.
ctest --test-dir build -C "$config" --output-on-failure

if [[ ${1:-} == --snapshots ]]; then
    snapshot=
    for candidate in "build/tools/$config/snapshot.exe" "build/tools/$config/snapshot" \
                     "build/tools/snapshot.exe"        "build/tools/snapshot"; do
        if [[ -x $candidate ]]; then
            snapshot=$candidate
            break
        fi
    done

    if [[ -z $snapshot ]]; then
        echo "build.sh: snapshot tool not found under build/tools" >&2
        exit 1
    fi

    mkdir -p snapshots

    for module in eq sat util opto dim deq vcomp; do
        "$snapshot" "$module" "snapshots/$module.png"
    done

    "$snapshot" rack snapshots/rack.png chain=util,eq,sat,opto,dim,deq,vcomp
fi
