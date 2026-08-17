#pragma once

#include "cases/caserecord.h"
#include "evaluation/evaluationrecord.h"

#include <QSqlDatabase>
#include <QString>
#include <QVector>

class QSqlQuery;

class EvaluationStore final
{
public:
    explicit EvaluationStore(QString databasePath);
    ~EvaluationStore();

    EvaluationStore(const EvaluationStore &) = delete;
    EvaluationStore &operator=(const EvaluationStore &) = delete;

    bool initialize(QString *errorMessage = nullptr);
    bool createOrGetFromCase(const CaseRecord &record,
                             const QString &feedback,
                             EvaluationCandidate *candidate,
                             QString *errorMessage = nullptr);
    bool saveCandidate(EvaluationCandidate candidate,
                       QString *savedId = nullptr,
                       QString *errorMessage = nullptr);
    bool importCandidates(const QVector<EvaluationCandidate> &candidates,
                          int *importedCount = nullptr,
                          QString *errorMessage = nullptr);
    bool getCandidate(const QString &id,
                      EvaluationCandidate *candidate,
                      QString *errorMessage = nullptr) const;
    QVector<EvaluationCandidate> listCandidates(const QString &reviewStatus = {},
                                                QString *errorMessage = nullptr) const;
    bool approveCandidate(const EvaluationCandidate &candidate,
                          ApprovedEvaluationSample *sample,
                          QString *errorMessage = nullptr);
    bool rejectCandidate(const QString &id, QString *errorMessage = nullptr);
    QVector<ApprovedEvaluationSample> listApprovedSamples(QString *errorMessage = nullptr) const;
    bool saveRun(EvaluationRun run, QString *savedId = nullptr, QString *errorMessage = nullptr);
    QVector<EvaluationRun> listRuns(int limit = 20, QString *errorMessage = nullptr) const;

    QString databasePath() const;
    bool isReady() const;

private:
    static EvaluationCandidate candidateFromQuery(const QSqlQuery &query);
    static ApprovedEvaluationSample sampleFromQuery(const QSqlQuery &query);
    static EvaluationRun runFromQuery(const QSqlQuery &query);
    static QVector<ExpectedEvidenceRef> evidenceRefsFromResult(const QJsonObject &result);
    static QString annotationGranularity(const QVector<ExpectedEvidenceRef> &refs);
    static QString timestamp();
    void setError(QString *errorMessage, const QString &message) const;

    QString databasePath_;
    QString connectionName_;
    QSqlDatabase database_;
    bool ready_ = false;
};
