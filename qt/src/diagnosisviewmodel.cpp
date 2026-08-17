#include "diagnosisviewmodel.h"

#include <QJsonArray>
#include <QJsonValue>

namespace {
QString valueAsText(const QJsonValue &value)
{
    if (value.isString()) {
        return value.toString();
    }
    if (value.isDouble()) {
        return QString::number(value.toDouble(), 'g', 12);
    }
    return {};
}
} // namespace

DiagnosisViewModel DiagnosisViewModel::fromJson(const QJsonObject &object, QString *errorMessage)
{
    DiagnosisViewModel model;
    if (!object.value(QStringLiteral("answer")).isString()
        || !object.value(QStringLiteral("grounded")).isBool()
        || !object.value(QStringLiteral("evidence")).isArray()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("诊断 JSON 缺少 answer、grounded 或 evidence 字段。");
        }
        return model;
    }

    model.query = object.value(QStringLiteral("query")).toString();
    model.answer = object.value(QStringLiteral("answer")).toString();
    model.grounded = object.value(QStringLiteral("grounded")).toBool();
    model.refusalReason = object.value(QStringLiteral("refusal_reason")).toString();

    const QJsonArray evidenceArray = object.value(QStringLiteral("evidence")).toArray();
    model.evidence.reserve(evidenceArray.size());
    for (const QJsonValue &value : evidenceArray) {
        const QJsonObject evidenceObject = value.toObject();
        const QJsonObject metadata = evidenceObject.value(QStringLiteral("metadata")).toObject();
        const QJsonObject scores = evidenceObject.value(QStringLiteral("scores")).toObject();
        EvidenceViewModel evidence;
        evidence.rank = evidenceObject.value(QStringLiteral("rank")).toInt();
        evidence.docId = valueAsText(metadata.value(QStringLiteral("doc_id")));
        evidence.page = valueAsText(metadata.value(QStringLiteral("page")));
        evidence.section = valueAsText(metadata.value(QStringLiteral("section")));
        evidence.source = evidenceObject.value(QStringLiteral("source")).toString();
        evidence.chunkId = evidenceObject.value(QStringLiteral("chunk_id")).toString();
        evidence.rerankerScore = scores.value(QStringLiteral("reranker")).toDouble();
        evidence.hybridScore = scores.value(QStringLiteral("hybrid")).toDouble();
        evidence.bm25Score = scores.value(QStringLiteral("bm25")).toDouble();
        evidence.denseScore = scores.value(QStringLiteral("dense")).toDouble();
        model.evidence.push_back(evidence);
    }

    const QJsonObject metadata = object.value(QStringLiteral("metadata")).toObject();
    model.embedding = metadata.value(QStringLiteral("embedding")).toString();
    model.reranker = metadata.value(QStringLiteral("reranker")).toString();
    model.retrievalMs = metadata.value(QStringLiteral("retrieval_ms")).toDouble();
    model.reportPath = metadata.value(QStringLiteral("report_path")).toString();
    const QJsonValue hardFaultValue = metadata.value(QStringLiteral("hardfault"));
    if (hardFaultValue.isObject()) {
        const QJsonObject hardFault = hardFaultValue.toObject();
        model.hardFault.available = true;
        model.hardFault.rawLog = hardFault.value(QStringLiteral("raw_log")).toString();
        const QJsonObject registers = hardFault.value(QStringLiteral("registers")).toObject();
        for (auto iterator = registers.constBegin(); iterator != registers.constEnd(); ++iterator) {
            model.hardFault.registers.insert(iterator.key(), valueAsText(iterator.value()));
        }
        const QJsonArray flags = hardFault.value(QStringLiteral("decoded_flags")).toArray();
        model.hardFault.decodedFlags.reserve(flags.size());
        for (const QJsonValue &flagValue : flags) {
            const QJsonObject flagObject = flagValue.toObject();
            model.hardFault.decodedFlags.push_back({
                flagObject.value(QStringLiteral("register")).toString(),
                flagObject.value(QStringLiteral("bit")).toInt(),
                flagObject.value(QStringLiteral("name")).toString(),
                flagObject.value(QStringLiteral("group")).toString(),
                flagObject.value(QStringLiteral("meaning")).toString()});
        }
        for (const QJsonValue &value : hardFault.value(QStringLiteral("observations")).toArray()) {
            model.hardFault.observations.push_back(value.toString());
        }
        for (const QJsonValue &value : hardFault.value(QStringLiteral("next_actions")).toArray()) {
            model.hardFault.nextActions.push_back(value.toString());
        }
        model.hardFault.generatedQuery = hardFault.value(QStringLiteral("generated_query")).toString();
    }
    const QJsonValue protocolLogValue = metadata.value(QStringLiteral("protocol_log"));
    if (protocolLogValue.isObject()) {
        QString protocolError;
        model.protocolLog = ProtocolLogAnalysisViewModel::fromJson(protocolLogValue.toObject(), &protocolError);
        if (!protocolError.isEmpty()) {
            if (errorMessage) {
                *errorMessage = protocolError;
            }
            return {};
        }
    }
    if (errorMessage) {
        errorMessage->clear();
    }
    return model;
}
