#include "diagnosticworkbench.h"
#include "runtime/runtimesettingsdialog.h"

#include <QApplication>
#include <QComboBox>
#include <QCheckBox>
#include <QDir>
#include <QFont>
#include <QImage>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QStyleFactory>
#include <QTimer>

namespace {
bool saveWidgetScreenshot(QWidget &widget, const QString &path)
{
    QImage screenshot(widget.size(), QImage::Format_ARGB32_Premultiplied);
    screenshot.fill(Qt::white);
    widget.render(&screenshot);
    return screenshot.save(path, "PNG");
}
} // namespace

int main(int argc, char *argv[])
{
    QApplication application(argc, argv);
    application.setApplicationName(QStringLiteral("STM32F4 RAG Diagnostic Workbench"));
    application.setOrganizationName(QStringLiteral("Embedded Intelligence Lab"));
    application.setStyle(QStyleFactory::create(QStringLiteral("Fusion")));

    QFont font = application.font();
    font.setFamilies({QStringLiteral("Microsoft YaHei UI"), QStringLiteral("Segoe UI")});
    font.setPointSize(10);
    application.setFont(font);

    DiagnosticWorkbench window;
    const QStringList arguments = application.arguments();
    const qsizetype viewportOption = arguments.indexOf(QStringLiteral("--viewport"));
    if (viewportOption >= 0 && viewportOption + 1 < arguments.size()) {
        const QRegularExpressionMatch match = QRegularExpression(QStringLiteral("^(\\d+)x(\\d+)$"))
                                                  .match(arguments.at(viewportOption + 1));
        if (match.hasMatch()) {
            window.resize(match.captured(1).toInt(), match.captured(2).toInt());
        }
    }
    window.show();
    if (arguments.contains(QStringLiteral("--hardfault"))) {
        if (auto *modeSelector = window.findChild<QComboBox *>(QStringLiteral("modeSelector"))) {
            modeSelector->setCurrentIndex(1);
        }
    }
    if (arguments.contains(QStringLiteral("--protocol"))) {
        if (auto *modeSelector = window.findChild<QComboBox *>(QStringLiteral("modeSelector"))) {
            modeSelector->setCurrentIndex(2);
        }
    }
    const bool autoHardFault = arguments.contains(QStringLiteral("--auto-hardfault"));
    const bool autoQuery = arguments.contains(QStringLiteral("--auto-query"));
    const bool autoProtocol = arguments.contains(QStringLiteral("--auto-protocol"));
    if (autoHardFault) {
        if (auto *logEdit = window.findChild<QPlainTextEdit *>(QStringLiteral("hardFaultLog"))) {
            logEdit->setPlainText(QStringLiteral(
                "HardFault\nCFSR=0x00008200\nHFSR=0x40000000\nBFAR=0x2003FFF8\nPC=0x080126AC\nLR=0xFFFFFFF9"));
        }
        if (auto *button = window.findChild<QPushButton *>(QStringLiteral("hardFaultButton"))) {
            auto *readyTimer = new QTimer(&window);
            readyTimer->setInterval(250);
            QObject::connect(readyTimer, &QTimer::timeout, &window, [button, readyTimer] {
                if (button->isEnabled()) {
                    readyTimer->stop();
                    button->click();
                }
            });
            readyTimer->start();
        }
    }
    if (autoQuery) {
        if (auto *modeSelector = window.findChild<QComboBox *>(QStringLiteral("modeSelector"))) {
            modeSelector->setCurrentIndex(0);
        }
        if (auto *questionEdit = window.findChild<QPlainTextEdit *>(QStringLiteral("expertQuestion"))) {
            questionEdit->setPlainText(QStringLiteral("STM32F4 的 ETH_DMASR.EBS 置位时应该如何排查？"));
        }
        if (auto *button = window.findChild<QPushButton *>(QStringLiteral("primaryButton"))) {
            auto *readyTimer = new QTimer(&window);
            readyTimer->setInterval(250);
            QObject::connect(readyTimer, &QTimer::timeout, &window, [button, readyTimer] {
                if (button->isEnabled()) {
                    readyTimer->stop();
                    button->click();
                }
            });
            readyTimer->start();
        }
    }
    if (autoProtocol) {
        if (auto *modeSelector = window.findChild<QComboBox *>(QStringLiteral("modeSelector"))) {
            modeSelector->setCurrentIndex(2);
        }
        if (auto *profile = window.findChild<QComboBox *>(QStringLiteral("protocolProfile"))) {
            profile->setCurrentIndex(profile->findData(QStringLiteral("udp")));
        }
        if (auto *cycle = window.findChild<QCheckBox *>(QStringLiteral("protocolCycleEnabled"))) {
            cycle->setChecked(true);
        }
        if (auto *length = window.findChild<QCheckBox *>(QStringLiteral("protocolLengthEnabled"))) {
            length->setChecked(true);
        }
        if (auto *logEdit = window.findChild<QPlainTextEdit *>(QStringLiteral("protocolLog"))) {
            logEdit->setPlainText(QStringLiteral(
                "2026-08-15T10:00:00.000Z UDP src=10.0.0.1:1000 dst=10.0.0.2:2000 seq=40 len=64\n"
                "2026-08-15T10:00:00.025Z UDP src=10.0.0.1:1000 dst=10.0.0.2:2000 seq=42 len=64"));
        }
        if (auto *button = window.findChild<QPushButton *>(QStringLiteral("protocolLogButton"))) {
            auto *readyTimer = new QTimer(&window);
            readyTimer->setInterval(250);
            QObject::connect(readyTimer, &QTimer::timeout, &window, [button, readyTimer] {
                if (button->isEnabled()) {
                    readyTimer->stop();
                    button->click();
                }
            });
            readyTimer->start();
        }
    }
    const bool openCases = arguments.contains(QStringLiteral("--cases"));
    if (openCases) {
        QTimer::singleShot(800, &window, [&window] {
            const QList<QPushButton *> buttons = window.findChildren<QPushButton *>();
            for (QPushButton *button : buttons) {
                if (button->property("testId").toString() == QStringLiteral("caseLibraryNavigation")) {
                    button->click();
                    return;
                }
            }
        });
    }
    const bool openKnowledge = arguments.contains(QStringLiteral("--knowledge"));
    if (openKnowledge) {
        QTimer::singleShot(800, &window, [&window] {
            const QList<QPushButton *> buttons = window.findChildren<QPushButton *>();
            for (QPushButton *button : buttons) {
                if (button->property("testId").toString() == QStringLiteral("knowledgeNavigation")) {
                    button->click();
                    return;
                }
            }
        });
    }
    const bool openEvaluation = arguments.contains(QStringLiteral("--evaluation"));
    if (openEvaluation) {
        QTimer::singleShot(800, &window, [&window] {
            const QList<QPushButton *> buttons = window.findChildren<QPushButton *>();
            for (QPushButton *button : buttons) {
                if (button->property("testId").toString() == QStringLiteral("evaluationNavigation")) {
                    button->click();
                    return;
                }
            }
        });
    }
    const bool openSettings = arguments.contains(QStringLiteral("--settings"));
    if (openSettings) {
        QTimer::singleShot(800, &window, [&window] {
            const QList<QPushButton *> buttons = window.findChildren<QPushButton *>();
            for (QPushButton *button : buttons) {
                if (button->property("testId").toString() == QStringLiteral("runtimeSettingsButton")) {
                    button->click();
                    return;
                }
            }
        });
    }    const qsizetype screenshotOption = arguments.indexOf(QStringLiteral("--screenshot"));
    if (screenshotOption >= 0 && screenshotOption + 1 < arguments.size()) {
        const QString screenshotPath = QDir::cleanPath(arguments.at(screenshotOption + 1));
        if (openSettings) {
            QTimer::singleShot(1600, &window, [&application, &window, screenshotPath] {
                RuntimeSettingsDialog *settingsDialog = nullptr;
                for (QWidget *widget : QApplication::topLevelWidgets()) {
                    settingsDialog = dynamic_cast<RuntimeSettingsDialog *>(widget);
                    if (settingsDialog) {
                        break;
                    }
                }
                if (!settingsDialog) {
                    application.exit(4);
                    return;
                }
                QImage screenshot(window.size(), QImage::Format_ARGB32_Premultiplied);
                screenshot.fill(Qt::white);
                window.render(&screenshot);
                const QPoint dialogPosition(
                    qMax(0, (window.width() - settingsDialog->width()) / 2),
                    qMax(0, (window.height() - settingsDialog->height()) / 2));
                settingsDialog->render(&screenshot, dialogPosition);
                screenshot.save(screenshotPath, "PNG");
                application.quit();
            });
        } else if (autoHardFault || autoQuery || autoProtocol) {
            auto *completionTimer = new QTimer(&window);
            completionTimer->setInterval(500);
            QObject::connect(completionTimer, &QTimer::timeout, &window,
                             [&application, &window, screenshotPath, completionTimer, openCases] {
                const QList<QLabel *> labels = window.findChildren<QLabel *>();
                for (const QLabel *label : labels) {
                    // 读机器可读的 engineState 属性，不匹配界面文案。
                    const QString engineState = label->property("engineState").toString();
                    if (engineState == QStringLiteral("Succeeded")) {
                        completionTimer->stop();
                        if (!openCases) {
                            saveWidgetScreenshot(window, screenshotPath);
                            application.quit();
                            return;
                        }
                        const QList<QPushButton *> buttons = window.findChildren<QPushButton *>();
                        QPushButton *caseNavigation = nullptr;
                        for (QPushButton *button : buttons) {
                            if (button->property("testId").toString() == QStringLiteral("caseLibraryNavigation")) {
                                caseNavigation = button;
                                break;
                            }
                        }
                        if (!caseNavigation) {
                            application.exit(4);
                            return;
                        }
                        caseNavigation->click();
                        QTimer::singleShot(250, &window, [&application, &window, screenshotPath] {
                            saveWidgetScreenshot(window, screenshotPath);
                            application.quit();
                        });
                        return;
                    }
                    if (engineState == QStringLiteral("Failed")) {
                        completionTimer->stop();
                        application.exit(2);
                        return;
                    }
                }
            });
            completionTimer->start();
            QTimer::singleShot(180000, &window, [&application] { application.exit(3); });
        } else {
            QTimer::singleShot(openCases || openKnowledge || openEvaluation ? 1200 : 750,
                               &window, [&application, &window, screenshotPath] {
                saveWidgetScreenshot(window, screenshotPath);
                application.quit();
            });
        }
    }
    return application.exec();
}
