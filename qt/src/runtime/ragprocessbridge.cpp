#include "ragprocessbridge.h"

#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QProcessEnvironment>
#include <QUuid>

namespace {
QString operationDisplayName(RagOperation operation)
{
    switch (operation) {
    case RagOperation::Query:
        return QStringLiteral("查询");
    case RagOperation::HardFaultDiagnosis:
        return QStringLiteral("HardFault 诊断");
    case RagOperation::ProtocolLogDiagnosis:
        return QStringLiteral("协议日志诊断");
    case RagOperation::Reindex:
        return QStringLiteral("索引重建");
    case RagOperation::Evaluation:
        return QStringLiteral("检索评估");
    }
    return QStringLiteral("RAG 任务");
}

QString summarizeBytes(const QByteArray &bytes)
{
    constexpr int maximumCharacters = 1200;
    QString text = QString::fromUtf8(bytes).trimmed();
    if (text.size() > maximumCharacters) {
        text = text.left(maximumCharacters) + QStringLiteral("\n……（输出已截断）");
    }
    return text;
}
} // namespace

RagProcessBridge::RagProcessBridge(RagRuntimeConfig config, QObject *parent)
    : QObject(parent)
    , config_(std::move(config))
{
    process_.setProcessChannelMode(QProcess::SeparateChannels);
    connect(&process_, &QProcess::readyReadStandardOutput, this, [this] { readStandardOutput(); });
    connect(&process_, &QProcess::readyReadStandardError, this, [this] { readStandardError(); });
    connect(&process_, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error != QProcess::FailedToStart) {
            return;
        }
        const QString message = QStringLiteral("RAG Worker 无法启动：%1").arg(process_.errorString());
        setWorkerState(WorkerState::Failed);
        setState(EngineState::Failed);
        if (failureHandler_) {
            failureHandler_(operation_, message, {});
        }
    });
    connect(&process_, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
            [this](int exitCode, QProcess::ExitStatus exitStatus) { finishWorker(exitCode, exitStatus); });
}

RagProcessBridge::~RagProcessBridge()
{
    stopWorker();
}

RagRuntimeConfig RagProcessBridge::discoverRuntime(const QString &applicationDirectory)
{
    const QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    const QString configuredRoot =
        environment.value(QStringLiteral("RAG_DIAGNOSTIC_PROJECT_ROOT")).trimmed();
    if (!configuredRoot.isEmpty()) {
        const QString projectRoot = QDir::cleanPath(QFileInfo(configuredRoot).absoluteFilePath());
        const QString configuredPython =
            environment.value(QStringLiteral("RAG_DIAGNOSTIC_PYTHON_EXECUTABLE")).trimmed();
        const QString python = configuredPython.isEmpty()
                                   ? QDir(projectRoot).filePath(
                                         QStringLiteral(".venv312/Scripts/python.exe"))
                                   : QFileInfo(configuredPython).absoluteFilePath();
        return {projectRoot, QDir::cleanPath(python)};
    }

    QDir candidate(applicationDirectory);
    QString projectRoot;
    for (int level = 0; level < 6; ++level) {
        const QFileInfo marker(candidate.filePath(QStringLiteral("pyproject.toml")));
        if (marker.isFile()) {
            projectRoot = candidate.absolutePath();
            break;
        }
        if (!candidate.cdUp()) {
            break;
        }
    }

    if (projectRoot.isEmpty()) {
        projectRoot = QDir(applicationDirectory).absolutePath();
    }
    const QString python = QDir(projectRoot).filePath(QStringLiteral(".venv312/Scripts/python.exe"));
    return {QDir::cleanPath(projectRoot), QDir::cleanPath(python)};
}

QString RagProcessBridge::stateName(EngineState state)
{
    switch (state) {
    case EngineState::Idle:
        return QStringLiteral("Idle");
    case EngineState::Running:
        return QStringLiteral("Running");
    case EngineState::Succeeded:
        return QStringLiteral("Succeeded");
    case EngineState::Failed:
        return QStringLiteral("Failed");
    }
    return QStringLiteral("Unknown");
}

QString RagProcessBridge::workerStateName(WorkerState state)
{
    switch (state) {
    case WorkerState::Stopped:
        return QStringLiteral("Stopped");
    case WorkerState::Starting:
        return QStringLiteral("Starting");
    case WorkerState::Ready:
        return QStringLiteral("Ready");
    case WorkerState::Failed:
        return QStringLiteral("Failed");
    }
    return QStringLiteral("Unknown");
}

