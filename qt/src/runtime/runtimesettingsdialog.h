#pragma once

#include "runtimesettings.h"
#include "ui/uikit.h"

#include <QDialog>
#include <QJsonObject>

#include <functional>

class QFormLayout;
class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;
class QSpinBox;

class RuntimeSettingsDialog final : public QDialog
{
public:
    using ApplyHandler = std::function<bool(const RuntimeSettings &)>;
    using RefreshHandler = std::function<bool()>;

    RuntimeSettingsDialog(const RuntimeSettings &settings,
                          const RuntimeSettings &defaults,
                          const QJsonObject &healthSnapshot,
                          const QVector<HealthIssue> &issues,
                          bool taskRunning,
                          QWidget *parent = nullptr);

    void setApplyHandler(ApplyHandler handler);
    void setRefreshHandler(RefreshHandler handler);
    void setHealthSnapshot(const QJsonObject &snapshot);
    void setIssues(const QVector<HealthIssue> &issues);
    void setTaskRunning(bool running);

private:
    QWidget *createSettingsGroup();
    QWidget *createHealthGroup();
    QWidget *createIssuesGroup();
    QWidget *createButtonRow();
    void addIssueRow(const QString &badge, const QString &text, ragui::Tone tone);
    void fitIssuesHeight();
    void setActionStatus(const QString &text, ragui::Tone tone);
    RuntimeSettings currentSettings() const;
    void populate(const RuntimeSettings &settings);
    void chooseProjectRoot();
    void choosePythonExecutable();
    void applyAndRestart();
    void restoreDefaults();
    void refreshHealth();

    RuntimeSettings defaults_;
    QLineEdit *projectRootEdit_ = nullptr;
    QLineEdit *pythonExecutableEdit_ = nullptr;
    QLineEdit *embeddingModelEdit_ = nullptr;
    QLineEdit *rerankerModelEdit_ = nullptr;
    QSpinBox *defaultTopKSpinBox_ = nullptr;
    QSpinBox *taskTimeoutSpinBox_ = nullptr;
    QLabel *workerValueLabel_ = nullptr;
    QLabel *indexStateLabel_ = nullptr;
    ragui::ElidedLabel *pythonValueLabel_ = nullptr;
    ragui::ElidedLabel *indexValueLabel_ = nullptr;
    ragui::ElidedLabel *modelsValueLabel_ = nullptr;
    ragui::ElidedLabel *rerankerStateLabel_ = nullptr;
    ragui::ElidedLabel *activeRequestValueLabel_ = nullptr;
    QLabel *actionStatusLabel_ = nullptr;
    QListWidget *issuesList_ = nullptr;
    QPushButton *applyButton_ = nullptr;
    QPushButton *restoreButton_ = nullptr;
    QPushButton *refreshButton_ = nullptr;
    ApplyHandler applyHandler_;
    RefreshHandler refreshHandler_;
};
