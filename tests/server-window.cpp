#include "SettingsWindow.h"
#include <QApplication>
#include <QDialog>
#include <QProcess>
#include <QTest>
#include <QTimer>
#include <qt_windows.h>

class SettingsWindowTest : public QObject {
    Q_OBJECT
private slots:
    void hiddenStartupFirstShow() {
        QProcess child;
        child.setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments* args) {
            args->startupInfo->dwFlags |= STARTF_USESHOWWINDOW;
            args->startupInfo->wShowWindow = SW_HIDE;
        });
        child.start(QCoreApplication::applicationFilePath(), {"--hidden-probe"});
        QVERIFY(child.waitForFinished(5000)); QCOMPARE(child.exitStatus(), QProcess::NormalExit);
        QCOMPARE(child.exitCode(), 0);
    }
    void reopenHiddenAndMinimizedDialog() {
        QDialog window;
        auto nativeVisible = [&] { return IsWindowVisible(reinterpret_cast<HWND>(window.winId())) != FALSE; };
        showSettingsWindow(&window); QTRY_VERIFY(window.isVisible() && nativeVisible());
        window.close(); QVERIFY(!window.isVisible());
        showSettingsWindow(&window); QTRY_VERIFY(window.isVisible() && nativeVisible());
        window.showMinimized(); QTRY_VERIFY(window.isMinimized());
        showSettingsWindow(&window); QTRY_VERIFY(!window.isMinimized() && nativeVisible());
    }
};
int main(int argc, char** argv) {
    QApplication app(argc, argv); app.setQuitOnLastWindowClosed(false);
    if (app.arguments().contains("--hidden-probe")) {
        STARTUPINFOW startup{}; startup.cb = sizeof(startup); GetStartupInfoW(&startup);
        if (!(startup.dwFlags & STARTF_USESHOWWINDOW) || startup.wShowWindow != SW_HIDE) return 2;
        QDialog window;
        showSettingsWindow(&window);
        QTimer::singleShot(300, &app, [&] {
            app.exit(window.isVisible() && !window.isMinimized()
                && IsWindowVisible(reinterpret_cast<HWND>(window.winId())) ? 0 : 3);
        });
        return app.exec();
    }
    SettingsWindowTest test;
    return QTest::qExec(&test, argc, argv);
}
#include "server-window.moc"
