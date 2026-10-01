import QtQuick 2.15
import QtQuick.Controls 2.15

/// A right-click menu in the theme. Submenus (a Menu inside it) get themed
/// items too; use ContextMenuItem and ContextMenuSeparator for the rest.
Menu {
    id: menu
    padding: 6
    implicitWidth: 250
    delegate: ContextMenuItem { }
    background: Rectangle {
        implicitWidth: 250
        radius: 10
        color: Theme.surface
        border.width: 1
        border.color: Theme.separator
    }
}
