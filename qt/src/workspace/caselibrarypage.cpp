#include "workspace/caselibrarypage.h"

#include "ui/uikit.h"

#include <QCheckBox>
#include <QComboBox>
#include <QEvent>
#include <QFont>
#include <QFrame>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSplitter>
#include <QStackedWidget>
#include <QTableWidget>
#include <QTextBrowser>
#include <QVBoxLayout>

namespace {

// 纵向预算（1080×700 最小窗口下本页净高只有 ~458px：外壳扣掉导航、全局页头、
// Runtime Activity 之后剩这么多）。所以每个 min 都是能被裁掉之前的下限，
// 而不是想要的高度——想要的高度靠 stretch 拿：
//   详情卡最小 ≈ 428 = 卡内边距 24 + 身份行 38 + 三块正文 56/100/48
//                      + 三个标题行 20/38/38 + 底部按钮排 40 + 间距
// 只读区（输入现场 / 诊断结论）背景透明，多出来的高度是白的、不显形，
// 所以它们不设上限，直接按 1:2 分掉剩余空间；只有填色的「我的备注」必须封顶，
// 否则一整块空的浅灰会变成这一栏最大的物体。
constexpr int kInputMinHeight = 56;
constexpr int kAnswerMinHeight = 100;
constexpr int kNoteMinHeight = 48;
constexpr int kNoteMaxHeight = 80;
constexpr int kSectionGap = 12;   // 三块之间：靠留白分隔，不画线
constexpr int kSectionInner = 4;  // 标题贴着自己的正文
constexpr int kListPanelMinWidth = 320;
constexpr int kFavoriteButtonWidth = 104; // 「收藏」↔「取消收藏」换文案时不让身份行跳动

// 列宽：三个元数据列按各自最长值实测 + 单元格内边距卡死，一格都不多给。
// 省下来的每一像素都归「问题/摘要」——那是这张表唯一要读的内容。
// 数字是量出来的不是算出来的：全局 QSS 把字号抬到 14px 之后，"HardFault" 要 106，
// 按 14px×字符数估会少给十几像素，结果就是「HardFa...」。
constexpr int kTimeColumnWidth = 108;     // 08-15 07:39
constexpr int kModeColumnWidth = 106;     // HardFault（最长的一个模式名）
constexpr int kGroundedColumnWidth = 82;  // 有证据 / 无证据
constexpr int kFavoriteColumnWidth = 56;  // ★，表头「收藏」两个字就是它的下限
// 「问题/摘要」低于这个宽度就只剩两三个词，这张表也就不用看了。
constexpr int kSummaryMinWidth = 200;

QString displayMode(const QString &mode)
{
    if (mode == QStringLiteral("hardfault")) {
        return QStringLiteral("HardFault");
    }
    if (mode == QStringLiteral("protocol_log")) {
        return QStringLiteral("协议日志");
    }
    return QStringLiteral("专家问答");
}

QString displayGrounded(const CaseRecord &record)
{
    return record.grounded ? QStringLiteral("有证据") : QStringLiteral("无证据");
}

/// "2026-08-15T07:39:33.000Z" → "2026-08-15 07:39:33"（不做时区换算，与落库文本一致）
QString fullTimestamp(const QString &isoTimestamp)
{
    QString text = isoTimestamp.left(19);
    text.replace(QLatin1Char('T'), QLatin1Char(' '));
    return text;
}

/// 表格里的短格式 "08-15 07:39"：19 个字符白占宽度，完整时间进 tooltip。
QString shortTimestamp(const QString &isoTimestamp)
{
    const QString full = fullTimestamp(isoTimestamp);
    return full.size() >= 16 ? full.mid(5, 11) : full;
}

/// 详情页元信息里的 "2026-08-15 07:39"。
///
/// 去掉秒不是为了好看，是为了让后面的「检索 xxx」整段落在可见范围内：
/// 这一行右边压着「收藏」按钮，只剩两百多像素，多三个字符就会把耗时省略成
/// 「检索 5132…」——半个数字比不显示更糟，它看起来像一个真的读数。
/// 秒级精度没有丢，列表那一列的 tooltip 里是完整时刻。
QString minuteTimestamp(const QString &isoTimestamp)
{
    return fullTimestamp(isoTimestamp).left(16);
}

/// 检索耗时读数，毫秒到秒自动换挡。
///
/// 冷启动那一次是 51329 ms——五位数既要在脑子里做一次除法才知道是半分钟，
/// 又会把后面的单位挤出可见范围。换成 51.3 s 之后读数永远不超过 6 个字符。
QString retrievalReading(double milliseconds)
{
    if (milliseconds >= 1000.0) {
        return QStringLiteral("%1 s").arg(milliseconds / 1000.0, 0, 'f', 1);
    }
    return QStringLiteral("%1 ms").arg(qRound(milliseconds));
}

QString singleLine(const QString &text)
{
    QString flat = text;
    flat.replace(QLatin1Char('\n'), QLatin1Char(' '));
    flat.replace(QLatin1Char('\r'), QLatin1Char(' '));
    return flat.simplified();
}

/// 行内状态：不带底色，只换文字颜色。
///
/// 改版前它是过滤条右端一枚薄荷胶囊，初始文案「案例库就绪」——那颗胶囊和旁边的
/// 筛选控件同高同圆角，读起来像第四个筛选按钮，而它其实是操作回执（"备注已保存"）。
/// 现在退成 statusInline：没事时是空的，出事时才有颜色，几何尺寸始终不变。
void applyStatus(ragui::ElidedLabel *label, const QString &text, ragui::Tone tone)
{
    if (!label) {
        return;
    }
    label->setFullText(text);
    label->setProperty("tone", ragui::toneName(tone));
    ragui::repolish(label);
}

QHBoxLayout *rowLayout(QWidget *host, int spacing)
{
    auto *layout = new QHBoxLayout(host);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(spacing);
    return layout;
}

/// 一个区块 = 标题行 + 它的正文。
///
/// 这是透明的布局容器，不是第二层卡片（版式铁律 1）：它存在的唯一理由是把标题
/// 和正文绑成一体，好让外层用一次 12px 间距把三块分开——这套语言用留白分隔，不画线。
QWidget *makeSection(QWidget *header, QWidget *body)
{
    auto *section = new QWidget;
    auto *layout = new QVBoxLayout(section);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(kSectionInner);
    layout->addWidget(header);
    layout->addWidget(body, 1);
    return section;
}

} // namespace

