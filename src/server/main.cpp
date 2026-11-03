#include "Broker.h"
#include "ServerControl.h"
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QTextStream>
#include <memory>
#ifdef Q_OS_WIN
#include <QAction>
#include <QApplication>
#include <QDialog>
#include <QLabel>
#include <QMenu>
#include <QPushButton>
#include <QSystemTrayIcon>
#include <QVBoxLayout>

static int runWithTray(QApplication& app, const QString& endpoint, const QString& pipe, bool showWindow) {
    app.setQuitOnLastWindowClosed(false);
    const QIcon icon(":/icons/server.png");
    app.setWindowIcon(icon);
    QDialog status;
    status.setWindowTitle(QStringLiteral("TVTest Remote サーバー"));
    auto* layout = new QVBoxLayout(&status);
    layout->addWidget(new QLabel(QStringLiteral("サーバーは稼働中です\n待受: %1\nIPC: %2").arg(endpoint, pipe), &status));
    auto* stop = new QPushButton(QStringLiteral("サーバーを終了"), &status);
    layout->addWidget(stop);
    QObject::connect(stop, &QPushButton::clicked, &app, &QCoreApplication::quit);
    QMenu menu;
    auto* show = menu.addAction(QStringLiteral("状態を表示"));
    QObject::connect(show, &QAction::triggered, &status, [&status] { status.show(); status.raise(); status.activateWindow(); });
    menu.addSeparator();
    QObject::connect(menu.addAction(QStringLiteral("終了")), &QAction::triggered, &app, &QCoreApplication::quit);
    QSystemTrayIcon tray(icon);
    tray.setToolTip(QStringLiteral("TVTest Remote サーバー\n%1").arg(endpoint));
    tray.setContextMenu(&menu);
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
        {{"p", "port"}, "WebSocket port", "port", "18765"},
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
    bool ok = false; const auto port = parser.value("port").toUInt(&ok);
    if (!address.setAddress(parser.value("address")) || !ok || port > 65535) return fail("Invalid address/port");
    ServerControl control;
    Broker broker;
    if (!broker.start(address, quint16(port), parser.value("pipe"))) return fail(broker.errorString());
    if (!control.start(parser.value("pipe"), [&app] { app.quit(); })) return fail("Local control: " + control.errorString());
    QTextStream(stdout) << "WS " << address.toString() << ':' << broker.port()
        << " | IPC " << parser.value("pipe") << Qt::endl;
#ifdef Q_OS_WIN
    if (useTray) return runWithTray(static_cast<QApplication&>(app),
        QStringLiteral("ws://%1:%2").arg(address.toString()).arg(broker.port()), parser.value("pipe"), parser.isSet("show-window"));
#endif
    return app.exec();
}
