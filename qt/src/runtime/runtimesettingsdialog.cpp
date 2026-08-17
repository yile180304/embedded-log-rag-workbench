#include "runtimesettingsdialog.h"

#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QListWidgetItem>
#include <QMessageBox>
#include <QPushButton>
#include <QSize>
#include <QSpinBox>
#include <QVBoxLayout>

namespace {

QWidget *pathEditor(QLineEdit *edit, QPushButton *button)
{
    auto *container = new QWidget;
    auto *layout = new QHBoxLayout(container);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(ragui::RagMetrics().space2);
    layout->addWidget(edit, 1);
    layout->addWidget(button);
    return container;
}

/// 让标签按自身宽度靠左，不被 QFormLayout 的字段列拉成一条通栏色块。
QWidget *hugLeft(QWidget *widget, QWidget *trailing = nullptr)
{
    auto *row = new QWidget;
    auto *layout = new QHBoxLayout(row);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(ragui::RagMetrics().space2);
    layout->addWidget(widget);
    if (trailing) {
        layout->addWidget(trailing, 1);
    } else {
        layout->addStretch(1);
    }
    return row;
}

QString loadedText(const QJsonObject &model)
{
    const QString name = model.value(QStringLiteral("name")).toString(QStringLiteral("unknown"));
    return QStringLiteral("%1 · %2")
        .arg(name, model.value(QStringLiteral("loaded")).toBool()
                       ? QStringLiteral("已加载")
                       : QStringLiteral("未加载"));
}

/// Worker 契约取值：starting | ready | busy | failed。
ragui::Tone workerTone(const QString &state)
{
    const QString normalized = state.trimmed().toLower();
    if (normalized == QStringLiteral("ready")) {
        return ragui::Tone::Success;
    }
    if (normalized == QStringLiteral("busy")) {
        return ragui::Tone::Info;
    }
    if (normalized == QStringLiteral("starting")) {
        return ragui::Tone::Warning;
    }
    if (normalized == QStringLiteral("failed")) {
        return ragui::Tone::Error;
    }
    return ragui::Tone::Neutral;
}

/// 同一份表单里索引那行是中文，Worker 这行也要中文，否则中英混排。
/// 契约里的原始英文取值不变，只改显示。
QString displayWorkerState(const QString &state)
{
    const QString normalized = state.trimmed().toLower();
    if (normalized == QStringLiteral("ready"))    return QStringLiteral("已就绪");
    if (normalized == QStringLiteral("busy"))     return QStringLiteral("忙碌");
    if (normalized == QStringLiteral("starting")) return QStringLiteral("启动中");
    if (normalized == QStringLiteral("failed"))   return QStringLiteral("失败");
    return QStringLiteral("未知");
}

} // namespace

RuntimeSettingsDialog::RuntimeSettingsDialog(const RuntimeSettings &settings,
                                             const RuntimeSettings &defaults,
                                             const QJsonObject &healthSnapshot,
                                             const QVector<HealthIssue> &issues,
                                             bool taskRunning,
                                             QWidget *parent)
    : QDialog(parent)
    , defaults_(defaults)
{
    const ragui::RagMetrics metrics;
    setWindowTitle(QStringLiteral("运行设置与健康自检"));
    // 900×620 要完整可用，所以最小尺寸必须小于它（改版前是 760×680，比目标还高）。
    setMinimumSize(720, 400);

    auto *rootLayout = new QVBoxLayout(this);
    rootLayout->setContentsMargins(metrics.space4, metrics.space3, metrics.space4, metrics.space3);
    rootLayout->setSpacing(metrics.space3);

    // 设置与健康快照并排：两栏比竖着堆矮一半，620 高度里才留得出问题列表的位置。
    auto *topRow = new QWidget;
    auto *topLayout = new QHBoxLayout(topRow);
    topLayout->setContentsMargins(0, 0, 0, 0);
    topLayout->setSpacing(metrics.space3);
    topLayout->addWidget(createSettingsGroup(), 5);
    topLayout->addWidget(createHealthGroup(), 4);
    rootLayout->addWidget(topRow);

    // 问题列表按内容占位、不吃掉全部剩余高度：以前它是 stretch=1，
    // 「未发现需要处理的运行问题」这一条会撑成一个 260px 的空白大框。
    rootLayout->addWidget(createIssuesGroup());
    rootLayout->addStretch(1);

    actionStatusLabel_ = new QLabel;
    actionStatusLabel_->setObjectName(QStringLiteral("statusTag"));
    actionStatusLabel_->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    actionStatusLabel_->setWordWrap(true); // 中文提示不允许被截断
    actionStatusLabel_->setVisible(false);
    rootLayout->addWidget(actionStatusLabel_);

    rootLayout->addWidget(createButtonRow());

    populate(settings);
    setHealthSnapshot(healthSnapshot);
    setIssues(issues);
    setTaskRunning(taskRunning);
    // 按内容定高：问题列表现在贴着行数收缩，再固定 620 高就会在底部空出一大片。
    // 宽度仍取 900，因为两栏表单里有长路径。
    adjustSize();
    resize(900, qMax(400, sizeHint().height()));
}