CaseLibraryPage::CaseLibraryPage(QWidget *parent)
    : QWidget(parent)
{
    const ragui::RagMetrics metrics;
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(metrics.space2);
    layout->addWidget(createFilterBar());

    auto *splitter = new QSplitter(Qt::Horizontal);
    splitter->setObjectName(QStringLiteral("caseLibrarySplitter"));
    splitter->setChildrenCollapsible(false);
    splitter->addWidget(createListPanel());
    splitter->addWidget(createDetailPanel());
    // 4:3 而不是原来的 5:4——详情卡瘦身之后（底部从五个按钮减到三个）它的最小宽度
    // 从 ~576 掉到 ~428，多出来的 100 多像素全部让给左边的「问题/摘要」列。
    splitter->setStretchFactor(0, 4);
    splitter->setStretchFactor(1, 3);
    splitter->setSizes({640, 480});
    layout->addWidget(splitter, 1);
    updateDetail();
    updateActionState();
}

void CaseLibraryPage::setFilterHandler(FilterHandler handler)
{
    filterHandler_ = std::move(handler);
}

void CaseLibraryPage::setReopenHandler(ReopenHandler handler)
{
    reopenHandler_ = std::move(handler);
}

void CaseLibraryPage::setNoteHandler(NoteHandler handler)
{
    noteHandler_ = std::move(handler);
}

void CaseLibraryPage::setFavoriteHandler(FavoriteHandler handler)
{
    favoriteHandler_ = std::move(handler);
}

void CaseLibraryPage::setCopyHandler(CopyHandler handler)
{
    copyHandler_ = std::move(handler);
}

void CaseLibraryPage::setExportHandler(ExportHandler handler)
{
    exportHandler_ = std::move(handler);
}

void CaseLibraryPage::setFeedbackHandler(FeedbackHandler handler)
{
    feedbackHandler_ = std::move(handler);
}

void CaseLibraryPage::setCandidateHandler(CandidateHandler handler)
{
    candidateHandler_ = std::move(handler);
}

