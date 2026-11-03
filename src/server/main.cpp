#include "Broker.h"
#include "ServerControl.h"
#include "StartupSettings.h"
#include "ServerSettings.h"
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QTextStream>
#include <memory>
#ifdef Q_OS_WIN
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QDialog>
#include <QLabel>
#include <QMenu>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QHBoxLayout>
#include <QSystemTrayIcon>
#include <QVBoxLayout>

static int runWithTray(QApplication& app, Broker& broker, ServerSettings& settings, const QString& address, const QString& pipe, bool showWindow) {
    auto endpoint = QStringLiteral("ws://%1:%2").arg(address).arg(broker.port());
    app.setQuitOnLastWindowClosed(false);
    const QIcon icon(":/icons/server.png");
    app.setWindowIcon(icon);
    QDialog status;
    status.setWindowTitle(QStringLiteral("TVTest Remote サーバー"));
    auto* layout = new QVBoxLayout(&status);
    auto* summary = new QLabel(QStringLiteral("サーバーは稼働中です\n待受: %1\nIPC: %2").arg(endpoint, pipe), &status);
    summary->setTextFormat(Qt::PlainText); layout->addWidget(summary);
    auto* portRow = new QHBoxLayout;
    auto* portLabel = new QLabel(QStringLiteral("待受ポート"), &status);
    auto* portInput = new QSpinBox(&status);
    portInput->setRange(1, 65535); portInput->setValue(broker.port());
    portInput->setAccessibleName(QStringLiteral("待受ポート")); portLabel->setBuddy(portInput);
    auto* applyPort = new QPushButton(QStringLiteral("適用"), &status);
    portRow->addWidget(portLabel); portRow->addWidget(portInput); portRow->addWidget(applyPort); layout->addLayout(portRow);
    auto* portInfo = new QLabel(QStringLiteral("変更後はアプリの接続URLも新しいポートに変更してください。"), &status);
    portInfo->setWordWrap(true); layout->addWidget(portInfo);
    QSettings registry(QStringLiteral("HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Run"), QSettings::NativeFormat);
    StartupSettings startup(registry, pipe);
    auto startupCommand = StartupSettings::makeCommand(QCoreApplication::applicationFilePath(), address, broker.port(), pipe);
    auto* autoStart = new QCheckBox(QStringLiteral("Windowsサインイン時に自動起動"), &status);
    autoStart->setChecked(startup.enabled());
    layout->addWidget(autoStart);
    auto* startupInfo = new QLabel(QStringLiteral("現在の待受IP・ポート・IPC名を保存し、通知領域へ常駐します。"), &status);
    startupInfo->setWordWrap(true);
    startupInfo->setMinimumWidth(320);
    layout->addWidget(startupInfo);
    // Keep an already-enabled registration valid when a new build is launched.
    if (startup.enabled() && startup.command() != startupCommand) {
        QString error;
        if (!startup.setEnabled(true, startupCommand, error)) startupInfo->setText(error);
    }
    QObject::connect(autoStart, &QCheckBox::toggled, &status, [&](bool enabled) {
        QString error;
        if (startup.setEnabled(enabled, startupCommand, error))
            startupInfo->setText(enabled ? QStringLiteral("サインイン時の自動起動を有効にしました。") : QStringLiteral("サインイン時の自動起動を無効にしました。"));
        else {
            const QSignalBlocker blocker(autoStart);
            autoStart->setChecked(startup.enabled()); startupInfo->setText(error);
        }
    });
    auto* stop = new QPushButton(QStringLiteral("サーバーを終了"), &status);
    layout->addWidget(stop);
    QObject::connect(stop, &QPushButton::clicked, &app, &QCoreApplication::quit);
    QMenu menu;
    auto* show = menu.addAction(QStringLiteral("設定"));
    QObject::connect(show, &QAction::triggered, &status, [&status] { status.show(); status.raise(); status.activateWindow(); });
    menu.addSeparator();
    QObject::connect(menu.addAction(QStringLiteral("終了")), &QAction::triggered, &app, &QCoreApplication::quit);
    QSystemTrayIcon tray(icon);
    tray.setToolTip(QStringLiteral("TVTest Remote サーバー\n%1").arg(endpoint));
    tray.setContextMenu(&menu);
    QObject::connect(applyPort, &QPushButton::clicked, &status, [&] {
        QString error;
        const auto previousPort = broker.port();
        if (!settings.applyPort(broker, portInput->value(), address, &startup, QCoreApplication::applicationFilePath(), error)) {
            portInfo->setText(error); return;
        }
        startupCommand = StartupSettings::makeCommand(QCoreApplication::applicationFilePath(), address, broker.port(), pipe);
        endpoint = QStringLiteral("ws://%1:%2").arg(address).arg(broker.port());
        summary->setText(QStringLiteral("サーバーは稼働中です\n待受: %1\nIPC: %2").arg(endpoint, pipe));
        tray.setToolTip(QStringLiteral("TVTest Remote サーバー\n%1").arg(endpoint));
        portInfo->setText(previousPort == broker.port() ? QStringLiteral("待受ポートを保存しました。")
            : QStringLiteral("待受ポートを変更・保存しました。アプリの接続URLも %1 に変更してください。").arg(endpoint));
    });
    QObject::connect(&tray, &QSystemTrayIcon::activated, &status, [&status](QSystemTrayIcon::ActivationReason reason) {
        if (reason == QSystemTrayIcon::DoubleClick) { status.show(); status.raise(); status.activateWindow(); }
    });
    if (QSystemTrayIcon::isSystemTrayAvailable()) {
        tray.show();
        if (showWindow) status.show();
    }
    else {
        QObject::connect(&status, &QDialog::finished, &app, &QCoreApplication::quit);
        status.show();
    }
    return app.exec();
}
#endif

