#include "diagnosisworkspacepage.h"

#include "diagnosticworkorder.h"
#include "protocol/protocollogpanel.h"
#include "protocol/protocolworkorder.h"
#include "ui/uikit.h"

#include <QButtonGroup>
#include <QComboBox>
#include <QDir>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QItemSelectionModel>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QSizePolicy>
#include <QSpinBox>
#include <QSplitter>
#include <QStackedWidget>
#include <QTableWidget>
#include <QTextBrowser>
#include <QVBoxLayout>

namespace {

/// 详情区里的字段名（灰色小字）。
///
/// 用 mutedText 而不是 faintText：字段名是内容的一部分，faintText 只有 3.6:1，
/// 那是留给占位符的档位。
QLabel *detailCaption(const QString &text)
{
    auto *label = new QLabel(text);
    label->setObjectName(QStringLiteral("mutedText"));
    label->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    return label;
}

/// 详情区里的字段值：等宽、单行省略、完整内容进 tooltip。
///
/// 必须是 ElidedLabel —— Doc ID / Chunk ID 可能很长，普通 QLabel 会把 QSplitter
/// 的 Evidence 一侧顶宽、挤掉结论区；换行版又会让详情区高度随选中行跳动。
ragui::ElidedLabel *detailValue()
{
    auto *label = ragui::makeElidedLabel(QString(), QStringLiteral("monoText"));
    label->setTextInteractionFlags(Qt::TextSelectableByMouse);
    return label;
}

void setDetailText(ragui::ElidedLabel *label, const QString &text)
{
    label->setFullText(text.trimmed().isEmpty() ? QStringLiteral("—") : text);
}

/// 结论区的空态文案。空态不留死白：文字居中浮在结论区中央（EmptyStateOverlay），
/// 而不是像占位符那样贴在左上角，让下面 400px 全白。
QString answerIdleHint()
{
    return QStringLiteral("诊断完成后这里显示结论正文、拒答原因，以及结论引用到的手册条目");
}

QString answerRunningHint()
{
    return QStringLiteral("正在检索证据并生成结论…");
}

} // namespace

