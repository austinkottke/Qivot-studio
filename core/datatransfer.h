#ifndef DATATRANSFER_H
#define DATATRANSFER_H

#include <QObject>
#include <QPointer>
#include <QQmlEngine>
#include <QSqlQuery>
#include <QStringList>
#include <QVariantList>
#include <QVector>

#include "databasesession.h"

/// Rows to and from files.
namespace DataTransfer {

/// A local path from a `file:` URL or a plain path.
QString localPath(const QVariant &fileOrUrl);

/// Write every row of `query` (already run, forward-only is fine) to `path`
/// as "csv" (a header row; NULL as an empty cell) or "json" (an array of
/// objects; numbers as numbers, NULL as null). Streams: the rows are never
/// all in memory. `{ ok, rows, error, path }`.
QVariantMap write(QSqlQuery &query, const QString &path, const QString &format);

/// Parse CSV text: quoted fields (with "" for a quote, and line breaks inside),
/// any of , ; or tab as the delimiter. A leading byte-order mark is dropped.
QVector<QStringList> parseCsv(const QString &text, QChar delimiter);

/// The delimiter `text` most likely uses: , ; or tab (from its first line).
QChar guessDelimiter(const QString &text);

}

/// Importing a CSV file into a table.
/**
  load() reads the file and matches its columns to the table's by name;
  setMapping() changes that; run() inserts every row in one transaction on
  the session's writable connection (so changes must be allowed) — a row that
  fails stops it and nothing is imported.

\code
    CsvImport { id: importer; session: db; table: "publisher" }
    importer.load(file)       // fileColumns, mapping, preview, rowCount
    importer.run()            // imported, or error ("Row 12: ...")
\endcode
 */
class CsvImport : public QObject {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(DatabaseSession *session READ session WRITE setSession NOTIFY sessionChanged)
    Q_PROPERTY(QString table READ table WRITE setTable NOTIFY tableChanged)
    /// Whether the file's first row names its columns (default true).
    Q_PROPERTY(bool headerRow READ headerRow WRITE setHeaderRow NOTIFY loaded)
    /// Whether an empty cell means NULL (default true) rather than ''.
    Q_PROPERTY(bool emptyIsNull READ emptyIsNull WRITE setEmptyIsNull NOTIFY emptyIsNullChanged)
    Q_PROPERTY(QString fileName READ fileName NOTIFY loaded)
    Q_PROPERTY(QString delimiterName READ delimiterName NOTIFY loaded)
    /// The file's columns (from its header, or "Column 1", ...).
    Q_PROPERTY(QStringList fileColumns READ fileColumns NOTIFY loaded)
    /// For each file column, the table column it goes into ("" = skipped).
    Q_PROPERTY(QStringList mapping READ mapping NOTIFY mappingChanged)
    /// The table's columns, to choose from.
    Q_PROPERTY(QStringList tableColumns READ tableColumns NOTIFY tableChanged)
    /// The first rows of the file, as lists of strings.
    Q_PROPERTY(QVariantList preview READ preview NOTIFY loaded)
    Q_PROPERTY(int rowCount READ rowCount NOTIFY loaded)
    Q_PROPERTY(int imported READ imported NOTIFY finished)
    Q_PROPERTY(QString error READ error NOTIFY errorChanged)

public:
    explicit CsvImport(QObject *parent = nullptr);

    DatabaseSession *session() const { return m_session; }
    void setSession(DatabaseSession *session);
    QString table() const { return m_table; }
    void setTable(const QString &table);
    bool headerRow() const { return m_headerRow; }
    void setHeaderRow(bool on);
    bool emptyIsNull() const { return m_emptyIsNull; }
    void setEmptyIsNull(bool on);
    QString fileName() const { return m_fileName; }
    QString delimiterName() const;
    QStringList fileColumns() const { return m_fileColumns; }
    QStringList mapping() const { return m_mapping; }
    QStringList tableColumns() const;
    QVariantList preview() const;
    int rowCount() const;
    int imported() const { return m_imported; }
    QString error() const { return m_error; }

    /// Read `fileOrUrl` and match its columns to the table's.
    Q_INVOKABLE bool load(const QVariant &fileOrUrl);
    /// Send file column `index` into table column `column` ("" to skip it).
    Q_INVOKABLE void setMapping(int index, const QString &column);
    /// Insert every row. False (with error()) if anything fails: then nothing is imported.
    Q_INVOKABLE bool run();

signals:
    void sessionChanged();
    void tableChanged();
    void loaded();
    void mappingChanged();
    void emptyIsNullChanged();
    void finished();
    void errorChanged();

private:
    void setError(const QString &e);
    void split();                 // header and data rows from m_records
    void autoMap();

    QPointer<DatabaseSession> m_session;
    QString m_table;
    bool m_headerRow = true;
    bool m_emptyIsNull = true;
    QString m_fileName;
    QChar m_delimiter = QLatin1Char(',');
    QVector<QStringList> m_records;          // every record in the file
    QStringList m_fileColumns;
    QStringList m_mapping;
    int m_imported = 0;
    QString m_error;
};

#endif // DATATRANSFER_H
