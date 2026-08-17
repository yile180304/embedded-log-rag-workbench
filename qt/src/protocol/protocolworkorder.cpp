#include "protocol/protocolworkorder.h"

#include "ui/uikit.h"

#include <QEvent>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QJsonDocument>
#include <QLabel>
#include <QPushButton>
#include <QTableWidget>
#include <QTextBrowser>
#include <QVBoxLayout>

namespace {

void configureTable(QTableWidget *table, const QStringList &headers)
{
    table->setColumnCount(headers.size());
    table->setHorizontalHeaderLabels(headers);
    ragui::applyTableDefaults(table);
    table->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    table->setMinimumHeight(96);
}

/// 协议日志的时间戳，只留时分秒。
///
/// 完整 ISO 串（2026-08-15T10:00:00.000Z）要约 190px，而日期部分在同一份日志里是常量——
/// 九列挤在半栏时，这一列省下的 100px 决定了 Source / Destination 能不能显示完整地址。
/// 完整值仍然进 tooltip。
QString compactTimestamp(const QString &iso)
{
    const qsizetype marker = iso.indexOf(QLatin1Char('T'));
    if (marker < 0 || marker + 1 >= iso.size()) {
        return iso;
    }
    QString time = iso.mid(marker + 1);
    if (time.endsWith(QLatin1Char('Z'))) {
        time.chop(1);
    }
    return time;
}

QWidget *trailingRow(QWidget *widget)
{
    auto *row = new QWidget;
    auto *layout = new QHBoxLayout(row);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addStretch(1);
    layout->addWidget(widget);
    return row;
}

/// 让表格贴着行数收缩，超过 maxRows 行才在自己内部滚动。
///
/// 两张表都靠 stretch 抢高度的结果是：只有 2 条事件时事件表也要占满它那份配额，
/// 白白留出两百多像素空白，而下面的异常表只剩一个表头、RAG 结论整块被挤出屏幕——
/// 这一页最该被读到的东西反而看不见。
///
/// 必须用 setFixedHeight：QTableWidget 的竖直 sizePolicy 默认是 Expanding，
/// 只给 minimum/maximum 的话它照样会一路涨到 maximum。
void fitTableHeight(QTableWidget *table, int minRows, int maxRows)
{
    if (!table) {
        return;
    }
    const ragui::RagMetrics metrics;
    const int chrome = table->horizontalHeader()->height() + 4;
    const int rows = qBound(minRows, table->rowCount(), maxRows);
    table->setFixedHeight(chrome + rows * metrics.rowHeight);
}

} // namespace

