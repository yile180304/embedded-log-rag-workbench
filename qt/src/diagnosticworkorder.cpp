#include "diagnosticworkorder.h"

#include "ui/uikit.h"

#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QTableWidget>
#include <QTextBrowser>
#include <QVBoxLayout>

namespace {

/// 表头 + 列宽策略以外的配置全部走 ragui，六张表以前各写各的。
void configureTable(QTableWidget *table, const QStringList &headers)
{
    table->setColumnCount(headers.size());
    table->setHorizontalHeaderLabels(headers);
    ragui::applyTableDefaults(table);
    table->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    table->setMinimumHeight(96);
}

/// 机器生成的检索问题：放进浅色内嵌槽，和人写的内容区分开。
QWidget *wrapInWell(QWidget *content)
{
    auto *well = new QFrame;
    well->setObjectName(QStringLiteral("inlineWell"));
    auto *layout = new QVBoxLayout(well);
    layout->setContentsMargins(10, 6, 10, 6);
    layout->setSpacing(0);
    layout->addWidget(content);
    return well;
}

/// 次要操作按钮靠右，不要占满一整行。
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

} // namespace

DiagnosticWorkOrder::DiagnosticWorkOrder(QWidget *parent)
    : QWidget(parent)
{
    const ragui::RagMetrics metrics;
    auto *layout = new QVBoxLayout(this);
    // 外层不再包卡：这里全是自带边框的表格和文本框，套卡就是卡中卡。
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(metrics.space2);

    auto *tables = new QWidget;
    auto *tablesLayout = new QGridLayout(tables);
    tablesLayout->setContentsMargins(0, 0, 0, 0);
    tablesLayout->setHorizontalSpacing(metrics.space2);
    tablesLayout->setVerticalSpacing(metrics.space1);
    tablesLayout->addWidget(ragui::makeSectionTitle(QStringLiteral("Fault Register Snapshot"),
                                                    QStringLiteral("现场事实")), 0, 0);
    tablesLayout->addWidget(ragui::makeSectionTitle(QStringLiteral("Decoded Fault Flags"),
                                                    QStringLiteral("确定性位解码")), 0, 1);

    registerTable_ = new QTableWidget;
    configureTable(registerTable_, {QStringLiteral("Register"), QStringLiteral("Value")});
    registerTable_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    flagTable_ = new QTableWidget;
    configureTable(flagTable_, {QStringLiteral("Group"), QStringLiteral("Flag"), QStringLiteral("Meaning")});
    flagTable_->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    tablesLayout->addWidget(registerTable_, 1, 0);
    tablesLayout->addWidget(flagTable_, 1, 1);
    tablesLayout->setColumnStretch(0, 4);
    tablesLayout->setColumnStretch(1, 6);

    auto *notes = new QWidget;
    auto *notesLayout = new QGridLayout(notes);
    notesLayout->setContentsMargins(0, 0, 0, 0);
    notesLayout->setHorizontalSpacing(metrics.space2);
    notesLayout->setVerticalSpacing(metrics.space1);
    notesLayout->addWidget(ragui::makeSectionTitle(QStringLiteral("确定性观察"),
                                                   QStringLiteral("寄存器明确说明的事实")), 0, 0);
    notesLayout->addWidget(ragui::makeSectionTitle(QStringLiteral("下一步检查"),
                                                   QStringLiteral("可执行排查动作")), 0, 1);
    observationBrowser_ = new QTextBrowser;
    observationBrowser_->setMinimumHeight(88);
    actionBrowser_ = new QTextBrowser;
    actionBrowser_->setMinimumHeight(88);
    notesLayout->addWidget(observationBrowser_, 1, 0);
    notesLayout->addWidget(actionBrowser_, 1, 1);
    notesLayout->setColumnStretch(0, 1);
    notesLayout->setColumnStretch(1, 1);

    generatedQueryLabel_ = new QLabel;
    generatedQueryLabel_->setObjectName(QStringLiteral("generatedQuery"));
    generatedQueryLabel_->setWordWrap(true);
    generatedQueryLabel_->setTextInteractionFlags(Qt::TextSelectableByMouse);

    evidenceStateLabel_ = ragui::makeTag(QStringLiteral("尚未检索"), ragui::Tone::Neutral);
    QWidget *ragHeading = ragui::makeSectionHeader(QStringLiteral("Evidence-backed Diagnosis"),
                                                   QStringLiteral("知识库证据支持的建议"),
                                                   evidenceStateLabel_);
    ragAnswerBrowser_ = new QTextBrowser;
    ragAnswerBrowser_->setMinimumHeight(90);
    reportButton_ = new QPushButton(QStringLiteral("打开 HardFault Markdown 工单"));
    reportButton_->setObjectName(QStringLiteral("secondaryButton"));
    reportButton_->setEnabled(false);

    layout->addWidget(tables, 5);
    layout->addWidget(notes, 4);
    layout->addWidget(wrapInWell(generatedQueryLabel_));
    layout->addWidget(ragHeading);
    layout->addWidget(ragAnswerBrowser_, 3);
    layout->addWidget(trailingRow(reportButton_));
    showPlaceholder();
}

