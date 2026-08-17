#include "workspace/evaluationcenterpage.h"

#include "ui/uikit.h"

#include <QAbstractItemView>
#include <QFrame>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QItemSelectionModel>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonValue>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSplitter>
#include <QSpinBox>
#include <QStringList>
#include <QTableWidget>
#include <QVBoxLayout>

#include <algorithm>
#include <functional>

namespace {

/// 「还没有这个数」的统一写法。
///
/// 以前这里是 em dash：36px 的指标字号把它渲染成一条 20px 的横杠，三张卡并排就是三条横线，
/// 读起来像分隔线而不是「没有数据」。改成两个字之后，占位本身就是一句话，
/// 再配合 QLabel[metric="value"][empty="true"]（淡色 + 常规字重）退到背景里。
/// 指标区永远不用 0 / 0.0% 冒充「还没跑过」——那是产品级要求，不是版式偏好。
QString emptyMetric()
{
    return QStringLiteral("暂无");
}

QString displayFeedback(const QString &feedback)
{
    if (feedback == QStringLiteral("helpful")) return QStringLiteral("有帮助");
    if (feedback == QStringLiteral("unhelpful")) return QStringLiteral("没帮助");
    return QStringLiteral("未评价");
}

QString displayStatus(const QString &status)
{
    if (status == QStringLiteral("approved")) return QStringLiteral("已批准");
    if (status == QStringLiteral("rejected")) return QStringLiteral("已拒绝");
    return QStringLiteral("待审核");
}

/// 状态标签是一个 tag，塞进长 query / 长错误信息会把工具条撑爆。
/// 这里主动截断并把全文交给 tooltip，而不是让布局去裁中文。
QString shorten(const QString &text, int limit)
{
    const QString trimmed = text.trimmed();
    return trimmed.size() <= limit ? trimmed : trimmed.left(limit) + QChar(0x2026);
}

/// JSON 里 page / rank 有时是数字有时是字符串，统一成显示文本。
QString jsonText(const QJsonValue &value)
{
    if (value.isString()) return value.toString().trimmed();
    if (value.isDouble()) {
        const double number = value.toDouble();
        return qFuzzyIsNull(number - qRound(number)) ? QString::number(qRound(number))
                                                     : QString::number(number, 'g', 6);
    }
    return QString();
}

/// 单条证据的人类可读形式：优先 chunk_id，其次 source / doc_id；再补 section 或页码。
/// 例：core-cfsr · SCB_Type。完整字段由调用方放进 tooltip。
QString formatEvidenceRef(const QJsonObject &reference, bool withRank)
{
    QStringList parts;
    if (withRank) {
        const QString rank = jsonText(reference.value(QStringLiteral("rank")));
        if (!rank.isEmpty()) parts << QStringLiteral("#%1").arg(rank);
    }
    const QString chunkId = jsonText(reference.value(QStringLiteral("chunk_id")));
    const QString source = jsonText(reference.value(QStringLiteral("source")));
    const QString docId = jsonText(reference.value(QStringLiteral("doc_id")));
    const QString section = jsonText(reference.value(QStringLiteral("section")));
    const QString page = jsonText(reference.value(QStringLiteral("page")));
    if (!chunkId.isEmpty()) parts << chunkId;
    else if (!source.isEmpty()) parts << source;
    else if (!docId.isEmpty()) parts << docId;
    if (!section.isEmpty()) parts << section;
    else if (!page.isEmpty()) parts << QStringLiteral("p.%1").arg(page);
    if (parts.isEmpty()) return QStringLiteral("(无可追溯字段)");
    return parts.join(QStringLiteral(" · "));
}

QString formatEvidenceList(const QJsonArray &references, bool withRank)
{
    QStringList items;
    items.reserve(references.size());
    for (const QJsonValue &value : references) items << formatEvidenceRef(value.toObject(), withRank);
    return items.isEmpty() ? QStringLiteral("(空)") : items.join(QStringLiteral("、"));
}

/// 原始 JSON 进 tooltip：读得懂的在单元格里，查得清的在悬浮里。
QString jsonTooltip(const QJsonArray &references)
{
    if (references.isEmpty()) return QStringLiteral("[]");
    QString text = QString::fromUtf8(QJsonDocument(references).toJson(QJsonDocument::Indented)).trimmed();
    if (text.size() > 1600) text = text.left(1600) + QStringLiteral("\n…");
    return text;
}

/// 指标只在 result 里真的存在数值时才渲染，缺键一律回落到空态。
QString formatPercent(const QJsonObject &result, const QString &key)
{
    const QJsonValue value = result.value(key);
    if (!value.isDouble()) return QString();
    return QStringLiteral("%1%").arg(value.toDouble() * 100.0, 0, 'f', 1);
}

QString formatScore(const QJsonObject &result, const QString &key)
{
    const QJsonValue value = result.value(key);
    if (!value.isDouble()) return QString();
    return QString::number(value.toDouble(), 'f', 3);
}

} // namespace

