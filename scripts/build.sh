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
#
# --parallel is seeded into the array rather than passed alongside it because
# macOS ships bash 3.2, where expanding an empty array under `set -u` is an
# unbound-variable error. A single-config build left the array empty, so this
# script aborted before it compiled anything on any Mac; Windows is
# multi-config, never took that branch, and never showed it.
build_args=(--parallel)

if grep -q '^CMAKE_CONFIGURATION_TYPES:' build/CMakeCache.txt; then
    build_args+=(--config "$config")
fi

cmake --build build "${build_args[@]}"

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

    # By the name `snapshot` takes, which is the module's id rather than the
    # product's display name -- BMO Defang is `deesser`, BMO Linger is `reverb`,
    # BMO FET is `fetcomp` -- except LTV Comp, which `snapshot` knows only as
    # `ltvcomp`, never `vcomp`. One loop and one rack render, both covering the
    # whole registry: the FET change added a second copy of each rather than
    # extending these, and its rack render overwrote the first with a chain that
    # was missing BMO Defang.
    for module in eq sat util opto dim deq ltvcomp deesser fetcomp reverb; do
        "$snapshot" "$module" "snapshots/$module.png"
    done

    # The rack has eight slots (`RackProcessor::kSlots`) and the registry has ten
    # modules, so one rack render cannot hold them all: `addModule` refuses a
    # ninth without a word, and the ten-module chain this used to name rendered
    # exactly these eight. BMO FET and BMO Linger are the two left out, and each
    # is covered by its own snapshot in the loop above. Written as eight so the
    # picture is what the line says.
    "$snapshot" rack snapshots/rack.png chain=util,eq,sat,opto,dim,deq,ltvcomp,deesser
fi