void DiagnosticWorkOrder::clear()
{
    registerTable_->clearContents();
    registerTable_->setRowCount(0);
    flagTable_->clearContents();
    flagTable_->setRowCount(0);
    observationBrowser_->clear();
    actionBrowser_->clear();
    generatedQueryLabel_->clear();
    ragAnswerBrowser_->clear();
    reportButton_->setEnabled(false);
}

void DiagnosticWorkOrder::showPlaceholder()
{
    clear();
    observationBrowser_->setPlaceholderText(QStringLiteral("日志解析后，这里显示寄存器能够确定的观察。"));
    actionBrowser_->setPlaceholderText(QStringLiteral("下一步检查会按可执行顺序列出。"));
    generatedQueryLabel_->setText(QStringLiteral("自动检索问题：等待 HardFault 日志分析"));
    ragAnswerBrowser_->setPlaceholderText(QStringLiteral("规则解析完成后，RAG 会在这里补充手册依据和排查建议。"));
    ragui::setTag(evidenceStateLabel_, QStringLiteral("尚未检索"), ragui::Tone::Neutral);
}

void DiagnosticWorkOrder::render(const HardFaultAnalysisViewModel &analysis, const QString &ragAnswer, bool grounded)
{
    clear();
    const QStringList registerOrder = {
        QStringLiteral("CFSR"), QStringLiteral("HFSR"), QStringLiteral("BFAR"),
        QStringLiteral("MMFAR"), QStringLiteral("PC"), QStringLiteral("LR")};
    for (const QString &name : registerOrder) {
        if (!analysis.registers.contains(name)) {
            continue;
        }
        const int row = registerTable_->rowCount();
        registerTable_->insertRow(row);
        registerTable_->setItem(row, 0, ragui::makeTextItem(name));
        registerTable_->setItem(row, 1, ragui::makeNumericItem(analysis.registers.value(name)));
    }
    flagTable_->setRowCount(analysis.decodedFlags.size());
    for (qsizetype row = 0; row < analysis.decodedFlags.size(); ++row) {
        const DecodedFaultFlagViewModel &flag = analysis.decodedFlags.at(row);
        flagTable_->setItem(row, 0, ragui::makeTextItem(flag.group));
        flagTable_->setItem(row, 1, ragui::makeTextItem(
            QStringLiteral("%1.%2 [bit %3]").arg(flag.registerName, flag.name).arg(flag.bit)));
        flagTable_->setItem(row, 2, ragui::makeTextItem(flag.meaning));
    }
    observationBrowser_->setHtml(QStringLiteral("<ul><li>%1</li></ul>")
                                     .arg(analysis.observations.join(QStringLiteral("</li><li>"))));
    QStringList numberedActions;
    for (qsizetype index = 0; index < analysis.nextActions.size(); ++index) {
        numberedActions.push_back(QStringLiteral("<li>%1</li>").arg(analysis.nextActions.at(index)));
    }
    actionBrowser_->setHtml(QStringLiteral("<ol>%1</ol>").arg(numberedActions.join(QString())));
    generatedQueryLabel_->setText(QStringLiteral("自动检索问题：%1").arg(analysis.generatedQuery));
    ragAnswerBrowser_->setPlainText(ragAnswer);
    ragui::setTag(evidenceStateLabel_,
                  grounded ? QStringLiteral("✓ 有证据") : QStringLiteral("无证据"),
                  grounded ? ragui::Tone::Success : ragui::Tone::Warning);
    reportButton_->setEnabled(true);
}

void DiagnosticWorkOrder::showError(const QString &message)
{
    clear();
    ragui::setTag(evidenceStateLabel_, QStringLiteral("校验失败"), ragui::Tone::Error);
    ragAnswerBrowser_->setPlainText(message);
    generatedQueryLabel_->setText(QStringLiteral("自动检索问题：未启动模型检索"));
}

QPushButton *DiagnosticWorkOrder::reportButton() const
{
    return reportButton_;
}
