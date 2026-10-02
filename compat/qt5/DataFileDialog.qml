import QtQuick 2.15
import QtQuick.Dialogs 1.3

/// Qt 5's version of app/qml/DataFileDialog.qml.
FileDialog {
    property bool saving: false
    property string kind: "csv"
    signal picked(url file)
    title: saving ? (kind === "sql" ? "Save the SQL" : /png|svg|pdf/.test(kind) ? "Export the diagram as " + kind.toUpperCase()
                                                     : "Export as " + kind.toUpperCase()) : "Import a CSV file"
    selectExisting: !saving
    nameFilters: kind === "json" ? [ "JSON (*.json)", "All files (*)" ]
               : kind === "sql"  ? [ "SQL (*.sql)", "All files (*)" ]
               : kind === "png"  ? [ "PNG image (*.png)" ]
               : kind === "svg"  ? [ "SVG image (*.svg)" ]
               : kind === "pdf"  ? [ "PDF (*.pdf)" ]
                                 : [ "CSV (*.csv *.tsv *.txt)", "All files (*)" ]
    onAccepted: picked(fileUrl)
}
