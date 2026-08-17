#include "workspace/knowledgecenterpage.h"

#include "ui/uikit.h"

#include <QBrush>
#include <QColor>
#include <QDir>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QStringList>
#include <QTableWidget>
#include <QVBoxLayout>

namespace {

// 表格列位。错误列固定在最后一列（smoke 按 (row, 7) 取值），只做显隐不做挪位。
enum Column
{
    ColumnSourceId = 0,
    ColumnName,
    ColumnType,
    ColumnManifest,
    ColumnIndex,
    ColumnChunks,
    ColumnPath,
    ColumnError,
    ColumnCount
};

QString displayManifestStatus(const QString &status)
{
    if (status == QStringLiteral("allowed")) {
        return QStringLiteral("白名单");
    }
    if (status == QStringLiteral("missing")) {
        return QStringLiteral("文件缺失");
    }
    return QStringLiteral("已忽略");
}

QString displayIndexStatus(const QString &status)
{
    if (status == QStringLiteral("indexed")) {
        return QStringLiteral("已索引");
    }
    if (status == QStringLiteral("stale")) {
        return QStringLiteral("待重建");
    }
    if (status == QStringLiteral("error")) {
        return QStringLiteral("错误");
    }
    return QStringLiteral("未索引");
}

/// 统一成 '/' 分隔并去掉尾部分隔符，方便做前缀比较。
QString normalizedPath(const QString &path)
{
    QString normalized = QDir::fromNativeSeparators(path.trimmed());
    while (normalized.size() > 1 && normalized.endsWith(QLatin1Char('/'))) {
        normalized.chop(1);
    }
    return normalized;
}

/// 所有来源共同的父目录。没传项目根时用它兜底，避免每行都显示同一段绝对路径。
/// 只剩盘符（或只有一条记录）时返回空，交给「父目录/文件名」的更保守写法。
QString commonDirectoryPrefix(const QVector<KnowledgeSourceRecord> &records)
{
    if (records.size() < 2) {
        return {};
    }
    QStringList shared;
    bool first = true;
    for (const KnowledgeSourceRecord &record : records) {
        if (record.path.isEmpty()) {
            return {};
        }
        QStringList parts = normalizedPath(record.path).split(QLatin1Char('/'));
        parts.removeLast(); // 去掉文件名，只比目录
        if (first) {
            shared = parts;
            first = false;
            continue;
        }
        int keep = 0;
        while (keep < shared.size() && keep < parts.size() && shared.at(keep) == parts.at(keep)) {
            ++keep;
        }
        shared = shared.mid(0, keep);
    }
    return shared.size() < 2 ? QString() : shared.join(QLatin1Char('/'));
}

/// 状态文案过长时收尾，完整文案进 tooltip；标签本身不能被布局挤扁。
QString clampStatusText(const QString &text)
{
    constexpr int kMaxChars = 40;
    return text.size() <= kMaxChars ? text : text.left(kMaxChars - 1) + QStringLiteral("…");
}

// countTone 已收进 ragui（uikit.h），本地副本删除以免 ADL 二义。

} // namespace

KnowledgeCenterPage::KnowledgeCenterPage(QWidget *parent)
    : QWidget(parent)
{
    const ragui::RagMetrics metrics;
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    // 三块内容靠留白分层，不靠线：页头（自带 8px 下留白）→24→ 计数条 →16→ 表格。
    // 计数条离表格比离页头近，因为它是表格的摘要而不是页头的附属。
    layout->setSpacing(metrics.space4);
    layout->addWidget(createToolbar());
    layout->addWidget(createSummaryRow());
    layout->addWidget(createSourceTable(), 1);
    setReindexPhase(ReindexPhase::Idle);
    updateSummary();
    updateEmptyState();
    updateActionState();
}

void KnowledgeCenterPage::setRefreshHandler(RefreshHandler handler)
{
    refreshHandler_ = std::move(handler);
}

