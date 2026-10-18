#include <QtTest>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QCryptographicHash>
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
    void repeatedAssignmentReplacementPreservesOtherCategories();
    void invalidAndOversizeFileDoesNotWrite();
    void transientFilePreviewSurvivesReloadWithoutCatalogOrEnablement();
    void validFileImportAssignsAndCollisionIsPreserved();
    void failedSettingsSaveRemovesNewImport();

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

static QByteArray testPattern(const QString &name = "Imported")
{
    QJsonArray channels;
    channels.append(QJsonObject{{"to", 1}});
    channels.append(QJsonObject{{"to", 2}});
    channels.append(QJsonObject{{"to", 3}});
    QJsonArray steps;
    steps.append(QJsonObject{{"duration_ms", 1000}, {"channels", channels}});
    return QJsonDocument(QJsonObject{{"version", 1}, {"id", "source"}, {"name", name},
        {"repeat", 0}, {"steps", steps}}).toJson(QJsonDocument::Compact);
}

static QString writeTestFile(const QString &dir, const QString &name, const QByteArray &data)
{
    const QString path = QDir(dir).filePath(name);
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly)) return QString();
    if (file.write(data) != data.size()) return QString();
    file.close();
    return path;
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

void PlayerTest::repeatedAssignmentReplacementPreservesOtherCategories()
{
    QTemporaryDir directory;
    std::unique_ptr<Player> testPlayer(player(&directory));
    QVERIFY(testPlayer->SetPattern("clock", "alternating-alert"));
    for (int i = 0; i < 80; ++i) {
        const QString id = i % 2 ? "kitt-swoosh" : "stock-breathing";
        QVERIFY(testPlayer->SetPattern("idle", id));
        const auto assignments = state(testPlayer.get()).value("assignments").toObject();
        QCOMPARE(assignments.value("idle").toString(), id);
        QCOMPARE(assignments.value("clock").toString(), QString("alternating-alert"));
    }
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

void PlayerTest::invalidAndOversizeFileDoesNotWrite()
{
    QTemporaryDir directory;
    const QString user = QDir(directory.path()).filePath("patterns");
    QVERIFY(QDir().mkpath(user));
    const QString invalid = writeTestFile(directory.path(), "invalid.json", "{}");
    std::unique_ptr<Player> inspection(player(&directory));
    QCOMPARE(QJsonDocument::fromJson(inspection->InspectPatternFile(invalid).toUtf8()).object().value("valid").toBool(), false);
    for (const QString &path : {QString("relative.json"), QString("https://example.invalid/pattern.json"),
                                QString("file://remote.invalid/tmp/pattern.json"), directory.path()})
        QVERIFY(!QJsonDocument::fromJson(inspection->InspectPatternFile(path).toUtf8()).object().value("valid").toBool());
    const QString oversize = writeTestFile(directory.path(), "oversize.json", QByteArray(65537, 'x'));
    std::unique_ptr<Player> testPlayer(new Player(true, QStringList() << patternsPath() << user,
        QDir(directory.path()).filePath("settings.json")));
    QVERIFY(!testPlayer->SetPatternFile("idle", invalid));
    QVERIFY(!testPlayer->SetPatternFile("idle", oversize));
    QCOMPARE(QDir(user).entryList(QStringList() << "*.json", QDir::Files).size(), 0);
}

void PlayerTest::transientFilePreviewSurvivesReloadWithoutCatalogOrEnablement()
{
    QTemporaryDir directory;
    const QString source = writeTestFile(directory.path(), "source.json", testPattern().replace("\"source\"", "\"off\""));
    std::unique_ptr<Player> testPlayer(player(&directory));
    QVERIFY(testPlayer->PreviewFile(source, 500));
    QVERIFY(!state(testPlayer.get()).value("enabled").toBool());
    QVERIFY(state(testPlayer.get()).value("preview").toBool());
    QCOMPARE(state(testPlayer.get()).value("patterns").toArray().size(), 12);
    testPlayer->ReloadPatterns();
    QVERIFY(state(testPlayer.get()).value("preview").toBool());
    QCOMPARE(state(testPlayer.get()).value("playing").toString(), QString("preview-file"));
    QVERIFY(!QFile::exists(QDir(directory.path()).filePath("settings.json")));
    testPlayer->StopPreview();
    QVERIFY(!state(testPlayer.get()).value("preview").toBool());
}

void PlayerTest::validFileImportAssignsAndCollisionIsPreserved()
{
    QTemporaryDir directory;
    const QString user = QDir(directory.path()).filePath("patterns");
    QVERIFY(QDir().mkpath(user));
    const QByteArray sourceBytes = testPattern();
    const QString source = writeTestFile(directory.path(), "source.json", sourceBytes);
    const QByteArray normalized = QJsonDocument::fromJson(sourceBytes).toJson(QJsonDocument::Compact);
    const QString stem = "import-" + QString::fromLatin1(QCryptographicHash::hash(normalized, QCryptographicHash::Sha256).toHex().left(24));
    writeTestFile(user, stem + ".json", "distinct");
    std::unique_ptr<Player> testPlayer(new Player(true, QStringList() << patternsPath() << user,
        QDir(directory.path()).filePath("settings.json")));
    QVERIFY(testPlayer->SetPatternFile("idle", source));
    const QString assigned = state(testPlayer.get()).value("assignments").toObject().value("idle").toString();
    QVERIFY(assigned.startsWith(stem + "-"));
    QFile original(QDir(user).filePath(stem + ".json"));
    QVERIFY(original.open(QIODevice::ReadOnly));
    QCOMPARE(original.readAll(), QByteArray("distinct"));
    QVERIFY(testPlayer->SetPatternFile("calendar", source));
    QCOMPARE(state(testPlayer.get()).value("assignments").toObject().value("calendar").toString(), assigned);
    QVERIFY(QFile::exists(QDir(user).filePath(assigned + ".json")));
    QVERIFY(state(testPlayer.get()).value("patterns").toArray().size() >= 13);
    testPlayer->ReloadPatterns();
    QCOMPARE(state(testPlayer.get()).value("assignments").toObject().value("idle").toString(), assigned);
}

void PlayerTest::failedSettingsSaveRemovesNewImport()
{
    QTemporaryDir directory;
    const QString user = QDir(directory.path()).filePath("patterns");
    QVERIFY(QDir().mkpath(user));
    const QString source = writeTestFile(directory.path(), "source.json", testPattern());
    const QString blocked = QDir(directory.path()).filePath("blocked");
    QFile blocker(blocked);
    QVERIFY(blocker.open(QIODevice::WriteOnly));
    blocker.write("x");
    blocker.close();
    std::unique_ptr<Player> testPlayer(new Player(true, QStringList() << patternsPath() << user,
        QDir(blocked).filePath("settings.json")));
    QVERIFY(!testPlayer->SetPatternFile("idle", source));
    QCOMPARE(QDir(user).entryList(QStringList() << "import-*.json", QDir::Files).size(), 0);
}

QTEST_MAIN(PlayerTest)
#include "tst_player.moc"
