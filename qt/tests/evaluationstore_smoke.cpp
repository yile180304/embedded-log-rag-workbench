#include "evaluation/evaluationstore.h"

#include <QCoreApplication>
#include <QDir>
#include <QJsonArray>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QTextStream>

namespace {
CaseRecord makeCase(const QString &id, bool withEvidence)
{
    CaseRecord record;
    record.id = id;
    record.query = QStringLiteral("ETH_DMASR 的 EBS 表示什么？");
    record.inputText = record.query;
    QJsonArray evidence;
    if (withEvidence) {
        evidence.append(QJsonObject{
            {QStringLiteral("source"), QStringLiteral("RM0090.pdf")},
            {QStringLiteral("chunk_id"), QStringLiteral("rm0090-eth-dmasr")},
            {QStringLiteral("metadata"), QJsonObject{
                 {QStringLiteral("doc_id"), QStringLiteral("RM0090")},
                 {QStringLiteral("page"), 1214},
                 {QStringLiteral("section"), QStringLiteral("ETH_DMASR")},
             }},
        });
    }
    record.resultJson = {{QStringLiteral("evidence"), evidence}};
    return record;
}

bool setFailingSampleTrigger(const QString &databasePath, bool enabled, QString *errorMessage)
{
    const QString connectionName = QStringLiteral("evaluation-store-trigger");
    bool ok = false;
    {
        QSqlDatabase database = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connectionName);
        database.setDatabaseName(databasePath);
        if (!database.open()) {
            *errorMessage = database.lastError().text();
        } else {
            QSqlQuery query(database);
            const QString sql = enabled
                                    ? QStringLiteral("CREATE TRIGGER fail_evaluation_sample BEFORE INSERT ON evaluation_samples "
                                                     "BEGIN SELECT RAISE(ABORT, 'forced sample failure'); END")
                                    : QStringLiteral("DROP TRIGGER fail_evaluation_sample");
            ok = query.exec(sql);
            if (!ok) *errorMessage = query.lastError().text();
            database.close();
        }
    }
    QSqlDatabase::removeDatabase(connectionName);
    return ok;
}
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QTextStream output(stdout);
    QTemporaryDir temp;
    if (!temp.isValid()) return 2;
    const QString databasePath = QDir(temp.path()).filePath(QStringLiteral("evaluation.sqlite3"));
    EvaluationStore store(databasePath);
    QString error;
    if (!store.initialize(&error)) {
        output << "initialize failed: " << error << '\n';
        return 3;
    }

    EvaluationCandidate candidate;
    if (!store.createOrGetFromCase(makeCase(QStringLiteral("case-1"), true),
                                   QStringLiteral("unhelpful"), &candidate, &error)
        || candidate.expectedEvidence.size() != 1
        || candidate.expectedEvidence.front().page != QStringLiteral("1214")) {
        output << "candidate create failed: " << error << '\n';
        return 4;
    }
    EvaluationCandidate duplicate;
    if (!store.createOrGetFromCase(makeCase(QStringLiteral("case-1"), true),
                                   QStringLiteral("helpful"), &duplicate, &error)
        || duplicate.id != candidate.id || duplicate.userFeedback != QStringLiteral("helpful")
        || store.listCandidates({}, &error).size() != 1) {
        output << "candidate idempotency failed: " << error << '\n';
        return 5;
    }

    EvaluationCandidate incomplete;
    if (!store.createOrGetFromCase(makeCase(QStringLiteral("case-2"), false),
                                   QStringLiteral("unhelpful"), &incomplete, &error)) {
        output << "incomplete candidate create failed: " << error << '\n';
        return 6;
    }
    ApprovedEvaluationSample sample;
    if (store.approveCandidate(incomplete, &sample, &error)) {
        output << "candidate without evidence was approved\n";
        return 7;
    }
    EvaluationCandidate stillCandidate;
    if (!store.getCandidate(incomplete.id, &stillCandidate, &error)
        || stillCandidate.reviewStatus != QStringLiteral("candidate")) {
        output << "validation failure changed candidate status\n";
        return 8;
    }

    if (!setFailingSampleTrigger(databasePath, true, &error)) {
        output << "trigger setup failed: " << error << '\n';
        return 9;
    }
    if (store.approveCandidate(duplicate, &sample, &error)) {
        output << "forced transaction unexpectedly succeeded\n";
        return 10;
    }
    EvaluationCandidate rolledBack;
    if (!store.getCandidate(duplicate.id, &rolledBack, &error)
        || rolledBack.reviewStatus != QStringLiteral("candidate")) {
        output << "approval transaction did not roll back\n";
        return 11;
    }
    if (!setFailingSampleTrigger(databasePath, false, &error)
        || !store.approveCandidate(rolledBack, &sample, &error)
        || sample.annotationGranularity != QStringLiteral("chunk")) {
        output << "approval failed: " << error << '\n';
        return 12;
    }
    ApprovedEvaluationSample sameSample;
    if (!store.approveCandidate(rolledBack, &sameSample, &error)
        || sameSample.id != sample.id || store.listApprovedSamples(&error).size() != 1) {
        output << "approved sample idempotency failed: " << error << '\n';
        return 13;
    }
    if (!store.rejectCandidate(incomplete.id, &error)
        || store.listCandidates(QStringLiteral("rejected"), &error).size() != 1) {
        output << "reject failed: " << error << '\n';
        return 14;
    }
    EvaluationCandidate imported;
    imported.query = QStringLiteral("导入的旧评估问题");
    imported.reviewStatus = QStringLiteral("approved");
    imported.expectedEvidence = {{QStringLiteral("legacy.md"), {}, {}, {}, {}}};
    int importedCount = 0;
    if (!store.importCandidates({imported}, &importedCount, &error) || importedCount != 1
        || store.listCandidates(QStringLiteral("candidate"), &error).size() != 1
        || store.listApprovedSamples(&error).size() != 1) {
        output << "candidate-only import failed: " << error << '\n';
        return 19;
    }

    const QJsonArray sampleSnapshot{sample.toJson()};
    EvaluationRun succeeded;
    succeeded.status = QStringLiteral("succeeded");
    succeeded.topK = 5;
    succeeded.sampleCount = 1;
    succeeded.samples = sampleSnapshot;
    succeeded.result = {
        {QStringLiteral("sample_count"), 1},
        {QStringLiteral("top_k"), 5},
        {QStringLiteral("hit_rate@5"), 1.0},
        {QStringLiteral("recall@5"), 1.0},
        {QStringLiteral("mrr"), 1.0},
        {QStringLiteral("duration_ms"), 12.5},
        {QStringLiteral("runtime_snapshot"), QJsonObject{{QStringLiteral("index"), QJsonObject{
                                                              {QStringLiteral("chunks"), 42},
                                                              {QStringLiteral("updated_at"), QStringLiteral("2026-08-15T00:00:00Z")},
                                                          }}}},
    };
    QString succeededId;
    if (!store.saveRun(succeeded, &succeededId, &error) || succeededId.isEmpty()) {
        output << "succeeded run save failed: " << error << '\n';
        return 15;
    }
    EvaluationRun failed;
    failed.status = QStringLiteral("failed");
    failed.topK = 5;
    failed.sampleCount = 1;
    failed.samples = sampleSnapshot;
    failed.errorMessage = QStringLiteral("forced evaluation failure");
    if (!store.saveRun(failed, nullptr, &error)) {
        output << "failed run save failed: " << error << '\n';
        return 16;
    }
    EvaluationStore reopened(databasePath);
    if (!reopened.initialize(&error)) {
        output << "reopen failed: " << error << '\n';
        return 17;
    }
    const QVector<EvaluationRun> runs = reopened.listRuns(10, &error);
    if (runs.size() != 2 || runs.front().status != QStringLiteral("failed")
        || runs.back().id != succeededId || runs.back().samples.size() != 1
        || runs.back().result.value(QStringLiteral("runtime_snapshot")).toObject().isEmpty()) {
        output << "run history reload failed: " << error << '\n';
        return 18;
    }

    output << "EVALUATION_STORE_SMOKE_OK\n";
    return 0;
}