int main(int argc, char** argv) {
    bool useTray = false;
#ifdef Q_OS_WIN
    useTray = true;
    for (int i = 1; i < argc; ++i) {
        const QByteArray arg(argv[i]);
        if (arg == "--no-tray" || arg == "--stop" || arg == "--help" || arg == "--help-all"
            || arg == "-h" || arg == "--version" || arg == "-v") useTray = false;
    }
#endif
    std::unique_ptr<QCoreApplication> application;
#ifdef Q_OS_WIN
    if (useTray) application = std::make_unique<QApplication>(argc, argv);
    else
#endif
        application = std::make_unique<QCoreApplication>(argc, argv);
    auto& app = *application;
    app.setApplicationName("tvt-remote-server"); app.setApplicationVersion(REMOTE_VERSION);
    QCommandLineParser parser; parser.addHelpOption(); parser.addVersionOption();
    parser.addOptions({{{"a", "address"}, "Listen IP", "address", "0.0.0.0"},
        {{"p", "port"}, "WebSocket port (overrides saved setting; default 18765)", "port"},
        {"pipe", "Local IPC name", "name", Remote::PipeName},
        {"no-tray", "Run without a notification-area icon"},
        {"show-window", "Show the server status window at startup"},
        {"stop", "Stop the running server for this local IPC name"}});
    if (!parser.parse(app.arguments())) { QTextStream(stderr) << parser.errorText() << '\n'; return 1; }
    if (parser.isSet("help") || parser.isSet("help-all")) { QTextStream(stdout) << parser.helpText(); return 0; }
    if (parser.isSet("version")) { QTextStream(stdout) << app.applicationVersion() << '\n'; return 0; }
    auto fail = [](const QString& error) { QTextStream(stderr) << error << '\n'; return 1; };
    if (parser.isSet("stop")) {
        QString error;
        if (!ServerControl::requestStop(parser.value("pipe"), error)) return fail(error);
        QTextStream(stdout) << "Server shutdown requested" << Qt::endl; return 0;
    }
    QHostAddress address;
    QSettings preferences("nanamitm", "TVTestRemoteServer");
    ServerSettings settings(preferences, parser.value("pipe"));
    bool ok = true; const auto port = parser.isSet("port") ? parser.value("port").toUInt(&ok) : settings.port();
    if (!address.setAddress(parser.value("address")) || !ok || port > 65535) return fail("Invalid address/port");
    ServerControl control;
    Broker broker;
    if (!broker.start(address, quint16(port), parser.value("pipe"))) return fail(broker.errorString());
    if (!control.start(parser.value("pipe"), [&app] { app.quit(); })) return fail("Local control: " + control.errorString());
    QTextStream(stdout) << "WS " << address.toString() << ':' << broker.port()
        << " | IPC " << parser.value("pipe") << Qt::endl;
#ifdef Q_OS_WIN
    if (useTray) return runWithTray(static_cast<QApplication&>(app), broker, settings,
        address.toString(), parser.value("pipe"), parser.isSet("show-window"));
#endif
    return app.exec();
}
