#include "RemoteClient.h"
#include <QGuiApplication>
#include <QIcon>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QImage>
#include <QKeyEvent>
#include <QTimer>
#include <QFontDatabase>
#include <QStandardPaths>

namespace {
// Routes hardware volume keys to TVTest's volume while the app is in front.
// The key is consumed at the window before it reaches any QML item.
class VolumeKeyFilter : public QObject {
public:
    explicit VolumeKeyFilter(RemoteClient& remote) : m_remote(remote) {}
    bool eventFilter(QObject* watched, QEvent* event) override {
        if (event->type() == QEvent::KeyPress || event->type() == QEvent::KeyRelease) {
            const auto* key = static_cast<QKeyEvent*>(event);
            if (key->key() == Qt::Key_VolumeUp || key->key() == Qt::Key_VolumeDown || key->key() == Qt::Key_VolumeMute) {
                if (event->type() == QEvent::KeyPress) m_remote.volumeKey(key->key(), key->isAutoRepeat());
                return true;
            }
        }
        return QObject::eventFilter(watched, event);
    }
private:
    RemoteClient& m_remote;
};
}

int main(int argc, char** argv) {
#ifdef Q_OS_ANDROID
    // Qt's Android input layer leaves volume keys to the system unless this is
    // set; the app plays no audio, so the keys control TVTest instead.
    qputenv("QT_ANDROID_VOLUME_KEYS", "1");
#endif
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
    QString japaneseFontFamily;
#ifdef Q_OS_ANDROID
    // Pin Japanese glyph forms instead of relying on the device's CJK fallback.
    const auto fontId = QFontDatabase::addApplicationFont(QStringLiteral(":/fonts/NotoSansJP.ttf"));
    const auto fontFamilies = QFontDatabase::applicationFontFamilies(fontId);
    if (!fontFamilies.isEmpty()) {
        japaneseFontFamily = fontFamilies.first();
        QFont font = app.font();
        font.setFamily(japaneseFontFamily);
        font.setWeight(QFont::Normal);
        app.setFont(font);
        QFontDatabase::setApplicationFallbackFontFamilies(QChar::Script_Han, {japaneseFontFamily});
    } else qWarning("Bundled Japanese UI font could not be loaded");
#endif
    RemoteClient remote;
    remote.setActive(app.applicationState() != Qt::ApplicationHidden && app.applicationState() != Qt::ApplicationSuspended);
    QObject::connect(&app, &QGuiApplication::applicationStateChanged, &remote,
        [&remote](Qt::ApplicationState state) {
            if (state == Qt::ApplicationActive) remote.setActive(true);
            else if (state == Qt::ApplicationHidden || state == Qt::ApplicationSuspended) remote.setActive(false);
        });
    VolumeKeyFilter volumeKeys(remote);
    app.installEventFilter(&volumeKeys);
    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty("remote", &remote);
    engine.rootContext()->setContextProperty("japaneseFontFamily", japaneseFontFamily);
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