EvaluationCenterPage::EvaluationCenterPage(QWidget *parent)
    : QWidget(parent)
{
    const ragui::RagMetrics metrics;

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(metrics.space4);

    // 页头减重：导航栏已经标明当前位置，这里只剩一条工具条，
    // 左边页名、右边承载 showFailure() / showProgress() 的状态 tag。
    auto *toolbar = ragui::makeToolbar(true);
    auto *toolbarLayout = qobject_cast<QHBoxLayout *>(toolbar->layout());
    auto *title = new QLabel(QStringLiteral("评估中心"));
    title->setObjectName(QStringLiteral("pageTitle"));
    toolbarLayout->addWidget(title);
    toolbarLayout->addStretch(1);
    statusLabel_ = ragui::makeTag(QStringLiteral("尚未建立正式评估集"), ragui::Tone::Warning);
    toolbarLayout->addWidget(statusLabel_, 0, Qt::AlignVCenter);
    layout->addWidget(toolbar);

    // 记分板通栏：三张指标卡是这一页的主角，放在最上面，谁都不用滚动去找。
    layout->addWidget(buildResultBand());

    // 左列是两张「待处理 query 清单」，右列是工作面。这样两列的高度需求各自减半，
    // 1440×900 下不需要滚动区。
    auto *splitter = new QSplitter(Qt::Horizontal);
    splitter->setObjectName(QStringLiteral("evaluationCenterSplitter"));
    splitter->setChildrenCollapsible(false);
    splitter->addWidget(buildQueueColumn());
    splitter->addWidget(buildWorkColumn());
    splitter->setStretchFactor(0, 1);
    splitter->setStretchFactor(1, 1);
    // 两列都要宽度：左边 Failed Cases 三列都是长文本，右边证据表有六列。对半分最公平。
    splitter->setSizes({556, 588});
    layout->addWidget(splitter, 1);

    connect(candidateTable_, &QTableWidget::itemSelectionChanged, this, [this] {
        selectedRow_ = candidateTable_->currentRow();
        updateCandidateDetail();
        updateActionState();
    });
    connect(addEvidenceButton_, &QPushButton::clicked, this, [this] {
        addEvidenceRow();
        evidenceTable_->selectRow(evidenceTable_->rowCount() - 1);
        evidenceTable_->setCurrentCell(evidenceTable_->rowCount() - 1, 1);
        evidenceTable_->editItem(evidenceTable_->item(evidenceTable_->rowCount() - 1, 1));
        updateActionState();
    });
    connect(removeEvidenceButton_, &QPushButton::clicked, this, [this] {
        removeSelectedEvidenceRows();
        updateActionState();
    });
    connect(evidenceTable_->selectionModel(), &QItemSelectionModel::selectionChanged,
            this, [this] { updateActionState(); });
    connect(approveButton_, &QPushButton::clicked, this, [this] {
        if (approveHandler_ && approveHandler_(editedCandidate())) {
            showStatus(QStringLiteral("候选已批准为正式样本"), ragui::Tone::Success);
        }
    });
    connect(rejectButton_, &QPushButton::clicked, this, [this] {
        const EvaluationCandidate *candidate = selectedCandidate();
        if (candidate && rejectHandler_ && rejectHandler_(candidate->id)) {
            showStatus(QStringLiteral("候选已拒绝"), ragui::Tone::Success);
        }
    });
    connect(runButton_, &QPushButton::clicked, this, [this] {
        if (runHandler_) runHandler_(topKSpin_->value());
    });
    connect(importButton_, &QPushButton::clicked, this, [this] {
        if (importHandler_) importHandler_();
    });

    clearLatestRun();
    updateActionState();
}

QWidget *EvaluationCenterPage::buildResultBand()
{
    const ragui::RagMetrics metrics;
    auto *band = new QWidget;
    auto *bandLayout = new QVBoxLayout(band);
    bandLayout->setContentsMargins(0, 0, 0, 0);
    bandLayout->setSpacing(metrics.space3);

    // 区块名 + 本次运行的快照共用一行：快照是长文本（模型名、耗时、完成时间），
    // 用 ElidedLabel 才不会把整页的最小宽度顶起来，全文在 tooltip 里。
    auto *headRow = new QWidget;
    auto *headLayout = new QHBoxLayout(headRow);
    headLayout->setContentsMargins(0, 0, 0, 0);
    headLayout->setSpacing(metrics.space3);
    auto *headTitle = new QLabel(QStringLiteral("最近一次运行"));
    headTitle->setObjectName(QStringLiteral("sectionTitle"));
    headLayout->addWidget(headTitle);
    runMetaLabel_ = ragui::makeElidedLabel(QString(), QStringLiteral("evaluationRunMeta"));
    // objectName 被测试选择器占着，所以外观走 metric 动态属性（淡灰 13px），
    // 而不是再起一个 #mutedText 的名字。
    runMetaLabel_->setProperty("metric", QStringLiteral("caption"));
    headLayout->addWidget(runMetaLabel_, 1);
    bandLayout->addWidget(headRow);

    // 三张卡同款：同一个淡薄荷图标片、同一种字号排布。
    // 图标片不上语义色——绿色的 Hit Rate 会被读成「这个数很好」，而它只是个名字。
    auto *cards = new QWidget;
    auto *cardsLayout = new QHBoxLayout(cards);
    cardsLayout->setContentsMargins(0, 0, 0, 0);
    cardsLayout->setSpacing(metrics.space4);
    const ragui::StatCard hitCard =
        ragui::makeStatCard(QStringLiteral("Hit Rate"), ragui::Glyph::Chart, ragui::Tone::Neutral,
                            QStringLiteral("metricHitRate"), emptyMetric());
    const ragui::StatCard recallCard =
        ragui::makeStatCard(QStringLiteral("Recall"), ragui::Glyph::List, ragui::Tone::Neutral,
                            QStringLiteral("metricRecall"), emptyMetric());
    const ragui::StatCard mrrCard =
        ragui::makeStatCard(QStringLiteral("MRR"), ragui::Glyph::Pulse, ragui::Tone::Neutral,
                            QStringLiteral("metricMrr"), emptyMetric());
    hitRateValue_ = hitCard.value;
    recallValue_ = recallCard.value;
    mrrValue_ = mrrCard.value;
    hitRateCaption_ = hitCard.caption;
    recallCaption_ = recallCard.caption;
    mrrCaption_ = mrrCard.caption;
    cardsLayout->addWidget(hitCard.card, 1);
    cardsLayout->addWidget(recallCard.card, 1);
    cardsLayout->addWidget(mrrCard.card, 1);
    bandLayout->addWidget(cards);

    metricsBand_ = band;
    return band;
}