ProtocolWorkOrder::ProtocolWorkOrder(QWidget *parent)
    : QWidget(parent)
{
    const ragui::RagMetrics metrics;
    auto *layout = new QVBoxLayout(this);
    // 外层不再包卡：下面两张表自带边框，就是容器本身。
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(metrics.space2);

    // 告警 / 错误条数是这一屏最该先看到的东西，所以提到标题行右侧做成标签；
    // countTone 会在计数为 0 时自动降级成中性灰，避免「错误 0」也是红的。
    warningTag_ = ragui::makeTag(QStringLiteral("告警 0"), ragui::Tone::Neutral);
    errorTag_ = ragui::makeTag(QStringLiteral("错误 0"), ragui::Tone::Neutral);
    auto *severityRow = new QWidget;
    auto *severityLayout = new QHBoxLayout(severityRow);
    severityLayout->setContentsMargins(0, 0, 0, 0);
    severityLayout->setSpacing(metrics.space1);
    severityLayout->addWidget(warningTag_);
    severityLayout->addWidget(errorTag_);
    layout->addWidget(ragui::makeSectionHeader(QStringLiteral("Protocol Rule Facts"),
                                               QStringLiteral("确定性解析，不由 LLM 猜测"),
                                               severityRow));

    summaryLabel_ = new QLabel;
    summaryLabel_->setObjectName(QStringLiteral("protocolSummary"));
    summaryLabel_->setWordWrap(true);
    summaryLabel_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    layout->addWidget(summaryLabel_);

    eventTable_ = new QTableWidget;
    eventTable_->setObjectName(QStringLiteral("protocolEventTable"));
    configureTable(eventTable_, {
        QStringLiteral("Line"), QStringLiteral("Time"), QStringLiteral("Protocol"),
        QStringLiteral("Source"), QStringLiteral("Destination"), QStringLiteral("Seq"),
        QStringLiteral("Length"), QStringLiteral("Interval ms"), QStringLiteral("CRC")});
    // 九列塞进半栏必然溢出。ResizeToContents 会让每列都按最长内容要宽度，
    // 于是两个 Stretch 的地址列被挤到最小宽度、只剩省略号，还常驻一条横向滚动条
    // （截图里表头都被裁成了 "ourc" / "tina"）。
    // 改成显式宽度 + 地址列吸收富余，宽度不够时按 updateEventColumnBudget() 丢列。
    auto *eventHeader = eventTable_->horizontalHeader();
    eventHeader->setSectionResizeMode(QHeaderView::Interactive);
    eventHeader->setSectionResizeMode(3, QHeaderView::Stretch);
    eventHeader->setSectionResizeMode(4, QHeaderView::Stretch);
    eventHeader->resizeSection(0, 58);  // Line
    eventHeader->resizeSection(1, 122); // Time：必须放得下 10:00:00.000，毫秒正是这一列的意义
    eventHeader->resizeSection(2, 76);  // Protocol
    eventHeader->resizeSection(5, 58);  // Seq
    eventHeader->resizeSection(6, 66);  // Length
    eventHeader->resizeSection(7, 88);  // Interval ms
    eventHeader->resizeSection(8, 92);  // CRC：要放得下「不校验 / 未校验」
    // 列预算跟着表格自身的宽度走，不是窗口宽度——中间还隔着一个可拖的 QSplitter。
    eventTable_->viewport()->installEventFilter(this);
    layout->addWidget(eventTable_);

    layout->addWidget(ragui::makeSectionTitle(QStringLiteral("Protocol Anomalies"),
                                              QStringLiteral("序列 / 周期 / 长度 / CRC16")));
    anomalyTable_ = new QTableWidget;
    anomalyTable_->setObjectName(QStringLiteral("protocolAnomalyTable"));
    configureTable(anomalyTable_, {
        QStringLiteral("Severity"), QStringLiteral("Type"), QStringLiteral("Line"), QStringLiteral("Message")});
    anomalyTable_->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Stretch);
    layout->addWidget(anomalyTable_);

    generatedQueryLabel_ = new QLabel;
    // 只读的一行说明，不该填色——填色在这套设计语言里等于"这里能输入"。
    // objectName 承担样式，测试选择器走 testId。
    generatedQueryLabel_->setObjectName(QStringLiteral("mutedText"));
    generatedQueryLabel_->setProperty("testId", QStringLiteral("protocolGeneratedQuery"));
    generatedQueryLabel_->setWordWrap(true);
    generatedQueryLabel_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    layout->addWidget(generatedQueryLabel_);

    evidenceStateLabel_ = ragui::makeTag(QStringLiteral("尚未检索"), ragui::Tone::Neutral);
    layout->addWidget(ragui::makeSectionHeader(QStringLiteral("Evidence-backed Diagnosis"),
                                               QStringLiteral("规则事实之外的知识库解释"),
                                               evidenceStateLabel_));
    ragAnswerBrowser_ = new QTextBrowser;
    ragAnswerBrowser_->setObjectName(QStringLiteral("protocolRagAnswer"));
    ragAnswerBrowser_->setMinimumHeight(90);
    reportButton_ = new QPushButton(QStringLiteral("打开协议日志 Markdown 工单"));
    reportButton_->setObjectName(QStringLiteral("secondaryButton"));
    reportButton_->setProperty("testId", QStringLiteral("openProtocolReport"));
    reportButton_->setEnabled(false);
    layout->addWidget(ragAnswerBrowser_, 1);
    layout->addWidget(trailingRow(reportButton_));
    showPlaceholder();
}

bool ProtocolWorkOrder::eventFilter(QObject *watched, QEvent *event)
{
    if (eventTable_ && watched == eventTable_->viewport() && event->type() == QEvent::Resize) {
        updateEventColumnBudget();
    }
    return QWidget::eventFilter(watched, event);
}

void ProtocolWorkOrder::updateEventColumnBudget()
{
    if (!eventTable_) {
        return;
    }
    // 两个地址列各要 ~120px 才放得下 10.0.0.1:1000 这样的完整地址。放不下就按价值丢列：
    //   · Protocol：同一份日志里是常量，摘要行已经写了「Profile UDP」
    //   · Line：行号和表格自身的行序一一对应，是最容易重建的一列
    // 丢列不丢数据——两列的内容本来就在 tooltip 和摘要里。
    const int width = eventTable_->viewport()->width();
    eventTable_->setColumnHidden(2, width < 820);
    eventTable_->setColumnHidden(0, width < 700);
}

void ProtocolWorkOrder::clear()
{
    eventTable_->clearContents();
    eventTable_->setRowCount(0);
    anomalyTable_->clearContents();
    anomalyTable_->setRowCount(0);
    fitTableHeight(eventTable_, 2, 9);
    fitTableHeight(anomalyTable_, 2, 6);
    summaryLabel_->clear();
    generatedQueryLabel_->clear();
    ragAnswerBrowser_->clear();
    reportButton_->setEnabled(false);
    ragui::setTag(warningTag_, QStringLiteral("告警 0"), ragui::countTone(0, ragui::Tone::Warning));
    ragui::setTag(errorTag_, QStringLiteral("错误 0"), ragui::countTone(0, ragui::Tone::Error));
}

