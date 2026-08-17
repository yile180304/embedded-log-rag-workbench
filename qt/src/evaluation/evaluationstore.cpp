#include "evaluation/evaluationstore.h"

#include "storage/localdatabaseschema.h"

#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSqlError>
#include <QSqlQuery>
#include <QUuid>

namespace {
QString candidateColumns()
{
    return QStringLiteral("id, origin_record_id, created_at, updated_at, query_text, "
                          "user_feedback, review_status, expected_evidence_json");
}

QString sampleColumns()
{
    return QStringLiteral("id, candidate_id, approved_at, query_text, expected_evidence_json, "
                          "annotation_granularity, active");
}

QString runColumns()
{
    return QStringLiteral("id, created_at, finished_at, status, top_k, sample_count, "
                          "samples_json, result_json, error_message");
}

QString nonNullText(const QString &value)
{
    return value.isNull() ? QStringLiteral("") : value;
}

QVector<ExpectedEvidenceRef> parseRefs(const QByteArray &json)
{
    QVector<ExpectedEvidenceRef> refs;
    const QJsonArray array = QJsonDocument::fromJson(json).array();
    refs.reserve(array.size());
    for (const QJsonValue &value : array) {
        const ExpectedEvidenceRef ref = ExpectedEvidenceRef::fromJson(value.toObject());
        if (ref.isUsable()) {
            refs.push_back(ref);
        }
    }
    return refs;
}
}

EvaluationStore::EvaluationStore(QString databasePath)
    : databasePath_(QDir::cleanPath(std::move(databasePath)))
    , connectionName_(QStringLiteral("evaluation-store-%1")
                          .arg(QUuid::createUuid().toString(QUuid::WithoutBraces)))
{
}

EvaluationStore::~EvaluationStore()
{
    if (database_.isValid()) {
        database_.close();
    }
    database_ = {};
    QSqlDatabase::removeDatabase(connectionName_);
}

bool EvaluationStore::initialize(QString *errorMessage)
{
    const QFileInfo databaseFile(databasePath_);
    if (!QDir().mkpath(databaseFile.absolutePath())) {
        setError(errorMessage, QStringLiteral("无法创建评估库目录：%1").arg(databaseFile.absolutePath()));
        return false;
    }
    database_ = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connectionName_);
    database_.setDatabaseName(databasePath_);
    if (!database_.open()) {
        setError(errorMessage, QStringLiteral("无法打开评估库：%1").arg(database_.lastError().text()));
        return false;
    }
    if (!LocalDatabaseSchema::ensure(database_, errorMessage)) {
        database_.close();
        return false;
    }
    ready_ = true;
    if (errorMessage) {
        errorMessage->clear();
    }
    return true;
}

bool EvaluationStore::createOrGetFromCase(const CaseRecord &record,
                                          const QString &feedback,
                                          EvaluationCandidate *candidate,
                                          QString *errorMessage)
{
    if (!ready_ || !candidate || record.id.isEmpty()) {
        setError(errorMessage, QStringLiteral("创建候选需要有效的案例和输出参数。"));
        return false;
    }
    QSqlQuery existing(database_);
    existing.prepare(QStringLiteral("SELECT %1 FROM evaluation_candidates "
                                    "WHERE origin_record_id = :origin AND review_status IN ('candidate', 'approved') "
                                    "ORDER BY updated_at DESC LIMIT 1")
                         .arg(candidateColumns()));
    existing.bindValue(QStringLiteral(":origin"), record.id);
    if (!existing.exec()) {
        setError(errorMessage, existing.lastError().text());
        return false;
    }
    if (existing.next()) {
        *candidate = candidateFromQuery(existing);
        if (candidate->reviewStatus == QStringLiteral("candidate") && !feedback.isEmpty()
            && candidate->userFeedback != feedback) {
            candidate->userFeedback = feedback;
            return saveCandidate(*candidate, nullptr, errorMessage);
        }
        if (errorMessage) {
            errorMessage->clear();
        }
        return true;
    }

    EvaluationCandidate created;
    created.originRecordId = record.id;
    created.query = record.query.trimmed().isEmpty() ? record.inputText.trimmed() : record.query.trimmed();
    created.userFeedback = feedback.isEmpty() ? QStringLiteral("unreviewed") : feedback;
    created.expectedEvidence = evidenceRefsFromResult(record.resultJson);
    QString savedId;
    if (!saveCandidate(created, &savedId, errorMessage)) {
        return false;
    }
    return getCandidate(savedId, candidate, errorMessage);
}

