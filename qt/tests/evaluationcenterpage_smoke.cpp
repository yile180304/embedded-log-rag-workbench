#include "workspace/evaluationcenterpage.h"

#include <QApplication>
#include <QDir>
#include <QLabel>
#include <QPushButton>
#include <QTableWidget>

#include <iostream>

namespace {
QPushButton *findButton(QWidget &widget, const QString &testId)
{
    for (QPushButton *button : widget.findChildren<QPushButton *>()) {
        if (button->property("testId").toString() == testId) return button;
    }
    return nullptr;
}
}

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    EvaluationCenterPage page;
    page.setObjectName(QStringLiteral("evaluationCenterPage"));
    page.resize(900, 520);
    page.show();
    app.processEvents();
    EvaluationCandidate candidate;
    candidate.id = QStringLiteral("candidate-1");
    candidate.query = QStringLiteral("CFSR 在哪里定义？");
    candidate.userFeedback = QStringLiteral("unhelpful");
    candidate.expectedEvidence = {{QStringLiteral("core_cm4.h"), {}, {},
                                   QStringLiteral("SCB_Type"), QStringLiteral("core-cfsr")}};
    bool approved = false;
    page.setApproveHandler([&](const EvaluationCandidate &edited) {
        approved = edited.id == candidate.id && edited.expectedEvidence.size() == 1
                   && edited.expectedEvidence.front().source == QStringLiteral("PM0214.pdf")
                   && edited.expectedEvidence.front().docId == QStringLiteral("PM0214")
                   && edited.expectedEvidence.front().page == QStringLiteral("45")
                   && edited.expectedEvidence.front().section == QStringLiteral("Fault types");
        return approved;
    });
    page.setCandidates({candidate});
    page.setApprovedSampleCount(1);
    EvaluationRun latestRun;
    latestRun.status = QStringLiteral("succeeded");
    latestRun.topK = 5;
    latestRun.sampleCount = 3;
    latestRun.result = {
        {QStringLiteral("hit_rate@5"), 2.0 / 3.0},
        {QStringLiteral("recall@5"), 0.5},
        {QStringLiteral("mrr"), 0.5},
    };
    page.setLatestRun(latestRun);
    WorkspaceTaskState taskState;
    taskState.workerState = WorkerState::Ready;
    page.setTaskState(taskState);
    bool runRequested = false;
    page.setRunHandler([&](int topK) {
        runRequested = topK == 5;
        return runRequested;
    });
    const QJsonObject failedCase{
        {QStringLiteral("query"), QStringLiteral("missing query")},
        {QStringLiteral("expected_evidence"), QJsonArray{QJsonObject{{QStringLiteral("chunk_id"), QStringLiteral("missing")}}}},
        {QStringLiteral("retrieved_evidence"), QJsonArray{QJsonObject{{QStringLiteral("chunk_id"), QStringLiteral("actual")}}}},
    };
    page.showEvaluationResult({
        {QStringLiteral("sample_count"), 1},
        {QStringLiteral("top_k"), 5},
        {QStringLiteral("hit_rate@5"), 0.0},
        {QStringLiteral("recall@5"), 0.0},
        {QStringLiteral("mrr"), 0.0},
        {QStringLiteral("duration_ms"), 10.0},
        {QStringLiteral("failed_cases"), QJsonArray{failedCase}},
        {QStringLiteral("runtime_snapshot"), QJsonObject{
             {QStringLiteral("models"), QJsonObject{
                  {QStringLiteral("embedding"), QJsonObject{{QStringLiteral("name"), QStringLiteral("test-embedding")}}},
                  {QStringLiteral("reranker"), QJsonObject{{QStringLiteral("name"), QStringLiteral("test-reranker")}}},
              }},
             {QStringLiteral("index"), QJsonObject{
                  {QStringLiteral("chunks"), 12},
                  {QStringLiteral("updated_at"), QStringLiteral("2026-08-15T00:00:00Z")},
              }},
         }},
    });
    app.processEvents();
    const auto labels = page.findChildren<QLabel *>();
    bool hasTitle = false;
    for (const QLabel *label : labels) {
        hasTitle = hasTitle || label->text() == QStringLiteral("评估中心");
    }
    // 指标改成 metric tiles 后，这里按稳定选择器断言，不再匹配界面文案。
    // 原来断言 label 文本包含 "Hit Rate 0.0%" / "test-embedding"，
    // 那样任何文案或版式调整都会连带弄坏测试，而断言的意图其实是
    // 「跑完评估后指标和运行快照有显示」——用 objectName 表达更准确也更稳。
    auto *hitRateLabel = page.findChild<QLabel *>(QStringLiteral("metricHitRate"));
    auto *recallLabel = page.findChild<QLabel *>(QStringLiteral("metricRecall"));
    auto *mrrLabel = page.findChild<QLabel *>(QStringLiteral("metricMrr"));
    auto *runMetaLabel = page.findChild<QLabel *>(QStringLiteral("evaluationRunMeta"));
    const bool hasRunSummary = hitRateLabel && recallLabel && mrrLabel
                               && hitRateLabel->text() == QStringLiteral("0.0%");
    // 元信息行是 ragui::ElidedLabel，text() 是省略后的串，完整内容在 tooltip 里。
    const bool hasRuntimeSummary =
        runMetaLabel && runMetaLabel->toolTip().contains(QStringLiteral("test-embedding"));
    auto *candidateTable = page.findChild<QTableWidget *>(QStringLiteral("evaluationCandidateTable"));
    auto *failedTable = page.findChild<QTableWidget *>(QStringLiteral("evaluationFailedCasesTable"));
    auto *evidenceTable = page.findChild<QTableWidget *>(QStringLiteral("evaluationEvidenceTable"));
    auto *approveButton = findButton(page, QStringLiteral("evaluationApprove"));
    auto *addEvidenceButton = findButton(page, QStringLiteral("evaluationEvidenceAdd"));
    auto *removeEvidenceButton = findButton(page, QStringLiteral("evaluationEvidenceRemove"));
    auto *runButton = findButton(page, QStringLiteral("evaluationRun"));
    if (!hasTitle || !hasRunSummary || !hasRuntimeSummary || !candidateTable || !failedTable || !evidenceTable
        || failedTable->rowCount() != 1 || candidateTable->rowCount() != 1 || !approveButton
        || !addEvidenceButton || !removeEvidenceButton || !runButton) {
        std::cerr << "evaluation page skeleton missing\n";
        return 2;
    }
    runButton->click();
    if (!runRequested) {
        std::cerr << "evaluation run callback failed\n";
        return 4;
    }
    const QString captureDirectory = QDir::current().filePath(QStringLiteral("qt/build/visual"));
    QDir().mkpath(captureDirectory);
    page.resize(1280, 800);
    app.processEvents();
    if (!page.grab().save(QDir(captureDirectory).filePath(QStringLiteral("evaluation-center-1280x800.png")))) {
        std::cerr << "desktop screenshot failed\n";
        return 5;
    }
    page.resize(900, 620);
    app.processEvents();
    if (!page.grab().save(QDir(captureDirectory).filePath(QStringLiteral("evaluation-center-900x620.png")))) {
        std::cerr << "compact screenshot failed\n";
        return 6;
    }
    evidenceTable->selectRow(0);
    removeEvidenceButton->click();
    if (evidenceTable->rowCount() != 0) {
        std::cerr << "evaluation evidence removal failed\n";
        return 7;
    }
    addEvidenceButton->click();
    const int addedRow = evidenceTable->rowCount() - 1;
    evidenceTable->item(addedRow, 1)->setText(QStringLiteral("PM0214.pdf"));
    evidenceTable->item(addedRow, 2)->setText(QStringLiteral("PM0214"));
    evidenceTable->item(addedRow, 3)->setText(QStringLiteral("45"));
    evidenceTable->item(addedRow, 4)->setText(QStringLiteral("Fault types"));
    approveButton->click();
    if (!approved) {
        std::cerr << "evaluation approval callback failed\n";
        return 3;
    }
    std::cout << "EVALUATION_CENTER_PAGE_SMOKE_OK\n";
    return 0;
}