void KnowledgeCenterPage::setReindexHandler(ReindexHandler handler)
{
    reindexHandler_ = std::move(handler);
}

void KnowledgeCenterPage::setProjectRoot(const QString &root)
{
    projectRoot_ = root;
    if (!records_.isEmpty()) {
        const QVector<KnowledgeSourceRecord> snapshot = records_;
        setRecords(snapshot);
    } else {
        updatePathBase();
    }
}

void KnowledgeCenterPage::setRecords(const QVector<KnowledgeSourceRecord> &records)
{
    records_ = records;
    updatePathBase();

    const ragui::RagPalette palette;
    const QBrush warningBrush(QColor::fromString(palette.warningFg));
    const QBrush errorBrush(QColor::fromString(palette.errorFg));

    bool hasError = false;
    sourceTable_->setUpdatesEnabled(false);
    sourceTable_->clearContents();
    sourceTable_->setRowCount(records_.size());
    for (int row = 0; row < records_.size(); ++row) {
        const KnowledgeSourceRecord &record = records_.at(row);
        const QString fullPath = QDir::toNativeSeparators(record.path);

        auto *sourceId = ragui::makeTextItem(
            record.sourceId.isEmpty() ? QStringLiteral("-") : record.sourceId,
            record.sourceId.isEmpty() ? QStringLiteral("未登记到 Source Manifest") : record.sourceId);
        auto *name = ragui::makeTextItem(record.displayName);
        auto *type = ragui::makeTextItem(record.sourceType.toUpper());
        auto *manifest = ragui::makeTextItem(displayManifestStatus(record.manifestStatus));
        auto *index = ragui::makeTextItem(displayIndexStatus(record.indexStatus));
        auto *chunks = ragui::makeNumericItem(QString::number(record.chunks));
        // 每行都显示同一段绝对路径等于没信息：这里只留相对部分，全路径进 tooltip。
        auto *path = ragui::makeTextItem(displayPath(record.path), fullPath);
        auto *error = ragui::makeTextItem(record.error);

        if (record.manifestStatus == QStringLiteral("missing")) {
            manifest->setForeground(warningBrush);
        }
        if (record.indexStatus == QStringLiteral("stale")) {
            index->setForeground(warningBrush);
        } else if (record.indexStatus == QStringLiteral("error")) {
            index->setForeground(errorBrush);
        }
        if (!record.error.isEmpty()) {
            error->setForeground(errorBrush);
            hasError = true;
        }

        sourceTable_->setItem(row, ColumnSourceId, sourceId);
        sourceTable_->setItem(row, ColumnName, name);
        sourceTable_->setItem(row, ColumnType, type);
        sourceTable_->setItem(row, ColumnManifest, manifest);
        sourceTable_->setItem(row, ColumnIndex, index);
        sourceTable_->setItem(row, ColumnChunks, chunks);
        sourceTable_->setItem(row, ColumnPath, path);
        sourceTable_->setItem(row, ColumnError, error);
    }
    // 错误列全空时只是白占宽度，收起来把空间还给资料名称。
    sourceTable_->setColumnHidden(ColumnError, !hasError);
    sourceTable_->setUpdatesEnabled(true);

    emptyStateLabel_->setText(QStringLiteral("尚未获取知识源清单，点击刷新状态"));
    updateEmptyState();
    updateSummary();
}

void KnowledgeCenterPage::setTaskState(const WorkspaceTaskState &state)
{
    taskState_ = state;
    updateActionState();
}

