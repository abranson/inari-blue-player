#include <QtTest>
#include <QDir>
#include <QFile>
#include <QSet>

#include "pattern.h"

using namespace Inari;

static Levels levels(int red, int green, int blue)
{
    return Levels{{red, green, blue}};
}

static QString patternsPath()
{
    if (QDir("patterns").exists())
        return "patterns";
    if (QDir("../patterns").exists()) return "../patterns";
    return "/usr/share/inari-blue/patterns";
}

class PatternTest : public QObject
{
    Q_OBJECT

private slots:
    void rejectsMalformed_data();
    void rejectsMalformed();
    void rejectsLimits();
    void finiteCompletion();
    void kittStartupAndLoopFadeBoundaries();
    void travellingFadeStagger();
    void exact3125msBlink();
    void bundledPatternsParseAndIdsAreUnique();
};

static QByteArray patternJson(const QByteArray &step =
                              "{\"duration_ms\":10,\"channels\":[{\"to\":0},{\"to\":0},{\"to\":0}]}")
{
    return "{\"version\":1,\"id\":\"test\",\"name\":\"Test\",\"repeat\":0,\"steps\":[" + step + "]}";
}

void PatternTest::rejectsMalformed_data()
{
    QTest::addColumn<QByteArray>("json");
    QTest::addColumn<QString>("reason");

    QTest::newRow("invalid json") << QByteArray("{") << QString("json");
    QTest::newRow("unknown version") << patternJson().replace("\"version\":1", "\"version\":2") << QString("version");
    QTest::newRow("extra field") << patternJson().replace("\"steps\":", "\"extra\":1,\"steps\":") << QString("field");
    QTest::newRow("invalid id") << patternJson().replace("\"test\"", "\"Bad ID\"") << QString("id");
    QTest::newRow("empty name") << patternJson().replace("\"Test\"", "\"  \"") << QString("name");
    QTest::newRow("repeat is fractional") << patternJson().replace("\"repeat\":0", "\"repeat\":1.5") << QString("repeat");
    QTest::newRow("repeat is string") << patternJson().replace("\"repeat\":0", "\"repeat\":\"once\"") << QString("repeat");
    QTest::newRow("no steps") << QByteArray("{\"version\":1,\"id\":\"test\",\"name\":\"Test\",\"repeat\":0,\"steps\":[]}") << QString("steps");
    QTest::newRow("channel target fractional") << patternJson("{\"duration_ms\":10,\"channels\":[{\"to\":1.5},{\"to\":0},{\"to\":0}]}") << QString("target");
    QTest::newRow("wrong channel count") << patternJson("{\"duration_ms\":10,\"channels\":[{\"to\":0}]}") << QString("channels");
    QTest::newRow("transition too long") << patternJson("{\"duration_ms\":10,\"channels\":[{\"to\":1,\"delay_ms\":6,\"fade_ms\":5},{\"to\":0},{\"to\":0}]}") << QString("transition");
}

void PatternTest::rejectsMalformed()
{
    QFETCH(QByteArray, json);
    QFETCH(QString, reason);
    Pattern pattern;
    QString error;
    QVERIFY2(!Pattern::parse(json, &pattern, &error), qPrintable(reason));
    QVERIFY(!error.isEmpty());
}

void PatternTest::rejectsLimits()
{
    Pattern pattern;
    QString error;

    QByteArray tooLarge(65537, ' ');
    QVERIFY(!Pattern::parse(tooLarge, &pattern, &error));

    QByteArray tooMany = "{\"version\":1,\"id\":\"test\",\"name\":\"Test\",\"repeat\":0,\"steps\":[";
    for (int i = 0; i < 257; ++i)
        tooMany += (i ? "," : "") + QByteArray("{\"duration_ms\":1,\"channels\":[{\"to\":0},{\"to\":0},{\"to\":0}]}");
    tooMany += "]}";
    QVERIFY(!Pattern::parse(tooMany, &pattern, &error));

    QVERIFY(!Pattern::parse(patternJson("{\"duration_ms\":60001,\"channels\":[{\"to\":0},{\"to\":0},{\"to\":0}]}") , &pattern, &error));
    QVERIFY(!Pattern::parse(patternJson("{\"duration_ms\":10,\"channels\":[{\"to\":256},{\"to\":0},{\"to\":0}]}") , &pattern, &error));

    QByteArray fiveMinutes = "{\"duration_ms\":60000,\"channels\":[{\"to\":0},{\"to\":0},{\"to\":0}]}";
    QByteArray cycle = "{\"version\":1,\"id\":\"test\",\"name\":\"Test\",\"repeat\":0,\"steps\":[";
    for (int i = 0; i < 6; ++i)
        cycle += (i ? "," : "") + fiveMinutes;
    cycle += "]}";
    QVERIFY(!Pattern::parse(cycle, &pattern, &error));
}

