#pragma once

#include <QJsonObject>
#include <QWidget>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QPlainTextEdit;
class QPushButton;
class QSpinBox;

class ProtocolLogPanel final : public QWidget
{
public:
    explicit ProtocolLogPanel(QWidget *parent = nullptr);

    QJsonObject requestPayload(QString *errorMessage = nullptr) const;
    QString logText() const;
    void setLogText(const QString &text);
    void setTaskState(bool submitAvailable, bool taskRunning);
    QPushButton *submitButton() const;

private:
    QPlainTextEdit *logEdit_ = nullptr;
    QComboBox *profileComboBox_ = nullptr;
    QCheckBox *cycleCheckBox_ = nullptr;
    QDoubleSpinBox *cycleSpinBox_ = nullptr;
    QDoubleSpinBox *jitterSpinBox_ = nullptr;
    QCheckBox *lengthCheckBox_ = nullptr;
    QSpinBox *lengthSpinBox_ = nullptr;
    QComboBox *crcComboBox_ = nullptr;
    QPushButton *submitButton_ = nullptr;
};