DiagnosisWorkspacePage::DiagnosisWorkspacePage(QWidget *parent)
    : QWidget(parent)
{
    const ragui::RagMetrics metrics;
    setObjectName(QStringLiteral("diagnosisWorkspacePage"));
    auto *rootLayout = new QVBoxLayout(this);
    rootLayout->setContentsMargins(0, 0, 0, 0);
    // 页面已经在白色浮层里，块与块之间靠 24 的留白分界，不靠线也不靠卡。
    rootLayout->setSpacing(metrics.space5);

    // 页头卡被删掉了：导航栏已经标明当前位置，标题卡只是白占 90px。
    // 模式切换、证据条数、主操作全部收进输入区正上方这一行工具条。
    QWidget *toolbar = createToolbar();

    inputStack_ = new QStackedWidget;
    inputStack_->addWidget(createQueryPanel());
    inputStack_->addWidget(createHardFaultPanel());
    protocolLogPanel_ = new ProtocolLogPanel;
    // 三个模式的输入区都是「区块标题 + 填色输入框」，一律不包卡：
    // 填色的输入框本身就说明了"这里可以打字"，外面再套一圈边就是框中框。
    inputStack_->addWidget(protocolLogPanel_);
    // 三个模式的主操作共用工具条右端这一个位置，按模式切换。
    actionStack_->addWidget(queryButton_);
    actionStack_->addWidget(hardFaultButton_);
    actionStack_->addWidget(protocolLogPanel_->submitButton());

    rootLayout->addWidget(toolbar);
    rootLayout->addWidget(inputStack_);
    sizeInputStackToPage(0);

    auto *resultSplitter = new QSplitter(Qt::Horizontal);
    resultSplitter->setChildrenCollapsible(false);
    resultStack_ = new QStackedWidget;
    // 分隔器把手只有 12px，两栏又都是裸铺的内容，各自再让出 12 的边距，
    // 中缝合计 36 —— 靠留白把两栏分开，不画竖线。
    resultStack_->layout()->setContentsMargins(0, 0, metrics.space3, 0);
    resultStack_->addWidget(createAnswerPanel());
    // 两个工单里全是自带边框的表格，再套一层卡就是卡中卡，这里直接放进滚动区。
    workOrder_ = new DiagnosticWorkOrder;
    auto *workOrderScroll = new QScrollArea;
    workOrderScroll->setWidgetResizable(true);
    workOrderScroll->setFrameShape(QFrame::NoFrame);
    workOrderScroll->setWidget(workOrder_);
    resultStack_->addWidget(workOrderScroll);
    protocolWorkOrder_ = new ProtocolWorkOrder;
    auto *protocolScroll = new QScrollArea;
    protocolScroll->setWidgetResizable(true);
    protocolScroll->setFrameShape(QFrame::NoFrame);
    protocolScroll->setWidget(protocolWorkOrder_);
    resultStack_->addWidget(protocolScroll);
    QWidget *evidencePanel = createEvidencePanel();
    // 任何一侧都不许被挤没：结论要读得下整句，Evidence 要放得下五列。
    resultStack_->setMinimumWidth(360);
    evidencePanel->setMinimumWidth(300);
    resultSplitter->addWidget(resultStack_);
    resultSplitter->addWidget(evidencePanel);
    resultSplitter->setStretchFactor(0, 6);
    resultSplitter->setStretchFactor(1, 4);
    resultSplitter->setSizes({760, 500});
    rootLayout->addWidget(resultSplitter, 1);

    connect(modeComboBox_, qOverload<int>(&QComboBox::currentIndexChanged), this,
            [this](int index) {
                inputStack_->setCurrentIndex(index);
                resultStack_->setCurrentIndex(index);
                actionStack_->setCurrentIndex(index);
                sizeInputStackToPage(index);
                syncModeButtons(index);
            });
    connect(modeGroup_, &QButtonGroup::idClicked, this,
            [this](int index) { modeComboBox_->setCurrentIndex(index); });
    connect(evidenceTable_, &QTableWidget::itemSelectionChanged, this,
            [this] { updateEvidenceDetail(); });
    connect(queryButton_, &QPushButton::clicked, this, [this] { submitQuery(); });
    connect(hardFaultButton_, &QPushButton::clicked, this, [this] { submitHardFault(); });
    connect(protocolLogPanel_->submitButton(), &QPushButton::clicked, this, [this] { submitProtocolLog(); });
    connect(openReportButton_, &QPushButton::clicked, this, [this] { openCurrentReport(); });
    connect(workOrder_->reportButton(), &QPushButton::clicked, this, [this] { openCurrentReport(); });
    connect(protocolWorkOrder_->reportButton(), &QPushButton::clicked, this, [this] { openCurrentReport(); });
    syncAnswerPlaceholder(answerIdleHint());
    setTaskState({});
}

void DiagnosisWorkspacePage::setQueryHandler(QueryHandler handler)
{
    queryHandler_ = std::move(handler);
}

void DiagnosisWorkspacePage::setHardFaultHandler(HardFaultHandler handler)
{
    hardFaultHandler_ = std::move(handler);
}

void DiagnosisWorkspacePage::setProtocolLogHandler(ProtocolLogHandler handler)
{
    protocolLogHandler_ = std::move(handler);
}

void DiagnosisWorkspacePage::setReportHandler(ReportHandler handler)
{
    reportHandler_ = std::move(handler);
}

void DiagnosisWorkspacePage::setStatusHandler(StatusHandler handler)
{
    statusHandler_ = std::move(handler);
}

void DiagnosisWorkspacePage::setMode(DiagnosisMode mode)
{
    modeComboBox_->setCurrentIndex(static_cast<int>(mode));
}

DiagnosisMode DiagnosisWorkspacePage::mode() const
{
    return static_cast<DiagnosisMode>(modeComboBox_->currentIndex());
}

void DiagnosisWorkspacePage::restoreCaseInput(const QString &mode, const QString &input)
{
    if (mode == QStringLiteral("protocol_log")) {
        setMode(DiagnosisMode::ProtocolLog);
        protocolLogPanel_->setLogText(input);
    } else if (mode == QStringLiteral("hardfault")) {
        setMode(DiagnosisMode::HardFault);
        hardFaultLogEdit_->setPlainText(input);
    } else {
        setMode(DiagnosisMode::Expert);
        questionEdit_->setPlainText(input);
    }
}

