#include "diagnosticworkbench.h"
#include "runtime/runtimesettings.h"

#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSettings>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QTextStream>
#include <QThread>

namespace {
QPushButton *findButton(QWidget &widget, const QString &testId)
{
    for (QPushButton *button : widget.findChildren<QPushButton *>()) {
        if (button->property("testId").toString() == testId) {
            return button;
        }
    }
    return nullptr;
}

bool waitFor(QWidget &widget, const std::function<bool()> &condition, int timeoutMs)
{
    QElapsedTimer timer;
    timer.start();
    while (!condition() && timer.elapsed() < timeoutMs) {
        QApplication::processEvents(QEventLoop::AllEvents, 20);
        QThread::msleep(20);
    }
    QApplication::processEvents();
    return condition();
}

QByteArray fakeWorkerSource()
{
    return R"PY(import json
import sys

SOURCES = [{"source_id": "RM0090", "display_name": "Reference Manual", "path": "E:/docs/RM0090.pdf", "source_type": "pdf", "manifest_status": "allowed", "index_status": "indexed", "chunks": 3, "error": None}]
FAILED_SOURCES = [{"source_id": "RM0090", "display_name": "Reference Manual", "path": "E:/docs/RM0090.pdf", "source_type": "pdf", "manifest_status": "allowed", "index_status": "error", "chunks": 0, "error": "parse failed"}]
health_count = 0
reindex_count = 0

def emit(request_id, event, data):
    print(json.dumps({"protocol_version": 1, "request_id": request_id, "event": event, "data": data}), flush=True)

def health(include_sources=True):
    data = {"worker": "ready", "python_version": "3.12.0", "project_root": "fake", "index": {"ready": True, "chunks": 3, "updated_at": None}, "models": {"embedding": {"name": "fake", "loaded": False}, "reranker": {"name": "fake", "loaded": False}}, "active_request_id": None}
    if include_sources:
        data["sources"] = SOURCES
    return data

emit("worker-startup", "result", health())
for raw in sys.stdin:
    request = json.loads(raw)
    request_id = request["request_id"]
    operation = request["operation"]
    if operation == "health":
        health_count += 1
        emit(request_id, "result", health(include_sources=health_count != 1))
    elif operation == "reindex":
        reindex_count += 1
        emit(request_id, "accepted", {"operation": "reindex"})
        if reindex_count == 1:
            emit(request_id, "result", {"status": "ok", "documents": 1, "chunks": 3, "index_count": 3, "errors": [], "ignored": [], "sources": SOURCES})
        else:
            emit(request_id, "error", {"code": "runtime_unavailable", "message": "STM32F4 索引重建失败。", "errors": [{"source": "RM0090.pdf", "error": "parse failed"}], "ignored": [], "sources": FAILED_SOURCES})
    elif operation == "shutdown":
        emit(request_id, "result", {"status": "shutting_down"})
        break
)PY";
}
} // namespace

int main(int argc, char *argv[])
{
    QApplication application(argc, argv);
    application.setOrganizationName(QStringLiteral("KnowledgeWorkbenchSmoke"));
    application.setApplicationName(QStringLiteral("KnowledgeWorkbenchSmoke"));
    QTextStream output(stdout);
    QTemporaryDir temporaryDirectory;
    if (!temporaryDirectory.isValid()) {
        output << "temporary directory failed\n";
        return 2;
    }
    const QString root = temporaryDirectory.path();
    QDir(root).mkpath(QStringLiteral("rag_diagnostic"));
    QFile projectFile(QDir(root).filePath(QStringLiteral("pyproject.toml")));
    QFile packageFile(QDir(root).filePath(QStringLiteral("rag_diagnostic/__init__.py")));
    QFile workerFile(QDir(root).filePath(QStringLiteral("rag_diagnostic/__main__.py")));
    if (!projectFile.open(QIODevice::WriteOnly) || projectFile.write("[project]\nname='fake'\n") < 1
        || !packageFile.open(QIODevice::WriteOnly) || packageFile.write("\n") < 1
        || !workerFile.open(QIODevice::WriteOnly) || workerFile.write(fakeWorkerSource()) < 1) {
        output << "fake worker fixture failed\n";
        return 3;
    }
    projectFile.close();
    packageFile.close();
    workerFile.close();

    const QStringList arguments = QCoreApplication::arguments();
    const QString realProjectRoot = arguments.size() > 1 ? arguments.at(1) : QString();
    const QString python = QDir(realProjectRoot).filePath(QStringLiteral(".venv312/Scripts/python.exe"));
    RuntimeSettings settings;
    settings.projectRoot = root;
    settings.pythonExecutable = python;
    settings.embeddingModel = QStringLiteral("fake-embedding");
    settings.rerankerModel = QStringLiteral("fake-reranker");
    settings.defaultTopK = 5;
    settings.taskTimeoutSeconds = 60;
    QSettings persistentSettings;
    RuntimeSettingsStore(persistentSettings).save(settings);

    {
        DiagnosticWorkbench window;
        window.show();
        auto *navigation = findButton(window, QStringLiteral("knowledgeNavigation"));
        if (!navigation || !waitFor(window, [&] { return window.findChild<QTableWidget *>(QStringLiteral("knowledgeSourceTable")); }, 3000)) {
            output << "knowledge page did not initialize\n";
            return 4;
        }
        navigation->click();
        QApplication::processEvents();
        auto *table = window.findChild<QTableWidget *>(QStringLiteral("knowledgeSourceTable"));
        auto *refresh = findButton(window, QStringLiteral("knowledgeRefresh"));
        auto *reindex = findButton(window, QStringLiteral("knowledgeReindex"));
        auto *log = window.findChild<QPlainTextEdit *>(QStringLiteral("runtimeActivityLog"));
        if (!table || !refresh || !reindex || !log
            || !waitFor(window, [&] { return table->rowCount() == 1; }, 3000)) {
            output << "knowledge source data missing\n";
            return 5;
        }
        refresh->click();
        if (!waitFor(window, [&] {
                if (table->rowCount() != 0) {
                    return false;
                }
                for (QLabel *label : window.findChildren<QLabel *>()) {
                    if (label->text().contains(QStringLiteral("未提供来源明细"))) {
                        return true;
                    }
                }
                return false;
            }, 1000)) {
            output << "legacy worker compatibility failed\n";
            return 6;
        }
        refresh->click();
        if (!waitFor(window, [&] { return table->rowCount() == 1; }, 1000)) {
            output << "knowledge refresh did not restore sources\n";
            return 7;
        }
        reindex->click();
        if (!waitFor(window, [&] {
                for (QLabel *label : window.findChildren<QLabel *>()) {
                    if (label->text().contains(QStringLiteral("重建完成"))) {
                        return true;
                    }
                }
                return false;
            },
            2000)) {
            output << "knowledge reindex did not complete\n";
            return 8;
        }
        reindex->click();
        if (!waitFor(window, [&] {
                return table->rowCount() == 1
                       && table->item(0, 4)->text() == QStringLiteral("错误")
                       && table->item(0, 7)->text() == QStringLiteral("parse failed");
            },
            2000)) {
            output << "knowledge reindex failure details missing\n";
            return 9;
        }
        const QString logs = log->toPlainText();
        if (logs.count(QStringLiteral("启动长驻 RAG Worker")) != 1 || table->rowCount() != 1) {
            output << "worker was duplicated or sources were lost\n";
            return 10;
        }
    }
    persistentSettings.clear();
    output << "WORKBENCH_KNOWLEDGE_SMOKE_OK\n";
    return 0;
}
