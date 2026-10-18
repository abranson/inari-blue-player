#pragma once
#include "pattern.h"
#include "hardware.h"
#include <QDBusContext>
#include <QDBusServiceWatcher>
#include <QElapsedTimer>
#include <QFileSystemWatcher>
#include <QJsonObject>
#include <QMap>
#include <QTimer>

class Player : public QObject, protected QDBusContext {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.sailfishos.InariBlue1")
public:
    Player(bool simulate, const QStringList &directories, const QString &config, QObject *parent = nullptr);
    ~Player();
    void shutdown();
public slots:
    QString GetState() const;
    bool SetEnabled(bool enabled);
    bool SetPattern(const QString &category, const QString &id);
    bool Preview(const QString &id, uint durationMs);
    void StopPreview();
    bool StartEvent(uint cookie, const QString &category);
    void StopEvent(uint cookie);
    void ReloadPatterns();
signals:
    void StateChanged(const QString &json);
    void FrameChanged(int channel0, int channel1, int channel2);
private:
    struct Event { QString owner; QString category; quint64 sequence; };
    QString caller() const;
    QString eventKey(const QString &owner, uint cookie) const;
    bool save();
    void loadConfig();
    void select();
    void tick();
    void notify();
    void ownerGone(const QString &owner);
    void watchOwner(const QString &owner);
    void pruneOwners();
    static QStringList categories();
    Hardware m_hardware;
    QStringList m_directories;
    QString m_configPath;
    QString m_error;
    QJsonObject m_config;
    QMap<QString, Inari::Pattern> m_patterns;
    QMap<QString, Event> m_events;
    QDBusServiceWatcher m_owners;
    QFileSystemWatcher m_files;
    QTimer m_reload;
    QTimer m_tick;
    QTimer m_previewTimer;
    QElapsedTimer m_elapsed;
    QString m_previewId;
    QString m_previewOwner;
    QString m_selected;
    QString m_selectionKey;
    Inari::Levels m_last{{-1, -1, -1}};
    quint64 m_sequence = 0;
    bool m_enabled = false;
    bool m_configValid = true;
    bool m_outputReady = false;
};
