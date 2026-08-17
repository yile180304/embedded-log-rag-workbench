#pragma once

#include <QJsonObject>
#include <QJsonArray>
#include <QString>
#include <QVector>

struct ExpectedEvidenceRef
{
    QString source;
    QString docId;
    QString page;
    QString section;
    QString chunkId;

    bool isUsable() const;
    QString granularity() const;
    QJsonObject toJson() const;
    static ExpectedEvidenceRef fromJson(const QJsonObject &object);
};

struct EvaluationCandidate
{
    QString id;
    QString originRecordId;
    QString createdAt;
    QString updatedAt;
    QString query;
    QString userFeedback = QStringLiteral("unreviewed");
    QString reviewStatus = QStringLiteral("candidate");
    QVector<ExpectedEvidenceRef> expectedEvidence;

    QJsonObject toJson() const;
};

struct ApprovedEvaluationSample
{
    QString id;
    QString candidateId;
    QString approvedAt;
    QString query;
    QString annotationGranularity;
    QVector<ExpectedEvidenceRef> expectedEvidence;

    QJsonObject toJson() const;
};

struct EvaluationRun
{
    QString id;
    QString createdAt;
    QString finishedAt;
    QString status;
    int topK = 0;
    int sampleCount = 0;
    QJsonArray samples;
    QJsonObject result;
    QString errorMessage;

    QJsonObject toJson() const;
};
