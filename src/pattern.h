#pragma once
#include <QByteArray>
#include <QString>
#include <QVector>
#include <array>

namespace Inari {
using Levels = std::array<int, 3>;
struct Ramp {
    int target = 0;
    qint64 delay = 0;
    qint64 fade = 0;
};
struct Step {
    qint64 duration = 0;
    std::array<Ramp, 3> channels;
};
struct Pattern {
    QString id;
    QString name;
    QVector<Step> steps;
    qint64 duration = 0;
    int repeat = 0;
    Levels frame(qint64 elapsed, bool *finished = nullptr) const;
    static bool parse(const QByteArray &json, Pattern *out, QString *error);
};
}
