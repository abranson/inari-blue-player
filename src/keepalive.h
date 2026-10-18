#ifndef INARI_BLUE_KEEPALIVE_H
#define INARI_BLUE_KEEPALIVE_H

#include <QtCore/QObject>
#include <QtDBus/QDBusConnection>

class QDBusPendingCallWatcher;
class QTimer;

class KeepAlive : public QObject
{
    Q_OBJECT
public:
    explicit KeepAlive(bool simulate, QObject *parent = 0);
    KeepAlive(bool simulate, const QDBusConnection &connection, QObject *parent = 0);
    ~KeepAlive();

    bool request(bool enabled);
    bool ready() const { return m_ready; }
    bool error() const { return m_error; }
    void shutdown();

Q_SIGNALS:
    void changed();

private Q_SLOTS:
    void replyFinished(QDBusPendingCallWatcher *watcher);
    void renew();
    void renewalTimedOut();
    void retry();
    void serviceOwnerChanged(const QString &service, const QString &oldOwner,
                             const QString &newOwner);

private:
    enum Operation { Start, Renew, Stop, Period };

    void setState(bool ready, bool error);
    void beginStart();
    void sendStart(Operation operation, quint64 generation);
    void sendStop(bool waitForReply = true);
    void failClosed();
    void continueAfterStop();
    void armRenewal(int seconds);

    bool m_simulate;
    QDBusConnection m_connection;
    QTimer *m_timer;
    QTimer *m_retryTimer;
    QTimer *m_watchdogTimer;
    QString m_id;
    quint64 m_generation;
    quint64 m_stopGeneration;
    int m_periodSeconds;
    bool m_requested;
    bool m_ready;
    bool m_error;
    bool m_active;
    bool m_startPending;
    bool m_stopInFlight;
    bool m_renewPending;
    bool m_shuttingDown;
};

#endif
