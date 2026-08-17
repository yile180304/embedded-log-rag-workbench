#include "cases/caserecord.h"

QJsonObject CaseRecord::toExportJson() const
{
    return {
        {QStringLiteral("case_id"), id},
        {QStringLiteral("created_at"), createdAt},
        {QStringLiteral("updated_at"), updatedAt},
        {QStringLiteral("mode"), mode},
        {QStringLiteral("input"), inputText},
        {QStringLiteral("query"), query},
        {QStringLiteral("answer"), answer},
        {QStringLiteral("grounded"), grounded},
        {QStringLiteral("refusal_reason"), refusalReason},
        {QStringLiteral("report_path"), reportPath},
        {QStringLiteral("note"), note},
        {QStringLiteral("favorite"), favorite},
        {QStringLiteral("embedding"), embedding},
        {QStringLiteral("reranker"), reranker},
        {QStringLiteral("retrieval_ms"), retrievalMs},
        {QStringLiteral("result"), resultJson},
    };
}
