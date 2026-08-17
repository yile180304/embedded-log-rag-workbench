#include "workspace/caselibrarypage.h"

#include <QApplication>
#include <QComboBox>
#include <QJsonArray>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTableWidget>
#include <QTextStream>

namespace {
CaseRecord makeRecord(const QString &id, const QString &mode, bool favorite)
{
    CaseRecord record;
    record.id = id;
    record.createdAt = QStringLiteral("2026-08-14T10:00:00.000Z");
    record.updatedAt = record.createdAt;
    record.mode = mode;
    record.inputText = mode == QStringLiteral("hardfault") ? QStringLiteral("CFSR=0x82") : QStringLiteral("EBS 是什么？");
    record.query = mode == QStringLiteral("hardfault") ? QStringLiteral("PRECISERR 如何排查？") : record.inputText;
    record.answer = QStringLiteral("来自手册的可复核结论");
    record.grounded = true;
    record.resultJson = {
        {QStringLiteral("query"), record.query},
        {QStringLiteral("answer"), record.answer},
        {QStringLiteral("grounded"), true},
        {QStringLiteral("evidence"), QJsonArray{}},
    };
    record.favorite = favorite;
    record.note = QStringLiteral("初始备注");
    return record;
}
} // namespace

int main(int argc, char *argv[])
{
    QApplication application(argc, argv);
    QTextStream output(stdout);
    CaseLibraryPage page;
    CaseFilter receivedFilter;
    bool filterCalled = false;
    QString reopenedId;
    QString noteId;
    QString receivedNote;
    QString favoriteId;
    bool receivedFavorite = false;
    QString copiedText;
    QString exportedId;
    page.setFilterHandler([&](const CaseFilter &filter) {
        receivedFilter = filter;
        filterCalled = true;
    });
    page.setReopenHandler([&](const CaseRecord &record) {
        reopenedId = record.id;
        return true;
    });
    page.setNoteHandler([&](const QString &id, const QString &note) {
        noteId = id;
        receivedNote = note;
        return true;
    });
    page.setFavoriteHandler([&](const QString &id, bool favorite) {
        favoriteId = id;
        receivedFavorite = favorite;
        return true;
    });
    page.setCopyHandler([&](const QString &text) { copiedText = text; });
    page.setExportHandler([&](const CaseRecord &record) {
        exportedId = record.id;
        return true;
    });

    page.setRecords({makeRecord(QStringLiteral("case-1"), QStringLiteral("expert"), false),
                     makeRecord(QStringLiteral("case-2"), QStringLiteral("hardfault"), true),
                     makeRecord(QStringLiteral("case-3"), QStringLiteral("protocol_log"), false)});
    page.setTaskState({WorkerState::Ready, EngineState::Idle, false});
    auto *table = page.findChild<QTableWidget *>(QStringLiteral("caseRecordsTable"));
    auto *keyword = page.findChild<QLineEdit *>(QStringLiteral("caseKeyword"));
    auto *modeFilter = page.findChild<QComboBox *>(QStringLiteral("caseModeFilter"));
    auto *reopen = page.findChild<QPushButton *>(QStringLiteral("caseReopenButton"));
    auto *copy = page.findChild<QPushButton *>(QStringLiteral("caseCopyButton"));
    auto *saveNote = page.findChild<QPushButton *>(QStringLiteral("caseSaveNoteButton"));
    auto *favorite = page.findChild<QPushButton *>(QStringLiteral("caseFavoriteButton"));
    auto *exportButton = page.findChild<QPushButton *>(QStringLiteral("caseExportButton"));
    auto *note = page.findChild<QPlainTextEdit *>(QStringLiteral("caseNote"));
    if (!table || !keyword || !modeFilter || !reopen || !copy || !saveNote || !favorite || !exportButton || !note
        || table->rowCount() != 3 || table->item(2, 1)->text() != QStringLiteral("协议日志")) {
        output << "case page controls or records missing\n";
        return 2;
    }
    modeFilter->setCurrentIndex(modeFilter->findData(QStringLiteral("protocol_log")));
    if (!filterCalled || receivedFilter.mode != QStringLiteral("protocol_log")) {
        output << "protocol mode filter failed\n";
        return 3;
    }
    modeFilter->setCurrentIndex(0);
    keyword->setText(QStringLiteral("EBS"));
    if (!filterCalled || receivedFilter.keyword != QStringLiteral("EBS")) {
        output << "case filter failed\n";
        return 4;
    }
    reopen->click();
    if (reopenedId != QStringLiteral("case-1")) {
        output << "reopen handler failed\n";
        return 5;
    }
    note->setPlainText(QStringLiteral("需要补充页码"));
    saveNote->click();
    favorite->click();
    copy->click();
    exportButton->click();
    if (noteId != QStringLiteral("case-1") || receivedNote != QStringLiteral("需要补充页码")
        || favoriteId != QStringLiteral("case-1") || !receivedFavorite || copiedText.isEmpty()
        || exportedId != QStringLiteral("case-1")) {
        output << "case actions failed\n";
        return 6;
    }
    page.setTaskState({WorkerState::Ready, EngineState::Running, true});
    if (reopen->isEnabled()) {
        output << "active request did not disable reopen\n";
        return 7;
    }
    page.setTaskState({WorkerState::Stopped, EngineState::Failed, false});
    if (!reopen->isEnabled()) {
        output << "idle case could not reopen without worker\n";
        return 8;
    }
    output << "CASE_LIBRARY_PAGE_SMOKE_OK\n";
    return 0;
}
