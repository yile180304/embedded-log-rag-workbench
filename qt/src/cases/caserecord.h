#pragma once

#include <QJsonObject>
#include <QString>

struct CaseRecord
{
    QString id;
    QString createdAt;
    QString updatedAt;
    QString mode;
    QString inputText;
    QString query;
    QString answer;
    bool grounded = false;
    QString refusalReason;
    QJsonObject resultJson;
    QString reportPath;
    QString note;
    bool favorite = false;
    QString embedding;
    QString reranker;
    double retrievalMs = 0.0;

    QJsonObject toExportJson() const;
};

struct CaseFilter
{
    QString keyword;
    QString mode;
    int grounded = -1;
    bool favoriteOnly = false;
};