QWidget *RuntimeSettingsDialog::createSettingsGroup()
{
    const ragui::RagMetrics metrics;
    auto *group = new QGroupBox(QStringLiteral("运行设置 v1"));
    auto *form = new QFormLayout(group);
    form->setContentsMargins(0, 0, 0, 0); // 内边距由 QGroupBox 的 QSS padding 提供，别叠两层
    form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    form->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);
    form->setHorizontalSpacing(metrics.space3);
    form->setVerticalSpacing(metrics.space2);

    projectRootEdit_ = new QLineEdit;
    projectRootEdit_->setObjectName(QStringLiteral("runtimeProjectRoot"));
    projectRootEdit_->setPlaceholderText(QStringLiteral("RAG 项目根目录"));
    // 长路径在输入框里只看得到一截，完整值挂 tooltip。
    connect(projectRootEdit_, &QLineEdit::textChanged, projectRootEdit_,
            [this](const QString &text) { projectRootEdit_->setToolTip(text); });
    auto *projectBrowseButton = new QPushButton(QStringLiteral("选择目录"));
    connect(projectBrowseButton, &QPushButton::clicked, this, [this] { chooseProjectRoot(); });
    form->addRow(QStringLiteral("项目目录"), pathEditor(projectRootEdit_, projectBrowseButton));

    pythonExecutableEdit_ = new QLineEdit;
    pythonExecutableEdit_->setObjectName(QStringLiteral("runtimePythonExecutable"));
    pythonExecutableEdit_->setPlaceholderText(QStringLiteral("python.exe 完整路径"));
    connect(pythonExecutableEdit_, &QLineEdit::textChanged, pythonExecutableEdit_,
            [this](const QString &text) { pythonExecutableEdit_->setToolTip(text); });
    auto *pythonBrowseButton = new QPushButton(QStringLiteral("选择 Python"));
    connect(pythonBrowseButton, &QPushButton::clicked, this, [this] { choosePythonExecutable(); });
    form->addRow(QStringLiteral("Python 解释器"), pathEditor(pythonExecutableEdit_, pythonBrowseButton));

    embeddingModelEdit_ = new QLineEdit;
    embeddingModelEdit_->setObjectName(QStringLiteral("runtimeEmbeddingModel"));
    connect(embeddingModelEdit_, &QLineEdit::textChanged, embeddingModelEdit_,
            [this](const QString &text) { embeddingModelEdit_->setToolTip(text); });
    form->addRow(QStringLiteral("Embedding 模型"), embeddingModelEdit_);

    rerankerModelEdit_ = new QLineEdit;
    rerankerModelEdit_->setObjectName(QStringLiteral("runtimeRerankerModel"));
    connect(rerankerModelEdit_, &QLineEdit::textChanged, rerankerModelEdit_,
            [this](const QString &text) { rerankerModelEdit_->setToolTip(text); });
    form->addRow(QStringLiteral("Reranker 模型"), rerankerModelEdit_);

    defaultTopKSpinBox_ = new QSpinBox;
    defaultTopKSpinBox_->setObjectName(QStringLiteral("runtimeDefaultTopK"));
    defaultTopKSpinBox_->setRange(1, 20);
    defaultTopKSpinBox_->setSuffix(QStringLiteral(" 条证据"));
    defaultTopKSpinBox_->setMaximumWidth(150); // 数字框不需要吃满字段列
    form->addRow(QStringLiteral("默认 top-k"), hugLeft(defaultTopKSpinBox_));

    taskTimeoutSpinBox_ = new QSpinBox;
    taskTimeoutSpinBox_->setObjectName(QStringLiteral("runtimeTaskTimeout"));
    taskTimeoutSpinBox_->setRange(30, 3600);
    taskTimeoutSpinBox_->setSuffix(QStringLiteral(" 秒"));
    taskTimeoutSpinBox_->setMaximumWidth(150);
    taskTimeoutSpinBox_->setToolTip(QStringLiteral("本轮只持久化该契约；任务超时恢复将在后续功能启用。"));
    form->addRow(QStringLiteral("任务超时"), hugLeft(taskTimeoutSpinBox_));

    return group;
}

