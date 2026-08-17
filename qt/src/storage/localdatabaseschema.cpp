#include "storage/localdatabaseschema.h"

#include <QSqlError>
#include <QSqlQuery>

namespace {
QString diagnosisTableSql()
{
    return QStringLiteral(
        "CREATE TABLE diagnosis_cases ("
        "id TEXT PRIMARY KEY, created_at TEXT NOT NULL, updated_at TEXT NOT NULL, "
        "mode TEXT NOT NULL CHECK(mode IN ('expert', 'hardfault', 'protocol_log')), "
        "input_text TEXT NOT NULL, query_text TEXT NOT NULL, answer TEXT NOT NULL, "
        "grounded INTEGER NOT NULL, refusal_reason TEXT NOT NULL DEFAULT '', "
        "result_json TEXT NOT NULL, report_path TEXT NOT NULL DEFAULT '', "
        "note TEXT NOT NULL DEFAULT '', favorite INTEGER NOT NULL DEFAULT 0, "
        "embedding TEXT NOT NULL DEFAULT '', reranker TEXT NOT NULL DEFAULT '', "
        "retrieval_ms REAL NOT NULL DEFAULT 0)");
}

QString candidateTableSql()
{
    return QStringLiteral(
        "CREATE TABLE evaluation_candidates ("
        "id TEXT PRIMARY KEY, origin_record_id TEXT NOT NULL DEFAULT '', "
        "created_at TEXT NOT NULL, updated_at TEXT NOT NULL, query_text TEXT NOT NULL, "
        "user_feedback TEXT NOT NULL CHECK(user_feedback IN ('helpful', 'unhelpful', 'unreviewed')), "
        "review_status TEXT NOT NULL CHECK(review_status IN ('candidate', 'approved', 'rejected')), "
        "expected_evidence_json TEXT NOT NULL DEFAULT '[]')");
}

QString sampleTableSql()
{
    return QStringLiteral(
        "CREATE TABLE evaluation_samples ("
        "id TEXT PRIMARY KEY, candidate_id TEXT NOT NULL, approved_at TEXT NOT NULL, "
        "query_text TEXT NOT NULL, expected_evidence_json TEXT NOT NULL, "
        "annotation_granularity TEXT NOT NULL, active INTEGER NOT NULL DEFAULT 1, "
        "FOREIGN KEY(candidate_id) REFERENCES evaluation_candidates(id))");
}

QString runTableSql()
{
    return QStringLiteral(
        "CREATE TABLE evaluation_runs ("
        "id TEXT PRIMARY KEY, created_at TEXT NOT NULL, finished_at TEXT NOT NULL DEFAULT '', "
        "status TEXT NOT NULL CHECK(status IN ('succeeded', 'failed')), top_k INTEGER NOT NULL, "
        "sample_count INTEGER NOT NULL, samples_json TEXT NOT NULL DEFAULT '[]', "
        "result_json TEXT NOT NULL DEFAULT '{}', error_message TEXT NOT NULL DEFAULT '')");
}

} // namespace

bool LocalDatabaseSchema::createEvaluationTables(QSqlDatabase &database, QString *errorMessage)
{
    return execute(database, candidateTableSql(), errorMessage)
           && execute(database, sampleTableSql(), errorMessage)
           && execute(database, runTableSql(), errorMessage)
           && execute(
               database,
               QStringLiteral("CREATE INDEX idx_evaluation_candidates_status "
                              "ON evaluation_candidates(review_status, updated_at DESC)"),
               errorMessage)
           && execute(
               database,
               QStringLiteral("CREATE UNIQUE INDEX idx_evaluation_samples_active_candidate "
                              "ON evaluation_samples(candidate_id) WHERE active = 1"),
               errorMessage)
           && execute(
               database,
               QStringLiteral("CREATE INDEX idx_evaluation_runs_created ON evaluation_runs(created_at DESC)"),
               errorMessage);
}

bool LocalDatabaseSchema::ensure(QSqlDatabase &database, QString *errorMessage)
{
    if (!database.isOpen()) {
        setError(errorMessage, QStringLiteral("本地数据库尚未打开。"));
        return false;
    }
    const int current = version(database, errorMessage);
    if (current < 0) {
        return false;
    }
    bool ok = false;
    if (current == 0) {
        ok = createSchemaV3(database, errorMessage);
    } else if (current == 1) {
        ok = migrateSchemaV1ToV3(database, errorMessage);
    } else if (current == 2) {
        ok = migrateSchemaV2ToV3(database, errorMessage);
    } else if (current == CurrentVersion) {
        ok = true;
    } else {
        setError(errorMessage, QStringLiteral("本地数据库 schema 版本 %1 高于当前支持的 v%2。")
                                      .arg(current)
                                      .arg(CurrentVersion));
    }
    if (ok && errorMessage) {
        errorMessage->clear();
    }
    return ok;
}

