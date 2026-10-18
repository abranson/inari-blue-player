#include <QtTest/QtTest>
#include <QDBusConnection>
#include <QDBusContext>
#include <QDBusMessage>

#include "../src/keepalive.h"

class FakeMce : public QObject, protected QDBusContext
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "com.nokia.mce.request")
public:
    int starts = 0;
    int stops = 0;
    int periods = 0;
    bool invalidPeriod = false;
    bool delayStart = false;
    QSet<QString> leases;
    QDBusMessage pending;

public slots:
    void req_cpu_keepalive_start(const QString &id) {
        ++starts;
        leases.insert(id);
        if (delayStart) { pending = message(); setDelayedReply(true); }
    }
    void req_cpu_keepalive_stop(const QString &id) { ++stops; leases.remove(id); }
    int req_cpu_keepalive_period() { ++periods; return invalidPeriod ? 1 : 2; }
};

class KeepAliveTests : public QObject
{
    Q_OBJECT
private slots:
    void init() { QTRY_VERIFY(mce.leases.isEmpty()); }
    void initTestCase();
    void cleanupTestCase();
    void pendingStartIsCancelledBeforeReenable();
    void repeatedRequestWhilePendingIsIdempotent();
    void ownerLossFailsClosedAndReenableStartsAgain();
    void renewalIsIssuedBeforeThePeriodExpires();
    void invalidRenewalPeriodFailsClosed();
    void simulationCanBeEnabledAndDisabled();
    void delayedRenewalFailsBeforeExpiry();

private:
    QDBusConnection bus = QDBusConnection::connectToBus(
        QDBusConnection::SessionBus, QString::fromLatin1("keepalive-tests"));
    FakeMce mce;
};

void KeepAliveTests::initTestCase()
{
    QVERIFY(bus.isConnected());
    QVERIFY(bus.registerService(QString::fromLatin1("com.nokia.mce")));
    QVERIFY(bus.registerObject(QString::fromLatin1("/com/nokia/mce/request"),
                               &mce, QDBusConnection::ExportAllSlots));
}

void KeepAliveTests::cleanupTestCase()
{
    bus.unregisterObject(QString::fromLatin1("/com/nokia/mce/request"));
    bus.unregisterService(QString::fromLatin1("com.nokia.mce"));
}

void KeepAliveTests::pendingStartIsCancelledBeforeReenable()
{
    KeepAlive keepAlive(false, QDBusConnection::sessionBus());
    QVERIFY(keepAlive.request(true));
    QVERIFY(keepAlive.request(false));
    QTRY_VERIFY(mce.stops >= 1);
    QVERIFY(!keepAlive.ready());
    QVERIFY(keepAlive.request(true));
    QTRY_VERIFY(keepAlive.ready());
    QVERIFY(mce.starts >= 1);
    keepAlive.shutdown();
    QTRY_VERIFY(!keepAlive.ready());
}

void KeepAliveTests::ownerLossFailsClosedAndReenableStartsAgain()
{
    KeepAlive keepAlive(false, QDBusConnection::sessionBus());
    connect(&keepAlive, &KeepAlive::changed, &keepAlive, [&keepAlive]() {
        if (keepAlive.error())
            keepAlive.request(true);
    });
    QVERIFY(keepAlive.request(true));
    QTRY_VERIFY(keepAlive.ready());
    bus.unregisterObject(QString::fromLatin1("/com/nokia/mce/request"));
    QVERIFY(bus.unregisterService(QString::fromLatin1("com.nokia.mce")));
    QTRY_VERIFY(keepAlive.error());
    QVERIFY(!keepAlive.ready());

    QVERIFY(bus.registerService(QString::fromLatin1("com.nokia.mce")));
    QVERIFY(bus.registerObject(QString::fromLatin1("/com/nokia/mce/request"),
                               &mce, QDBusConnection::ExportAllSlots));
    QTRY_VERIFY(keepAlive.ready());
    keepAlive.shutdown();
    QTRY_VERIFY(!keepAlive.ready());
}

void KeepAliveTests::repeatedRequestWhilePendingIsIdempotent()
{
    const int startsBefore = mce.starts;
    const int stopsBefore = mce.stops;
    KeepAlive keepAlive(false, QDBusConnection::sessionBus());
    QVERIFY(keepAlive.request(true));
    QVERIFY(keepAlive.request(true));
    QTRY_VERIFY(keepAlive.ready());
    QTest::qWait(100);
    QCOMPARE(mce.starts, startsBefore + 1);
    QCOMPARE(mce.stops, stopsBefore);
    keepAlive.shutdown();
    QTRY_VERIFY(!keepAlive.ready());
}

void KeepAliveTests::renewalIsIssuedBeforeThePeriodExpires()
{
    const int before = mce.starts;
    KeepAlive keepAlive(false, QDBusConnection::sessionBus());
    QVERIFY(keepAlive.request(true));
    QTRY_VERIFY(keepAlive.ready());
    QTRY_VERIFY(mce.starts > before + 1);
    keepAlive.shutdown();
    QTRY_VERIFY(!keepAlive.ready());
}

void KeepAliveTests::invalidRenewalPeriodFailsClosed()
{
    mce.invalidPeriod = true;
    KeepAlive keepAlive(false, QDBusConnection::sessionBus());
    QVERIFY(keepAlive.request(true));
    QTRY_VERIFY(keepAlive.error());
    QVERIFY(!keepAlive.ready());
    mce.invalidPeriod = false;
    QVERIFY(keepAlive.request(false));
    QVERIFY(keepAlive.request(true));
    QTRY_VERIFY(keepAlive.ready());
    keepAlive.shutdown();
    QTRY_VERIFY(!keepAlive.ready());
}

void KeepAliveTests::simulationCanBeEnabledAndDisabled()
{
    KeepAlive keepAlive(true);
    QSignalSpy changed(&keepAlive, &KeepAlive::changed);
    QVERIFY(keepAlive.request(true));
    QVERIFY(keepAlive.ready());
    QVERIFY(!keepAlive.error());
    QVERIFY(changed.count() > 0);
    QVERIFY(keepAlive.request(false));
    QVERIFY(!keepAlive.ready());
    QVERIFY(!keepAlive.error());
}

void KeepAliveTests::delayedRenewalFailsBeforeExpiry()
{
    KeepAlive keepAlive(false, QDBusConnection::sessionBus());
    connect(&keepAlive, &KeepAlive::changed, &keepAlive, [&keepAlive] {
        if (keepAlive.error()) keepAlive.request(true);
    });
    QVERIFY(keepAlive.request(true));
    QTRY_VERIFY(keepAlive.ready());
    mce.delayStart = true;
    QElapsedTimer elapsed; elapsed.start();
    QTRY_VERIFY(keepAlive.error());
    QVERIFY(elapsed.elapsed() < 2000);
    QVERIFY(!keepAlive.ready());
    const int starts = mce.starts;
    QTest::qWait(150);
    QCOMPARE(mce.starts, starts); // No retry loop through changed().
    mce.delayStart = false;
    keepAlive.request(false);
    keepAlive.request(true);
    QTRY_VERIFY(keepAlive.ready());
    bus.send(mce.pending.createReply());
    QTest::qWait(100);
    QVERIFY(keepAlive.ready()); // Old renewal cannot cancel the new lease.
    keepAlive.shutdown();
}

QTEST_GUILESS_MAIN(KeepAliveTests)
#include "keepalive-tests.moc"
