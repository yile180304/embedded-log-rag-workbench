#pragma once

#include <QJsonObject>
#include <QJsonArray>
#include <QObject>
#include <QProcess>

#include <functional>

enum class EngineState
{
    Idle,
    Running,
    Succeeded,
    Failed
};

enum class WorkerState
{
    Stopped,
    Starting,
    Ready,
    Failed
};

enum class RagOperation
{
    Query,
    HardFaultDiagnosis,
    ProtocolLogDiagnosis,
    Reindex,
    Evaluation
};

struct RagRuntimeConfig
{
    QString projectRoot;
    QString pythonExecutable;
    QString embeddingModel = QStringLiteral("BAAI/bge-small-zh-v1.5");
    QString rerankerModel = QStringLiteral("BAAI/bge-reranker-base");
    int defaultTopK = 5;
    int taskTimeoutSeconds = 180;
};

class RagProcessBridge final : public QObject
{
public:
    using StateHandler = std::function<void(EngineState)>;
    using WorkerStateHandler = std::function<void(WorkerState)>;
    using HealthHandler = std::function<void(const QJsonObject &)>;
    using LogHandler = std::function<void(const QString &)>;
    using ProgressHandler = std::function<void(RagOperation, const QJsonObject &)>;
    using SuccessHandler = std::function<void(RagOperation, const QJsonObject &)>;
    using FailureHandler = std::function<void(RagOperation, const QString &, const QJsonObject &)>;

    explicit RagProcessBridge(RagRuntimeConfig config, QObject *parent = nullptr);
    ~RagProcessBridge() override;

    static RagRuntimeConfig discoverRuntime(const QString &applicationDirectory);
    static QString stateName(EngineState state);
    static QString workerStateName(WorkerState state);
    static QJsonObject parseJsonDocument(const QByteArray &output, QString *errorMessage = nullptr);

    bool startWorker();
    bool restartWorker(const RagRuntimeConfig &config);
    bool requestHealth();
    bool startQuery(const QString &question, int topK);
    bool startHardFaultDiagnosis(const QString &hardFaultLog, int topK);
    bool startProtocolLogDiagnosis(const QJsonObject &payload, int topK);
    bool startReindex();
    bool startEvaluation(const QJsonArray &samples, int topK);
    bool isRunning() const;
    bool isWorkerReady() const;
    qint64 workerProcessId() const;
    EngineState state() const;
    WorkerState workerState() const;
    const RagRuntimeConfig &config() const;

    void setStateHandler(StateHandler handler);
    void setWorkerStateHandler(WorkerStateHandler handler);
    void setHealthHandler(HealthHandler handler);
    void setLogHandler(LogHandler handler);
    void setProgressHandler(ProgressHandler handler);
    void setSuccessHandler(SuccessHandler handler);
    void setFailureHandler(FailureHandler handler);

private:
    bool start(RagOperation operation, const QString &operationName,
               const QJsonObject &payload, const QJsonObject &options = {});
    void readStandardOutput();
    void readStandardError();
    void processProtocolLine(const QByteArray &line);
    void finishWorker(int exitCode, QProcess::ExitStatus exitStatus);
    void failBeforeStart(RagOperation operation, const QString &message);
    void setState(EngineState state);
    void setWorkerState(WorkerState state);
    void stopWorker();
    bool writeControlRequest(const QString &requestId, const QString &operation);

    RagRuntimeConfig config_;
    QProcess process_;
    EngineState state_ = EngineState::Idle;
    WorkerState workerState_ = WorkerState::Stopped;
    RagOperation operation_ = RagOperation::Query;
    QByteArray standardOutputBuffer_;
    QString activeRequestId_;
    QString healthRequestId_;
    bool expectedShutdown_ = false;
    StateHandler stateHandler_;
    WorkerStateHandler workerStateHandler_;
    HealthHandler healthHandler_;
    LogHandler logHandler_;
    ProgressHandler progressHandler_;
    SuccessHandler successHandler_;
    FailureHandler failureHandler_;
};
