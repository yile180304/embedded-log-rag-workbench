#include "cases/casestore.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QSet>
#include <QTemporaryDir>
#include <QTextStream>

#include <algorithm>

namespace {
CaseRecord makeRecord(const QString &mode, const QString &query, bool grounded)
{
    CaseRecord record;
    record.mode = mode;
    if (mode == QStringLiteral("hardfault")) {
        record.inputText = QStringLiteral("CFSR=0x82");
    } else if (mode == QStringLiteral("protocol_log")) {
        record.inputText = QStringLiteral("UDP seq=40 len=64\nUDP seq=42 len=64");
    } else {
        record.inputText = query;
    }
    record.query = query;
    record.answer = grounded ? QStringLiteral("有手册证据的结论") : QStringLiteral("无法根据当前知识库确认");
    record.grounded = grounded;
    record.refusalReason = grounded ? QString() : QStringLiteral("no_evidence");
    record.resultJson = {
        {QStringLiteral("query"), query},
        {QStringLiteral("answer"), record.answer},
        {QStringLiteral("grounded"), grounded},
        {QStringLiteral("evidence"), QJsonArray{}},
    };
    record.embedding = QStringLiteral("test-embedding");
    record.reranker = QStringLiteral("test-reranker");
    record.retrievalMs = 12.5;
    return record;
}

bool createV1Database(const QString &databasePath, QString *errorMessage)
{
    const QString connectionName = QStringLiteral("case-store-v1-seed");
    {
        QSqlDatabase database = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connectionName);
        database.setDatabaseName(databasePath);
        if (!database.open()) {
            *errorMessage = database.lastError().text();
            return false;
        }
        QSqlQuery query(database);
        const QStringList statements = {
            QStringLiteral(
                "CREATE TABLE diagnosis_cases ("
                "id TEXT PRIMARY KEY, created_at TEXT NOT NULL, updated_at TEXT NOT NULL, "
                "mode TEXT NOT NULL CHECK(mode IN ('expert', 'hardfault')), "
                "input_text TEXT NOT NULL, query_text TEXT NOT NULL, answer TEXT NOT NULL, "
                "grounded INTEGER NOT NULL, refusal_reason TEXT NOT NULL DEFAULT '', "
                "result_json TEXT NOT NULL, report_path TEXT NOT NULL DEFAULT '', "
                "note TEXT NOT NULL DEFAULT '', favorite INTEGER NOT NULL DEFAULT 0, "
                "embedding TEXT NOT NULL DEFAULT '', reranker TEXT NOT NULL DEFAULT '', retrieval_ms REAL NOT NULL DEFAULT 0)"),
            QStringLiteral(
                "INSERT INTO diagnosis_cases VALUES ("
                "'legacy-expert','2026-08-14T00:00:00.000Z','2026-08-14T00:00:00.000Z','expert',"
                "'CFSR 是什么？','CFSR 是什么？','旧案例结论',1,'',"
                "'{\"query\":\"CFSR 是什么？\",\"answer\":\"旧案例结论\",\"grounded\":true,\"evidence\":[]}',"
                "'', '旧备注', 1, 'legacy-embedding', 'legacy-reranker', 8.5)"),
            QStringLiteral("PRAGMA user_version = 1"),
        };
        for (const QString &statement : statements) {
            if (!query.exec(statement)) {
                *errorMessage = query.lastError().text();
                database.close();
                return false;
            }
        }
        database.close();
    }
    QSqlDatabase::removeDatabase(connectionName);
    errorMessage->clear();
    return true;
}

bool hasSchemaV3(const QString &databasePath, QString *errorMessage)
{
    const QString connectionName = QStringLiteral("case-store-schema-check");
    bool valid = false;
    {
        QSqlDatabase database = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connectionName);
        database.setDatabaseName(databasePath);
        if (!database.open()) {
            *errorMessage = database.lastError().text();
        } else {
            QSqlQuery version(database);
            QSqlQuery tables(database);
            QSet<QString> names;
            if (version.exec(QStringLiteral("PRAGMA user_version")) && version.next()
                && tables.exec(QStringLiteral(
                    "SELECT name FROM sqlite_master WHERE type='table' AND name LIKE 'evaluation_%'"))) {
                while (tables.next()) {
                    names.insert(tables.value(0).toString());
                }
                valid = version.value(0).toInt() == 3
                        && names == QSet<QString>{QStringLiteral("evaluation_candidates"),
                                                 QStringLiteral("evaluation_samples"),
                                                 QStringLiteral("evaluation_runs")};
            }
            if (!valid && errorMessage->isEmpty()) {
                *errorMessage = QStringLiteral("schema v3 or evaluation tables missing");
            }
            database.close();
        }
    }
    QSqlDatabase::removeDatabase(connectionName);
    return valid;
}
} // namespace

