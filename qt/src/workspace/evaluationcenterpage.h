#pragma once

#include "evaluation/evaluationrecord.h"
#include "ui/uikit.h"
#include "workspace/workspacetaskstate.h"

#include <QJsonObject>
#include <QWidget>

class QFrame;
class QLabel;
class QLineEdit;
class QPushButton;
class QSpinBox;
class QTableWidget;

#include <functional>

/// 评估中心。
///
/// 版式分三个区，每个区只干一件事：
///   · 顶部记分板：三张 ragui::makeStatCard —— Hit Rate / Recall / MRR，右边一行运行快照。
///   · 左列清单：反馈候选（待审核的 query）+ Failed Cases（跑完后没召回的 query）。
///     两张表都是"待处理的 query 列表"，放在一起才好互相对照。
///   · 右列工作面：人工审核（Query + Evidence + 决策）→ 运行评估。
///
/// 为什么不再套 QScrollArea：改版前右栏三段叠在一起放不下，只好塞进滚动区，
/// 结果 Failed Cases 的表头被裁掉半行。现在把「运行评估」挪到右列、
/// 「Failed Cases」挪到左列，两列的高度需求各自减半，1440×900 下三区全部落地。
class EvaluationCenterPage final : public QWidget
{
public:
    using ApproveHandler = std::function<bool(const EvaluationCandidate &)>;
    using RejectHandler = std::function<bool(const QString &)>;
    using RunHandler = std::function<bool(int)>;
    using ImportHandler = std::function<bool()>;

    explicit EvaluationCenterPage(QWidget *parent = nullptr);

    void setCandidates(const QVector<EvaluationCandidate> &candidates);
    void setApprovedSampleCount(int count);
    void setLatestRun(const EvaluationRun &run);
    void clearLatestRun();
    void setApproveHandler(ApproveHandler handler);
    void setRejectHandler(RejectHandler handler);
    void setRunHandler(RunHandler handler);
    void setImportHandler(ImportHandler handler);
    void setTaskState(const WorkspaceTaskState &state);
    void showProgress(const QJsonObject &progress);
    void showEvaluationResult(const QJsonObject &result);
    void showFailure(const QString &message);

private:
    QWidget *buildResultBand();
    QWidget *buildQueueColumn();
    QWidget *buildWorkColumn();
    QWidget *buildCandidateSection();
    QWidget *buildFailedSection();
    QWidget *buildReviewSection();
    QWidget *buildRunSection();

    void addEvidenceRow(const ExpectedEvidenceRef &reference = {});
    void removeSelectedEvidenceRows();
    void updateCandidateDetail();
    void updateActionState();
    void updateEmptyStates();
    /// 指标区统一入口。空串一律显示不刺眼的占位，绝不把「没有运行」渲染成 0%。
    /// topK > 0 时把注解写成 top-k = k 内命中/召回，否则退回指标定义。
    /// emptyNote 是数值缺席时写进注解的原因（尚无运行 / 运行失败 / 没有样本）。
    void setMetricCards(const QString &hitRate, const QString &recall, const QString &mrr, int topK,
                        const QString &emptyNote);
    void setRunNarrative(const QString &metaLine);
    void setFailedSummary(const QString &text, ragui::Tone tone);
    EvaluationCandidate editedCandidate() const;
    const EvaluationCandidate *selectedCandidate() const;
    void showStatus(const QString &message, ragui::Tone tone, const QString &tooltip = QString());

    QLabel *statusLabel_ = nullptr;
    QLabel *candidateSummaryLabel_ = nullptr;
    QLabel *reviewStatusLabel_ = nullptr;
    QLabel *sampleSummaryLabel_ = nullptr;
    QLabel *failedSummaryLabel_ = nullptr;
    ragui::ElidedLabel *runMetaLabel_ = nullptr;
    QLabel *hitRateValue_ = nullptr;
    QLabel *recallValue_ = nullptr;
    QLabel *mrrValue_ = nullptr;
    QLabel *hitRateCaption_ = nullptr;
    QLabel *recallCaption_ = nullptr;
    QLabel *mrrCaption_ = nullptr;
    QWidget *metricsBand_ = nullptr;
    QTableWidget *failedCasesTable_ = nullptr;
    QTableWidget *candidateTable_ = nullptr;
    QLineEdit *queryEdit_ = nullptr;
    QTableWidget *evidenceTable_ = nullptr;
    QPushButton *addEvidenceButton_ = nullptr;
    QPushButton *removeEvidenceButton_ = nullptr;
    QPushButton *approveButton_ = nullptr;
    QPushButton *rejectButton_ = nullptr;
    QPushButton *importButton_ = nullptr;
    QPushButton *runButton_ = nullptr;
    QSpinBox *topKSpin_ = nullptr;
    ragui::EmptyStateOverlay *candidateEmpty_ = nullptr;
    ragui::EmptyStateOverlay *evidenceEmpty_ = nullptr;
    ragui::EmptyStateOverlay *failedEmpty_ = nullptr;
    QString failedEmptyText_;
    QVector<EvaluationCandidate> candidates_;
    int selectedRow_ = -1;
    WorkspaceTaskState taskState_;
    ApproveHandler approveHandler_;
    RejectHandler rejectHandler_;
    RunHandler runHandler_;
    ImportHandler importHandler_;
    int approvedSampleCount_ = 0;
};