QWidget *RuntimeSettingsDialog::createHealthGroup()
{
    const ragui::RagMetrics metrics;
    auto *group = new QGroupBox(QStringLiteral("Worker 健康快照"));
    auto *form = new QFormLayout(group);
    form->setContentsMargins(0, 0, 0, 0);
    form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    form->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);
    form->setHorizontalSpacing(metrics.space3);
    form->setVerticalSpacing(metrics.space2);

    workerValueLabel_ = ragui::makeTag(QStringLiteral("尚未连接"), ragui::Tone::Neutral);
    form->addRow(QStringLiteral("Worker"), hugLeft(workerValueLabel_));

    pythonValueLabel_ = ragui::makeElidedLabel(QStringLiteral("未知"), QString());
    form->addRow(QStringLiteral("Python"), pythonValueLabel_);

    indexStateLabel_ = ragui::makeTag(QStringLiteral("未知"), ragui::Tone::Neutral);
    indexValueLabel_ = ragui::makeElidedLabel(QString(), QStringLiteral("mutedText"));
    form->addRow(QStringLiteral("索引"), hugLeft(indexStateLabel_, indexValueLabel_));

    modelsValueLabel_ = ragui::makeElidedLabel(QStringLiteral("未知"), QString());
    form->addRow(QStringLiteral("Embedding"), modelsValueLabel_);

    rerankerStateLabel_ = ragui::makeElidedLabel(QStringLiteral("未知"), QString());
    form->addRow(QStringLiteral("Reranker"), rerankerStateLabel_);

    activeRequestValueLabel_ = ragui::makeElidedLabel(QStringLiteral("无"), QString());
    activeRequestValueLabel_->setElideMode(Qt::ElideMiddle); // request id 两头都有信息
    form->addRow(QStringLiteral("Active Request"), activeRequestValueLabel_);

    return group;
}

QWidget *RuntimeSettingsDialog::createIssuesGroup()
{
    auto *group = new QGroupBox(QStringLiteral("可操作问题"));
    auto *layout = new QVBoxLayout(group);
    layout->setContentsMargins(0, 0, 0, 0);
    issuesList_ = new QListWidget;
    issuesList_->setObjectName(QStringLiteral("runtimeHealthIssues"));
    issuesList_->setSelectionMode(QAbstractItemView::NoSelection);
    // 不用交替行色：这里最多几条诊断，不是数据表，斑马纹只会在标签底下透出一条杂色。
    issuesList_->setAlternatingRowColors(false);
    issuesList_->setFrameShape(QFrame::NoFrame);
    issuesList_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    issuesList_->setMinimumHeight(40);
    issuesList_->setMaximumHeight(200); // 实际高度由 fitIssuesHeight() 按行数定
    layout->addWidget(issuesList_);
    return group;
}