bool EvaluationStore::saveCandidate(EvaluationCandidate candidate,
                                    QString *savedId,
                                    QString *errorMessage)
{
    if (!ready_) {
        setError(errorMessage, QStringLiteral("评估库尚未初始化。"));
        return false;
    }
    if (candidate.query.trimmed().isEmpty() || candidate.query.size() > 2000) {
        setError(errorMessage, QStringLiteral("候选 query 必须是 1 到 2000 个字符。"));
        return false;
    }
    if (candidate.userFeedback != QStringLiteral("helpful")
        && candidate.userFeedback != QStringLiteral("unhelpful")
        && candidate.userFeedback != QStringLiteral("unreviewed")) {
        setError(errorMessage, QStringLiteral("候选反馈值无效。"));
        return false;
    }
    if (candidate.reviewStatus != QStringLiteral("candidate")
        && candidate.reviewStatus != QStringLiteral("approved")
        && candidate.reviewStatus != QStringLiteral("rejected")) {
        setError(errorMessage, QStringLiteral("候选审核状态无效。"));
        return false;
    }
    for (const ExpectedEvidenceRef &ref : candidate.expectedEvidence) {
        if (!ref.isUsable()) {
            setError(errorMessage, QStringLiteral("候选包含无法追溯的 Evidence 引用。"));
            return false;
        }
    }
    const QString now = timestamp();
    if (candidate.id.isEmpty()) {
        candidate.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        candidate.createdAt = now;
        candidate.updatedAt = now;
        QSqlQuery query(database_);
        query.prepare(QStringLiteral(
            "INSERT INTO evaluation_candidates (id, origin_record_id, created_at, updated_at, query_text, "
            "user_feedback, review_status, expected_evidence_json) VALUES (:id, :origin, :created, :updated, "
            ":query, :feedback, :status, :evidence)"));
        query.bindValue(QStringLiteral(":id"), candidate.id);
        query.bindValue(QStringLiteral(":origin"), nonNullText(candidate.originRecordId));
        query.bindValue(QStringLiteral(":created"), candidate.createdAt);
        query.bindValue(QStringLiteral(":updated"), candidate.updatedAt);
        query.bindValue(QStringLiteral(":query"), candidate.query.trimmed());
        query.bindValue(QStringLiteral(":feedback"), candidate.userFeedback);
        query.bindValue(QStringLiteral(":status"), candidate.reviewStatus);
        query.bindValue(QStringLiteral(":evidence"),
                        QJsonDocument([&] {
                            QJsonArray array;
                            for (const auto &ref : candidate.expectedEvidence) array.append(ref.toJson());
                            return array;
                        }()).toJson(QJsonDocument::Compact));
        if (!query.exec()) {
            setError(errorMessage, query.lastError().text());
            return false;
        }
    } else {
        QSqlQuery statusQuery(database_);
        statusQuery.prepare(QStringLiteral("SELECT review_status FROM evaluation_candidates WHERE id = :id"));
        statusQuery.bindValue(QStringLiteral(":id"), candidate.id);
        if (!statusQuery.exec() || !statusQuery.next()) {
            setError(errorMessage, QStringLiteral("候选不存在：%1").arg(candidate.id));
            return false;
        }
        if (statusQuery.value(0).toString() == QStringLiteral("approved")
            && candidate.reviewStatus != QStringLiteral("approved")) {
            setError(errorMessage, QStringLiteral("已批准候选不能退回编辑状态。"));
            return false;
        }
        candidate.updatedAt = now;
        QSqlQuery query(database_);
        query.prepare(QStringLiteral(
            "UPDATE evaluation_candidates SET updated_at = :updated, query_text = :query, user_feedback = :feedback, "
            "review_status = :status, expected_evidence_json = :evidence WHERE id = :id"));
        query.bindValue(QStringLiteral(":id"), candidate.id);
        query.bindValue(QStringLiteral(":updated"), candidate.updatedAt);
        query.bindValue(QStringLiteral(":query"), candidate.query.trimmed());
        query.bindValue(QStringLiteral(":feedback"), candidate.userFeedback);
        query.bindValue(QStringLiteral(":status"), candidate.reviewStatus);
        QJsonArray array;
        for (const auto &ref : candidate.expectedEvidence) array.append(ref.toJson());
        query.bindValue(QStringLiteral(":evidence"), QJsonDocument(array).toJson(QJsonDocument::Compact));
        if (!query.exec() || query.numRowsAffected() != 1) {
            setError(errorMessage, query.lastError().text().isEmpty()
                                       ? QStringLiteral("候选不存在：%1").arg(candidate.id)
                                       : query.lastError().text());
            return false;
        }
    }
    if (savedId) {
        *savedId = candidate.id;
    }
    if (errorMessage) {
        errorMessage->clear();
    }
    return true;
}

