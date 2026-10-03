#include "qivotcli.h"
#include <QCoreApplication>
#include <QTextStream>
#include <cstdio>

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    // The same names as the app, so sample:<id> opens the samples Studio made.
    app.setApplicationName(QStringLiteral("Qivot Studio"));
    app.setOrganizationName(QStringLiteral("Qivot"));
    app.setApplicationVersion(QStringLiteral(PROJECT_VERSION_STRING));

    QivotCli::useTerminal();
    QTextStream out(stdout), err(stderr);
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
    out.setCodec("UTF-8");                    // Qt 6 writes UTF-8 already
    err.setCodec("UTF-8");
#endif
    return QivotCli::run(app.arguments().mid(1), out, err);
}
