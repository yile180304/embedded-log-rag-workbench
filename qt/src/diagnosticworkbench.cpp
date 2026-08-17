#include "diagnosticworkbench.h"

#include "diagnosisviewmodel.h"
#include "runtime/runtimesettingsdialog.h"
#include "ui/theme.h"
#include "ui/uikit.h"
#include "workspace/caselibrarypage.h"
#include "workspace/diagnosisworkspacepage.h"
#include "workspace/evaluationcenterpage.h"
#include "workspace/knowledgecenterpage.h"

#include <QApplication>
#include <QClipboard>
#include <QCoreApplication>
#include <QDesktopServices>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSettings>
#include <QSignalBlocker>
#include <QSplitter>
#include <QStackedWidget>
#include <QStatusBar>
#include <QStyle>
#include <QUrl>
#include <QVBoxLayout>

namespace {
constexpr int kActivityCollapsedHeight = 56;
constexpr int kActivityExpandedHeight = 220;
constexpr int kNavIconSize = 21;
} // namespace

DiagnosticWorkbench::DiagnosticWorkbench(QWidget *parent)
    : QMainWindow(parent)
{
    setWindowTitle(QStringLiteral("STM32F4 RAG 智能诊断工作台"));
    resize(1440, 900);
    setMinimumSize(1080, 700);

    auto *centralWidget = new ragui::SurfaceBackground;
    auto *rootLayout = new QHBoxLayout(centralWidget);
    // 布局边距必须等于自绘白面板的内缩量，否则内容会压到薄荷底上。
    const int inset = ragui::SurfaceBackground::inset();
    rootLayout->setContentsMargins(inset, inset, inset, inset);
    rootLayout->setSpacing(0);
    rootLayout->addWidget(createNavigationPanel());

    auto *content = new QWidget;
    auto *contentLayout = new QVBoxLayout(content);
    contentLayout->setContentsMargins(28, 22, 28, 20);
    contentLayout->setSpacing(18);
    contentLayout->addWidget(createStatusStrip());

    workspaceStack_ = new QStackedWidget;
    workspaceStack_->setObjectName(QStringLiteral("workspaceStack"));
    diagnosisPage_ = new DiagnosisWorkspacePage;
    caseLibraryPage_ = new CaseLibraryPage;
    knowledgeCenterPage_ = new KnowledgeCenterPage;
    evaluationCenterPage_ = new EvaluationCenterPage;
    workspaceStack_->addWidget(diagnosisPage_);
    workspaceStack_->addWidget(caseLibraryPage_);
    workspaceStack_->addWidget(knowledgeCenterPage_);
    workspaceStack_->addWidget(evaluationCenterPage_);
    workspaceSplitter_ = new QSplitter(Qt::Vertical);
    workspaceSplitter_->setObjectName(QStringLiteral("workspaceActivitySplitter"));
    workspaceSplitter_->setChildrenCollapsible(false);
    workspaceSplitter_->addWidget(workspaceStack_);
    workspaceSplitter_->addWidget(createRuntimeActivityPanel());
    workspaceSplitter_->setStretchFactor(0, 1);
    workspaceSplitter_->setStretchFactor(1, 0);
    contentLayout->addWidget(workspaceSplitter_, 1);
    rootLayout->addWidget(content, 1);
    setCentralWidget(centralWidget);

    statusBar()->showMessage(QStringLiteral("工作台已就绪，等待连接 Python RAG 引擎"));
    applyTheme();
    setActivityExpanded(false);
    setActiveNavigation(diagnosisNavigationButton_);
    bindDiagnosisPage();
    bindCaseLibraryPage();
    bindKnowledgeCenterPage();
    bindEvaluationCenterPage();
    configureRuntime();
}

QPushButton *DiagnosticWorkbench::addNavigationButton(QVBoxLayout *layout, const QString &text,
                                                     const QString &testId, ragui::Glyph glyph)
{
    auto *button = new QPushButton(text);
    // objectName 保持恒定，选中态走 active 动态属性。
    // 改版前是在 navigationButton / navigationButtonActive 之间来回换 objectName，
    // 四个页面切换函数里各复制了一份 15 行的 unpolish/polish。
    button->setObjectName(QStringLiteral("navigationButton"));
    button->setProperty("testId", testId);
    button->setProperty("active", QStringLiteral("false"));
    // 图标要随选中态换色重画（QSS 管不到 QIcon 的像素），所以把字形记在属性里，
    // 免得为四个按钮各加一个成员变量。
    button->setProperty("glyph", static_cast<int>(glyph));
    button->setIcon(ragui::makeGlyphIcon(glyph, ragui::RagPalette().textMuted, kNavIconSize));
    button->setIconSize(QSize(kNavIconSize, kNavIconSize));
    button->setCursor(Qt::PointingHandCursor);
    layout->addWidget(button);
    return button;
}

void DiagnosticWorkbench::setActiveNavigation(QPushButton *active)
{
    const ragui::RagPalette palette;
    for (QPushButton *button : {diagnosisNavigationButton_, caseLibraryNavigationButton_,
                                knowledgeNavigationButton_, evaluationNavigationButton_}) {
        if (!button) {
            continue;
        }
        const bool selected = (button == active);
        ragui::setDynamicProperty(button, "active",
                                  selected ? QStringLiteral("true") : QStringLiteral("false"));
        const auto glyph = static_cast<ragui::Glyph>(button->property("glyph").toInt());
        button->setIcon(ragui::makeGlyphIcon(
            glyph, selected ? palette.accentText : palette.textMuted, kNavIconSize));
    }
}

