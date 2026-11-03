#include "RemoteClient.h"
#include <QGuiApplication>
#include <QIcon>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QImage>
#include <QTimer>
#include <QFontDatabase>
#include <QStandardPaths>

int main(int argc, char** argv) {
    QGuiApplication app(argc, argv);
    app.setOrganizationName("nanamitm"); app.setApplicationName("TVTestRemote");
    app.setWindowIcon(QIcon(QStringLiteral(":/icons/remote.png")));
    QQuickStyle::setStyle("Material");
    if (qEnvironmentVariable("QT_QPA_PLATFORM") == "offscreen") {
        const auto fontId = QFontDatabase::addApplicationFont(
            QStandardPaths::writableLocation(QStandardPaths::FontsLocation) + "/YuGothR.ttc");
        const auto families = QFontDatabase::applicationFontFamilies(fontId);
        if (!families.isEmpty()) app.setFont(QFont(families.first(), 10));
    }
    RemoteClient remote;
    remote.setActive(app.applicationState() != Qt::ApplicationHidden && app.applicationState() != Qt::ApplicationSuspended);
    QObject::connect(&app, &QGuiApplication::applicationStateChanged, &remote,
        [&remote](Qt::ApplicationState state) {
            if (state == Qt::ApplicationActive) remote.setActive(true);
            else if (state == Qt::ApplicationHidden || state == Qt::ApplicationSuspended) remote.setActive(false);
        });
    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty("remote", &remote);
    QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed, &app, [] { QCoreApplication::exit(1); }, Qt::QueuedConnection);
    engine.loadFromModule("TVTRemote", "Main");
    const auto args = app.arguments();
    const auto screenshot = args.indexOf("--screenshot");
    if (screenshot >= 0 && screenshot + 1 < args.size()) {
        QTimer::singleShot(800, &app, [&] {
            auto* window = engine.rootObjects().isEmpty() ? nullptr : qobject_cast<QQuickWindow*>(engine.rootObjects().first());
            const bool saved = window && window->grabWindow().save(args[screenshot + 1]);
            app.exit(saved ? 0 : 2);
        });
    }
    return app.exec();
}
