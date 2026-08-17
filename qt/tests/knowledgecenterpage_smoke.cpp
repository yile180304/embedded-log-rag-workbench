#include "workspace/knowledgecenterpage.h"

#include <QApplication>
#include <QJsonArray>
#include <QJsonObject>
#include <QLabel>
#include <QPushButton>
#include <QTableWidget>
#include <QTextStream>

int main(int argc, char *argv[])
{
    QApplication application(argc, argv);
    QTextStream output(stdout);
    const QJsonArray payload{
        QJsonObject{{QStringLiteral("source_id"), QStringLiteral("RM0090")},
                    {QStringLiteral("display_name"), QStringLiteral("Reference Manual")},
                    {QStringLiteral("path"), QStringLiteral("E:/docs/RM0090.pdf")},
                    {QStringLiteral("source_type"), QStringLiteral("pdf")},
                    {QStringLiteral("manifest_status"), QStringLiteral("allowed")},
                    {QStringLiteral("index_status"), QStringLiteral("indexed")},
                    {QStringLiteral("chunks"), 1200},
                    {QStringLiteral("error"), QJsonValue::Null}},
        QJsonObject{{QStringLiteral("source_id"), QStringLiteral("PM0214")},
                    {QStringLiteral("display_name"), QStringLiteral("Programming Manual")},
                    {QStringLiteral("path"), QStringLiteral("E:/docs/PM0214.pdf")},
                    {QStringLiteral("source_type"), QStringLiteral("pdf")},
                    {QStringLiteral("manifest_status"), QStringLiteral("missing")},
                    {QStringLiteral("index_status"), QStringLiteral("not_indexed")},
                    {QStringLiteral("chunks"), 0}},
        QJsonObject{{QStringLiteral("display_name"), QStringLiteral("unapproved.pdf")},
                    {QStringLiteral("path"), QStringLiteral("E:/docs/unapproved.pdf")},
                    {QStringLiteral("source_type"), QStringLiteral("pdf")},
                    {QStringLiteral("manifest_status"), QStringLiteral("ignored")},
                    {QStringLiteral("index_status"), QStringLiteral("not_indexed")},
                    {QStringLiteral("chunks"), 0}},
        QJsonObject{{QStringLiteral("source_id"), QStringLiteral("CMSIS-CORE-CM4")},
                    {QStringLiteral("display_name"), QStringLiteral("core_cm4.h")},
                    {QStringLiteral("path"), QStringLiteral("E:/docs/core_cm4.h")},
                    {QStringLiteral("source_type"), QStringLiteral("h")},
                    {QStringLiteral("manifest_status"), QStringLiteral("allowed")},
                    {QStringLiteral("index_status"), QStringLiteral("error")},
                    {QStringLiteral("chunks"), 0},
                    {QStringLiteral("error"), QStringLiteral("parse failed")}},
    };
    const QVector<KnowledgeSourceRecord> records = knowledgeSourceRecordsFromJson(payload);
    KnowledgeCenterPage page;
    bool refreshCalled = false;
    bool reindexCalled = false;
    page.setRefreshHandler([&] {
        refreshCalled = true;
        return true;
    });
    page.setReindexHandler([&] {
        reindexCalled = true;
        return true;
    });
    page.setRecords(records);
    page.setTaskState({WorkerState::Ready, EngineState::Idle, false});
    auto *table = page.findChild<QTableWidget *>(QStringLiteral("knowledgeSourceTable"));
    QPushButton *refresh = nullptr;
    QPushButton *reindex = nullptr;
    for (QPushButton *button : page.findChildren<QPushButton *>()) {
        if (button->property("testId").toString() == QStringLiteral("knowledgeRefresh")) {
            refresh = button;
        } else if (button->property("testId").toString() == QStringLiteral("knowledgeReindex")) {
            reindex = button;
        }
    }
    if (!table || !refresh || !reindex || table->rowCount() != 4 || records.at(0).chunks != 1200) {
        output << "knowledge controls or typed records missing\n";
        return 2;
    }
    refresh->click();
    reindex->click();
    if (!refreshCalled || !reindexCalled) {
        output << "knowledge handlers failed\n";
        return 3;
    }
    page.setTaskState({WorkerState::Ready, EngineState::Running, true});
    if (reindex->isEnabled() || !refresh->isEnabled()) {
        output << "knowledge running state failed\n";
        return 4;
    }
    page.setReindexPhase(ReindexPhase::Failed, QStringLiteral("单文件解析失败"));
    bool errorVisible = false;
    for (QLabel *label : page.findChildren<QLabel *>()) {
        errorVisible = errorVisible || label->text().contains(QStringLiteral("单文件解析失败"));
    }
    if (!errorVisible || table->item(3, 7)->text() != QStringLiteral("parse failed")) {
        output << "knowledge error rendering failed\n";
        return 5;
    }
    output << "KNOWLEDGE_CENTER_PAGE_SMOKE_OK\n";
    return 0;
}
