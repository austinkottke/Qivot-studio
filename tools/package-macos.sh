#!/usr/bin/env bash
# Build Qivot Studio for release and wrap it in a drag-to-Applications DMG.
#
#   tools/package-macos.sh <qt-prefix> [version] [out-dir]
#
#   <qt-prefix>  the Qt to build with, e.g. ~/Qt/6.8.3/macos (needs bin/macdeployqt)
#   [version]    goes in the DMG's name (default: the project version)
#   [out-dir]    where the DMG lands (default: dist/)
#
# Builds a universal binary (Apple Silicon and Intel) when Qt is universal, or
# the architectures in MACOS_ARCHS (e.g. "arm64"): the database client libraries
# bundled with the drivers are this Mac's, so a DMG with them is built per
# architecture (tools/package/build-sql-drivers.py first; see release.yml).
# REQUIRE_DRIVERS (default: none) lists drivers that must load in the finished
# app, e.g. "QPSQL,QMYSQL"; the build fails if one doesn't.
# Signing: set MACOS_SIGN_IDENTITY to a "Developer ID Application: ..." identity
# in the keychain to sign properly; otherwise the app is signed ad hoc (it runs,
# but macOS asks the user to right-click > Open the first time).
set -euo pipefail

QT="${1:?usage: tools/package-macos.sh <qt-prefix> [version] [out-dir]}"
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
VERSION="${2:-$(sed -n 's/^ *VERSION \([0-9.]*\)$/\1/p' "$ROOT/CMakeLists.txt" | head -1)}"
OUT="${3:-$ROOT/dist}"
BUILD="$ROOT/build-release"
APP="$BUILD/app/Qivot Studio.app"

# Universal if Qt is, unless told otherwise.
ARCHS="${MACOS_ARCHS:-$(lipo -archs "$QT/lib/QtCore.framework/QtCore" 2>/dev/null || uname -m)}"
ARCHS="${ARCHS// /;}"
case "$ARCHS" in
    *";"*) SUFFIX="macOS" ;;
    arm64) SUFFIX="macOS-AppleSilicon" ;;
    *)     SUFFIX="macOS-Intel" ;;
esac
DMG="$OUT/Qivot-Studio-$VERSION-$SUFFIX.dmg"

echo "== Building $VERSION for $ARCHS"
cmake -S "$ROOT" -B "$BUILD" -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH="$QT" \
      -DCMAKE_OSX_ARCHITECTURES="$ARCHS" -DCMAKE_OSX_DEPLOYMENT_TARGET=12.0 >/dev/null
cmake --build "$BUILD" --target qivot-studio --parallel

# Qt's ODBC driver looks for iODBC where Homebrew put it on the Mac Qt was built
# on; point it at this Mac's, if there is one, so it's bundled too.
ODBC="$QT/plugins/sqldrivers/libqsqlodbc.dylib"
if [ -f "$ODBC" ] && command -v brew >/dev/null && [ -f "$(brew --prefix)/opt/libiodbc/lib/libiodbc.2.dylib" ]; then
    wanted="$(otool -L "$ODBC" | awk '/libiodbc/ {print $1}' | head -1)"
    here="$(brew --prefix)/opt/libiodbc/lib/libiodbc.2.dylib"
    if [ -n "$wanted" ] && [ "$wanted" != "$here" ]; then
        chmod u+w "$ODBC"
        install_name_tool -change "$wanted" "$here" "$ODBC"
    fi
fi

echo "== Bundling Qt into the app"
# The QML folders tell macdeployqt which Qt Quick modules the screens import.
"$QT/bin/macdeployqt" "$APP" -qmldir="$ROOT/app/qml" -qmldir="$ROOT/ui" -always-overwrite
# Mimer SQL isn't something Studio connects to; drop its driver.
rm -f "$APP/Contents/PlugIns/sqldrivers/libqsqlmimer.dylib"

