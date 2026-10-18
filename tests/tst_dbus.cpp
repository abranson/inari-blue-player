#include <QtTest>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QTemporaryDir>
#include <QDir>

class BusTest : public QObject {
    Q_OBJECT
    QTemporaryDir config;
    QProcess server;
    QDBusConnection admin = QDBusConnection::sessionBus();
    QDBusMessage call(const QDBusConnection &bus, const QString &method, const QVariantList &args = {}) {
        auto msg = QDBusMessage::createMethodCall("org.sailfishos.InariBlue1", "/org/sailfishos/InariBlue1",
                                                  "org.sailfishos.InariBlue1", method);
        msg.setArguments(args);
        return bus.call(msg, QDBus::Block, 1000);
    }
    QString playing() {
        return QJsonDocument::fromJson(call(admin, "GetState").arguments().value(0).toString().toUtf8())
            .object().value("playing").toString();
    }
private slots:
    void initTestCase() {
        const auto root = QDir::currentPath();
        server.setProgram(root + "/build/inari-blue-player");
        server.setArguments({"--simulate", "--patterns-dir", root + "/patterns",
                             "--config-file", config.filePath("settings.json")});
        server.start();
        QVERIFY(server.waitForStarted());
        QTRY_VERIFY(call(admin, "GetState").type() == QDBusMessage::ReplyMessage);
        QVERIFY(call(admin, "SetEnabled", {true}).arguments().first().toBool());
        QVERIFY(call(admin, "SetPattern", {"idle", "steady"}).arguments().first().toBool());
        QVERIFY(call(admin, "SetPattern", {"clock", "alternating-alert"}).arguments().first().toBool());
    }
    void eventOwnerAndDisconnect() {
        {
            auto owner = QDBusConnection::connectToBus(QDBusConnection::SessionBus, "event-owner");
            QVERIFY(call(owner, "StartEvent", {uint(7), "clock"}).arguments().first().toBool());
            QCOMPARE(playing(), QString("alternating-alert"));
            call(admin, "StopEvent", {uint(7)});
            QCOMPARE(playing(), QString("alternating-alert"));
        }
        QDBusConnection::disconnectFromBus("event-owner");
        QTRY_COMPARE(playing(), QString("steady"));
    }
    void previewOwnerAndDisconnect() {
        {
            auto owner = QDBusConnection::connectToBus(QDBusConnection::SessionBus, "preview-owner");
            QVERIFY(call(owner, "Preview", {"kitt-swoosh", uint(5000)}).arguments().first().toBool());
            QCOMPARE(playing(), QString("kitt-swoosh"));
            call(admin, "StopPreview");
            QCOMPARE(playing(), QString("kitt-swoosh"));
        }
        QDBusConnection::disconnectFromBus("preview-owner");
        QTRY_COMPARE(playing(), QString("steady"));
    }
    void cleanupTestCase() {
        server.terminate();
        QVERIFY(server.waitForFinished(4000));
        QCOMPARE(server.exitCode(), 0);
    }
};
QTEST_GUILESS_MAIN(BusTest)
#include "tst_dbus.moc"