QJsonObject RagProcessBridge::parseJsonDocument(const QByteArray &rawOutput, QString *errorMessage)
{
    const QByteArray output = rawOutput.trimmed();
    QJsonParseError parseError;
    QJsonDocument document = QJsonDocument::fromJson(output, &parseError);
    if (parseError.error == QJsonParseError::NoError && document.isObject()) {
        return document.object();
    }

    for (qsizetype offset = output.indexOf('{'); offset >= 0; offset = output.indexOf('{', offset + 1)) {
        document = QJsonDocument::fromJson(output.mid(offset), &parseError);
        if (parseError.error == QJsonParseError::NoError && document.isObject()) {
            return document.object();
        }
    }

    if (errorMessage) {
        *errorMessage = QStringLiteral("解析错误：%1；输出摘要：\n%2")
                            .arg(parseError.errorString(), summarizeBytes(output));
    }
    return {};
}

bool RagProcessBridge::startWorker()
{
    if (process_.state() != QProcess::NotRunning) {
        return true;
    }
    if (!QFileInfo::exists(config_.pythonExecutable)) {
        failBeforeStart(operation_, QStringLiteral("找不到 Python 解释器：%1").arg(config_.pythonExecutable));
        return false;
    }
    if (!QFileInfo(config_.projectRoot).isDir()) {
        failBeforeStart(operation_, QStringLiteral("找不到 RAG 项目目录：%1").arg(config_.projectRoot));
        return false;
    }

    expectedShutdown_ = false;
    standardOutputBuffer_.clear();
    process_.setWorkingDirectory(config_.projectRoot);
    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    environment.insert(QStringLiteral("PYTHONUTF8"), QStringLiteral("1"));
    environment.insert(QStringLiteral("PYTHONIOENCODING"), QStringLiteral("utf-8"));
    process_.setProcessEnvironment(environment);
    setWorkerState(WorkerState::Starting);
    if (logHandler_) {
        logHandler_(QStringLiteral("启动长驻 RAG Worker：%1").arg(config_.pythonExecutable));
    }
    process_.start(config_.pythonExecutable,
                   {QStringLiteral("-m"), QStringLiteral("rag_diagnostic"),
                    QStringLiteral("--root"), config_.projectRoot,
                    QStringLiteral("--embedding"), QStringLiteral("bge"),
                    QStringLiteral("--embedding-model"), config_.embeddingModel,
                    QStringLiteral("--reranker-model"), config_.rerankerModel,
                    QStringLiteral("worker")});
    return true;
}

bool RagProcessBridge::restartWorker(const RagRuntimeConfig &config)
{
    if (isRunning()) {
        if (logHandler_) {
            logHandler_(QStringLiteral("RAG 任务正在运行，不能重启 Worker。"));
        }
        return false;
    }
    stopWorker();
    config_ = config;
    setState(EngineState::Idle);
    setWorkerState(WorkerState::Stopped);
    return startWorker();
}

bool RagProcessBridge::requestHealth()
{
    if (!isWorkerReady() || !healthRequestId_.isEmpty()) {
        return false;
    }
    healthRequestId_ = QUuid::createUuid().toString(QUuid::WithoutBraces);
    if (!writeControlRequest(healthRequestId_, QStringLiteral("health"))) {
        healthRequestId_.clear();
        return false;
    }
    return true;
}

bool RagProcessBridge::startQuery(const QString &question, int topK)
{
    return start(RagOperation::Query, QStringLiteral("query"),
                 {{QStringLiteral("query"), question}},
                 {{QStringLiteral("top_k"), topK}});
}

bool RagProcessBridge::startHardFaultDiagnosis(const QString &hardFaultLog, int topK)
{
    return start(RagOperation::HardFaultDiagnosis, QStringLiteral("hardfault"),
                 {{QStringLiteral("log"), hardFaultLog}},
                 {{QStringLiteral("top_k"), topK}});
}

bool RagProcessBridge::startProtocolLogDiagnosis(const QJsonObject &payload, int topK)
{
    return start(RagOperation::ProtocolLogDiagnosis, QStringLiteral("protocol_log"), payload,
                 {{QStringLiteral("top_k"), topK}});
}

bool RagProcessBridge::startReindex()
{
    return start(RagOperation::Reindex, QStringLiteral("reindex"), {}, {});
}

bool RagProcessBridge::startEvaluation(const QJsonArray &samples, int topK)
{
    return start(RagOperation::Evaluation,
                 QStringLiteral("evaluate"),
                 {{QStringLiteral("samples"), samples}},
                 {{QStringLiteral("top_k"), topK}});
}

bool RagProcessBridge::isRunning() const
{
    return !activeRequestId_.isEmpty();
}

bool RagProcessBridge::isWorkerReady() const
{
    return workerState_ == WorkerState::Ready;
}

qint64 RagProcessBridge::workerProcessId() const
{
    return process_.processId();
}

EngineState RagProcessBridge::state() const
{
    return state_;
}

