#include <QtTest>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <memory>

#include "player.h"

class PlayerTest : public QObject
{
    Q_OBJECT

private slots:
    void defaultStateIsDisabledAndOff();
    void enablingAndAssigningPatternUpdatesState();
    void invalidAssignmentIsRejectedAndPreserved();
    void previewExpiresAndRestoresIdle();
    void explicitPreviewStopAndEventStopRestoreIdle();
    void categoryPriorityIsExplicit();
    void disabledCategoryDoesNotFallThrough();
    void malformedConfigIsPreserved();
    void invalidPatternOverrideLeavesBundledPattern();
    void autonomousBreathingAndPreviewRestore();

private:
    QString patternsPath() const;
    Player *player(QTemporaryDir *directory, const QString &configName = "settings.json");
    static QJsonObject state(Player *player);
};

QString PlayerTest::patternsPath() const
{
    const QString besideBinary = QDir(QCoreApplication::applicationDirPath())
        .absoluteFilePath("../patterns");
    if (QDir(besideBinary).exists())
        return QDir::cleanPath(besideBinary);
    if (QDir("patterns").exists()) return QDir::current().absoluteFilePath("patterns");
    return "/usr/share/inari-blue/patterns";
}

Player *PlayerTest::player(QTemporaryDir *directory, const QString &configName)
{
    return new Player(true, QStringList() << patternsPath(),
                      QDir(directory->path()).filePath(configName));
}

QJsonObject PlayerTest::state(Player *player)
{
    return QJsonDocument::fromJson(player->GetState().toUtf8()).object();
}

void PlayerTest::defaultStateIsDisabledAndOff()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    std::unique_ptr<Player> testPlayer(player(&directory));
    const auto current = state(testPlayer.get());
    QVERIFY(current.value("present").toBool());
    QVERIFY(!current.value("enabled").toBool());
    QCOMPARE(current.value("playing").toString(), QString());
    QCOMPARE(current.value("assignments").toObject().value("idle").toString(), QString("off"));
}

void PlayerTest::autonomousBreathingAndPreviewRestore()
{
    QTemporaryDir directory;
    std::unique_ptr<Player> testPlayer(player(&directory));
    QVERIFY(testPlayer->SetEnabled(true));
    QVERIFY(testPlayer->SetPattern("idle", "stock-breathing"));
    QVERIFY(state(testPlayer.get()).value("autonomous").toBool());
    QVERIFY(testPlayer->Preview("stock-breathing", 100));
    QVERIFY(!state(testPlayer.get()).value("autonomous").toBool());
    QTRY_VERIFY(!state(testPlayer.get()).value("preview").toBool());
    QVERIFY(state(testPlayer.get()).value("autonomous").toBool());
    QVERIFY(testPlayer->SetPattern("idle", "kitt-swoosh"));
    QVERIFY(!state(testPlayer.get()).value("autonomous").toBool());
    QVERIFY(testPlayer->SetEnabled(false));
    QVERIFY(!state(testPlayer.get()).value("ready").toBool());
}

void PlayerTest::enablingAndAssigningPatternUpdatesState()
{
    QTemporaryDir directory;
    std::unique_ptr<Player> testPlayer(player(&directory));
    QSignalSpy frames(testPlayer.get(), &Player::FrameChanged);
    QVERIFY(testPlayer->SetEnabled(true));
    QVERIFY(testPlayer->SetPattern("idle", "steady"));
    QTRY_VERIFY(frames.count() > 0);
    const auto current = state(testPlayer.get());
    QVERIFY(current.value("enabled").toBool());
    QCOMPARE(current.value("assignments").toObject().value("idle").toString(), QString("steady"));
    QCOMPARE(current.value("playing").toString(), QString("steady"));
}

void PlayerTest::invalidAssignmentIsRejectedAndPreserved()
{
    QTemporaryDir directory;
    std::unique_ptr<Player> testPlayer(player(&directory));
    QVERIFY(testPlayer->SetEnabled(true));
    QVERIFY(testPlayer->SetPattern("message", "steady"));
    QFile file(QDir(directory.path()).filePath("settings.json"));
    QVERIFY(file.open(QIODevice::ReadOnly));
    const QByteArray before = file.readAll();
    file.close();
    QVERIFY(!testPlayer->SetPattern("not-a-category", "steady"));
    QVERIFY(!testPlayer->SetPattern("message", "missing-pattern"));
    QVERIFY(file.open(QIODevice::ReadOnly));
    QCOMPARE(file.readAll(), before);
}

