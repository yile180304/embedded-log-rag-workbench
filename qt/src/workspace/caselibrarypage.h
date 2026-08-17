#pragma once

#include "cases/caserecord.h"
#include "workspace/workspacetaskstate.h"

#include <QWidget>

#include <functional>

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QSplitter;
class QStackedWidget;
class QTableWidget;
class QTextBrowser;

namespace ragui {
class ElidedLabel;
class EmptyStateOverlay;
} // namespace ragui

class CaseLibraryPage final : public QWidget
{
public:
    using FilterHandler = std::function<void(const CaseFilter &)>;
    using ReopenHandler = std::function<bool(const CaseRecord &)>;
    using NoteHandler = std::function<bool(const QString &, const QString &)>;
    using FavoriteHandler = std::function<bool(const QString &, bool)>;
    using CopyHandler = std::function<void(const QString &)>;
    using ExportHandler = std::function<bool(const CaseRecord &)>;
    using FeedbackHandler = std::function<bool(const CaseRecord &, const QString &)>;
    using CandidateHandler = std::function<bool(const CaseRecord &)>;

    explicit CaseLibraryPage(QWidget *parent = nullptr);

    void setFilterHandler(FilterHandler handler);
    void setReopenHandler(ReopenHandler handler);
    void setNoteHandler(NoteHandler handler);
    void setFavoriteHandler(FavoriteHandler handler);
    void setCopyHandler(CopyHandler handler);
    void setExportHandler(ExportHandler handler);
    void setFeedbackHandler(FeedbackHandler handler);
    void setCandidateHandler(CandidateHandler handler);

    void setRecords(const QVector<CaseRecord> &records);
    void setTaskState(const WorkspaceTaskState &state);
    void showStoreError(const QString &message);
    CaseFilter currentFilter() const;

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    QWidget *createFilterBar();
    QWidget *createListPanel();
    QWidget *createDetailPanel();
    QWidget *createDetailContent();
    QWidget *createDetailIdentity();
    QWidget *createDetailPlaceholder();
    QWidget *createDetailActions();
    QWidget *createFeedbackStrip();
    void emitFilter();
    void selectRecord(int row);
    void updateDetail();
    void updateActionState();
    void updateColumnBudget();
    CaseRecord *selectedRecord();
    const CaseRecord *selectedRecord() const;
    void showStatus(const QString &message);

    QLineEdit *keywordEdit_ = nullptr;
    QComboBox *modeFilter_ = nullptr;
    QComboBox *groundedFilter_ = nullptr;
    QCheckBox *favoriteFilter_ = nullptr;
    QLabel *countLabel_ = nullptr;
    QTableWidget *recordsTable_ = nullptr;
    ragui::EmptyStateOverlay *recordsEmptyHint_ = nullptr;
    QStackedWidget *detailStack_ = nullptr;
    QWidget *detailContentPage_ = nullptr;
    QWidget *detailPlaceholderPage_ = nullptr;
    QLabel *selectedTitleLabel_ = nullptr;
    ragui::ElidedLabel *selectedMetaLabel_ = nullptr;
    // 输入现场与诊断结论是并列的两块只读展示区，所以是同一种控件（版式铁律 4）。
    // 另一个原因见 createDetailContent()：只读 QPlainTextEdit 的内边距对不齐。
    QTextBrowser *inputEdit_ = nullptr;
    QTextBrowser *answerBrowser_ = nullptr;
    QPlainTextEdit *noteEdit_ = nullptr;
    ragui::ElidedLabel *statusLabel_ = nullptr;
    QPushButton *reopenButton_ = nullptr;
    QPushButton *copyButton_ = nullptr;
    QPushButton *saveNoteButton_ = nullptr;
    QPushButton *favoriteButton_ = nullptr;
    QPushButton *exportButton_ = nullptr;
    QPushButton *helpfulButton_ = nullptr;
    QPushButton *unhelpfulButton_ = nullptr;
    QPushButton *candidateButton_ = nullptr;
    QVector<CaseRecord> records_;
    int selectedRow_ = -1;
    bool anyFavorite_ = false;
    WorkspaceTaskState taskState_;
    FilterHandler filterHandler_;
    ReopenHandler reopenHandler_;
    NoteHandler noteHandler_;
    FavoriteHandler favoriteHandler_;
    CopyHandler copyHandler_;
    ExportHandler exportHandler_;
    FeedbackHandler feedbackHandler_;
    CandidateHandler candidateHandler_;
};
