import QtQuick 2.15
import QtQuick.Dialogs

/// Picks a folder. Qt 5 builds use compat/qt5/FolderPicker.qml instead.
FolderDialog {
    signal picked(url folder)
    title: "Choose where to put the project"
    onAccepted: picked(selectedFolder)
}
