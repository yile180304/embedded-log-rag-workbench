#pragma once

#include <QJsonArray>
#include <QString>
#include <QVector>

struct KnowledgeSourceRecord
{
    QString sourceId;
    QString displayName;
    QString path;
    QString sourceType;
    QString manifestStatus;
    QString indexStatus;
    int chunks = 0;
    QString error;
};

QVector<KnowledgeSourceRecord> knowledgeSourceRecordsFromJson(const QJsonArray &array);
