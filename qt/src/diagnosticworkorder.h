#pragma once

#include <QWidget>

#include "diagnosisviewmodel.h"

class QLabel;
class QTableWidget;
class QTextBrowser;
class QPushButton;

class DiagnosticWorkOrder final : public QWidget
{
public:
    explicit DiagnosticWorkOrder(QWidget *parent = nullptr);

    void clear();
    void showPlaceholder();
    void render(const HardFaultAnalysisViewModel &analysis, const QString &ragAnswer, bool grounded);
    void showError(const QString &message);
    QPushButton *reportButton() const;

private:
    QTableWidget *registerTable_ = nullptr;
    QTableWidget *flagTable_ = nullptr;
    QTextBrowser *observationBrowser_ = nullptr;
    QTextBrowser *actionBrowser_ = nullptr;
    QLabel *generatedQueryLabel_ = nullptr;
    QLabel *evidenceStateLabel_ = nullptr;
    QTextBrowser *ragAnswerBrowser_ = nullptr;
    QPushButton *reportButton_ = nullptr;
};
