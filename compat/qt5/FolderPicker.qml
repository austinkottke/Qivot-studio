import QtQuick 2.15
import QtQuick.Dialogs 1.3

/// Qt 5's version of app/qml/FolderPicker.qml.
FileDialog {
    signal picked(url folder)
    title: "Choose where to put the project"
    selectFolder: true
    onAccepted: picked(fileUrl)
}
