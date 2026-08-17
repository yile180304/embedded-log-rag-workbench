#pragma once

#include "protocol/protocollogviewmodel.h"

#include <QWidget>

class QLabel;
class QPushButton;
class QTableWidget;
class QTextBrowser;

class ProtocolWorkOrder final : public QWidget
{
public:
    explicit ProtocolWorkOrder(QWidget *parent = nullptr);

    void showPlaceholder();
    void render(const ProtocolLogAnalysisViewModel &analysis, const QString &ragAnswer, bool grounded);
    void showError(const QString &message);
    QPushButton *reportButton() const;

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void clear();
    /// 宽度不够时按价值顺序丢列，而不是让九列一起挤成省略号。
    void updateEventColumnBudget();
    QLabel *summaryLabel_ = nullptr;
    QLabel *warningTag_ = nullptr;
    QLabel *errorTag_ = nullptr;
    QTableWidget *eventTable_ = nullptr;
    QTableWidget *anomalyTable_ = nullptr;
    QLabel *generatedQueryLabel_ = nullptr;
    QLabel *evidenceStateLabel_ = nullptr;
    QTextBrowser *ragAnswerBrowser_ = nullptr;
    QPushButton *reportButton_ = nullptr;
};
