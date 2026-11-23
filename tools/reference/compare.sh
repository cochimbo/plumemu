#!/usr/bin/env bash
# Compares two run.sh results (by default, the saved reference with a new run) and
# says which runs changed. Exit code 0 when they are the same.
#
#   compare.sh NEW [REFERENCE]
set -u
here=$(dirname "$(realpath "$0")")
new=$1; ref=${2:-$here/reference.txt}
if diff <(grep -v '^#' "$ref" | sort) <(grep -v '^#' "$new" | sort) > /dev/null; then
    echo "Same as the reference: $(grep -vc '^#' "$new") runs."
    exit 0
fi
echo "Different from the reference (< reference, > new):"
diff <(grep -v '^#' "$ref" | sort) <(grep -v '^#' "$new" | sort) | grep '^[<>]'
exit 1