QWidget *DiagnosticWorkbench::createNavigationPanel()
{
    auto *navigation = new QFrame;
    navigation->setObjectName(QStringLiteral("navigationRail"));
    navigation->setFixedWidth(212);
    auto *layout = new QVBoxLayout(navigation);
    layout->setContentsMargins(14, 22, 20, 20);
    layout->setSpacing(6);

    auto *brandBlock = new QWidget;
    auto *brandLayout = new QVBoxLayout(brandBlock);
    brandLayout->setContentsMargins(14, 0, 8, 0);
    brandLayout->setSpacing(4);
    auto *brand = new QLabel(QStringLiteral("RAG 诊断台"));
    brand->setObjectName(QStringLiteral("navigationBrand"));
    auto *caption = new QLabel(QStringLiteral("STM32F4 本地知识增强诊断"));
    caption->setObjectName(QStringLiteral("navigationCaption"));
    caption->setWordWrap(true);
    brandLayout->addWidget(brand);
    brandLayout->addWidget(caption);
    layout->addWidget(brandBlock);
    layout->addSpacing(26);

    diagnosisNavigationButton_ =
        addNavigationButton(layout, QStringLiteral("诊断工作区"),
                            QStringLiteral("diagnosisNavigation"), ragui::Glyph::Pulse);
    caseLibraryNavigationButton_ =
        addNavigationButton(layout, QStringLiteral("诊断案例库"),
                            QStringLiteral("caseLibraryNavigation"), ragui::Glyph::List);
    knowledgeNavigationButton_ =
        addNavigationButton(layout, QStringLiteral("知识源管理"),
                            QStringLiteral("knowledgeNavigation"), ragui::Glyph::Book);
    evaluationNavigationButton_ =
        addNavigationButton(layout, QStringLiteral("评估中心"),
                            QStringLiteral("evaluationNavigation"), ragui::Glyph::Chart);
    layout->addStretch(1);

    auto *footnoteBlock = new QWidget;
    auto *footnoteLayout = new QVBoxLayout(footnoteBlock);
    footnoteLayout->setContentsMargins(14, 0, 8, 0);
    footnoteLayout->setSpacing(0);
    auto *scope = new QLabel(QStringLiteral("本地单用户\n证据优先 · 无证据拒答"));
    scope->setObjectName(QStringLiteral("navigationFootnote"));
    scope->setWordWrap(true);
    footnoteLayout->addWidget(scope);
    layout->addWidget(footnoteBlock);

    connect(diagnosisNavigationButton_, &QPushButton::clicked, this, [this] { showDiagnosisWorkspace(); });
    connect(caseLibraryNavigationButton_, &QPushButton::clicked, this, [this] { showCaseLibrary(); });
    connect(knowledgeNavigationButton_, &QPushButton::clicked, this, [this] { showKnowledgeCenter(); });
    connect(evaluationNavigationButton_, &QPushButton::clicked, this, [this] { showEvaluationCenter(); });
    return navigation;
}

QWidget *DiagnosticWorkbench::createStatusStrip()
{
    // 单行运行状态条。改版前这里是一张 110px 高的卡片，装着标题、副标题和 2×2 的状态胶囊；
    // 标题和副标题是写给开发者看的实现说明，导航栏已经标明位置，所以整块删掉。
    // 现在它不再成卡：直接裸露在白面板上，像参考稿顶部那一行。
    auto *strip = new QFrame;
    strip->setObjectName(QStringLiteral("statusStrip"));
    auto *layout = new QHBoxLayout(strip);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(14);

    // Worker 也用行内状态，不用填色胶囊。
    //
    // 在这套设计语言里填色等于"可点/可编辑"——一个灰胶囊夹在四条纯文字状态和一个真按钮
    // 之间，会被读成禁用按钮，而且它抢走的注意力恰恰给了这一行里最不需要操作的那一项。
    // 异常态由 QLabel#statusInline[tone="error"] 变红加粗表达，够用。
    workerStatusLabel_ = ragui::makeInlineStatus();
    ragui::setTag(workerStatusLabel_, QStringLiteral("Worker · 待启动"), ragui::Tone::Neutral);
    layout->addWidget(workerStatusLabel_);

    engineStatusLabel_ = ragui::makeInlineStatus(QStringLiteral("引擎 · 空闲"));
    engineStatusLabel_->setObjectName(QStringLiteral("statusInline"));
    engineStatusLabel_->setProperty("engineState", QStringLiteral("Idle"));
    layout->addWidget(engineStatusLabel_);
    layout->addWidget(ragui::makeVDivider());

    indexStatusLabel_ = ragui::makeInlineStatus(QStringLiteral("索引 · 待连接"));
    pythonStatusLabel_ = ragui::makeInlineStatus(QStringLiteral("Python · 待检测"));
    projectStatusLabel_ = ragui::makeInlineStatus(QStringLiteral("项目 · 待检测"));
    layout->addWidget(indexStatusLabel_);
    layout->addWidget(pythonStatusLabel_);
    layout->addWidget(projectStatusLabel_);
    layout->addStretch(1);

    // 重建索引的入口统一收在「知识源管理」页，这里不再放第二个同名按钮。
    settingsButton_ = new QPushButton(QStringLiteral("运行设置"));
    settingsButton_->setObjectName(QStringLiteral("secondaryButton"));
    settingsButton_->setProperty("testId", QStringLiteral("runtimeSettingsButton"));
    settingsButton_->setIcon(
        ragui::makeGlyphIcon(ragui::Glyph::Sliders, ragui::RagPalette().textMuted, 18));
    settingsButton_->setIconSize(QSize(18, 18));
    settingsButton_->setCursor(Qt::PointingHandCursor);
    layout->addWidget(settingsButton_);
    return strip;
}

QWidget *DiagnosticWorkbench::createRuntimeActivityPanel()
{
    auto *card = new QFrame;
    card->setObjectName(QStringLiteral("card"));
    auto *layout = new QVBoxLayout(card);
    layout->setContentsMargins(18, 12, 18, 12);
    layout->setSpacing(8);

    auto *heading = new QWidget;
    auto *headingLayout = new QHBoxLayout(heading);
    headingLayout->setContentsMargins(0, 0, 0, 0);
    headingLayout->setSpacing(8);
    activityToggleButton_ = new QPushButton;
    activityToggleButton_->setProperty("variant", QStringLiteral("ghost"));
    activityToggleButton_->setCheckable(true);
    activityToggleButton_->setCursor(Qt::PointingHandCursor);
    headingLayout->addWidget(activityToggleButton_);
    // 折叠时把最后一条日志摘出来显示，这样平时不占空间也知道 Worker 在干什么。
    activityPreviewLabel_ = ragui::makeElidedLabel(QString(), QStringLiteral("faintText"));
    headingLayout->addWidget(activityPreviewLabel_, 1);
    indexSummaryLabel_ = new QLabel(QStringLiteral("索引摘要：尚未执行"));
    indexSummaryLabel_->setObjectName(QStringLiteral("mutedText"));
    headingLayout->addWidget(indexSummaryLabel_, 0, Qt::AlignRight);

    logEdit_ = new QPlainTextEdit;
    logEdit_->setObjectName(QStringLiteral("runtimeActivityLog"));
    logEdit_->setReadOnly(true);
    logEdit_->setPlaceholderText(QStringLiteral("运行信息将在这里持续显示。"));
    logEdit_->setMaximumBlockCount(2000);
    // 不再设 maximumHeight：改版前上限 110px，导致底部 splitter 永远拖不大，
    // 排错时日志只有两行可见。
    logEdit_->setMinimumHeight(96);

    layout->addWidget(heading);
    layout->addWidget(logEdit_, 1);

    connect(activityToggleButton_, &QPushButton::toggled, this,
            [this](bool checked) { setActivityExpanded(checked); });
    return card;
}

