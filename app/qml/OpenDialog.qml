import QtQuick 2.15
import QtQuick.Dialogs

/// Picks a SQLite file. Qt 5 builds use compat/qt5/OpenDialog.qml instead,
/// since QtQuick.Dialogs changed between the two.
FileDialog {
    signal picked(url file)
    title: "Open SQLite database"
    nameFilters: [ "Databases (*.db *.sqlite *.sqlite3 *.db3 *.duckdb *.ddb)", "SQLite (*.db *.sqlite *.sqlite3 *.db3)", "DuckDB (*.duckdb *.ddb)", "All files (*)" ]
    onAccepted: picked(selectedFile)
}