void PatternTest::finiteCompletion()
{
    Pattern pattern;
    QByteArray finite = patternJson("{\"duration_ms\":10,\"channels\":[{\"to\":255},{\"to\":0},{\"to\":0}]}");
    finite.replace("\"repeat\":0", "\"repeat\":1");
    QVERIFY(Pattern::parse(finite, &pattern, nullptr));
    bool finished = false;
    QCOMPARE(pattern.frame(0, &finished), levels(255, 0, 0));
    QVERIFY(!finished);
    QCOMPARE(pattern.frame(9999, &finished), levels(255, 0, 0));
    QVERIFY(!finished);
    QCOMPARE(pattern.frame(10000, &finished), levels(0, 0, 0));
    QVERIFY(finished);
}

void PatternTest::kittStartupAndLoopFadeBoundaries()
{
    QFile file(QDir(patternsPath()).filePath("kitt-swoosh.json"));
    QVERIFY(file.open(QIODevice::ReadOnly));
    Pattern pattern;
    QVERIFY(Pattern::parse(file.readAll(), &pattern, nullptr));
    QCOMPARE(pattern.duration, qint64(1000000));
    QCOMPARE(pattern.frame(0), levels(255, 0, 0));
    QCOMPARE(pattern.frame(249999), levels(255, 0, 0));
    QCOMPARE(pattern.frame(250000), levels(255, 255, 0));
    QCOMPARE(pattern.frame(350000), levels(0, 255, 0));
    QCOMPARE(pattern.frame(750000), levels(0, 255, 255));
    QCOMPARE(pattern.frame(1000000), levels(255, 255, 0));
}

void PatternTest::travellingFadeStagger()
{
    QByteArray json = patternJson("{\"duration_ms\":1000,\"channels\":["
                                  "{\"to\":255,\"fade_ms\":500},"
                                  "{\"to\":255,\"delay_ms\":250,\"fade_ms\":500},"
                                  "{\"to\":255,\"delay_ms\":500,\"fade_ms\":500}]}");
    Pattern pattern;
    QVERIFY(Pattern::parse(json, &pattern, nullptr));
    QCOMPARE(pattern.frame(0), levels(0, 0, 0));
    QCOMPARE(pattern.frame(250000), levels(128, 0, 0));
    QCOMPARE(pattern.frame(500000), levels(255, 128, 0));
    QCOMPARE(pattern.frame(750000), levels(255, 255, 128));
}

void PatternTest::exact3125msBlink()
{
    QFile file(QDir(patternsPath()).filePath("rapid-blink.json"));
    QVERIFY(file.open(QIODevice::ReadOnly));
    Pattern pattern;
    QVERIFY(Pattern::parse(file.readAll(), &pattern, nullptr));
    QCOMPARE(pattern.steps.first().duration, qint64(31250));
    QCOMPARE(pattern.duration, qint64(62500));
    QCOMPARE(pattern.frame(31249), levels(255, 255, 255));
    QCOMPARE(pattern.frame(31250), levels(0, 0, 0));
    QCOMPARE(pattern.frame(62499), levels(0, 0, 0));
    QCOMPARE(pattern.frame(62500), levels(255, 255, 255));
}

void PatternTest::bundledPatternsParseAndIdsAreUnique()
{
    QDir directory(patternsPath());
    const QStringList files = directory.entryList(QStringList() << "*.json", QDir::Files);
    QVERIFY(!files.isEmpty());
    QSet<QString> ids;
    for (const QString &name : files) {
        QFile file(directory.filePath(name));
        QVERIFY(file.open(QIODevice::ReadOnly));
        Pattern pattern;
        QString error;
        QVERIFY2(Pattern::parse(file.readAll(), &pattern, &error), qPrintable(name + ": " + error));
        QVERIFY2(!ids.contains(pattern.id), qPrintable("duplicate id: " + pattern.id));
        ids.insert(pattern.id);
    }
    QCOMPARE(ids.size(), files.size());
}

QTEST_MAIN(PatternTest)
#include "tst_pattern.moc"