void PlayerTest::previewExpiresAndRestoresIdle()
{
    QTemporaryDir directory;
    std::unique_ptr<Player> testPlayer(player(&directory));
    QVERIFY(testPlayer->SetEnabled(true));
    QVERIFY(testPlayer->SetPattern("idle", "steady"));
    QVERIFY(testPlayer->Preview("kitt-swoosh", 100));
    QCOMPARE(state(testPlayer.get()).value("preview").toBool(), true);
    QCOMPARE(state(testPlayer.get()).value("playing").toString(), QString("kitt-swoosh"));
    QTRY_VERIFY(!state(testPlayer.get()).value("preview").toBool());
    QCOMPARE(state(testPlayer.get()).value("playing").toString(), QString("steady"));
}

void PlayerTest::explicitPreviewStopAndEventStopRestoreIdle()
{
    QTemporaryDir directory;
    std::unique_ptr<Player> testPlayer(player(&directory));
    QVERIFY(testPlayer->SetEnabled(true));
    QVERIFY(testPlayer->SetPattern("message", "kitt-swoosh"));
    QVERIFY(testPlayer->StartEvent(7, "message"));
    QCOMPARE(state(testPlayer.get()).value("playing").toString(), QString("kitt-swoosh"));
    QVERIFY(testPlayer->Preview("steady", 5000));
    QCOMPARE(state(testPlayer.get()).value("playing").toString(), QString("steady"));
    testPlayer->StopPreview();
    QCOMPARE(state(testPlayer.get()).value("playing").toString(), QString("kitt-swoosh"));
    testPlayer->StopEvent(7);
    QCOMPARE(state(testPlayer.get()).value("playing").toString(), QString());
}

void PlayerTest::categoryPriorityIsExplicit()
{
    QTemporaryDir directory;
    std::unique_ptr<Player> testPlayer(player(&directory));
    QVERIFY(testPlayer->SetEnabled(true));
    QVERIFY(testPlayer->SetPattern("message", "quick-flash"));
    QVERIFY(testPlayer->SetPattern("clock", "steady"));
    QVERIFY(testPlayer->SetPattern("ringtone", "kitt-swoosh"));
    QVERIFY(testPlayer->StartEvent(1, "message"));
    QVERIFY(testPlayer->StartEvent(2, "clock"));
    QVERIFY(testPlayer->StartEvent(3, "ringtone"));
    QCOMPARE(state(testPlayer.get()).value("playing").toString(), QString("kitt-swoosh"));
    testPlayer->StopEvent(3);
    QCOMPARE(state(testPlayer.get()).value("playing").toString(), QString("steady"));
    testPlayer->StopEvent(2);
    QCOMPARE(state(testPlayer.get()).value("playing").toString(), QString("quick-flash"));
}

void PlayerTest::disabledCategoryDoesNotFallThrough()
{
    QTemporaryDir directory;
    std::unique_ptr<Player> testPlayer(player(&directory));
    QVERIFY(testPlayer->SetEnabled(true));
    QVERIFY(testPlayer->SetPattern("idle", "steady"));
    QVERIFY(testPlayer->SetPattern("message", "off"));
    QVERIFY(testPlayer->StartEvent(1, "message"));
    QCOMPARE(state(testPlayer.get()).value("playing").toString(), QString("steady"));
}

void PlayerTest::malformedConfigIsPreserved()
{
    QTemporaryDir directory;
    const QByteArray malformed("{\"enabled\":true,\"assignments\":");
    QFile file(QDir(directory.path()).filePath("settings.json"));
    QVERIFY(file.open(QIODevice::WriteOnly));
    QCOMPARE(file.write(malformed), qint64(malformed.size()));
    file.close();
    std::unique_ptr<Player> testPlayer(player(&directory));
    QCOMPARE(state(testPlayer.get()).value("error").toString(),
             QString("Invalid player settings; existing file preserved"));
    QVERIFY(!testPlayer->SetEnabled(true));
    QVERIFY(file.open(QIODevice::ReadOnly));
    QCOMPARE(file.readAll(), malformed);
}

void PlayerTest::invalidPatternOverrideLeavesBundledPattern()
{
    QTemporaryDir directory;
    const QString userPatterns = QDir(directory.path()).filePath("patterns");
    QVERIFY(QDir().mkpath(userPatterns));
    QFile overrideFile(QDir(userPatterns).filePath("steady.json"));
    QVERIFY(overrideFile.open(QIODevice::WriteOnly));
    overrideFile.write("{\"version\":1,\"id\":\"steady\",\"steps\":[]}");
    overrideFile.close();
    std::unique_ptr<Player> testPlayer(new Player(true,
        QStringList() << patternsPath() << userPatterns,
        QDir(directory.path()).filePath("settings.json")));
    const auto patterns = state(testPlayer.get()).value("patterns").toArray();
    bool foundSteady = false;
    for (const auto &value : patterns)
        foundSteady = foundSteady || value.toObject().value("id").toString() == "steady";
    QVERIFY(foundSteady);
}

QTEST_MAIN(PlayerTest)
#include "tst_player.moc"
