#pragma once

#include <QJsonObject>
#include <QString>
#include <QVector>

struct ProtocolLogEventViewModel
{
    int lineNo = 0;
    QString timestamp;
    QString protocol;
    QString source;
    QString destination;
    QString sequence;
    QString length;
    QString payloadLength;
    QString intervalMs;
    QString crcState;
    QJsonObject fields;
    QString rawMessage;
};

struct ProtocolAnomalyViewModel
{
    QString type;
    QString severity;
    QString message;
    int lineNo = 0;
    QJsonObject details;
};

struct ProtocolLogAnalysisViewModel
{
    bool available = false;
    QString profile;
    int lineCount = 0;
    int eventCount = 0;
    int errorCount = 0;
    int warningCount = 0;
    QVector<ProtocolLogEventViewModel> events;
    QVector<ProtocolAnomalyViewModel> anomalies;
    QJsonObject config;
    QString generatedQuery;

    static ProtocolLogAnalysisViewModel fromJson(const QJsonObject &object, QString *errorMessage = nullptr);
};