WorkerState RagProcessBridge::workerState() const
{
    return workerState_;
}

const RagRuntimeConfig &RagProcessBridge::config() const
{
    return config_;
}

void RagProcessBridge::setStateHandler(StateHandler handler)
{
    stateHandler_ = std::move(handler);
}

void RagProcessBridge::setWorkerStateHandler(WorkerStateHandler handler)
{
    workerStateHandler_ = std::move(handler);
}

void RagProcessBridge::setHealthHandler(HealthHandler handler)
{
    healthHandler_ = std::move(handler);
}

void RagProcessBridge::setLogHandler(LogHandler handler)
{
    logHandler_ = std::move(handler);
}

void RagProcessBridge::setProgressHandler(ProgressHandler handler)
{
    progressHandler_ = std::move(handler);
}

void RagProcessBridge::setSuccessHandler(SuccessHandler handler)
{
    successHandler_ = std::move(handler);
}

void RagProcessBridge::setFailureHandler(FailureHandler handler)
{
    failureHandler_ = std::move(handler);
}

bool RagProcessBridge::start(RagOperation operation, const QString &operationName,
                             const QJsonObject &payload, const QJsonObject &options)
{
    if (!isWorkerReady()) {
        if (process_.state() == QProcess::NotRunning) {
            startWorker();
        }
        if (logHandler_) {
            logHandler_(QStringLiteral("RAG Worker 尚未 Ready，本次操作未启动。"));
        }
        return false;
    }
    if (isRunning()) {
        if (logHandler_) {
            logHandler_(QStringLiteral("已有 RAG 任务正在运行，本次操作未启动。"));
        }
        return false;
    }

    operation_ = operation;
    activeRequestId_ = QUuid::createUuid().toString(QUuid::WithoutBraces);
    const QJsonObject request{
        {QStringLiteral("protocol_version"), 1},
        {QStringLiteral("request_id"), activeRequestId_},
        {QStringLiteral("operation"), operationName},
        {QStringLiteral("payload"), payload},
        {QStringLiteral("options"), options},
    };
    const QByteArray line = QJsonDocument(request).toJson(QJsonDocument::Compact) + '\n';
    if (process_.write(line) < 0) {
        const QString message = QStringLiteral("无法向 RAG Worker 写入请求：%1").arg(process_.errorString());
        activeRequestId_.clear();
        setState(EngineState::Failed);
        if (failureHandler_) {
            failureHandler_(operation_, message, {});
        }
        return false;
    }
    setState(EngineState::Running);
    if (logHandler_) {
        logHandler_(QStringLiteral("提交%1请求：%2")
                        .arg(operationDisplayName(operation_), activeRequestId_));
    }
    return true;
}

void RagProcessBridge::readStandardOutput()
{
    standardOutputBuffer_.append(process_.readAllStandardOutput());
    for (qsizetype newline = standardOutputBuffer_.indexOf('\n'); newline >= 0;
         newline = standardOutputBuffer_.indexOf('\n')) {
        const QByteArray line = standardOutputBuffer_.left(newline).trimmed();
        standardOutputBuffer_.remove(0, newline + 1);
        if (!line.isEmpty()) {
            processProtocolLine(line);
        }
    }
}

void RagProcessBridge::readStandardError()
{
    const QByteArray incoming = process_.readAllStandardError();
    if (logHandler_ && !incoming.trimmed().isEmpty()) {
        logHandler_(QString::fromUtf8(incoming).trimmed());
    }
}

