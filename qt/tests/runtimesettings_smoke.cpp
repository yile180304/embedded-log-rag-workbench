#include "runtime/runtimesettings.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QSettings>
#include <QTemporaryDir>
#include <QTextStream>

int main(int argc, char *argv[])
{
    QCoreApplication application(argc, argv);
    QTextStream output(stdout);
    if (application.arguments().size() < 2) {
        output << "usage: RuntimeSettingsSmoke <project-root>\n";
        return 2;
    }

    const QString root = QFileInfo(application.arguments().at(1)).absoluteFilePath();
    QTemporaryDir temporaryDirectory;
    if (!temporaryDirectory.isValid()) {
        output << "temporary directory failed\n";
        return 3;
    }
    const QString settingsPath = QDir(temporaryDirectory.path()).filePath(QStringLiteral("runtime-settings.ini"));
    QSettings isolatedSettings(settingsPath, QSettings::IniFormat);
    RuntimeSettingsStore store(isolatedSettings);
    const RuntimeSettings defaults = RuntimeSettings::defaults(root);
    if (store.load(defaults) != defaults || hasPreflightErrors(runRuntimePreflight(defaults))) {
        output << "default settings failed\n";
        return 4;
    }

    RuntimeSettings customized = defaults;
    customized.embeddingModel = QStringLiteral("local/embedding");
    customized.rerankerModel = QStringLiteral("local/reranker");
    customized.defaultTopK = 20;
    customized.taskTimeoutSeconds = 3600;
    store.save(customized);
    if (store.load(defaults) != customized) {
        output << "settings round-trip failed\n";
        return 5;
    }

    const QByteArray previousRootOverride = qgetenv("RAG_DIAGNOSTIC_PROJECT_ROOT");
    const QByteArray previousPythonOverride = qgetenv("RAG_DIAGNOSTIC_PYTHON_EXECUTABLE");
    RuntimeSettings stalePaths = customized;
    stalePaths.projectRoot = QDir(root).filePath(QStringLiteral("stale-project"));
    stalePaths.pythonExecutable = QDir(root).filePath(QStringLiteral("stale-python.exe"));
    store.save(stalePaths);
    const QString environmentRoot = QDir(temporaryDirectory.path()).filePath(QStringLiteral("portable-root"));
    const QString environmentPython = QDir(environmentRoot).filePath(QStringLiteral("python.exe"));
    qputenv("RAG_DIAGNOSTIC_PROJECT_ROOT", environmentRoot.toLocal8Bit());
    qputenv("RAG_DIAGNOSTIC_PYTHON_EXECUTABLE", environmentPython.toLocal8Bit());
    RuntimeSettings environmentExpected = customized;
    environmentExpected.projectRoot = QDir::cleanPath(QFileInfo(environmentRoot).absoluteFilePath());
    environmentExpected.pythonExecutable = QDir::cleanPath(QFileInfo(environmentPython).absoluteFilePath());
    if (store.load(defaults) != environmentExpected) {
        const RuntimeSettings actual = store.load(defaults);
        output << "environment override failed\n"
               << "actual root=" << actual.projectRoot << "\n"
               << "expected root=" << environmentExpected.projectRoot << "\n"
               << "actual python=" << actual.pythonExecutable << "\n"
               << "expected python=" << environmentExpected.pythonExecutable << "\n"
               << "actual topK=" << actual.defaultTopK << " expected topK="
               << environmentExpected.defaultTopK << "\n";
        return 9;
    }
    if (previousRootOverride.isNull()) {
        qunsetenv("RAG_DIAGNOSTIC_PROJECT_ROOT");
    } else {
        qputenv("RAG_DIAGNOSTIC_PROJECT_ROOT", previousRootOverride);
    }
    if (previousPythonOverride.isNull()) {
        qunsetenv("RAG_DIAGNOSTIC_PYTHON_EXECUTABLE");
    } else {
        qputenv("RAG_DIAGNOSTIC_PYTHON_EXECUTABLE", previousPythonOverride);
    }
    store.save(customized);

    RuntimeSettings invalid = customized;
    invalid.projectRoot = QDir(root).filePath(QStringLiteral("missing-project"));
    invalid.pythonExecutable = QDir(root).filePath(QStringLiteral("missing-python.exe"));
    invalid.embeddingModel.clear();
    invalid.rerankerModel.clear();
    invalid.defaultTopK = 0;
    invalid.taskTimeoutSeconds = 29;
    const QVector<HealthIssue> issues = runRuntimePreflight(invalid);
    if (issues.size() != 6 || !hasPreflightErrors(issues)) {
        output << "preflight validation failed\n";
        return 6;
    }

    RuntimeSettings minimum = defaults;
    minimum.defaultTopK = 1;
    minimum.taskTimeoutSeconds = 30;
    if (hasPreflightErrors(runRuntimePreflight(minimum))) {
        output << "minimum boundary failed\n";
        return 7;
    }

    store.clear();
    if (store.load(defaults) != defaults) {
        output << "restore defaults failed\n";
        return 8;
    }
    output << "RUNTIME_SETTINGS_SMOKE_OK\n";
    return 0;
}
