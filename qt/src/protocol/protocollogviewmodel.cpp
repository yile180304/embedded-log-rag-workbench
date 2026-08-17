#include "protocol/protocollogviewmodel.h"

#include <QJsonArray>
#include <QJsonValue>

namespace {
QString optionalText(const QJsonValue &value, int precision = 3)
{
    if (value.isString()) {
        return value.toString();
    }
    if (value.isDouble()) {
        return QString::number(value.toDouble(), 'f', precision);
    }
    return {};
}

QString crcState(const QJsonObject &event)
{
    const QJsonValue valid = event.value(QStringLiteral("crc_valid"));
    if (!valid.isBool()) {
        return QStringLiteral("未校验");
    }
    const QString calculated = event.value(QStringLiteral("calculated_crc")).toString();
    return valid.toBool()
               ? QStringLiteral("通过 · %1").arg(calculated)
               : QStringLiteral("失败 · %1").arg(calculated);
}
} // namespace

ProtocolLogAnalysisViewModel ProtocolLogAnalysisViewModel::fromJson(
    const QJsonObject &object,
    QString *errorMessage)
{
    ProtocolLogAnalysisViewModel model;
    if (!object.value(QStringLiteral("profile")).isString()
        || !object.value(QStringLiteral("summary")).isObject()
        || !object.value(QStringLiteral("events")).isArray()
        || !object.value(QStringLiteral("anomalies")).isArray()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("protocol_log JSON 缺少 profile、summary、events 或 anomalies 字段。");
        }
        return model;
    }

    model.available = true;
    model.profile = object.value(QStringLiteral("profile")).toString();
    const QJsonObject summary = object.value(QStringLiteral("summary")).toObject();
    model.lineCount = summary.value(QStringLiteral("line_count")).toInt();
    model.eventCount = summary.value(QStringLiteral("event_count")).toInt();
    model.errorCount = summary.value(QStringLiteral("error_count")).toInt();
    model.warningCount = summary.value(QStringLiteral("warning_count")).toInt();
    model.config = object.value(QStringLiteral("config")).toObject();
    model.generatedQuery = object.value(QStringLiteral("generated_query")).toString();

    const QJsonArray events = object.value(QStringLiteral("events")).toArray();
    model.events.reserve(events.size());
    for (const QJsonValue &value : events) {
        const QJsonObject event = value.toObject();
        model.events.push_back({
            event.value(QStringLiteral("line_no")).toInt(),
            event.value(QStringLiteral("timestamp")).toString(),
            event.value(QStringLiteral("protocol")).toString(),
            event.value(QStringLiteral("source")).toString(),
            event.value(QStringLiteral("destination")).toString(),
            optionalText(event.value(QStringLiteral("sequence")), 0),
            optionalText(event.value(QStringLiteral("length")), 0),
            optionalText(event.value(QStringLiteral("payload_length")), 0),
            optionalText(event.value(QStringLiteral("interval_ms"))),
            crcState(event),
            event.value(QStringLiteral("fields")).toObject(),
            event.value(QStringLiteral("raw_message")).toString(),
        });
    }

    const QJsonArray anomalies = object.value(QStringLiteral("anomalies")).toArray();
    model.anomalies.reserve(anomalies.size());
    for (const QJsonValue &value : anomalies) {
        const QJsonObject anomaly = value.toObject();
        model.anomalies.push_back({
            anomaly.value(QStringLiteral("type")).toString(),
            anomaly.value(QStringLiteral("severity")).toString(),
            anomaly.value(QStringLiteral("message")).toString(),
            anomaly.value(QStringLiteral("line_no")).toInt(),
            anomaly,
        });
    }
    if (errorMessage) {
        errorMessage->clear();
    }
    return model;
}
