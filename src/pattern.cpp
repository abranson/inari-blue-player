#include "pattern.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <cmath>

namespace Inari {
static bool keys(const QJsonObject &obj, const QStringList &allowed)
{
    for (auto it = obj.begin(); it != obj.end(); ++it)
        if (!allowed.contains(it.key())) return false;
    return true;
}
static bool number(const QJsonValue &v, double min, double max, double *out)
{
    if (!v.isDouble() || !std::isfinite(v.toDouble()) || v.toDouble() < min || v.toDouble() > max)
        return false;
    *out = v.toDouble();
    return true;
}
bool Pattern::parse(const QByteArray &json, Pattern *out, QString *error)
{
    auto fail = [error](const QString &why) { if (error) *error = why; return false; };
    if (json.size() > 65536) return fail("Pattern exceeds 64 KiB");
    QJsonParseError parseError;
    const auto doc = QJsonDocument::fromJson(json, &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) return fail("Invalid JSON object");
    const auto root = doc.object();
    if (!keys(root, {"version", "id", "name", "repeat", "steps"}) || root.value("version") != QJsonValue(1))
        return fail("Unsupported version or field");
    Pattern p;
    p.id = root.value("id").toString();
    p.name = root.value("name").toString();
    if (!QRegularExpression("^[a-z0-9][a-z0-9-]{0,63}$").match(p.id).hasMatch()
        || p.name.trimmed().isEmpty() || p.name.size() > 128) return fail("Invalid ID or name");
    double value;
    if (!number(root.value("repeat"), 0, 100000, &value) || value != std::floor(value)) return fail("Invalid repeat count");
    p.repeat = int(value);
    const auto steps = root.value("steps").toArray();
    if (steps.isEmpty() || steps.size() > 256) return fail("Expected 1 to 256 steps");
    for (const auto &entry : steps) {
        if (!entry.isObject()) return fail("Invalid step");
        const auto obj = entry.toObject();
        Step step;
        if (!keys(obj, {"duration_ms", "channels"}) || !number(obj.value("duration_ms"), 1, 60000, &value))
            return fail("Invalid step duration");
        step.duration = qRound64(value * 1000);
        const auto channels = obj.value("channels").toArray();
        if (channels.size() != 3) return fail("Each step needs exactly three channels");
        for (int i = 0; i < 3; ++i) {
            if (!channels[i].isObject()) return fail("Invalid channel");
            const auto ch = channels[i].toObject();
            auto &ramp = step.channels[i];
            if (!keys(ch, {"to", "fade_ms", "delay_ms"}) || !number(ch.value("to"), 0, 255, &value)
                || value != std::floor(value)) return fail("Brightness must be an integer from 0 to 255");
            ramp.target = int(value);
            if (ch.contains("delay_ms")) {
                if (!number(ch.value("delay_ms"), 0, 60000, &value)) return fail("Invalid delay");
                ramp.delay = qRound64(value * 1000);
            }
            if (ch.contains("fade_ms")) {
                if (!number(ch.value("fade_ms"), 0, 60000, &value)) return fail("Invalid fade");
                ramp.fade = qRound64(value * 1000);
            }
            if (ramp.delay + ramp.fade > step.duration) return fail("Channel transition exceeds step duration");
        }
        p.duration += step.duration;
        if (p.duration > 300000000) return fail("Cycle exceeds five minutes");
        p.steps.append(step);
    }
    *out = p;
    if (error) error->clear();
    return true;
}
Levels Pattern::frame(qint64 elapsed, bool *finished) const
{
    Levels previous{{0, 0, 0}};
    const bool done = steps.isEmpty() || duration <= 0 || (repeat && elapsed >= duration * repeat);
    if (finished) *finished = done;
    if (done || elapsed < 0) return previous;
    if (elapsed >= duration)
        for (int i = 0; i < 3; ++i) previous[i] = steps.last().channels[i].target;
    qint64 offset = elapsed % duration;
    for (const auto &step : steps) {
        if (offset < step.duration) {
            Levels result;
            for (int i = 0; i < 3; ++i) {
                const auto &ramp = step.channels[i];
                const qint64 time = offset - ramp.delay;
                if (time < 0) result[i] = previous[i];
                else if (!ramp.fade || time >= ramp.fade) result[i] = ramp.target;
                else result[i] = qRound(previous[i] + (ramp.target - previous[i]) * double(time) / ramp.fade);
            }
            return result;
        }
        offset -= step.duration;
        for (int i = 0; i < 3; ++i) previous[i] = step.channels[i].target;
    }
    return {{0, 0, 0}};
}
}
