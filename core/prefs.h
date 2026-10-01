#ifndef PREFS_H
#define PREFS_H

#include <QObject>
#include <QQmlEngine>
#include <QSettings>

/// Small persistent preferences for the UI (remembered connection fields,
/// console history), as strings — JSON where a value is structured.
/// In C++ rather than QML's Settings type, whose import differs between
/// Qt 5 (Qt.labs.settings) and Qt 6 (QtCore).
class Prefs : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

public:
    using QObject::QObject;

    Q_INVOKABLE QString value(const QString &key, const QString &fallback = QString()) const
    {
        return QSettings().value(key, fallback).toString();
    }
    Q_INVOKABLE void setValue(const QString &key, const QString &value)
    {
        QSettings().setValue(key, value);
    }
};

#endif // PREFS_H