void DiagnosisWorkspacePage::setDefaultTopK(int topK)
{
    topKSpinBox_->setValue(topK);
}

void DiagnosisWorkspacePage::setTaskState(const WorkspaceTaskState &state)
{
    const bool available = state.workerState == WorkerState::Ready && !state.taskRunning;
    queryButton_->setEnabled(available);
    hardFaultButton_->setEnabled(available);
    protocolLogPanel_->setTaskState(available, state.taskRunning);
    topKSpinBox_->setEnabled(!state.taskRunning);
    // modeComboBox_ 虽然不可见，仍是模式的唯一状态源，外部脚本和用例回放都在用它。
    modeComboBox_->setEnabled(!state.taskRunning);
    for (QPushButton *button : modeButtons_) {
        button->setEnabled(!state.taskRunning);
    }
}

void DiagnosisWorkspacePage::setDiagnosisResult(const DiagnosisViewModel &model)
{
    currentReportPath_ = QDir::cleanPath(model.reportPath);
    if (model.protocolLog.available) {
        setMode(DiagnosisMode::ProtocolLog);
        protocolWorkOrder_->render(model.protocolLog, model.answer, model.grounded);
        protocolWorkOrder_->reportButton()->setEnabled(!currentReportPath_.isEmpty());
    } else if (model.hardFault.available) {
        setMode(DiagnosisMode::HardFault);
        workOrder_->render(model.hardFault, model.answer, model.grounded);
        workOrder_->reportButton()->setEnabled(!currentReportPath_.isEmpty());
    } else {
        setMode(DiagnosisMode::Expert);
        answerBrowser_->setPlainText(model.answer);
    }
    syncAnswerPlaceholder(answerIdleHint());
    renderEvidence(model.evidence);
    if (model.grounded) {
        ragui::setTag(groundedStatusLabel_, QStringLiteral("✓ 有证据"), ragui::Tone::Success);
    } else {
        const QString reason = model.refusalReason.isEmpty() ? QStringLiteral("no_evidence") : model.refusalReason;
        ragui::setTag(groundedStatusLabel_, QStringLiteral("无证据 · %1").arg(reason), ragui::Tone::Warning);
    }
    resultMetaLabel_->setFullText(QStringLiteral("Embedding %1 · Reranker %2 · 检索 %3 ms")
                                      .arg(model.embedding.isEmpty() ? QStringLiteral("unknown") : model.embedding,
                                           model.reranker.isEmpty() ? QStringLiteral("unknown") : model.reranker,
                                           QString::number(model.retrievalMs, 'f', 1)));
    openReportButton_->setEnabled(!currentReportPath_.isEmpty());
    showStatus(model.grounded
                   ? QStringLiteral("诊断完成：已找到可追溯证据")
                   : QStringLiteral("诊断完成：当前知识库没有足够证据"),
               6000);
}

void DiagnosisWorkspacePage::showQueryFailure(const QString &message)
{
    markFailed();
    answerBrowser_->setPlainText(QStringLiteral("诊断任务未完成。%1").arg(message));
    syncAnswerPlaceholder(answerIdleHint());
    clearEvidence();
    currentReportPath_.clear();
    openReportButton_->setEnabled(false);
    showStatus(QStringLiteral("诊断失败：请查看运行日志"), 8000);
}

void DiagnosisWorkspacePage::showHardFaultFailure(const QString &message)
{
    setMode(DiagnosisMode::HardFault);
    markFailed();
    workOrder_->showError(message);
    clearEvidence();
    currentReportPath_.clear();
    showStatus(QStringLiteral("HardFault 日志校验或诊断失败"), 8000);
}

void DiagnosisWorkspacePage::showProtocolLogFailure(const QString &message)
{
    setMode(DiagnosisMode::ProtocolLog);
    markFailed();
    protocolWorkOrder_->showError(message);
    protocolWorkOrder_->reportButton()->setEnabled(false);
    clearEvidence();
    currentReportPath_.clear();
    openReportButton_->setEnabled(false);
    showStatus(QStringLiteral("协议日志校验或诊断失败"), 8000);
}