# A DMG for one architecture carries only that architecture: Qt's frameworks and
# DuckDB's library come universal, and half of each would never run.
case "$ARCHS" in
    *";"*) ;;
    *)
        echo "== Keeping only $ARCHS"
        while IFS= read -r -d '' f; do
            file "$f" | grep -q "Mach-O universal" || continue
            lipo "$f" -thin "$ARCHS" -output "$f.thin" && mv "$f.thin" "$f"
        done < <(find "$APP" -type f -print0)
        ;;
esac

echo "== Checking nothing loads from outside the app"
# Every library a binary in the app loads is the system's or in the app. A
# driver whose library couldn't be bundled goes (the app offers only drivers
# that load); anything else outside is an error.
# (otool's dependency lines are the indented ones; a universal binary has a
# header line per architecture.)
outside() {
    otool -L "$1" | grep -E '^[[:space:]]' | awk '{print $1}' | sort -u \
        | grep -vE '^(/System/|/usr/lib/|@rpath/|@loader_path/|@executable_path/)' \
        | grep -vxF "$(otool -D "$1" | grep -v ':$' | sort -u)" || true
}
check_bundle() {
    bad=""
    while IFS= read -r -d '' f; do
        file "$f" | grep -q Mach-O || continue
        refs="$(outside "$f")"
        [ -z "$refs" ] && continue
        case "$f" in
            */PlugIns/sqldrivers/*)
                echo "   dropping $(basename "$f"): needs $refs"; rm -f "$f" ;;
            */Contents/Frameworks/*.dylib)
                # A database client library that came with what it needs only
                # partly: its drivers go with it.
                lib="$(basename "$f")"
                users=()
                for d in "$APP/Contents/PlugIns/sqldrivers/"*.dylib; do
                    [ -f "$d" ] && grep -q "$lib" "$d" && users+=("$d")
                done
                if [ ${#users[@]} -gt 0 ]; then
                    echo "   dropping $lib and $(for u in "${users[@]}"; do printf '%s ' "$(basename "$u")"; done)(it needs $(echo $refs))"
                    rm -f "$f" "${users[@]}"
                else
                    bad="$bad$f -> $refs"$'\n'
                fi ;;
            *) bad="$bad$f -> $refs"$'\n' ;;
        esac
    done < <(find "$APP" -type f -print0)
}
check_bundle
check_bundle          # again, now that anything dropped is gone
if [ -n "$bad" ]; then
    printf 'Loads from outside the app:\n%s' "$bad"
    exit 1
fi

echo "== Database drivers"
"$APP/Contents/MacOS/Qivot Studio" --list-drivers ${REQUIRE_DRIVERS:+--require-drivers "$REQUIRE_DRIVERS"}

# The licences go in the app (before signing, so they're sealed with it).
cp "$ROOT/LICENSE" "$ROOT/THIRD-PARTY-NOTICES.md" "$APP/Contents/Resources/"

echo "== Signing"
IDENTITY="${MACOS_SIGN_IDENTITY:--}"
if [ "$IDENTITY" = "-" ]; then
    codesign --force --deep --sign - "$APP"
else
    codesign --force --deep --options runtime --timestamp --sign "$IDENTITY" "$APP"
fi
codesign --verify --deep --strict "$APP"

echo "== Making the DMG"
STAGE="$(mktemp -d)"
cp -R "$APP" "$STAGE/"
ln -s /Applications "$STAGE/Applications"
cp "$ROOT/LICENSE" "$STAGE/LICENSE.txt"
cp "$ROOT/THIRD-PARTY-NOTICES.md" "$STAGE/Third-party notices.md"
mkdir -p "$OUT"
rm -f "$DMG"
hdiutil create -volname "Qivot Studio $VERSION" -srcfolder "$STAGE" -fs HFS+ -format UDZO -imagekey zlib-level=9 "$DMG" >/dev/null
rm -rf "$STAGE"
if [ "$IDENTITY" != "-" ]; then
    codesign --sign "$IDENTITY" --timestamp "$DMG"
fi

echo "== $DMG ($(du -h "$DMG" | cut -f1))"
