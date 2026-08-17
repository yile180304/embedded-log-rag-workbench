#include "cases/casestore.h"
#include "diagnosticworkbench.h"
#include "runtime/runtimesettings.h"

#include <QApplication>
#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSettings>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QTextBrowser>
#include <QTextStream>

namespace {
CaseRecord makeRecord(
    const QString &id,
    const QString &mode,
    const QString &input,
    const QJsonObject &result)
{
    CaseRecord record;
    record.id = id;
    record.mode = mode;
    record.inputText = input;
    record.query = result.value(QStringLiteral("query")).toString();
    record.answer = result.value(QStringLiteral("answer")).toString();
    record.grounded = result.value(QStringLiteral("grounded")).toBool();
    record.resultJson = result;
    return record;
}

QPushButton *findButton(QWidget &widget, const QString &testId)
{
    for (QPushButton *button : widget.findChildren<QPushButton *>()) {
        if (button->property("testId").toString() == testId) {
            return button;
        }
    }
    return nullptr;
}
} // namespace

int main(int argc, char *argv[])
{
    QApplication application(argc, argv);
    application.setOrganizationName(QStringLiteral("CaseLibraryWorkbenchSmoke"));
    application.setApplicationName(QStringLiteral("CaseLibraryWorkbenchSmoke"));
    QTextStream output(stdout);
    QTemporaryDir temporaryDirectory;
    if (!temporaryDirectory.isValid()) {
        output << "temporary directory failed\n";
        return 2;
    }
    const QString projectRoot = temporaryDirectory.path();
    QFile projectFile(QDir(projectRoot).filePath(QStringLiteral("pyproject.toml")));
    if (!projectFile.open(QIODevice::WriteOnly) || projectFile.write("[project]\nname='smoke'\n") < 1) {
        output << "project fixture failed\n";
        return 3;
    }
    projectFile.close();

    const QJsonObject validResult{{QStringLiteral("query"), QStringLiteral("保存的专家问题")},
                                  {QStringLiteral("answer"), QStringLiteral("保存的专家结论")},
                                  {QStringLiteral("grounded"), true},
                                  {QStringLiteral("evidence"), QJsonArray{}}};
    const QJsonObject invalidResult{{QStringLiteral("answer"), QStringLiteral("坏快照")}};
    const QString protocolInput = QStringLiteral(
        "2026-08-15T10:00:00.000Z UDP seq=40 len=64\n"
        "2026-08-15T10:00:00.025Z UDP seq=42 len=64");
    const QJsonObject protocolResult{
        {QStringLiteral("query"), QStringLiteral("UDP sequence_gap missing 41")},
        {QStringLiteral("answer"), QStringLiteral("保存的协议日志规则事实")},
        {QStringLiteral("grounded"), false},
        {QStringLiteral("refusal_reason"), QStringLiteral("no_evidence")},
        {QStringLiteral("evidence"), QJsonArray{}},
        {QStringLiteral("metadata"), QJsonObject{
             {QStringLiteral("protocol_log"), QJsonObject{
                  {QStringLiteral("profile"), QStringLiteral("udp")},
                  {QStringLiteral("summary"), QJsonObject{
                       {QStringLiteral("line_count"), 2},
                       {QStringLiteral("event_count"), 2},
                       {QStringLiteral("error_count"), 0},
                       {QStringLiteral("warning_count"), 1}}},
                  {QStringLiteral("events"), QJsonArray{
                       QJsonObject{{QStringLiteral("line_no"), 1}, {QStringLiteral("protocol"), QStringLiteral("UDP")}, {QStringLiteral("sequence"), 40}, {QStringLiteral("length"), 64}, {QStringLiteral("raw_message"), protocolInput.section('\n', 0, 0)}},
                       QJsonObject{{QStringLiteral("line_no"), 2}, {QStringLiteral("protocol"), QStringLiteral("UDP")}, {QStringLiteral("sequence"), 42}, {QStringLiteral("length"), 64}, {QStringLiteral("interval_ms"), 25.0}, {QStringLiteral("raw_message"), protocolInput.section('\n', 1, 1)}}}},
                  {QStringLiteral("anomalies"), QJsonArray{
                       QJsonObject{{QStringLiteral("type"), QStringLiteral("sequence_gap")}, {QStringLiteral("severity"), QStringLiteral("warning")}, {QStringLiteral("line_no"), 2}, {QStringLiteral("message"), QStringLiteral("缺少序列 41")}}}},
                  {QStringLiteral("config"), QJsonObject{}},
                  {QStringLiteral("generated_query"), QStringLiteral("UDP sequence_gap missing 41")}}}}}};
    const QString databasePath = QDir(projectRoot).filePath(QStringLiteral("data/runtime/diagnosis_cases.sqlite3"));
    CaseStore fixtureStore(databasePath);
    QString error;
    if (!fixtureStore.initialize(&error)) {
        output << "fixture store failed: " << error << '\n';
        return 4;
    }
    QString validId;
    QString invalidId;
    QString protocolId;
    if (!fixtureStore.save(makeRecord(QStringLiteral("valid-case"), QStringLiteral("expert"), QStringLiteral("保存的专家问题"), validResult),
                           &validId, &error)
        || !fixtureStore.save(makeRecord(QStringLiteral("protocol-case"), QStringLiteral("protocol_log"), protocolInput, protocolResult),
                              &protocolId, &error)
        || !fixtureStore.save(makeRecord(QStringLiteral("invalid-case"), QStringLiteral("expert"), QStringLiteral("坏快照问题"), invalidResult),
                              &invalidId, &error)) {
        output << "fixture records failed: " << error << '\n';
        return 5;
    }

    RuntimeSettings settings;
    settings.projectRoot = projectRoot;
    settings.pythonExecutable = QDir(projectRoot).filePath(QStringLiteral("missing-python.exe"));
    settings.embeddingModel = QStringLiteral("test-embedding");
    settings.rerankerModel = QStringLiteral("test-reranker");
    settings.defaultTopK = 5;
    settings.taskTimeoutSeconds = 60;
    QSettings persistentSettings;
    RuntimeSettingsStore(persistentSettings).save(settings);

    DiagnosticWorkbench window;
    window.show();
    QApplication::processEvents();
    if (!findButton(window, QStringLiteral("caseLibraryNavigation"))) {
        output << "case navigation missing\n";
        return 6;
    }
    findButton(window, QStringLiteral("caseLibraryNavigation"))->click();
    QApplication::processEvents();
    auto *table = window.findChild<QTableWidget *>(QStringLiteral("caseRecordsTable"));
    auto *question = window.findChild<QPlainTextEdit *>(QStringLiteral("expertQuestion"));
    auto *answer = window.findChild<QTextBrowser *>(QStringLiteral("diagnosisAnswer"));
    auto *modeSelector = window.findChild<QComboBox *>(QStringLiteral("modeSelector"));
    auto *protocolLog = window.findChild<QPlainTextEdit *>(QStringLiteral("protocolLog"));
    auto *protocolAnswer = window.findChild<QTextBrowser *>(QStringLiteral("protocolRagAnswer"));
    auto *reopen = findButton(window, QStringLiteral("caseReopen"));
    if (!table || !question || !answer || !modeSelector || !protocolLog || !protocolAnswer
        || !reopen || table->rowCount() != 3) {
        output << "workbench case controls missing\n";
        return 7;
    }

    int validRow = -1;
    int invalidRow = -1;
    int protocolRow = -1;
    for (int row = 0; row < table->rowCount(); ++row) {
        const QString id = table->item(row, 0)->data(Qt::UserRole).toString();
        if (id == validId) {
            validRow = row;
        } else if (id == invalidId) {
            invalidRow = row;
        } else if (id == protocolId) {
            protocolRow = row;
        }
    }
    if (validRow < 0 || invalidRow < 0 || protocolRow < 0) {
        output << "fixture rows missing\n";
        return 8;
    }

    table->selectRow(validRow);
    QApplication::processEvents();
    if (!reopen->isEnabled()) {
        output << "idle reopen unexpectedly disabled\n";
        return 9;
    }
    reopen->click();
    QApplication::processEvents();
    if (question->toPlainText() != QStringLiteral("保存的专家问题")
        || !answer->toPlainText().contains(QStringLiteral("保存的专家结论"))) {
        output << "valid case did not reopen\n";
        return 10;
    }

    findButton(window, QStringLiteral("caseLibraryNavigation"))->click();
    QApplication::processEvents();
    table->selectRow(protocolRow);
    QApplication::processEvents();
    reopen->click();
    QApplication::processEvents();
    if (modeSelector->currentIndex() != 2
        || protocolLog->toPlainText() != protocolInput
        || !protocolAnswer->toPlainText().contains(QStringLiteral("协议日志规则事实"))) {
        output << "protocol case did not reopen\n";
        return 11;
    }

    findButton(window, QStringLiteral("caseLibraryNavigation"))->click();
    QApplication::processEvents();
    table->selectRow(invalidRow);
    QApplication::processEvents();
    const QString questionBeforeBadReopen = question->toPlainText();
    reopen->click();
    QApplication::processEvents();
    const auto *log = window.findChild<QPlainTextEdit *>(QStringLiteral("runtimeActivityLog"));
    if (question->toPlainText() != questionBeforeBadReopen || !log
        || !log->toPlainText().contains(QStringLiteral("案例无法重开"))) {
        output << "bad snapshot protection failed\n";
        return 12;
    }
    output << "WORKBENCH_CASE_LIBRARY_SMOKE_OK\n";
    return 0;
}
