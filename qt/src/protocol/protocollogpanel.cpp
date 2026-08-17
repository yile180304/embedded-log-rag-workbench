#include "protocol/protocollogpanel.h"

#include "ui/uikit.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

ProtocolLogPanel::ProtocolLogPanel(QWidget *parent)
    : QWidget(parent)
{
    const ragui::RagMetrics metrics;
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(metrics.space2);

    layout->addWidget(ragui::makeSectionTitle(
        QStringLiteral("Protocol Log"),
        QStringLiteral("离线 UDP / TRDP / 通用日志；不读取 PCAP 或实时设备")));

    logEdit_ = new QPlainTextEdit;
    logEdit_->setObjectName(QStringLiteral("protocolLog"));
    logEdit_->setPlaceholderText(QStringLiteral(
        "2026-08-15T10:00:00.000Z UDP src=10.0.0.1:1000 dst=10.0.0.2:2000 seq=40 len=64\n"
        "2026-08-15T10:00:00.025Z UDP src=10.0.0.1:1000 dst=10.0.0.2:2000 seq=42 len=64"));
    logEdit_->setMinimumHeight(50);
    logEdit_->setMaximumHeight(96);
    layout->addWidget(logEdit_);

    auto *controls = new QWidget;
    auto *row = new QHBoxLayout(controls);
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(metrics.space2);

    // 下面这些控件以前是 setFixedWidth：中文文案一变（"自动识别" → "自动识别 Profile"）
    // 就会把字挤掉，改成最小宽度后只会撑开，不会截断。
    profileComboBox_ = new QComboBox;
    profileComboBox_->setObjectName(QStringLiteral("protocolProfile"));
    profileComboBox_->addItem(QStringLiteral("自动识别"), QStringLiteral("auto"));
    profileComboBox_->addItem(QStringLiteral("通用日志"), QStringLiteral("generic"));
    profileComboBox_->addItem(QStringLiteral("UDP"), QStringLiteral("udp"));
    profileComboBox_->addItem(QStringLiteral("TRDP 显式字段"), QStringLiteral("trdp"));
    profileComboBox_->setMinimumWidth(112);

    cycleCheckBox_ = new QCheckBox(QStringLiteral("校验周期"));
    cycleCheckBox_->setObjectName(QStringLiteral("protocolCycleEnabled"));
    cycleSpinBox_ = new QDoubleSpinBox;
    cycleSpinBox_->setObjectName(QStringLiteral("protocolExpectedCycle"));
    cycleSpinBox_->setRange(0.001, 60000.0);
    cycleSpinBox_->setDecimals(3);
    cycleSpinBox_->setValue(10.0);
    cycleSpinBox_->setSuffix(QStringLiteral(" ms"));
    cycleSpinBox_->setMinimumWidth(104);
    jitterSpinBox_ = new QDoubleSpinBox;
    jitterSpinBox_->setObjectName(QStringLiteral("protocolJitterTolerance"));
    jitterSpinBox_->setRange(0.0, 60000.0);
    jitterSpinBox_->setDecimals(3);
    jitterSpinBox_->setValue(2.0);
    jitterSpinBox_->setSuffix(QStringLiteral(" ms"));
    jitterSpinBox_->setMinimumWidth(104);
    jitterSpinBox_->setToolTip(QStringLiteral("周期抖动容差"));

    lengthCheckBox_ = new QCheckBox(QStringLiteral("校验长度"));
    lengthCheckBox_->setObjectName(QStringLiteral("protocolLengthEnabled"));
    lengthSpinBox_ = new QSpinBox;
    lengthSpinBox_->setObjectName(QStringLiteral("protocolExpectedLength"));
    lengthSpinBox_->setRange(0, 65535);
    lengthSpinBox_->setValue(64);
    lengthSpinBox_->setSuffix(QStringLiteral(" bytes"));
    lengthSpinBox_->setMinimumWidth(110);

    crcComboBox_ = new QComboBox;
    crcComboBox_->setObjectName(QStringLiteral("protocolCrcAlgorithm"));
    crcComboBox_->addItem(QStringLiteral("不校验 CRC"), QStringLiteral("none"));
    crcComboBox_->addItem(QStringLiteral("CRC16 MODBUS"), QStringLiteral("modbus"));
    crcComboBox_->addItem(QStringLiteral("CRC16 CCITT-FALSE"), QStringLiteral("ccitt_false"));
    crcComboBox_->setMinimumWidth(146);
    crcComboBox_->setToolTip(QStringLiteral("Frame Integrity Check：只校验显式 FRAME/CRC，不猜帧尾格式"));

    // 主操作按钮由 DiagnosisWorkspacePage 收进页面工具条（三个模式共用同一个位置），
    // 这里只负责创建、命名和 setTaskState；先挂在面板下并隐藏，避免未被接管时乱入布局。
    submitButton_ = new QPushButton(QStringLiteral("分析协议日志"), this);
    submitButton_->setObjectName(QStringLiteral("protocolLogButton"));
    submitButton_->setProperty("variant", QStringLiteral("primary"));
    submitButton_->setMinimumWidth(112);
    submitButton_->hide();

    auto *profileLabel = new QLabel(QStringLiteral("Profile"));
    profileLabel->setObjectName(QStringLiteral("mutedText"));
    auto *jitterLabel = new QLabel(QStringLiteral("±"));
    jitterLabel->setObjectName(QStringLiteral("mutedText"));
    auto *crcLabel = new QLabel(QStringLiteral("CRC16"));
    crcLabel->setObjectName(QStringLiteral("mutedText"));

    row->addWidget(profileLabel);
    row->addWidget(profileComboBox_);
    row->addWidget(ragui::makeVDivider());
    row->addWidget(cycleCheckBox_);
    row->addWidget(cycleSpinBox_);
    row->addWidget(jitterLabel);
    row->addWidget(jitterSpinBox_);
    row->addWidget(ragui::makeVDivider());
    row->addWidget(lengthCheckBox_);
    row->addWidget(lengthSpinBox_);
    row->addWidget(ragui::makeVDivider());
    row->addWidget(crcLabel);
    row->addWidget(crcComboBox_);
    row->addStretch(1);
    layout->addWidget(controls);

    connect(cycleCheckBox_, &QCheckBox::toggled, this, [this](bool enabled) {
        cycleSpinBox_->setEnabled(enabled);
        jitterSpinBox_->setEnabled(enabled);
    });
    connect(lengthCheckBox_, &QCheckBox::toggled, lengthSpinBox_, &QSpinBox::setEnabled);
    cycleSpinBox_->setEnabled(false);
    jitterSpinBox_->setEnabled(false);
    lengthSpinBox_->setEnabled(false);
}

