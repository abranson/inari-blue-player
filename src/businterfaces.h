#pragma once
#include <QDBusAbstractInterface>
#include <QDBusObjectPath>
#include <QVariantMap>

using InterfaceMap = QMap<QString, QVariantMap>;
Q_DECLARE_METATYPE(InterfaceMap)

// Like qdbusxml2cpp proxies, these declarations let QtDBus relay signals to
// typed QObject connections without string-based signal/slot connections.
class TohManager : public QDBusAbstractInterface {
    Q_OBJECT
public:
    TohManager(const QDBusConnection &bus, QObject *parent)
        : QDBusAbstractInterface("org.sailfishos.tohd1", "/org/sailfishos/tohd1",
                                 "org.freedesktop.DBus.ObjectManager", bus, parent) {}
signals:
    void InterfacesAdded(const QDBusObjectPath &path, const InterfaceMap &interfaces);
    void InterfacesRemoved(const QDBusObjectPath &path, const QStringList &interfaces);
};
class Privacy : public QDBusAbstractInterface {
    Q_OBJECT
public:
    Privacy(const QDBusConnection &bus, QObject *parent)
        : QDBusAbstractInterface("org.sailfishos.privacyswitch", "/privacyswitch",
                                 "org.sailfishos.privacyswitch", bus, parent) {}
signals:
    void privacyModeActiveChanged(bool active);
};
