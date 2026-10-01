import QtQuick 2.15
import QtQuick.Dialogs 1.3

/// Qt 5's version of app/qml/DesignFileDialog.qml.
FileDialog {
    property bool saving: false
    signal picked(url file)
    title: saving ? "Save the design" : "Open a design"
    selectExisting: !saving
    nameFilters: [ "Qivot Studio designs (*.qivotdesign)", "All files (*)" ]
    onAccepted: picked(fileUrl)
}
