#pragma once

#include "knowledge/knowledgesourcerecord.h"
#include "workspace/workspacetaskstate.h"

#include <QString>
#include <QWidget>

#include <functional>

class QLabel;
class QPushButton;
class QTableWidget;

enum class ReindexPhase
{
    Idle,
    Running,
    Succeeded,
    Failed
};

class KnowledgeCenterPage final : public QWidget
{
public:
    using RefreshHandler = std::function<bool()>;
    using ReindexHandler = std::function<bool()>;

    explicit KnowledgeCenterPage(QWidget *parent = nullptr);

    void setRefreshHandler(RefreshHandler handler);
    void setReindexHandler(ReindexHandler handler);
    void setRecords(const QVector<KnowledgeSourceRecord> &records);
    void setTaskState(const WorkspaceTaskState &state);
    void setReindexPhase(ReindexPhase phase, const QString &message = {});
    void showSourcesUnavailable(const QString &message);

    /// 新增：告诉页面项目根，路径列据此显示相对路径（完整路径进 tooltip）。
    /// 不调用也能工作——那时会退化成「所有来源的公共目录」或「父目录/文件名」。
    void setProjectRoot(const QString &root);

private:
    QWidget *createToolbar();
    QWidget *createSummaryRow();
    QWidget *createSourceTable();
    void updateSummary();
    void updateActionState();
    void updateEmptyState();
    void updatePathBase();
    QString displayPath(const QString &path) const;

    QLabel *statusLabel_ = nullptr;
    QLabel *allowedLabel_ = nullptr;
    QLabel *indexedLabel_ = nullptr;
    QLabel *staleLabel_ = nullptr;
    QLabel *missingLabel_ = nullptr;
    QLabel *ignoredLabel_ = nullptr;
    QLabel *errorLabel_ = nullptr;
    QLabel *emptyStateLabel_ = nullptr;
    QPushButton *refreshButton_ = nullptr;
    QPushButton *reindexButton_ = nullptr;
    QTableWidget *sourceTable_ = nullptr;
    QVector<KnowledgeSourceRecord> records_;
    WorkspaceTaskState taskState_;
    ReindexPhase reindexPhase_ = ReindexPhase::Idle;
    QString projectRoot_;
    QString pathBase_;
    RefreshHandler refreshHandler_;
    ReindexHandler reindexHandler_;
};
