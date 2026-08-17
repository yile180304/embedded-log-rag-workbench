#include "evaluation/evaluationrecord.h"

#include <QJsonArray>

namespace {
void addIfPresent(QJsonObject *object, const QString &key, const QString &value)
{
    if (!value.isEmpty()) {
        object->insert(key, value);
    }
}

QJsonArray refsToJson(const QVector<ExpectedEvidenceRef> &refs)
{
    QJsonArray result;
    for (const ExpectedEvidenceRef &ref : refs) {
        result.append(ref.toJson());
    }
    return result;
}
}

bool ExpectedEvidenceRef::isUsable() const
{
    return !chunkId.isEmpty() || (!docId.isEmpty() && !page.isEmpty())
           || (!source.isEmpty() && !section.isEmpty()) || !source.isEmpty();
}

QString ExpectedEvidenceRef::granularity() const
{
    if (!chunkId.isEmpty()) {
        return QStringLiteral("chunk");
    }
    if (!docId.isEmpty() && !page.isEmpty()) {
        return QStringLiteral("page");
    }
    if (!source.isEmpty() && !section.isEmpty()) {
        return QStringLiteral("section");
    }
    return QStringLiteral("source");
}

QJsonObject ExpectedEvidenceRef::toJson() const
{
    QJsonObject object;
    addIfPresent(&object, QStringLiteral("source"), source);
    addIfPresent(&object, QStringLiteral("doc_id"), docId);
    addIfPresent(&object, QStringLiteral("page"), page);
    addIfPresent(&object, QStringLiteral("section"), section);
    addIfPresent(&object, QStringLiteral("chunk_id"), chunkId);
    return object;
}

ExpectedEvidenceRef ExpectedEvidenceRef::fromJson(const QJsonObject &object)
{
    return {
        object.value(QStringLiteral("source")).toString(),
        object.value(QStringLiteral("doc_id")).toString(),
        object.value(QStringLiteral("page")).toVariant().toString(),
        object.value(QStringLiteral("section")).toString(),
        object.value(QStringLiteral("chunk_id")).toString(),
    };
}

QJsonObject EvaluationCandidate::toJson() const
{
    return {
        {QStringLiteral("candidate_id"), id},
        {QStringLiteral("origin_record_id"), originRecordId},
        {QStringLiteral("created_at"), createdAt},
        {QStringLiteral("updated_at"), updatedAt},
        {QStringLiteral("query"), query},
        {QStringLiteral("user_feedback"), userFeedback},
        {QStringLiteral("review_status"), reviewStatus},
        {QStringLiteral("expected_evidence"), refsToJson(expectedEvidence)},
    };
}

QJsonObject ApprovedEvaluationSample::toJson() const
{
    return {
        {QStringLiteral("sample_id"), id},
        {QStringLiteral("candidate_id"), candidateId},
        {QStringLiteral("approved_at"), approvedAt},
        {QStringLiteral("query"), query},
        {QStringLiteral("annotation_granularity"), annotationGranularity},
        {QStringLiteral("expected_evidence"), refsToJson(expectedEvidence)},
    };
}

QJsonObject EvaluationRun::toJson() const
{
    return {
        {QStringLiteral("run_id"), id},
        {QStringLiteral("created_at"), createdAt},
        {QStringLiteral("finished_at"), finishedAt},
        {QStringLiteral("status"), status},
        {QStringLiteral("top_k"), topK},
        {QStringLiteral("sample_count"), sampleCount},
        {QStringLiteral("samples"), samples},
        {QStringLiteral("result"), result},
        {QStringLiteral("error_message"), errorMessage},
    };
}
