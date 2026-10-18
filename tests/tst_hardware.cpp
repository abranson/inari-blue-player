#include <QtTest>
#include <QDBusContext>
#include <QDBusMessage>
#include <QDBusObjectPath>
#include <QDBusUnixFileDescriptor>
#include <fcntl.h>
#include <unistd.h>
#include "hardware.h"

class Cover : public QObject, protected QDBusContext {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.sailfishos.tohd1.Toh")
    Q_PROPERTY(uint VendorId READ vendor)
    Q_PROPERTY(uint ProductId READ product)
public:
    uint vendor() const { return 1; }
    uint product() const { return present ? 4 : 0; }
    bool present = true;
    bool delay = false;
    int borrowed = 0;
    int returned = 0;
    QDBusMessage pending;
    void finish(const QDBusConnection &bus) {
        int fd = ::open("/dev/null", O_RDWR);
        QDBusUnixFileDescriptor descriptor(fd); ::close(fd);
        bus.send(pending.createReply(QVariant::fromValue(descriptor)));
    }
public slots:
    QDBusUnixFileDescriptor BorrowI2cDevAccessWithPower(bool) {
        ++borrowed;
        if (delay) { pending = message(); setDelayedReply(true); return {}; }
        int fd = ::open("/dev/null", O_RDWR);
        QDBusUnixFileDescriptor descriptor(fd); ::close(fd); return descriptor;
    }
    void ReturnI2cDevAccess() { ++returned; }
};
class PrivacyMock : public QObject {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.sailfishos.privacyswitch")
public:
    bool active = false;
    void set(bool value) { active = value; emit privacyModeActiveChanged(value); }
public slots:
    bool privacyModeActive() { return active; }
signals:
    void privacyModeActiveChanged(bool active);
};
class HardwareTest : public QObject {
    Q_OBJECT
private slots:
    void privacyPreferenceAndUnavailableService() {
        auto server = QDBusConnection::connectToBus(qgetenv("DBUS_SESSION_BUS_ADDRESS"), "privacy-policy-mock");
        auto client = QDBusConnection::sessionBus();
        QVERIFY(server.registerService("org.sailfishos.tohd1"));
        QVERIFY(server.registerService("org.sailfishos.privacyswitch"));
        Cover cover; PrivacyMock privacy; privacy.active = true;
        QVERIFY(server.registerObject("/org/sailfishos/tohd1/toh", &cover, QDBusConnection::ExportAllSlots | QDBusConnection::ExportAllProperties));
        QVERIFY(server.registerObject("/privacyswitch", &privacy, QDBusConnection::ExportAllSlots | QDBusConnection::ExportAllSignals));
        Hardware hardware(client, client, [](int, int) { return true; });
        hardware.request(true);
        QTRY_VERIFY(hardware.present());
        QVERIFY(hardware.blocked());
        hardware.setPrivacySwitchEnabled(false);
        QTRY_VERIFY(hardware.ready() && !hardware.blocked());
        hardware.setPrivacySwitchEnabled(true);
        QVERIFY(hardware.blocked() && !hardware.ready());
        QTRY_COMPARE(cover.returned, 1);
        hardware.setPrivacySwitchEnabled(false);
        QTRY_VERIFY(hardware.ready());
        server.unregisterService("org.sailfishos.privacyswitch");
        QTRY_VERIFY(hardware.blocked() && !hardware.ready());
        QTRY_COMPARE(cover.returned, 2);
        QVERIFY(server.registerService("org.sailfishos.privacyswitch"));
        QTRY_VERIFY(hardware.ready() && !hardware.blocked());
        hardware.shutdown();
        server.unregisterService("org.sailfishos.tohd1");
        server.unregisterService("org.sailfishos.privacyswitch");
    }
    void lifecycle() {
        auto server = QDBusConnection::connectToBus(qgetenv("DBUS_SESSION_BUS_ADDRESS"), "hardware-mock");
        auto client = QDBusConnection::sessionBus();
        QVERIFY(server.registerService("org.sailfishos.tohd1"));
        QVERIFY(server.registerService("org.sailfishos.privacyswitch"));
        Cover cover; PrivacyMock privacy;
        QVERIFY(server.registerObject("/org/sailfishos/tohd1/toh", &cover, QDBusConnection::ExportAllSlots | QDBusConnection::ExportAllProperties));
        QVERIFY(server.registerObject("/privacyswitch", &privacy, QDBusConnection::ExportAllSlots | QDBusConnection::ExportAllSignals));
        Inari::Registers writes;
        Hardware hardware(client, client, [&writes](int a, int v) { writes.append(qMakePair(a,v)); return true; });
        QTRY_VERIFY(hardware.present() && !hardware.blocked());
        // Privacy during an in-flight loan must prevent initialisation.
        cover.delay = true; hardware.request(true);
        QTRY_COMPARE(cover.borrowed, 1);
        privacy.set(true); QTRY_VERIFY(hardware.blocked());
        cover.finish(server); QTRY_COMPARE(cover.returned, 1);
        QVERIFY(!hardware.ready());
        for (const auto &write : writes) QVERIFY(write != qMakePair(0x01, 1));
        // Re-enable, program autonomous output, then restore manual PWM.
        cover.delay = false; privacy.set(false);
        QTRY_VERIFY(hardware.ready());
        QVERIFY(hardware.startBreathing({{0x31, 0x1f}, {0x30, 0x0f}}));
        QVERIFY(hardware.writeFrame({{255, 0, 0}}));
        QVERIFY(writes.contains(qMakePair(0x31, 0x0f)));
        QCOMPARE(writes.last(), qMakePair(0x30, 7));
        // Removal releases the loan immediately and does not reacquire it.
        cover.present = false;
        auto removed = QDBusMessage::createSignal("/org/sailfishos/tohd1", "org.freedesktop.DBus.ObjectManager", "InterfacesRemoved");
        removed << QVariant::fromValue(QDBusObjectPath("/org/sailfishos/tohd1/toh")) << QStringList{"org.sailfishos.tohd1.Toh"};
        server.send(removed);
        QTRY_VERIFY(!hardware.present() && !hardware.ready());
        QTRY_COMPARE(cover.returned, 2);
        hardware.request(false);
        // A removed cover's delayed loan must not become valid for a replacement.
        cover.present = true; cover.delay = true;
        server.unregisterService("org.sailfishos.tohd1");
        QTest::qWait(30);
        QVERIFY(server.registerService("org.sailfishos.tohd1"));
        QTRY_VERIFY(hardware.present());
        hardware.request(true); QTRY_COMPARE(cover.borrowed, 3);
        cover.present = false; server.send(removed);
        QTRY_VERIFY(!hardware.present());
        hardware.request(false); cover.finish(server);
        QTRY_COMPARE(cover.returned, 3);
        QVERIFY(!hardware.ready());
        hardware.shutdown();
        server.unregisterService("org.sailfishos.tohd1");
        server.unregisterService("org.sailfishos.privacyswitch");
    }
    void breathingCompilation() {
        Inari::Pattern p; p.repeat = 0;
        for (int s = 0; s < 4; ++s) {
            Inari::Step step; step.duration = 1040000;
            for (auto &c : step.channels) { c.target = s < 2 ? 255 : 0; c.fade = s % 2 == 0 ? step.duration : 0; }
            p.steps.append(step);
        }
        Inari::Registers registers;
        QVERIFY(Inari::compileBreathing(p, &registers));
        QVERIFY(registers.contains(qMakePair(0x37, 0x66)));
        QVERIFY(registers.contains(qMakePair(0x38, 0x66)));
        QCOMPARE(registers.first(), qMakePair(0x30, 0));
        QCOMPARE(registers.last(), qMakePair(0x30, 0x0f));
        p.repeat = 2; QVERIFY(!Inari::compileBreathing(p, &registers)); p.repeat = 0;
        p.steps[0].duration = 1000000; QVERIFY(!Inari::compileBreathing(p, &registers));
        p.steps[0].duration = 1040000; p.steps[0].channels[0].delay = 1;
        QVERIFY(!Inari::compileBreathing(p, &registers));
    }
};
QTEST_GUILESS_MAIN(HardwareTest)
#include "tst_hardware.moc"
