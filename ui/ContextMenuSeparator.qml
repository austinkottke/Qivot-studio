import QtQuick 2.15
import QtQuick.Controls 2.15

/// A hairline between groups in a ContextMenu.
MenuSeparator {
    topPadding: 4
    bottomPadding: 4
    contentItem: Rectangle { implicitHeight: 1; color: Theme.separator }
}
