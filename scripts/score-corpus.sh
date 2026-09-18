#!/usr/bin/env bash
# The whole synthetic corpus, scored: generate, render, score, one table.
#
#   scripts/score-corpus.sh                      defaults: 48 kHz, CLASSIC, Live
#   scripts/score-corpus.sh --rate 44100 --set scale=Minor --set key=A
#
# Writes corpus/ (the signals), renders/ (the outputs and analysis dumps) and
# reports/corpus-<stamp>.csv, one row per item. Nothing here is committed;
# the generator is the source of truth and regenerates the same bytes.

set -euo pipefail
cd "$(dirname "$0")/.."

rate=48000
sets=()
while [[ $# -gt 0 ]]; do
    case $1 in
        --rate) rate=$2; shift 2 ;;
        --set)  sets+=(--set "$2"); shift 2 ;;
        *) echo "usage: $0 [--rate hz] [--set id=value ...]" >&2; exit 2 ;;
    esac
done

bin=
for candidate in build/tools/tune/Release build/tools/tune build-dsp/tools/tune/Release build-dsp/tools/tune; do
    [[ -x $candidate/bmo-tune-cli.exe || -x $candidate/bmo-tune-cli ]] && { bin=$candidate; break; }
done
[[ -n $bin ]] || { echo "score-corpus: build the tools first (see WORKFLOWS.md)" >&2; exit 1; }

exe() { if [[ -x $bin/$1.exe ]]; then echo "$bin/$1.exe"; else echo "$bin/$1"; fi; }
gen=$(exe bmo-tune-gen); cli=$(exe bmo-tune-cli); score=$(exe bmo-tune-score)

corpus=corpus/$rate
renders=renders/$rate
mkdir -p "$renders" reports
"$gen" "$corpus" --rate "$rate"

summary=reports/corpus-$(date +%Y%m%d-%H%M%S)-$rate.csv

tail -n +2 "$corpus/manifest.csv" | while IFS=, read -r name group _ _; do
    "$cli" "$corpus/$name.wav" "$renders/$name.wav" "${sets[@]}" \
        --dump-analysis "$renders/$name.analysis.csv" 2>/dev/null
    "$score" "$renders/$name.analysis.csv" "$corpus/$name.f0.csv" --rate "$rate" \
        --summary "$summary" --item "$name" > /dev/null
done

echo "score-corpus: $summary"
column -s, -t < "$summary" 2>/dev/null || cat "$summary"