QWidget *DiagnosisWorkspacePage::createToolbar()
{
    auto *toolbar = ragui::makeToolbar();
    auto *layout = qobject_cast<QHBoxLayout *>(toolbar->layout());

    // 模式的唯一状态源。分段按钮只是它的外观，shell / 用例回放 / 命令行开关
    // 仍然通过 findChild("modeSelector") + setCurrentIndex() 切模式。
    modeComboBox_ = new QComboBox(toolbar);
    modeComboBox_->setObjectName(QStringLiteral("modeSelector"));
    modeComboBox_->addItem(QStringLiteral("专家问答"));
    modeComboBox_->addItem(QStringLiteral("HardFault 日志"));
    modeComboBox_->addItem(QStringLiteral("协议日志"));
    modeComboBox_->setVisible(false);

    auto *segmentRow = new QWidget;
    auto *segmentLayout = new QHBoxLayout(segmentRow);
    segmentLayout->setContentsMargins(0, 0, 0, 0);
    segmentLayout->setSpacing(0); // 分段控件必须贴合，相邻边框共用一条线
    modeGroup_ = new QButtonGroup(this);
    modeGroup_->setExclusive(true);
    const QStringList segmentLabels{QStringLiteral("专家问答"), QStringLiteral("HardFault"),
                                    QStringLiteral("协议日志")};
    const QStringList segmentRoles{QStringLiteral("first"), QStringLiteral("middle"),
                                   QStringLiteral("last")};
    for (int index = 0; index < segmentLabels.size(); ++index) {
        auto *button = new QPushButton(segmentLabels.at(index));
        button->setCheckable(true);
        button->setProperty("segment", segmentRoles.at(index));
        button->setToolTip(modeComboBox_->itemText(index));
        modeGroup_->addButton(button, index);
        segmentLayout->addWidget(button);
        modeButtons_.push_back(button);
    }
    modeButtons_.first()->setChecked(true);

    // top-k 三个模式共用：HardFault 与协议日志一直在读它的值，
    // 以前它只长在专家问答面板里，一切模式就消失，控制不到自己的行为。
    auto *topKLabel = new QLabel(QStringLiteral("证据条数"));
    topKLabel->setObjectName(QStringLiteral("mutedText"));
    topKSpinBox_ = new QSpinBox;
    topKSpinBox_->setObjectName(QStringLiteral("diagnosisTopK"));
    topKSpinBox_->setRange(1, 20);
    topKSpinBox_->setValue(5);
    topKSpinBox_->setSuffix(QStringLiteral(" 条"));
    topKSpinBox_->setMinimumWidth(88);
    topKSpinBox_->setToolTip(QStringLiteral("每次诊断检索并保留的证据条数，三个模式共用"));

    queryButton_ = new QPushButton(QStringLiteral("开始诊断"));
    queryButton_->setObjectName(QStringLiteral("primaryButton"));
    queryButton_->setProperty("testId", QStringLiteral("expertQueryButton"));
    queryButton_->setMinimumWidth(112);
    hardFaultButton_ = new QPushButton(QStringLiteral("分析 HardFault"));
    hardFaultButton_->setObjectName(QStringLiteral("hardFaultButton"));
    hardFaultButton_->setMinimumWidth(112);
    actionStack_ = new QStackedWidget;
    actionStack_->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);

    layout->addWidget(segmentRow);
    layout->addStretch(1);
    layout->addWidget(topKLabel);
    layout->addWidget(topKSpinBox_);
    layout->addWidget(actionStack_);
    return toolbar;
}

