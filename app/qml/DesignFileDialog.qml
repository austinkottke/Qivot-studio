import QtQuick 2.15
import QtQuick.Dialogs

/// Opens or saves a .qivotdesign file. Qt 5 builds use compat/qt5/DesignFileDialog.qml.
FileDialog {
    property bool saving: false
    signal picked(url file)
    title: saving ? "Save the design" : "Open a design"
    fileMode: saving ? FileDialog.SaveFile : FileDialog.OpenFile
    nameFilters: [ "Qivot Studio designs (*.qivotdesign)", "All files (*)" ]
    defaultSuffix: "qivotdesign"
    onAccepted: picked(selectedFile)
}
