#pragma once
#include "pattern.h"
#include "aw2023.h"
#include <QObject>
#include <QDBusConnection>
#include <QDBusUnixFileDescriptor>
#include <QTimer>
#include <functional>

class Hardware : public QObject {
    Q_OBJECT
public:
    explicit Hardware(bool simulate, QObject *parent = nullptr);
    // Injection keeps lifecycle tests on a private bus without real I2C access.
    Hardware(const QDBusConnection &system, const QDBusConnection &session,
             std::function<bool(int, int)> writeRegister, QObject *parent = nullptr);
    ~Hardware();
    bool present() const { return m_present; }
    bool blocked() const { return m_blocked; }
    bool ready() const { return m_ready; }
    QString error() const { return m_error; }
    void request(bool wanted);
    bool writeFrame(const Inari::Levels &levels);
    bool startBreathing(const Inari::Registers &registers);
    void shutdown();
signals:
    void changed();
private:
    Hardware(bool simulate, const QDBusConnection &system, const QDBusConnection &session,
             std::function<bool(int, int)> writeRegister, QObject *parent);
    void refreshCover();
    void refreshPrivacy();
    void reconcile();
    void release();
    bool reg(int address, int value);
    void fail(const QString &message);
    QDBusConnection m_bus;
    QDBusConnection m_session;
    std::function<bool(int, int)> m_writeRegister;
    QDBusUnixFileDescriptor m_fd;
    bool m_simulate;
    bool m_present = false;
    bool m_blocked = true;
    bool m_wanted = false;
    bool m_pending = false;
    bool m_ready = false;
    bool m_autonomous = false;
    bool m_shuttingDown = false;
    quint64 m_coverQuery = 0;
    quint64 m_privacyQuery = 0;
    quint64 m_coverGeneration = 0;
    QString m_error;
    QString m_loanService;
    QTimer m_retry;
};