void DiagnosticWorkbench::setActivityExpanded(bool expanded)
{
    if (!activityToggleButton_ || !logEdit_ || !workspaceSplitter_) {
        return;
    }
    QSignalBlocker blocker(activityToggleButton_);
    activityToggleButton_->setChecked(expanded);
    activityToggleButton_->setText(expanded ? QStringLiteral("▾  运行日志")
                                            : QStringLiteral("▸  运行日志"));
    activityToggleButton_->setToolTip(expanded ? QStringLiteral("收起运行日志")
                                               : QStringLiteral("展开运行日志（Python 输出、模型加载与错误摘要）"));
    logEdit_->setVisible(expanded);
    activityPreviewLabel_->setVisible(!expanded);

    const int total = qMax(workspaceSplitter_->height(), 400);
    const int activity = expanded ? kActivityExpandedHeight : kActivityCollapsedHeight;
    workspaceSplitter_->setSizes({qMax(200, total - activity), activity});
}

void DiagnosticWorkbench::applyTheme()
{
    // 全项目唯一的样式加载入口。所有 token 定义在 ui/theme.h，
    // 任何页面都不要自己调用 setStyleSheet()。
    setStyleSheet(ragui::buildThemeStyleSheet());
}

void DiagnosticWorkbench::bindDiagnosisPage()
{
    diagnosisPage_->setQueryHandler([this](const QString &question, int topK) {
        return startQuery(question, topK);
    });
    diagnosisPage_->setHardFaultHandler([this](const QString &log, int topK) {
        return startHardFaultDiagnosis(log, topK);
    });
    diagnosisPage_->setProtocolLogHandler([this](const QJsonObject &payload, int topK) {
        return startProtocolLogDiagnosis(payload, topK);
    });
    diagnosisPage_->setReportHandler([this](const QString &path) { openReport(path); });
    diagnosisPage_->setStatusHandler([this](const QString &message, int timeoutMs) {
        statusBar()->showMessage(message, timeoutMs);
    });
    connect(settingsButton_, &QPushButton::clicked, this, [this] { openRuntimeSettings(); });
}

void DiagnosticWorkbench::bindCaseLibraryPage()
{
    caseLibraryPage_->setFilterHandler([this](const CaseFilter &filter) { refreshCaseLibrary(filter); });
    caseLibraryPage_->setReopenHandler([this](const CaseRecord &record) { return reopenCase(record); });
    caseLibraryPage_->setNoteHandler([this](const QString &id, const QString &note) {
        return updateCaseNote(id, note);
    });
    caseLibraryPage_->setFavoriteHandler([this](const QString &id, bool favorite) {
        return updateCaseFavorite(id, favorite);
    });
    caseLibraryPage_->setCopyHandler([this](const QString &text) { copyCaseText(text); });
    caseLibraryPage_->setExportHandler([this](const CaseRecord &record) { return exportCase(record); });
    caseLibraryPage_->setFeedbackHandler(
        [this](const CaseRecord &record, const QString &feedback) { return recordCaseFeedback(record, feedback); });
    caseLibraryPage_->setCandidateHandler(
        [this](const CaseRecord &record) { return createEvaluationCandidate(record); });
}

void DiagnosticWorkbench::bindKnowledgeCenterPage()
{
    knowledgeCenterPage_->setRefreshHandler([this] { return processBridge_ && processBridge_->requestHealth(); });
    knowledgeCenterPage_->setReindexHandler([this] { return startReindex(); });
}

void DiagnosticWorkbench::bindEvaluationCenterPage()
{
    evaluationCenterPage_->setApproveHandler(
        [this](const EvaluationCandidate &candidate) { return approveEvaluationCandidate(candidate); });
    evaluationCenterPage_->setRejectHandler(
        [this](const QString &id) { return rejectEvaluationCandidate(id); });
    evaluationCenterPage_->setRunHandler([this](int topK) { return startEvaluation(topK); });
    evaluationCenterPage_->setImportHandler([this] { return importEvaluationCandidates(); });
}

void DiagnosticWorkbench::configureRuntime()
{
    runtimeDefaults_ = RuntimeSettings::defaults(QCoreApplication::applicationDirPath());
    QSettings settings;
    RuntimeSettingsStore store(settings);
    runtimeSettings_ = store.load(runtimeDefaults_);
    knowledgeCenterPage_->setProjectRoot(runtimeSettings_.projectRoot);
    configureCaseStore(runtimeSettings_.projectRoot);
    healthIssues_ = runRuntimePreflight(runtimeSettings_);
    const RagRuntimeConfig config{
        runtimeSettings_.projectRoot,
        runtimeSettings_.pythonExecutable,
        runtimeSettings_.embeddingModel,
        runtimeSettings_.rerankerModel,
        runtimeSettings_.defaultTopK,
        runtimeSettings_.taskTimeoutSeconds,
    };
    processBridge_ = std::make_unique<RagProcessBridge>(config, this);
    diagnosisPage_->setDefaultTopK(runtimeSettings_.defaultTopK);
    updateRuntimeStatus();

    processBridge_->setStateHandler([this](EngineState state) { handleEngineState(state); });
    processBridge_->setWorkerStateHandler([this](WorkerState state) { handleWorkerState(state); });
    processBridge_->setHealthHandler([this](const QJsonObject &snapshot) { updateHealthSnapshot(snapshot); });
    processBridge_->setLogHandler([this](const QString &message) { appendLog(message); });
    processBridge_->setProgressHandler([this](RagOperation operation, const QJsonObject &progress) {
        if (operation == RagOperation::Evaluation) {
            evaluationCenterPage_->showProgress(progress);
        }
    });
    processBridge_->setSuccessHandler([this](RagOperation operation, const QJsonObject &result) {
        if (operation == RagOperation::Query
            || operation == RagOperation::HardFaultDiagnosis
            || operation == RagOperation::ProtocolLogDiagnosis) {
            renderDiagnosis(result);
        } else if (operation == RagOperation::Reindex) {
            renderIngestSummary(result);
        } else {
            finishEvaluationSuccess(result);
        }
    });
    processBridge_->setFailureHandler([this](RagOperation operation, const QString &message, const QJsonObject &details) {
        if (operation == RagOperation::HardFaultDiagnosis) {
            handleHardFaultFailure(message);
        } else if (operation == RagOperation::ProtocolLogDiagnosis) {
            handleProtocolLogFailure(message);
        } else if (operation == RagOperation::Reindex) {
            reindexRunning_ = false;
            appendLog(QStringLiteral("索引重建失败：%1").arg(message));
            const QJsonValue sources = details.value(QStringLiteral("sources"));
            if (sources.isArray()) {
                knowledgeCenterPage_->setRecords(knowledgeSourceRecordsFromJson(sources.toArray()));
            }
            knowledgeCenterPage_->setReindexPhase(ReindexPhase::Failed, message);
            statusBar()->showMessage(QStringLiteral("索引重建失败，请查看知识源管理与 Runtime Activity"), 8000);
        } else if (operation == RagOperation::Evaluation) {
            appendLog(QStringLiteral("评估失败：%1").arg(message));
            finishEvaluationFailure(message);
        } else {
            handleProcessFailure(message);
        }
    });

    appendLog(QStringLiteral("项目目录：%1").arg(config.projectRoot));
    appendLog(QStringLiteral("Python：%1").arg(config.pythonExecutable));
    if (hasPreflightErrors(healthIssues_)) {
        appendLog(QStringLiteral("运行设置预检失败，请打开“运行设置”修正。"));
        handleWorkerState(WorkerState::Failed);
    } else {
        processBridge_->startWorker();
    }
}