QWidget *EvaluationCenterPage::buildQueueColumn()
{
    const ragui::RagMetrics metrics;
    auto *column = new QWidget;
    // 最小宽度决定整页的最小宽度（主窗最小 1080 → 内容区约 780），
    // 两列加起来必须留在这个预算里，否则窗口一缩就横向裁字。
    column->setMinimumWidth(280);
    auto *columnLayout = new QVBoxLayout(column);
    columnLayout->setContentsMargins(0, 0, 0, 0);
    columnLayout->setSpacing(metrics.space2);
    columnLayout->addWidget(buildCandidateSection(), 3);
    // 段与段之间用留白分组，不画线：#hDivider 是 #EEF2F3，压在白面板上根本看不见，
    // 只会白占 1px 并且两列的线还对不齐。theme.h 的原话是「区块之间用间距代替分隔线」。
    columnLayout->addSpacing(metrics.space3);
    columnLayout->addWidget(buildFailedSection(), 2);
    return column;
}

QWidget *EvaluationCenterPage::buildWorkColumn()
{
    const ragui::RagMetrics metrics;
    auto *column = new QWidget;
    // 6 列的证据表和「导入 / top-k / 运行」这一行都在这里，最小宽度比左列高。
    column->setMinimumWidth(460);
    auto *columnLayout = new QVBoxLayout(column);
    columnLayout->setContentsMargins(0, 0, 0, 0);
    columnLayout->setSpacing(metrics.space2);
    columnLayout->addWidget(buildReviewSection(), 1);
    columnLayout->addSpacing(metrics.space3);
    columnLayout->addWidget(buildRunSection(), 0);
    return column;
}

QWidget *EvaluationCenterPage::buildCandidateSection()
{
    const ragui::RagMetrics metrics;
    auto *section = new QWidget;
    auto *sectionLayout = new QVBoxLayout(section);
    sectionLayout->setContentsMargins(0, 0, 0, 0);
    sectionLayout->setSpacing(metrics.space2);

    // 四个区块的标题行同款：标题 + 灰色说明 + 右侧一个状态 tag。
    // tag 让空态的表头也带着信息，不至于「一片空白 + 一行提示」。
    candidateSummaryLabel_ = ragui::makeTag(QStringLiteral("暂无候选"), ragui::Tone::Neutral);
    sectionLayout->addWidget(ragui::makeSectionHeader(QStringLiteral("反馈候选"),
                                                      QStringLiteral("标记只是线索，需人工确认"),
                                                      candidateSummaryLabel_));

    candidateTable_ = new QTableWidget;
    candidateTable_->setObjectName(QStringLiteral("evaluationCandidateTable"));
    candidateTable_->setColumnCount(4);
    candidateTable_->setHorizontalHeaderLabels({QStringLiteral("状态"), QStringLiteral("反馈"),
                                                QStringLiteral("Query"), QStringLiteral("证据数")});
    ragui::applyTableDefaults(candidateTable_);
    candidateTable_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    candidateTable_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    candidateTable_->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    candidateTable_->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    // 表头 38 + 两行 40：再矮空态提示就会贴到分隔线上。
    candidateTable_->setMinimumHeight(118);
    sectionLayout->addWidget(candidateTable_, 1);
    candidateEmpty_ = new ragui::EmptyStateOverlay(
        candidateTable_, QStringLiteral("还没有反馈候选，先在案例库里标记有帮助/没帮助"));
    return section;
}

QWidget *EvaluationCenterPage::buildFailedSection()
{
    const ragui::RagMetrics metrics;
    auto *section = new QWidget;
    auto *sectionLayout = new QVBoxLayout(section);
    sectionLayout->setContentsMargins(0, 0, 0, 0);
    sectionLayout->setSpacing(metrics.space2);

    failedSummaryLabel_ = ragui::makeTag(QStringLiteral("尚未运行"), ragui::Tone::Neutral);
    sectionLayout->addWidget(ragui::makeSectionHeader(
        QStringLiteral("Failed Cases"), QStringLiteral("top-k 内没召回正确证据的 query"),
        failedSummaryLabel_));

    failedCasesTable_ = new QTableWidget;
    failedCasesTable_->setObjectName(QStringLiteral("evaluationFailedCasesTable"));
    failedCasesTable_->setColumnCount(3);
    failedCasesTable_->setHorizontalHeaderLabels({QStringLiteral("Query"), QStringLiteral("正确证据"),
                                                  QStringLiteral("实际 Top-k")});
    ragui::applyTableDefaults(failedCasesTable_);
    failedCasesTable_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    failedCasesTable_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    failedCasesTable_->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    failedCasesTable_->setMinimumHeight(118);
    sectionLayout->addWidget(failedCasesTable_, 1);
    failedEmptyText_ = QStringLiteral("尚无已完成运行");
    failedEmpty_ = new ragui::EmptyStateOverlay(failedCasesTable_, failedEmptyText_);
    return section;
}