void CaseLibraryPage::setRecords(const QVector<CaseRecord> &records)
{
    const QString selectedId = selectedRecord() ? selectedRecord()->id : QString();
    records_ = records;
    anyFavorite_ = false;
    recordsTable_->setUpdatesEnabled(false);
    recordsTable_->clearContents();
    recordsTable_->setRowCount(records_.size());
    selectedRow_ = -1;
    for (int row = 0; row < records_.size(); ++row) {
        const CaseRecord &record = records_.at(row);
        auto *time = ragui::makeTextItem(shortTimestamp(record.updatedAt),
                                         fullTimestamp(record.updatedAt));
        time->setData(Qt::UserRole, record.id);
        auto *mode = ragui::makeTextItem(displayMode(record.mode));
        // 「无证据」是这张表里唯一真的发生了什么的状态，所以只有它带色（语义色只在
        // 真的发生时出现）。有色之后这一列 82px 就够扫，不必靠宽度换可读性。
        // 对齐跟着 applyTableDefaults 的左对齐表头走：单元格全部左对齐，
        // 表头才落在它描述的那一列文字上方。
        auto *grounded = ragui::makeStatusItem(
            displayGrounded(record),
            record.grounded ? ragui::Tone::Neutral : ragui::Tone::Warning);
        const QString summary = singleLine(record.query.isEmpty() ? record.inputText : record.query);
        auto *summaryItem = ragui::makeTextItem(summary.left(96), summary);
        const QString note = singleLine(record.note);
        auto *noteItem = ragui::makeTextItem(note.left(96), note);
        auto *favorite = ragui::makeTextItem(record.favorite ? QStringLiteral("★") : QString(),
                                             record.favorite ? QStringLiteral("已收藏")
                                                             : QStringLiteral("未收藏"));
        recordsTable_->setItem(row, 0, time);
        recordsTable_->setItem(row, 1, mode);
        recordsTable_->setItem(row, 2, grounded);
        recordsTable_->setItem(row, 3, summaryItem);
        recordsTable_->setItem(row, 4, noteItem);
        recordsTable_->setItem(row, 5, favorite);
        anyFavorite_ = anyFavorite_ || record.favorite;
        if (record.id == selectedId) {
            selectedRow_ = row;
        }
    }
    countLabel_->setText(QStringLiteral("%1 条记录").arg(records_.size()));
    recordsTable_->setUpdatesEnabled(true);
    updateColumnBudget();
    const CaseFilter filter = currentFilter();
    const bool filtering = !filter.keyword.isEmpty() || !filter.mode.isEmpty() || filter.grounded >= 0
                           || filter.favoriteOnly;
    recordsEmptyHint_->setText(filtering ? QStringLiteral("没有匹配的案例，试试放宽筛选条件")
                                         : QStringLiteral("还没有诊断案例"));
    recordsEmptyHint_->setVisible(recordsTable_->rowCount() == 0);
    if (selectedRow_ < 0 && !records_.isEmpty()) {
        selectedRow_ = 0;
    }
    if (selectedRow_ >= 0) {
        recordsTable_->selectRow(selectedRow_);
    }
    updateDetail();
    updateActionState();
}

void CaseLibraryPage::setTaskState(const WorkspaceTaskState &state)
{
    taskState_ = state;
    updateActionState();
}

void CaseLibraryPage::showStoreError(const QString &message)
{
    applyStatus(statusLabel_, QStringLiteral("案例库错误：%1").arg(message), ragui::Tone::Error);
    records_.clear();
    anyFavorite_ = false;
    selectedRow_ = -1;
    recordsTable_->clearContents();
    recordsTable_->setRowCount(0);
    recordsEmptyHint_->setText(QStringLiteral("案例库不可用"));
    recordsEmptyHint_->setVisible(true);
    countLabel_->setText(QStringLiteral("案例库不可用"));
    updateColumnBudget();
    updateDetail();
    updateActionState();
}

CaseFilter CaseLibraryPage::currentFilter() const
{
    CaseFilter filter;
    filter.keyword = keywordEdit_->text();
    filter.mode = modeFilter_->currentData().toString();
    filter.grounded = groundedFilter_->currentData().toInt();
    filter.favoriteOnly = favoriteFilter_->isChecked();
    return filter;
}