bool EvaluationStore::getCandidate(const QString &id,
                                   EvaluationCandidate *candidate,
                                   QString *errorMessage) const
{
    if (!ready_ || !candidate) {
        setError(errorMessage, QStringLiteral("评估库未就绪或输出参数无效。"));
        return false;
    }
    QSqlQuery query(database_);
    query.prepare(QStringLiteral("SELECT %1 FROM evaluation_candidates WHERE id = :id").arg(candidateColumns()));
    query.bindValue(QStringLiteral(":id"), id);
    if (!query.exec() || !query.next()) {
        setError(errorMessage, QStringLiteral("候选不存在：%1").arg(id));
        return false;
    }
    *candidate = candidateFromQuery(query);
    if (errorMessage) errorMessage->clear();
    return true;
}

bool EvaluationStore::importCandidates(const QVector<EvaluationCandidate> &candidates,
                                       int *importedCount,
                                       QString *errorMessage)
{
    if (!ready_ || candidates.isEmpty() || candidates.size() > 500) {
        setError(errorMessage, QStringLiteral("导入评估候选需要 1 到 500 条有效记录。"));
        return false;
    }
    if (!database_.transaction()) {
        setError(errorMessage, database_.lastError().text());
        return false;
    }
    int imported = 0;
    for (EvaluationCandidate candidate : candidates) {
        candidate.id.clear();
        candidate.originRecordId.clear();
        candidate.reviewStatus = QStringLiteral("candidate");
        if (!saveCandidate(candidate, nullptr, errorMessage)) {
            database_.rollback();
            return false;
        }
        ++imported;
    }
    if (!database_.commit()) {
        database_.rollback();
        setError(errorMessage, database_.lastError().text());
        return false;
    }
    if (importedCount) *importedCount = imported;
    if (errorMessage) errorMessage->clear();
    return true;
}

QVector<EvaluationCandidate> EvaluationStore::listCandidates(const QString &reviewStatus,
                                                              QString *errorMessage) const
{
    QVector<EvaluationCandidate> candidates;
    if (!ready_) {
        setError(errorMessage, QStringLiteral("评估库尚未初始化。"));
        return candidates;
    }
    QString sql = QStringLiteral("SELECT %1 FROM evaluation_candidates").arg(candidateColumns());
    if (!reviewStatus.isEmpty()) sql += QStringLiteral(" WHERE review_status = :status");
    sql += QStringLiteral(" ORDER BY updated_at DESC");
    QSqlQuery query(database_);
    query.prepare(sql);
    if (!reviewStatus.isEmpty()) query.bindValue(QStringLiteral(":status"), reviewStatus);
    if (!query.exec()) {
        setError(errorMessage, query.lastError().text());
        return candidates;
    }
    while (query.next()) candidates.push_back(candidateFromQuery(query));
    if (errorMessage) errorMessage->clear();
    return candidates;
}

