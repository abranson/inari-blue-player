#include "player.h"
#include <QDir>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>
#include <QDebug>

Player::Player(bool simulate, const QStringList &directories, const QString &config, QObject *parent)
    : QObject(parent), m_hardware(simulate, this), m_keepalive(simulate, this), m_directories(directories), m_configPath(config),
      m_owners(QString(), QDBusConnection::sessionBus(), QDBusServiceWatcher::WatchForUnregistration, this)
{
    m_tick.setInterval(10);
    m_tick.setTimerType(Qt::PreciseTimer);
    m_previewTimer.setSingleShot(true);
    m_reload.setSingleShot(true);
    m_reload.setInterval(100);
    connect(&m_tick, &QTimer::timeout, this, &Player::tick);
    connect(&m_previewTimer, &QTimer::timeout, this, [this] {
        m_previewId.clear(); m_previewOwner.clear(); pruneOwners(); select(); notify();
    });
    connect(&m_hardware, &Hardware::changed, this, [this] { select(); notify(); });
    connect(&m_keepalive, &KeepAlive::changed, this, [this] { select(); notify(); });
    connect(&m_owners, &QDBusServiceWatcher::serviceUnregistered, this, &Player::ownerGone);
    connect(&m_files, &QFileSystemWatcher::directoryChanged, this, [this] { m_reload.start(); });
    connect(&m_files, &QFileSystemWatcher::fileChanged, this, [this] { m_reload.start(); });
    connect(&m_reload, &QTimer::timeout, this, &Player::ReloadPatterns);
    loadConfig();
    ReloadPatterns();
}
Player::~Player() { shutdown(); }
void Player::shutdown() { m_shuttingDown = true; m_tick.stop(); m_previewTimer.stop(); m_hardware.shutdown(); m_keepalive.shutdown(); }
QStringList Player::categories()
{
    return {"idle", "ringtone", "internet-call", "message", "chat", "email", "clock", "calendar", "other"};
}
QString Player::caller() const { return calledFromDBus() ? message().service() : QString("local"); }
QString Player::eventKey(const QString &owner, uint cookie) const { return owner + "/" + QString::number(cookie); }
void Player::loadConfig()
{
    QFile file(m_configPath);
    if (!file.exists()) return;
    QJsonParseError error;
    if (!file.open(QIODevice::ReadOnly) || file.size() > 65536) {
        m_error = "Cannot read player settings"; m_configValid = false; return;
    }
    const auto doc = QJsonDocument::fromJson(file.readAll(), &error);
    if (error.error != QJsonParseError::NoError || !doc.isObject()
        || !doc.object().value("enabled").isBool() || !doc.object().value("assignments").isObject()) {
        m_error = "Invalid player settings; existing file preserved"; m_configValid = false; return;
    }
    m_config = doc.object();
    m_enabled = m_config.value("enabled").toBool();
}
bool Player::save()
{
    if (!m_configValid) return false;
    QDir().mkpath(QFileInfo(m_configPath).absolutePath());
    QSaveFile file(m_configPath);
    const auto bytes = QJsonDocument(m_config).toJson();
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit()) {
        m_error = "Cannot save player settings"; notify(); return false;
    }
    m_error.clear();
    return true;
}
QString Player::GetState() const
{
    QJsonObject state;
    state.insert("present", m_hardware.present());
    state.insert("enabled", m_enabled);
    state.insert("blocked", m_hardware.blocked());
    state.insert("ready", m_hardware.ready());
    state.insert("error", m_error.isEmpty() ? m_hardware.error() : m_error);
    if (m_keepalive.error()) state.insert("error", "Cannot keep software playback awake");
    state.insert("autonomous", m_autonomous);
    state.insert("playing", m_selected);
    state.insert("preview", !m_previewId.isEmpty());
    QJsonArray patterns;
    for (const auto &p : m_patterns) patterns.append(QJsonObject{{"id", p.id}, {"name", p.name}});
    state.insert("patterns", patterns);
    QJsonObject assignments;
    const auto saved = m_config.value("assignments").toObject();
    for (const auto &category : categories()) assignments.insert(category, saved.value(category).toString("off"));
    state.insert("assignments", assignments);
    return QString::fromUtf8(QJsonDocument(state).toJson(QJsonDocument::Compact));
}
void Player::notify() { emit StateChanged(GetState()); }
bool Player::SetEnabled(bool enabled)
{
    if (!m_configValid) return false;
    const auto previous = m_config;
    m_config.insert("enabled", enabled);
    if (!m_config.contains("assignments")) m_config.insert("assignments", QJsonObject());
    if (!save()) { m_config = previous; return false; }
    m_enabled = enabled;
    if (!enabled) { m_previewTimer.stop(); m_previewId.clear(); m_previewOwner.clear(); pruneOwners(); }
    select(); notify(); return true;
}
bool Player::SetPattern(const QString &category, const QString &id)
{
    if (!m_configValid || !categories().contains(category) || !m_patterns.contains(id)) return false;
    const auto previous = m_config;
    const auto saved = m_config.value("assignments").toObject();
    // Build a fresh child object. Qt 5.6's binary JSON compaction can crash
    // when a modified shared child is inserted back into its parent.
    QJsonObject assignments;
    for (auto it = saved.constBegin(); it != saved.constEnd(); ++it)
        if (it.key() != category) assignments.insert(it.key(), it.value());
    assignments.insert(category, id);
    m_config.insert("assignments", assignments);
    m_config.insert("enabled", m_enabled);
    if (!save()) { m_config = previous; return false; }
    select(); notify(); return true;
}
bool Player::Preview(const QString &id, uint durationMs)
{
    if (!m_enabled || !m_hardware.present() || m_hardware.blocked() || !m_patterns.contains(id)
        || durationMs < 100 || durationMs > 10000) return false;
    m_previewOwner = caller();
    watchOwner(m_previewOwner);
    m_previewId = id;
    m_previewTimer.start(int(durationMs));
    m_selectionKey.clear();
    pruneOwners(); select(); notify(); return true;
}
void Player::StopPreview()
{
    if (m_previewOwner != caller()) return;
    m_previewTimer.stop(); m_previewId.clear(); m_previewOwner.clear();
    pruneOwners(); select(); notify();
}
void Player::watchOwner(const QString &owner)
{
    if (owner == "local" || m_owners.watchedServices().contains(owner)) return;
    m_owners.addWatchedService(owner);
    // The sender may have exited before its first request was dispatched.
    // Subscribe first, then query so that this race cannot leave an orphan.
    auto msg = QDBusMessage::createMethodCall("org.freedesktop.DBus", "/org/freedesktop/DBus",
                                              "org.freedesktop.DBus", "NameHasOwner");
    msg << owner;
    auto watcher = new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(msg), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, watcher, owner] {
        QDBusPendingReply<bool> reply = *watcher;
        watcher->deleteLater();
        if (reply.isError() || !reply.value()) ownerGone(owner);
    });
}
void Player::pruneOwners()
{
    for (const auto &owner : m_owners.watchedServices()) {
        bool keep = owner == m_previewOwner;
        for (const auto &event : m_events) keep = keep || event.owner == owner;
        if (!keep) m_owners.removeWatchedService(owner);
    }
}
bool Player::StartEvent(uint cookie, const QString &category)
{
    if (!categories().contains(category) || category == "idle") return false;
    const auto owner = caller();
    const auto key = eventKey(owner, cookie);
    if (!m_events.contains(key) && m_events.size() >= 128) return false;
    m_events.insert(key, Event{owner, category, ++m_sequence});
    watchOwner(owner);
    select(); notify();
    return true;
}
void Player::StopEvent(uint cookie)
{
    if (m_events.remove(eventKey(caller(), cookie))) { pruneOwners(); select(); notify(); }
}
void Player::ownerGone(const QString &owner)
{
    for (auto it = m_events.begin(); it != m_events.end();) {
        if (it.value().owner == owner) it = m_events.erase(it); else ++it;
    }
    if (m_previewOwner == owner) { m_previewTimer.stop(); m_previewId.clear(); m_previewOwner.clear(); }
    pruneOwners(); select(); notify();
}
void Player::ReloadPatterns()
{
    if (!m_files.files().isEmpty()) m_files.removePaths(m_files.files());
    if (!m_files.directories().isEmpty()) m_files.removePaths(m_files.directories());
    QMap<QString, Inari::Pattern> loaded;
    // Directories are ordered system first, user last. Invalid overrides never
    // remove a valid bundled pattern with the same ID.
    for (const auto &directory : m_directories) {
        QDir dir(directory);
        if (!dir.exists()) {
            // Watch nearest existing ancestor so a newly created user pattern
            // directory is noticed without restarting the service.
            QString parent = QFileInfo(directory).absolutePath();
            while (!QDir(parent).exists() && parent != "/") parent = QFileInfo(parent).absolutePath();
            m_files.addPath(parent);
            continue;
        }
        m_files.addPath(directory);
        const auto files = dir.entryInfoList({"*.json"}, QDir::Files, QDir::Name);
        int count = 0;
        for (const auto &info : files) {
            if (++count > 256) break;
            QFile file(info.absoluteFilePath());
            Inari::Pattern pattern;
            QString error;
            m_files.addPath(info.absoluteFilePath());
            if (info.size() > 65536 || !file.open(QIODevice::ReadOnly)
                || !Inari::Pattern::parse(file.read(65537), &pattern, &error)
                || pattern.id != info.completeBaseName()) {
                qWarning() << "Ignoring invalid pattern" << info.fileName() << error;
                continue;
            }
            loaded.insert(pattern.id, pattern);
        }
    }
    m_patterns = loaded;
    // Restart the selected timeline after a file replacement, including same-ID edits.
    m_selectionKey.clear();
    select(); notify();
}
void Player::select()
{
    // request() may synchronously emit changed in simulation or on release.
    if (m_selecting || m_shuttingDown) return;
    m_selecting = true;
    QString selected;
    QString key;
    if (m_enabled && m_hardware.present() && !m_hardware.blocked()) {
        const auto assignments = m_config.value("assignments").toObject();
        if (!m_previewId.isEmpty()) { selected = m_previewId; key = "preview/" + selected; }
        else {
            selected = assignments.value("idle").toString("off");
            key = "idle/" + selected;
            // Explicit category priority; latest event wins ties. Off categories
            // never inherit another category's selection.
            const QStringList priority{"other", "email", "chat", "message", "calendar", "clock", "internet-call", "ringtone"};
            int best = -1;
            quint64 sequence = 0;
            for (auto it = m_events.begin(); it != m_events.end(); ++it) {
                const auto &event = it.value();
                const auto id = assignments.value(event.category).toString("off");
                const int rank = priority.indexOf(event.category);
                if (id == "off" || !m_patterns.contains(id)) continue;
                if (rank > best || (rank == best && event.sequence > sequence)) {
                    best = rank; sequence = event.sequence; selected = id; key = it.key() + "/" + id;
                }
            }
        }
    }
    if (selected == "off" || !m_patterns.contains(selected)) { selected.clear(); key.clear(); }
    const bool selectionChanged = key != m_selectionKey;
    if (selectionChanged) {
        m_selected = selected; m_selectionKey = key; m_elapsed.restart(); m_last = {{-1, -1, -1}};
        m_autonomous = false;
        m_finished = false;
    }
    const bool want = !m_selected.isEmpty();
    Inari::Registers registers;
    const auto pattern = m_patterns.value(m_selected);
    // Preview needs a live three-channel timeline and a guaranteed timeout.
    const bool canAutonomous = want && m_previewId.isEmpty()
                               && Inari::compileBreathing(pattern, &registers);
    bool constant = want && !pattern.repeat;
    for (const auto &step : pattern.steps)
        for (int c = 0; c < 3; ++c)
            constant = constant && !step.channels[c].delay && !step.channels[c].fade
                       && step.channels[c].target == pattern.steps[0].channels[c].target;
    const bool software = want && ((!canAutonomous && !constant && !m_finished) || !m_previewId.isEmpty());
    m_keepalive.request(software);
    const bool allowed = want && (!software || m_keepalive.ready());
    m_hardware.request(allowed);
    const bool ready = allowed && m_hardware.ready();
    const bool starting = ready && (!m_outputReady || selectionChanged);
    if (starting) { m_elapsed.restart(); m_last = {{-1, -1, -1}}; }
    m_outputReady = ready;
    if (ready && canAutonomous) {
        if (starting) m_autonomous = m_hardware.startBreathing(registers);
        m_tick.stop();
    } else if (ready && !m_finished) { m_tick.start(); tick(); }
    else if (ready) m_tick.stop();
    else {
        m_autonomous = false;
        m_tick.stop();
        if (m_last != Inari::Levels{{0, 0, 0}}) { m_last = {{0, 0, 0}}; emit FrameChanged(0, 0, 0); }
    }
    m_selecting = false;
}
void Player::tick()
{
    if (!m_outputReady || m_finished || !m_patterns.contains(m_selected)) return;
    bool finished;
    const auto frame = m_patterns.value(m_selected).frame(m_elapsed.nsecsElapsed() / 1000, &finished);
    if (frame != m_last && m_hardware.writeFrame(frame)) {
        m_last = frame; emit FrameChanged(frame[0], frame[1], frame[2]);
    }
    bool constant = true;
    for (const auto &step : m_patterns.value(m_selected).steps)
        for (int i = 0; i < 3; ++i)
            constant = constant && !step.channels[i].delay && !step.channels[i].fade
                       && step.channels[i].target == frame[i];
    if (finished || (constant && !m_patterns.value(m_selected).repeat)) {
        m_finished = finished;
        m_tick.stop();
        // A preview always retains its lease until its bounded timeout.
        if (m_previewId.isEmpty()) m_keepalive.request(false);
    }
}
