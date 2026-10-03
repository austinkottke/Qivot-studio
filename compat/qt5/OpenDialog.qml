import QtQuick 2.15
import QtQuick.Dialogs 1.3

/// Qt 5's version of app/qml/OpenDialog.qml (fileUrl rather than selectedFile).
FileDialog {
    signal picked(url file)
    title: "Open SQLite database"
    nameFilters: [ "Databases (*.db *.sqlite *.sqlite3 *.db3 *.duckdb *.ddb)", "SQLite (*.db *.sqlite *.sqlite3 *.db3)", "DuckDB (*.duckdb *.ddb)", "All files (*)" ]
    onAccepted: picked(fileUrl)
}
