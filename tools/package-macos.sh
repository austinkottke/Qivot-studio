#!/usr/bin/env bash
# Build Qivot Studio for release and wrap it in a drag-to-Applications DMG.
#
#   tools/package-macos.sh <qt-prefix> [version] [out-dir]
#
#   <qt-prefix>  the Qt to build with, e.g. ~/Qt/6.8.3/macos (needs bin/macdeployqt)
#   [version]    goes in the DMG's name (default: the project version)
#   [out-dir]    where the DMG lands (default: dist/)
#
# Builds a universal binary (Apple Silicon and Intel) when Qt is universal.
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
DMG="$OUT/Qivot-Studio-$VERSION-macOS.dmg"

# Universal if Qt is.
ARCHS="$(lipo -archs "$QT/lib/QtCore.framework/QtCore" 2>/dev/null || uname -m)"
ARCHS="${ARCHS// /;}"

echo "== Building $VERSION for $ARCHS"
cmake -S "$ROOT" -B "$BUILD" -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH="$QT" \
      -DCMAKE_OSX_ARCHITECTURES="$ARCHS" -DCMAKE_OSX_DEPLOYMENT_TARGET=12.0 >/dev/null
cmake --build "$BUILD" --target qivot-studio --parallel

echo "== Bundling Qt into the app"
# The QML folders tell macdeployqt which Qt Quick modules the screens import.
"$QT/bin/macdeployqt" "$APP" -qmldir="$ROOT/app/qml" -qmldir="$ROOT/ui" -always-overwrite
# Mimer SQL isn't something Studio connects to; drop its driver.
rm -f "$APP/Contents/PlugIns/sqldrivers/libqsqlmimer.dylib"
# Qt's database drivers whose client library isn't on every Mac stay in: the
# app checks which drivers load and only offers those (PostgreSQL loads when
# Postgres.app is installed, SQL Server with Microsoft's ODBC driver).

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
