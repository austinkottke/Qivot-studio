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

    QTextStream out(stdout), err(stderr);
    return QivotCli::run(app.arguments().mid(1), out, err);
}