QWidget *EvaluationCenterPage::buildReviewSection()
{
    const ragui::RagMetrics metrics;
    auto *section = new QWidget;
    auto *sectionLayout = new QVBoxLayout(section);
    sectionLayout->setContentsMargins(0, 0, 0, 0);
    sectionLayout->setSpacing(metrics.space2);

    // tag 说的是「当前候选处于哪一步」，顺带解释了下面几个按钮为什么是灰的。
    reviewStatusLabel_ = ragui::makeTag(QStringLiteral("未选择候选"), ragui::Tone::Neutral);
    sectionLayout->addWidget(ragui::makeSectionHeader(
        QStringLiteral("人工审核"), QStringLiteral("确认 Query 与正确证据后才能批准"),
        reviewStatusLabel_));

    // 「评估 Query」这个字段名并进 section caption，省一行；输入框本身有 placeholder 和 tooltip。
    queryEdit_ = new QLineEdit;
    queryEdit_->setObjectName(QStringLiteral("evaluationQueryEdit"));
    queryEdit_->setPlaceholderText(QStringLiteral("评估 Query：请输入可独立复现的检索问题"));
    queryEdit_->setToolTip(QStringLiteral("评估 Query"));
    sectionLayout->addWidget(queryEdit_);

    // 证据表的两个编辑按钮跟着表头走，而不是在表格下面另起一行：
    // 一来它们是这张表的工具而不是页面级操作，二来空态时不会有两枚灰胶囊浮在空白里。
    auto *evidenceHead = new QWidget;
    auto *evidenceHeadLayout = new QHBoxLayout(evidenceHead);
    evidenceHeadLayout->setContentsMargins(0, 0, 0, 0);
    evidenceHeadLayout->setSpacing(metrics.space2);
    auto *evidenceLabel = new QLabel(QStringLiteral("正确 Evidence"));
    evidenceLabel->setObjectName(QStringLiteral("mutedText"));
    auto *evidenceHint = ragui::makeElidedLabel(QStringLiteral("可勾选、编辑或补充"),
                                                QStringLiteral("faintText"));
    addEvidenceButton_ = new QPushButton(QStringLiteral("新增引用"));
    addEvidenceButton_->setProperty("testId", QStringLiteral("evaluationEvidenceAdd"));
    addEvidenceButton_->setToolTip(QStringLiteral("新增一条 Expected Evidence 引用"));
    removeEvidenceButton_ = new QPushButton(QStringLiteral("删除选中"));
    removeEvidenceButton_->setProperty("testId", QStringLiteral("evaluationEvidenceRemove"));
    removeEvidenceButton_->setToolTip(QStringLiteral("删除选中的 Expected Evidence 引用"));
    evidenceHeadLayout->addWidget(evidenceLabel);
    evidenceHeadLayout->addWidget(evidenceHint, 1);
    evidenceHeadLayout->addWidget(addEvidenceButton_);
    evidenceHeadLayout->addWidget(removeEvidenceButton_);
    sectionLayout->addWidget(evidenceHead);

    evidenceTable_ = new QTableWidget;
    evidenceTable_->setObjectName(QStringLiteral("evaluationEvidenceTable"));
    evidenceTable_->setColumnCount(6);
    evidenceTable_->setHorizontalHeaderLabels({QStringLiteral("使用"), QStringLiteral("来源"),
                                               QStringLiteral("Doc ID"), QStringLiteral("页码"),
                                               QStringLiteral("章节"), QStringLiteral("Chunk ID")});
    ragui::applyTableDefaults(evidenceTable_, /*singleSelection=*/false);
    // applyTableDefaults 默认只读单选；这张表是审核的工作面，必须能改、能多选。
    evidenceTable_->setEditTriggers(QAbstractItemView::DoubleClicked | QAbstractItemView::EditKeyPressed
                                    | QAbstractItemView::SelectedClicked);
    evidenceTable_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    evidenceTable_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    evidenceTable_->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    evidenceTable_->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    evidenceTable_->horizontalHeader()->setSectionResizeMode(4, QHeaderView::Stretch);
    evidenceTable_->horizontalHeader()->setSectionResizeMode(5, QHeaderView::Stretch);
    evidenceTable_->setMinimumHeight(118);
    // 勾选框的外观由 theme.h 的 QTableView::indicator 规则统一管（和 QCheckBox 同一组取值）。
    sectionLayout->addWidget(evidenceTable_, 1);
    evidenceEmpty_ = new ragui::EmptyStateOverlay(evidenceTable_,
                                                  QStringLiteral("先在左侧选择一个反馈候选"));

    // 决策类按钮右对齐，单独占一行——它们是这一段的出口，不该和表格工具混在一起。
    // 「拒绝」不用 danger 变体：QSS 里 variant 比 :disabled 后写，禁用时那块粉色不会退掉，
    // 空态下就变成一枚看起来还能点的粉胶囊。何况拒绝候选并不是不可逆的破坏性操作。
    auto *decisionRow = new QWidget;
    auto *decisionLayout = new QHBoxLayout(decisionRow);
    decisionLayout->setContentsMargins(0, 0, 0, 0);
    decisionLayout->setSpacing(metrics.space2);
    rejectButton_ = new QPushButton(QStringLiteral("拒绝候选"));
    rejectButton_->setProperty("testId", QStringLiteral("evaluationReject"));
    approveButton_ = new QPushButton(QStringLiteral("批准为正式样本"));
    approveButton_->setObjectName(QStringLiteral("primaryButton"));
    approveButton_->setProperty("testId", QStringLiteral("evaluationApprove"));
    decisionLayout->addStretch(1);
    decisionLayout->addWidget(rejectButton_);
    decisionLayout->addWidget(approveButton_);
    sectionLayout->addWidget(decisionRow);
    return section;
}