QWidget *RuntimeSettingsDialog::createButtonRow()
{
    auto *buttons = new QDialogButtonBox;
    restoreButton_ = buttons->addButton(QStringLiteral("恢复默认"), QDialogButtonBox::ResetRole);
    refreshButton_ = buttons->addButton(QStringLiteral("刷新健康状态"), QDialogButtonBox::ActionRole);
    applyButton_ = buttons->addButton(QStringLiteral("应用并重启 Worker"), QDialogButtonBox::ApplyRole);
    restoreButton_->setObjectName(QStringLiteral("runtimeRestoreDefaults"));
    refreshButton_->setObjectName(QStringLiteral("runtimeRefreshHealth"));
    applyButton_->setObjectName(QStringLiteral("runtimeApplyRestart"));
    restoreButton_->setProperty("testId", QStringLiteral("runtimeRestoreDefaults"));
    refreshButton_->setProperty("testId", QStringLiteral("runtimeRefreshHealth"));
    applyButton_->setProperty("testId", QStringLiteral("runtimeApplyRestart"));
    // 主操作用 variant 动态属性上主按钮外观：objectName 是 smoke 的定位键，不能改成 primaryButton。
    applyButton_->setProperty("variant", QStringLiteral("primary"));
    ragui::repolish(applyButton_);
    restoreButton_->setToolTip(QStringLiteral("只把表单恢复成自动发现的默认值，点“应用并重启 Worker”才会保存。"));
    refreshButton_->setToolTip(QStringLiteral("重新拉取 Worker 的 Health Snapshot，不改动任何设置。"));
    applyButton_->setToolTip(QStringLiteral("保存设置并用新配置重启 Worker。"));

    auto *closeButton = buttons->addButton(QDialogButtonBox::Close);
    // QDialogButtonBox 的标准按钮默认走英文文案，这一排其余都是中文，统一掉。
    closeButton->setText(QStringLiteral("关闭"));
    connect(restoreButton_, &QPushButton::clicked, this, [this] { restoreDefaults(); });
    connect(refreshButton_, &QPushButton::clicked, this, [this] { refreshHealth(); });
    connect(applyButton_, &QPushButton::clicked, this, [this] { applyAndRestart(); });
    connect(closeButton, &QPushButton::clicked, this, &QDialog::reject);
    return buttons;
}

void RuntimeSettingsDialog::setApplyHandler(ApplyHandler handler)
{
    applyHandler_ = std::move(handler);
}

void RuntimeSettingsDialog::setRefreshHandler(RefreshHandler handler)
{
    refreshHandler_ = std::move(handler);
}

void RuntimeSettingsDialog::setHealthSnapshot(const QJsonObject &snapshot)
{
    if (snapshot.isEmpty()) {
        ragui::setTag(workerValueLabel_, QStringLiteral("尚未连接"), ragui::Tone::Neutral);
        pythonValueLabel_->setFullText(QStringLiteral("未知"));
        ragui::setTag(indexStateLabel_, QStringLiteral("未知"), ragui::Tone::Neutral);
        indexValueLabel_->setFullText(QString());
        modelsValueLabel_->setFullText(QStringLiteral("未知"));
        rerankerStateLabel_->setFullText(QStringLiteral("未知"));
        activeRequestValueLabel_->setFullText(QStringLiteral("无"));
        return;
    }
    const QString worker = snapshot.value(QStringLiteral("worker")).toString(QStringLiteral("unknown"));
    // 索引那一行用中文标签，Worker 这行如果直接把协议里的 "ready" 原样显示，
    // 同一个表单里就会一半中文一半英文。这里统一成中文，原始值进 tooltip 便于排错。
    ragui::setTag(workerValueLabel_, displayWorkerState(worker), workerTone(worker));
    workerValueLabel_->setToolTip(QStringLiteral("worker=%1").arg(worker));
    pythonValueLabel_->setFullText(
        snapshot.value(QStringLiteral("python_version")).toString(QStringLiteral("unknown")));

    const QJsonObject index = snapshot.value(QStringLiteral("index")).toObject();
    const bool indexReady = index.value(QStringLiteral("ready")).toBool();
    ragui::setTag(indexStateLabel_, indexReady ? QStringLiteral("已就绪") : QStringLiteral("尚未建立"),
                  indexReady ? ragui::Tone::Success : ragui::Tone::Warning);
    indexValueLabel_->setFullText(
        indexReady ? QStringLiteral("%1 chunks · 更新于 %2")
                         .arg(index.value(QStringLiteral("chunks")).toInt())
                         .arg(index.value(QStringLiteral("updated_at"))
                                  .toString(QStringLiteral("时间未知")))
                   : QStringLiteral("在知识源管理页重建索引后可用"));

    const QJsonObject models = snapshot.value(QStringLiteral("models")).toObject();
    modelsValueLabel_->setFullText(loadedText(models.value(QStringLiteral("embedding")).toObject()));
    rerankerStateLabel_->setFullText(loadedText(models.value(QStringLiteral("reranker")).toObject()));

    const QString activeRequest = snapshot.value(QStringLiteral("active_request_id")).toString();
    activeRequestValueLabel_->setFullText(activeRequest.isEmpty() ? QStringLiteral("无") : activeRequest);
}

