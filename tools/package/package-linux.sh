#!/usr/bin/env bash
# Build Qivot Studio for release and wrap it in an AppImage: one file that runs
# on most Linux distributions, with Qt and the database drivers' libraries in it.
#
#   tools/package/package-linux.sh <qt-prefix> [version] [out-dir]
#
# Run tools/package/build-sql-drivers.py first, so Qt has PostgreSQL and MySQL
# drivers to bundle. REQUIRE_DRIVERS (e.g. "QPSQL,QMYSQL") must load in the
# finished AppImage, and each URL in TRY_CONNECT (space-separated) must connect,
# or the build fails. Needs FUSE-less AppImage tools: they're run extracted.
set -euo pipefail

QT="${1:?usage: tools/package/package-linux.sh <qt-prefix> [version] [out-dir]}"
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
VERSION="${2:-$(sed -n 's/^ *VERSION \([0-9.]*\)$/\1/p' "$ROOT/CMakeLists.txt" | head -1)}"
OUT="${3:-$ROOT/dist}"
BUILD="$ROOT/build-release"
WORK="$ROOT/build-appimage"
ARCH="$(uname -m)"
export APPIMAGE_EXTRACT_AND_RUN=1      # no FUSE needed

echo "== Building $VERSION"
cmake -S "$ROOT" -B "$BUILD" -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH="$QT" >/dev/null
cmake --build "$BUILD" --target qivot-studio --parallel

echo "== The AppDir"
rm -rf "$WORK/AppDir"
mkdir -p "$WORK/AppDir/usr/bin" "$WORK/AppDir/usr/share/doc/qivot-studio"
cp "$BUILD/app/Qivot Studio" "$WORK/AppDir/usr/bin/qivot-studio"
cp "$ROOT/LICENSE" "$ROOT/THIRD-PARTY-NOTICES.md" "$WORK/AppDir/usr/share/doc/qivot-studio/"
cp "$ROOT/app/icons/qivot-studio-256.png" "$WORK/qivot-studio.png"
cat > "$WORK/qivot-studio.desktop" <<EOF
[Desktop Entry]
Type=Application
Name=Qivot Studio
Comment=Database designer and analyzer
Exec=qivot-studio
Icon=qivot-studio
Categories=Development;Database;
EOF

# Drivers whose client library isn't on this machine can't be bundled (and
# linuxdeploy stops at a missing library): leave them out. Mimer SQL isn't
# something Studio connects to.
rm -f "$QT/plugins/sqldrivers/libqsqlmimer.so"
for d in "$QT"/plugins/sqldrivers/*.so; do
    if ldd "$d" | grep -q "not found"; then
        echo "   leaving out $(basename "$d"): $(ldd "$d" | awk '/not found/ {print $1}' | tr '\n' ' ')"
        rm -f "$d"
    fi
done

echo "== linuxdeploy"
mkdir -p "$WORK/tools"
for tool in linuxdeploy linuxdeploy-plugin-qt; do
    f="$WORK/tools/$tool-$ARCH.AppImage"
    if [ ! -x "$f" ]; then
        curl -fsSL -o "$f" "https://github.com/linuxdeploy/$tool/releases/download/continuous/$tool-$ARCH.AppImage"
        chmod +x "$f"
    fi
done
export PATH="$WORK/tools:$PATH"
ln -sf "$WORK/tools/linuxdeploy-plugin-qt-$ARCH.AppImage" "$WORK/tools/linuxdeploy-plugin-qt"
export QMAKE="$QT/bin/qmake"
export LD_LIBRARY_PATH="$QT/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
export QML_SOURCES_PATHS="$ROOT/app/qml:$ROOT/ui"
export EXTRA_PLATFORM_PLUGINS="libqoffscreen.so;libqwayland-generic.so"
export EXTRA_QT_MODULES="sql;svg"
export OUTPUT="$WORK/Qivot-Studio-$VERSION-Linux-$ARCH.AppImage"
rm -f "$OUTPUT"
(cd "$WORK" && "$WORK/tools/linuxdeploy-$ARCH.AppImage" --appdir AppDir \
    --executable AppDir/usr/bin/qivot-studio \
    --desktop-file qivot-studio.desktop --icon-file qivot-studio.png \
    --plugin qt --output appimage)

echo "== Database drivers"
args=(--list-drivers)
[ -n "${REQUIRE_DRIVERS:-}" ] && args+=(--require-drivers "$REQUIRE_DRIVERS")
for url in ${TRY_CONNECT:-}; do args+=(--try-connect "$url"); done
QT_QPA_PLATFORM=offscreen "$OUTPUT" "${args[@]}"

mkdir -p "$OUT"
mv "$OUTPUT" "$OUT/"
echo "== $OUT/$(basename "$OUTPUT") ($(du -h "$OUT/$(basename "$OUTPUT")" | cut -f1))"
