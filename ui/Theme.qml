pragma Singleton
import QtQuick 2.15

/// Design tokens for every QivotUI component. Follows the system's light/dark
/// setting; set `dark` explicitly to override it.
// Colours with alpha are #AARRGGBB (Qt's order), not CSS's #RRGGBBAA.
QtObject {
    // Qt 6.5+ reports the colour scheme; Qt 5 doesn't, so judge the window palette.
    property bool dark: Qt.styleHints.colorScheme !== undefined
                        ? Qt.styleHints.colorScheme === 2       // Qt.ColorScheme.Dark
                        : systemPalette.window.hslLightness < 0.5
    readonly property SystemPalette systemPalette: SystemPalette {}

    // Surfaces
    readonly property color window:        dark ? "#1C1C1E" : "#F5F5F7"
    readonly property color sidebar:       dark ? "#232326" : "#ECECEF"
    readonly property color surface:       dark ? "#2C2C2E" : "#FFFFFF"
    readonly property color surfaceRaised: dark ? "#3A3A3C" : "#F2F2F5"
    readonly property color separator:     dark ? "#3D3D41" : "#E0E0E5"
    readonly property color hover:         dark ? "#14FFFFFF" : "#0A000000"
    readonly property color pressed:       dark ? "#24FFFFFF" : "#14000000"

    // Text
    readonly property color text:          dark ? "#F5F5F7" : "#1D1D1F"
    readonly property color textSecondary: dark ? "#A1A1A6" : "#6E6E73"
    readonly property color textTertiary:  dark ? "#6E6E73" : "#A1A1A6"

    // Accents
    readonly property color accent:        dark ? "#0A84FF" : "#007AFF"
    readonly property color accentSoft:    dark ? "#330A84FF" : "#1F007AFF"
    readonly property color key:           dark ? "#FFD60A" : "#B07D00"   // primary keys
    readonly property color keySoft:       dark ? "#26FFD60A" : "#26FFCC00"
    readonly property color link:          dark ? "#BF5AF2" : "#8944AB"   // foreign keys
    readonly property color linkSoft:      dark ? "#26BF5AF2" : "#1FAF52DE"
    readonly property color positive:      dark ? "#30D158" : "#248A3D"
    readonly property color warning:       dark ? "#FF9F0A" : "#C93400"
    readonly property color danger:        dark ? "#FF453A" : "#D70015"

    // Shape and type
    readonly property int radius:      10
    readonly property int radiusSmall: 6
    readonly property int fontTitle:   24
    readonly property int fontHeading: 15
    readonly property int fontBody:    13
    readonly property int fontSmall:   11
    readonly property string monoFont: Qt.platform.os === "osx" || Qt.platform.os === "ios" ? "Menlo"
                                     : Qt.platform.os === "windows" ? "Consolas" : "DejaVu Sans Mono"
}