QWidget *DiagnosisWorkspacePage::createQueryPanel()
{
    const ragui::RagMetrics metrics;
    auto *content = new QWidget;
    auto *layout = new QVBoxLayout(content);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(metrics.space2);
    layout->addWidget(ragui::makeSectionTitle(QStringLiteral("诊断问题"),
                                              QStringLiteral("输入寄存器、异常状态或协议现象")));
    questionEdit_ = new QPlainTextEdit;
    questionEdit_->setObjectName(QStringLiteral("expertQuestion"));
    questionEdit_->setPlaceholderText(QStringLiteral("例如：ETH_DMASR 的 EBS 表示什么？出现总线错误时应该检查哪些状态位？"));
    questionEdit_->setMinimumHeight(56);
    questionEdit_->setMaximumHeight(72);
    questionEdit_->setMinimumWidth(0);
    questionEdit_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    questionEdit_->setTabChangesFocus(true);
    layout->addWidget(questionEdit_);
    return content;
}

QWidget *DiagnosisWorkspacePage::createHardFaultPanel()
{
    const ragui::RagMetrics metrics;
    auto *content = new QWidget;
    auto *layout = new QVBoxLayout(content);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(metrics.space2);
    layout->addWidget(ragui::makeSectionTitle(QStringLiteral("HardFault Log"),
                                              QStringLiteral("粘贴 CFSR/HFSR/BFAR/MMFAR/PC/LR 现场")));
    hardFaultLogEdit_ = new QPlainTextEdit;
    hardFaultLogEdit_->setObjectName(QStringLiteral("hardFaultLog"));
    hardFaultLogEdit_->setPlaceholderText(QStringLiteral(
        "HardFault\nCFSR=0x00008200\nHFSR=0x40000000\nBFAR=0x2003FFF8\nPC=0x080126AC\nLR=0xFFFFFFF9"));
    hardFaultLogEdit_->setMinimumHeight(56);
    hardFaultLogEdit_->setMaximumHeight(120);
    hardFaultLogEdit_->setMinimumWidth(0);
    hardFaultLogEdit_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    layout->addWidget(hardFaultLogEdit_);
    return content;
}

QWidget *DiagnosisWorkspacePage::createAnswerPanel()
{
    const ragui::RagMetrics metrics;
    // 不包卡。右侧 Evidence 是裸铺在白面板上的表格，两栏地位对等就必须同款材质；
    // HardFault / 协议日志两页的工单也是裸的，切换模式时材质才不会跳变。
    auto *panel = new QWidget;
    auto *layout = new QVBoxLayout(panel);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(metrics.space3);
    groundedStatusLabel_ = ragui::makeTag(QStringLiteral("尚未查询"), ragui::Tone::Neutral);
    layout->addWidget(ragui::makeSectionHeader(QStringLiteral("诊断结论"),
                                               QStringLiteral("由检索证据约束生成"),
                                               groundedStatusLabel_));
    answerBrowser_ = new QTextBrowser;
    answerBrowser_->setObjectName(QStringLiteral("diagnosisAnswer"));
    answerBrowser_->setFrameShape(QFrame::NoFrame);
    layout->addWidget(answerBrowser_, 1);
    // 空态提示浮在结论区正中，和右栏「尚无证据」同一种做法（并列的东西必须同款）。
    // 不用 setPlaceholderText：那行字会贴在左上角，把下面一大片留成死白。
    auto *overlay = new ragui::EmptyStateOverlay(answerBrowser_, answerIdleHint());
    overlay->setWordWrap(true);
    overlay->setMargin(metrics.space5);
    answerEmptyOverlay_ = overlay;

    resultMetaLabel_ = ragui::makeElidedLabel(
        QStringLiteral("Embedding · Reranker · 检索耗时将在诊断完成后显示"));
    openReportButton_ = new QPushButton(QStringLiteral("打开 Markdown 诊断报告"));
    openReportButton_->setObjectName(QStringLiteral("secondaryButton"));
    openReportButton_->setProperty("testId", QStringLiteral("openDiagnosisReport"));
    openReportButton_->setEnabled(false);
    // 元信息和报告按钮并成一行：它们同属"这次运行的尾注"，各占一行会白吃 40px。
    auto *footer = new QWidget;
    auto *footerLayout = new QHBoxLayout(footer);
    footerLayout->setContentsMargins(0, 0, 0, 0);
    footerLayout->setSpacing(metrics.space3);
    footerLayout->addWidget(resultMetaLabel_, 1);
    footerLayout->addWidget(openReportButton_, 0);
    layout->addWidget(footer);
    return panel;
}

