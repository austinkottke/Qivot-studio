#!/usr/bin/env bash
# Update third_party/qivot to another Qivot: its single header (dist/qivot.hpp),
# its DuckDB driver (drivers/duckdb) and licence files, from GitHub (a branch,
# tag or commit; default main) or a local checkout.
#
#   tools/update-qivot.sh [ref | path/to/Qivot]
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
DEST="$ROOT/third_party/qivot"
SRC="${1:-main}"

FILES="dist/qivot.hpp LICENSE.txt NOTICE.txt drivers/duckdb/duckdbdriver.h drivers/duckdb/duckdbdriver.cpp"
mkdir -p "$DEST/drivers/duckdb"
# Where each lands: the header and licences at the top, the driver under drivers/.
dest_of() { case "$1" in drivers/*) echo "$DEST/$1" ;; *) echo "$DEST/$(basename "$1")" ;; esac; }

if [ -d "$SRC" ]; then
    for f in $FILES; do cp "$SRC/$f" "$(dest_of "$f")"; done
    ref="$(git -C "$SRC" rev-parse --short HEAD 2>/dev/null || echo local)"
else
    base="https://raw.githubusercontent.com/austinkottke/Qivot/$SRC"
    for f in $FILES; do
        curl -fsSL "$base/$f" -o "$(dest_of "$f")"
    done
    ref="$SRC"
fi
echo "third_party/qivot is now Qivot $ref. Update the commit in third_party/qivot/README.md, then build and run ctest."