QWidget *EvaluationCenterPage::buildRunSection()
{
    const ragui::RagMetrics metrics;
    auto *section = new QWidget;
    auto *sectionLayout = new QVBoxLayout(section);
    sectionLayout->setContentsMargins(0, 0, 0, 0);
    sectionLayout->setSpacing(metrics.space2);

    // 已批准样本数决定能不能运行，所以它属于这一段，而不是审核段的尾巴。
    sampleSummaryLabel_ = ragui::makeTag(QStringLiteral("已批准 0 个样本"), ragui::Tone::Neutral);
    sectionLayout->addWidget(ragui::makeSectionHeader(QStringLiteral("运行评估"),
                                                      QStringLiteral("只跑已批准样本"),
                                                      sampleSummaryLabel_));

    auto *runRow = new QWidget;
    auto *runLayout = new QHBoxLayout(runRow);
    runLayout->setContentsMargins(0, 0, 0, 0);
    runLayout->setSpacing(metrics.space2);
    importButton_ = new QPushButton(QStringLiteral("导入评估 JSON"));
    importButton_->setProperty("testId", QStringLiteral("evaluationImport"));
    topKSpin_ = new QSpinBox;
    topKSpin_->setRange(1, 20);
    topKSpin_->setValue(5);
    topKSpin_->setPrefix(QStringLiteral("top-k = "));
    topKSpin_->setProperty("testId", QStringLiteral("evaluationTopK"));
    runButton_ = new QPushButton(QStringLiteral("运行检索评估"));
    runButton_->setObjectName(QStringLiteral("primaryButton"));
    runButton_->setProperty("testId", QStringLiteral("evaluationRun"));
    runLayout->addWidget(importButton_);
    runLayout->addStretch(1);
    runLayout->addWidget(topKSpin_);
    runLayout->addWidget(runButton_);
    sectionLayout->addWidget(runRow);
    return section;
}

void EvaluationCenterPage::addEvidenceRow(const ExpectedEvidenceRef &reference)
{
    const int row = evidenceTable_->rowCount();
    evidenceTable_->insertRow(row);
    auto *enabled = new QTableWidgetItem;
    enabled->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsUserCheckable);
    enabled->setCheckState(Qt::Checked);
    evidenceTable_->setItem(row, 0, enabled);
    evidenceTable_->setItem(row, 1, ragui::makeTextItem(reference.source));
    evidenceTable_->setItem(row, 2, ragui::makeTextItem(reference.docId));
    evidenceTable_->setItem(row, 3, ragui::makeTextItem(reference.page));
    evidenceTable_->setItem(row, 4, ragui::makeTextItem(reference.section));
    evidenceTable_->setItem(row, 5, ragui::makeTextItem(reference.chunkId));
    updateEmptyStates();
}

void EvaluationCenterPage::removeSelectedEvidenceRows()
{
    const QModelIndexList selectedRows = evidenceTable_->selectionModel()->selectedRows();
    QList<int> rows;
    rows.reserve(selectedRows.size());
    for (const QModelIndex &index : selectedRows) rows.append(index.row());
    std::sort(rows.begin(), rows.end(), std::greater<int>());
    for (int row : rows) evidenceTable_->removeRow(row);
    updateEmptyStates();
}

void EvaluationCenterPage::setCandidates(const QVector<EvaluationCandidate> &candidates)
{
    const QString selectedId = selectedCandidate() ? selectedCandidate()->id : QString();
    candidates_ = candidates;
    candidateTable_->clearContents();
    candidateTable_->setRowCount(candidates_.size());
    selectedRow_ = -1;
    int pending = 0;
    for (int row = 0; row < candidates_.size(); ++row) {
        const EvaluationCandidate &candidate = candidates_.at(row);
        candidateTable_->setItem(row, 0, ragui::makeTextItem(displayStatus(candidate.reviewStatus)));
        candidateTable_->setItem(row, 1, ragui::makeTextItem(displayFeedback(candidate.userFeedback)));
        candidateTable_->setItem(row, 2, ragui::makeTextItem(candidate.query));
        candidateTable_->setItem(row, 3,
                                 ragui::makeNumericItem(QString::number(candidate.expectedEvidence.size())));
        if (candidate.reviewStatus == QStringLiteral("candidate")) ++pending;
        if (candidate.id == selectedId) selectedRow_ = row;
    }
    // 计数 tag：0 一律退成中性灰，只有真的有待办才上薄荷色。
    ragui::setTag(candidateSummaryLabel_,
                  candidates_.isEmpty()
                      ? QStringLiteral("暂无候选")
                      : QStringLiteral("待审核 %1 / 共 %2").arg(pending).arg(candidates_.size()),
                  ragui::countTone(pending, ragui::Tone::Info));
    if (selectedRow_ < 0 && !candidates_.isEmpty()) selectedRow_ = 0;
    if (selectedRow_ >= 0) candidateTable_->selectRow(selectedRow_);
    updateCandidateDetail();
    updateActionState();
}