QWidget *CaseLibraryPage::createFilterBar()
{
    // 过滤条不成卡：一屏两层容器已经够了（版式铁律 2）。
    auto *bar = ragui::makeToolbar();
    auto *layout = static_cast<QHBoxLayout *>(bar->layout());
    keywordEdit_ = new QLineEdit;
    keywordEdit_->setObjectName(QStringLiteral("caseKeyword"));
    keywordEdit_->setPlaceholderText(QStringLiteral("搜索问题、回答或备注"));
    keywordEdit_->setClearButtonEnabled(true);
    keywordEdit_->setMinimumWidth(200);
    // 搜索框吃满整行会变成一条 700px 的空槽——它是这一排最大的物体，却什么都不表示。
    // 封到 360 之后版式退回参考稿的样子：搜索靠左，筛选片靠右，中间留白。
    keywordEdit_->setMaximumWidth(360);
    modeFilter_ = new QComboBox;
    modeFilter_->setObjectName(QStringLiteral("caseModeFilter"));
    modeFilter_->addItem(QStringLiteral("全部模式"), QString());
    modeFilter_->addItem(QStringLiteral("专家问答"), QStringLiteral("expert"));
    modeFilter_->addItem(QStringLiteral("HardFault"), QStringLiteral("hardfault"));
    modeFilter_->addItem(QStringLiteral("协议日志"), QStringLiteral("protocol_log"));
    groundedFilter_ = new QComboBox;
    groundedFilter_->setObjectName(QStringLiteral("caseGroundedFilter"));
    groundedFilter_->addItem(QStringLiteral("全部证据状态"), -1);
    groundedFilter_->addItem(QStringLiteral("有证据"), 1);
    groundedFilter_->addItem(QStringLiteral("无证据"), 0);
    favoriteFilter_ = new QCheckBox(QStringLiteral("只看收藏"));
    favoriteFilter_->setObjectName(QStringLiteral("caseFavoriteFilter"));
    layout->addWidget(keywordEdit_, 1);
    layout->addStretch(1);
    layout->addWidget(modeFilter_);
    layout->addWidget(groundedFilter_);
    layout->addWidget(favoriteFilter_);
    connect(keywordEdit_, &QLineEdit::textChanged, this, [this] { emitFilter(); });
    connect(modeFilter_, &QComboBox::currentIndexChanged, this, [this] { emitFilter(); });
    connect(groundedFilter_, &QComboBox::currentIndexChanged, this, [this] { emitFilter(); });
    connect(favoriteFilter_, &QCheckBox::toggled, this, [this] { emitFilter(); });
    return bar;
}

QWidget *CaseLibraryPage::createListPanel()
{
    // 表格自带边框，本身就是容器，不再套 makeCard()（版式铁律 1）。
    auto *panel = new QWidget;
    auto *layout = new QVBoxLayout(panel);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(6);
    countLabel_ = new QLabel(QStringLiteral("0 条记录"));
    countLabel_->setObjectName(QStringLiteral("mutedText"));
    layout->addWidget(ragui::makeSectionHeader(QStringLiteral("案例列表"),
                                               QStringLiteral("按更新时间倒序"), countLabel_));

    recordsTable_ = new QTableWidget;
    recordsTable_->setObjectName(QStringLiteral("caseRecordsTable"));
    recordsTable_->setColumnCount(6);
    recordsTable_->setHorizontalHeaderLabels({QStringLiteral("更新时间"), QStringLiteral("模式"), QStringLiteral("证据"),
                                              QStringLiteral("问题/摘要"), QStringLiteral("备注"), QStringLiteral("收藏")});
    ragui::applyTableDefaults(recordsTable_);
    QHeaderView *header = recordsTable_->horizontalHeader();
    // applyTableDefaults() 关掉了 setStretchLastSection（它会和「按需隐藏最后一列」打架），
    // 所以这里单独把「问题/摘要」设成 Stretch 让它吃掉全部剩余宽度。
    // 固定宽度而不是 ResizeToContents：列宽不能随内容跳动，"协议日志" 也不能被截成 "协议日"。
    header->setSectionResizeMode(0, QHeaderView::Interactive);
    header->setSectionResizeMode(1, QHeaderView::Interactive);
    header->setSectionResizeMode(2, QHeaderView::Interactive);
    header->setSectionResizeMode(3, QHeaderView::Stretch);
    header->setSectionResizeMode(4, QHeaderView::Interactive);
    header->setSectionResizeMode(5, QHeaderView::Fixed);
    header->resizeSection(0, kTimeColumnWidth);
    header->resizeSection(1, kModeColumnWidth);
    header->resizeSection(2, kGroundedColumnWidth);
    header->resizeSection(5, kFavoriteColumnWidth);
    // 列的取舍（重新评估过一次）：
    //   · 「备注」始终隐藏——它是整句自由文本，给不到 150px 就只能显示头几个字，
    //     而多数案例根本没有备注；选中后右栏「我的备注」原样全文可见，列表没必要重复。
    //   · 「收藏」有星才出现（见 setRecords）——一颗 ★ 只要 56px，是「只看收藏」筛完
    //     之后唯一能在列表里直接验证的标记；但一条都没收藏时它就是一条空沟，
    //     而这一页最不该出现的就是白占宽度的列。
    //   · 三个元数据列在窄屏下会被 updateColumnBudget() 按价值从低到高摘掉。
    recordsTable_->setColumnHidden(4, true);
    recordsTable_->setColumnHidden(5, true);
    recordsEmptyHint_ = new ragui::EmptyStateOverlay(recordsTable_, QStringLiteral("还没有诊断案例"));
    layout->addWidget(recordsTable_, 1);
    panel->setMinimumWidth(kListPanelMinWidth);
    // 列预算跟着表格实际宽度走，而不是跟着窗口——splitter 是可以拖的。
    recordsTable_->viewport()->installEventFilter(this);
    connect(recordsTable_, &QTableWidget::itemSelectionChanged, this, [this] {
        selectedRow_ = recordsTable_->currentRow();
        updateDetail();
        updateActionState();
    });
    return panel;
}

