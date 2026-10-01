#!/usr/bin/env bash
# Regenerate sql/ from the samples in Studio (core/sampledatabase.cpp), after
# changing one:
#
#   tools/sample-servers/generate.sh [path/to/qivot-studio]
#   tools/sample-servers/generate.sh --check [path/to/qivot-studio]   # fail if sql/ is out of date (a test)
#
# The scripts are gzipped without timestamps, so unchanged samples give
# unchanged files.
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"
CHECK=0
if [ "${1:-}" = "--check" ]; then CHECK=1; shift; fi
APP="${1:-}"
if [ -z "$APP" ]; then
    for candidate in "$ROOT/build/app/Qivot Studio.app/Contents/MacOS/Qivot Studio" "$ROOT/build/app/qivot-studio"; do
        [ -x "$candidate" ] && APP="$candidate" && break
    done
fi
[ -x "$APP" ] || { echo "Build Studio first, or pass the path to qivot-studio." >&2; exit 1; }

OUT="$(mktemp -d)"
trap 'rm -rf "$OUT"' EXIT
"$APP" --sample-sql "$OUT"
if [ "$CHECK" = 1 ]; then
    stale=0
    for dialect in postgres mysql sqlserver; do
        for f in "$OUT/$dialect"/*.sql; do
            committed="$HERE/sql/$dialect/$(basename "$f").gz"
            if [ ! -f "$committed" ] || ! gzip -dc "$committed" | cmp -s - "$f"; then
                echo "out of date: sql/$dialect/$(basename "$f").gz" >&2
                stale=1
            fi
        done
    done
    [ "$stale" = 0 ] || { echo "Run tools/sample-servers/generate.sh to update them." >&2; exit 1; }
    echo "sql/ is up to date"
    exit 0
fi
for dialect in postgres mysql sqlserver; do
    rm -f "$HERE/sql/$dialect"/*.sql.gz
    mkdir -p "$HERE/sql/$dialect"
    for f in "$OUT/$dialect"/*.sql; do
        gzip -9nc "$f" > "$HERE/sql/$dialect/$(basename "$f").gz"
    done
done
echo "Wrote $HERE/sql"
