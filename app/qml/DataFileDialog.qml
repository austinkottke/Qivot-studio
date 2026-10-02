import QtQuick 2.15
import QtQuick.Dialogs

/// Saves an export (CSV or JSON) or opens a CSV to import. Qt 5 builds use compat/qt5/DataFileDialog.qml.
FileDialog {
    property bool saving: false
    property string kind: "csv"               // "csv" or "json"
    signal picked(url file)
    title: saving ? (kind === "sql" ? "Save the SQL" : /png|svg|pdf/.test(kind) ? "Export the diagram as " + kind.toUpperCase()
                                                     : "Export as " + kind.toUpperCase()) : "Import a CSV file"
    fileMode: saving ? FileDialog.SaveFile : FileDialog.OpenFile
    nameFilters: kind === "json" ? [ "JSON (*.json)", "All files (*)" ]
               : kind === "sql"  ? [ "SQL (*.sql)", "All files (*)" ]
               : kind === "png"  ? [ "PNG image (*.png)" ]
               : kind === "svg"  ? [ "SVG image (*.svg)" ]
               : kind === "pdf"  ? [ "PDF (*.pdf)" ]
                                 : [ "CSV (*.csv *.tsv *.txt)", "All files (*)" ]
    defaultSuffix: kind
    onAccepted: picked(selectedFile)
}
