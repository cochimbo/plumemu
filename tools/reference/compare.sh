#!/usr/bin/env bash
# Compares two run.sh results (a new run with an older one: by default, the saved
# reference.txt) and says which runs changed. Exit code 0 when they are the same.
#
#   compare.sh NEW [OLD]
set -u
here=$(dirname "$(realpath "$0")")
new=$1; ref=${2:-$here/reference.txt}
if diff <(grep -v '^#' "$ref" | sort) <(grep -v '^#' "$new" | sort) > /dev/null; then
    echo "Same as $(basename "$ref"): $(grep -vc '^#' "$new") runs."
    exit 0
fi
echo "Different from $(basename "$ref") (< $(basename "$ref"), > new):"
diff <(grep -v '^#' "$ref" | sort) <(grep -v '^#' "$new" | sort) | grep '^[<>]'
exit 1