QWidget *CaseLibraryPage::createFeedbackStrip()
{
    const ragui::RagMetrics metrics;
    auto *strip = new QWidget;
    auto *layout = rowLayout(strip, metrics.space2);
    helpfulButton_ = new QPushButton(QStringLiteral("有帮助"));
    helpfulButton_->setObjectName(QStringLiteral("caseHelpfulButton"));
    unhelpfulButton_ = new QPushButton(QStringLiteral("没帮助"));
    unhelpfulButton_->setObjectName(QStringLiteral("caseUnhelpfulButton"));
    candidateButton_ = new QPushButton(QStringLiteral("加入评估候选"));
    candidateButton_->setObjectName(QStringLiteral("caseCandidateButton"));
    candidateButton_->setProperty("testId", QStringLiteral("caseCandidate"));
    layout->addWidget(helpfulButton_);
    layout->addWidget(unhelpfulButton_);
    layout->addWidget(candidateButton_);
    connect(helpfulButton_, &QPushButton::clicked, this, [this] {
        if (selectedRecord() && feedbackHandler_
            && feedbackHandler_(*selectedRecord(), QStringLiteral("helpful"))) {
            showStatus(QStringLiteral("已记录“有帮助”，候选已进入评估中心"));
        }
    });
    connect(unhelpfulButton_, &QPushButton::clicked, this, [this] {
        if (selectedRecord() && feedbackHandler_
            && feedbackHandler_(*selectedRecord(), QStringLiteral("unhelpful"))) {
            showStatus(QStringLiteral("已记录“没帮助”，候选已进入评估中心"));
        }
    });
    connect(candidateButton_, &QPushButton::clicked, this, [this] {
        if (selectedRecord() && candidateHandler_ && candidateHandler_(*selectedRecord())) {
            showStatus(QStringLiteral("候选已打开到评估中心"));
        }
    });
    return strip;
}

QWidget *CaseLibraryPage::createDetailIdentity()
{
    const ragui::RagMetrics metrics;
    auto *identity = new QWidget;
    auto *identityLayout = rowLayout(identity, metrics.space3);
    selectedTitleLabel_ = new QLabel(QStringLiteral("未选择案例"));
    selectedTitleLabel_->setObjectName(QStringLiteral("caseSelectedTitle"));
    QFont titleFont = selectedTitleLabel_->font();
    titleFont.setBold(true);
    selectedTitleLabel_->setFont(titleFont);
    // 元信息以前是 setWordWrap(false) 的长 QLabel，把详情面板的最小宽度顶到 ~750px，
    // splitter 的 setSizes 因此完全失效。ElidedLabel 的水平 sizePolicy 是 Ignored，最小宽度为 0。
    selectedMetaLabel_ = ragui::makeElidedLabel();
    // 收藏是这条案例自己的属性，不是文件操作，所以它归在身份行而不是底部那排。
    // 搬上来同时把底部按钮从五个减到三个，详情卡最小宽度随之下降 ~150px。
    favoriteButton_ = new QPushButton(QStringLiteral("收藏"));
    favoriteButton_->setObjectName(QStringLiteral("caseFavoriteButton"));
    // 文案会在「收藏 / 取消收藏」之间切换，锁一个宽度免得整行随之跳动。
    favoriteButton_->setMinimumWidth(kFavoriteButtonWidth);
    identityLayout->addWidget(selectedTitleLabel_);
    identityLayout->addWidget(selectedMetaLabel_, 1);
    identityLayout->addWidget(favoriteButton_);
    connect(favoriteButton_, &QPushButton::clicked, this, [this] {
        if (selectedRecord() && favoriteHandler_
            && favoriteHandler_(selectedRecord()->id, !selectedRecord()->favorite)) {
            showStatus(selectedRecord()->favorite ? QStringLiteral("已取消收藏") : QStringLiteral("已收藏"));
        }
    });
    return identity;
}

