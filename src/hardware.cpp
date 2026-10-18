#include "hardware.h"
#include "businterfaces.h"
#include <QDBusMessage>
#include <QDBusMetaType>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDBusServiceWatcher>
#include <QDebug>
#include <linux/i2c-dev.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <cerrno>
#include <cstring>

static QDBusMessage tohCall(const QString &method, const QString &service = "org.sailfishos.tohd1")
{
    return QDBusMessage::createMethodCall(service, "/org/sailfishos/tohd1/toh",
                                         "org.sailfishos.tohd1.Toh", method);
}
Hardware::Hardware(bool simulate, QObject *parent)
    : Hardware(simulate, QDBusConnection::systemBus(), QDBusConnection::sessionBus(), {}, parent) {}
Hardware::Hardware(const QDBusConnection &system, const QDBusConnection &session,
                   std::function<bool(int, int)> writeRegister, QObject *parent)
    : Hardware(false, system, session, writeRegister, parent) {}
Hardware::Hardware(bool simulate, const QDBusConnection &system, const QDBusConnection &session,
                   std::function<bool(int, int)> writeRegister, QObject *parent)
    : QObject(parent), m_bus(system), m_session(session), m_writeRegister(writeRegister), m_simulate(simulate)
{
    m_retry.setSingleShot(true);
    m_retry.setInterval(5000);
    connect(&m_retry, &QTimer::timeout, this, [this] { refreshCover(); refreshPrivacy(); reconcile(); });
    if (simulate) { m_present = true; m_blocked = false; return; }
    qDBusRegisterMetaType<InterfaceMap>();
    auto manager = new TohManager(m_bus, this);
    connect(manager, &TohManager::InterfacesAdded, this, [this] { refreshCover(); });
    connect(manager, &TohManager::InterfacesRemoved, this, [this](const QDBusObjectPath &path) {
        if (path.path() != "/org/sailfishos/tohd1/toh") return;
        ++m_coverGeneration; ++m_coverQuery;
        m_present = false; reconcile(); emit changed(); refreshCover();
    });
    auto coverOwner = new QDBusServiceWatcher("org.sailfishos.tohd1", m_bus,
                                             QDBusServiceWatcher::WatchForOwnerChange, this);
    connect(coverOwner, &QDBusServiceWatcher::serviceOwnerChanged, this, [this] {
        ++m_coverGeneration; ++m_coverQuery;
        m_present = false; reconcile(); emit changed(); refreshCover();
    });
    auto privacy = new Privacy(m_session, this);
    connect(privacy, &Privacy::privacyModeActiveChanged, this, [this](bool active) {
        ++m_privacyQuery; m_blocked = active; reconcile(); emit changed();
    });
    auto privacyOwner = new QDBusServiceWatcher("org.sailfishos.privacyswitch", m_session,
                                               QDBusServiceWatcher::WatchForOwnerChange, this);
    connect(privacyOwner, &QDBusServiceWatcher::serviceOwnerChanged, this, [this] {
        m_blocked = true; reconcile(); emit changed(); refreshPrivacy();
    });
    refreshCover();
    refreshPrivacy();
}
Hardware::~Hardware() { shutdown(); }
void Hardware::refreshCover()
{
    if (m_simulate || m_shuttingDown) return;
    const auto sequence = ++m_coverQuery;
    auto msg = QDBusMessage::createMethodCall("org.sailfishos.tohd1", "/org/sailfishos/tohd1/toh",
                                              "org.freedesktop.DBus.Properties", "GetAll");
    msg << QString("org.sailfishos.tohd1.Toh");
    auto watcher = new QDBusPendingCallWatcher(m_bus.asyncCall(msg, 2000), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, sequence, watcher] {
        QDBusPendingReply<QVariantMap> reply = *watcher;
        watcher->deleteLater();
        if (sequence != m_coverQuery || m_shuttingDown) return;
        const auto values = reply.value();
        m_present = !reply.isError() && values.value("VendorId").toUInt() == 1
                    && values.value("ProductId").toUInt() == 4;
        reconcile();
        emit changed();
    });
}
void Hardware::refreshPrivacy()
{
    if (m_simulate || m_shuttingDown) return;
    const auto sequence = ++m_privacyQuery;
    auto msg = QDBusMessage::createMethodCall("org.sailfishos.privacyswitch", "/privacyswitch",
                                              "org.sailfishos.privacyswitch", "privacyModeActive");
    auto watcher = new QDBusPendingCallWatcher(m_session.asyncCall(msg, 2000), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, sequence, watcher] {
        QDBusPendingReply<bool> reply = *watcher;
        watcher->deleteLater();
        if (sequence != m_privacyQuery || m_shuttingDown) return;
        m_blocked = reply.isError() || reply.value();
        if (reply.isError()) m_retry.start();
        reconcile();
        emit changed();
    });
}
void Hardware::request(bool wanted)
{
    m_wanted = wanted;
    reconcile();
}
void Hardware::reconcile()
{
    const bool allowed = m_wanted && m_present && !m_blocked && !m_shuttingDown;
    if (m_simulate) {
        if (m_ready != allowed) { m_ready = allowed; emit changed(); }
        return;
    }
    if (!allowed) { if (m_fd.isValid() && !m_pending) release(); return; }
    if (m_fd.isValid() || m_pending || m_retry.isActive()) return;
    m_pending = true;
    const auto generation = m_coverGeneration;
    auto msg = tohCall("BorrowI2cDevAccessWithPower");
    msg << false; // Returning the loan powers the cover down.
    auto watcher = new QDBusPendingCallWatcher(m_bus.asyncCall(msg, 2000), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, watcher, generation] {
        QDBusPendingReply<QDBusUnixFileDescriptor> reply = *watcher;
        watcher->deleteLater();
        m_pending = false;
        if (reply.isError()) { fail(reply.error().message()); return; }
        m_loanService = reply.reply().service();
        m_fd = reply.value();
        if (!m_fd.isValid()) { fail("Symbiosis returned an invalid descriptor"); return; }
        if (generation != m_coverGeneration || !m_wanted || !m_present || m_blocked || m_shuttingDown) { release(); return; }
        if (!m_writeRegister && ::ioctl(m_fd.fileDescriptor(), I2C_SLAVE, 0x45) < 0) { fail(QString::fromLocal8Bit(strerror(errno))); return; }
        m_pending = true;
        QTimer::singleShot(500, this, [this, generation] {
            m_pending = false;
            if (generation != m_coverGeneration || !m_wanted || !m_present || m_blocked || m_shuttingDown) { release(); return; }
            // Manual PWM mode; no pattern-specific behavior is encoded here.
            if (!reg(0x30, 0) || !reg(0x01, 1) || !reg(0x04, 1)
                || !reg(0x31, 0x0f) || !reg(0x32, 0x0f) || !reg(0x33, 0x0f)
                || !reg(0x34, 0) || !reg(0x35, 0) || !reg(0x36, 0) || !reg(0x30, 7)) {
                fail("Failed to initialise AW2023"); return;
            }
            m_autonomous = false; m_ready = true; m_error.clear(); emit changed();
        });
    });
}
bool Hardware::reg(int address, int value)
{
    if (!m_fd.isValid()) return false;
    if (m_writeRegister) return m_writeRegister(address, value);
    const unsigned char bytes[] = {static_cast<unsigned char>(address), static_cast<unsigned char>(value)};
    ssize_t written;
    do { written = ::write(m_fd.fileDescriptor(), bytes, 2); } while (written < 0 && errno == EINTR);
    return written == 2;
}
bool Hardware::writeFrame(const Inari::Levels &levels)
{
    if (!m_ready) return false;
    if (m_simulate) return true;
    if (m_autonomous) {
        if (!reg(0x30, 0) || !reg(0x31, 0x0f) || !reg(0x32, 0x0f) || !reg(0x33, 0x0f)) {
            fail("Failed to leave automatic breathing mode"); return false;
        }
    }
    for (int i = 0; i < 3; ++i) {
        if (!reg(0x34 + i, levels[i])) { fail("Failed to write LED brightness"); return false; }
    }
    if (m_autonomous && !reg(0x30, 7)) { fail("Failed to enable manual output"); return false; }
    m_autonomous = false;
    return true;
}
bool Hardware::startBreathing(const Inari::Registers &registers)
{
    if (!m_ready) return false;
    if (!m_simulate) for (const auto &entry : registers) {
        if (!reg(entry.first, entry.second)) { fail("Failed to program automatic breathing"); return false; }
    }
    m_autonomous = true;
    return true;
}
void Hardware::fail(const QString &message)
{
    if (message != m_error) qWarning() << "Inari Blue:" << message;
    m_error = message;
    m_retry.start();
    release();
    emit changed();
}
void Hardware::release()
{
    m_ready = false;
    if (!m_fd.isValid()) return;
    // Even partial initialisation may have enabled the chip.
    reg(0x01, 0);
    m_autonomous = false;
    m_fd = QDBusUnixFileDescriptor();
    m_pending = true;
    auto msg = tohCall("ReturnI2cDevAccess", m_loanService);
    m_loanService.clear();
    auto watcher = new QDBusPendingCallWatcher(m_bus.asyncCall(msg, 2000), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, watcher] {
        QDBusPendingReply<> reply = *watcher;
        watcher->deleteLater(); m_pending = false;
        if (reply.isError()) { m_error = reply.error().message(); m_retry.start(); }
        reconcile(); emit changed();
    });
}
void Hardware::shutdown()
{
    if (m_shuttingDown) return;
    m_shuttingDown = true;
    m_retry.stop(); m_ready = false;
    if (m_fd.isValid()) {
        reg(0x01, 0);
        m_fd = QDBusUnixFileDescriptor();
        // A bounded final call ensures the loan is returned before this bus closes.
        auto msg = tohCall("ReturnI2cDevAccess", m_loanService);
        m_bus.call(msg, QDBus::Block, 2000);
    }
}
