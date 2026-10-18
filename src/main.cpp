#include "player.h"
#include <QCoreApplication>
#include <QCommandLineParser>
#include <QDBusConnection>
#include <QDir>
#include <QDebug>
#include <QDBusError>
#include <QSocketNotifier>
#include <QStandardPaths>
#include <csignal>
#include <unistd.h>
#include <fcntl.h>

static int signalPipe[2] = {-1, -1};
static void quitSignal(int)
{
    const char byte = 1;
    if (signalPipe[1] >= 0) { const auto ignored = ::write(signalPipe[1], &byte, 1); (void)ignored; }
}
int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName("inari-blue-player");
    QCommandLineParser parser;
    parser.addHelpOption();
    parser.addOption({"simulate", "Use a simulated cover and privacy state; never access hardware"});
    parser.addOption({"patterns-dir", "Override pattern search directories (repeatable)", "directory"});
    parser.addOption({"config-file", "Override the private settings file", "file"});
    parser.process(app);
    QStringList directories = parser.values("patterns-dir");
    if (directories.isEmpty()) directories = QStringList{"/usr/share/inari-blue/patterns",
        QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + "/inari-blue/patterns"};
    const QString config = parser.isSet("config-file") ? parser.value("config-file")
        : QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) + "/inari-blue/settings.json";
    Player player(parser.isSet("simulate"), directories, config);
    auto bus = QDBusConnection::sessionBus();
    if (!bus.registerObject("/org/sailfishos/InariBlue1", &player, QDBusConnection::ExportAllSlots | QDBusConnection::ExportAllSignals)
        || !bus.registerService("org.sailfishos.InariBlue1")) {
        qCritical() << "Cannot register player:" << bus.lastError().message();
        return 1;
    }
    if (::pipe2(signalPipe, O_NONBLOCK | O_CLOEXEC) < 0) return 1;
    QSocketNotifier notifier(signalPipe[0], QSocketNotifier::Read);
    QObject::connect(&notifier, &QSocketNotifier::activated,
                     &app, [&app] { app.quit(); });
    std::signal(SIGTERM, quitSignal);
    std::signal(SIGINT, quitSignal);
    QObject::connect(&app, &QCoreApplication::aboutToQuit, &player, &Player::shutdown);
    return app.exec();
}
