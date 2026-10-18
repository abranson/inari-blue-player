#include "keepalive.h"

#include <QtCore/QTimer>
#include <QtDBus/QDBusServiceWatcher>
#include <QtDBus/QDBusPendingCall>
#include <QtDBus/QDBusPendingCallWatcher>
#include <QtDBus/QDBusPendingReply>
#include <QtDBus/QDBusReply>

namespace {
const char serviceName[] = "com.nokia.mce";
const char objectPath[] = "/com/nokia/mce/request";
const char interfaceName[] = "com.nokia.mce.request";
const char defaultId[] = "InariBluePlayer";
}

KeepAlive::KeepAlive(bool simulate, QObject *parent)
    : KeepAlive(simulate, QDBusConnection::systemBus(), parent)
{
}

KeepAlive::KeepAlive(bool simulate, const QDBusConnection &connection, QObject *parent)
    : QObject(parent),
      m_simulate(simulate),
      m_connection(connection),
      m_timer(new QTimer(this)),
      m_retryTimer(new QTimer(this)),
      m_watchdogTimer(new QTimer(this)),
      m_id(QString::fromLatin1(defaultId)),
      m_generation(0),
      m_stopGeneration(0),
      m_periodSeconds(60),
      m_requested(false),
      m_ready(false),
      m_error(false),
      m_active(false),
      m_startPending(false),
      m_stopInFlight(false),
      m_renewPending(false),
      m_shuttingDown(false)
{
    m_timer->setSingleShot(true);
    m_retryTimer->setSingleShot(true);
    m_watchdogTimer->setSingleShot(true);
    m_watchdogTimer->setTimerType(Qt::PreciseTimer);
    connect(m_timer, &QTimer::timeout, this, &KeepAlive::renew);
    connect(m_retryTimer, &QTimer::timeout, this, &KeepAlive::retry);
    connect(m_watchdogTimer, &QTimer::timeout, this, &KeepAlive::renewalTimedOut);

    if (!m_simulate) {
        auto watcher = new QDBusServiceWatcher(QString::fromLatin1(serviceName), m_connection,
                                               QDBusServiceWatcher::WatchForOwnerChange, this);
        connect(watcher, &QDBusServiceWatcher::serviceOwnerChanged,
                this, &KeepAlive::serviceOwnerChanged);
    }
}

KeepAlive::~KeepAlive()
{
    shutdown();
}

void KeepAlive::setState(bool ready, bool error)
{
    if (m_ready == ready && m_error == error)
        return;
    m_ready = ready;
    m_error = error;
    emit changed();
}

bool KeepAlive::request(bool enabled)
{
    if (m_shuttingDown)
        return false;
    if (enabled == m_requested)
        return true;
    ++m_generation;
    m_requested = enabled;

    if (m_simulate) {
        m_active = enabled;
        setState(enabled, false);
        return true;
    }

    if (!enabled) {
        const bool wasPending = m_startPending;
        m_timer->stop();
        m_retryTimer->stop();
        m_watchdogTimer->stop();
        m_renewPending = false;
        m_startPending = false;
        setState(false, false);
        if (m_active || m_stopInFlight || wasPending)
            sendStop();
        else
            continueAfterStop();
        m_active = false;
        return true;
    }

    setState(false, false);
    m_retryTimer->stop();
    if (m_stopInFlight)
        return true;
    if (!m_active && !m_startPending)
        beginStart();
    return true;
}

void KeepAlive::beginStart()
{
    if (!m_requested || m_stopInFlight || m_startPending)
        return;
    m_startPending = true;
    sendStart(Start, m_generation);
}

void KeepAlive::sendStart(Operation operation, quint64 generation)
{
    QDBusMessage message = QDBusMessage::createMethodCall(
        QString::fromLatin1(serviceName), QString::fromLatin1(objectPath),
        QString::fromLatin1(interfaceName), QString::fromLatin1("req_cpu_keepalive_start"));
    message << m_id;
    QDBusPendingCall call = m_connection.asyncCall(message, 1000);
    QDBusPendingCallWatcher *watcher = new QDBusPendingCallWatcher(call, this);
    watcher->setProperty("operation", int(operation));
    watcher->setProperty("generation", QVariant::fromValue<qulonglong>(generation));
    connect(watcher, &QDBusPendingCallWatcher::finished,
            this, &KeepAlive::replyFinished);
}

void KeepAlive::sendStop(bool waitForReply)
{
    if (m_stopInFlight)
        return;
    m_stopGeneration = m_generation;
    QDBusMessage message = QDBusMessage::createMethodCall(
        QString::fromLatin1(serviceName), QString::fromLatin1(objectPath),
        QString::fromLatin1(interfaceName), QString::fromLatin1("req_cpu_keepalive_stop"));
    message << m_id;
    if (!waitForReply) {
        m_connection.asyncCall(message);
        return;
    }
    m_stopInFlight = true;
    QDBusPendingCall call = m_connection.asyncCall(message, 1000);
    QDBusPendingCallWatcher *watcher = new QDBusPendingCallWatcher(call, this);
    watcher->setProperty("operation", int(Stop));
    watcher->setProperty("generation", QVariant::fromValue<qulonglong>(m_generation));
    connect(watcher, &QDBusPendingCallWatcher::finished,
            this, &KeepAlive::replyFinished);
}

