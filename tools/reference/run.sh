#!/usr/bin/env bash
# Runs every line of runs.txt and writes one line per run to OUT:
#
#   name  exit-code  last-picture-hash  sound-hash  per-frame-log-hash
#
#   run.sh SEGA_HEADLESS NINTENDO_HEADLESS ROMS_DIR OUT [NAME_FILTER]
#
# SEGA_HEADLESS is plumsg-headless, NINTENDO_HEADLESS plumnin-headless (either can be
# "-" to skip that app's runs). ROMS_DIR is what fetch.sh filled. NAME_FILTER, a grep
# -E pattern, runs only the matching names. Compare two OUT files with compare.sh.
set -u
sega=$1; nintendo=$2; roms=$(realpath "$3"); out=$4; filter=${5:-.}
here=$(dirname "$(realpath "$0")")
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
: > "$out"

trim() { sed -E 's/^[[:space:]]+//; s/[[:space:]]+$//' <<< "$1"; }

while IFS='|' read -r app name game frames extra; do
    [[ "$app" =~ ^[[:space:]]*(#|$) ]] && continue
    app=$(trim "$app"); name=$(trim "$name"); game=$(trim "$game"); frames=$(trim "$frames"); extra=$(trim "$extra")
    grep -qE "$filter" <<< "$name" || continue
    case "$app" in
        sega) bin=$sega ;;
        nintendo) bin=$nintendo ;;
        *) echo "runs.txt: unknown app $app" >&2; continue ;;
    esac
    [[ "$bin" == "-" ]] && continue
    [[ "$game" != --* ]] && game="$roms/$game"
    extra=${extra//@SF2/$roms/GeneralUser-GS.sf2}
    extra=${extra//@DIR/$roms}
    eval "args=($extra)"
    rm -f "$work"/*
    "$bin" "$game" --frames "$frames" "${args[@]}" --hash-log "$work/log.txt" --json "$work/run.json" \
        --quiet > "$work/stdout.txt" 2>&1 < /dev/null
    code=$?
    picture=$(grep -oE '"hash": "[0-9a-f]+"' "$work/run.json" 2>/dev/null | grep -oE '[0-9a-f]{8}' || echo -)
    sound=$(grep -oE '"audio_hash": "[0-9a-f]+"' "$work/run.json" 2>/dev/null | grep -oE '[0-9a-f]{8}' || echo -)
    log=$( [[ -s "$work/log.txt" ]] && sha1sum "$work/log.txt" | cut -c1-12 || echo -)
    printf '%-24s %3s %s %s %s\n' "$name" "$code" "${picture:--}" "${sound:--}" "$log" | tee -a "$out"
done < "$here/runs.txt"
