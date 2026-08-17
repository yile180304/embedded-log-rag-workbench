#pragma once

#include <QString>
#include <QVector>

class QSettings;

struct RuntimeSettings
{
    QString projectRoot;
    QString pythonExecutable;
    QString embeddingModel = QStringLiteral("BAAI/bge-small-zh-v1.5");
    QString rerankerModel = QStringLiteral("BAAI/bge-reranker-base");
    int defaultTopK = 5;
    int taskTimeoutSeconds = 180;

    static RuntimeSettings defaults(const QString &applicationDirectory);
    bool operator==(const RuntimeSettings &other) const;
    bool operator!=(const RuntimeSettings &other) const;
};

enum class HealthSeverity
{
    Warning,
    Error
};

struct HealthIssue
{
    HealthSeverity severity = HealthSeverity::Error;
    QString field;
    QString summary;
    QString action;
};

class RuntimeSettingsStore final
{
public:
    explicit RuntimeSettingsStore(QSettings &settings);

    RuntimeSettings load(const RuntimeSettings &defaults) const;
    void save(const RuntimeSettings &settings);
    void clear();

private:
    QSettings &settings_;
};

QVector<HealthIssue> runRuntimePreflight(const RuntimeSettings &settings);
bool hasPreflightErrors(const QVector<HealthIssue> &issues);