void EvaluationCenterPage::setApprovedSampleCount(int count)
{
    approvedSampleCount_ = count;
    ragui::setTag(sampleSummaryLabel_, QStringLiteral("已批准 %1 个样本").arg(count),
                  ragui::countTone(count, ragui::Tone::Info));
    sampleSummaryLabel_->setToolTip(count > 0
                                        ? QStringLiteral("只有人工批准的样本会进入正式检索指标。")
                                        : QStringLiteral("还没有已批准样本，无法运行评估。"
                                                         "helpful / unhelpful 标记不会自动成为 ground truth。"));
    updateActionState();
}

void EvaluationCenterPage::setLatestRun(const EvaluationRun &run)
{
    if (run.status == QStringLiteral("failed")) {
        const QString reason = run.errorMessage.isEmpty() ? QStringLiteral("未知错误") : run.errorMessage;
        const QString line = QStringLiteral("最近一次运行失败：%1").arg(reason);
        setMetricCards(QString(), QString(), QString(), 0, QStringLiteral("本次运行失败"));
        setRunNarrative(QStringLiteral("%1（本次运行未产生成功指标）").arg(line));
        failedCasesTable_->clearContents();
        failedCasesTable_->setRowCount(0);
        failedEmptyText_ = QStringLiteral("本次运行失败，没有失败样本明细");
        setFailedSummary(QStringLiteral("运行失败"), ragui::Tone::Error);
        updateEmptyStates();
        return;
    }

    const QJsonObject result = run.result;
    const QString hitText = formatPercent(result, QStringLiteral("hit_rate@%1").arg(run.topK));
    const QString recallText = formatPercent(result, QStringLiteral("recall@%1").arg(run.topK));
    const QString mrrText = formatScore(result, QStringLiteral("mrr"));

    // 0 样本的「成功」运行不产生有意义的指标，宁可显示空态也不显示 0%。
    const bool hasSamples = run.sampleCount > 0;
    setMetricCards(hasSamples ? hitText : QString(),
                   hasSamples ? recallText : QString(),
                   hasSamples ? mrrText : QString(),
                   hasSamples ? run.topK : 0,
                   hasSamples ? QStringLiteral("本次运行未产生该指标")
                              : QStringLiteral("本次运行没有样本"));

    const QJsonObject runtime = result.value(QStringLiteral("runtime_snapshot")).toObject();
    const QJsonObject models = runtime.value(QStringLiteral("models")).toObject();
    const QJsonObject index = runtime.value(QStringLiteral("index")).toObject();
    const QString embedding =
        models.value(QStringLiteral("embedding")).toObject().value(QStringLiteral("name")).toString();
    const QString reranker =
        models.value(QStringLiteral("reranker")).toObject().value(QStringLiteral("name")).toString();

    QStringList meta;
    meta << QStringLiteral("%1 个样本").arg(run.sampleCount);
    meta << QStringLiteral("top-k=%1").arg(run.topK);
    if (result.value(QStringLiteral("duration_ms")).isDouble()) {
        meta << QStringLiteral("耗时 %1 ms")
                    .arg(result.value(QStringLiteral("duration_ms")).toDouble(), 0, 'f', 1);
    }
    if (!embedding.isEmpty()) meta << QStringLiteral("Embedding %1").arg(embedding);
    if (!reranker.isEmpty()) meta << QStringLiteral("Reranker %1").arg(reranker);
    if (index.contains(QStringLiteral("chunks"))) {
        meta << QStringLiteral("索引 %1 chunks").arg(index.value(QStringLiteral("chunks")).toInt());
    }
    // 时间戳带前缀，这样 0 已批准样本却还显示历史指标时，也能一眼看出那是哪一次跑的。
    if (!run.finishedAt.isEmpty()) {
        meta << QStringLiteral("完成于 %1").arg(run.finishedAt);
    } else if (!index.value(QStringLiteral("updated_at")).toString().isEmpty()) {
        meta << QStringLiteral("索引更新于 %1").arg(index.value(QStringLiteral("updated_at")).toString());
    }
    const QString metaLine = meta.join(QStringLiteral(" · "));

    if (hasSamples) {
        setRunNarrative(metaLine);
    } else {
        setRunNarrative(QStringLiteral("本次运行没有样本，未产生指标 · %1").arg(metaLine));
    }

    const QJsonArray failed = result.value(QStringLiteral("failed_cases")).toArray();
    failedCasesTable_->clearContents();
    failedCasesTable_->setRowCount(failed.size());
    for (int row = 0; row < failed.size(); ++row) {
        const QJsonObject item = failed.at(row).toObject();
        const QJsonArray expected = item.value(QStringLiteral("expected_evidence")).toArray();
        const QJsonArray retrieved = item.value(QStringLiteral("retrieved_evidence")).toArray();
        failedCasesTable_->setItem(row, 0, ragui::makeTextItem(item.value(QStringLiteral("query")).toString()));
        failedCasesTable_->setItem(row, 1, ragui::makeTextItem(formatEvidenceList(expected, false),
                                                               jsonTooltip(expected)));
        failedCasesTable_->setItem(row, 2, ragui::makeTextItem(formatEvidenceList(retrieved, true),
                                                               jsonTooltip(retrieved)));
    }
    failedEmptyText_ = hasSamples ? QStringLiteral("本次运行没有失败样本")
                                  : QStringLiteral("尚无已完成运行");
    if (!hasSamples) {
        setFailedSummary(QStringLiteral("没有样本"), ragui::Tone::Neutral);
    } else if (failed.isEmpty()) {
        setFailedSummary(QStringLiteral("全部召回"), ragui::Tone::Success);
    } else {
        setFailedSummary(QStringLiteral("%1 个未召回").arg(failed.size()), ragui::Tone::Warning);
    }
    updateEmptyStates();
}

