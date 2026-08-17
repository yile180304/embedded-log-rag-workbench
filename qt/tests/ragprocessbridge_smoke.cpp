#include "runtime/ragprocessbridge.h"
#include "diagnosisviewmodel.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QProcess>
#include <QTextStream>
#include <QTimer>

int main(int argc, char *argv[])
{
    QCoreApplication application(argc, argv);
    QTextStream output(stdout);
    if (application.arguments().size() < 2) {
        output << "usage: RagProcessBridgeSmoke <project-root>\n";
        return 2;
    }

    const QString projectRoot = QFileInfo(application.arguments().at(1)).absoluteFilePath();
    const bool unexpectedExitMode = application.arguments().contains(QStringLiteral("--unexpected-exit"));
    const bool settingsHealthMode = application.arguments().contains(QStringLiteral("--settings-health"));
    RagRuntimeConfig invalidConfig{projectRoot, QDir(projectRoot).filePath(QStringLiteral("missing-python.exe"))};
    RagProcessBridge invalidBridge(invalidConfig);
    bool missingPythonDetected = false;
    invalidBridge.setFailureHandler([&](RagOperation, const QString &message, const QJsonObject &) {
        missingPythonDetected = message.contains(QStringLiteral("找不到 Python"));
    });
    invalidBridge.startQuery(QStringLiteral("CFSR 是什么？"), 1);
    if (!missingPythonDetected || invalidBridge.state() != EngineState::Failed) {
        output << "missing-python path was not reported correctly\n";
        return 3;
    }

    QString parseError;
    const QJsonObject prefixedJson = RagProcessBridge::parseJsonDocument(
        QByteArray("model log\n{\"answer\":\"ok\",\"grounded\":true,\"evidence\":[]}"),
        &parseError);
    if (prefixedJson.value(QStringLiteral("answer")).toString() != QStringLiteral("ok")
        || !parseError.isEmpty()) {
        output << "prefixed JSON was not parsed correctly\n";
        return 7;
    }
    if (!RagProcessBridge::parseJsonDocument(QByteArray("not-json"), &parseError).isEmpty()
        || parseError.isEmpty()) {
        output << "bad JSON was not rejected correctly\n";
        return 8;
    }

    RagRuntimeConfig config{
        projectRoot,
        QDir(projectRoot).filePath(QStringLiteral(".venv312/Scripts/python.exe"))};
    RagProcessBridge bridge(config);
    bool competingTasksRejected = false;
    bool validationFailureObserved = false;
    bool hardFaultValidated = false;
    bool evaluationProgressObserved = false;
    bool workerKillRequested = false;
    int healthSnapshots = 0;
    qint64 workerPid = 0;
    enum class SmokePhase { WaitingForReady, WaitingForValidationError, WaitingForDiagnosis, WaitingForProtocol,
                            WaitingForEvaluation };
    SmokePhase phase = SmokePhase::WaitingForReady;
    bridge.setLogHandler([&](const QString &message) { output << message << '\n'; output.flush(); });
    bridge.setProgressHandler([&](RagOperation operation, const QJsonObject &progress) {
        if (phase == SmokePhase::WaitingForEvaluation && operation == RagOperation::Evaluation
            && progress.value(QStringLiteral("completed")).toInt() == 1
            && progress.value(QStringLiteral("total")).toInt() == 1
            && !progress.value(QStringLiteral("current_query")).toString().isEmpty()) {
            evaluationProgressObserved = true;
        }
    });
    bridge.setFailureHandler([&](RagOperation, const QString &message, const QJsonObject &) {
        if (phase == SmokePhase::WaitingForValidationError
            && message.contains(QStringLiteral("validation_error"))
            && bridge.workerState() == WorkerState::Ready
            && bridge.workerProcessId() == workerPid) {
            validationFailureObserved = true;
            phase = SmokePhase::WaitingForDiagnosis;
            const QString hardFaultLog = QStringLiteral(
                "CFSR=0x00008200 HFSR=0x40000000 BFAR=0x2003FFF8 PC=0x080126AC LR=0xFFFFFFF9");
            bridge.startHardFaultDiagnosis(hardFaultLog, 1);
            competingTasksRejected = !bridge.startQuery(QStringLiteral("ETH_DMASR 表示什么？"), 1)
                                     && !bridge.startHardFaultDiagnosis(hardFaultLog, 1)
                                     && !bridge.startProtocolLogDiagnosis(
                                         {{QStringLiteral("log"), QStringLiteral("UDP seq=1 len=64")}}, 1)
                                     && !bridge.startReindex()
                                     && !bridge.restartWorker(bridge.config())
                                     && bridge.workerProcessId() == workerPid;
            return;
        }
        output << "FAILED: " << message << '\n';
        application.exit(4);
    });
    bridge.setWorkerStateHandler([&](WorkerState state) {
        if (settingsHealthMode) {
            return;
        }
        if (unexpectedExitMode) {
            if (state == WorkerState::Ready && !workerKillRequested) {
                workerKillRequested = true;
                workerPid = bridge.workerProcessId();
                QProcess::execute(QStringLiteral("taskkill"),
                                  {QStringLiteral("/PID"), QString::number(workerPid),
                                   QStringLiteral("/T"), QStringLiteral("/F")});
                return;
            }
            if (state == WorkerState::Failed && workerKillRequested) {
                QTimer::singleShot(0, &application, [&] {
                    const bool valid = bridge.state() == EngineState::Failed
                                       && bridge.workerState() == WorkerState::Failed;
                    output << (valid ? "WORKER_EXIT_SMOKE_OK\n" : "WORKER_EXIT_SMOKE_BAD_STATE\n");
                    output.flush();
                    application.exit(valid ? 0 : 9);
                });
            }
            return;
        }
        if (state != WorkerState::Ready || phase != SmokePhase::WaitingForReady) {
            return;
        }
        workerPid = bridge.workerProcessId();
        phase = SmokePhase::WaitingForValidationError;
        bridge.startHardFaultDiagnosis(QStringLiteral("CFSR=oops"), 1);
    });
    bridge.setHealthHandler([&](const QJsonObject &health) {
        if (!settingsHealthMode) {
            return;
        }
        ++healthSnapshots;
        const bool validSnapshot = health.value(QStringLiteral("index")).toObject()
                                       .value(QStringLiteral("chunks")).toInt() > 0
                                   && !health.value(QStringLiteral("models")).toObject()
                                           .value(QStringLiteral("embedding")).toObject()
                                           .value(QStringLiteral("loaded")).toBool();
        if (!validSnapshot) {
            output << "SETTINGS_HEALTH_BAD_SNAPSHOT\n";
            application.exit(11);
            return;
        }
        if (healthSnapshots == 1) {
            workerPid = bridge.workerProcessId();
            if (!bridge.requestHealth()) {
                output << "SETTINGS_HEALTH_REFRESH_REJECTED\n";
                application.exit(12);
            }
            return;
        }
        if (healthSnapshots == 2) {
            RagRuntimeConfig updated = bridge.config();
            updated.defaultTopK = 7;
            updated.taskTimeoutSeconds = 240;
            if (!bridge.restartWorker(updated)) {
                output << "SETTINGS_HEALTH_RESTART_REJECTED\n";
                application.exit(13);
            }
            return;
        }
        const bool validRestart = bridge.workerProcessId() != workerPid
                                  && bridge.config().defaultTopK == 7
                                  && bridge.config().taskTimeoutSeconds == 240
                                  && bridge.workerState() == WorkerState::Ready;
        output << (validRestart ? "SETTINGS_HEALTH_SMOKE_OK\n" : "SETTINGS_HEALTH_BAD_RESTART\n");
        output.flush();
        application.exit(validRestart ? 0 : 14);
    });
    bridge.setSuccessHandler([&](RagOperation operation, const QJsonObject &result) {
        if (unexpectedExitMode || settingsHealthMode) {
            output << "unexpected success in worker-exit mode\n";
            application.exit(10);
            return;
        }
        QString modelError;
        const DiagnosisViewModel model = DiagnosisViewModel::fromJson(result, &modelError);
        if (phase == SmokePhase::WaitingForDiagnosis && operation == RagOperation::HardFaultDiagnosis) {
            hardFaultValidated = modelError.isEmpty()
                                 && !model.answer.isEmpty()
                                 && !model.reportPath.isEmpty()
                                 && model.hardFault.available
                                 && model.hardFault.registers.value(QStringLiteral("CFSR")) == QStringLiteral("0x00008200")
                                 && !model.hardFault.decodedFlags.isEmpty()
                                 && competingTasksRejected
                                 && validationFailureObserved
                                 && bridge.workerProcessId() == workerPid;
            if (!hardFaultValidated) {
                output << "BRIDGE_SMOKE_BAD_HARDFAULT_JSON\n";
                application.exit(5);
                return;
            }
            phase = SmokePhase::WaitingForProtocol;
            const QJsonObject payload{
                {QStringLiteral("log"), QStringLiteral(
                     "2026-08-15T10:00:00.000Z UDP seq=40 len=64\n"
                     "2026-08-15T10:00:00.025Z UDP seq=42 len=64")},
                {QStringLiteral("profile"), QStringLiteral("udp")},
                {QStringLiteral("expected_cycle_ms"), 10.0},
                {QStringLiteral("jitter_tolerance_ms"), 2.0},
                {QStringLiteral("expected_length"), 64},
                {QStringLiteral("crc16_algorithm"), QStringLiteral("none")},
            };
            if (!bridge.startProtocolLogDiagnosis(payload, 1)) {
                output << "BRIDGE_PROTOCOL_START_FAILED\n";
                application.exit(5);
            }
            return;
        }
        if (phase == SmokePhase::WaitingForProtocol && operation == RagOperation::ProtocolLogDiagnosis) {
            const bool valid = hardFaultValidated
                               && modelError.isEmpty()
                               && model.protocolLog.available
                               && model.protocolLog.profile == QStringLiteral("udp")
                               && model.protocolLog.events.size() == 2
                               && model.protocolLog.anomalies.size() == 2
                               && !model.reportPath.isEmpty()
                               && bridge.workerProcessId() == workerPid;
            if (!valid) {
                output << "BRIDGE_SMOKE_BAD_PROTOCOL_JSON\n";
                application.exit(5);
                return;
            }
            phase = SmokePhase::WaitingForEvaluation;
            const QJsonArray samples{QJsonObject{
                {QStringLiteral("sample_id"), QStringLiteral("bridge-eval-1")},
                {QStringLiteral("query"), QStringLiteral("ETH_DMASR 的 EBS 表示什么？")},
                {QStringLiteral("expected_evidence"), QJsonArray{QJsonObject{
                     {QStringLiteral("chunk_id"), QStringLiteral("dm00031020-stm32f405-415-stm32f407-417-stm32f427-6835632e60-p1214-ethernet-dma-status-register-eth-dmasr-001")},
                 }}},
            }};
            if (!bridge.startEvaluation(samples, 5)) {
                output << "BRIDGE_EVALUATION_START_FAILED\n";
                application.exit(5);
            }
            return;
        }
        if (phase == SmokePhase::WaitingForEvaluation && operation == RagOperation::Evaluation) {
            const QJsonObject runtime = result.value(QStringLiteral("runtime_snapshot")).toObject();
            const bool valid = evaluationProgressObserved
                               && result.value(QStringLiteral("sample_count")).toInt() == 1
                               && result.value(QStringLiteral("top_k")).toInt() == 5
                               && result.value(QStringLiteral("hit_rate@5")).toDouble() == 1.0
                               && result.value(QStringLiteral("recall@5")).toDouble() == 1.0
                               && result.value(QStringLiteral("mrr")).toDouble() > 0.0
                               && runtime.value(QStringLiteral("models")).toObject()
                                      .value(QStringLiteral("embedding")).toObject()
                                      .value(QStringLiteral("loaded")).toBool()
                               && runtime.value(QStringLiteral("index")).toObject()
                                      .value(QStringLiteral("chunks")).toInt() > 0
                               && bridge.workerProcessId() == workerPid;
            output << (valid ? "BRIDGE_SMOKE_OK\n" : "BRIDGE_SMOKE_BAD_EVALUATION_JSON\n");
            application.exit(valid ? 0 : 5);
            return;
        }
        output << "BRIDGE_SMOKE_UNEXPECTED_RESULT\n";
        application.exit(5);
    });

    QTimer::singleShot(0, &application, [&] {
        bridge.startWorker();
    });
    QTimer::singleShot((unexpectedExitMode || settingsHealthMode) ? 20000 : 180000, &application, [&] {
        output << "bridge smoke timed out\n";
        application.exit(6);
    });
    return application.exec();
}