void DiagnosticWorkbench::openRuntimeSettings()
{
    if (settingsDialog_) {
        settingsDialog_->raise();
        settingsDialog_->activateWindow();
        return;
    }
    settingsDialog_ = new RuntimeSettingsDialog(
        runtimeSettings_, runtimeDefaults_, healthSnapshot_, healthIssues_, processBridge_->isRunning(), this);
    settingsDialog_->setAttribute(Qt::WA_DeleteOnClose);
    settingsDialog_->setApplyHandler([this](const RuntimeSettings &settings) {
        return applyRuntimeSettings(settings);
    });
    settingsDialog_->setRefreshHandler([this] { return processBridge_->requestHealth(); });
    connect(settingsDialog_, &QObject::destroyed, this, [this] { settingsDialog_ = nullptr; });
    settingsDialog_->show();
}

bool DiagnosticWorkbench::applyRuntimeSettings(const RuntimeSettings &settings)
{
    if (processBridge_->isRunning()) {
        return false;
    }
    QSettings qSettings;
    RuntimeSettingsStore store(qSettings);
    store.save(settings);
    runtimeSettings_ = settings;
    knowledgeCenterPage_->setProjectRoot(runtimeSettings_.projectRoot);
    configureCaseStore(runtimeSettings_.projectRoot);
    healthIssues_ = runRuntimePreflight(runtimeSettings_);
    const RagRuntimeConfig config{
        settings.projectRoot,
        settings.pythonExecutable,
        settings.embeddingModel,
        settings.rerankerModel,
        settings.defaultTopK,
        settings.taskTimeoutSeconds,
    };
    diagnosisPage_->setDefaultTopK(settings.defaultTopK);
    healthSnapshot_ = {};
    updateRuntimeStatus();
    const bool restarted = processBridge_->restartWorker(config);
    appendLog(QStringLiteral("运行设置已保存；任务超时 %1 秒当前仅作为后续恢复功能的配置。")
                  .arg(settings.taskTimeoutSeconds));
    return restarted;
}

void DiagnosticWorkbench::updateHealthSnapshot(const QJsonObject &snapshot)
{
    healthSnapshot_ = snapshot;
    const QJsonValue sources = snapshot.value(QStringLiteral("sources"));
    if (sources.isArray()) {
        knowledgeCenterPage_->setRecords(knowledgeSourceRecordsFromJson(sources.toArray()));
    } else {
        knowledgeCenterPage_->showSourcesUnavailable(QStringLiteral("当前 Worker 未提供来源明细，请更新本地引擎。"));
    }
    healthIssues_ = runRuntimePreflight(runtimeSettings_);
    const QJsonObject index = snapshot.value(QStringLiteral("index")).toObject();
    if (!index.value(QStringLiteral("ready")).toBool()) {
        healthIssues_.append({
            HealthSeverity::Warning,
            QStringLiteral("index"),
            QStringLiteral("STM32F4 索引尚未建立。"),
            QStringLiteral("关闭设置后点击“重建 STM32F4 索引”。"),
        });
    }
    updateRuntimeStatus();
    if (settingsDialog_) {
        settingsDialog_->setHealthSnapshot(healthSnapshot_);
        settingsDialog_->setIssues(healthIssues_);
    }
}

void DiagnosticWorkbench::updateRuntimeStatus()
{
    const bool projectReady = QFileInfo(QDir(runtimeSettings_.projectRoot)
                                            .filePath(QStringLiteral("pyproject.toml"))).isFile();
    const bool pythonReady = QFileInfo(runtimeSettings_.pythonExecutable).isFile();
    const QJsonObject index = healthSnapshot_.value(QStringLiteral("index")).toObject();
    const bool indexKnown = !index.isEmpty();
    const bool indexReady = indexKnown && index.value(QStringLiteral("ready")).toBool();

    ragui::setTag(projectStatusLabel_,
                  projectReady ? QStringLiteral("项目 · 已定位") : QStringLiteral("项目 · 未定位"),
                  projectReady ? ragui::Tone::Neutral : ragui::Tone::Error);
    projectStatusLabel_->setToolTip(runtimeSettings_.projectRoot);

    ragui::setTag(pythonStatusLabel_,
                  pythonReady ? QStringLiteral("Python · 可用") : QStringLiteral("Python · 缺失"),
                  pythonReady ? ragui::Tone::Neutral : ragui::Tone::Error);
    pythonStatusLabel_->setToolTip(runtimeSettings_.pythonExecutable);

    if (!indexKnown) {
        ragui::setTag(indexStatusLabel_, QStringLiteral("索引 · 待连接"), ragui::Tone::Neutral);
        indexStatusLabel_->setToolTip(QStringLiteral("等待 Worker 返回健康快照"));
    } else if (indexReady) {
        ragui::setTag(indexStatusLabel_,
                      QStringLiteral("索引 · %1 chunks").arg(index.value(QStringLiteral("chunks")).toInt()),
                      ragui::Tone::Neutral);
        indexStatusLabel_->setToolTip(QStringLiteral("索引就绪"));
    } else {
        ragui::setTag(indexStatusLabel_, QStringLiteral("索引 · 缺失"), ragui::Tone::Warning);
        // 索引异常时不在这里再放一个「重建」按钮（那会和知识源页的主操作重复），
        // 而是把去处的指引写进 tooltip，动作入口只保留一个。
        indexStatusLabel_->setToolTip(QStringLiteral("索引尚未建立：到「知识源管理」执行重建"));
    }
}

