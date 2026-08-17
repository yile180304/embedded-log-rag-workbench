#pragma once

#include <QMainWindow>
#include <QJsonArray>
#include <memory>

#include "cases/casestore.h"
#include "evaluation/evaluationstore.h"
#include "runtime/ragprocessbridge.h"
#include "runtime/runtimesettings.h"
#include "workspace/workspacetaskstate.h"

class DiagnosisWorkspacePage;
class CaseLibraryPage;
class KnowledgeCenterPage;
class EvaluationCenterPage;
struct DiagnosisViewModel;
class QLabel;
class QPlainTextEdit;
class QPushButton;
class QSplitter;
class QStackedWidget;
class QVBoxLayout;
class RuntimeSettingsDialog;

namespace ragui {
class ElidedLabel;
enum class Glyph;
}

class DiagnosticWorkbench final : public QMainWindow
{
public:
    explicit DiagnosticWorkbench(QWidget *parent = nullptr);

private:
    QWidget *createNavigationPanel();
    QPushButton *addNavigationButton(QVBoxLayout *layout, const QString &text,
                                     const QString &testId, ragui::Glyph glyph);
    void setActiveNavigation(QPushButton *active);
    QWidget *createStatusStrip();
    QWidget *createRuntimeActivityPanel();
    void setActivityExpanded(bool expanded);
    void applyTheme();
    void configureRuntime();
    void configureCaseStore(const QString &projectRoot);
    void bindDiagnosisPage();
    void bindCaseLibraryPage();
    void bindKnowledgeCenterPage();
    void bindEvaluationCenterPage();
    void showDiagnosisWorkspace();
    void showCaseLibrary();
    void showKnowledgeCenter();
    void showEvaluationCenter();
    void refreshCaseLibrary(const CaseFilter &filter = {});
    void refreshEvaluationCenter();
    bool reopenCase(const CaseRecord &record);
    bool updateCaseNote(const QString &id, const QString &note);
    bool updateCaseFavorite(const QString &id, bool favorite);
    void copyCaseText(const QString &text);
    bool exportCase(const CaseRecord &record);
    bool recordCaseFeedback(const CaseRecord &record, const QString &feedback);
    bool createEvaluationCandidate(const CaseRecord &record);
    bool approveEvaluationCandidate(const EvaluationCandidate &candidate);
    bool rejectEvaluationCandidate(const QString &id);
    bool importEvaluationCandidates();
    bool startEvaluation(int topK);
    void finishEvaluationSuccess(const QJsonObject &result);
    void finishEvaluationFailure(const QString &message);
    void openRuntimeSettings();
    bool applyRuntimeSettings(const RuntimeSettings &settings);
    void updateHealthSnapshot(const QJsonObject &snapshot);
    void updateRuntimeStatus();
    bool startQuery(const QString &question, int topK);
    bool startHardFaultDiagnosis(const QString &hardFaultLog, int topK);
    bool startProtocolLogDiagnosis(const QJsonObject &payload, int topK);
    bool startReindex();
    void renderDiagnosis(const QJsonObject &result);
    void saveDiagnosisCase(const QJsonObject &result, const DiagnosisViewModel &model);
    void renderIngestSummary(const QJsonObject &result);
    void handleEngineState(EngineState state);
    void handleWorkerState(WorkerState state);
    void updateWorkspaceTaskState();
    void handleProcessFailure(const QString &message);
    void handleHardFaultFailure(const QString &message);
    void handleProtocolLogFailure(const QString &message);
    void appendLog(const QString &message);
    void openReport(const QString &reportPath);

    QLabel *projectStatusLabel_ = nullptr;
    QLabel *pythonStatusLabel_ = nullptr;
    QLabel *indexStatusLabel_ = nullptr;
    // Worker 生命周期与 Engine 任务状态是两件事，各占一个指示器。
    // 改版前它们共用一个 label，后写的会把先写的覆盖掉。
    QLabel *workerStatusLabel_ = nullptr;
    QLabel *engineStatusLabel_ = nullptr;
    QLabel *indexSummaryLabel_ = nullptr;
    ragui::ElidedLabel *activityPreviewLabel_ = nullptr;
    QPushButton *activityToggleButton_ = nullptr;
    QPushButton *diagnosisNavigationButton_ = nullptr;
    QPushButton *caseLibraryNavigationButton_ = nullptr;
    QPushButton *knowledgeNavigationButton_ = nullptr;
    QPushButton *evaluationNavigationButton_ = nullptr;
    QPushButton *settingsButton_ = nullptr;
    QSplitter *workspaceSplitter_ = nullptr;
    QStackedWidget *workspaceStack_ = nullptr;
    DiagnosisWorkspacePage *diagnosisPage_ = nullptr;
    CaseLibraryPage *caseLibraryPage_ = nullptr;
    KnowledgeCenterPage *knowledgeCenterPage_ = nullptr;
    EvaluationCenterPage *evaluationCenterPage_ = nullptr;
    QPlainTextEdit *logEdit_ = nullptr;
    std::unique_ptr<RagProcessBridge> processBridge_;
    std::unique_ptr<CaseStore> caseStore_;
    std::unique_ptr<EvaluationStore> evaluationStore_;
    RuntimeSettings runtimeSettings_;
    RuntimeSettings runtimeDefaults_;
    QJsonObject healthSnapshot_;
    QVector<HealthIssue> healthIssues_;
    RuntimeSettingsDialog *settingsDialog_ = nullptr;
    WorkspaceTaskState workspaceTaskState_;
    bool reindexRunning_ = false;
    QJsonArray activeEvaluationSamples_;
    QString activeEvaluationStartedAt_;
    int activeEvaluationTopK_ = 0;
};
