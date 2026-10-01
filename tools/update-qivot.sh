#!/usr/bin/env bash
# Update third_party/qivot to another Qivot: its single header (dist/qivot.hpp)
# and licence files, from GitHub (a branch, tag or commit; default main) or a
# local checkout.
#
#   tools/update-qivot.sh [ref | path/to/Qivot]
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
DEST="$ROOT/third_party/qivot"
SRC="${1:-main}"

if [ -d "$SRC" ]; then
    for f in dist/qivot.hpp LICENSE.txt NOTICE.txt; do cp "$SRC/$f" "$DEST/"; done
    ref="$(git -C "$SRC" rev-parse --short HEAD 2>/dev/null || echo local)"
else
    base="https://raw.githubusercontent.com/austinkottke/Qivot/$SRC"
    for f in dist/qivot.hpp LICENSE.txt NOTICE.txt; do
        curl -fsSL "$base/$f" -o "$DEST/$(basename "$f")"
    done
    ref="$SRC"
fi
echo "third_party/qivot is now Qivot $ref. Update the commit in third_party/qivot/README.md, then build and run ctest."
