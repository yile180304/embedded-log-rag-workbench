#pragma once

#include "protocol/protocollogviewmodel.h"

#include <QJsonObject>
#include <QMap>
#include <QString>
#include <QStringList>
#include <QVector>

struct EvidenceViewModel
{
    int rank = 0;
    QString docId;
    QString page;
    QString section;
    QString source;
    QString chunkId;
    double rerankerScore = 0.0;
    double hybridScore = 0.0;
    double bm25Score = 0.0;
    double denseScore = 0.0;
};

struct DecodedFaultFlagViewModel
{
    QString registerName;
    int bit = 0;
    QString name;
    QString group;
    QString meaning;
};

struct HardFaultAnalysisViewModel
{
    bool available = false;
    QString rawLog;
    QMap<QString, QString> registers;
    QVector<DecodedFaultFlagViewModel> decodedFlags;
    QStringList observations;
    QStringList nextActions;
    QString generatedQuery;
};

struct DiagnosisViewModel
{
    QString query;
    QString answer;
    bool grounded = false;
    QString refusalReason;
    QVector<EvidenceViewModel> evidence;
    QString embedding;
    QString reranker;
    double retrievalMs = 0.0;
    QString reportPath;
    HardFaultAnalysisViewModel hardFault;
    ProtocolLogAnalysisViewModel protocolLog;

    static DiagnosisViewModel fromJson(const QJsonObject &object, QString *errorMessage = nullptr);
};
