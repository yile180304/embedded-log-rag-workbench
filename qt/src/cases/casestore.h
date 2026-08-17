#pragma once

#include "cases/caserecord.h"

#include <QSqlDatabase>
#include <QString>
#include <QVector>

class CaseStore final
{
public:
    explicit CaseStore(QString databasePath);
    ~CaseStore();

    CaseStore(const CaseStore &) = delete;
    CaseStore &operator=(const CaseStore &) = delete;

    bool initialize(QString *errorMessage = nullptr);
    bool save(CaseRecord record, QString *savedId = nullptr, QString *errorMessage = nullptr);
    QVector<CaseRecord> list(const CaseFilter &filter = {}, QString *errorMessage = nullptr) const;
    bool get(const QString &id, CaseRecord *record, QString *errorMessage = nullptr) const;
    bool updateNote(const QString &id, const QString &note, QString *errorMessage = nullptr);
    bool updateFavorite(const QString &id, bool favorite, QString *errorMessage = nullptr);
    bool exportRecord(const CaseRecord &record, const QString &filePath, QString *errorMessage = nullptr) const;

    QString databasePath() const;
    bool isReady() const;

private:
    bool updateField(const QString &id, const QString &column, const QVariant &value, QString *errorMessage);
    void setError(QString *errorMessage, const QString &message) const;

    QString databasePath_;
    QString connectionName_;
    QSqlDatabase database_;
    bool ready_ = false;
};