void KnowledgeCenterPage::setReindexPhase(ReindexPhase phase, const QString &message)
{
    reindexPhase_ = phase;
    QString text = message;
    ragui::Tone tone = ragui::Tone::Neutral;
    if (phase == ReindexPhase::Running) {
        tone = ragui::Tone::Warning;
    } else if (phase == ReindexPhase::Succeeded) {
        tone = ragui::Tone::Success;
    } else if (phase == ReindexPhase::Failed) {
        tone = ragui::Tone::Error;
    }
    if (text.isEmpty()) {
        if (phase == ReindexPhase::Running) {
            text = QStringLiteral("索引重建中");
        } else if (phase == ReindexPhase::Succeeded) {
            text = QStringLiteral("索引重建完成");
        } else if (phase == ReindexPhase::Failed) {
            text = QStringLiteral("索引重建失败");
        } else {
            text = QStringLiteral("等待刷新来源状态");
        }
    }
    statusLabel_->setToolTip(text);
    ragui::setTag(statusLabel_, clampStatusText(text), tone);
    updateActionState();
}

void KnowledgeCenterPage::showSourcesUnavailable(const QString &message)
{
    records_.clear();
    sourceTable_->clearContents();
    sourceTable_->setRowCount(0);
    sourceTable_->setColumnHidden(ColumnError, true);
    updatePathBase();
    updateSummary();
    emptyStateLabel_->setText(message.isEmpty()
                                  ? QStringLiteral("知识源清单不可用，请检查 Worker 与 Source Manifest")
                                  : message);
    updateEmptyState();
    setReindexPhase(ReindexPhase::Failed, message);
}

QWidget *KnowledgeCenterPage::createToolbar()
{
    // 页头卡删掉了：标题、说明、状态、两个操作压进一行工具条，把垂直空间还给表格。
    const ragui::RagMetrics metrics;
    auto *row = ragui::makeToolbar(true);
    auto *layout = qobject_cast<QHBoxLayout *>(row->layout());

    auto *title = new QLabel(QStringLiteral("知识源管理"));
    title->setObjectName(QStringLiteral("pageTitle"));
    auto *subtitle = ragui::makeElidedLabel(
        QStringLiteral("查看 Source Manifest、文件状态、索引新鲜度和单文件错误。"),
        QStringLiteral("pageSubtitle"));

    // 状态是一句话，不是一个操作。带底色的胶囊挨着两个真按钮时会被读成"第三个按钮，
    // 而且是禁用的"——填色在这套语言里意味着可点或可编辑。行内状态只换文字颜色，
    // 几何尺寸不随状态变化，切换时按钮不会左右跳。
    statusLabel_ = ragui::makeInlineStatus(QStringLiteral("等待刷新来源状态"));

    refreshButton_ = new QPushButton(QStringLiteral("刷新状态"));
    refreshButton_->setObjectName(QStringLiteral("secondaryButton"));
    refreshButton_->setProperty("testId", QStringLiteral("knowledgeRefresh"));
    refreshButton_->setToolTip(QStringLiteral("重新读取 Source Manifest 与索引状态，不会改动索引。"));

    reindexButton_ = new QPushButton(QStringLiteral("重建 STM32F4 索引"));
    reindexButton_->setObjectName(QStringLiteral("primaryButton"));
    reindexButton_->setProperty("testId", QStringLiteral("knowledgeReindex"));
    reindexButton_->setToolTip(
        QStringLiteral("按 Source Manifest 白名单重建全量索引；这是重建索引的唯一入口。"));

    layout->addWidget(title);
    layout->addWidget(subtitle, 1); // 省略标签吃掉剩余宽度，状态标签变长时按钮不会左右跳。
    layout->addWidget(statusLabel_);
    // 状态文字和按钮之间留出一个明确的间隙：贴着按钮（和按钮之间同样是 8px）时，
    // 三样东西会被当成同一组控件读。
    layout->addSpacing(metrics.space2);
    layout->addWidget(refreshButton_);
    layout->addWidget(reindexButton_);

    connect(refreshButton_, &QPushButton::clicked, this, [this] {
        if (refreshHandler_ && refreshHandler_()) {
            setReindexPhase(reindexPhase_, QStringLiteral("来源状态刷新请求已发送"));
        }
    });
    connect(reindexButton_, &QPushButton::clicked, this, [this] {
        if (reindexHandler_ && reindexHandler_()) {
            setReindexPhase(ReindexPhase::Running);
        }
    });
    return row;
}

