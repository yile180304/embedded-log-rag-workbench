#include "workspace/diagnosisworkspacepage.h"

#include <QApplication>
#include <QComboBox>
#include <QCheckBox>
#include <QDoubleSpinBox>
#include <QJsonArray>
#include <QJsonObject>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTextBrowser>
#include <QTableWidget>
#include <QTextStream>

int main(int argc, char *argv[])
{
    QApplication application(argc, argv);
    QTextStream output(stdout);
    DiagnosisWorkspacePage page;
    bool queryCalled = false;
    bool hardFaultCalled = false;
    bool protocolCalled = false;
    QString openedReport;
    page.setQueryHandler([&](const QString &question, int topK) {
        queryCalled = question == QStringLiteral("CFSR 是什么？") && topK == 7;
        return queryCalled;
    });
    page.setHardFaultHandler([&](const QString &log, int topK) {
        hardFaultCalled = log.contains(QStringLiteral("CFSR=0x00008200")) && topK == 7;
        return hardFaultCalled;
    });
    page.setProtocolLogHandler([&](const QJsonObject &payload, int topK) {
        protocolCalled = payload.value(QStringLiteral("log")).toString().contains(QStringLiteral("seq=40"))
                         && payload.value(QStringLiteral("profile")).toString() == QStringLiteral("udp")
                         && payload.value(QStringLiteral("expected_cycle_ms")).toDouble() == 10.0
                         && payload.value(QStringLiteral("jitter_tolerance_ms")).toDouble() == 2.0
                         && payload.value(QStringLiteral("expected_length")).toInt() == 64
                         && topK == 7;
        return protocolCalled;
    });
    page.setReportHandler([&](const QString &path) { openedReport = path; });

    page.setDefaultTopK(7);
    page.setTaskState({WorkerState::Ready, EngineState::Idle, false});
    auto *question = page.findChild<QPlainTextEdit *>(QStringLiteral("expertQuestion"));
    auto *hardFaultLog = page.findChild<QPlainTextEdit *>(QStringLiteral("hardFaultLog"));
    QPushButton *queryButton = nullptr;
    for (QPushButton *button : page.findChildren<QPushButton *>()) {
        if (button->property("testId").toString() == QStringLiteral("expertQueryButton")) {
            queryButton = button;
            break;
        }
    }
    auto *hardFaultButton = page.findChild<QPushButton *>(QStringLiteral("hardFaultButton"));
    auto *modeSelector = page.findChild<QComboBox *>(QStringLiteral("modeSelector"));
    auto *protocolLog = page.findChild<QPlainTextEdit *>(QStringLiteral("protocolLog"));
    auto *protocolProfile = page.findChild<QComboBox *>(QStringLiteral("protocolProfile"));
    auto *cycleEnabled = page.findChild<QCheckBox *>(QStringLiteral("protocolCycleEnabled"));
    auto *lengthEnabled = page.findChild<QCheckBox *>(QStringLiteral("protocolLengthEnabled"));
    auto *protocolButton = page.findChild<QPushButton *>(QStringLiteral("protocolLogButton"));
    QPushButton *reportButton = nullptr;
    for (QPushButton *button : page.findChildren<QPushButton *>()) {
        if (button->property("testId").toString() == QStringLiteral("openDiagnosisReport")) {
            reportButton = button;
            break;
        }
    }
    if (!question || !hardFaultLog || !queryButton || !hardFaultButton || !modeSelector || !reportButton
        || !protocolLog || !protocolProfile || !cycleEnabled || !lengthEnabled || !protocolButton) {
        output << "page controls missing\n";
        return 2;
    }

    question->setPlainText(QStringLiteral("CFSR 是什么？"));
    queryButton->click();
    if (!queryCalled) {
        output << "query handler failed\n";
        return 3;
    }

    DiagnosisViewModel result;
    result.answer = QStringLiteral("CFSR 是故障状态寄存器。");
    result.grounded = true;
    result.reportPath = QStringLiteral("reports/query.md");
    page.setDiagnosisResult(result);
    auto *answer = page.findChild<QTextBrowser *>(QStringLiteral("diagnosisAnswer"));
    if (!answer || !answer->toPlainText().contains(QStringLiteral("CFSR"))) {
        output << "typed result was not rendered\n";
        return 4;
    }
    reportButton->click();
    if (openedReport != QStringLiteral("reports/query.md")) {
        output << "report handler failed\n";
        return 5;
    }

    modeSelector->setCurrentIndex(1);
    hardFaultLog->setPlainText(QStringLiteral("CFSR=0x00008200 HFSR=0x40000000"));
    hardFaultButton->click();
    if (!hardFaultCalled) {
        output << "hardfault handler failed\n";
        return 6;
    }
    modeSelector->setCurrentIndex(0);
    if (question->toPlainText() != QStringLiteral("CFSR 是什么？")) {
        output << "mode switching lost page input\n";
        return 7;
    }

    modeSelector->setCurrentIndex(2);
    protocolProfile->setCurrentIndex(2);
    cycleEnabled->setChecked(true);
    lengthEnabled->setChecked(true);
    protocolLog->setPlainText(QStringLiteral(
        "2026-08-15T10:00:00.000Z UDP seq=40 len=64\n"
        "2026-08-15T10:00:00.025Z UDP seq=42 len=64"));
    protocolButton->click();
    if (!protocolCalled) {
        output << "protocol handler failed\n";
        return 8;
    }

    const QJsonObject protocolResult{
        {QStringLiteral("query"), QStringLiteral("UDP sequence gap")},
        {QStringLiteral("answer"), QStringLiteral("当前知识库无法确认协议条款。")},
        {QStringLiteral("grounded"), false},
        {QStringLiteral("refusal_reason"), QStringLiteral("no_evidence")},
        {QStringLiteral("evidence"), QJsonArray{}},
        {QStringLiteral("metadata"), QJsonObject{
             {QStringLiteral("report_path"), QStringLiteral("reports/protocol.md")},
             {QStringLiteral("protocol_log"), QJsonObject{
                  {QStringLiteral("profile"), QStringLiteral("udp")},
                  {QStringLiteral("summary"), QJsonObject{
                       {QStringLiteral("line_count"), 2},
                       {QStringLiteral("event_count"), 2},
                       {QStringLiteral("error_count"), 0},
                       {QStringLiteral("warning_count"), 1}}},
                  {QStringLiteral("events"), QJsonArray{
                       QJsonObject{{QStringLiteral("line_no"), 1}, {QStringLiteral("protocol"), QStringLiteral("UDP")}, {QStringLiteral("sequence"), 40}, {QStringLiteral("length"), 64}},
                       QJsonObject{{QStringLiteral("line_no"), 2}, {QStringLiteral("protocol"), QStringLiteral("UDP")}, {QStringLiteral("sequence"), 42}, {QStringLiteral("length"), 64}, {QStringLiteral("interval_ms"), 25.0}}}},
                  {QStringLiteral("anomalies"), QJsonArray{
                       QJsonObject{{QStringLiteral("type"), QStringLiteral("sequence_gap")}, {QStringLiteral("severity"), QStringLiteral("warning")}, {QStringLiteral("line_no"), 2}, {QStringLiteral("message"), QStringLiteral("缺少序列 41")}}}},
                  {QStringLiteral("config"), QJsonObject{}},
                  {QStringLiteral("generated_query"), QStringLiteral("UDP sequence_gap missing 41")}}}}}};
    QString protocolError;
    const DiagnosisViewModel protocolModel = DiagnosisViewModel::fromJson(protocolResult, &protocolError);
    page.setDiagnosisResult(protocolModel);
    auto *eventTable = page.findChild<QTableWidget *>(QStringLiteral("protocolEventTable"));
    auto *anomalyTable = page.findChild<QTableWidget *>(QStringLiteral("protocolAnomalyTable"));
    auto *protocolAnswer = page.findChild<QTextBrowser *>(QStringLiteral("protocolRagAnswer"));
    QPushButton *protocolReportButton = nullptr;
    for (QPushButton *button : page.findChildren<QPushButton *>()) {
        if (button->property("testId").toString() == QStringLiteral("openProtocolReport")) {
            protocolReportButton = button;
            break;
        }
    }
    if (!protocolError.isEmpty() || !protocolModel.protocolLog.available
        || !eventTable || eventTable->rowCount() != 2
        || !anomalyTable || anomalyTable->rowCount() != 1
        || !protocolAnswer || !protocolAnswer->toPlainText().contains(QStringLiteral("无法确认"))
        || !protocolReportButton || !protocolReportButton->isEnabled()) {
        output << "protocol typed result was not rendered\n";
        return 9;
    }
    protocolReportButton->click();
    if (openedReport != QStringLiteral("reports/protocol.md")) {
        output << "protocol report handler failed\n";
        return 10;
    }

    page.setTaskState({WorkerState::Ready, EngineState::Running, true});
    if (queryButton->isEnabled() || hardFaultButton->isEnabled() || protocolButton->isEnabled() || modeSelector->isEnabled()) {
        output << "running state did not disable page actions\n";
        return 11;
    }
    page.setTaskState({WorkerState::Ready, EngineState::Succeeded, false});
    if (!queryButton->isEnabled() || !hardFaultButton->isEnabled() || !protocolButton->isEnabled()) {
        output << "ready state did not enable page actions\n";
        return 12;
    }
    page.setTaskState({WorkerState::Failed, EngineState::Failed, false});
    if (queryButton->isEnabled() || hardFaultButton->isEnabled() || protocolButton->isEnabled()) {
        output << "failed worker state did not disable page actions\n";
        return 13;
    }
    output << "DIAGNOSIS_WORKSPACE_PAGE_SMOKE_OK\n";
    return 0;
}
