#pragma once

#include "diagnosisviewmodel.h"
#include "workspace/workspacetaskstate.h"

#include <QVector>
#include <QWidget>

#include <functional>

class DiagnosticWorkOrder;
class ProtocolLogPanel;
class ProtocolWorkOrder;
class QButtonGroup;
class QLabel;
class QComboBox;
class QPlainTextEdit;
class QPushButton;
class QSpinBox;
class QStackedWidget;
class QTableWidget;
class QTextBrowser;

namespace ragui {
class ElidedLabel;
}

enum class WorkspacePageId
{
    Diagnosis,
    Cases,
    Knowledge
};

enum class DiagnosisMode
{
    Expert,
    HardFault,
    ProtocolLog
};

class DiagnosisWorkspacePage final : public QWidget
{
public:
    using QueryHandler = std::function<bool(const QString &, int)>;
    using HardFaultHandler = std::function<bool(const QString &, int)>;
    using ProtocolLogHandler = std::function<bool(const QJsonObject &, int)>;
    using ReportHandler = std::function<void(const QString &)>;
    using StatusHandler = std::function<void(const QString &, int)>;

    explicit DiagnosisWorkspacePage(QWidget *parent = nullptr);

    void setQueryHandler(QueryHandler handler);
    void setHardFaultHandler(HardFaultHandler handler);
    void setProtocolLogHandler(ProtocolLogHandler handler);
    void setReportHandler(ReportHandler handler);
    void setStatusHandler(StatusHandler handler);

    void setMode(DiagnosisMode mode);
    DiagnosisMode mode() const;
    void restoreCaseInput(const QString &mode, const QString &input);
    void setDefaultTopK(int topK);
    void setTaskState(const WorkspaceTaskState &state);
    void setDiagnosisResult(const DiagnosisViewModel &model);
    void showQueryFailure(const QString &message);
    void showHardFaultFailure(const QString &message);
    void showProtocolLogFailure(const QString &message);

private:
    QWidget *createToolbar();
    QWidget *createQueryPanel();
    QWidget *createHardFaultPanel();
    QWidget *createAnswerPanel();
    QWidget *createEvidencePanel();
    QWidget *createEvidenceDetail();
    void configureEvidenceTable();
    void syncModeButtons(int index);
    void sizeInputStackToPage(int index);
    void syncAnswerPlaceholder(const QString &hint = QString());
    void submitQuery();
    void submitHardFault();
    void submitProtocolLog();
    void openCurrentReport();
    void renderEvidence(const QVector<EvidenceViewModel> &evidence);
    void clearEvidence();
    void updateEvidenceDetail();
    void markRunning();
    void markFailed();
    void showStatus(const QString &message, int timeoutMs = 0);

    QComboBox *modeComboBox_ = nullptr;
    QButtonGroup *modeGroup_ = nullptr;
    QVector<QPushButton *> modeButtons_;
    QStackedWidget *actionStack_ = nullptr;
    QStackedWidget *inputStack_ = nullptr;
    QStackedWidget *resultStack_ = nullptr;
    QPlainTextEdit *questionEdit_ = nullptr;
    QPlainTextEdit *hardFaultLogEdit_ = nullptr;
    ProtocolLogPanel *protocolLogPanel_ = nullptr;
    QSpinBox *topKSpinBox_ = nullptr;
    QPushButton *queryButton_ = nullptr;
    QPushButton *hardFaultButton_ = nullptr;
    QPushButton *openReportButton_ = nullptr;
    QTextBrowser *answerBrowser_ = nullptr;
    QLabel *answerEmptyOverlay_ = nullptr;
    QLabel *groundedStatusLabel_ = nullptr;
    ragui::ElidedLabel *resultMetaLabel_ = nullptr;
    DiagnosticWorkOrder *workOrder_ = nullptr;
    ProtocolWorkOrder *protocolWorkOrder_ = nullptr;
    QTableWidget *evidenceTable_ = nullptr;
    QLabel *evidenceCountLabel_ = nullptr;
    QWidget *evidenceEmptyOverlay_ = nullptr;
    QStackedWidget *evidenceDetailStack_ = nullptr;
    ragui::ElidedLabel *evidenceDocIdLabel_ = nullptr;
    ragui::ElidedLabel *evidenceChunkIdLabel_ = nullptr;
    ragui::ElidedLabel *evidenceScoreLabel_ = nullptr;
    QVector<EvidenceViewModel> currentEvidence_;
    QString currentReportPath_;
    QueryHandler queryHandler_;
    HardFaultHandler hardFaultHandler_;
    ProtocolLogHandler protocolLogHandler_;
    ReportHandler reportHandler_;
    StatusHandler statusHandler_;
};