QWidget *KnowledgeCenterPage::createSummaryRow()
{
    // 计数条不再是一张卡：卡片和表格二选一，这里只是一排贴边的标签。
    auto *row = ragui::makeToolbar();
    auto *layout = qobject_cast<QHBoxLayout *>(row->layout());
    layout->setSpacing(ragui::RagMetrics().space1 + 2);

    allowedLabel_ = ragui::makeTag();
    allowedLabel_->setToolTip(QStringLiteral("Source Manifest 白名单内的资料数"));
    indexedLabel_ = ragui::makeTag();
    indexedLabel_->setToolTip(QStringLiteral("已经进入索引、可被检索的资料数"));
    staleLabel_ = ragui::makeTag();
    staleLabel_->setToolTip(QStringLiteral("文件比索引新，需要重建索引"));
    missingLabel_ = ragui::makeTag();
    missingLabel_->setToolTip(QStringLiteral("Manifest 里登记了但磁盘上找不到的文件"));
    ignoredLabel_ = ragui::makeTag();
    ignoredLabel_->setToolTip(QStringLiteral("不在白名单内、不会被索引的文件"));
    errorLabel_ = ragui::makeTag();
    errorLabel_->setToolTip(QStringLiteral("解析或索引失败的文件，详见表格「错误」列"));

    for (QLabel *label : {allowedLabel_, indexedLabel_, staleLabel_, missingLabel_,
                          ignoredLabel_, errorLabel_}) {
        layout->addWidget(label);
    }
    layout->addStretch(1);
    return row;
}

QWidget *KnowledgeCenterPage::createSourceTable()
{
    sourceTable_ = new QTableWidget;
    sourceTable_->setObjectName(QStringLiteral("knowledgeSourceTable"));
    sourceTable_->setColumnCount(ColumnCount);
    sourceTable_->setHorizontalHeaderLabels({QStringLiteral("Source ID"), QStringLiteral("资料名称"),
                                             QStringLiteral("类型"), QStringLiteral("Manifest"),
                                             QStringLiteral("索引状态"), QStringLiteral("Chunks"),
                                             QStringLiteral("路径"), QStringLiteral("错误")});
    ragui::applyTableDefaults(sourceTable_);

    QHeaderView *header = sourceTable_->horizontalHeader();
    header->setStretchLastSection(false);
    header->setSectionResizeMode(ColumnSourceId, QHeaderView::ResizeToContents);
    header->setSectionResizeMode(ColumnName, QHeaderView::Stretch); // 资料名称是唯一的人读标识，给足宽度
    header->setSectionResizeMode(ColumnType, QHeaderView::ResizeToContents);
    header->setSectionResizeMode(ColumnManifest, QHeaderView::ResizeToContents);
    header->setSectionResizeMode(ColumnIndex, QHeaderView::ResizeToContents);
    header->setSectionResizeMode(ColumnChunks, QHeaderView::ResizeToContents);
    header->setSectionResizeMode(ColumnPath, QHeaderView::Interactive);
    header->setSectionResizeMode(ColumnError, QHeaderView::Interactive);
    sourceTable_->setColumnWidth(ColumnPath, 220);
    sourceTable_->setColumnWidth(ColumnError, 200);
    sourceTable_->setColumnHidden(ColumnError, true);

    emptyStateLabel_ = new ragui::EmptyStateOverlay(
        sourceTable_, QStringLiteral("尚未获取知识源清单，点击刷新状态"));
    emptyStateLabel_->setWordWrap(true);
    return sourceTable_;
}

