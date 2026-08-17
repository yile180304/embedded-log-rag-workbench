#include "cases/casestore.h"

#include "storage/localdatabaseschema.h"

#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QSaveFile>
#include <QSqlError>
#include <QSqlQuery>
#include <QUuid>
#include <QVariant>

namespace {
QString currentTimestamp()
{
    return QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
}

QString nonNullText(const QString &value)
{
    return value.isNull() ? QStringLiteral("") : value;
}

CaseRecord recordFromQuery(const QSqlQuery &query)
{
    CaseRecord record;
    record.id = query.value(QStringLiteral("id")).toString();
    record.createdAt = query.value(QStringLiteral("created_at")).toString();
    record.updatedAt = query.value(QStringLiteral("updated_at")).toString();
    record.mode = query.value(QStringLiteral("mode")).toString();
    record.inputText = query.value(QStringLiteral("input_text")).toString();
    record.query = query.value(QStringLiteral("query_text")).toString();
    record.answer = query.value(QStringLiteral("answer")).toString();
    record.grounded = query.value(QStringLiteral("grounded")).toBool();
    record.refusalReason = query.value(QStringLiteral("refusal_reason")).toString();
    record.resultJson = QJsonDocument::fromJson(query.value(QStringLiteral("result_json")).toByteArray()).object();
    record.reportPath = query.value(QStringLiteral("report_path")).toString();
    record.note = query.value(QStringLiteral("note")).toString();
    record.favorite = query.value(QStringLiteral("favorite")).toBool();
    record.embedding = query.value(QStringLiteral("embedding")).toString();
    record.reranker = query.value(QStringLiteral("reranker")).toString();
    record.retrievalMs = query.value(QStringLiteral("retrieval_ms")).toDouble();
    return record;
}

QString selectColumns()
{
    return QStringLiteral(
        "id, created_at, updated_at, mode, input_text, query_text, answer, grounded, "
        "refusal_reason, result_json, report_path, note, favorite, embedding, reranker, retrieval_ms");
}
} // namespace

CaseStore::CaseStore(QString databasePath)
    : databasePath_(QDir::cleanPath(std::move(databasePath)))
    , connectionName_(QStringLiteral("case-store-%1").arg(QUuid::createUuid().toString(QUuid::WithoutBraces)))
{
}

CaseStore::~CaseStore()
{
    if (database_.isValid()) {
        database_.close();
    }
    database_ = {};
    QSqlDatabase::removeDatabase(connectionName_);
}