bool LocalDatabaseSchema::createSchemaV3(QSqlDatabase &database, QString *errorMessage)
{
    if (!database.transaction()) {
        setError(errorMessage, database.lastError().text());
        return false;
    }
    const bool created = execute(database, diagnosisTableSql(), errorMessage)
                        && execute(database,
                                   QStringLiteral("CREATE INDEX idx_diagnosis_cases_updated "
                                                  "ON diagnosis_cases(updated_at DESC)"),
                                   errorMessage)
                        && execute(database,
                                   QStringLiteral("CREATE INDEX idx_diagnosis_cases_mode_grounded "
                                                  "ON diagnosis_cases(mode, grounded, favorite)"),
                                   errorMessage)
                        && createEvaluationTables(database, errorMessage)
                        && execute(database, QStringLiteral("PRAGMA user_version = 3"), errorMessage);
    if (!created || !database.commit()) {
        database.rollback();
        if (created) {
            setError(errorMessage, database.lastError().text());
        }
        return false;
    }
    return true;
}

bool LocalDatabaseSchema::migrateSchemaV1ToV3(QSqlDatabase &database, QString *errorMessage)
{
    if (!database.transaction()) {
        setError(errorMessage, database.lastError().text());
        return false;
    }
    const QString columns = QStringLiteral(
        "id, created_at, updated_at, mode, input_text, query_text, answer, grounded, "
        "refusal_reason, result_json, report_path, note, favorite, embedding, reranker, retrieval_ms");
    const bool migrated = execute(database, QStringLiteral("ALTER TABLE diagnosis_cases RENAME TO diagnosis_cases_v1"), errorMessage)
                          && execute(database, diagnosisTableSql(), errorMessage)
                          && execute(database,
                                     QStringLiteral("INSERT INTO diagnosis_cases (%1) SELECT %1 FROM diagnosis_cases_v1")
                                         .arg(columns),
                                     errorMessage)
                          && execute(database, QStringLiteral("DROP TABLE diagnosis_cases_v1"), errorMessage)
                          && execute(database,
                                     QStringLiteral("CREATE INDEX idx_diagnosis_cases_updated "
                                                    "ON diagnosis_cases(updated_at DESC)"),
                                     errorMessage)
                          && execute(database,
                                     QStringLiteral("CREATE INDEX idx_diagnosis_cases_mode_grounded "
                                                    "ON diagnosis_cases(mode, grounded, favorite)"),
                                     errorMessage)
                          && createEvaluationTables(database, errorMessage)
                          && execute(database, QStringLiteral("PRAGMA user_version = 3"), errorMessage);
    if (!migrated || !database.commit()) {
        database.rollback();
        if (migrated) {
            setError(errorMessage, database.lastError().text());
        }
        return false;
    }
    return true;
}

bool LocalDatabaseSchema::migrateSchemaV2ToV3(QSqlDatabase &database, QString *errorMessage)
{
    if (!database.transaction()) {
        setError(errorMessage, database.lastError().text());
        return false;
    }
    const bool migrated = createEvaluationTables(database, errorMessage)
                          && execute(database, QStringLiteral("PRAGMA user_version = 3"), errorMessage);
    if (!migrated || !database.commit()) {
        database.rollback();
        if (migrated) {
            setError(errorMessage, database.lastError().text());
        }
        return false;
    }
    return true;
}

bool LocalDatabaseSchema::execute(QSqlDatabase &database, const QString &sql, QString *errorMessage)
{
    QSqlQuery query(database);
    if (!query.exec(sql)) {
        setError(errorMessage, query.lastError().text());
        return false;
    }
    return true;
}

int LocalDatabaseSchema::version(QSqlDatabase &database, QString *errorMessage)
{
    QSqlQuery query(database);
    if (!query.exec(QStringLiteral("PRAGMA user_version")) || !query.next()) {
        setError(errorMessage, query.lastError().text());
        return -1;
    }
    return query.value(0).toInt();
}

void LocalDatabaseSchema::setError(QString *errorMessage, const QString &message)
{
    if (errorMessage) {
        *errorMessage = message;
    }
}