void RagProcessBridge::processProtocolLine(const QByteArray &line)
{
    QString parseError;
    const QJsonObject message = parseJsonDocument(line, &parseError);
    if (message.isEmpty()) {
        if (logHandler_) {
            logHandler_(QStringLiteral("忽略非法 Worker 输出：%1").arg(parseError));
        }
        return;
    }
    if (message.value(QStringLiteral("protocol_version")).toInt() != 1) {
        if (logHandler_) {
            logHandler_(QStringLiteral("忽略不支持版本的 Worker 消息。"));
        }
        return;
    }

    const QString requestId = message.value(QStringLiteral("request_id")).toString();
    const QString event = message.value(QStringLiteral("event")).toString();
    const QJsonObject data = message.value(QStringLiteral("data")).toObject();
    if (requestId == QStringLiteral("worker-startup") && event == QStringLiteral("result")) {
        setState(EngineState::Idle);
        setWorkerState(WorkerState::Ready);
        if (logHandler_) {
            logHandler_(QStringLiteral("RAG Worker Ready，PID=%1，索引 chunks=%2")
                            .arg(process_.processId())
                            .arg(data.value(QStringLiteral("index")).toObject().value(QStringLiteral("chunks")).toInt()));
        }
        if (healthHandler_) {
            healthHandler_(data);
        }
        return;
    }
    if (requestId == healthRequestId_) {
        healthRequestId_.clear();
        if (event == QStringLiteral("result")) {
            if (healthHandler_) {
                healthHandler_(data);
            }
        } else if (event == QStringLiteral("error") && logHandler_) {
            logHandler_(QStringLiteral("刷新 Worker 健康状态失败：%1")
                            .arg(data.value(QStringLiteral("message")).toString()));
        }
        return;
    }
    if (requestId != activeRequestId_) {
        if (logHandler_) {
            logHandler_(QStringLiteral("忽略非当前请求的 Worker 事件：%1").arg(requestId));
        }
        return;
    }
    if (event == QStringLiteral("accepted")) {
        if (logHandler_) {
            logHandler_(QStringLiteral("%1已被 Worker 接受。") .arg(operationDisplayName(operation_)));
        }
        return;
    }
    if (event == QStringLiteral("progress")) {
        if (progressHandler_) {
            progressHandler_(operation_, data);
        }
        return;
    }
    if (event == QStringLiteral("result")) {
        activeRequestId_.clear();
        setState(EngineState::Succeeded);
        if (logHandler_) {
            logHandler_(QStringLiteral("%1完成，Worker 保持 Ready。") .arg(operationDisplayName(operation_)));
        }
        if (successHandler_) {
            successHandler_(operation_, data);
        }
        return;
    }
    if (event == QStringLiteral("error")) {
        activeRequestId_.clear();
        setState(EngineState::Failed);
        QString details = data.value(QStringLiteral("message")).toString(QStringLiteral("Worker 返回未知错误。"));
        const QString field = data.value(QStringLiteral("field")).toString();
        const QString code = data.value(QStringLiteral("code")).toString();
        if (!field.isEmpty()) {
            details += QStringLiteral("\n字段：%1").arg(field);
        }
        if (!code.isEmpty()) {
            details += QStringLiteral("\n错误码：%1").arg(code);
        }
        if (failureHandler_) {
            failureHandler_(operation_, details, data);
        }
    }
}

void RagProcessBridge::finishWorker(int exitCode, QProcess::ExitStatus exitStatus)
{
    readStandardOutput();
    readStandardError();
    const bool expected = expectedShutdown_ && exitStatus == QProcess::NormalExit && exitCode == 0;
    expectedShutdown_ = false;
    healthRequestId_.clear();
    if (expected) {
        setWorkerState(WorkerState::Stopped);
        return;
    }

    const QString message = QStringLiteral("RAG Worker 意外退出（exit code %1）。").arg(exitCode);
    const bool hadActiveRequest = !activeRequestId_.isEmpty();
    activeRequestId_.clear();
    setWorkerState(WorkerState::Failed);
    setState(EngineState::Failed);
    if (logHandler_) {
        logHandler_(message);
    }
    if (hadActiveRequest && failureHandler_) {
        failureHandler_(operation_, message, {});
    }
}

void RagProcessBridge::failBeforeStart(RagOperation operation, const QString &message)
{
    operation_ = operation;
    setWorkerState(WorkerState::Failed);
    setState(EngineState::Failed);
    if (failureHandler_) {
        failureHandler_(operation, message, {});
    }
}

void RagProcessBridge::setState(EngineState state)
{
    state_ = state;
    if (stateHandler_) {
        stateHandler_(state);
    }
}

void RagProcessBridge::setWorkerState(WorkerState state)
{
    workerState_ = state;
    if (workerStateHandler_) {
        workerStateHandler_(state);
    }
}

void RagProcessBridge::stopWorker()
{
    if (process_.state() == QProcess::NotRunning) {
        return;
    }
    if (!isRunning() && isWorkerReady()) {
        expectedShutdown_ = true;
        const QString requestId = QUuid::createUuid().toString(QUuid::WithoutBraces);
        writeControlRequest(requestId, QStringLiteral("shutdown"));
        process_.waitForFinished(2000);
    }
    if (process_.state() != QProcess::NotRunning) {
        process_.terminate();
        if (!process_.waitForFinished(1500)) {
            process_.kill();
            process_.waitForFinished(1000);
        }
    }
}

bool RagProcessBridge::writeControlRequest(const QString &requestId, const QString &operation)
{
    const QJsonObject request{
        {QStringLiteral("protocol_version"), 1},
        {QStringLiteral("request_id"), requestId},
        {QStringLiteral("operation"), operation},
        {QStringLiteral("payload"), QJsonObject{}},
        {QStringLiteral("options"), QJsonObject{}},
    };
    return process_.write(QJsonDocument(request).toJson(QJsonDocument::Compact) + '\n') >= 0;
}