void EvaluationCenterPage::clearLatestRun()
{
    // 契约：没有已完成运行时，指标区显示空态占位，绝不显示 0.0% 冒充结果。
    setMetricCards(QString(), QString(), QString(), 0, QStringLiteral("尚无已完成运行"));
    setRunNarrative(QStringLiteral("还没有跑过检索评估 · 先批准候选，再运行"));
    failedCasesTable_->clearContents();
    failedCasesTable_->setRowCount(0);
    failedEmptyText_ = QStringLiteral("尚无已完成运行");
    setFailedSummary(QStringLiteral("尚未运行"), ragui::Tone::Neutral);
    updateEmptyStates();
}

void EvaluationCenterPage::setMetricCards(const QString &hitRate, const QString &recall,
                                          const QString &mrr, int topK, const QString &emptyNote)
{
    // 数值缺席时：占位退成淡色常规字重（empty 动态属性驱动 QSS），
    // 「为什么没有」写进卡片下面那行注解——空态也是一句完整的话，不是一条横杠。
    const auto apply = [&emptyNote](QLabel *value, QLabel *caption, const QString &text,
                                    const QString &note) {
        const bool empty = text.isEmpty();
        value->setText(empty ? emptyMetric() : text);
        ragui::setDynamicProperty(value, "empty", empty ? QStringLiteral("true") : QString());
        caption->setText(empty ? emptyNote : note);
    };
    // Hit Rate / Recall 都是 @k 指标，注解跟着本次运行的 k 走；没有运行时退回指标定义。
    apply(hitRateValue_, hitRateCaption_, hitRate,
          topK > 0 ? QStringLiteral("top-k = %1 内命中").arg(topK)
                   : QStringLiteral("命中正确证据的样本占比"));
    apply(recallValue_, recallCaption_, recall,
          topK > 0 ? QStringLiteral("top-k = %1 内召回").arg(topK)
                   : QStringLiteral("正确证据被召回的比例"));
    apply(mrrValue_, mrrCaption_, mrr, QStringLiteral("正确证据排名的倒数均值"));
}

void EvaluationCenterPage::setRunNarrative(const QString &metaLine)
{
    // setFullText 会把未省略的全文写进 tooltip，指标区直接复用同一份。
    runMetaLabel_->setFullText(metaLine);
    if (metricsBand_) metricsBand_->setToolTip(metaLine);
}

void EvaluationCenterPage::setFailedSummary(const QString &text, ragui::Tone tone)
{
    ragui::setTag(failedSummaryLabel_, text, tone);
}

void EvaluationCenterPage::setApproveHandler(ApproveHandler handler)
{
    approveHandler_ = std::move(handler);
}

void EvaluationCenterPage::setRejectHandler(RejectHandler handler)
{
    rejectHandler_ = std::move(handler);
}

void EvaluationCenterPage::setRunHandler(RunHandler handler)
{
    runHandler_ = std::move(handler);
}

void EvaluationCenterPage::setImportHandler(ImportHandler handler)
{
    importHandler_ = std::move(handler);
}

void EvaluationCenterPage::setTaskState(const WorkspaceTaskState &state)
{
    taskState_ = state;
    if (state.taskRunning) {
        showStatus(QStringLiteral("评估或其他 RAG 任务运行中"), ragui::Tone::Info);
    } else if (state.workerState == WorkerState::Ready) {
        showStatus(QStringLiteral("Worker 已就绪"), ragui::Tone::Success);
    } else {
        showStatus(QStringLiteral("Worker 未就绪"), ragui::Tone::Warning);
    }
    updateActionState();
}

void EvaluationCenterPage::showProgress(const QJsonObject &progress)
{
    const int completed = progress.value(QStringLiteral("completed")).toInt();
    const int total = progress.value(QStringLiteral("total")).toInt();
    const QString query = progress.value(QStringLiteral("current_query")).toString();
    showStatus(QStringLiteral("评估进度 %1/%2 · %3").arg(completed).arg(total).arg(shorten(query, 14)),
               ragui::Tone::Info,
               QStringLiteral("评估进度 %1/%2\n当前 query：%3").arg(completed).arg(total).arg(query));
}