bool DiagnosticWorkbench::startQuery(const QString &question, int topK)
{
    return processBridge_->startQuery(question, topK);
}

bool DiagnosticWorkbench::startHardFaultDiagnosis(const QString &hardFaultLog, int topK)
{
    return processBridge_->startHardFaultDiagnosis(hardFaultLog, topK);
}

bool DiagnosticWorkbench::startProtocolLogDiagnosis(const QJsonObject &payload, int topK)
{
    return processBridge_->startProtocolLogDiagnosis(payload, topK);
}

bool DiagnosticWorkbench::startReindex()
{
    if (processBridge_->startReindex()) {
        reindexRunning_ = true;
        indexSummaryLabel_->setText(QStringLiteral("索引摘要：正在重建，请等待完成"));
        ragui::setTag(indexStatusLabel_, QStringLiteral("索引 · 重建中"), ragui::Tone::Warning);
        knowledgeCenterPage_->setReindexPhase(ReindexPhase::Running);
        return true;
    }
    return false;
}

void DiagnosticWorkbench::renderDiagnosis(const QJsonObject &result)
{
    QString errorMessage;
    const DiagnosisViewModel model = DiagnosisViewModel::fromJson(result, &errorMessage);
    if (!errorMessage.isEmpty()) {
        handleProcessFailure(errorMessage);
        return;
    }
    diagnosisPage_->setDiagnosisResult(model);
    if (model.protocolLog.available) {
        appendLog(QStringLiteral(
            "协议日志完成：profile=%1 · lines=%2 · events=%3 · anomalies=%4 · grounded=%5")
                      .arg(model.protocolLog.profile)
                      .arg(model.protocolLog.lineCount)
                      .arg(model.protocolLog.eventCount)
                      .arg(model.protocolLog.anomalies.size())
                      .arg(model.grounded ? QStringLiteral("true") : QStringLiteral("false")));
    }
    saveDiagnosisCase(result, model);
}

void DiagnosticWorkbench::configureCaseStore(const QString &projectRoot)
{
    const QString databasePath = QDir(projectRoot).filePath(QStringLiteral("data/runtime/diagnosis_cases.sqlite3"));
    evaluationStore_.reset();
    caseStore_ = std::make_unique<CaseStore>(databasePath);
    QString errorMessage;
    if (!caseStore_->initialize(&errorMessage)) {
        appendLog(QStringLiteral("案例库不可用，诊断结果仍会显示：%1").arg(errorMessage));
        caseLibraryPage_->showStoreError(errorMessage);
        return;
    }
    appendLog(QStringLiteral("案例库已连接：%1").arg(databasePath));
    refreshCaseLibrary(caseLibraryPage_->currentFilter());
    evaluationStore_ = std::make_unique<EvaluationStore>(databasePath);
    if (!evaluationStore_->initialize(&errorMessage)) {
        appendLog(QStringLiteral("评估库不可用：%1").arg(errorMessage));
        evaluationStore_.reset();
    } else {
        refreshEvaluationCenter();
    }
}

void DiagnosticWorkbench::saveDiagnosisCase(const QJsonObject &result, const DiagnosisViewModel &model)
{
    if (!caseStore_ || !caseStore_->isReady()) {
        return;
    }
    CaseRecord record;
    if (model.protocolLog.available) {
        record.mode = QStringLiteral("protocol_log");
        QStringList rawLines;
        rawLines.reserve(model.protocolLog.events.size());
        for (const ProtocolLogEventViewModel &event : model.protocolLog.events) {
            rawLines.push_back(event.rawMessage);
        }
        record.inputText = rawLines.join('\n');
    } else if (model.hardFault.available) {
        record.mode = QStringLiteral("hardfault");
        record.inputText = model.hardFault.rawLog;
    } else {
        record.mode = QStringLiteral("expert");
        record.inputText = model.query;
    }
    record.query = model.query;
    record.answer = model.answer;
    record.grounded = model.grounded;
    record.refusalReason = model.refusalReason;
    record.resultJson = result;
    record.reportPath = model.reportPath;
    record.embedding = model.embedding;
    record.reranker = model.reranker;
    record.retrievalMs = model.retrievalMs;
    QString savedId;
    QString errorMessage;
    if (!caseStore_->save(record, &savedId, &errorMessage)) {
        appendLog(QStringLiteral("案例保存失败，当前诊断不受影响：%1").arg(errorMessage));
        return;
    }
    appendLog(QStringLiteral("案例已保存：%1").arg(savedId));
    refreshCaseLibrary(caseLibraryPage_->currentFilter());
}

void DiagnosticWorkbench::showDiagnosisWorkspace()
{
    workspaceStack_->setCurrentWidget(diagnosisPage_);
    setActiveNavigation(diagnosisNavigationButton_);
}

void DiagnosticWorkbench::showCaseLibrary()
{
    workspaceStack_->setCurrentWidget(caseLibraryPage_);
    setActiveNavigation(caseLibraryNavigationButton_);
    refreshCaseLibrary(caseLibraryPage_->currentFilter());
}

void DiagnosticWorkbench::showKnowledgeCenter()
{
    workspaceStack_->setCurrentWidget(knowledgeCenterPage_);
    setActiveNavigation(knowledgeNavigationButton_);
    if (processBridge_ && processBridge_->isWorkerReady()) {
        processBridge_->requestHealth();
    }
}

void DiagnosticWorkbench::showEvaluationCenter()
{
    workspaceStack_->setCurrentWidget(evaluationCenterPage_);
    setActiveNavigation(evaluationNavigationButton_);
    refreshEvaluationCenter();
}