bool EvaluationStore::approveCandidate(const EvaluationCandidate &candidate,
                                       ApprovedEvaluationSample *sample,
                                       QString *errorMessage)
{
    if (!ready_ || !sample) {
        setError(errorMessage, QStringLiteral("评估库未就绪或输出参数无效。"));
        return false;
    }
    EvaluationCandidate stored;
    if (!getCandidate(candidate.id, &stored, errorMessage)) return false;
    if (stored.reviewStatus == QStringLiteral("approved")) {
        QSqlQuery existing(database_);
        existing.prepare(QStringLiteral("SELECT %1 FROM evaluation_samples WHERE candidate_id = :candidate AND active = 1")
                             .arg(sampleColumns()));
        existing.bindValue(QStringLiteral(":candidate"), candidate.id);
        if (!existing.exec() || !existing.next()) {
            setError(errorMessage, QStringLiteral("已批准候选缺少 active 样本：%1").arg(candidate.id));
            return false;
        }
        *sample = sampleFromQuery(existing);
        return true;
    }
    if (stored.reviewStatus != QStringLiteral("candidate")) {
        setError(errorMessage, QStringLiteral("只有 candidate 状态可以批准。"));
        return false;
    }
    if (candidate.query.trimmed().isEmpty() || candidate.expectedEvidence.isEmpty()) {
        setError(errorMessage, QStringLiteral("批准需要 query 和至少一个 Expected Evidence Ref。"));
        return false;
    }
    const QString sampleId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    const QString now = timestamp();
    QJsonArray refs;
    for (const auto &ref : candidate.expectedEvidence) refs.append(ref.toJson());
    if (!database_.transaction()) {
        setError(errorMessage, database_.lastError().text());
        return false;
    }
    QSqlQuery insert(database_);
    insert.prepare(QStringLiteral(
        "INSERT INTO evaluation_samples (id, candidate_id, approved_at, query_text, expected_evidence_json, "
        "annotation_granularity, active) VALUES (:id, :candidate, :approved, :query, :evidence, :granularity, 1)"));
    insert.bindValue(QStringLiteral(":id"), sampleId);
    insert.bindValue(QStringLiteral(":candidate"), candidate.id);
    insert.bindValue(QStringLiteral(":approved"), now);
    insert.bindValue(QStringLiteral(":query"), candidate.query.trimmed());
    insert.bindValue(QStringLiteral(":evidence"), QJsonDocument(refs).toJson(QJsonDocument::Compact));
    insert.bindValue(QStringLiteral(":granularity"), annotationGranularity(candidate.expectedEvidence));
    QSqlQuery update(database_);
    update.prepare(QStringLiteral(
        "UPDATE evaluation_candidates SET review_status = 'approved', updated_at = :updated, "
        "query_text = :query, expected_evidence_json = :evidence "
        "WHERE id = :id AND review_status = 'candidate'"));
    update.bindValue(QStringLiteral(":updated"), now);
    update.bindValue(QStringLiteral(":id"), candidate.id);
    update.bindValue(QStringLiteral(":query"), candidate.query.trimmed());
    update.bindValue(QStringLiteral(":evidence"), QJsonDocument(refs).toJson(QJsonDocument::Compact));
    if (!insert.exec() || !update.exec() || update.numRowsAffected() != 1 || !database_.commit()) {
        database_.rollback();
        setError(errorMessage, insert.lastError().text().isEmpty()
                                   ? (update.lastError().text().isEmpty() ? database_.lastError().text()
                                                                         : update.lastError().text())
                                   : insert.lastError().text());
        return false;
    }
    *sample = {sampleId,
               candidate.id,
               now,
               candidate.query.trimmed(),
               annotationGranularity(candidate.expectedEvidence),
               candidate.expectedEvidence};
    if (errorMessage) errorMessage->clear();
    return true;
}

bool EvaluationStore::rejectCandidate(const QString &id, QString *errorMessage)
{
    if (!ready_) {
        setError(errorMessage, QStringLiteral("评估库尚未初始化。"));
        return false;
    }
    QSqlQuery query(database_);
    query.prepare(QStringLiteral("UPDATE evaluation_candidates SET review_status = 'rejected', updated_at = :updated "
                                 "WHERE id = :id AND review_status = 'candidate'"));
    query.bindValue(QStringLiteral(":updated"), timestamp());
    query.bindValue(QStringLiteral(":id"), id);
    if (!query.exec() || query.numRowsAffected() != 1) {
        setError(errorMessage, QStringLiteral("候选不存在或已离开 candidate 状态：%1").arg(id));
        return false;
    }
    if (errorMessage) errorMessage->clear();
    return true;
}