QWidget *DiagnosisWorkspacePage::createEvidencePanel()
{
    const ragui::RagMetrics metrics;
    // 表格直接铺在白面板上：无外框、无网格线，本身就是内容而不是容器，所以不包卡。
    auto *panel = new QWidget;
    auto *layout = new QVBoxLayout(panel);
    layout->setContentsMargins(metrics.space3, 0, 0, 0);
    layout->setSpacing(metrics.space3);
    evidenceCountLabel_ = ragui::makeTag(QStringLiteral("0 条"), ragui::Tone::Neutral);
    // 标题行和左栏「诊断结论」同构：标题 + 副标题 + 右端一个状态标签。
    layout->addWidget(ragui::makeSectionHeader(QStringLiteral("Evidence"),
                                               QStringLiteral("混合检索与重排序后的原始依据"),
                                               evidenceCountLabel_));
    evidenceTable_ = new QTableWidget;
    evidenceTable_->setObjectName(QStringLiteral("evidenceTable"));
    configureEvidenceTable();
    layout->addWidget(evidenceTable_, 1);
    layout->addWidget(createEvidenceDetail());
    evidenceEmptyOverlay_ = new ragui::EmptyStateOverlay(evidenceTable_, QStringLiteral("尚无证据"));
    evidenceEmptyOverlay_->setVisible(true);
    return panel;
}

QWidget *DiagnosisWorkspacePage::createEvidenceDetail()
{
    const ragui::RagMetrics metrics;
    // 这里全是只读展示（Doc ID / Chunk ID / 三路分数）。按"填色 = 可编辑"的全局约定，
    // 它不能再套 QFrame#inlineWell —— 一块什么都不表示的浅灰会成为这一列最重的物体。
    // 和上方表格之间只留一条发丝线，其余靠留白。
    auto *panel = new QWidget;
    auto *panelLayout = new QVBoxLayout(panel);
    panelLayout->setContentsMargins(0, 0, 0, 0);
    panelLayout->setSpacing(metrics.space3);
    panelLayout->addWidget(ragui::makeHDivider());

    // 两页等高切换，避免选中 / 未选中时容器高度跳动。
    evidenceDetailStack_ = new QStackedWidget;
    auto *hint = new QLabel(QStringLiteral("选择一行查看完整评分与 ID"));
    hint->setObjectName(QStringLiteral("mutedText"));
    hint->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    evidenceDetailStack_->addWidget(hint);

    auto *detail = new QWidget;
    auto *grid = new QGridLayout(detail);
    grid->setContentsMargins(0, 0, 0, 0);
    grid->setHorizontalSpacing(metrics.space3);
    grid->setVerticalSpacing(metrics.space1);
    evidenceDocIdLabel_ = detailValue();
    evidenceChunkIdLabel_ = detailValue();
    evidenceScoreLabel_ = detailValue();
    grid->addWidget(detailCaption(QStringLiteral("Doc ID")), 0, 0);
    grid->addWidget(evidenceDocIdLabel_, 0, 1);
    grid->addWidget(detailCaption(QStringLiteral("Chunk ID")), 1, 0);
    grid->addWidget(evidenceChunkIdLabel_, 1, 1);
    grid->addWidget(detailCaption(QStringLiteral("评分")), 2, 0);
    grid->addWidget(evidenceScoreLabel_, 2, 1);
    grid->setColumnStretch(1, 1);
    evidenceDetailStack_->addWidget(detail);

    panelLayout->addWidget(evidenceDetailStack_);
    return panel;
}

void DiagnosisWorkspacePage::configureEvidenceTable()
{
    // 主列只留判断来源用得上的五列；Doc ID / Chunk ID / Hybrid / BM25 / Dense
    // 一条不少，全部落在表格下方的详情抽屉里，选中哪行看哪行。
    const QStringList headers = {
        QStringLiteral("#"), QStringLiteral("来源"), QStringLiteral("章节"),
        QStringLiteral("页码"), QStringLiteral("Reranker")};
    evidenceTable_->setColumnCount(headers.size());
    evidenceTable_->setHorizontalHeaderLabels(headers);
    ragui::applyTableDefaults(evidenceTable_);
    QHeaderView *header = evidenceTable_->horizontalHeader();
    header->setMinimumSectionSize(40);
    header->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    header->setSectionResizeMode(1, QHeaderView::Stretch);
    header->setSectionResizeMode(2, QHeaderView::Interactive);
    header->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    header->setSectionResizeMode(4, QHeaderView::ResizeToContents);
    header->resizeSection(2, 104);
}