void DiagnosticWorkbench::refreshCaseLibrary(const CaseFilter &filter)
{
    if (!caseStore_ || !caseStore_->isReady()) {
        return;
    }
    QString errorMessage;
    const QVector<CaseRecord> records = caseStore_->list(filter, &errorMessage);
    if (!errorMessage.isEmpty()) {
        appendLog(QStringLiteral("案例库读取失败：%1").arg(errorMessage));
        caseLibraryPage_->showStoreError(errorMessage);
        return;
    }
    caseLibraryPage_->setRecords(records);
}

void DiagnosticWorkbench::refreshEvaluationCenter()
{
    if (!evaluationStore_ || !evaluationStore_->isReady()) {
        return;
    }
    QString errorMessage;
    const QVector<EvaluationCandidate> candidates = evaluationStore_->listCandidates({}, &errorMessage);
    if (!errorMessage.isEmpty()) {
        evaluationCenterPage_->showFailure(errorMessage);
        return;
    }
    evaluationCenterPage_->setCandidates(candidates);
    const QVector<ApprovedEvaluationSample> samples = evaluationStore_->listApprovedSamples(&errorMessage);
    if (!errorMessage.isEmpty()) {
        evaluationCenterPage_->showFailure(errorMessage);
        return;
    }
    evaluationCenterPage_->setApprovedSampleCount(samples.size());
    const QVector<EvaluationRun> runs = evaluationStore_->listRuns(1, &errorMessage);
    if (!errorMessage.isEmpty()) {
        evaluationCenterPage_->showFailure(errorMessage);
        return;
    }
    if (runs.isEmpty()) evaluationCenterPage_->clearLatestRun();
    else evaluationCenterPage_->setLatestRun(runs.front());
}

bool DiagnosticWorkbench::recordCaseFeedback(const CaseRecord &record, const QString &feedback)
{
    if (!evaluationStore_ || !evaluationStore_->isReady()) {
        return false;
    }
    EvaluationCandidate candidate;
    QString errorMessage;
    if (!evaluationStore_->createOrGetFromCase(record, feedback, &candidate, &errorMessage)) {
        appendLog(QStringLiteral("评估反馈保存失败：%1").arg(errorMessage));
        evaluationCenterPage_->showFailure(errorMessage);
        return false;
    }
    appendLog(QStringLiteral("评估候选已保存：%1").arg(candidate.id));
    refreshEvaluationCenter();
    return true;
}

bool DiagnosticWorkbench::createEvaluationCandidate(const CaseRecord &record)
{
    if (!recordCaseFeedback(record, QStringLiteral("unreviewed"))) {
        return false;
    }
    showEvaluationCenter();
    return true;
}

bool DiagnosticWorkbench::approveEvaluationCandidate(const EvaluationCandidate &candidate)
{
    if (!evaluationStore_ || !evaluationStore_->isReady()) {
        return false;
    }
    ApprovedEvaluationSample sample;
    QString errorMessage;
    if (!evaluationStore_->approveCandidate(candidate, &sample, &errorMessage)) {
        appendLog(QStringLiteral("批准评估候选失败：%1").arg(errorMessage));
        evaluationCenterPage_->showFailure(errorMessage);
        return false;
    }
    appendLog(QStringLiteral("正式评估样本已生成：%1").arg(sample.id));
    refreshEvaluationCenter();
    return true;
}

bool DiagnosticWorkbench::rejectEvaluationCandidate(const QString &id)
{
    if (!evaluationStore_ || !evaluationStore_->isReady()) {
        return false;
    }
    QString errorMessage;
    if (!evaluationStore_->rejectCandidate(id, &errorMessage)) {
        appendLog(QStringLiteral("拒绝评估候选失败：%1").arg(errorMessage));
        evaluationCenterPage_->showFailure(errorMessage);
        return false;
    }
    refreshEvaluationCenter();
    return true;
}

bool DiagnosticWorkbench::importEvaluationCandidates()
{
    if (!evaluationStore_ || !evaluationStore_->isReady() || workspaceTaskState_.taskRunning) return false;
    const QString initialDirectory = QDir(runtimeSettings_.projectRoot).filePath(QStringLiteral("data/eval"));
    const QString path = QFileDialog::getOpenFileName(this, QStringLiteral("导入评估候选"), initialDirectory,
                                                      QStringLiteral("JSON 文件 (*.json)"));
    if (path.isEmpty()) return false;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        evaluationCenterPage_->showFailure(QStringLiteral("无法读取评估 JSON：%1").arg(file.errorString()));
        return false;
    }
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isArray()) {
        evaluationCenterPage_->showFailure(QStringLiteral("评估 JSON 必须是数组：%1").arg(parseError.errorString()));
        return false;
    }
    const QJsonArray rows = document.array();
    QVector<EvaluationCandidate> candidates;
    candidates.reserve(rows.size());
    for (int index = 0; index < rows.size(); ++index) {
        if (!rows.at(index).isObject()) {
            evaluationCenterPage_->showFailure(QStringLiteral("第 %1 条评估记录不是 JSON object。").arg(index + 1));
            return false;
        }
        const QJsonObject row = rows.at(index).toObject();
        EvaluationCandidate candidate;
        candidate.query = row.value(QStringLiteral("query")).toString().trimmed();
        candidate.userFeedback = QStringLiteral("unreviewed");
        const QJsonArray expected = row.value(QStringLiteral("expected_evidence")).toArray();
        for (const QJsonValue &value : expected) candidate.expectedEvidence.push_back(ExpectedEvidenceRef::fromJson(value.toObject()));
        for (const QJsonValue &value : row.value(QStringLiteral("relevant_chunk_ids")).toArray()) {
            ExpectedEvidenceRef ref;
            ref.chunkId = value.toString();
            candidate.expectedEvidence.push_back(ref);
        }
        for (const QJsonValue &value : row.value(QStringLiteral("relevant_sources")).toArray()) {
            ExpectedEvidenceRef ref;
            ref.source = value.toString();
            candidate.expectedEvidence.push_back(ref);
        }
        if (candidate.query.isEmpty() || candidate.query.size() > 2000
            || candidate.expectedEvidence.isEmpty() || candidate.expectedEvidence.size() > 20) {
            evaluationCenterPage_->showFailure(QStringLiteral("第 %1 条记录的 query 或 Evidence 数量无效。").arg(index + 1));
            return false;
        }
        for (const ExpectedEvidenceRef &ref : candidate.expectedEvidence) {
            if (!ref.isUsable()) {
                evaluationCenterPage_->showFailure(QStringLiteral("第 %1 条记录包含无法追溯的 Evidence。").arg(index + 1));
                return false;
            }
        }
        candidates.push_back(candidate);
    }
    int importedCount = 0;
    QString errorMessage;
    if (!evaluationStore_->importCandidates(candidates, &importedCount, &errorMessage)) {
        evaluationCenterPage_->showFailure(errorMessage);
        return false;
    }
    appendLog(QStringLiteral("已导入 %1 条评估候选，尚未批准为正式样本。").arg(importedCount));
    statusBar()->showMessage(QStringLiteral("评估候选导入完成：%1 条").arg(importedCount), 5000);
    refreshEvaluationCenter();
    return true;
}