bool CaseStore::initialize(QString *errorMessage)
{
    const QFileInfo databaseFile(databasePath_);
    if (!QDir().mkpath(databaseFile.absolutePath())) {
        setError(errorMessage, QStringLiteral("无法创建案例库目录：%1").arg(databaseFile.absolutePath()));
        return false;
    }
    database_ = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connectionName_);
    database_.setDatabaseName(databasePath_);
    if (!database_.open()) {
        setError(errorMessage, QStringLiteral("无法打开案例库：%1").arg(database_.lastError().text()));
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

bool CaseStore::save(CaseRecord record, QString *savedId, QString *errorMessage)
{
    if (!ready_) {
        setError(errorMessage, QStringLiteral("案例库尚未初始化。"));
        return false;
    }
    if (record.mode != QStringLiteral("expert")
        && record.mode != QStringLiteral("hardfault")
        && record.mode != QStringLiteral("protocol_log")) {
        setError(errorMessage, QStringLiteral("案例模式必须是 expert、hardfault 或 protocol_log。"));
        return false;
    }
    if (record.resultJson.isEmpty()) {
        setError(errorMessage, QStringLiteral("案例缺少可重开的诊断 JSON。"));
        return false;
    }
    if (record.id.isEmpty()) {
        record.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    }
    const QString timestamp = currentTimestamp();
    if (record.createdAt.isEmpty()) {
        record.createdAt = timestamp;
    }
    record.updatedAt = timestamp;

    if (!database_.transaction()) {
        setError(errorMessage, database_.lastError().text());
        return false;
    }
    QSqlQuery query(database_);
    query.prepare(QStringLiteral(
        "INSERT INTO diagnosis_cases (id, created_at, updated_at, mode, input_text, query_text, answer, "
        "grounded, refusal_reason, result_json, report_path, note, favorite, embedding, reranker, retrieval_ms) "
        "VALUES (:id, :created_at, :updated_at, :mode, :input_text, :query_text, :answer, :grounded, "
        ":refusal_reason, :result_json, :report_path, :note, :favorite, :embedding, :reranker, :retrieval_ms)"));
    query.bindValue(QStringLiteral(":id"), record.id);
    query.bindValue(QStringLiteral(":created_at"), record.createdAt);
    query.bindValue(QStringLiteral(":updated_at"), record.updatedAt);
    query.bindValue(QStringLiteral(":mode"), record.mode);
    query.bindValue(QStringLiteral(":input_text"), nonNullText(record.inputText));
    query.bindValue(QStringLiteral(":query_text"), nonNullText(record.query));
    query.bindValue(QStringLiteral(":answer"), nonNullText(record.answer));
    query.bindValue(QStringLiteral(":grounded"), record.grounded ? 1 : 0);
    query.bindValue(QStringLiteral(":refusal_reason"), nonNullText(record.refusalReason));
    query.bindValue(QStringLiteral(":result_json"), QJsonDocument(record.resultJson).toJson(QJsonDocument::Compact));
    query.bindValue(QStringLiteral(":report_path"), nonNullText(record.reportPath));
    query.bindValue(QStringLiteral(":note"), nonNullText(record.note));
    query.bindValue(QStringLiteral(":favorite"), record.favorite ? 1 : 0);
    query.bindValue(QStringLiteral(":embedding"), nonNullText(record.embedding));
    query.bindValue(QStringLiteral(":reranker"), nonNullText(record.reranker));
    query.bindValue(QStringLiteral(":retrieval_ms"), record.retrievalMs);
    if (!query.exec()) {
        database_.rollback();
        setError(errorMessage, query.lastError().text());
        return false;
    }
    if (!database_.commit()) {
        database_.rollback();
        setError(errorMessage, database_.lastError().text());
        return false;
    }
    if (savedId) {
        *savedId = record.id;
    }
    if (errorMessage) {
        errorMessage->clear();
    }
    return true;
}

QVector<CaseRecord> CaseStore::list(const CaseFilter &filter, QString *errorMessage) const
{
    QVector<CaseRecord> records;
    if (!ready_) {
        setError(errorMessage, QStringLiteral("案例库尚未初始化。"));
        return records;
    }
    QString sql = QStringLiteral("SELECT %1 FROM diagnosis_cases WHERE 1=1").arg(selectColumns());
    if (!filter.keyword.trimmed().isEmpty()) {
        sql += QStringLiteral(
            " AND (input_text LIKE :keyword OR query_text LIKE :keyword OR answer LIKE :keyword OR note LIKE :keyword)");
    }
    if (!filter.mode.isEmpty()) {
        sql += QStringLiteral(" AND mode = :mode");
    }
    if (filter.grounded >= 0) {
        sql += QStringLiteral(" AND grounded = :grounded");
    }
    if (filter.favoriteOnly) {
        sql += QStringLiteral(" AND favorite = 1");
    }
    sql += QStringLiteral(" ORDER BY updated_at DESC");

    QSqlQuery query(database_);
    query.prepare(sql);
    if (!filter.keyword.trimmed().isEmpty()) {
        query.bindValue(QStringLiteral(":keyword"),
                        QStringLiteral("%") + filter.keyword.trimmed() + QStringLiteral("%"));
    }
    if (!filter.mode.isEmpty()) {
        query.bindValue(QStringLiteral(":mode"), filter.mode);
    }
    if (filter.grounded >= 0) {
        query.bindValue(QStringLiteral(":grounded"), filter.grounded);
    }
    if (!query.exec()) {
        setError(errorMessage, query.lastError().text());
        return records;
    }
    while (query.next()) {
        records.push_back(recordFromQuery(query));
    }
    if (errorMessage) {
        errorMessage->clear();
    }
    return records;
}

bool CaseStore::get(const QString &id, CaseRecord *record, QString *errorMessage) const
{
    if (!ready_ || !record) {
        setError(errorMessage, QStringLiteral("案例库未就绪或输出参数无效。"));
        return false;
    }
    QSqlQuery query(database_);
    query.prepare(QStringLiteral("SELECT %1 FROM diagnosis_cases WHERE id = :id").arg(selectColumns()));
    query.bindValue(QStringLiteral(":id"), id);
    if (!query.exec()) {
        setError(errorMessage, query.lastError().text());
        return false;
    }
    if (!query.next()) {
        setError(errorMessage, QStringLiteral("案例不存在：%1").arg(id));
        return false;
    }
    *record = recordFromQuery(query);
    if (errorMessage) {
        errorMessage->clear();
    }
    return true;
}

bool CaseStore::updateNote(const QString &id, const QString &note, QString *errorMessage)
{
    return updateField(id, QStringLiteral("note"), note, errorMessage);
}

bool CaseStore::updateFavorite(const QString &id, bool favorite, QString *errorMessage)
{
    return updateField(id, QStringLiteral("favorite"), favorite ? 1 : 0, errorMessage);
}

bool CaseStore::exportRecord(const CaseRecord &record, const QString &filePath, QString *errorMessage) const
{
    QSaveFile file(filePath);
    if (!file.open(QIODevice::WriteOnly)) {
        setError(errorMessage, QStringLiteral("无法创建导出文件：%1").arg(file.errorString()));
        return false;
    }
    const QByteArray content = QJsonDocument(record.toExportJson()).toJson(QJsonDocument::Indented);
    if (file.write(content) != content.size() || !file.commit()) {
        setError(errorMessage, QStringLiteral("无法写入导出文件：%1").arg(file.errorString()));
        return false;
    }
    if (errorMessage) {
        errorMessage->clear();
    }
    return true;
}

QString CaseStore::databasePath() const
{
    return databasePath_;
}

bool CaseStore::isReady() const
{
    return ready_;
}

bool CaseStore::updateField(const QString &id, const QString &column, const QVariant &value, QString *errorMessage)
{
    if (!ready_) {
        setError(errorMessage, QStringLiteral("案例库尚未初始化。"));
        return false;
    }
    if (column != QStringLiteral("note") && column != QStringLiteral("favorite")) {
        setError(errorMessage, QStringLiteral("不允许更新案例字段：%1").arg(column));
        return false;
    }
    if (!database_.transaction()) {
        setError(errorMessage, database_.lastError().text());
        return false;
    }
    QSqlQuery query(database_);
    query.prepare(QStringLiteral("UPDATE diagnosis_cases SET %1 = :value, updated_at = :updated_at WHERE id = :id")
                      .arg(column));
    query.bindValue(QStringLiteral(":value"), value);
    query.bindValue(QStringLiteral(":updated_at"), currentTimestamp());
    query.bindValue(QStringLiteral(":id"), id);
    if (!query.exec() || query.numRowsAffected() != 1) {
        database_.rollback();
        setError(errorMessage, query.lastError().text().isEmpty()
                                   ? QStringLiteral("案例不存在：%1").arg(id)
                                   : query.lastError().text());
        return false;
    }
    if (!database_.commit()) {
        database_.rollback();
        setError(errorMessage, database_.lastError().text());
        return false;
    }
    if (errorMessage) {
        errorMessage->clear();
    }
    return true;
}

void CaseStore::setError(QString *errorMessage, const QString &message) const
{
    if (errorMessage) {
        *errorMessage = message;
    }
}