void DiagnosisWorkspacePage::syncModeButtons(int index)
{
    if (index < 0 || index >= modeButtons_.size()) {
        return;
    }
    QPushButton *button = modeButtons_.at(index);
    if (!button->isChecked()) {
        button->setChecked(true);
    }
}

/// QStackedWidget 默认按最高的一页定高：协议日志那一页最高，切回只有一个输入框的
/// 专家问答时会白留一大片。让非当前页在垂直方向放弃尺寸主张（QStackedLayout 会把
/// Ignored 的一页按 0 计算），输入区就跟着当前模式的实际高度走。
void DiagnosisWorkspacePage::sizeInputStackToPage(int index)
{
    for (int page = 0; page < inputStack_->count(); ++page) {
        QWidget *widget = inputStack_->widget(page);
        widget->setSizePolicy(QSizePolicy::Preferred,
                              page == index ? QSizePolicy::Preferred : QSizePolicy::Ignored);
    }
    inputStack_->updateGeometry();
}

void DiagnosisWorkspacePage::submitQuery()
{
    const QString question = questionEdit_->toPlainText().trimmed();
    if (question.isEmpty()) {
        questionEdit_->setFocus();
        ragui::setInvalid(questionEdit_, true);
        showStatus(QStringLiteral("请输入诊断问题后再开始查询"), 4000);
        return;
    }
    ragui::setInvalid(questionEdit_, false);
    if (!queryHandler_ || !queryHandler_(question, topKSpinBox_->value())) {
        showStatus(QStringLiteral("当前无法启动专家诊断"), 4000);
        return;
    }
    answerBrowser_->clear();
    clearEvidence();
    currentReportPath_.clear();
    openReportButton_->setEnabled(false);
    markRunning();
}

void DiagnosisWorkspacePage::submitHardFault()
{
    const QString log = hardFaultLogEdit_->toPlainText().trimmed();
    if (log.isEmpty()) {
        hardFaultLogEdit_->setFocus();
        ragui::setInvalid(hardFaultLogEdit_, true);
        workOrder_->showError(QStringLiteral("HardFault 日志为空，请粘贴 CFSR 或 HFSR 等故障现场。"));
        showStatus(QStringLiteral("请输入 HardFault 日志后再开始分析"), 4000);
        return;
    }
    ragui::setInvalid(hardFaultLogEdit_, false);
    if (!hardFaultHandler_ || !hardFaultHandler_(log, topKSpinBox_->value())) {
        showStatus(QStringLiteral("当前无法启动 HardFault 诊断"), 4000);
        return;
    }
    workOrder_->showPlaceholder();
    clearEvidence();
    currentReportPath_.clear();
    workOrder_->reportButton()->setEnabled(false);
    markRunning();
}

void DiagnosisWorkspacePage::submitProtocolLog()
{
    QString validationError;
    const QJsonObject payload = protocolLogPanel_->requestPayload(&validationError);
    if (!validationError.isEmpty()) {
        protocolWorkOrder_->showError(validationError);
        showStatus(QStringLiteral("请输入协议日志后再开始分析"), 4000);
        return;
    }
    if (!protocolLogHandler_ || !protocolLogHandler_(payload, topKSpinBox_->value())) {
        showStatus(QStringLiteral("当前无法启动协议日志诊断"), 4000);
        return;
    }
    protocolWorkOrder_->showPlaceholder();
    protocolWorkOrder_->reportButton()->setEnabled(false);
    clearEvidence();
    currentReportPath_.clear();
    openReportButton_->setEnabled(false);
    markRunning();
}

void DiagnosisWorkspacePage::openCurrentReport()
{
    if (reportHandler_ && !currentReportPath_.isEmpty()) {
        reportHandler_(currentReportPath_);
    }
}

