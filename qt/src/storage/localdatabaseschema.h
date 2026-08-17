#pragma once

#include <QSqlDatabase>
#include <QString>

class LocalDatabaseSchema final
{
public:
    static constexpr int CurrentVersion = 3;

    static bool ensure(QSqlDatabase &database, QString *errorMessage = nullptr);

private:
    static bool createSchemaV3(QSqlDatabase &database, QString *errorMessage);
    static bool createEvaluationTables(QSqlDatabase &database, QString *errorMessage);
    static bool migrateSchemaV1ToV3(QSqlDatabase &database, QString *errorMessage);
    static bool migrateSchemaV2ToV3(QSqlDatabase &database, QString *errorMessage);
    static bool execute(QSqlDatabase &database, const QString &sql, QString *errorMessage);
    static int version(QSqlDatabase &database, QString *errorMessage);
    static void setError(QString *errorMessage, const QString &message);
};