QVector<ApprovedEvaluationSample> EvaluationStore::listApprovedSamples(QString *errorMessage) const
{
    QVector<ApprovedEvaluationSample> samples;
    if (!ready_) {
        setError(errorMessage, QStringLiteral("评估库尚未初始化。"));
        return samples;
    }
    QSqlQuery query(database_);
    if (!query.exec(QStringLiteral("SELECT %1 FROM evaluation_samples WHERE active = 1 ORDER BY approved_at DESC")
                        .arg(sampleColumns()))) {
        setError(errorMessage, query.lastError().text());
        return samples;
    }
    while (query.next()) samples.push_back(sampleFromQuery(query));
    if (errorMessage) errorMessage->clear();
    return samples;
}

bool EvaluationStore::saveRun(EvaluationRun run, QString *savedId, QString *errorMessage)
{
    if (!ready_) {
        setError(errorMessage, QStringLiteral("评估库尚未初始化。"));
        return false;
    }
    if (run.status != QStringLiteral("succeeded") && run.status != QStringLiteral("failed")) {
        setError(errorMessage, QStringLiteral("评估运行状态无效。"));
        return false;
    }
    if (run.topK < 1 || run.topK > 20 || run.sampleCount < 1 || run.samples.size() != run.sampleCount) {
        setError(errorMessage, QStringLiteral("评估运行快照的 top-k 或样本数无效。"));
        return false;
    }
    if (run.status == QStringLiteral("succeeded") && run.result.isEmpty()) {
        setError(errorMessage, QStringLiteral("成功评估运行必须保存结果快照。"));
        return false;
    }
    const QString now = timestamp();
    if (run.id.isEmpty()) run.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    if (run.createdAt.isEmpty()) run.createdAt = now;
    if (run.finishedAt.isEmpty()) run.finishedAt = now;
    QSqlQuery query(database_);
    query.prepare(QStringLiteral(
        "INSERT INTO evaluation_runs (id, created_at, finished_at, status, top_k, sample_count, "
        "samples_json, result_json, error_message) VALUES (:id, :created, :finished, :status, :top_k, "
        ":sample_count, :samples, :result, :error)"));
    query.bindValue(QStringLiteral(":id"), run.id);
    query.bindValue(QStringLiteral(":created"), run.createdAt);
    query.bindValue(QStringLiteral(":finished"), run.finishedAt);
    query.bindValue(QStringLiteral(":status"), run.status);
    query.bindValue(QStringLiteral(":top_k"), run.topK);
    query.bindValue(QStringLiteral(":sample_count"), run.sampleCount);
    query.bindValue(QStringLiteral(":samples"), QJsonDocument(run.samples).toJson(QJsonDocument::Compact));
    query.bindValue(QStringLiteral(":result"), QJsonDocument(run.result).toJson(QJsonDocument::Compact));
    query.bindValue(QStringLiteral(":error"), nonNullText(run.errorMessage));
    if (!query.exec()) {
        setError(errorMessage, query.lastError().text());
        return false;
    }
    if (savedId) *savedId = run.id;
    if (errorMessage) errorMessage->clear();
    return true;
}

QVector<EvaluationRun> EvaluationStore::listRuns(int limit, QString *errorMessage) const
{
    QVector<EvaluationRun> runs;
    if (!ready_) {
        setError(errorMessage, QStringLiteral("评估库尚未初始化。"));
        return runs;
    }
    const int boundedLimit = qBound(1, limit, 100);
    QSqlQuery query(database_);
    query.prepare(QStringLiteral("SELECT %1 FROM evaluation_runs ORDER BY created_at DESC LIMIT %2")
                      .arg(runColumns())
                      .arg(boundedLimit));
    if (!query.exec()) {
        setError(errorMessage, query.lastError().text());
        return runs;
    }
    while (query.next()) runs.push_back(runFromQuery(query));
    if (errorMessage) errorMessage->clear();
    return runs;
}