void DiagnosisWorkspacePage::renderEvidence(const QVector<EvidenceViewModel> &evidence)
{
    currentEvidence_ = evidence;
    evidenceTable_->clearContents();
    evidenceTable_->setRowCount(evidence.size());
    for (qsizetype row = 0; row < evidence.size(); ++row) {
        const EvidenceViewModel &item = evidence.at(row);
        const QString sourceTooltip = item.docId.isEmpty()
                                          ? item.source
                                          : QStringLiteral("%1 · %2").arg(item.source, item.docId);
        evidenceTable_->setItem(row, 0, ragui::makeNumericItem(QString::number(item.rank)));
        evidenceTable_->setItem(row, 1, ragui::makeTextItem(item.source, sourceTooltip));
        evidenceTable_->setItem(row, 2, ragui::makeTextItem(item.section));
        evidenceTable_->setItem(row, 3, ragui::makeTextItem(item.page));
        evidenceTable_->setItem(row, 4,
                                ragui::makeNumericItem(QString::number(item.rerankerScore, 'f', 4)));
    }
    if (!evidence.isEmpty()) {
        evidenceTable_->selectRow(0);
    }
    updateEvidenceDetail();
    evidenceEmptyOverlay_->setVisible(evidenceTable_->rowCount() == 0);
    // 计数为 0 时 countTone 会自动降级成中性灰，避免「0 条」也是彩色的。
    ragui::setTag(evidenceCountLabel_, QStringLiteral("%1 条").arg(evidence.size()),
                  ragui::countTone(evidence.size(), ragui::Tone::Info));
}

void DiagnosisWorkspacePage::clearEvidence()
{
    currentEvidence_.clear();
    evidenceTable_->clearContents();
    evidenceTable_->setRowCount(0);
    updateEvidenceDetail();
    ragui::setTag(evidenceCountLabel_, QStringLiteral("0 条"), ragui::Tone::Neutral);
    if (evidenceEmptyOverlay_) {
        evidenceEmptyOverlay_->setVisible(true);
    }
}

void DiagnosisWorkspacePage::updateEvidenceDetail()
{
    const QModelIndexList selected = evidenceTable_->selectionModel()->selectedRows();
    const int row = selected.isEmpty() ? -1 : selected.first().row();
    if (row < 0 || row >= currentEvidence_.size()) {
        evidenceDetailStack_->setCurrentIndex(0);
        return;
    }
    const EvidenceViewModel &item = currentEvidence_.at(row);
    setDetailText(evidenceDocIdLabel_, item.docId);
    setDetailText(evidenceChunkIdLabel_, item.chunkId);
    setDetailText(evidenceScoreLabel_,
                  QStringLiteral("Hybrid %1 · BM25 %2 · Dense %3")
                      .arg(QString::number(item.hybridScore, 'f', 4),
                           QString::number(item.bm25Score, 'f', 4),
                           QString::number(item.denseScore, 'f', 4)));
    evidenceDetailStack_->setCurrentIndex(1);
}

void DiagnosisWorkspacePage::markRunning()
{
    ragui::setTag(groundedStatusLabel_, QStringLiteral("检索中…"), ragui::Tone::Info);
    syncAnswerPlaceholder(answerRunningHint());
}

void DiagnosisWorkspacePage::markFailed()
{
    ragui::setTag(groundedStatusLabel_, QStringLiteral("诊断失败"), ragui::Tone::Error);
}

/// 结论区的空态：正文一有内容就把浮层收起来，空的时候居中给一句话。
/// hint 非空时顺带换掉文案（等待中 / 已就绪说的不是同一件事）。
void DiagnosisWorkspacePage::syncAnswerPlaceholder(const QString &hint)
{
    if (!answerEmptyOverlay_) {
        return;
    }
    if (!hint.isEmpty()) {
        answerEmptyOverlay_->setText(hint);
    }
    answerEmptyOverlay_->setVisible(answerBrowser_->toPlainText().trimmed().isEmpty());
}

void DiagnosisWorkspacePage::showStatus(const QString &message, int timeoutMs)
{
    if (statusHandler_) {
        statusHandler_(message, timeoutMs);
    }
}
