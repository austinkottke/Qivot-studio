#ifndef QIVOTCLI_H
#define QIVOTCLI_H

#include <QStringList>

class QTextStream;

/// qivot-cli: Qivot Studio without the window.
/**
  Every command takes databases the way Studio opens them: an SQLite or DuckDB
  file, `sample:<id>`, a server URL (`postgres://user@host/db`, `mysql://…`,
  `sqlserver://…`; the password may come from QIVOT_PASSWORD instead of the
  URL), or `migrations:<dir>`: an SQLite database built by running a folder of
  migrations.
 */
namespace QivotCli {

/// The exit codes.
enum Exit {
    Ok = 0,
    Differs = 1,    ///< with --exit-code: differences found, or migrations pending
    Failed = 2,     ///< bad arguments, or something went wrong
};

/// Colour results (`out`) and messages (`err`). Both are off unless set, so
/// output that goes to a file or another program stays plain.
void setColors(bool out, bool err);

/// Colour stdout and stderr when each is a terminal, unless NO_COLOR is set
/// or TERM is "dumb" (FORCE_COLOR colours them regardless). On Windows this
/// also switches the console to UTF-8 and ANSI colours.
void useTerminal();

/// Run qivot-cli with `args` (not including the program name). Results go to
/// `out`, messages to `err`. Returns an Exit code.
int run(const QStringList &args, QTextStream &out, QTextStream &err);

} // namespace QivotCli

#endif // QIVOTCLI_H