QWidget *CaseLibraryPage::createDetailContent()
{
    auto *page = new QWidget;
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(kSectionGap);
    layout->addWidget(createDetailIdentity());

    // 输入现场用 QTextBrowser 而不是只读 QPlainTextEdit，有两个原因：
    //   1. 它和下面的「诊断结论」是并列的两块只读展示区，得是同一款（版式铁律 4）；
    //   2. Qt 的样式表几何量（padding / border）是 polish 时按「无伪状态」的规则算的，
    //      只有绘制才按 :read-only 重新求值。所以只读 QPlainTextEdit 会画成透明无边，
    //      却仍然按基础规则留出 1+14px 左内缩——正文比标题和结论区右移 15px，
    //      肉眼看就是一块对不齐的缩进。QTextBrowser 在读写两态都命中同一条规则，没有这个问题。
    inputEdit_ = new QTextBrowser;
    inputEdit_->setObjectName(QStringLiteral("caseInput"));
    inputEdit_->setMinimumHeight(kInputMinHeight);
    // 2:3 分剩余高度。不是 1:2——「输入现场」装的是 HardFault 寄存器组或整段协议日志，
    // 它比生成出来的结论更容易溢出；结论仍然多分一点，因为那是来看这一页的理由。
    layout->addWidget(makeSection(ragui::makeSectionTitle(QStringLiteral("输入现场")), inputEdit_), 2);

    // 反馈那三个按钮从底部搬到结论区标题行，底部就不会再挤出第二排、被卡片裁掉一半。
    // 标题行不带副标题：这一行右边还压着三个按钮，留给文字的只有一百多像素，
    // 原来那句「对评估有帮助吗？」永远显示成「对评估有…」——半句中文比不写更糟，
    // 而「有帮助 / 没帮助」贴在「诊断结论」标题右边，指的是哪一段本来就没有歧义。
    answerBrowser_ = new QTextBrowser;
    answerBrowser_->setObjectName(QStringLiteral("caseAnswer"));
    answerBrowser_->setMinimumHeight(kAnswerMinHeight);
    layout->addWidget(makeSection(ragui::makeSectionHeader(QStringLiteral("诊断结论"), QString(),
                                                           createFeedbackStrip()),
                                  answerBrowser_),
                      3);

    // 「保存备注」是这个输入框的提交动作，跟着输入框走比排在底部一堆文件操作里更好找。
    saveNoteButton_ = new QPushButton(QStringLiteral("保存备注"));
    saveNoteButton_->setObjectName(QStringLiteral("caseSaveNoteButton"));
    noteEdit_ = new QPlainTextEdit;
    noteEdit_->setObjectName(QStringLiteral("caseNote"));
    noteEdit_->setPlaceholderText(QStringLiteral("记下复核结论、页码或后续动作"));
    noteEdit_->setMinimumHeight(kNoteMinHeight);
    // 全页唯一还填色的框（填色 = 可编辑）。正因为显眼，它必须封顶并且不参与拉伸，
    // 否则空备注时一整块浅灰会变成这一栏最大的物体。
    noteEdit_->setMaximumHeight(kNoteMaxHeight);
    layout->addWidget(makeSection(ragui::makeSectionHeader(QStringLiteral("我的备注"), QString(),
                                                           saveNoteButton_),
                                  noteEdit_),
                      0);
    connect(saveNoteButton_, &QPushButton::clicked, this, [this] {
        if (selectedRecord() && noteHandler_
            && noteHandler_(selectedRecord()->id, noteEdit_->toPlainText())) {
            showStatus(QStringLiteral("备注已保存"));
        }
    });
    return page;
}

QWidget *CaseLibraryPage::createDetailPlaceholder()
{
    auto *page = new QWidget;
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(4);
    auto *title = new QLabel(QStringLiteral("未选择案例"));
    title->setObjectName(QStringLiteral("emptyHint"));
    title->setAlignment(Qt::AlignCenter);
    auto *hint = new QLabel(QStringLiteral("在左侧列表选一条记录，这里会显示输入现场与诊断结论。"));
    hint->setObjectName(QStringLiteral("faintText"));
    hint->setAlignment(Qt::AlignCenter);
    hint->setWordWrap(true);
    layout->addStretch(1);
    layout->addWidget(title);
    layout->addWidget(hint);
    layout->addStretch(1);
    return page;
}