QJsonObject ProtocolLogPanel::requestPayload(QString *errorMessage) const
{
    const QString log = logEdit_->toPlainText().trimmed();
    if (log.isEmpty()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("协议日志为空，请粘贴离线日志文本。");
        }
        return {};
    }
    QJsonObject payload{
        {QStringLiteral("log"), log},
        {QStringLiteral("profile"), profileComboBox_->currentData().toString()},
        {QStringLiteral("expected_cycle_ms"), QJsonValue::Null},
        {QStringLiteral("jitter_tolerance_ms"), QJsonValue::Null},
        {QStringLiteral("expected_length"), QJsonValue::Null},
        {QStringLiteral("crc16_algorithm"), crcComboBox_->currentData().toString()},
    };
    if (cycleCheckBox_->isChecked()) {
        payload.insert(QStringLiteral("expected_cycle_ms"), cycleSpinBox_->value());
        payload.insert(QStringLiteral("jitter_tolerance_ms"), jitterSpinBox_->value());
    }
    if (lengthCheckBox_->isChecked()) {
        payload.insert(QStringLiteral("expected_length"), lengthSpinBox_->value());
    }
    if (errorMessage) {
        errorMessage->clear();
    }
    return payload;
}

QString ProtocolLogPanel::logText() const
{
    return logEdit_->toPlainText();
}

void ProtocolLogPanel::setLogText(const QString &text)
{
    logEdit_->setPlainText(text);
}

void ProtocolLogPanel::setTaskState(bool submitAvailable, bool taskRunning)
{
    submitButton_->setEnabled(submitAvailable);
    logEdit_->setEnabled(!taskRunning);
    profileComboBox_->setEnabled(!taskRunning);
    cycleCheckBox_->setEnabled(!taskRunning);
    cycleSpinBox_->setEnabled(!taskRunning && cycleCheckBox_->isChecked());
    jitterSpinBox_->setEnabled(!taskRunning && cycleCheckBox_->isChecked());
    lengthCheckBox_->setEnabled(!taskRunning);
    lengthSpinBox_->setEnabled(!taskRunning && lengthCheckBox_->isChecked());
    crcComboBox_->setEnabled(!taskRunning);
}

QPushButton *ProtocolLogPanel::submitButton() const
{
    return submitButton_;
}