bool DiagnosticWorkbench::startEvaluation(int topK)
{
    if (!evaluationStore_ || !evaluationStore_->isReady() || !processBridge_) return false;
    QString errorMessage;
    const QVector<ApprovedEvaluationSample> samples = evaluationStore_->listApprovedSamples(&errorMessage);
    if (!errorMessage.isEmpty()) {
        evaluationCenterPage_->showFailure(errorMessage);
        return false;
    }
    if (samples.isEmpty()) {
        evaluationCenterPage_->showFailure(QStringLiteral("尚无已批准样本。"));
        return false;
    }
    QJsonArray snapshot;
    for (const ApprovedEvaluationSample &sample : samples) snapshot.append(sample.toJson());
    if (!processBridge_->startEvaluation(snapshot, topK)) return false;
    activeEvaluationSamples_ = snapshot;
    activeEvaluationTopK_ = topK;
    activeEvaluationStartedAt_ = QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
    appendLog(QStringLiteral("开始评估：%1 个 Approved Sample，top-k=%2").arg(snapshot.size()).arg(topK));
    return true;
}

void DiagnosticWorkbench::finishEvaluationSuccess(const QJsonObject &result)
{
    if (evaluationStore_ && !activeEvaluationSamples_.isEmpty()) {
        EvaluationRun run;
        run.createdAt = activeEvaluationStartedAt_;
        run.status = QStringLiteral("succeeded");
        run.topK = activeEvaluationTopK_;
        run.sampleCount = activeEvaluationSamples_.size();
        run.samples = activeEvaluationSamples_;
        run.result = result;
        QString errorMessage;
        if (!evaluationStore_->saveRun(run, nullptr, &errorMessage)) {
            appendLog(QStringLiteral("评估结果保存失败：%1").arg(errorMessage));
        }
    }
    activeEvaluationSamples_ = {};
    activeEvaluationStartedAt_.clear();
    activeEvaluationTopK_ = 0;
    refreshEvaluationCenter();
    evaluationCenterPage_->showEvaluationResult(result);
}

void DiagnosticWorkbench::finishEvaluationFailure(const QString &message)
{
    if (evaluationStore_ && !activeEvaluationSamples_.isEmpty()) {
        EvaluationRun run;
        run.createdAt = activeEvaluationStartedAt_;
        run.status = QStringLiteral("failed");
        run.topK = activeEvaluationTopK_;
        run.sampleCount = activeEvaluationSamples_.size();
        run.samples = activeEvaluationSamples_;
        run.errorMessage = message;
        QString errorMessage;
        if (!evaluationStore_->saveRun(run, nullptr, &errorMessage)) {
            appendLog(QStringLiteral("评估失败记录保存失败：%1").arg(errorMessage));
        }
    }
    activeEvaluationSamples_ = {};
    activeEvaluationStartedAt_.clear();
    activeEvaluationTopK_ = 0;
    refreshEvaluationCenter();
    evaluationCenterPage_->showFailure(message);
}

bool DiagnosticWorkbench::reopenCase(const CaseRecord &record)
{
    if (workspaceTaskState_.taskRunning) {
        return false;
    }
    QString errorMessage;
    const DiagnosisViewModel model = DiagnosisViewModel::fromJson(record.resultJson, &errorMessage);
    if (!errorMessage.isEmpty()) {
        appendLog(QStringLiteral("案例无法重开：%1").arg(errorMessage));
        return false;
    }
    diagnosisPage_->restoreCaseInput(record.mode, record.inputText);
    diagnosisPage_->setDiagnosisResult(model);
    showDiagnosisWorkspace();
    statusBar()->showMessage(QStringLiteral("已重开案例：%1").arg(record.id), 6000);
    return true;
}

bool DiagnosticWorkbench::updateCaseNote(const QString &id, const QString &note)
{
    if (!caseStore_ || !caseStore_->isReady()) {
        return false;
    }
    QString errorMessage;
    if (!caseStore_->updateNote(id, note, &errorMessage)) {
        appendLog(QStringLiteral("案例备注保存失败：%1").arg(errorMessage));
        return false;
    }
    refreshCaseLibrary(caseLibraryPage_->currentFilter());
    return true;
}

bool DiagnosticWorkbench::updateCaseFavorite(const QString &id, bool favorite)
{
    if (!caseStore_ || !caseStore_->isReady()) {
        return false;
    }
    QString errorMessage;
    if (!caseStore_->updateFavorite(id, favorite, &errorMessage)) {
        appendLog(QStringLiteral("案例收藏更新失败：%1").arg(errorMessage));
        return false;
    }
    refreshCaseLibrary(caseLibraryPage_->currentFilter());
    return true;
}

void DiagnosticWorkbench::copyCaseText(const QString &text)
{
    QApplication::clipboard()->setText(text);
}

bool DiagnosticWorkbench::exportCase(const CaseRecord &record)
{
    const QString defaultName = QStringLiteral("diagnosis_case_%1.json").arg(record.id.left(8));
    const QString path = QFileDialog::getSaveFileName(this, QStringLiteral("导出诊断案例"), defaultName,
                                                      QStringLiteral("JSON 文件 (*.json)"));
    if (path.isEmpty() || !caseStore_) {
        return false;
    }
    QString errorMessage;
    if (!caseStore_->exportRecord(record, path, &errorMessage)) {
        appendLog(QStringLiteral("案例导出失败：%1").arg(errorMessage));
        return false;
    }
    appendLog(QStringLiteral("案例已导出：%1").arg(path));
    return true;
}