QWidget *CaseLibraryPage::createDetailActions()
{
    const ragui::RagMetrics metrics;
    auto *actions = new QWidget;
    auto *layout = rowLayout(actions, metrics.space2);
    // 底部只留「打开这条案例」这一类去处：重开是主操作（全屏唯一的实心青绿），
    // 复制和导出是同款白胶囊。收藏搬去了身份行、保存备注搬去了备注区标题行——
    // 它们本来就各自属于某一块内容，排在这里既看不出主次，又把详情卡撑宽了 150px。
    reopenButton_ = new QPushButton(QStringLiteral("重开到诊断工作区"));
    reopenButton_->setObjectName(QStringLiteral("caseReopenButton"));
    reopenButton_->setProperty("testId", QStringLiteral("caseReopen"));
    reopenButton_->setProperty("variant", QStringLiteral("primary"));
    copyButton_ = new QPushButton(QStringLiteral("复制结论"));
    copyButton_->setObjectName(QStringLiteral("caseCopyButton"));
    exportButton_ = new QPushButton(QStringLiteral("导出 JSON"));
    exportButton_->setObjectName(QStringLiteral("caseExportButton"));
    exportButton_->setProperty("testId", QStringLiteral("caseExport"));
    layout->addWidget(reopenButton_);
    layout->addWidget(copyButton_);
    layout->addWidget(exportButton_);
    // 操作回执紧跟在按钮后面（"已复制" 出现在刚点过的按钮旁边才有用）。
    // ElidedLabel 的水平 sizePolicy 是 Ignored，所以再长的错误信息也不会把这一排顶宽。
    statusLabel_ = ragui::makeElidedLabel(QString(), QStringLiteral("statusInline"));
    layout->addSpacing(metrics.space2);
    layout->addWidget(statusLabel_, 1);
    connect(reopenButton_, &QPushButton::clicked, this, [this] {
        if (selectedRecord() && reopenHandler_ && reopenHandler_(*selectedRecord())) {
            showStatus(QStringLiteral("案例已重开到诊断工作区"));
        }
    });
    connect(copyButton_, &QPushButton::clicked, this, [this] {
        if (selectedRecord() && copyHandler_) {
            copyHandler_(selectedRecord()->answer);
            showStatus(QStringLiteral("诊断结论已复制"));
        }
    });
    connect(exportButton_, &QPushButton::clicked, this, [this] {
        if (selectedRecord() && exportHandler_ && exportHandler_(*selectedRecord())) {
            showStatus(QStringLiteral("案例已导出"));
        }
    });
    return actions;
}

QWidget *CaseLibraryPage::createDetailPanel()
{
    const ragui::RagMetrics metrics;
    auto *content = new QWidget;
    auto *layout = new QVBoxLayout(content);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(kSectionGap);
    detailStack_ = new QStackedWidget;
    detailContentPage_ = createDetailContent();
    detailPlaceholderPage_ = createDetailPlaceholder();
    detailStack_->addWidget(detailContentPage_);
    detailStack_->addWidget(detailPlaceholderPage_);
    layout->addWidget(detailStack_, 1);
    // 文件操作固定在底部一排，不随内容滚动，也不再折行。
    layout->addWidget(createDetailActions());
    // 这里刻意不调用 setMinimumWidth()：QWidget 的显式最小宽度会「覆盖」而不是「抬高」
    // 布局算出来的最小宽度，写死一个数反而会把底部按钮排压进去裁掉。
    // 卡片内容自身的最小宽度约 428px（底部三个按钮 + 内边距），由布局自己顶出来。
    return ragui::makeCard(content, metrics.cardPadH, kSectionGap);
}

void CaseLibraryPage::emitFilter()
{
    if (!filterHandler_) {
        return;
    }
    filterHandler_(currentFilter());
}

void CaseLibraryPage::selectRecord(int row)
{
    if (row >= 0 && row < records_.size()) {
        selectedRow_ = row;
        recordsTable_->selectRow(row);
    }
}

