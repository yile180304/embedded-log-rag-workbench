#include "cases/casestore.h"
#include "diagnosticworkbench.h"
#include "evaluation/evaluationstore.h"
#include "runtime/runtimesettings.h"

#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QLabel>
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
        if (button->property("testId").toString() == testId) return button;
    }
    return nullptr;
}

bool waitFor(const std::function<bool()> &condition, int timeoutMs)
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

def emit(request_id, event, data):
    print(json.dumps({"protocol_version": 1, "request_id": request_id, "event": event, "data": data}), flush=True)

def health():
    return {"worker": "ready", "python_version": "3.12.0", "project_root": "fake", "index": {"ready": True, "chunks": 12, "updated_at": "2026-08-15T00:00:00Z"}, "models": {"embedding": {"name": "fake-embedding", "loaded": True}, "reranker": {"name": "fake-reranker", "loaded": True}}, "active_request_id": None, "sources": []}

emit("worker-startup", "result", health())
for raw in sys.stdin:
    request = json.loads(raw)
    request_id = request["request_id"]
    operation = request["operation"]
    if operation == "evaluate":
        samples = request["payload"]["samples"]
        top_k = request["options"]["top_k"]
        emit(request_id, "accepted", {"operation": operation})
        emit(request_id, "progress", {"completed": 1, "total": len(samples), "current_query": samples[0]["query"]})
        emit(request_id, "result", {"sample_count": len(samples), "top_k": top_k, "hit_rate@5": 1.0, "recall@5": 1.0, "mrr": 1.0, "duration_ms": 8.5, "details": [{"sample_id": samples[0]["sample_id"], "query": samples[0]["query"], "hit": True, "hit_ranks": [1]}], "failed_cases": [], "runtime_snapshot": health()})
    elif operation == "health":
        emit(request_id, "result", health())
    elif operation == "shutdown":
        emit(request_id, "result", {"status": "shutting_down"})
        break
)PY";
}
}

int main(int argc, char *argv[])
{
    QApplication application(argc, argv);
    application.setOrganizationName(QStringLiteral("EvaluationWorkbenchSmoke"));
    application.setApplicationName(QStringLiteral("EvaluationWorkbenchSmoke"));
    QTextStream output(stdout);
    QTemporaryDir temporaryDirectory;
    if (!temporaryDirectory.isValid()) return 2;
    const QString root = temporaryDirectory.path();
    QDir(root).mkpath(QStringLiteral("rag_diagnostic"));
    QFile projectFile(QDir(root).filePath(QStringLiteral("pyproject.toml")));
    QFile packageFile(QDir(root).filePath(QStringLiteral("rag_diagnostic/__init__.py")));
    QFile workerFile(QDir(root).filePath(QStringLiteral("rag_diagnostic/__main__.py")));
    if (!projectFile.open(QIODevice::WriteOnly) || projectFile.write("[project]\nname='fake'\n") < 1
        || !packageFile.open(QIODevice::WriteOnly) || packageFile.write("\n") < 1
        || !workerFile.open(QIODevice::WriteOnly) || workerFile.write(fakeWorkerSource()) < 1) {
        return 3;
    }
    projectFile.close();
    packageFile.close();
    workerFile.close();

    const QString databasePath = QDir(root).filePath(QStringLiteral("data/runtime/diagnosis_cases.sqlite3"));
    {
        CaseStore store(databasePath);
        QString error;
        if (!store.initialize(&error)) return 4;
        CaseRecord record;
        record.mode = QStringLiteral("expert");
        record.inputText = QStringLiteral("ETH_DMASR 的 EBS 表示什么？");
        record.query = record.inputText;
        record.answer = QStringLiteral("检查 EBS 字段。");
        record.grounded = true;
        record.resultJson = {
            {QStringLiteral("query"), record.query},
            {QStringLiteral("answer"), record.answer},
            {QStringLiteral("grounded"), true},
            {QStringLiteral("evidence"), QJsonArray{QJsonObject{
                 {QStringLiteral("source"), QStringLiteral("RM0090.pdf")},
                 {QStringLiteral("chunk_id"), QStringLiteral("eth-dmasr")},
                 {QStringLiteral("metadata"), QJsonObject{
                      {QStringLiteral("doc_id"), QStringLiteral("RM0090")},
                      {QStringLiteral("page"), 1214},
                      {QStringLiteral("section"), QStringLiteral("ETH_DMASR")},
                  }},
             }}},
        };
        if (!store.save(record, nullptr, &error)) return 5;
    }

    const QStringList arguments = QCoreApplication::arguments();
    const QString realProjectRoot = arguments.size() > 1 ? arguments.at(1) : QString();
    RuntimeSettings settings;
    settings.projectRoot = root;
    settings.pythonExecutable = QDir(realProjectRoot).filePath(QStringLiteral(".venv312/Scripts/python.exe"));
    settings.embeddingModel = QStringLiteral("fake-embedding");
    settings.rerankerModel = QStringLiteral("fake-reranker");
    settings.defaultTopK = 5;
    settings.taskTimeoutSeconds = 60;
    QSettings persistentSettings;
    RuntimeSettingsStore(persistentSettings).save(settings);

    {
        DiagnosticWorkbench window;
        window.show();
        auto *caseNavigation = findButton(window, QStringLiteral("caseLibraryNavigation"));
        if (!caseNavigation || !waitFor([&] { return findButton(window, QStringLiteral("caseCandidate")); }, 3000)) return 6;
        caseNavigation->click();
        auto *caseTable = window.findChild<QTableWidget *>(QStringLiteral("caseRecordsTable"));
        auto *candidateButton = findButton(window, QStringLiteral("caseCandidate"));
        if (!caseTable || !candidateButton || !waitFor([&] { return caseTable->rowCount() == 1; }, 1000)) return 7;
        caseTable->selectRow(0);
        QApplication::processEvents();
        candidateButton->click();

        auto *candidateTable = window.findChild<QTableWidget *>(QStringLiteral("evaluationCandidateTable"));
        auto *approveButton = findButton(window, QStringLiteral("evaluationApprove"));
        auto *runButton = findButton(window, QStringLiteral("evaluationRun"));
        if (!candidateTable || !approveButton || !runButton
            || !waitFor([&] { return candidateTable->rowCount() == 1; }, 1000)) return 8;
        approveButton->click();
        if (!waitFor([&] { return runButton->isEnabled(); }, 3000)) return 9;
        runButton->click();
        // 指标改成 metric tiles 后，数值和标注分属两个标签，
        // 这里按稳定选择器断言，不再匹配拼接出来的界面文案。
        if (!waitFor([&] {
                auto *hitRate = window.findChild<QLabel *>(QStringLiteral("metricHitRate"));
                return hitRate && hitRate->text() == QStringLiteral("100.0%");
            }, 3000)) return 10;
    }

    EvaluationStore reopened(databasePath);
    QString error;
    if (!reopened.initialize(&error)) return 11;
    const QVector<EvaluationRun> runs = reopened.listRuns(1, &error);
    if (runs.size() != 1 || runs.front().status != QStringLiteral("succeeded")
        || runs.front().samples.size() != 1 || runs.front().result.value(QStringLiteral("mrr")).toDouble() != 1.0) {
        output << "evaluation run history missing: " << error << '\n';
        return 12;
    }
    persistentSettings.clear();
    output << "WORKBENCH_EVALUATION_SMOKE_OK\n";
    return 0;
}