void KeepAlive::armRenewal(int seconds)
{
    m_periodSeconds = qMax(2, seconds);
    m_timer->start(qMax(1000, (m_periodSeconds * 1000) / 2));
}

void KeepAlive::renew()
{
    if (m_shuttingDown || !m_requested || !m_active || m_renewPending)
        return;
    m_renewPending = true;
    // Renewal starts halfway through the lease. Allow only another quarter
    // before failing closed, leaving time to stop output before expiry.
    m_watchdogTimer->start((m_periodSeconds * 1000) / 4);
    sendStart(Renew, m_generation);
}

void KeepAlive::renewalTimedOut()
{
    if (!m_renewPending)
        return;
    m_renewPending = false;
    failClosed();
}

void KeepAlive::replyFinished(QDBusPendingCallWatcher *watcher)
{
    const Operation operation = Operation(watcher->property("operation").toInt());
    const quint64 generation = watcher->property("generation").toULongLong();
    const bool valid = !watcher->isError();
    QDBusPendingReply<int> period;
    if (operation == Period && valid)
        period = *watcher;
    watcher->deleteLater();

    if (operation == Stop) {
        if (generation != m_stopGeneration) return;
        m_stopInFlight = false;
        m_active = false;
        m_startPending = false;
        if (m_requested && !m_shuttingDown && !m_retryTimer->isActive())
            beginStart();
        return;
    }

    if (operation == Period) {
        if (generation != m_generation || !m_requested || !m_active)
            return;
        if (!valid) {
            failClosed();
            return;
        }
        const int seconds = period.value();
        if (period.isError() || seconds < 2 || seconds > 3600) {
            failClosed();
            return;
        }
        armRenewal(seconds);
        setState(true, false);
        return;
    }

    if (generation != m_generation || !m_requested) {
        if (valid && m_requested && m_stopGeneration < generation)
            sendStop();
        return;
    }
    if (operation == Renew) {
        m_renewPending = false;
        m_watchdogTimer->stop();
    }
    if (!valid) {
        failClosed();
        return;
    }

    m_startPending = false;
    m_active = true;
    if (operation == Start) {
        QDBusMessage message = QDBusMessage::createMethodCall(
            QString::fromLatin1(serviceName), QString::fromLatin1(objectPath),
            QString::fromLatin1(interfaceName), QString::fromLatin1("req_cpu_keepalive_period"));
        QDBusPendingCall call = m_connection.asyncCall(message, 1000);
        QDBusPendingCallWatcher *period = new QDBusPendingCallWatcher(call, this);
        period->setProperty("operation", int(Period));
        period->setProperty("generation", QVariant::fromValue<qulonglong>(generation));
        connect(period, &QDBusPendingCallWatcher::finished,
                this, &KeepAlive::replyFinished);
    } else {
        armRenewal(m_periodSeconds);
    }
}

void KeepAlive::retry()
{
    if (m_requested && !m_active && !m_startPending && !m_stopInFlight) {
        setState(false, false);
        beginStart();
    }
}

void KeepAlive::failClosed()
{
    const bool wasPending = m_startPending;
    const bool wasActive = m_active;
    ++m_generation;
    m_startPending = false;
    m_renewPending = false;
    m_timer->stop();
    m_watchdogTimer->stop();
    m_active = false;
    if (wasActive || wasPending)
        sendStop();
    if (m_requested && !m_shuttingDown)
        m_retryTimer->start(5000);
    setState(false, true);
}

void KeepAlive::continueAfterStop()
{
    m_active = false;
    m_startPending = false;
}

void KeepAlive::serviceOwnerChanged(const QString &service, const QString &oldOwner,
                                    const QString &newOwner)
{
    Q_UNUSED(oldOwner)
    if (service != QString::fromLatin1(serviceName))
        return;
    if (newOwner.isEmpty() || !oldOwner.isEmpty()) {
        ++m_generation;
        m_stopGeneration = m_generation;
        m_stopInFlight = false;
        m_startPending = false;
        m_renewPending = false;
        m_timer->stop();
        m_watchdogTimer->stop();
        m_active = false;
        setState(false, true);
        if (m_requested && !m_shuttingDown) {
            if (newOwner.isEmpty())
                m_retryTimer->start(5000);
            else {
                m_retryTimer->stop();
                beginStart();
            }
        }
    } else if (m_requested && !m_shuttingDown) {
        m_retryTimer->stop();
        beginStart();
    }
}

void KeepAlive::shutdown()
{
    if (m_shuttingDown)
        return;
    m_shuttingDown = true;
    ++m_generation;
    m_requested = false;
    m_timer->stop();
    m_retryTimer->stop();
    m_watchdogTimer->stop();
    if (m_simulate) {
        m_active = false;
        setState(false, false);
        return;
    }
    if (m_active || m_startPending)
        sendStop(false);
    m_active = false;
    m_startPending = false;
    setState(false, false);
}
