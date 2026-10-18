#pragma once
#include "pattern.h"
#include <QObject>
#include <QDBusConnection>
#include <QDBusUnixFileDescriptor>
#include <QTimer>

class Hardware : public QObject {
    Q_OBJECT
public:
    explicit Hardware(bool simulate, QObject *parent = nullptr);
    ~Hardware();
    bool present() const { return m_present; }
    bool blocked() const { return m_blocked; }
    bool ready() const { return m_ready; }
    QString error() const { return m_error; }
    void request(bool wanted);
    bool writeFrame(const Inari::Levels &levels);
    void shutdown();
signals:
    void changed();
private:
    void refreshCover();
    void refreshPrivacy();
    void reconcile();
    void release();
    bool reg(int address, int value);
    void fail(const QString &message);
    QDBusConnection m_bus;
    QDBusUnixFileDescriptor m_fd;
    bool m_simulate;
    bool m_present = false;
    bool m_blocked = true;
    bool m_wanted = false;
    bool m_pending = false;
    bool m_ready = false;
    bool m_initialized = false;
    bool m_shuttingDown = false;
    quint64 m_coverQuery = 0;
    quint64 m_privacyQuery = 0;
    QString m_error;
    QTimer m_retry;
};
