#include "runtime/runtimesettingsdialog.h"

#include <QApplication>
#include <QDir>
#include <QFileInfo>
#include <QJsonObject>
#include <QLineEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QTextStream>

int main(int argc, char *argv[])
{
    QApplication application(argc, argv);
    QTextStream output(stdout);
    if (application.arguments().size() < 2) {
        output << "usage: RuntimeSettingsDialogSmoke <project-root>\n";
        return 2;
    }
    const QString root = QFileInfo(application.arguments().at(1)).absoluteFilePath();
    const RuntimeSettings defaults = RuntimeSettings::defaults(root);
    const QJsonObject health{
        {QStringLiteral("worker"), QStringLiteral("ready")},
        {QStringLiteral("python_version"), QStringLiteral("3.12.13")},
        {QStringLiteral("index"), QJsonObject{
             {QStringLiteral("ready"), true},
             {QStringLiteral("chunks"), 5540},
             {QStringLiteral("updated_at"), QStringLiteral("2026-08-14T19:30:00+08:00")},
         }},
        {QStringLiteral("models"), QJsonObject{
             {QStringLiteral("embedding"), QJsonObject{
                  {QStringLiteral("name"), defaults.embeddingModel},
                  {QStringLiteral("loaded"), false},
              }},
             {QStringLiteral("reranker"), QJsonObject{
                  {QStringLiteral("name"), defaults.rerankerModel},
                  {QStringLiteral("loaded"), false},
              }},
         }},
        {QStringLiteral("active_request_id"), QJsonValue::Null},
    };
    RuntimeSettingsDialog dialog(defaults, defaults, health, {}, false);
    bool applied = false;
    bool refreshed = false;
    dialog.setApplyHandler([&](const RuntimeSettings &settings) {
        applied = settings.defaultTopK == 7 && settings.taskTimeoutSeconds == 240;
        return applied;
    });
    dialog.setRefreshHandler([&] {
        refreshed = true;
        return true;
    });

    auto *topK = dialog.findChild<QSpinBox *>(QStringLiteral("runtimeDefaultTopK"));
    auto *timeout = dialog.findChild<QSpinBox *>(QStringLiteral("runtimeTaskTimeout"));
    auto *apply = dialog.findChild<QPushButton *>(QStringLiteral("runtimeApplyRestart"));
    auto *refresh = dialog.findChild<QPushButton *>(QStringLiteral("runtimeRefreshHealth"));
    auto *restore = dialog.findChild<QPushButton *>(QStringLiteral("runtimeRestoreDefaults"));
    if (!topK || !timeout || !apply || !refresh || !restore) {
        output << "dialog controls missing\n";
        return 3;
    }
    topK->setValue(7);
    timeout->setValue(240);
    apply->click();
    refresh->click();
    if (!applied || !refreshed) {
        output << "dialog handlers not connected\n";
        return 4;
    }
    dialog.setTaskRunning(true);
    if (apply->isEnabled() || restore->isEnabled() || !refresh->isEnabled()) {
        output << "busy button policy failed\n";
        return 5;
    }
    output << "RUNTIME_SETTINGS_DIALOG_SMOKE_OK\n";
    return 0;
}
