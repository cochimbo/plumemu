#!/usr/bin/env bash
# Fills ROMS_DIR with what runs.txt needs: the SoundFont, the 240p Test Suite (from
# this repository) and the Game Boy, NES and SNES test ROMs (at fixed versions). What is already there is not downloaded again.
#
#   fetch.sh ROMS_DIR
set -eu
roms=$(mkdir -p "$1" && realpath "$1")
here=$(dirname "$(realpath "$0")")
repo=$(realpath "$here/../..")

get() {  # get URL FILE
    [[ -s "$2" ]] && return 0
    mkdir -p "$(dirname "$2")"
    curl -sSfL --retry 3 -o "$2.part" "$1" && mv "$2.part" "$2"
}

# GeneralUser GS, the SoundFont the installers ship, at a fixed commit.
get https://raw.githubusercontent.com/mrbumpy409/GeneralUser-GS/684543d5e5efaef08d02be50dcda8d552478fa60/GeneralUser-GS.sf2 \
    "$roms/GeneralUser-GS.sf2"

# The 240p Test Suite and its scripts: in the repository (plumsg's tests/240p).
for d in "$repo/sega/tests/240p" "$repo/tests/240p"; do
    if [[ -d "$d" ]]; then
        mkdir -p "$roms/240p"
        cp -r "$d/240pSuite-1.32.bin" "$d/guiones" "$roms/240p/"
        break
    fi
done

# Game Boy: c-sp's game-boy-test-roms v7.0.
if [[ ! -d "$roms/gb/blargg" ]]; then
    get https://github.com/c-sp/game-boy-test-roms/releases/download/v7.0/game-boy-test-roms-v7.0.zip "$roms/gbtr.zip"
    mkdir -p "$roms/gb" && unzip -qo "$roms/gbtr.zip" -d "$roms/gb"
fi

# NES and SNES: only the files runs.txt uses, at fixed commits.
nes=https://raw.githubusercontent.com/christopherpow/nes-test-roms/95d8f621ae55cee0d09b91519a8989ae0e64753b
snes=https://raw.githubusercontent.com/PeterLemon/SNES/350b394e86ec5d62f600b5cbf64cdce3721bb6ef
grep -oE '\| *(nes|snes)/[^|]+' "$here/runs.txt" | sed -E 's/^\| *//; s/ *$//' | sort -u |
while read -r path; do
    case "$path" in
        nes/*)  get "$nes/${path#nes/}" "$roms/$path" ;;
        snes/*) get "$snes/${path#snes/}" "$roms/$path" ;;
    esac || echo "warning: could not download $path" >&2
done
echo "ROMs in $roms"
