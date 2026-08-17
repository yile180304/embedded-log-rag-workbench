#include "runtimesettings.h"

#include "ragprocessbridge.h"

#include <QDir>
#include <QFileInfo>
#include <QProcessEnvironment>
#include <QSettings>

namespace {
constexpr auto settingsPrefix = "runtime/v1/";

QString settingsKey(const char *name)
{
    return QString::fromLatin1(settingsPrefix) + QString::fromLatin1(name);
}

HealthIssue errorIssue(const QString &field, const QString &summary, const QString &action)
{
    return {HealthSeverity::Error, field, summary, action};
}
} // namespace

RuntimeSettings RuntimeSettings::defaults(const QString &applicationDirectory)
{
    const RagRuntimeConfig discovered = RagProcessBridge::discoverRuntime(applicationDirectory);
    return {
        discovered.projectRoot,
        discovered.pythonExecutable,
        discovered.embeddingModel,
        discovered.rerankerModel,
        discovered.defaultTopK,
        discovered.taskTimeoutSeconds,
    };
}

bool RuntimeSettings::operator==(const RuntimeSettings &other) const
{
    return projectRoot == other.projectRoot
           && pythonExecutable == other.pythonExecutable
           && embeddingModel == other.embeddingModel
           && rerankerModel == other.rerankerModel
           && defaultTopK == other.defaultTopK
           && taskTimeoutSeconds == other.taskTimeoutSeconds;
}

bool RuntimeSettings::operator!=(const RuntimeSettings &other) const
{
    return !(*this == other);
}

RuntimeSettingsStore::RuntimeSettingsStore(QSettings &settings)
    : settings_(settings)
{
}

RuntimeSettings RuntimeSettingsStore::load(const RuntimeSettings &defaults) const
{
    RuntimeSettings result = defaults;
    result.projectRoot = settings_.value(settingsKey("project_root"), defaults.projectRoot).toString();
    result.pythonExecutable = settings_.value(settingsKey("python_executable"), defaults.pythonExecutable).toString();
    result.embeddingModel = settings_.value(settingsKey("embedding_model"), defaults.embeddingModel).toString();
    result.rerankerModel = settings_.value(settingsKey("reranker_model"), defaults.rerankerModel).toString();
    result.defaultTopK = settings_.value(settingsKey("default_top_k"), defaults.defaultTopK).toInt();
    result.taskTimeoutSeconds = settings_.value(settingsKey("task_timeout_seconds"), defaults.taskTimeoutSeconds).toInt();

    const QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    const QString projectRootOverride =
        environment.value(QStringLiteral("RAG_DIAGNOSTIC_PROJECT_ROOT")).trimmed();
    const QString pythonOverride =
        environment.value(QStringLiteral("RAG_DIAGNOSTIC_PYTHON_EXECUTABLE")).trimmed();
    if (!projectRootOverride.isEmpty()) {
        result.projectRoot = QDir::cleanPath(QFileInfo(projectRootOverride).absoluteFilePath());
    }
    if (!pythonOverride.isEmpty()) {
        result.pythonExecutable = QDir::cleanPath(QFileInfo(pythonOverride).absoluteFilePath());
    }
    return result;
}

void RuntimeSettingsStore::save(const RuntimeSettings &settings)
{
    settings_.setValue(settingsKey("project_root"), settings.projectRoot);
    settings_.setValue(settingsKey("python_executable"), settings.pythonExecutable);
    settings_.setValue(settingsKey("embedding_model"), settings.embeddingModel);
    settings_.setValue(settingsKey("reranker_model"), settings.rerankerModel);
    settings_.setValue(settingsKey("default_top_k"), settings.defaultTopK);
    settings_.setValue(settingsKey("task_timeout_seconds"), settings.taskTimeoutSeconds);
    settings_.sync();
}

void RuntimeSettingsStore::clear()
{
    settings_.beginGroup(QStringLiteral("runtime/v1"));
    settings_.remove(QString());
    settings_.endGroup();
    settings_.sync();
}

QVector<HealthIssue> runRuntimePreflight(const RuntimeSettings &settings)
{
    QVector<HealthIssue> issues;
    const QString projectRoot = QDir::cleanPath(settings.projectRoot.trimmed());
    const QString pythonExecutable = QDir::cleanPath(settings.pythonExecutable.trimmed());
    if (projectRoot.isEmpty()
        || !QFileInfo(QDir(projectRoot).filePath(QStringLiteral("pyproject.toml"))).isFile()) {
        issues.append(errorIssue(
            QStringLiteral("project_root"),
            QStringLiteral("项目目录中没有 pyproject.toml。"),
            QStringLiteral("请选择 RAG 项目根目录。")));
    }
    if (pythonExecutable.isEmpty() || !QFileInfo(pythonExecutable).isFile()) {
        issues.append(errorIssue(
            QStringLiteral("python_executable"),
            QStringLiteral("Python 解释器不存在。"),
            QStringLiteral("请选择项目 .venv312/Scripts/python.exe。")));
    }
    if (settings.embeddingModel.trimmed().isEmpty()) {
        issues.append(errorIssue(
            QStringLiteral("embedding_model"),
            QStringLiteral("Embedding 模型不能为空。"),
            QStringLiteral("填写 Hugging Face 模型名或本地模型路径。")));
    }
    if (settings.rerankerModel.trimmed().isEmpty()) {
        issues.append(errorIssue(
            QStringLiteral("reranker_model"),
            QStringLiteral("Reranker 模型不能为空。"),
            QStringLiteral("填写 Hugging Face 模型名或本地模型路径。")));
    }
    if (settings.defaultTopK < 1 || settings.defaultTopK > 20) {
        issues.append(errorIssue(
            QStringLiteral("default_top_k"),
            QStringLiteral("默认 top-k 必须在 1 到 20 之间。"),
            QStringLiteral("调整证据条数后再保存。")));
    }
    if (settings.taskTimeoutSeconds < 30 || settings.taskTimeoutSeconds > 3600) {
        issues.append(errorIssue(
            QStringLiteral("task_timeout_seconds"),
            QStringLiteral("任务超时必须在 30 到 3600 秒之间。"),
            QStringLiteral("调整超时设置后再保存。")));
    }
    return issues;
}

bool hasPreflightErrors(const QVector<HealthIssue> &issues)
{
    for (const HealthIssue &issue : issues) {
        if (issue.severity == HealthSeverity::Error) {
            return true;
        }
    }
    return false;
}