void ProtocolWorkOrder::showPlaceholder()
{
    clear();
    summaryLabel_->setText(QStringLiteral("等待协议日志分析：规则事实会与 RAG 建议分开显示。"));
    generatedQueryLabel_->setText(QStringLiteral("自动检索问题：等待规则分析"));
    ragAnswerBrowser_->setPlaceholderText(QStringLiteral("规则分析完成后，这里显示有来源的建议或明确拒答。"));
    ragui::setTag(evidenceStateLabel_, QStringLiteral("尚未检索"), ragui::Tone::Neutral);
}

void ProtocolWorkOrder::render(
    const ProtocolLogAnalysisViewModel &analysis,
    const QString &ragAnswer,
    bool grounded)
{
    clear();
    summaryLabel_->setText(QStringLiteral("Profile %1 · 行 %2 · 事件 %3")
                               .arg(analysis.profile.toUpper())
                               .arg(analysis.lineCount)
                               .arg(analysis.eventCount));
    ragui::setTag(warningTag_, QStringLiteral("告警 %1").arg(analysis.warningCount),
                  ragui::countTone(analysis.warningCount, ragui::Tone::Warning));
    ragui::setTag(errorTag_, QStringLiteral("错误 %1").arg(analysis.errorCount),
                  ragui::countTone(analysis.errorCount, ragui::Tone::Error));

    eventTable_->setRowCount(analysis.events.size());
    for (qsizetype row = 0; row < analysis.events.size(); ++row) {
        const ProtocolLogEventViewModel &event = analysis.events.at(row);
        eventTable_->setItem(row, 0, ragui::makeNumericItem(QString::number(event.lineNo),
                                                            event.rawMessage));
        eventTable_->setItem(row, 1, ragui::makeTextItem(compactTimestamp(event.timestamp),
                                                        event.timestamp));
        eventTable_->setItem(row, 2, ragui::makeTextItem(event.protocol));
        eventTable_->setItem(row, 3, ragui::makeTextItem(event.source));
        eventTable_->setItem(row, 4, ragui::makeTextItem(event.destination));
        eventTable_->setItem(row, 5, ragui::makeNumericItem(event.sequence));
        eventTable_->setItem(row, 6, ragui::makeNumericItem(event.length));
        eventTable_->setItem(row, 7, ragui::makeNumericItem(event.intervalMs));
        eventTable_->setItem(row, 8, ragui::makeTextItem(event.crcState));
    }

    anomalyTable_->setRowCount(analysis.anomalies.size());
    for (qsizetype row = 0; row < analysis.anomalies.size(); ++row) {
        const ProtocolAnomalyViewModel &anomaly = analysis.anomalies.at(row);
        anomalyTable_->setItem(row, 0, ragui::makeTextItem(anomaly.severity));
        anomalyTable_->setItem(row, 1, ragui::makeTextItem(anomaly.type));
        anomalyTable_->setItem(row, 2, ragui::makeNumericItem(
            anomaly.lineNo > 0 ? QString::number(anomaly.lineNo) : QStringLiteral("-")));
        anomalyTable_->setItem(row, 3, ragui::makeTextItem(anomaly.message));
    }
    // 两张表都按实际行数收缩，省下的高度归下面的 RAG 结论区——
    // 那才是这一页最该被读到的东西。
    fitTableHeight(eventTable_, 2, 9);
    fitTableHeight(anomalyTable_, 2, 6);

    generatedQueryLabel_->setText(QStringLiteral("自动检索问题：%1").arg(analysis.generatedQuery));
    ragAnswerBrowser_->setPlainText(ragAnswer);
    ragui::setTag(evidenceStateLabel_,
                  grounded ? QStringLiteral("✓ 有证据") : QStringLiteral("无证据 · 保留规则事实"),
                  grounded ? ragui::Tone::Success : ragui::Tone::Warning);
    reportButton_->setEnabled(true);
}

void ProtocolWorkOrder::showError(const QString &message)
{
    clear();
    summaryLabel_->setText(QStringLiteral("协议日志校验失败；模型检索未启动。"));
    generatedQueryLabel_->setText(QStringLiteral("自动检索问题：未生成"));
    ragAnswerBrowser_->setPlainText(message);
    ragui::setTag(evidenceStateLabel_, QStringLiteral("校验失败"), ragui::Tone::Error);
}

QPushButton *ProtocolWorkOrder::reportButton() const
{
    return reportButton_;
}