void CaseLibraryPage::updateDetail()
{
    const CaseRecord *record = selectedRecord();
    if (!record) {
        detailStack_->setCurrentWidget(detailPlaceholderPage_);
        selectedTitleLabel_->setText(QStringLiteral("未选择案例"));
        selectedMetaLabel_->setFullText(QString());
        inputEdit_->clear();
        answerBrowser_->clear();
        noteEdit_->clear();
        favoriteButton_->setText(QStringLiteral("收藏"));
        return;
    }
    detailStack_->setCurrentWidget(detailContentPage_);
    selectedTitleLabel_->setText(
        QStringLiteral("%1 · %2").arg(displayMode(record->mode), displayGrounded(*record)));
    // 元信息一行装不下，尾部一定被省略——所以顺序就是优先级：时间和检索耗时是每条案例
    // 都不一样的、值得一眼看的，模型名基本恒定，让它们排在后面被省略掉，完整串进 tooltip。
    selectedMetaLabel_->setFullText(
        QStringLiteral("%1 · 检索 %2 · Embedding: %3 · Reranker: %4")
            .arg(minuteTimestamp(record->updatedAt),
                 retrievalReading(record->retrievalMs),
                 record->embedding.isEmpty() ? QStringLiteral("unknown") : record->embedding,
                 record->reranker.isEmpty() ? QStringLiteral("unknown") : record->reranker));
    inputEdit_->setPlainText(record->inputText);
    answerBrowser_->setPlainText(record->answer);
    noteEdit_->setPlainText(record->note);
    favoriteButton_->setText(record->favorite ? QStringLiteral("取消收藏") : QStringLiteral("收藏"));
}

void CaseLibraryPage::updateActionState()
{
    const bool selected = selectedRecord() != nullptr;
    const bool canReopen = selected && !taskState_.taskRunning;
    reopenButton_->setEnabled(canReopen);
    copyButton_->setEnabled(selected);
    saveNoteButton_->setEnabled(selected);
    favoriteButton_->setEnabled(selected);
    exportButton_->setEnabled(selected);
    const bool canAnnotate = selected && !taskState_.taskRunning;
    helpfulButton_->setEnabled(canAnnotate);
    unhelpfulButton_->setEnabled(canAnnotate);
    candidateButton_->setEnabled(canAnnotate);
}

/// 窄屏下按价值从低到高摘掉元数据列，保证「问题/摘要」永远读得成一句话。
///
/// 摘列顺序：先「更新时间」——列表本来就按它倒序，先后关系由行序本身表达，
/// 精确到秒的时刻在右栏和 tooltip 里都有；再「证据」——无证据是带色的少数派，
/// 摘掉损失的是扫读速度而不是内容。「模式」永远留着：它和「问题/摘要」凑在一起
/// 才构成一行的完整意思。宁可少一列，也不要让表格横向滚动——
/// 一张要横拖才能读完的表，等于没有表。
void CaseLibraryPage::updateColumnBudget()
{
    if (!recordsTable_) {
        return;
    }
    static constexpr int kDropOrder[] = {0, 2};
    static constexpr int kDropWidth[] = {kTimeColumnWidth, kGroundedColumnWidth};
    constexpr int kDropCount = 2;
    const int viewport = recordsTable_->viewport()->width();
    int spent = kModeColumnWidth + kTimeColumnWidth + kGroundedColumnWidth
                + (anyFavorite_ ? kFavoriteColumnWidth : 0);
    int dropped = 0;
    while (dropped < kDropCount && viewport - spent < kSummaryMinWidth) {
        spent -= kDropWidth[dropped];
        ++dropped;
    }
    for (int index = 0; index < kDropCount; ++index) {
        recordsTable_->setColumnHidden(kDropOrder[index], index < dropped);
    }
    recordsTable_->setColumnHidden(5, !anyFavorite_);
}

bool CaseLibraryPage::eventFilter(QObject *watched, QEvent *event)
{
    if (recordsTable_ && watched == recordsTable_->viewport() && event->type() == QEvent::Resize) {
        updateColumnBudget();
    }
    return QWidget::eventFilter(watched, event);
}

CaseRecord *CaseLibraryPage::selectedRecord()
{
    return selectedRow_ >= 0 && selectedRow_ < records_.size() ? &records_[selectedRow_] : nullptr;
}

const CaseRecord *CaseLibraryPage::selectedRecord() const
{
    return selectedRow_ >= 0 && selectedRow_ < records_.size() ? &records_[selectedRow_] : nullptr;
}

void CaseLibraryPage::showStatus(const QString &message)
{
    applyStatus(statusLabel_, message, ragui::Tone::Success);
}