void KnowledgeCenterPage::updateSummary()
{
    int allowed = 0;
    int indexed = 0;
    int stale = 0;
    int missing = 0;
    int ignored = 0;
    int errors = 0;
    for (const KnowledgeSourceRecord &record : records_) {
        allowed += record.manifestStatus == QStringLiteral("allowed") ? 1 : 0;
        missing += record.manifestStatus == QStringLiteral("missing") ? 1 : 0;
        ignored += record.manifestStatus == QStringLiteral("ignored") ? 1 : 0;
        indexed += record.indexStatus == QStringLiteral("indexed") ? 1 : 0;
        stale += record.indexStatus == QStringLiteral("stale") ? 1 : 0;
        errors += record.indexStatus == QStringLiteral("error") ? 1 : 0;
    }
    ragui::setTag(allowedLabel_, QStringLiteral("白名单 %1").arg(allowed), ragui::Tone::Neutral);
    ragui::setTag(indexedLabel_, QStringLiteral("已索引 %1").arg(indexed),
                  countTone(indexed, ragui::Tone::Success));
    ragui::setTag(staleLabel_, QStringLiteral("待重建 %1").arg(stale),
                  countTone(stale, ragui::Tone::Warning));
    ragui::setTag(missingLabel_, QStringLiteral("缺失 %1").arg(missing),
                  countTone(missing, ragui::Tone::Warning));
    ragui::setTag(ignoredLabel_, QStringLiteral("忽略 %1").arg(ignored), ragui::Tone::Neutral);
    ragui::setTag(errorLabel_, QStringLiteral("错误 %1").arg(errors),
                  countTone(errors, ragui::Tone::Error));
}

void KnowledgeCenterPage::updateActionState()
{
    const bool workerReady = taskState_.workerState == WorkerState::Ready;
    refreshButton_->setEnabled(workerReady);
    reindexButton_->setEnabled(workerReady && !taskState_.taskRunning && reindexPhase_ != ReindexPhase::Running);
}

void KnowledgeCenterPage::updateEmptyState()
{
    if (emptyStateLabel_ && sourceTable_) {
        emptyStateLabel_->setVisible(sourceTable_->rowCount() == 0);
    }
}

void KnowledgeCenterPage::updatePathBase()
{
    pathBase_ = normalizedPath(projectRoot_);
    bool usable = !pathBase_.isEmpty();
    if (usable) {
        usable = false;
        for (const KnowledgeSourceRecord &record : records_) {
            if (normalizedPath(record.path).startsWith(pathBase_ + QLatin1Char('/'),
                                                       Qt::CaseInsensitive)) {
                usable = true;
                break;
            }
        }
    }
    if (!usable) {
        pathBase_ = commonDirectoryPrefix(records_);
    }
    if (QTableWidgetItem *pathHeader = sourceTable_ ? sourceTable_->horizontalHeaderItem(ColumnPath)
                                                    : nullptr) {
        pathHeader->setToolTip(pathBase_.isEmpty()
                                   ? QStringLiteral("完整路径见单元格提示")
                                   : QStringLiteral("相对 %1 的路径；完整路径见单元格提示")
                                         .arg(QDir::toNativeSeparators(pathBase_)));
    }
}

QString KnowledgeCenterPage::displayPath(const QString &path) const
{
    if (path.isEmpty()) {
        return QStringLiteral("-");
    }
    const QString normalized = normalizedPath(path);
    if (!pathBase_.isEmpty()
        && normalized.startsWith(pathBase_ + QLatin1Char('/'), Qt::CaseInsensitive)) {
        return QDir::toNativeSeparators(normalized.mid(pathBase_.size() + 1));
    }
    // 兜底：留「父目录\文件名」，比整条绝对路径可读，完整路径仍在 tooltip 里。
    const int lastSlash = normalized.lastIndexOf(QLatin1Char('/'));
    if (lastSlash <= 0) {
        return QDir::toNativeSeparators(normalized);
    }
    const int parentSlash = normalized.lastIndexOf(QLatin1Char('/'), lastSlash - 1);
    return QDir::toNativeSeparators(parentSlash < 0 ? normalized : normalized.mid(parentSlash + 1));
}