void DiagnosticWorkbench::renderIngestSummary(const QJsonObject &result)
{
    const QString status = result.value(QStringLiteral("status")).toString();
    if (status != QStringLiteral("ok")) {
        handleProcessFailure(QStringLiteral("索引重建返回异常状态：%1").arg(status));
        return;
    }
    const int documents = result.value(QStringLiteral("documents")).toInt();
    const int chunks = result.value(QStringLiteral("chunks")).toInt();
    const int indexCount = result.value(QStringLiteral("index_count")).toInt();
    const int errors = result.value(QStringLiteral("errors")).toArray().size();
    const int ignored = result.value(QStringLiteral("ignored")).toArray().size();
    const QJsonValue sources = result.value(QStringLiteral("sources"));
    if (sources.isArray()) {
        knowledgeCenterPage_->setRecords(knowledgeSourceRecordsFromJson(sources.toArray()));
    }
    indexSummaryLabel_->setText(QStringLiteral("索引摘要：%1 documents · %2 chunks · %3 vectors · %4 errors · %5 ignored")
                                    .arg(documents)
                                    .arg(chunks)
                                    .arg(indexCount)
                                    .arg(errors)
                                    .arg(ignored));
    ragui::setTag(indexStatusLabel_, QStringLiteral("索引 · 已就绪"), ragui::Tone::Neutral);
    reindexRunning_ = false;
    knowledgeCenterPage_->setReindexPhase(
        ReindexPhase::Succeeded,
        QStringLiteral("重建完成：%1 chunks · %2 errors · %3 ignored").arg(chunks).arg(errors).arg(ignored));
    statusBar()->showMessage(QStringLiteral("索引重建完成：%1 个 chunks 已写入 Chroma").arg(chunks), 8000);
    processBridge_->requestHealth();
}

void DiagnosticWorkbench::handleEngineState(EngineState state)
{
    workspaceTaskState_.engineState = state;
    workspaceTaskState_.taskRunning = state == EngineState::Running || processBridge_->isRunning();
    updateWorkspaceTaskState();
    // Engine 是「当前任务」的状态，只写 engineStatusLabel_；
    // Worker 生命周期由 handleWorkerState 写 workerStatusLabel_。两者互不覆盖。
    //
    // 同时把机器可读的状态挂到属性上：截图自动化（main.cpp --auto-query 等）读这个属性判断
    // 任务是否完成，不再去匹配界面文案，改文案不会再连带弄坏自动化。
    engineStatusLabel_->setProperty("engineState", RagProcessBridge::stateName(state));
    switch (state) {
    case EngineState::Running:
        ragui::setTag(engineStatusLabel_, QStringLiteral("引擎 · 运行中"), ragui::Tone::Info);
        statusBar()->showMessage(QStringLiteral("RAG 引擎运行中，界面仍可滚动和调整"));
        break;
    case EngineState::Succeeded:
        ragui::setTag(engineStatusLabel_, QStringLiteral("引擎 · 完成"), ragui::Tone::Success);
        statusBar()->showMessage(QStringLiteral("RAG 引擎已结束"));
        break;
    case EngineState::Failed:
        ragui::setTag(engineStatusLabel_, QStringLiteral("引擎 · 失败"), ragui::Tone::Error);
        statusBar()->showMessage(QStringLiteral("RAG 引擎已结束"));
        break;
    case EngineState::Idle:
        ragui::setTag(engineStatusLabel_, QStringLiteral("引擎 · 空闲"), ragui::Tone::Neutral);
        statusBar()->showMessage(QStringLiteral("RAG 引擎已结束"));
        break;
    }
}

void DiagnosticWorkbench::handleWorkerState(WorkerState state)
{
    workspaceTaskState_.workerState = state;
    workspaceTaskState_.taskRunning = processBridge_ && processBridge_->isRunning();
    updateWorkspaceTaskState();
    if (state == WorkerState::Starting) {
        // 同一行状态里其余四项都是中文，Worker 这项也跟着中文——
        // 运行设置对话框里的 displayWorkerState() 早就这么做了，这里只是补齐。
        ragui::setTag(workerStatusLabel_, QStringLiteral("Worker · 启动中"), ragui::Tone::Warning);
        statusBar()->showMessage(QStringLiteral("正在启动长驻 Python RAG Worker"));
    } else if (state == WorkerState::Ready) {
        ragui::setTag(workerStatusLabel_, QStringLiteral("Worker · 已就绪"), ragui::Tone::Success);
        statusBar()->showMessage(QStringLiteral("RAG Worker 已就绪，后续任务将复用已加载模型"), 6000);
    } else if (state == WorkerState::Failed) {
        ragui::setTag(workerStatusLabel_, QStringLiteral("Worker · 失败"), ragui::Tone::Error);
        statusBar()->showMessage(QStringLiteral("RAG Worker 不可用，请展开下方运行日志查看原因"), 8000);
    }
}

void DiagnosticWorkbench::updateWorkspaceTaskState()
{
    diagnosisPage_->setTaskState(workspaceTaskState_);
    caseLibraryPage_->setTaskState(workspaceTaskState_);
    knowledgeCenterPage_->setTaskState(workspaceTaskState_);
    evaluationCenterPage_->setTaskState(workspaceTaskState_);
    if (settingsDialog_) {
        settingsDialog_->setTaskRunning(workspaceTaskState_.taskRunning);
    }
}

void DiagnosticWorkbench::handleProcessFailure(const QString &message)
{
    appendLog(QStringLiteral("错误：%1").arg(message));
    diagnosisPage_->showQueryFailure(message);
}

void DiagnosticWorkbench::handleHardFaultFailure(const QString &message)
{
    appendLog(QStringLiteral("错误：%1").arg(message));
    diagnosisPage_->showHardFaultFailure(message);
}

void DiagnosticWorkbench::handleProtocolLogFailure(const QString &message)
{
    appendLog(QStringLiteral("错误：%1").arg(message));
    diagnosisPage_->showProtocolLogFailure(message);
}

void DiagnosticWorkbench::appendLog(const QString &message)
{
    const QString trimmed = message.trimmed();
    if (trimmed.isEmpty()) {
        return;
    }
    logEdit_->appendPlainText(trimmed);
    // 折叠状态下把最后一条摘出来，平时不占空间也能看到 Worker 在干什么。
    if (activityPreviewLabel_) {
        activityPreviewLabel_->setFullText(trimmed);
    }
}

void DiagnosticWorkbench::openReport(const QString &reportPath)
{
    const QFileInfo report(reportPath);
    if (!report.isFile()) {
        handleProcessFailure(QStringLiteral("报告文件不存在：%1").arg(reportPath));
        return;
    }
    if (!QDesktopServices::openUrl(QUrl::fromLocalFile(report.absoluteFilePath()))) {
        handleProcessFailure(QStringLiteral("系统默认程序无法打开报告：%1").arg(report.absoluteFilePath()));
    }
}