QString EvaluationStore::databasePath() const
{
    return databasePath_;
}

bool EvaluationStore::isReady() const
{
    return ready_;
}

EvaluationCandidate EvaluationStore::candidateFromQuery(const QSqlQuery &query)
{
    EvaluationCandidate candidate;
    candidate.id = query.value(QStringLiteral("id")).toString();
    candidate.originRecordId = query.value(QStringLiteral("origin_record_id")).toString();
    candidate.createdAt = query.value(QStringLiteral("created_at")).toString();
    candidate.updatedAt = query.value(QStringLiteral("updated_at")).toString();
    candidate.query = query.value(QStringLiteral("query_text")).toString();
    candidate.userFeedback = query.value(QStringLiteral("user_feedback")).toString();
    candidate.reviewStatus = query.value(QStringLiteral("review_status")).toString();
    candidate.expectedEvidence = parseRefs(query.value(QStringLiteral("expected_evidence_json")).toByteArray());
    return candidate;
}

ApprovedEvaluationSample EvaluationStore::sampleFromQuery(const QSqlQuery &query)
{
    ApprovedEvaluationSample sample;
    sample.id = query.value(QStringLiteral("id")).toString();
    sample.candidateId = query.value(QStringLiteral("candidate_id")).toString();
    sample.approvedAt = query.value(QStringLiteral("approved_at")).toString();
    sample.query = query.value(QStringLiteral("query_text")).toString();
    sample.expectedEvidence = parseRefs(query.value(QStringLiteral("expected_evidence_json")).toByteArray());
    sample.annotationGranularity = query.value(QStringLiteral("annotation_granularity")).toString();
    return sample;
}

EvaluationRun EvaluationStore::runFromQuery(const QSqlQuery &query)
{
    EvaluationRun run;
    run.id = query.value(QStringLiteral("id")).toString();
    run.createdAt = query.value(QStringLiteral("created_at")).toString();
    run.finishedAt = query.value(QStringLiteral("finished_at")).toString();
    run.status = query.value(QStringLiteral("status")).toString();
    run.topK = query.value(QStringLiteral("top_k")).toInt();
    run.sampleCount = query.value(QStringLiteral("sample_count")).toInt();
    run.samples = QJsonDocument::fromJson(query.value(QStringLiteral("samples_json")).toByteArray()).array();
    run.result = QJsonDocument::fromJson(query.value(QStringLiteral("result_json")).toByteArray()).object();
    run.errorMessage = query.value(QStringLiteral("error_message")).toString();
    return run;
}

QVector<ExpectedEvidenceRef> EvaluationStore::evidenceRefsFromResult(const QJsonObject &result)
{
    QVector<ExpectedEvidenceRef> refs;
    const QJsonArray evidence = result.value(QStringLiteral("evidence")).toArray();
    refs.reserve(evidence.size());
    for (const QJsonValue &value : evidence) {
        const QJsonObject evidenceObject = value.toObject();
        const QJsonObject metadata = evidenceObject.value(QStringLiteral("metadata")).toObject();
        ExpectedEvidenceRef ref;
        ref.source = evidenceObject.value(QStringLiteral("source")).toString();
        ref.chunkId = evidenceObject.value(QStringLiteral("chunk_id")).toString();
        ref.docId = metadata.value(QStringLiteral("doc_id")).toVariant().toString();
        ref.page = metadata.value(QStringLiteral("page")).toVariant().toString();
        ref.section = metadata.value(QStringLiteral("section")).toString();
        if (ref.isUsable()) refs.push_back(ref);
    }
    return refs;
}

QString EvaluationStore::annotationGranularity(const QVector<ExpectedEvidenceRef> &refs)
{
    QString value;
    for (const ExpectedEvidenceRef &ref : refs) {
        const QString current = ref.granularity();
        if (value.isEmpty()) value = current;
        else if (value != current) return QStringLiteral("mixed");
    }
    return value.isEmpty() ? QStringLiteral("unknown") : value;
}

QString EvaluationStore::timestamp()
{
    return QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
}

void EvaluationStore::setError(QString *errorMessage, const QString &message) const
{
    if (errorMessage) *errorMessage = message;
}