void RuntimeSettingsDialog::addIssueRow(const QString &badge, const QString &text, ragui::Tone tone)
{
    auto *row = new QWidget;
    auto *layout = new QHBoxLayout(row);
    layout->setContentsMargins(0, 2, 0, 2);
    layout->setSpacing(ragui::RagMetrics().space2);
    layout->addWidget(ragui::makeTag(badge, tone));
    layout->addWidget(ragui::makeElidedLabel(text, QString()), 1);

    auto *item = new QListWidgetItem;
    // 行高由内嵌控件决定，不能拍脑袋写常数：状态标签的字号和内边距随主题变，
    // 高度给小了控件会被裁，标签和文字就会叠在一起。+4 是 QSS 里给这个列表留的行内边距。
    item->setSizeHint(QSize(0, row->sizeHint().height() + 4));
    issuesList_->addItem(item);
    issuesList_->setItemWidget(item, row);
}

void RuntimeSettingsDialog::setIssues(const QVector<HealthIssue> &issues)
{
    issuesList_->clear();
    if (issues.isEmpty()) {
        addIssueRow(QStringLiteral("正常"), QStringLiteral("未发现需要处理的运行问题。"),
                    ragui::Tone::Success);
        fitIssuesHeight();
        return;
    }
    for (const HealthIssue &issue : issues) {
        const bool fatal = issue.severity == HealthSeverity::Error;
        addIssueRow(fatal ? QStringLiteral("错误") : QStringLiteral("警告"),
                    QStringLiteral("%1  建议：%2").arg(issue.summary, issue.action),
                    fatal ? ragui::Tone::Error : ragui::Tone::Warning);
    }
    fitIssuesHeight();
}

void RuntimeSettingsDialog::fitIssuesHeight()
{
    // 按内容定高。只给上限不够——一条「未发现需要处理的运行问题」照样会留出
    // 一个一百多像素的空框；这里让列表贴着行数收缩，超过约四条才滚动。
    //
    // 用 item()->sizeHint() 而不是 sizeHintForRow()：后者问的是 delegate 对
    // 「文字」的意见，而这些行的内容是 setItemWidget() 塞进去的控件，delegate 看不见，
    // 于是会算出一个偏小的高度，列表贴着它收缩后把控件裁掉。
    int content = 2 * issuesList_->frameWidth() + 4;
    for (int row = 0; row < issuesList_->count(); ++row) {
        content += issuesList_->item(row)->sizeHint().height();
    }
    issuesList_->setFixedHeight(qBound(40, content, 200));
}

void RuntimeSettingsDialog::setActionStatus(const QString &text, ragui::Tone tone)
{
    if (text.isEmpty()) {
        actionStatusLabel_->clear();
        actionStatusLabel_->setToolTip(QString());
        actionStatusLabel_->setVisible(false);
        return;
    }
    actionStatusLabel_->setToolTip(text);
    ragui::setTag(actionStatusLabel_, text, tone);
    actionStatusLabel_->setVisible(true);
}