void EvaluationCenterPage::showEvaluationResult(const QJsonObject &result)
{
    EvaluationRun run;
    run.status = QStringLiteral("succeeded");
    run.topK = result.value(QStringLiteral("top_k")).toInt();
    run.sampleCount = result.value(QStringLiteral("sample_count")).toInt();
    run.result = result;
    setLatestRun(run);
    showStatus(QStringLiteral("评估请求完成"), ragui::Tone::Success);
}

void EvaluationCenterPage::showFailure(const QString &message)
{
    showStatus(QStringLiteral("评估不可用：%1").arg(shorten(message, 28)), ragui::Tone::Error,
               QStringLiteral("评估不可用：%1").arg(message));
}

void EvaluationCenterPage::updateCandidateDetail()
{
    const EvaluationCandidate *candidate = selectedCandidate();
    queryEdit_->setText(candidate ? candidate->query : QString());
    queryEdit_->setToolTip(candidate ? QStringLiteral("评估 Query：%1").arg(candidate->query)
                                     : QStringLiteral("评估 Query"));
    evidenceTable_->clearContents();
    evidenceTable_->setRowCount(0);
    if (candidate) {
        for (const ExpectedEvidenceRef &reference : candidate->expectedEvidence) addEvidenceRow(reference);
    }
    updateEmptyStates();
}

void EvaluationCenterPage::updateActionState()
{
    const EvaluationCandidate *candidate = selectedCandidate();
    const bool editable = candidate && candidate->reviewStatus == QStringLiteral("candidate")
                          && !taskState_.taskRunning;
    queryEdit_->setEnabled(editable);
    evidenceTable_->setEnabled(editable);
    addEvidenceButton_->setEnabled(editable);
    removeEvidenceButton_->setEnabled(editable && evidenceTable_->selectionModel()->hasSelection());
    approveButton_->setEnabled(editable);
    rejectButton_->setEnabled(editable);
    const bool workerAvailable = taskState_.workerState == WorkerState::Ready && !taskState_.taskRunning;
    importButton_->setEnabled(!taskState_.taskRunning);
    topKSpin_->setEnabled(workerAvailable);
    runButton_->setEnabled(workerAvailable && approvedSampleCount_ > 0);
    // 审核段的 tag 跟着候选状态走：已批准/已拒绝的候选不可再编辑，
    // 按钮变灰的原因就写在这枚 tag 上，不用用户猜。
    if (!candidate) {
        ragui::setTag(reviewStatusLabel_, QStringLiteral("未选择候选"), ragui::Tone::Neutral);
    } else if (candidate->reviewStatus == QStringLiteral("approved")) {
        ragui::setTag(reviewStatusLabel_, QStringLiteral("已批准 · 不可再改"), ragui::Tone::Success);
    } else if (candidate->reviewStatus == QStringLiteral("rejected")) {
        ragui::setTag(reviewStatusLabel_, QStringLiteral("已拒绝 · 不可再改"), ragui::Tone::Neutral);
    } else {
        ragui::setTag(reviewStatusLabel_, QStringLiteral("待审核"), ragui::Tone::Info);
    }
    updateEmptyStates();
}

void EvaluationCenterPage::updateEmptyStates()
{
    if (candidateEmpty_) candidateEmpty_->setVisible(candidateTable_->rowCount() == 0);
    if (evidenceEmpty_) {
        evidenceEmpty_->setText(selectedCandidate()
                                    ? QStringLiteral("该候选还没有 Expected Evidence，点「新增引用」补充")
                                    : QStringLiteral("先在左侧选择一个反馈候选"));
        evidenceEmpty_->setVisible(evidenceTable_->rowCount() == 0);
    }
    if (failedEmpty_) {
        failedEmpty_->setText(failedEmptyText_);
        failedEmpty_->setVisible(failedCasesTable_->rowCount() == 0);
    }
}

EvaluationCandidate EvaluationCenterPage::editedCandidate() const
{
    const EvaluationCandidate *selected = selectedCandidate();
    if (!selected) return {};
    EvaluationCandidate candidate = *selected;
    candidate.query = queryEdit_->text().trimmed();
    candidate.expectedEvidence.clear();
    for (int row = 0; row < evidenceTable_->rowCount(); ++row) {
        if (evidenceTable_->item(row, 0)
            && evidenceTable_->item(row, 0)->checkState() == Qt::Checked) {
            const auto value = [this, row](int column) {
                const QTableWidgetItem *item = evidenceTable_->item(row, column);
                return item ? item->text().trimmed() : QString();
            };
            candidate.expectedEvidence.push_back({value(1), value(2), value(3), value(4), value(5)});
        }
    }
    return candidate;
}

const EvaluationCandidate *EvaluationCenterPage::selectedCandidate() const
{
    return selectedRow_ >= 0 && selectedRow_ < candidates_.size() ? &candidates_.at(selectedRow_) : nullptr;
}

void EvaluationCenterPage::showStatus(const QString &message, ragui::Tone tone, const QString &tooltip)
{
    ragui::setTag(statusLabel_, message, tone);
    statusLabel_->setToolTip(tooltip.isEmpty() ? message : tooltip);
}