int main(int argc, char *argv[])
{
    QCoreApplication application(argc, argv);
    QTextStream output(stdout);
    QTemporaryDir temporaryDirectory;
    if (!temporaryDirectory.isValid()) {
        output << "temporary directory failed\n";
        return 2;
    }
    const QString databasePath = QDir(temporaryDirectory.path()).filePath(QStringLiteral("data/runtime/cases.sqlite3"));
    QDir().mkpath(QFileInfo(databasePath).absolutePath());
    QString expertId;
    QString hardFaultId;
    QString protocolId;
    QString seedError;
    if (!createV1Database(databasePath, &seedError)) {
        output << "v1 seed failed: " << seedError << '\n';
        return 3;
    }
    {
        CaseStore store(databasePath);
        QString error;
        if (!store.initialize(&error) || !store.isReady()) {
            output << "initialize failed: " << error << '\n';
            return 4;
        }
        CaseRecord legacy;
        if (!store.get(QStringLiteral("legacy-expert"), &legacy, &error)
            || legacy.note != QStringLiteral("旧备注") || !legacy.favorite) {
            output << "v1 migration lost legacy data\n";
            return 5;
        }
        if (!store.save(makeRecord(QStringLiteral("expert"), QStringLiteral("ETH_DMASR EBS"), true), &expertId, &error)
            || !store.save(makeRecord(QStringLiteral("hardfault"), QStringLiteral("CFSR PRECISERR"), false), &hardFaultId, &error)
            || !store.save(makeRecord(QStringLiteral("protocol_log"), QStringLiteral("UDP sequence_gap"), false), &protocolId, &error)) {
            output << "save failed: " << error << '\n';
            return 6;
        }
        CaseFilter filter;
        filter.keyword = QStringLiteral("EBS");
        const QVector<CaseRecord> keywordRecords = store.list(filter, &error);
        if (!error.isEmpty() || keywordRecords.size() != 1 || keywordRecords.front().id != expertId) {
            output << "keyword filter failed\n";
            return 7;
        }
        filter = {};
        filter.mode = QStringLiteral("hardfault");
        filter.grounded = 0;
        const QVector<CaseRecord> hardFaultRecords = store.list(filter, &error);
        if (hardFaultRecords.size() != 1 || hardFaultRecords.front().id != hardFaultId) {
            output << "mode/grounded filter failed\n";
            return 8;
        }
        filter = {};
        filter.mode = QStringLiteral("protocol_log");
        const QVector<CaseRecord> protocolRecords = store.list(filter, &error);
        if (protocolRecords.size() != 1 || protocolRecords.front().id != protocolId
            || !protocolRecords.front().inputText.contains(QStringLiteral("seq=42"))) {
            output << "protocol mode filter failed\n";
            return 9;
        }
        if (!store.updateNote(expertId, QStringLiteral("重点复习"), &error)
            || !store.updateFavorite(expertId, true, &error)) {
            output << "update failed: " << error << '\n';
            return 10;
        }
        CaseRecord updated;
        if (!store.get(expertId, &updated, &error) || updated.note != QStringLiteral("重点复习") || !updated.favorite) {
            output << "get updated record failed\n";
            return 11;
        }
        const QString exportPath = QDir(temporaryDirectory.path()).filePath(QStringLiteral("case.json"));
        if (!store.exportRecord(updated, exportPath, &error)) {
            output << "export failed: " << error << '\n';
            return 12;
        }
        QFile exportFile(exportPath);
        if (!exportFile.open(QIODevice::ReadOnly)) {
            output << "export open failed\n";
            return 13;
        }
        const QJsonObject exportJson = QJsonDocument::fromJson(exportFile.readAll()).object();
        if (exportJson.value(QStringLiteral("case_id")).toString() != expertId
            || !exportJson.value(QStringLiteral("result")).isObject()
            || exportJson.contains(QStringLiteral("api_key"))) {
            output << "export content failed\n";
            return 14;
        }
    }
    {
        CaseStore reopened(databasePath);
        QString error;
        if (!reopened.initialize(&error)) {
            output << "reopen initialize failed: " << error << '\n';
            return 15;
        }
        CaseFilter favoriteFilter;
        favoriteFilter.favoriteOnly = true;
        const QVector<CaseRecord> records = reopened.list(favoriteFilter, &error);
        if (records.size() != 2
            || !std::any_of(records.cbegin(), records.cend(), [&](const CaseRecord &record) {
                   return record.id == expertId && record.note == QStringLiteral("重点复习");
               })
            || !std::any_of(records.cbegin(), records.cend(), [](const CaseRecord &record) {
                   return record.id == QStringLiteral("legacy-expert") && record.note == QStringLiteral("旧备注");
               })) {
            output << "cross-connection persistence failed\n";
            return 16;
        }
    }
    QString schemaError;
    if (!hasSchemaV3(databasePath, &schemaError)) {
        output << "schema v3 failed: " << schemaError << '\n';
        return 17;
    }
    output << "CASE_STORE_SMOKE_OK\n";
    return 0;
}