void RuntimeSettingsDialog::setTaskRunning(bool running)
{
    applyButton_->setEnabled(!running);
    restoreButton_->setEnabled(!running);
    setActionStatus(running ? QStringLiteral("诊断任务正在运行；可刷新健康状态，但暂不能应用或恢复设置。")
                            : QString(),
                    ragui::Tone::Warning);
}

RuntimeSettings RuntimeSettingsDialog::currentSettings() const
{
    return {
        projectRootEdit_->text().trimmed(),
        pythonExecutableEdit_->text().trimmed(),
        embeddingModelEdit_->text().trimmed(),
        rerankerModelEdit_->text().trimmed(),
        defaultTopKSpinBox_->value(),
        taskTimeoutSpinBox_->value(),
    };
}

void RuntimeSettingsDialog::populate(const RuntimeSettings &settings)
{
    projectRootEdit_->setText(settings.projectRoot);
    projectRootEdit_->setCursorPosition(0);
    pythonExecutableEdit_->setText(settings.pythonExecutable);
    pythonExecutableEdit_->setCursorPosition(0);
    embeddingModelEdit_->setText(settings.embeddingModel);
    rerankerModelEdit_->setText(settings.rerankerModel);
    defaultTopKSpinBox_->setValue(settings.defaultTopK);
    taskTimeoutSpinBox_->setValue(settings.taskTimeoutSeconds);
}

void RuntimeSettingsDialog::chooseProjectRoot()
{
    const QString path = QFileDialog::getExistingDirectory(
        this, QStringLiteral("选择 RAG 项目根目录"), projectRootEdit_->text());
    if (!path.isEmpty()) {
        projectRootEdit_->setText(path);
    }
}

void RuntimeSettingsDialog::choosePythonExecutable()
{
    const QString path = QFileDialog::getOpenFileName(
        this, QStringLiteral("选择 Python 解释器"), pythonExecutableEdit_->text(),
        QStringLiteral("Python (python.exe);;All files (*)"));
    if (!path.isEmpty()) {
        pythonExecutableEdit_->setText(path);
    }
}

void RuntimeSettingsDialog::applyAndRestart()
{
    const RuntimeSettings settings = currentSettings();
    const QVector<HealthIssue> issues = runRuntimePreflight(settings);
    setIssues(issues);
    if (hasPreflightErrors(issues)) {
        setActionStatus(QStringLiteral("设置未保存：请先修正上面的错误。"), ragui::Tone::Error);
        return;
    }
    if (!applyHandler_ || !applyHandler_(settings)) {
        setActionStatus(QStringLiteral("设置未应用：Worker 当前无法重启，请查看运行日志。"),
                        ragui::Tone::Error);
        return;
    }
    setActionStatus(QStringLiteral("设置已保存，正在使用新配置重启 Worker。"), ragui::Tone::Success);
}

void RuntimeSettingsDialog::restoreDefaults()
{
    const QMessageBox::StandardButton choice = QMessageBox::question(
        this,
        QStringLiteral("恢复默认运行设置"),
        QStringLiteral("将表单恢复为自动发现的项目、Python 和默认模型参数。确认继续吗？"),
        QMessageBox::Yes | QMessageBox::No,
        QMessageBox::No);
    if (choice == QMessageBox::Yes) {
        populate(defaults_);
        setIssues(runRuntimePreflight(defaults_));
        setActionStatus(QStringLiteral("表单已恢复默认；点击“应用并重启 Worker”后才会保存。"),
                        ragui::Tone::Info);
    }
}

void RuntimeSettingsDialog::refreshHealth()
{
    if (!refreshHandler_ || !refreshHandler_()) {
        setActionStatus(QStringLiteral("当前无法刷新：Worker 尚未 Ready 或已有刷新请求。"),
                        ragui::Tone::Warning);
        return;
    }
    setActionStatus(QStringLiteral("已请求最新 Health Snapshot。"), ragui::Tone::Info);
}
