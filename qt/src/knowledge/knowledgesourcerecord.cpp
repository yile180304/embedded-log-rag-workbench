#include "knowledge/knowledgesourcerecord.h"

#include <QJsonObject>

QVector<KnowledgeSourceRecord> knowledgeSourceRecordsFromJson(const QJsonArray &array)
{
    QVector<KnowledgeSourceRecord> records;
    records.reserve(array.size());
    for (const QJsonValue &value : array) {
        const QJsonObject object = value.toObject();
        KnowledgeSourceRecord record;
        record.sourceId = object.value(QStringLiteral("source_id")).toString();
        record.displayName = object.value(QStringLiteral("display_name")).toString();
        record.path = object.value(QStringLiteral("path")).toString();
        record.sourceType = object.value(QStringLiteral("source_type")).toString();
        record.manifestStatus = object.value(QStringLiteral("manifest_status")).toString();
        record.indexStatus = object.value(QStringLiteral("index_status")).toString();
        record.chunks = object.value(QStringLiteral("chunks")).toInt();
        record.error = object.value(QStringLiteral("error")).toString();
        records.push_back(record);
    }
    return records;
}
